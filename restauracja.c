#include "restauracja.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef void (*child_fn)(void);

static pid_t spawn_child(child_fn fn, const char *name)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        fn();
        _exit(0);
    }
    if (pid < 0)
    {
        fprintf(stderr, "fork failed (%s)\n", name);
        perror("fork");
        exit(1);
    }
    return pid;
}

static void signal_if_running(pid_t pid, int sig)
{
    if (pid > 0)
        kill(pid, sig);
}

static const char *name_for_pid(pid_t pid, const pid_t *pids, const char *const *names, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (pid == pids[i])
            return names[i];
    }
    return "nieznany";
}

// ====== MAIN ======
int main(void)
{
    srand(time(NULL));
    stworz_ipc();
    generator_stolikow(stoliki);
    fflush(stdout); // opróżnij bufor przed fork() aby uniknąć duplikatów

    *restauracja_otwarta = 1;

    pid_obsluga = spawn_child(obsluga, "obsługa");
    pid_kucharz = spawn_child(kucharz, "kucharz");
    pid_kierownik = spawn_child(kierownik, "kierownik");
    pid_generator = spawn_child(generator_klientow, "generator");

    // Czekaj maksymalnie CZAS_PRACY lub do sygnału zamknięcia (np. sygnał 3 od kierownika)
    time_t start_czekania = time(NULL);
    while (time(NULL) - start_czekania < CZAS_PRACY && *restauracja_otwarta)
    {
        sleep(1);
    }

    *restauracja_otwarta = 0;
    printf("\n===Czas pracy restauracji minął!===\n");
    fflush(stdout);

    const pid_t pids[] = {pid_obsluga, pid_kucharz, pid_kierownik, pid_generator};
    const char *names[] = {"obsługa", "kucharz", "kierownik", "generator"};
    const int n_pids = 4;

    // Czekanie na zakończenie wszystkich procesów potomnych z timeoutem
    time_t czas_start = time(NULL);
    int status;
    int licznik_procesow = 0;

    // Czekamy na 4 główne procesy (obsluga, kucharz, kierownik, generator)
    while (licznik_procesow < n_pids)
    {
        int proces = waitpid(-1, &status, WNOHANG);

        if (proces > 0)
        {
            licznik_procesow++;
            printf("Proces %d (%s) zakończył się (%d/%d)\n",
                   proces,
                   name_for_pid(proces, pids, names, n_pids),
                   licznik_procesow,
                   n_pids);
        }
        else if (proces == 0)
        {
            sleep(1);
        }
        else if (proces == -1 && errno == ECHILD)
        {
            printf("Wszystkie procesy zakończone.\n");
            break;
        }

        if (time(NULL) - czas_start > 10)
        {
            printf("Timeout! Wymuszanie zakończenia pozostałych procesów...\n");

            for (int i = 0; i < n_pids; i++)
                signal_if_running(pids[i], SIGTERM);

            sleep(3);

            for (int i = 0; i < n_pids; i++)
                signal_if_running(pids[i], SIGKILL);

            break;
        }
    }

    sleep(1);

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    printf("Program zakończony.\n");
    return 0;
}