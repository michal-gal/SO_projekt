#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <unistd.h>

static volatile sig_atomic_t shutdown_requested = 0;

static void kierownik_obsluz_sigterm(int signo)
{
    (void)signo;
    shutdown_requested = 1;
}

static void kierownik_zamknij_restauracje_i_zakoncz_klientow(void)
{
    LOGI("\n===Kierownik zamyka restaurację (sam)===\n");
    *restauracja_otwarta = 0;

    zakoncz_klientow_i_wyczysc_stoliki_i_kolejke();
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
                LOGE_ERRNO("kill(SIGUSR1) obsluga");
        }
        LOGI("Kierownik wysyła SIGUSR1 do obsługi (PID %d)\n", pid_obsl);
    }
    else if (v == 2)
    {
        if (pid_obsl > 0)
        {
            if (kill(pid_obsl, SIGUSR2) != 0 && errno != ESRCH)
                LOGE_ERRNO("kill(SIGUSR2) obsluga");
        }
        LOGI("Kierownik wysyła SIGUSR2 do obsługi (PID %d)\n", pid_obsl);
    }
    else if (v == 3)
    {
        kierownik_zamknij_restauracje_i_zakoncz_klientow();
        LOGI("Kierownik zamyka restaurację (bez sygnału do obsługi).\n");
    }
    else
    {
        LOGI("Kierownik: brak sygnału (normalna praca)\n");
    }
}

void kierownik(void)
{
    if (pid_kierownik_shm)
        *pid_kierownik_shm = getpid();

    zainicjuj_losowosc();

    if (signal(SIGTERM, kierownik_obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    while (*restauracja_otwarta && !shutdown_requested)
    {
        kierownik_wyslij_sygnal();
        rest_sleep(1);
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
        LOGE("Użycie: %s <shm_id> <sem_id> <msgq_id>\n", argv[0]);
        return 1;
    }

    int shm = parsuj_int_lub_zakoncz("shm_id", argv[1]);
    int sem = parsuj_int_lub_zakoncz("sem_id", argv[2]);
    msgq_id = parsuj_int_lub_zakoncz("msgq_id", argv[3]);
    dolacz_ipc(shm, sem);
    kierownik();
    return 0;
}
