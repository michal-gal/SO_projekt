#include "restauracja.h"

#include <errno.h>
#include <signal.h>
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

static const char *nazwa_sygnalu(int signo)
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
        pid_t p = waitpid(-1, status, WNOHANG);
        if (p <= 0)
            break;
    }
}

static pid_t uruchom_potomka_exec(const char *file, const char *argv0) // uruchamia proces potomny przez fork()+exec()
{
    pid_t pid = fork();
    if (pid == 0)
    {
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

    if (children_pgid > 0)
        kill(-children_pgid, SIGTERM);

    time_t start = time(NULL);
    while (!czy_grupa_procesow_pusta(children_pgid) && time(NULL) - start < timeout_term)
    {
        zbierz_zombie_nieblokujaco(status);
        rest_sleep(1);
    }

    if (!czy_grupa_procesow_pusta(children_pgid))
    {
        if (children_pgid > 0)
            kill(-children_pgid, SIGKILL);

        start = time(NULL);
        while (!czy_grupa_procesow_pusta(children_pgid) && time(NULL) - start < timeout_kill)
        {
            zbierz_zombie_nieblokujaco(status);
            rest_sleep(1);
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static void generator_utworz_jedna_grupe(void) // tworzy jedną grupę klientów
{
    pid_t pid = uruchom_potomka_exec("./klient", "klient");
    if (pid < 0)
    {
        if (restauracja_otwarta)
            *restauracja_otwarta = 0;
        return;
    }
    if (children_pgid > 0)
        (void)setpgid(pid, children_pgid);
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

    pid_obsluga = uruchom_potomka_exec("./obsluga", "obsluga"); // uruchom proces obsługa
    if (pid_obsluga < 0)
        goto awaryjne_zamkniecie;
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
        goto awaryjne_zamkniecie;
    if (children_pgid > 0)
        (void)setpgid(pid_kucharz, children_pgid);

    pid_kierownik = uruchom_potomka_exec("./kierownik", "kierownik"); // uruchom proces kierownik
    if (pid_kierownik < 0)
        goto awaryjne_zamkniecie;
    *pid_kierownik_shm = pid_kierownik;
    if (children_pgid > 0)
        (void)setpgid(pid_kierownik, children_pgid);

    time_t start_czekania = time(NULL);
    time_t next_spawn = start_czekania;
    int status;

    while (time(NULL) - start_czekania < czas_pracy && *restauracja_otwarta) // główna pętla pracy restauracji
    {
        if (sigint_requested)
        {
            *restauracja_otwarta = 0;
            break;
        }

        // Zbieraj zakończone dzieci (klienci + ewentualnie procesy główne).
        zbierz_zombie_nieblokujaco(&status);

        time_t now = time(NULL);
        if (now >= next_spawn)
        {
            generator_utworz_jedna_grupe();
            next_spawn = now + (rand() % 3 + 1);
        }

        rest_sleep(1);
    }

    int koniec_czasu = (time(NULL) - start_czekania >= czas_pracy);                        // czy zakończenie z powodu upływu czasu
    int przerwano_sygnalem = sigint_requested;                                             // czy zakończenie z powodu sygnału przerwania
    int zamknieto_flaga = (!koniec_czasu && !przerwano_sygnalem && !*restauracja_otwarta); // czy zakończenie z powodu zamknięcia restauracji flagą

    *restauracja_otwarta = 0; // zamknij restaurację
    if (przerwano_sygnalem)
        printf("\n===Przerwano pracę restauracji (%s)!===\n", nazwa_sygnalu((int)shutdown_signal));
    else if (zamknieto_flaga)
        printf("\n===Restauracja została zamknięta normalnie!===\n");
    else
        printf("\n===Czas pracy restauracji minął!===\n");
    fflush(stdout);

    *kolej_podsumowania = 1;

    // Dodatkowo: ubij klientów i wyczyść stoliki/kolejkę, żeby nie zostawić
    // osieroconego stanu w pamięci współdzielonej przy przerwaniu.
    zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();

    // Prosto: kończymy wszystkie dzieci jednym mechanizmem, niezależnie od typu procesu.
    zakoncz_wszystkie_dzieci(&status);

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    if (msgq_id >= 0)
        msgctl(msgq_id, IPC_RMID, NULL);

    printf("Program zakończony.\n");
    return 0;

awaryjne_zamkniecie:
    // Błąd fork() nie powinien zostawiać osieroconych procesów/IPC.
    if (restauracja_otwarta)
        *restauracja_otwarta = 0;
    if (kolej_podsumowania)
        *kolej_podsumowania = 1;

    printf("\n===Awaryjne zamknięcie: błąd tworzenia procesu (fork)!===\n");
    fflush(stdout);

    // Jeśli cokolwiek już wystartowało (dzieci w grupie), zakończ je.
    {
        int status;
        zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
        zakoncz_wszystkie_dzieci(&status);
    }

    if (shm_id >= 0)
        shmctl(shm_id, IPC_RMID, NULL);
    if (sem_id >= 0)
        semctl(sem_id, 0, IPC_RMID);
    if (msgq_id >= 0)
        msgctl(msgq_id, IPC_RMID, NULL);

    fprintf(stderr, "Awaryjne zamknięcie: nie udało się utworzyć procesu (fork).\n");
    return 1;
}