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

static volatile sig_atomic_t sigint_requested = 0;

static void restauracja_on_sigint(int signo)
{
    (void)signo;
    sigint_requested = 1;
    if (children_pgid > 0)
        (void)kill(-children_pgid, SIGTERM);
}

static void restauracja_on_sigtstp(int signo)
{
    (void)signo;
    if (children_pgid > 0)
        (void)kill(-children_pgid, SIGTSTP);

    // Zatrzymaj też proces rodzica (job control jak przy Ctrl+Z).
    signal(SIGTSTP, SIG_DFL);
    (void)kill(getpid(), SIGTSTP);
}

static void restauracja_on_sigcont(int signo)
{
    (void)signo;
    if (children_pgid > 0)
        (void)kill(-children_pgid, SIGCONT);
}

static void zbierz_zombie_nieblokujaco(int *status)
{
    for (;;)
    {
        pid_t p = waitpid(-1, status, WNOHANG);
        if (p <= 0)
            break;
    }
}

static pid_t uruchom_potomka_exec(const char *file, const char *argv0)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        execl(file, argv0, arg_shm, arg_sem, arg_msgq, (char *)NULL);
        perror("execl");
        _exit(127);
    }
    if (pid < 0)
    {
        perror("fork");
        _exit(1);
    }
    return pid;
}

static int czy_grupa_procesow_pusta(pid_t pgid)
{
    if (pgid <= 0)
        return 1;
    if (kill(-pgid, 0) == -1 && errno == ESRCH)
        return 1;
    return 0;
}

static void zakoncz_wszystkie_dzieci(int *status)
{
    const int timeout_term = 8;
    const int timeout_kill = 3;

    if (children_pgid > 0)
        kill(-children_pgid, SIGTERM);

    time_t start = time(NULL);
    while (!czy_grupa_procesow_pusta(children_pgid) && time(NULL) - start < timeout_term)
    {
        zbierz_zombie_nieblokujaco(status);
        sleep(1);
    }

    if (!czy_grupa_procesow_pusta(children_pgid))
    {
        if (children_pgid > 0)
            kill(-children_pgid, SIGKILL);

        start = time(NULL);
        while (!czy_grupa_procesow_pusta(children_pgid) && time(NULL) - start < timeout_kill)
        {
            zbierz_zombie_nieblokujaco(status);
            sleep(1);
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static void generator_utworz_jedna_grupe(void)
{
    pid_t pid = uruchom_potomka_exec("./klient", "klient");
    if (children_pgid > 0)
        (void)setpgid(pid, children_pgid);
}

// ====== MAIN ======
int main(void)
{
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    stworz_ipc();
    generator_stolikow(stoliki);
    fflush(stdout); // opróżnij bufor przed fork() aby uniknąć duplikatów

    // Przekaż shm_id/sem_id/msgq_id do procesów uruchamianych przez exec() jako argumenty.
    snprintf(arg_shm, sizeof(arg_shm), "%d", shm_id);
    snprintf(arg_sem, sizeof(arg_sem), "%d", sem_id);
    snprintf(arg_msgq, sizeof(arg_msgq), "%d", msgq_id);

    *restauracja_otwarta = 1;
    *kolej_podsumowania = 1;

    pid_obsluga = uruchom_potomka_exec("./obsluga", "obsluga");
    *pid_obsluga_shm = pid_obsluga;

    // Ustal grupę procesów dla wszystkich dzieci.
    children_pgid = pid_obsluga;
    (void)setpgid(pid_obsluga, children_pgid);

    // Job-control sygnały z terminala (Ctrl+C / Ctrl+Z / Ctrl+\) trafiają do grupy procesu rodzica.
    // Ponieważ dzieci są w osobnej grupie, forwardujemy je, żeby program reagował jak użytkownik oczekuje.
    signal(SIGINT, restauracja_on_sigint);
    signal(SIGQUIT, restauracja_on_sigint);
    signal(SIGTSTP, restauracja_on_sigtstp);
    signal(SIGCONT, restauracja_on_sigcont);

    pid_kucharz = uruchom_potomka_exec("./kucharz", "kucharz");
    if (children_pgid > 0)
        (void)setpgid(pid_kucharz, children_pgid);

    pid_kierownik = uruchom_potomka_exec("./kierownik", "kierownik");
    *pid_kierownik_shm = pid_kierownik;
    if (children_pgid > 0)
        (void)setpgid(pid_kierownik, children_pgid);

    time_t start_czekania = time(NULL);
    time_t next_spawn = start_czekania;
    int status;

    while (time(NULL) - start_czekania < CZAS_PRACY && *restauracja_otwarta)
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

        sleep(1);
    }

    *restauracja_otwarta = 0;
    printf("\n===Czas pracy restauracji minął!===\n");
    fflush(stdout);

    *kolej_podsumowania = 1;

    // Prosto: kończymy wszystkie dzieci jednym mechanizmem, niezależnie od typu procesu.
    zakoncz_wszystkie_dzieci(&status);

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    if (msgq_id >= 0)
        msgctl(msgq_id, IPC_RMID, NULL);

    printf("Program zakończony.\n");
    return 0;
}