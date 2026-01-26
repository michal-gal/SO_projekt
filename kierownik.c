#include "common.h"

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <unistd.h>

static volatile sig_atomic_t shutdown_requested = 0;

static void kierownik_obsluz_sigterm(int signo) // handler dla SIGTERM
{
    (void)signo;
    shutdown_requested = 1;
}

static void kierownik_wyslij_sygnal(void) // kierownik wysyła sygnał do obsługi lub zamyka restaurację
{
    // Używamy sygnałów do komunikacji z obsługą:
    // - SIGUSR1: zwiększ wydajność
    // - SIGUSR2: zmniejsz wydajność
    // - SIGTERM: zamknij restaurację i zakończ klientów (kierownik ubija klientów)
    // Pozostałe wartości = brak akcji (normalna praca).
    int v = rand() % 1000000;                                /* make signals ~10x rarer */
    pid_t pid_obsl = pid_obsluga_shm ? *pid_obsluga_shm : 0; // Pobierz PID obsługi z pamięci współdzielonej.

    if (v == 1) //
    {
        if (pid_obsl > 0)
        {
            if (kill(pid_obsl, SIGUSR1) != 0 && errno != ESRCH)
                LOGE_ERRNO("kill(SIGUSR1) obsluga");
        }
        //   LOGI("Kierownik wysyła SIGUSR1 do obsługi (PID %d)\n", pid_obsl);
    }
    else if (v == 2) // 2% szans na SIGUSR2
    {
        if (pid_obsl > 0)
        {
            if (kill(pid_obsl, SIGUSR2) != 0 && errno != ESRCH)
                LOGE_ERRNO("kill(SIGUSR2) obsluga");
        }
        //   LOGI("Kierownik wysyła SIGUSR2 do obsługi (PID %d)\n", pid_obsl);
    }
    // else if (v == 3) // 2% szans na zamknięcie restauracji przez kierownika
    // {
    //     if (!disable_close)
    //     {
    //         kierownik_zamknij_restauracje_i_zakoncz_klientow();
    //         LOGI("Kierownik zamyka restaurację (bez sygnału do obsługi).\n");
    //     }
    //     else
    //     {
    //         LOGI("Kierownik: zamykanie wyłączone (RESTAURACJA_DISABLE_MANAGER_CLOSE=1)\n");
    //     }
    // }
    else
    {
        // Brak akcji
    }
}

void kierownik(void) // główna funkcja procesu kierownika
{
    if (pid_kierownik_shm)
        *pid_kierownik_shm = getpid();

    zainicjuj_losowosc();

    if (signal(SIGTERM, kierownik_obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    ustaw_shutdown_flag(&shutdown_requested);

    /* Periodically perform manager actions when woken via SEM_KIEROWNIK.
       Restauracja (or any controller) may post SEM_KIEROWNIK to trigger
       `kierownik_wyslij_sygnal()` which sends occasional signals to `obsluga`. */
    while (*restauracja_otwarta && !shutdown_requested)
    {
        LOGI("kierownik: pid=%d waiting SEM_KIEROWNIK\n", (int)getpid());
        sem_operacja(SEM_KIEROWNIK, -1);
        LOGI("kierownik: pid=%d woke SEM_KIEROWNIK\n", (int)getpid());
        kierownik_wyslij_sygnal();
        if (shutdown_requested)
            break;
    }

    czekaj_na_ture(3, &shutdown_requested);

    LOGI("Kierownik kończy pracę.\n");
    exit(0);
}

int main(int argc, char **argv) // punkt wejścia procesu kierownika
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
