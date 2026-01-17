#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <unistd.h>

static void kierownik_zamknij_restauracje_i_zakoncz_klientow(void)
{
    printf("\n===Kierownik zamyka restaurację (sam)===\n");
    *restauracja_otwarta = 0;

    sem_operacja(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        while (stoliki[i].liczba_grup > 0)
        {
            int j = stoliki[i].liczba_grup - 1;
            if (stoliki[i].grupy[j].proces_id != 0)
            {
                pid_t pid = stoliki[i].grupy[j].proces_id;
                printf("Kierownik: zamykanie procesu klienta %d przy stoliku %d\n", pid, i);
                kill(pid, SIGTERM);
                stoliki[i].zajete_miejsca -= stoliki[i].grupy[j].osoby;
            }
            memset(&stoliki[i].grupy[j], 0, sizeof(struct Grupa));
            stoliki[i].liczba_grup--;
        }
        stoliki[i].zajete_miejsca = 0;
    }
    sem_operacja(SEM_STOLIKI, 1);

    // Opróżnij kolejkę klientow czekających na wejście i zakończ ich procesy.
    struct
    {
        long mtype;
        struct Grupa grupa;
    } msg;

    for (;;)
    {
        ssize_t r = msgrcv(msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
        if (r < 0)
        {
            if (errno == ENOMSG)
                break;
            if (errno == EINTR)
                continue;
            break;
        }

        if (msg.grupa.proces_id != 0)
        {
            printf("Kierownik: zamykanie procesu klienta %d z kolejki\n", msg.grupa.proces_id);
            kill(msg.grupa.proces_id, SIGTERM);
        }
    }
}

static void kierownik_wyslij_sygnal(void)
{
    // Używamy sygnałów do komunikacji z obsługą:
    // - SIGUSR1: zwiększ wydajność
    // - SIGUSR2: zmniejsz wydajność
    // - SIGTERM: zamknij restaurację i zakończ klientów (kierownik ubija klientów)
    // Pozostałe wartości = brak akcji (normalna praca).
    int v = rand() % 50;
    pid_t pid_obsl = pid_obsluga_shm ? *pid_obsluga_shm : 0; // Pobierz PID obsługi z pamięci współdzielonej.

    if (v == 1)
    {
        if (pid_obsl > 0)
        {
            if (kill(pid_obsl, SIGUSR1) != 0 && errno != ESRCH)
                perror("kill(SIGUSR1) obsluga");
        }
        printf("Kierownik wysyła SIGUSR1 do obsługi (PID %d)\n", pid_obsl);
    }
    else if (v == 2)
    {
        if (pid_obsl > 0)
        {
            if (kill(pid_obsl, SIGUSR2) != 0 && errno != ESRCH)
                perror("kill(SIGUSR2) obsluga");
        }
        printf("Kierownik wysyła SIGUSR2 do obsługi (PID %d)\n", pid_obsl);
    }
    else if (v == 3)
    {
        kierownik_zamknij_restauracje_i_zakoncz_klientow();
        printf("Kierownik zamyka restaurację (bez sygnału do obsługi).\n");
    }
    else
    {
        printf("Kierownik: brak sygnału (normalna praca)\n");
    }
}

void kierownik(void)
{
    if (pid_kierownik_shm)
        *pid_kierownik_shm = getpid();

    while (*restauracja_otwarta)
    {
        kierownik_wyslij_sygnal();
        sleep(1);
    }

    czekaj_na_ture(3);

    printf("Kierownik kończy pracę.\n");
    fflush(stdout);
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Użycie: %s <shm_id> <sem_id> <msgq_id>\n", argv[0]);
        return 1;
    }

    int shm = parsuj_int_lub_zakoncz("shm_id", argv[1]);
    int sem = parsuj_int_lub_zakoncz("sem_id", argv[2]);
    msgq_id = parsuj_int_lub_zakoncz("msgq_id", argv[3]);
    dolacz_ipc(shm, sem);
    kierownik();
    return 0;
}
