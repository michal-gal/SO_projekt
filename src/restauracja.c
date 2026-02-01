#define _POSIX_C_SOURCE 200809L

#include "restauracja.h" /* includes common.h */

#include <stdio.h>
/* Dodatkowe nagłówki systemowe spoza common.h */
#include <errno.h>
#include <sched.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>

#ifndef BIN_DIR
#define BIN_DIR "."
#endif

extern int current_log_level;

/* Pomocnik: zwraca upływ sekund od czasu `poczatek` według CLOCK_MONOTONIC. */
static inline long sekundy_od(const struct timespec *poczatek)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return now.tv_sec - poczatek->tv_sec;
}

static void dopisz_do_bufora(char *buf, size_t rozmiar, size_t *offset,
                             const char *fmt, ...)
{
    if (!buf || !offset || *offset >= rozmiar)
        return;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *offset, rozmiar - *offset, fmt, ap);
    va_end(ap);

    if (n <= 0)
        return;

    size_t dodano = (size_t)n;
    if (dodano >= rozmiar - *offset)
        *offset = rozmiar - 1;
    else
        *offset += dodano;
}

/* Pomocnik: parsuje dodatnią liczbę z argv; zwraca -1 przy błędzie. */
static long parsuj_arg_dodatni_lub_blad(const char *s)
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

/* Pomocnik: parsuje int z env w zakresie [min,max].
 * Gdy env nieustawiony lub błędny, zwraca wartość domyślną.
 * Ustaw `max < 0`, aby nie mieć górnego limitu. */
static int parsuj_env_int_zakres(const char *name, int domyslna, int min, int max)
{
    const char *s = getenv(name);
    if (!s || !*s)
        return domyslna;
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno == 0 && end && *end == '\0' && v >= min && (max < 0 || v <= max))
        return (int)v;
    return domyslna;
}

// ====== ZMIENNE GLOBALNE ======

/* Kontekst uruchomienia, aby ograniczyć globalne pola. */
struct KontekstRestauracji
{
    char arg_shm[32];
    char arg_sem[32];
    char arg_msgq[32];
    pid_t pgid_dzieci;
    volatile sig_atomic_t zamkniecie_zadane;
    volatile sig_atomic_t sygnal_zamkniecia;
    volatile sig_atomic_t stop_generatora;
    volatile sig_atomic_t stop_zbieracza;
};

static struct KontekstRestauracji kontekst_bufor = {.pgid_dzieci = -1,
                                                    .zamkniecie_zadane = 0,
                                                    .sygnal_zamkniecia = 0,
                                                    .stop_generatora = 0,
                                                    .stop_zbieracza = 0};
static struct KontekstRestauracji *kontekst = &kontekst_bufor;

// Wszystkie procesy potomne (obsluga/kucharz/kierownik/klienci) wrzucamy do
// jednej grupy, żeby móc zakończyć je jednym sygnałem: kill(-pgid,
// SIGTERM/SIGKILL).

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

/* Obsługa sygnału jest bezpieczna asynchronicznie: ustawia tylko flagi numeryczne. */

/* Jedna obsługa łącząca SIGINT/SIGQUIT/SIGTERM oraz SIGTSTP/SIGCONT.
 * Lokalny w module, aby szczegóły (i `kontekst`) pozostały w `restauracja.c`.
 */
static void obsluz_sygnal_restauracji(int signo)
{
    if (signo == SIGINT || signo == SIGQUIT || signo == SIGTERM)
    {
        kontekst->zamkniecie_zadane = 1;
        kontekst->sygnal_zamkniecia = signo;
        if (kontekst->pgid_dzieci > 0)
            (void)kill(-kontekst->pgid_dzieci, SIGTERM);
    }
    else if (signo == SIGTSTP)
    {
        if (kontekst->pgid_dzieci > 0)
            (void)kill(-kontekst->pgid_dzieci, SIGTSTP);
        (void)kill(getpid(), SIGSTOP);
    }
    else if (signo == SIGCONT)
    {
        if (kontekst->pgid_dzieci > 0)
            (void)kill(-kontekst->pgid_dzieci, SIGCONT);
    }
    else
    {
        /* ignoruj pozostałe sygnały */
    }
}

// ====== ZARZĄDZANIE PROCESAMI ======

/* Ujednolicony „zbieracz zombie”: pętla waitpid bez blokowania.
 * Gdy `count_clients` != 0, zwraca liczbę zebranych PID klientów
 * (pomija obsluga/kucharz/kierownik/szatnia). */
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

static pid_t generator_utworz_jedna_grupe(int numer_grupy);

struct GeneratorGrupCtx
{
    int liczba_utworzonych_grup;
    int numer_grupy;
};

static void *watek_generatora_grup(void *arg)
{
    struct GeneratorGrupCtx *gctx = (struct GeneratorGrupCtx *)arg;

    while (gctx->liczba_utworzonych_grup-- && !kontekst->zamkniecie_zadane &&
           !kontekst->stop_generatora)
    {
        pid_t pid = generator_utworz_jedna_grupe(gctx->numer_grupy++);
        if (pid > 0)
        {
            /* brak dodatkowych działań */
        }
        if (gctx->liczba_utworzonych_grup == 0)
        {
            printf("Utworzono wszystkie grupy klientów.\n");
        }
    }
    free(gctx);
    return NULL;
}

static void *watek_zbieracza_zombie(void *arg)
{
    (void)arg;
    int status;

    while (!kontekst->zamkniecie_zadane && !kontekst->stop_zbieracza)
    {
        (void)zbierz_zombie_nieblokujaco(&status, 0);
        (void)usypiaj_ms(POLL_MS_MED);
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    return NULL;
}

static pid_t uruchom_potomka_exec(
    const char *file, const char *argv0, int numer_grupy,
    int czy_klient) // uruchamia proces potomny przez fork()+exec()
{
    pid_t pid = fork();
    if (pid == 0)
    {
        zamknij_odziedziczone_fd_przed_exec();
        if (czy_klient)
        {
            char arg_grupa[32];
            snprintf(arg_grupa, sizeof(arg_grupa), "%d", numer_grupy);
            execl(file, argv0, kontekst->arg_shm, kontekst->arg_sem,
                  kontekst->arg_msgq, arg_grupa,
                  (char *)NULL);
        }
        else
        {
            execl(file, argv0, kontekst->arg_shm, kontekst->arg_sem,
                  kontekst->arg_msgq,
                  (char *)NULL);
        }
        /* ścieżka awaryjna; brak dodatkowego execl */
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

/* Pomocnik: uruchamia potomka przez uruchom_potomka_exec(), opcjonalnie
 * zapisuje PID do shm i dołącza do grupy procesów. */
static pid_t uruchom_potomka_i_ustaw_grupe(const char *file, const char *argv0,
                                           int numer_grupy, int czy_klient,
                                           pid_t *pid_shm_wyj,
                                           int utworz_grupe_jesli_brak)
{
    pid_t pid = uruchom_potomka_exec(file, argv0, numer_grupy, czy_klient);
    if (pid < 0)
        return -1;

    if (pid_shm_wyj)
        *pid_shm_wyj = pid;

    if (utworz_grupe_jesli_brak && kontekst->pgid_dzieci <= 0)
    {
        kontekst->pgid_dzieci = pid;
        (void)setpgid(pid, kontekst->pgid_dzieci);
    }
    else if (kontekst->pgid_dzieci > 0)
    {
        (void)setpgid(pid, kontekst->pgid_dzieci);
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
    /* Utrzymaj łączny czas oczekiwania poniżej typowego limitu testów (10 s). */
    const int timeout_term = SHUTDOWN_TERM_TIMEOUT;
    const int timeout_kill = SHUTDOWN_KILL_TIMEOUT;

    LOGD("zakoncz_wszystkie_dzieci: pid=%d start, pgid_dzieci=%d\n",
         (int)getpid(), (int)kontekst->pgid_dzieci);
    if (kontekst->pgid_dzieci > 0)
    {
        LOGD("zakoncz_wszystkie_dzieci: pid=%d wysyłam SIGTERM do -%d\n",
             (int)getpid(), (int)kontekst->pgid_dzieci);
        kill(-kontekst->pgid_dzieci, SIGTERM);
    }

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (!czy_grupa_procesow_pusta(kontekst->pgid_dzieci) &&
           sekundy_od(&start) < timeout_term)
    {
        LOGD("zakoncz_wszystkie_dzieci: pid=%d czekam na dzieci, uplynelo=%ld\n",
             (int)getpid(), (long)sekundy_od(&start));
        (void)zbierz_zombie_nieblokujaco(status, 0);
        sched_yield();
    }

    if (!czy_grupa_procesow_pusta(kontekst->pgid_dzieci))
    {
        if (kontekst->pgid_dzieci > 0)
        {
            LOGD("zakoncz_wszystkie_dzieci: pid=%d wysyłam SIGKILL do -%d\n",
                 (int)getpid(), (int)kontekst->pgid_dzieci);
            kill(-kontekst->pgid_dzieci, SIGKILL);
        }

        clock_gettime(CLOCK_MONOTONIC, &start);
        while (!czy_grupa_procesow_pusta(kontekst->pgid_dzieci) &&
               sekundy_od(&start) < timeout_kill)
        {
            LOGD("zakoncz_wszystkie_dzieci: pid=%d czekam po SIGKILL, "
                 "uplynelo=%ld\n",
                 (int)getpid(), (long)sekundy_od(&start));
            (void)zbierz_zombie_nieblokujaco(status, 0);
            sched_yield();
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        LOGD("zakoncz_wszystkie_dzieci: pid=%d zbieram pozostale dzieci\n",
             (int)getpid());
    }
    LOGD("zakoncz_wszystkie_dzieci: pid=%d zakonczono\n", (int)getpid());
}

// ====== GENERATOR KLIENTÓW ======

static pid_t
generator_utworz_jedna_grupe(int numer_grupy) // tworzy jedną grupę klientów
{
    pid_t pid = uruchom_potomka_exec(BIN_DIR "/klient", "klient", numer_grupy, 1);
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
    if (kontekst->pgid_dzieci > 0)
        (void)setpgid(pid, kontekst->pgid_dzieci);
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
    /* Zasygnalizuj otwarcie/turę, by czekające wątki nie zostały zablokowane. */
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
/* Inicjalizuje stan restauracji i uruchamia procesy pomocnicze.
 * Przy powodzeniu zwraca 0 i ustawia *out_czas_pracy na czas pracy.
 * Przy błędzie zwraca wartość niezerową (jak awaryjne_zamkniecie_fork()).
 */
int inicjuj_restauracje(int argc, char **argv, int *out_czas_pracy)
{
    int klienci = LICZBA_GRUP_DEFAULT;
    int czas = CZAS_PRACY_DEFAULT;
    int log_level = LOG_LEVEL_DEFAULT;

    /* Wczytaj domyślne wartości z env (łagodnie) z walidacją. */
    klienci = parsuj_env_int_zakres("RESTAURACJA_LICZBA_KLIENTOW", klienci, 1, -1);
    log_level = parsuj_env_int_zakres("RESTAURACJA_LOG_LEVEL", log_level, 0, 3);
    czas = parsuj_env_int_zakres("RESTAURACJA_CZAS_PRACY", czas, 1, -1);

    if (argc < 1 || argc > 4)
    {
        fprintf(stderr,
                "Użycie: %s [<liczba_klientow>] [<czas_sekund>] [<log_level>]\n",
                argv[0]);
        fprintf(stderr, "Domyślne: %d klientów, %d sekund, poziom logowania %d\n",
                klienci, czas, log_level);
        return 1;
    }

    if (argc >= 2)
    {
        long v = parsuj_arg_dodatni_lub_blad(argv[1]);
        if (v < 0)
        {
            fprintf(stderr, "Błędna liczba klientów: %s\n", argv[1]);
            return 1;
        }
        klienci = (int)v;
    }

    if (argc >= 3)
    {
        long v = parsuj_arg_dodatni_lub_blad(argv[2]);
        if (v < 0)
        {
            fprintf(stderr, "Błędny czas w sekundach: %s\n", argv[2]);
            return 1;
        }
        czas = (int)v;
    }

    if (argc >= 4)
    {
        long v = parsuj_arg_dodatni_lub_blad(argv[3]);
        if (v < 0 || v > 3)
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
    snprintf(kontekst->arg_shm, sizeof(kontekst->arg_shm), "%d",
             common_ctx->shm_id);
    snprintf(kontekst->arg_sem, sizeof(kontekst->arg_sem), "%d",
             common_ctx->sem_id);
    snprintf(kontekst->arg_msgq, sizeof(kontekst->arg_msgq), "%d",
             common_ctx->msgq_id);

    *common_ctx->restauracja_otwarta = 1;
    sygnalizuj_ture_na(1);

    /* Zarejestruj obsługę sygnałów przed startem potomków. */
    signal(SIGINT, obsluz_sygnal_restauracji);
    signal(SIGQUIT, obsluz_sygnal_restauracji);
    signal(SIGTERM, obsluz_sygnal_restauracji);
    signal(SIGTSTP, obsluz_sygnal_restauracji);
    signal(SIGCONT, obsluz_sygnal_restauracji);

    pid_t p;
    p = uruchom_potomka_i_ustaw_grupe(BIN_DIR "/obsluga", "obsluga", 0, 0,
                                      common_ctx->pid_obsluga_shm, 1);
    common_ctx->pid_obsluga = p;
    if (common_ctx->pid_obsluga < 0)
        return awaryjne_zamkniecie_fork();
    p = uruchom_potomka_i_ustaw_grupe(BIN_DIR "/szatnia", "szatnia", 0, 0,
                                      NULL, 0);
    common_ctx->pid_szatnia = p;
    if (common_ctx->pid_szatnia < 0)
        return awaryjne_zamkniecie_fork();
    p = uruchom_potomka_i_ustaw_grupe(BIN_DIR "/kucharz", "kucharz", 0, 0,
                                      NULL, 0);
    common_ctx->pid_kucharz = p;
    if (common_ctx->pid_kucharz < 0)
        return awaryjne_zamkniecie_fork();
    p = uruchom_potomka_i_ustaw_grupe(BIN_DIR "/kierownik", "kierownik", 0,
                                      0, common_ctx->pid_kierownik_shm, 0);
    common_ctx->pid_kierownik = p;
    if (common_ctx->pid_kierownik < 0)
        return awaryjne_zamkniecie_fork();

    if (out_czas_pracy)
        *out_czas_pracy = czas_pracy_domyslny;
    return 0;
}

/* Scentralizowana sekwencja zamknięcia/sprzątania. */
int zamknij_restauracje(int *status)
{
    zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
    zakoncz_wszystkie_dzieci(status);
    if (common_ctx->shm_id >= 0)
        shmctl(common_ctx->shm_id, IPC_RMID, NULL);
    if (common_ctx->sem_id >= 0)
        semctl(common_ctx->sem_id, 0, IPC_RMID);
    if (common_ctx->msgq_id >= 0)
        msgctl(common_ctx->msgq_id, IPC_RMID, NULL);

    char buf[2048];
    size_t offset = 0;
    dopisz_do_bufora(buf, sizeof(buf), &offset, "\n\n\n========== STATYSTYKI KLIENTÓW =================\n");
    int przyjeci = 0;
    int opuscili = 0;
    int kolejka = 0;
    if (common_ctx->statystyki_sync &&
        pthread_mutex_lock(&common_ctx->statystyki_sync->mutex) == 0)
    {
        przyjeci = *common_ctx->klienci_przyjeci;
        opuscili = *common_ctx->klienci_opuscili;
        pthread_mutex_unlock(&common_ctx->statystyki_sync->mutex);
    }
    if (common_ctx->klienci_w_kolejce)
        kolejka = *common_ctx->klienci_w_kolejce;

    dopisz_do_bufora(buf, sizeof(buf), &offset, "Klienci przyjęci: %d\n", przyjeci);
    dopisz_do_bufora(buf, sizeof(buf), &offset,
                     "Klienci którzy opuścili restaurację: %d\n", opuscili);
    dopisz_do_bufora(buf, sizeof(buf), &offset, "Klienci w kolejce: %d\n", kolejka);
    dopisz_do_bufora(buf, sizeof(buf), &offset, "================================================\n");
    dopisz_do_bufora(buf, sizeof(buf), &offset, "Program zakończony.\n");

    loguj_blokiem('I', buf);
    return 0;
}

/* Uruchamia główną pętlę restauracji i wykonuje sprzątanie. */
int uruchom_restauracje(int czas_pracy)
{
    struct timespec start_czekania;
    clock_gettime(CLOCK_MONOTONIC, &start_czekania);
    int status;
    int liczba_utworzonych_grup = liczba_klientow;
    int kierownik_interval = KIEROWNIK_INTERVAL_DEFAULT;
    struct timespec last_kierownik_post;
    clock_gettime(CLOCK_MONOTONIC, &last_kierownik_post);
    int numer_grupy = 1;

    struct GeneratorGrupCtx *gen_ctx = malloc(sizeof(*gen_ctx));
    if (!gen_ctx)
        return 1;
    *gen_ctx = (struct GeneratorGrupCtx){
        .liczba_utworzonych_grup = liczba_utworzonych_grup,
        .numer_grupy = numer_grupy,
    };
    pthread_t watek_generatora;
    pthread_t watek_zbieracza;
    int rc_generator = pthread_create(&watek_generatora, NULL,
                                      watek_generatora_grup, gen_ctx);
    if (rc_generator != 0)
    {
        LOGE_ERRNO("pthread_create(watek_generatora_grup)");
        (void)watek_generatora_grup(gen_ctx);
    }
    else
    {
        (void)pthread_detach(watek_generatora);
    }

    int rc_zbieracz = pthread_create(&watek_zbieracza, NULL,
                                     watek_zbieracza_zombie, NULL);
    if (rc_zbieracz != 0)
        LOGE_ERRNO("pthread_create(watek_zbieracza_zombie)");
    else
        (void)pthread_detach(watek_zbieracza);

    struct timespec sim_start;
    clock_gettime(CLOCK_MONOTONIC, &sim_start);
    while (sekundy_od(&sim_start) < czas_pracy &&
           !kontekst->zamkniecie_zadane)
    {
        if (kierownik_interval > 0 &&
            sekundy_od(&last_kierownik_post) >= kierownik_interval)
        {
            sem_operacja(SEM_KIEROWNIK, 1);
            clock_gettime(CLOCK_MONOTONIC, &last_kierownik_post);
        }
        sched_yield();
    }

    kontekst->stop_generatora = 1;
    kontekst->stop_zbieracza = 1;

    int przerwano_sygnalem = kontekst->zamkniecie_zadane;
    int restauracja_otwarta_przed = *common_ctx->restauracja_otwarta;
    int czas_minal = (sekundy_od(&start_czekania) >= czas_pracy);

    LOGD("restauracja: pid=%d sygnal_zamkniecia=%d\n", (int)getpid(),
         (int)kontekst->sygnal_zamkniecia);

    *common_ctx->restauracja_otwarta = 0;
    if (common_ctx->stoliki_sync)
        (void)pthread_cond_broadcast(&common_ctx->stoliki_sync->cond);
    if (przerwano_sygnalem)
    {
        const char *name = "(nieznany)";
        if (kontekst->sygnal_zamkniecia == SIGINT)
            name = "SIGINT";
        else if (kontekst->sygnal_zamkniecia == SIGQUIT)
            name = "SIGQUIT";
        else if (kontekst->sygnal_zamkniecia == SIGTERM)
            name = "SIGTERM";
        LOGS("\n===Przerwano pracę restauracji (%s)!===\n", name);
    }
    else if (!czas_minal && !restauracja_otwarta_przed)
        LOGS("\n===Restauracja została zamknięta normalnie!===\n");
    else
        LOGS("\n===Czas pracy restauracji minął!===\n");

    LOGD("restauracja: pid=%d ustawiam ture=1, sygnalizuje SEM_TURA_TURN1\n",
         (int)getpid());
    sygnalizuj_ture_na(1);

    /* Czekaj na powiadomienie od kucharza o zakończeniu tury 3. */
    (void)sem_czekaj_sekund(SEM_PARENT_NOTIFY3, SUMMARY_WAIT_SECONDS);
    (void)usypiaj_ms(POLL_MS_LONG);

    /* Wywołaj scentralizowane sprzątanie. */
    zamknij_restauracje(&status);
    return 0;
}

int main(int argc, char **argv)
{
    int czas_pracy = 0;
    int rc = inicjuj_restauracje(argc, argv, &czas_pracy);
    if (rc != 0)
        return rc;
    return uruchom_restauracje(czas_pracy);
}