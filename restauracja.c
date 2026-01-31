#define _POSIX_C_SOURCE 200809L

#ifndef CLIENTS_TO_CREATE
#define CLIENTS_TO_CREATE 10
#endif

#include "restauracja.h" /* includes common.h */

/* Additional system headers not included via common.h */
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>

extern int current_log_level;

/* Pomocnik: zwraca upływ sekund od czasu `start` według CLOCK_MONOTONIC. */
static inline long elapsed_seconds_since(const struct timespec *start)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return now.tv_sec - start->tv_sec;
}

// ====== ZMIENNE GLOBALNE ======

/* Per-run context to reduce scattered globals. */
struct RestauracjaCtx
{
    char arg_shm[32];
    char arg_sem[32];
    char arg_msgq[32];
    pid_t children_pgid;
    volatile sig_atomic_t sigint_requested;
    volatile sig_atomic_t shutdown_signal;
};

static struct RestauracjaCtx ctx_storage = {.children_pgid = -1,
                                            .sigint_requested = 0,
                                            .shutdown_signal = 0};
static struct RestauracjaCtx *ctx = &ctx_storage;

// Wszystkie procesy potomne (obsluga/kucharz/kierownik/klienci) wrzucamy do
// jednej grupy, żeby móc zakończyć je jednym sygnałem: kill(-pgid,
// SIGTERM/SIGKILL).
/* moved into `ctx` */

// ====== HANDLERY SYGNAŁÓW ======

static void zamknij_odziedziczone_fd_przed_exec(void)
{
    // Po uruchomieniu proces może dziedziczyć dodatkowe FD
    // zamykamy wszystko poza stdin/stdout/stderr.
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd <= 0)
        max_fd = 1024;

    for (int fd = 3; fd < (int)max_fd; fd++)
        (void)close(fd);
}

static const char *nazwa_sygnalu(int signo) // zwraca nazwę sygnału na podstawie jego numeru
{
    switch (signo)
    {
    case SIGINT:
        return "SIGINT";
    case SIGQUIT:
        return "SIGQUIT";
    default:
        return "(nieznany)";
    }
}

static void restauracja_on_sigint(int signo) // handler dla SIGINT/SIGQUIT
{
    (void)signo;
    ctx->sigint_requested = 1;
    ctx->shutdown_signal = signo;
    if (ctx->children_pgid > 0)
        (void)kill(-ctx->children_pgid, SIGTERM);
}

static void restauracja_on_sigtstp(int signo) // handler dla SIGTSTP
{
    (void)signo;
    if (ctx->children_pgid > 0)
        (void)kill(-ctx->children_pgid, SIGTSTP);

    (void)kill(getpid(), SIGSTOP);
}

static void restauracja_on_sigcont(int signo) // handler dla SIGCONT
{
    (void)signo;
    if (ctx->children_pgid > 0)
        (void)kill(-ctx->children_pgid, SIGCONT);
}

// ====== ZARZĄDZANIE PROCESAMI ======

static void
zbierz_zombie_nieblokujaco(int *status) // zbiera zakończone procesy potomne
{
    for (;;)
    {
        pid_t p = waitpid(-1, status, WNOHANG); // nieblokująco
        if (p <= 0)
            break;
        LOGD(
            "restauracja: zbierz_zombie_nieblokujaco: pid=%d reaped=%d status=%d\n",
            (int)getpid(), (int)p, (status ? *status : -1));
    }
}

static int zbierz_zombie_nieblokujaco_licznik(
    int *status) // zbiera zakończone procesy i zwraca liczbę klientów
{
    int reaped = 0;
    for (;;)
    {
        pid_t p = waitpid(-1, status, WNOHANG); // nieblokująco
        if (p <= 0)
            break;
        LOGD("restauracja: zbierz_zombie_nieblokujaco_licznik: pid=%d reaped=%d\n",
             (int)getpid(), (int)p);
        if (p != common_ctx->pid_obsluga && p != common_ctx->pid_kucharz &&
            p != common_ctx->pid_kierownik)
            reaped++;
    }
    return reaped;
}

static pid_t uruchom_potomka_exec(
    const char *file, const char *argv0, int numer_grupy,
    int is_klient) // uruchamia proces potomny przez fork()+exec()
{
    pid_t pid = fork();
    if (pid == 0)
    {
        zamknij_odziedziczone_fd_przed_exec();
        if (is_klient)
        {
            char arg_grupa[32];
            snprintf(arg_grupa, sizeof(arg_grupa), "%d", numer_grupy);
            execl(file, argv0, ctx->arg_shm, ctx->arg_sem, ctx->arg_msgq, arg_grupa,
                  (char *)NULL);
        }
        else
        {
            execl(file, argv0, ctx->arg_shm, ctx->arg_sem, ctx->arg_msgq,
                  (char *)NULL);
        }
        /* fallthrough handled above; no extra execl here */
        LOGE_ERRNO("execl");
        _exit(127);
    }
    if (pid < 0)
    {
        LOGE_ERRNO("fork");
        return -1;
    }
    return pid;
}

/* Helper: launch child via uruchom_potomka_exec(), optionally register its PID
 * into shared memory slot `out_shm_pid`, and ensure it's placed into the
 * children process group. If `make_group_if_missing` is non-zero and
 * `children_pgid` is not yet set, the launched pid becomes the group leader.
 */
static pid_t launch_child_and_set_group(const char *file, const char *argv0,
                                        int numer_grupy, int is_klient,
                                        pid_t *out_shm_pid,
                                        int make_group_if_missing)
{
    pid_t pid = uruchom_potomka_exec(file, argv0, numer_grupy, is_klient);
    if (pid < 0)
        return -1;

    if (out_shm_pid)
        *out_shm_pid = pid;

    if (make_group_if_missing && ctx->children_pgid <= 0)
    {
        ctx->children_pgid = pid;
        (void)setpgid(pid, ctx->children_pgid);
    }
    else if (ctx->children_pgid > 0)
    {
        (void)setpgid(pid, ctx->children_pgid);
    }
    return pid;
}

static int czy_grupa_procesow_pusta(
    pid_t pgid) // sprawdza, czy grupa procesów o danym pgid jest pusta
{
    if (pgid <= 0)
        return 1;
    if (kill(-pgid, 0) == -1 && errno == ESRCH)
        return 1;
    return 0;
}

static void zakoncz_wszystkie_dzieci(
    int *status) // kończy wszystkie procesy potomne w grupie
{
    /* Keep total wait time below typical test timeout (10s). */
    const int timeout_term = SHUTDOWN_TERM_TIMEOUT;
    const int timeout_kill = SHUTDOWN_KILL_TIMEOUT;

    LOGD("zakoncz_wszystkie_dzieci: pid=%d starting, children_pgid=%d\n",
         (int)getpid(), (int)ctx->children_pgid);
    if (ctx->children_pgid > 0)
    {
        LOGD("zakoncz_wszystkie_dzieci: pid=%d sending SIGTERM to -%d\n",
             (int)getpid(), (int)ctx->children_pgid);
        kill(-ctx->children_pgid, SIGTERM);
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (!czy_grupa_procesow_pusta(ctx->children_pgid) &&
           elapsed_seconds_since(&start) < timeout_term)
    {
        LOGD("zakoncz_wszystkie_dzieci: pid=%d waiting for children, elapsed=%ld\n",
             (int)getpid(), (long)elapsed_seconds_since(&start));
        zbierz_zombie_nieblokujaco(status);
        sched_yield();
    }

    if (!czy_grupa_procesow_pusta(ctx->children_pgid))
    {
        if (ctx->children_pgid > 0)
        {
            LOGD("zakoncz_wszystkie_dzieci: pid=%d sending SIGKILL to -%d\n",
                 (int)getpid(), (int)ctx->children_pgid);
            kill(-ctx->children_pgid, SIGKILL);
        }

        clock_gettime(CLOCK_MONOTONIC, &start);
        while (!czy_grupa_procesow_pusta(ctx->children_pgid) &&
               elapsed_seconds_since(&start) < timeout_kill)
        {
            LOGD("zakoncz_wszystkie_dzieci: pid=%d waiting after SIGKILL, "
                 "elapsed=%ld\n",
                 (int)getpid(), (long)elapsed_seconds_since(&start));
            zbierz_zombie_nieblokujaco(status);
            sched_yield();
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        LOGD("zakoncz_wszystkie_dzieci: pid=%d reaping remaining child\n",
             (int)getpid());
    }
    LOGD("zakoncz_wszystkie_dzieci: pid=%d finished\n", (int)getpid());
}

// ====== GENERATOR KLIENTÓW ======

static pid_t
generator_utworz_jedna_grupe(int numer_grupy) // tworzy jedną grupę klientów
{
    pid_t pid = uruchom_potomka_exec("./klient", "klient", numer_grupy, 1);
    if (pid < 0)
    {
        if (common_ctx->restauracja_otwarta)
        {
            *common_ctx->restauracja_otwarta = 0;
            if (common_ctx->stoliki_sync)
                (void)pthread_cond_broadcast(&common_ctx->stoliki_sync->cond);
        }
        return -1;
    }
    if (ctx->children_pgid > 0)
        (void)setpgid(pid, ctx->children_pgid);
    return pid;
}

// ====== AWARYJNE ZAMKNIĘCIE ======

static int awaryjne_zamkniecie_fork(void) // sprzątanie przy błędzie fork()
{
    // Błąd fork() nie powinien zostawiać osieroconych procesów/IPC.
    if (common_ctx->restauracja_otwarta)
    {
        *common_ctx->restauracja_otwarta = 0;
        if (common_ctx->stoliki_sync)
            (void)pthread_cond_broadcast(&common_ctx->stoliki_sync->cond);
    }
    if (common_ctx->kolej_podsumowania)
    {
        *common_ctx->kolej_podsumowania = 1;
        sygnalizuj_ture();
    }

    LOGS("\n===Awaryjne zamknięcie: błąd tworzenia procesu (fork)!===\n");

    // Jeśli cokolwiek już wystartowało (dzieci w grupie), zakończ je.
    {
        int status;
        zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
        zakoncz_wszystkie_dzieci(&status);
    }

    if (common_ctx->shm_id >= 0)
        shmctl(common_ctx->shm_id, IPC_RMID, NULL); // usuń pamięć współdzieloną
    if (common_ctx->sem_id >= 0)
        semctl(common_ctx->sem_id, 0, IPC_RMID); // usuń semafory
    if (common_ctx->msgq_id >= 0)
        msgctl(common_ctx->msgq_id, IPC_RMID, NULL); // usuń kolejkę komunikatów

    fprintf(stderr,
            "Awaryjne zamknięcie: nie udało się utworzyć procesu (fork).\n");
    return 1;
}

// ====== MAIN ======
/* Initialize restaurant state and start helper processes.
 * On success returns 0 and sets *out_czas_pracy to the configured run time.
 * On failure returns non-zero (same as awaryjne_zamkniecie_fork()).
 */
static int init_restauracja(int argc, char **argv, int *out_czas_pracy)
{
    int default_klienci = CLIENTS_TO_CREATE;
    int default_czas = CZAS_PRACY;
    int default_log = 1;

    if (argc < 1 || argc > 4)
    {
        fprintf(stderr,
                "Użycie: %s [<liczba_klientow>] [<czas_sekund>] [<log_level>]\n",
                argv[0]);
        fprintf(stderr, "Domyślne: %d klientów, %d sekund, log level %d\n",
                default_klienci, default_czas, default_log);
        return 1;
    }

    int klienci = default_klienci;
    int czas = default_czas;
    int log_level = default_log;

    if (argc >= 2)
    {
        errno = 0;
        char *end = NULL;
        long val = strtol(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' || val <= 0)
        {
            fprintf(stderr, "Błędna liczba klientów: %s\n", argv[1]);
            return 1;
        }
        klienci = (int)val;
    }

    if (argc >= 3)
    {
        errno = 0;
        char *end = NULL;
        long val = strtol(argv[2], &end, 10);
        if (errno != 0 || end == argv[2] || *end != '\0' || val <= 0)
        {
            fprintf(stderr, "Błędny czas w sekundach: %s\n", argv[2]);
            return 1;
        }
        czas = (int)val;
    }

    if (argc >= 4)
    {
        errno = 0;
        char *end = NULL;
        long val = strtol(argv[3], &end, 10);
        if (errno != 0 || end == argv[3] || *end != '\0' || val < 0 || val > 2)
        {
            fprintf(stderr, "Błędny log level (0-2): %s\n", argv[3]);
            return 1;
        }
        log_level = (int)val;
    }

    max_losowych_grup = klienci;
    czas_pracy_domyslny = czas;
    current_log_level = log_level;

    char log_level_str[2];
    snprintf(log_level_str, sizeof(log_level_str), "%d", log_level);
    setenv("LOG_LEVEL", log_level_str, 1);

    zainicjuj_losowosc();

    int czas_pracy = czas_pracy_domyslny;
    const char *czas_env = getenv("RESTAURACJA_CZAS_PRACY");
    if (czas_env && *czas_env)
    {
        errno = 0;
        char *end = NULL;
        long v = strtol(czas_env, &end, 10);
        if (errno == 0 && end && *end == '\0' && v > 0)
            czas_pracy = (int)v;
    }

    stworz_ipc();
    generator_stolikow(common_ctx->stoliki);
    fflush(stdout);
    snprintf(ctx->arg_shm, sizeof(ctx->arg_shm), "%d",
             common_ctx->shm_id);
    snprintf(ctx->arg_sem, sizeof(ctx->arg_sem), "%d",
             common_ctx->sem_id);
    snprintf(ctx->arg_msgq, sizeof(ctx->arg_msgq), "%d",
             common_ctx->msgq_id);

    *common_ctx->restauracja_otwarta = 1;
    *common_ctx->kolej_podsumowania = 1;
    sygnalizuj_ture();

    {
        pid_t p = launch_child_and_set_group("./obsluga", "obsluga", 0, 0,
                                             common_ctx->pid_obsluga_shm, 1);
        common_ctx->pid_obsluga = p;
        if (common_ctx->pid_obsluga < 0)
            return awaryjne_zamkniecie_fork();
    }

    signal(SIGINT, restauracja_on_sigint);
    signal(SIGQUIT, restauracja_on_sigint);
    signal(SIGTERM, restauracja_on_sigint);
    signal(SIGTSTP, restauracja_on_sigtstp);
    signal(SIGCONT, restauracja_on_sigcont);

    {
        pid_t p = launch_child_and_set_group("./kucharz", "kucharz", 0, 0,
                                             NULL, 0);
        common_ctx->pid_kucharz = p;
        if (common_ctx->pid_kucharz < 0)
            return awaryjne_zamkniecie_fork();
    }

    {
        pid_t p = launch_child_and_set_group("./kierownik", "kierownik", 0,
                                             0, common_ctx->pid_kierownik_shm, 0);
        common_ctx->pid_kierownik = p;
        if (common_ctx->pid_kierownik < 0)
            return awaryjne_zamkniecie_fork();
    }

    if (out_czas_pracy)
        *out_czas_pracy = czas_pracy;
    return 0;
}

/* Centralized shutdown/cleanup sequence extracted from run_restauracja().
 * Accepts pointer to status to allow reusing zakoncz_wszystkie_dzieci(&status).
 */
static int shutdown_restauracja(int *status)
{
    LOGD("restauracja: pid=%d initiating shutdown sequence\n", (int)getpid());

    LOGD("restauracja: pid=%d calling "
         "zakoncz_klientow_i_wyczysc_stoliki_i_kolejke()\n",
         (int)getpid());
    zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
    LOGD("restauracja: pid=%d returned from zakoncz_klientow...\n",
         (int)getpid());

    LOGD("restauracja: pid=%d calling zakoncz_wszystkie_dzieci()\n",
         (int)getpid());
    zakoncz_wszystkie_dzieci(status);
    LOGD("restauracja: pid=%d returned from zakoncz_wszystkie_dzieci()\n",
         (int)getpid());

    LOGD("restauracja: pid=%d removing IPC resources shm=%d sem=%d msgq=%d\n",
         (int)getpid(), common_ctx->shm_id, common_ctx->sem_id,
         common_ctx->msgq_id);
    if (common_ctx->shm_id >= 0)
        shmctl(common_ctx->shm_id, IPC_RMID, NULL);
    if (common_ctx->sem_id >= 0)
        semctl(common_ctx->sem_id, 0, IPC_RMID);
    if (common_ctx->msgq_id >= 0)
        msgctl(common_ctx->msgq_id, IPC_RMID, NULL);
    LOGD("restauracja: pid=%d IPC resources removed\n", (int)getpid());

    LOGS("\n=== STATYSTYKI KLIENTÓW ===\n");
    LOGS("Klienci przyjęci: %d\n", *common_ctx->klienci_przyjeci);
    LOGS("Klienci którzy opuścili restaurację: %d\n", *common_ctx->klienci_opuscili);
    LOGS("Klienci w kolejce: %d\n", *common_ctx->klienci_w_kolejce);
    LOGS("===========================\n");

    LOGS("Program zakończony.\n");
    return 0;
}

/* Run the main restaurant loop and perform shutdown/cleanup. Returns 0. */
static int run_restauracja(int czas_pracy)
{
    struct timespec start_czekania;
    clock_gettime(CLOCK_MONOTONIC, &start_czekania);
    int status;

    int zforkowane_grupy = 0;
    int max_aktywnych_klientow = MAX_AKTYWNYCH_KLIENTOW_DEFAULT;
    const char *max_env = getenv("RESTAURACJA_MAX_AKTYWNYCH_KLIENTOW");
    if (max_env && *max_env)
    {
        errno = 0;
        char *end = NULL;
        long v = strtol(max_env, &end, 10);
        if (errno == 0 && end && *end == '\0' && v > 0)
            max_aktywnych_klientow = (int)v;
    }

    const char *disable_env = getenv("RESTAURACJA_DISABLE_MANAGER_CLOSE");
    if (disable_env && strcmp(disable_env, "1") == 0)
        common_ctx->disable_close = 1;
    int aktywni_klienci = 0;
    int liczba_utworzonych_grup = max_losowych_grup;

    int kierownik_interval = KIEROWNIK_INTERVAL_DEFAULT;
    const char *kier_env = getenv("RESTAURACJA_KIEROWNIK_INTERVAL");
    if (kier_env && *kier_env)
    {
        errno = 0;
        char *end = NULL;
        long v = strtol(kier_env, &end, 10);
        if (errno == 0 && end && *end == '\0' && v > 0)
            kierownik_interval = (int)v;
    }
    struct timespec last_kierownik_post;
    clock_gettime(CLOCK_MONOTONIC, &last_kierownik_post);
    int numer_grupy = 1;
    while (liczba_utworzonych_grup-- && !ctx->sigint_requested)
    {
        if (kierownik_interval > 0 &&
            elapsed_seconds_since(&last_kierownik_post) >= kierownik_interval)
        {
            sem_operacja(SEM_KIEROWNIK, 1);
            clock_gettime(CLOCK_MONOTONIC, &last_kierownik_post);
        }
        aktywni_klienci -= zbierz_zombie_nieblokujaco_licznik(&status);

        while (aktywni_klienci >= max_aktywnych_klientow)
        {
            pid_t p = waitpid(-1, &status, 0);
            if (p > 0)
            {
                if (p != common_ctx->pid_obsluga && p != common_ctx->pid_kucharz &&
                    p != common_ctx->pid_kierownik)
                    aktywni_klienci--;
            }
            else if (p < 0 && errno == EINTR)
            {
                continue;
            }
            else
            {
                break;
            }
        }

        pid_t pid = generator_utworz_jedna_grupe(numer_grupy++);
        if (pid > 0)
        {
            aktywni_klienci++;
            zforkowane_grupy++;
        }
        if (liczba_utworzonych_grup == 0)
        {
            printf("Utworzono wszystkie grupy klientów.\n");
        }
    }

    {
        struct timespec sim_start;
        clock_gettime(CLOCK_MONOTONIC, &sim_start);
        while (elapsed_seconds_since(&sim_start) < SIMULATION_SECONDS_DEFAULT &&
               !ctx->sigint_requested)
        {
            if (kierownik_interval > 0 &&
                elapsed_seconds_since(&last_kierownik_post) >= kierownik_interval)
            {
                sem_operacja(SEM_KIEROWNIK, 1);
                clock_gettime(CLOCK_MONOTONIC, &last_kierownik_post);
            }
            zbierz_zombie_nieblokujaco(&status);
            sched_yield();
        }
    }

    int koniec_czasu = (elapsed_seconds_since(&start_czekania) >= czas_pracy);
    int przerwano_sygnalem = ctx->sigint_requested;
    int zamknieto_flaga = (!koniec_czasu && !przerwano_sygnalem &&
                           !*common_ctx->restauracja_otwarta);

    LOGD("restauracja: pid=%d shutdown flags: koniec_czasu=%d "
         "przerwano_sygnalem=%d zamknieto_flaga=%d shutdown_signal=%d\n",
         (int)getpid(), koniec_czasu, przerwano_sygnalem, zamknieto_flaga,
         (int)ctx->shutdown_signal);

    *common_ctx->restauracja_otwarta = 0;
    if (common_ctx->stoliki_sync)
        (void)pthread_cond_broadcast(&common_ctx->stoliki_sync->cond);
    if (przerwano_sygnalem)
        LOGS("\n===Przerwano pracę restauracji (%s)!===\n",
             nazwa_sygnalu((int)ctx->shutdown_signal));
    else if (zamknieto_flaga)
        LOGS("\n===Restauracja została zamknięta normalnie!===\n");
    else
        LOGS("\n===Czas pracy restauracji minął!===\n");

    *common_ctx->kolej_podsumowania = 1;
    LOGD("restauracja: pid=%d set kolej_podsumowania=1, signaling SEM_TURA\n",
         (int)getpid());
    sygnalizuj_ture();

    struct timespec podsumowanie_start;
    clock_gettime(CLOCK_MONOTONIC, &podsumowanie_start);
    while (*common_ctx->kolej_podsumowania != 3 &&
           elapsed_seconds_since(&podsumowanie_start) < SUMMARY_WAIT_SECONDS)
    {
        (void)sleep_ms(POLL_MS_MED);
    }
    (void)sleep_ms(POLL_MS_LONG);

    /* Call centralized shutdown/cleanup helper. */
    shutdown_restauracja(&status);
    return 0;
}

int main(int argc, char **argv)
{
    int czas_pracy = 0;
    int rc = init_restauracja(argc, argv, &czas_pracy);
    if (rc != 0)
        return rc;
    return run_restauracja(czas_pracy);
}