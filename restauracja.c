#define _POSIX_C_SOURCE 200809L

#include "restauracja.h" /* includes common.h */

#include <stdio.h>
/* Additional system headers not included via common.h */
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

/* (Removed parse_env_positive) */

/* Helper: parse positive integer strictly from argv; returns -1 on error. */
static long parse_arg_positive_or_error(const char *s)
{
    if (!s)
        return -1;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v <= 0)
        return -1;
    return v;
}

/* Helper: parse integer from environment with inclusive range [min,max].
 * If env var is unset or invalid, returns `fallback`.
 * Use `max < 0` to indicate no upper bound. */
static int parse_env_int_range(const char *name, int fallback, int min, int max)
{
    const char *s = getenv(name);
    if (!s || !*s)
        return fallback;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno == 0 && end && *end == '\0' && v >= min && (max < 0 || v <= max))
        return (int)v;
    return fallback;
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

/* Handler is async-signal-safe: it only sets numeric flags. */

/* Single signal handler that consolidates SIGINT/SIGQUIT/SIGTERM, SIGTSTP
 * and SIGCONT behavior. Keeping this local to the module keeps the
 * implementation details (and `ctx`) encapsulated in `restauracja.c`.
 */
static void restauracja_signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGQUIT || signo == SIGTERM)
    {
        ctx->sigint_requested = 1;
        ctx->shutdown_signal = signo;
        if (ctx->children_pgid > 0)
            (void)kill(-ctx->children_pgid, SIGTERM);
    }
    else if (signo == SIGTSTP)
    {
        if (ctx->children_pgid > 0)
            (void)kill(-ctx->children_pgid, SIGTSTP);
        (void)kill(getpid(), SIGSTOP);
    }
    else if (signo == SIGCONT)
    {
        if (ctx->children_pgid > 0)
            (void)kill(-ctx->children_pgid, SIGCONT);
    }
    else
    {
        /* ignore other signals */
    }
}

// ====== ZARZĄDZANIE PROCESAMI ======

/* Unified reaper: non-blocking waitpid loop. If `count_clients` is
 * non-zero, returns the number of reaped PIDs that are considered
 * client processes (skips obsluga/kucharz/kierownik). If
 * `count_clients` is zero the function behaves like the previous
 * `zbierz_zombie_nieblokujaco` (just reaps and logs). */
static int zbierz_zombie_nieblokujaco(int *status, int count_clients)
{
    int reaped = 0;
    for (;;)
    {
        pid_t p = waitpid(-1, status, WNOHANG); // nieblokująco
        if (p <= 0)
            break;
        if (count_clients)
        {
            LOGD("restauracja: zbierz_zombie_nieblokujaco: pid=%d reaped=%d\n",
                 (int)getpid(), (int)p);
            if (p != common_ctx->pid_obsluga && p != common_ctx->pid_kucharz &&
                p != common_ctx->pid_kierownik && p != common_ctx->pid_szatnia)
                reaped++;
        }
        else
        {
            LOGD(
                "restauracja: zbierz_zombie_nieblokujaco: pid=%d reaped=%d status=%d\n",
                (int)getpid(), (int)p, (status ? *status : -1));
        }
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
        (void)zbierz_zombie_nieblokujaco(status, 0);
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
            (void)zbierz_zombie_nieblokujaco(status, 0);
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

static void zakoncz_klientow_i_wyczysc_stoliki_i_kolejke(void);

static int awaryjne_zamkniecie_fork(void) // sprzątanie przy błędzie fork()
{
    // Błąd fork() nie powinien zostawiać osieroconych procesów/IPC.
    if (common_ctx->restauracja_otwarta)
    {
        *common_ctx->restauracja_otwarta = 0;
        if (common_ctx->stoliki_sync)
            (void)pthread_cond_broadcast(&common_ctx->stoliki_sync->cond);
    }
    /* Signal open/turn token so waiting workers don't stay blocked. */
    sygnalizuj_ture_na(1);

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

static void generator_stolikow(struct Stolik *stoliki_local)
{
    int idx = 0;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < ILOSC_STOLIKOW[i]; j++)
        {
            int suma_poprzednich = 0;
            for (int k = 0; k < i; k++)
                suma_poprzednich += ILOSC_STOLIKOW[k];

            idx = suma_poprzednich + j;
            stoliki_local[idx].numer_stolika = idx + 1;
            stoliki_local[idx].pojemnosc = i + 1;
            stoliki_local[idx].liczba_grup = 0;
            stoliki_local[idx].zajete_miejsca = 0;
            memset(stoliki_local[idx].grupy, 0,
                   sizeof(stoliki_local[idx].grupy));

            LOGP("Stolik %d o pojemności %d utworzony.\n",
                 stoliki_local[idx].numer_stolika,
                 stoliki_local[idx].pojemnosc);
        }
    }
}

static void zakoncz_klientow_i_wyczysc_stoliki_i_kolejke(
    void) // kończy wszystkich klientów i czyści stan stolików i kolejki
{
    LOGD("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d start\n",
         (int)getpid());

    // Klienci usadzeni przy stolikach.
    int stoliki_locked = 0;
    struct timespec lock_deadline;
    if (clock_gettime(CLOCK_REALTIME, &lock_deadline) == 0)
    {
        lock_deadline.tv_sec += 1;
        if (common_ctx->stoliki_sync &&
            pthread_mutex_timedlock(&common_ctx->stoliki_sync->mutex,
                                    &lock_deadline) == 0)
            stoliki_locked = 1;
    }
    if (!stoliki_locked)

        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            for (int j = 0; j < common_ctx->stoliki[i].liczba_grup; j++)
            {
                pid_t pid = common_ctx->stoliki[i].grupy[j].proces_id;
                if (pid > 0)
                {
                    (void)kill(pid, SIGTERM);
                }
            }

            memset(common_ctx->stoliki[i].grupy, 0,
                   sizeof(common_ctx->stoliki[i].grupy));
            common_ctx->stoliki[i].liczba_grup = 0;
            common_ctx->stoliki[i].zajete_miejsca = 0;
        }
    if (stoliki_locked)
        pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

    // Klienci w kolejce wejściowej.
    LOGD("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d cleaning queue\n",
         (int)getpid());
    QueueMsg msg;
    for (;;)
    {
        ssize_t r = msgrcv(common_ctx->msgq_id, &msg, sizeof(msg.grupa), 1,
                           IPC_NOWAIT);
        if (r < 0)
        {
            if (errno == ENOMSG)
                break;
            if (errno == EINTR)
                continue;
            if (errno == EIDRM || errno == EINVAL)
                return;
            break;
        }

        pid_t pid = msg.grupa.proces_id;
        LOGD("zakoncz_klientow: pid=%d popped queued client pid=%d\n",
             (int)getpid(), (int)pid);
        if (pid > 0)
        {
            LOGD("zakoncz_klientow: pid=%d killing queued client pid=%d\n",
                 (int)getpid(), (int)pid);
            (void)kill(pid, SIGTERM);
        }

        // Zdjęliśmy komunikat z kolejki, więc zwalniamy slot w liczniku queue_sync.
        if (pthread_mutex_trylock(&common_ctx->queue_sync->mutex) == 0)
        {
            if (common_ctx->queue_sync->count > 0)
                common_ctx->queue_sync->count--;
            /* Zmniejsz liczbę klientów w kolejce o rozmiar tej grupy. */
            if (common_ctx->klienci_w_kolejce && msg.grupa.osoby > 0)
            {
                if (*common_ctx->klienci_w_kolejce >= msg.grupa.osoby)
                    *common_ctx->klienci_w_kolejce -= msg.grupa.osoby;
                else
                    *common_ctx->klienci_w_kolejce = 0;
            }
            pthread_cond_signal(&common_ctx->queue_sync->not_full);
            pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
        }
    }
    LOGD("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d done\n",
         (int)getpid());
}

// ====== MAIN ======
/* Initialize restaurant state and start helper processes.
 * On success returns 0 and sets *out_czas_pracy to the configured run time.
 * On failure returns non-zero (same as awaryjne_zamkniecie_fork()).
 */
int init_restauracja(int argc, char **argv, int *out_czas_pracy)
{
    int klienci = LICZBA_GRUP_DEFAULT;
    int czas = CZAS_PRACY_DEFAULT;
    int log_level = LOG_LEVEL_DEFAULT;

    /* Read defaults from environment (lenient) with validation. */
    klienci = parse_env_int_range("RESTAURACJA_LICZBA_KLIENTOW", klienci, 1, -1);
    log_level = parse_env_int_range("RESTAURACJA_LOG_LEVEL", log_level, 0, 2);
    czas = parse_env_int_range("RESTAURACJA_CZAS_PRACY", czas, 1, -1);

    if (argc < 1 || argc > 4)
    {
        fprintf(stderr,
                "Użycie: %s [<liczba_klientow>] [<czas_sekund>] [<log_level>]\n",
                argv[0]);
        fprintf(stderr, "Domyślne: %d klientów, %d sekund, log level %d\n",
                klienci, czas, log_level);
        return 1;
    }

    if (argc >= 2)
    {
        long v = parse_arg_positive_or_error(argv[1]);
        if (v < 0)
        {
            fprintf(stderr, "Błędna liczba klientów: %s\n", argv[1]);
            return 1;
        }
        klienci = (int)v;
    }

    if (argc >= 3)
    {
        long v = parse_arg_positive_or_error(argv[2]);
        if (v < 0)
        {
            fprintf(stderr, "Błędny czas w sekundach: %s\n", argv[2]);
            return 1;
        }
        czas = (int)v;
    }

    if (argc >= 4)
    {
        long v = parse_arg_positive_or_error(argv[3]);
        if (v < 0 || v > 2)
        {
            fprintf(stderr, "Błędny log level (0-2): %s\n", argv[3]);
            return 1;
        }
        log_level = (int)v;
    }

    /* Ustaw globalne wartości dla bieżącego uruchomienia. */
    liczba_klientow = klienci;
    czas_pracy_domyslny = czas;
    current_log_level = log_level;

    char log_level_str[3];
    snprintf(log_level_str, sizeof(log_level_str), "%d", log_level);
    setenv("LOG_LEVEL", log_level_str, 1);

    zainicjuj_losowosc();

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
    sygnalizuj_ture_na(1);

    /* Register signal handlers early, before launching child processes, so
     * the parent can respond to interrupts during startup and while
     * creating children. */
    signal(SIGINT, restauracja_signal_handler);
    signal(SIGQUIT, restauracja_signal_handler);
    signal(SIGTERM, restauracja_signal_handler);
    signal(SIGTSTP, restauracja_signal_handler);
    signal(SIGCONT, restauracja_signal_handler);

    pid_t p;
    p = launch_child_and_set_group("./obsluga", "obsluga", 0, 0,
                                   common_ctx->pid_obsluga_shm, 1);
    common_ctx->pid_obsluga = p;
    if (common_ctx->pid_obsluga < 0)
        return awaryjne_zamkniecie_fork();
    p = launch_child_and_set_group("./szatnia", "szatnia", 0, 0,
                                   NULL, 0);
    common_ctx->pid_szatnia = p;
    if (common_ctx->pid_szatnia < 0)
        return awaryjne_zamkniecie_fork();
    p = launch_child_and_set_group("./kucharz", "kucharz", 0, 0,
                                   NULL, 0);
    common_ctx->pid_kucharz = p;
    if (common_ctx->pid_kucharz < 0)
        return awaryjne_zamkniecie_fork();
    p = launch_child_and_set_group("./kierownik", "kierownik", 0,
                                   0, common_ctx->pid_kierownik_shm, 0);
    common_ctx->pid_kierownik = p;
    if (common_ctx->pid_kierownik < 0)
        return awaryjne_zamkniecie_fork();

    if (out_czas_pracy)
        *out_czas_pracy = czas_pracy_domyslny;
    return 0;
}

/* Centralized shutdown/cleanup sequence extracted from run_restauracja().
 * Accepts pointer to status to allow reusing zakoncz_wszystkie_dzieci(&status).
 */
int shutdown_restauracja(int *status)
{
    zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
    zakoncz_wszystkie_dzieci(status);
    if (common_ctx->shm_id >= 0)
        shmctl(common_ctx->shm_id, IPC_RMID, NULL);
    if (common_ctx->sem_id >= 0)
        semctl(common_ctx->sem_id, 0, IPC_RMID);
    if (common_ctx->msgq_id >= 0)
        msgctl(common_ctx->msgq_id, IPC_RMID, NULL);

    LOGS("\n=== STATYSTYKI KLIENTÓW ===\n");
    LOGS("Klienci przyjęci: %d\n", *common_ctx->klienci_przyjeci);
    LOGS("Klienci którzy opuścili restaurację: %d\n", *common_ctx->klienci_opuscili);
    LOGS("Klienci w kolejce: %d\n", *common_ctx->klienci_w_kolejce);
    LOGS("===========================\n");

    LOGS("Program zakończony.\n");
    return 0;
}

/* Run the main restaurant loop and perform shutdown/cleanup. Returns 0. */
int run_restauracja(int czas_pracy)
{
    struct timespec start_czekania;
    clock_gettime(CLOCK_MONOTONIC, &start_czekania);
    int status;
    int aktywni_klienci = 0;
    int liczba_utworzonych_grup = liczba_klientow;
    int kierownik_interval = KIEROWNIK_INTERVAL_DEFAULT;
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
        aktywni_klienci -= zbierz_zombie_nieblokujaco(&status, 1);

        pid_t pid = generator_utworz_jedna_grupe(numer_grupy++);
        if (pid > 0)
        {
            aktywni_klienci++;
        }
        if (liczba_utworzonych_grup == 0)
        {
            printf("Utworzono wszystkie grupy klientów.\n");
        }
    }

    struct timespec sim_start;
    clock_gettime(CLOCK_MONOTONIC, &sim_start);
    while (elapsed_seconds_since(&sim_start) < czas_pracy &&
           !ctx->sigint_requested)
    {
        if (kierownik_interval > 0 &&
            elapsed_seconds_since(&last_kierownik_post) >= kierownik_interval)
        {
            sem_operacja(SEM_KIEROWNIK, 1);
            clock_gettime(CLOCK_MONOTONIC, &last_kierownik_post);
        }
        (void)zbierz_zombie_nieblokujaco(&status, 0);
        sched_yield();
    }

    int przerwano_sygnalem = ctx->sigint_requested;
    int restauracja_otwarta_przed = *common_ctx->restauracja_otwarta;
    int czas_minal = (elapsed_seconds_since(&start_czekania) >= czas_pracy);

    LOGD("restauracja: pid=%d shutdown_signal=%d\n", (int)getpid(),
         (int)ctx->shutdown_signal);

    *common_ctx->restauracja_otwarta = 0;
    if (common_ctx->stoliki_sync)
        (void)pthread_cond_broadcast(&common_ctx->stoliki_sync->cond);
    if (przerwano_sygnalem)
    {
        const char *name = "(nieznany)";
        if (ctx->shutdown_signal == SIGINT)
            name = "SIGINT";
        else if (ctx->shutdown_signal == SIGQUIT)
            name = "SIGQUIT";
        else if (ctx->shutdown_signal == SIGTERM)
            name = "SIGTERM";
        LOGS("\n===Przerwano pracę restauracji (%s)!===\n", name);
    }
    else if (!czas_minal && !restauracja_otwarta_przed)
        LOGS("\n===Restauracja została zamknięta normalnie!===\n");
    else
        LOGS("\n===Czas pracy restauracji minął!===\n");

    LOGD("restauracja: pid=%d set turn=1, signaling SEM_TURA\n",
         (int)getpid());
    sygnalizuj_ture_na(1);

    /* Wait for kucharz to notify parent that turn-3 processing completed. */
    (void)sem_timedwait_seconds(SEM_PARENT_NOTIFY3, SUMMARY_WAIT_SECONDS);
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