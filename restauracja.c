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
#include <string.h>
#include <time.h>
#include <unistd.h>

static void active_clients_inc(void)
{
    sem_op(SEM_AKTYWNI_KLIENCI, -1);
    (*aktywni_klienci)++;
    sem_op(SEM_AKTYWNI_KLIENCI, 1);
}

static void active_clients_dec_if_positive(void)
{
    sem_op(SEM_AKTYWNI_KLIENCI, -1);
    if (*aktywni_klienci > 0)
        (*aktywni_klienci)--;
    sem_op(SEM_AKTYWNI_KLIENCI, 1);
}

static void generator_reap_children_nonblocking(void)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        active_clients_dec_if_positive();
}

static void generator_reap_children_blocking(void)
{
    int status;
    while (waitpid(-1, &status, 0) > 0)
        active_clients_dec_if_positive();
}

static void generator_spawn_one_group(void)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        execl("./klient", "klient", (char *)NULL);
        perror("execl ./klient");
        _exit(127);
    }
    if (pid > 0)
    {
        active_clients_inc();
        return;
    }

    perror("fork");
}

void generator_klientow(void)
{
    srand(getpid());

    while (*restauracja_otwarta)
    {
        generator_reap_children_nonblocking();
        generator_spawn_one_group();
        sleep(rand() % 3 + 1);
    }

    generator_reap_children_blocking();

    wait_for_turn(0);

    printf("Generator klientów kończy pracę.\n");
    fflush(stdout);

    *kolej_podsumowania = 1;

    exit(0);
}

// ====== MAIN ======
int main(void)
{
    srand(time(NULL));
    stworz_ipc();
    generator_stolikow(stoliki);
    fflush(stdout); // opróżnij bufor przed fork() aby uniknąć duplikatów

    // Przekaż shm_id/sem_id do procesów uruchamianych przez exec()
    {
        char buf_shm[32];
        char buf_sem[32];
        char buf_msgq[32];
        snprintf(buf_shm, sizeof(buf_shm), "%d", shm_id);
        snprintf(buf_sem, sizeof(buf_sem), "%d", sem_id);
        snprintf(buf_msgq, sizeof(buf_msgq), "%d", msgq_id);
        if (setenv("RESTAURACJA_SHM_ID", buf_shm, 1) != 0)
        {
            perror("setenv RESTAURACJA_SHM_ID");
            return 1;
        }
        if (setenv("RESTAURACJA_SEM_ID", buf_sem, 1) != 0)
        {
            perror("setenv RESTAURACJA_SEM_ID");
            return 1;
        }
        if (setenv("RESTAURACJA_MSGQ_ID", buf_msgq, 1) != 0)
        {
            perror("setenv RESTAURACJA_MSGQ_ID");
            return 1;
        }
    }

    *restauracja_otwarta = 1;

    pid_obsluga = fork();
    if (pid_obsluga == 0)
    {
        execl("./obsluga", "obsluga", (char *)NULL);
        perror("execl ./obsluga");
        _exit(127);
    }
    if (pid_obsluga < 0)
    {
        perror("fork (obsluga)");
        _exit(1);
    }
    *pid_obsluga_shm = pid_obsluga;

    pid_kucharz = fork();
    if (pid_kucharz == 0)
    {
        execl("./kucharz", "kucharz", (char *)NULL);
        perror("execl ./kucharz");
        _exit(127);
    }
    if (pid_kucharz < 0)
    {
        perror("fork (kucharz)");
        _exit(1);
    }

    pid_kierownik = fork();
    if (pid_kierownik == 0)
    {
        execl("./kierownik", "kierownik", (char *)NULL);
        perror("execl ./kierownik");
        _exit(127);
    }
    if (pid_kierownik < 0)
    {
        perror("fork (kierownik)");
        exit(1);
    }
    *pid_kierownik_shm = pid_kierownik;

    pid_generator = fork();
    if (pid_generator == 0)
    {
        generator_klientow();
        _exit(0);
    }
    if (pid_generator < 0)
    {
        perror("fork (generator)");
        exit(1);
    }

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
            printf("Proces %d zakończył się (%d/%d)\n", proces, licznik_procesow, n_pids);
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
            {
                if (pids[i] > 0)
                    kill(pids[i], SIGTERM);
            }

            sleep(3);

            for (int i = 0; i < n_pids; i++)
            {
                if (pids[i] > 0)
                    kill(pids[i], SIGKILL);
            }

            break;
        }
    }

    sleep(1);

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    if (msgq_id >= 0)
        msgctl(msgq_id, IPC_RMID, NULL);

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    printf("Program zakończony.\n");
    return 0;
}