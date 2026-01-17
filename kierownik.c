#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void kierownik_update_signal(void)
{
    // Używamy sygnałów do komunikacji z obsługą:
    // - SIGUSR1: zwiększ wydajność
    // - SIGUSR2: zmniejsz wydajność
    // - SIGTERM: zamknij restaurację i zakończ klientów (obsługa ubija klientów)
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
        if (pid_obsl > 0)
        {
            if (kill(pid_obsl, SIGTERM) != 0 && errno != ESRCH)
                perror("kill(SIGTERM) obsluga");
        }
        printf("Kierownik zamyka restaurację: SIGTERM do obsługi (PID %d)\n", pid_obsl);
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
        kierownik_update_signal();
        sleep(1);
    }

    wait_for_turn(3);

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

    int shm = parse_int_or_die("shm_id", argv[1]);
    int sem = parse_int_or_die("sem_id", argv[2]);
    msgq_id = parse_int_or_die("msgq_id", argv[3]);
    dolacz_ipc(shm, sem);
    kierownik();
    return 0;
}
