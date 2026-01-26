#include "restauracja.h"

#include <errno.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char arg_shm[32];
static char arg_sem[32];
static char arg_msgq[32];

// Wszystkie procesy potomne (obsluga/kucharz/kierownik/klienci) wrzucamy do jednej grupy,
// żeby móc zakończyć je jednym sygnałem: kill(-pgid, SIGTERM/SIGKILL).
static pid_t children_pgid = -1;

static volatile sig_atomic_t sigint_requested = 0; // czy został otrzymany SIGINT/SIGQUIT
static volatile sig_atomic_t shutdown_signal = 0;  // który sygnał spowodował zamknięcie

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
    sigint_requested = 1;
    shutdown_signal = signo;
    if (children_pgid > 0)
        (void)kill(-children_pgid, SIGTERM);
}

static void restauracja_on_sigtstp(int signo) // handler dla SIGTSTP
{
    (void)signo;
    if (children_pgid > 0)
        (void)kill(-children_pgid, SIGTSTP);

    // Zatrzymaj też proces rodzica. Używamy SIGSTOP (niełapany), żeby nie wołać
    // nie-async-signal-safe funkcji (np. signal()) w handlerze.
    (void)kill(getpid(), SIGSTOP);
}

static void restauracja_on_sigcont(int signo) // handler dla SIGCONT
{
    (void)signo;
    if (children_pgid > 0)
        (void)kill(-children_pgid, SIGCONT);
}

static void zbierz_zombie_nieblokujaco(int *status) // zbiera zakończone procesy potomne
{
    for (;;)
    {
        pid_t p = waitpid(-1, status, WNOHANG); // nieblokująco
        if (p <= 0)
            break;
        LOGI("restauracja: zbierz_zombie_nieblokujaco: pid=%d reaped=%d status=%d\n", (int)getpid(), (int)p, (status ? *status : -1));
    }
}

static int zbierz_zombie_nieblokujaco_licznik(int *status) // zbiera zakończone procesy i zwraca liczbę klientów
{
    int reaped = 0;
    for (;;)
    {
        pid_t p = waitpid(-1, status, WNOHANG); // nieblokująco
        if (p <= 0)
            break;
        LOGI("restauracja: zbierz_zombie_nieblokujaco_licznik: pid=%d reaped=%d\n", (int)getpid(), (int)p);
        if (p != pid_obsluga && p != pid_kucharz && p != pid_kierownik)
            reaped++;
    }
    return reaped;
}

static pid_t uruchom_potomka_exec(const char *file, const char *argv0) // uruchamia proces potomny przez fork()+exec()
{
    pid_t pid = fork();
    if (pid == 0)
    {
        zamknij_odziedziczone_fd_przed_exec();
        execl(file, argv0, arg_shm, arg_sem, arg_msgq, (char *)NULL);
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

static int czy_grupa_procesow_pusta(pid_t pgid) // sprawdza, czy grupa procesów o danym pgid jest pusta
{
    if (pgid <= 0)
        return 1;
    if (kill(-pgid, 0) == -1 && errno == ESRCH)
        return 1;
    return 0;
}

static void zakoncz_wszystkie_dzieci(int *status) // kończy wszystkie procesy potomne w grupie
{
    const int timeout_term = 8;
    const int timeout_kill = 3;

    LOGI("zakoncz_wszystkie_dzieci: pid=%d starting, children_pgid=%d\n", (int)getpid(), (int)children_pgid);
    if (children_pgid > 0)
    {
        LOGI("zakoncz_wszystkie_dzieci: pid=%d sending SIGTERM to -%d\n", (int)getpid(), (int)children_pgid);
        kill(-children_pgid, SIGTERM);
    }

    time_t start = time(NULL);
    while (!czy_grupa_procesow_pusta(children_pgid) && time(NULL) - start < timeout_term)
    {
        LOGI("zakoncz_wszystkie_dzieci: pid=%d waiting for children, elapsed=%ld\n", (int)getpid(), (long)(time(NULL) - start));
        zbierz_zombie_nieblokujaco(status);
        sched_yield();
    }

    if (!czy_grupa_procesow_pusta(children_pgid))
    {
        if (children_pgid > 0)
        {
            LOGI("zakoncz_wszystkie_dzieci: pid=%d sending SIGKILL to -%d\n", (int)getpid(), (int)children_pgid);
            kill(-children_pgid, SIGKILL);
        }

        start = time(NULL);
        while (!czy_grupa_procesow_pusta(children_pgid) && time(NULL) - start < timeout_kill)
        {
            LOGI("zakoncz_wszystkie_dzieci: pid=%d waiting after SIGKILL, elapsed=%ld\n", (int)getpid(), (long)(time(NULL) - start));
            zbierz_zombie_nieblokujaco(status);
            sched_yield();
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        LOGI("zakoncz_wszystkie_dzieci: pid=%d reaping remaining child\n", (int)getpid());
    }
    LOGI("zakoncz_wszystkie_dzieci: pid=%d finished\n", (int)getpid());
}

static pid_t generator_utworz_jedna_grupe(void) // tworzy jedną grupę klientów
{
    pid_t pid = uruchom_potomka_exec("./klient", "klient");
    if (pid < 0)
    {
        if (restauracja_otwarta)
            *restauracja_otwarta = 0;
        return -1;
    }
    if (children_pgid > 0)
        (void)setpgid(pid, children_pgid);
    return pid;
}

static int awaryjne_zamkniecie_fork(void) // sprzątanie przy błędzie fork()
{
    // Błąd fork() nie powinien zostawiać osieroconych procesów/IPC.
    if (restauracja_otwarta)
        *restauracja_otwarta = 0;
    if (kolej_podsumowania)
    {
        *kolej_podsumowania = 1;
        sygnalizuj_ture();
    }

    LOGS("\n===Awaryjne zamknięcie: błąd tworzenia procesu (fork)!===\n");

    // Jeśli cokolwiek już wystartowało (dzieci w grupie), zakończ je.
    {
        int status;
        zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
        zakoncz_wszystkie_dzieci(&status);
    }

    if (shm_id >= 0)
        shmctl(shm_id, IPC_RMID, NULL); // usuń pamięć współdzieloną
    if (sem_id >= 0)
        semctl(sem_id, 0, IPC_RMID); // usuń semafory
    if (msgq_id >= 0)
        msgctl(msgq_id, IPC_RMID, NULL); // usuń kolejkę komunikatów

    fprintf(stderr, "Awaryjne zamknięcie: nie udało się utworzyć procesu (fork).\n");
    return 1;
}

// ====== MAIN ======
int main(void)
{
    zainicjuj_losowosc();

    int czas_pracy = CZAS_PRACY; // domyślny czas pracy restauracji w sekundach
    const char *czas_env = getenv("RESTAURACJA_CZAS_PRACY");
    if (czas_env && *czas_env)
    {
        errno = 0;
        char *end = NULL;
        long v = strtol(czas_env, &end, 10);
        if (errno == 0 && end && *end == '\0' && v > 0)
            czas_pracy = (int)v;
    }

    stworz_ipc();                // tworzy pamięć współdzieloną, semafory i kolejkę komunikatów
    generator_stolikow(stoliki); // generuje stoliki w restauracji
    fflush(stdout);              // opróżnij bufor przed fork() aby uniknąć duplikatów

    // Przekaż shm_id/sem_id/msgq_id do procesów uruchamianych przez exec() jako argumenty.
    snprintf(arg_shm, sizeof(arg_shm), "%d", shm_id);    // przekazywanie jako string do exec()
    snprintf(arg_sem, sizeof(arg_sem), "%d", sem_id);    // przekazywanie jako string do exec()
    snprintf(arg_msgq, sizeof(arg_msgq), "%d", msgq_id); // przekazywanie jako string do exec()

    *restauracja_otwarta = 1; // ustaw flagę otwarcia restauracji
    *kolej_podsumowania = 1;  // ustaw kolej na obsługę
    sygnalizuj_ture();

    pid_obsluga = uruchom_potomka_exec("./obsluga", "obsluga"); // uruchom proces obsługa
    if (pid_obsluga < 0)
        return awaryjne_zamkniecie_fork();
    *pid_obsluga_shm = pid_obsluga;

    // Ustal grupę procesów dla wszystkich potomkow.
    children_pgid = pid_obsluga;
    (void)setpgid(pid_obsluga, children_pgid);

    // Job-control sygnały z terminala (Ctrl+C / Ctrl+Z / Ctrl+\) trafiają do grupy procesu rodzica.
    // Ponieważ dzieci są w osobnej grupie, forwardujemy je, żeby program reagował jak użytkownik oczekuje.
    signal(SIGINT, restauracja_on_sigint);   // handler dla SIGINT/SIGQUIT
    signal(SIGQUIT, restauracja_on_sigint);  // handler dla SIGINT/SIGQUIT
    signal(SIGTSTP, restauracja_on_sigtstp); // handler dla SIGTSTP
    signal(SIGCONT, restauracja_on_sigcont); // handler dla SIGCONT

    pid_kucharz = uruchom_potomka_exec("./kucharz", "kucharz"); // uruchom proces kucharz
    if (pid_kucharz < 0)
        return awaryjne_zamkniecie_fork();
    if (children_pgid > 0)
        (void)setpgid(pid_kucharz, children_pgid);

    pid_kierownik = uruchom_potomka_exec("./kierownik", "kierownik"); // uruchom proces kierownik
    if (pid_kierownik < 0)
        return awaryjne_zamkniecie_fork();
    *pid_kierownik_shm = pid_kierownik;
    if (children_pgid > 0)
        (void)setpgid(pid_kierownik, children_pgid);

    time_t start_czekania = time(NULL);
    int status;

    int max_aktywnych_klientow = 256; // limit aktywnych klientów naraz (można nadpisać env)
    const char *max_env = getenv("RESTAURACJA_MAX_AKTYWNYCH_KLIENTOW");
    if (max_env && *max_env)
    {
        errno = 0;
        char *end = NULL;
        long v = strtol(max_env, &end, 10);
        if (errno == 0 && end && *end == '\0' && v > 0)
            max_aktywnych_klientow = (int)v;
    }

    int aktywni_klienci = 0;
#ifdef TEST_NO_SLEEP
    /* When TEST_NO_SLEEP is enabled, creation is very fast and can
       spawn too many processes for the test environment. Reduce
       the number to avoid resource exhaustion during tests. */
    int liczba_utworzonych_grup = 100; // reduced for TEST_NO_SLEEP
#else
    int liczba_utworzonych_grup = 5000; // liczba grup do utworzenia
#endif
    /* Interval (seconds) between waking the manager via SEM_KIEROWNIK. */
    int kierownik_interval = 30;
    const char *kier_env = getenv("RESTAURACJA_KIEROWNIK_INTERVAL");
    if (kier_env && *kier_env)
    {
        errno = 0;
        char *end = NULL;
        long v = strtol(kier_env, &end, 10);
        if (errno == 0 && end && *end == '\0' && v > 0)
            kierownik_interval = (int)v;
    }
    time_t last_kierownik_post = time(NULL);
    while (liczba_utworzonych_grup-- && !sigint_requested)
    {
        /* Periodically wake the manager to perform its actions. */
        if (kierownik_interval > 0 && time(NULL) - last_kierownik_post >= kierownik_interval)
        {
            sem_operacja(SEM_KIEROWNIK, 1);
            last_kierownik_post = time(NULL);
        }
        aktywni_klienci -= zbierz_zombie_nieblokujaco_licznik(&status);

        while (aktywni_klienci >= max_aktywnych_klientow)
        {
            pid_t p = waitpid(-1, &status, 0);
            if (p > 0)
            {
                if (p != pid_obsluga && p != pid_kucharz && p != pid_kierownik)
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

        pid_t pid = generator_utworz_jedna_grupe();
        if (pid > 0)
            aktywni_klienci++;
        if (liczba_utworzonych_grup == 0)
        {
            printf("Utworzono wszystkie grupy klientów.\n");
        }
    };
    // symulacja pracy restauracji przez 50 sekund (bez sleep)
    {
        time_t sim_start = time(NULL);
        while (time(NULL) - sim_start < 50 && !sigint_requested)
        {
            /* Periodically wake the manager during simulation as well. */
            if (kierownik_interval > 0 && time(NULL) - last_kierownik_post >= kierownik_interval)
            {
                sem_operacja(SEM_KIEROWNIK, 1);
                last_kierownik_post = time(NULL);
            }
            zbierz_zombie_nieblokujaco(&status);
            sched_yield();
        }
    }

    int koniec_czasu = (time(NULL) - start_czekania >= czas_pracy);                        // czy zakończenie z powodu upływu czasu
    int przerwano_sygnalem = sigint_requested;                                             // czy zakończenie z powodu sygnału przerwania
    int zamknieto_flaga = (!koniec_czasu && !przerwano_sygnalem && !*restauracja_otwarta); // czy zakończenie z powodu zamknięcia restauracji flagą

    *restauracja_otwarta = 0; // zamknij restaurację
    if (przerwano_sygnalem)
        LOGS("\n===Przerwano pracę restauracji (%s)!===\n", nazwa_sygnalu((int)shutdown_signal));
    else if (zamknieto_flaga)
        LOGS("\n===Restauracja została zamknięta normalnie!===\n");
    else
        LOGS("\n===Czas pracy restauracji minął!===\n");

    *kolej_podsumowania = 1;
    sygnalizuj_ture();

    LOGI("restauracja: pid=%d initiating shutdown sequence\n", (int)getpid());

    // Dodatkowo: ubij klientów i wyczyść stoliki/kolejkę, żeby nie zostawić
    // osieroconego stanu w pamięci współdzielonej przy przerwaniu.
    LOGI("restauracja: pid=%d calling zakoncz_klientow_i_wyczysc_stoliki_i_kolejke()\n", (int)getpid());
    zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
    LOGI("restauracja: pid=%d returned from zakoncz_klientow...\n", (int)getpid());

    // Prosto: kończymy wszystkie dzieci jednym mechanizmem, niezależnie od typu procesu.
    LOGI("restauracja: pid=%d calling zakoncz_wszystkie_dzieci()\n", (int)getpid());
    zakoncz_wszystkie_dzieci(&status);
    LOGI("restauracja: pid=%d returned from zakoncz_wszystkie_dzieci()\n", (int)getpid());

    LOGI("restauracja: pid=%d removing IPC resources shm=%d sem=%d msgq=%d\n", (int)getpid(), shm_id, sem_id, msgq_id);
    shmctl(shm_id, IPC_RMID, NULL); // usuń pamięć współdzieloną
    semctl(sem_id, 0, IPC_RMID);    // usuń semafory
    if (msgq_id >= 0)               // usuń kolejkę komunikatów
        msgctl(msgq_id, IPC_RMID, NULL);
    LOGI("restauracja: pid=%d IPC resources removed\n", (int)getpid());

    LOGS("Program zakończony.\n");
    return 0;
}