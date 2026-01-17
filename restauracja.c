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

static int active_clients_get(void)
{
    int value;
    sem_op(SEM_AKTYWNI_KLIENCI, -1);
    value = *aktywni_klienci;
    sem_op(SEM_AKTYWNI_KLIENCI, 1);
    return value;
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
    // Brak osobnego procesu generatora => pomijamy "turn 0".
    *kolej_podsumowania = 1;

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

    // Generator klientów działa w procesie restauracji (bez osobnego procesu generatora).
    srand(getpid());
    time_t start_czekania = time(NULL);
    time_t next_spawn = start_czekania;
    int status;

    int obsluga_done = 0;
    int kucharz_done = 0;
    int kierownik_done = 0;

    while (time(NULL) - start_czekania < CZAS_PRACY && *restauracja_otwarta)
    {
        // Zbieraj zakończone dzieci (klienci + ewentualnie procesy główne).
        for (;;)
        {
            pid_t p = waitpid(-1, &status, WNOHANG);
            if (p <= 0)
                break;
            if (p == pid_obsluga)
                obsluga_done = 1;
            else if (p == pid_kucharz)
                kucharz_done = 1;
            else if (p == pid_kierownik)
                kierownik_done = 1;
            else
                active_clients_dec_if_positive();
        }

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

    // Obsluga ma czekać na turn 1 po zamknięciu.
    *kolej_podsumowania = 1;

    const pid_t pids[] = {pid_obsluga, pid_kucharz, pid_kierownik};
    const int n_pids = 3;

    // Czekanie na zakończenie wszystkich procesów potomnych z timeoutem
    time_t czas_start = time(NULL);
    int licznik_procesow = 0;

    // Czekamy na 3 główne procesy (obsluga, kucharz, kierownik)
    licznik_procesow = obsluga_done + kucharz_done + kierownik_done;
    while (licznik_procesow < n_pids)
    {
        int proces = waitpid(-1, &status, WNOHANG);

        if (proces > 0)
        {
            if (proces == pid_obsluga && !obsluga_done)
            {
                obsluga_done = 1;
                licznik_procesow++;
                printf("Proces %d zakończył się (%d/%d)\n", proces, licznik_procesow, n_pids);
            }
            else if (proces == pid_kucharz && !kucharz_done)
            {
                kucharz_done = 1;
                licznik_procesow++;
                printf("Proces %d zakończył się (%d/%d)\n", proces, licznik_procesow, n_pids);
            }
            else if (proces == pid_kierownik && !kierownik_done)
            {
                kierownik_done = 1;
                licznik_procesow++;
                printf("Proces %d zakończył się (%d/%d)\n", proces, licznik_procesow, n_pids);
            }
            else
            {
                // klient
                active_clients_dec_if_positive();
            }
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

    // Daj klientom szansę dokończyć i zostać zebranym przez waitpid(), zanim usuniemy IPC.
    // To jest ograniczone czasowo, żeby nie wisieć w nieskończoność przy błędnym liczniku.
    {
        time_t client_wait_start = time(NULL);
        while (active_clients_get() > 0 && time(NULL) - client_wait_start < 5)
        {
            for (;;)
            {
                pid_t p = waitpid(-1, &status, WNOHANG);
                if (p <= 0)
                    break;
                if (p == pid_obsluga)
                    obsluga_done = 1;
                else if (p == pid_kucharz)
                    kucharz_done = 1;
                else if (p == pid_kierownik)
                    kierownik_done = 1;
                else
                    active_clients_dec_if_positive();
            }

            sleep(1);
        }

        int remaining = active_clients_get();
        if (remaining > 0)
            printf("Ostrzeżenie: kończę mimo aktywnych klientów: %d\n", remaining);
    }

    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    if (msgq_id >= 0)
        msgctl(msgq_id, IPC_RMID, NULL);

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    printf("Program zakończony.\n");
    return 0;
}