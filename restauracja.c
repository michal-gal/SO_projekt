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

static void reap_zombies_nonblocking(int *status)
{
    for (;;)
    {
        pid_t p = waitpid(-1, status, WNOHANG);
        if (p <= 0)
            break;
    }
}

static pid_t fork_exec_child(const char *file, const char *argv0)
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

static int process_group_empty(pid_t pgid)
{
    if (pgid <= 0)
        return 1;
    if (kill(-pgid, 0) == -1 && errno == ESRCH)
        return 1;
    return 0;
}

static void terminate_all_children(int *status)
{
    const int timeout_term = 8;
    const int timeout_kill = 3;

    if (children_pgid > 0)
        kill(-children_pgid, SIGTERM);

    time_t start = time(NULL);
    while (!process_group_empty(children_pgid) && time(NULL) - start < timeout_term)
    {
        reap_zombies_nonblocking(status);
        sleep(1);
    }

    if (!process_group_empty(children_pgid))
    {
        if (children_pgid > 0)
            kill(-children_pgid, SIGKILL);

        start = time(NULL);
        while (!process_group_empty(children_pgid) && time(NULL) - start < timeout_kill)
        {
            reap_zombies_nonblocking(status);
            sleep(1);
        }
    }

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static void generator_spawn_one_group(void)
{
    pid_t pid = fork_exec_child("./klient", "klient");
    if (children_pgid > 0)
        (void)setpgid(pid, children_pgid);
}

// ====== MAIN ======
int main(void)
{
    srand(time(NULL));
    stworz_ipc();
    generator_stolikow(stoliki);
    fflush(stdout); // opróżnij bufor przed fork() aby uniknąć duplikatów

    // Przekaż shm_id/sem_id/msgq_id do procesów uruchamianych przez exec() jako argumenty.
    snprintf(arg_shm, sizeof(arg_shm), "%d", shm_id);
    snprintf(arg_sem, sizeof(arg_sem), "%d", sem_id);
    snprintf(arg_msgq, sizeof(arg_msgq), "%d", msgq_id);

    *restauracja_otwarta = 1;
    *kolej_podsumowania = 1;

    pid_obsluga = fork_exec_child("./obsluga", "obsluga");
    *pid_obsluga_shm = pid_obsluga;

    // Ustal grupę procesów dla wszystkich dzieci.
    children_pgid = pid_obsluga;
    (void)setpgid(pid_obsluga, children_pgid);

    pid_kucharz = fork_exec_child("./kucharz", "kucharz");
    if (children_pgid > 0)
        (void)setpgid(pid_kucharz, children_pgid);

    pid_kierownik = fork_exec_child("./kierownik", "kierownik");
    *pid_kierownik_shm = pid_kierownik;
    if (children_pgid > 0)
        (void)setpgid(pid_kierownik, children_pgid);

    srand(getpid());
    time_t start_czekania = time(NULL);
    time_t next_spawn = start_czekania;
    int status;

    while (time(NULL) - start_czekania < CZAS_PRACY && *restauracja_otwarta)
    {
        // Zbieraj zakończone dzieci (klienci + ewentualnie procesy główne).
        reap_zombies_nonblocking(&status);

        time_t now = time(NULL);
        if (now >= next_spawn)
        {
            generator_spawn_one_group();
            next_spawn = now + (rand() % 3 + 1);
        }

        sleep(1);
    }

    *restauracja_otwarta = 0;
    printf("\n===Czas pracy restauracji minął!===\n");
    fflush(stdout);

    *kolej_podsumowania = 1; //

    // Prosto: kończymy wszystkie dzieci jednym mechanizmem, niezależnie od typu procesu.
    terminate_all_children(&status);

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    if (msgq_id >= 0)
        msgctl(msgq_id, IPC_RMID, NULL);

    printf("Program zakończony.\n");
    return 0;
}