#include "common.h"

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/msg.h>

// Globalna flaga zamknięcia
static volatile sig_atomic_t shutdown_requested = 0;

// Deklaracje wstępne
static void kierownik_obsluz_sigterm(int signo);
static void kierownik_wyslij_sygnal(void);

// Handler sygnału SIGTERM
static void kierownik_obsluz_sigterm(int signo) {
  (void)signo;
  shutdown_requested = 1;
}

// Wysyłanie sygnału do `obsluga` lub zamknięcie restauracji
static void kierownik_wyslij_sygnal(void) {
  // Używaj sygnałów do komunikacji z `obsluga`:
  // - SIGUSR1: zwiększ wydajność
  // - SIGUSR2: zmniejsz wydajność
  // - SIGTERM: zamknij restaurację i zakończ klientów
  // Inne wartości = brak akcji (normalne działanie).
  int random_value = rand() % 1000000; // Zmniejsz częstotliwość sygnałów ~10x
  pid_t pid_obsl =
      pid_obsluga_shm
          ? *pid_obsluga_shm
          : 0; // Pobierz PID procesu `obsluga` z pamięci współdzielonej

  if (random_value == 1) // ~0.0001% chance for SIGUSR1
  {
    if (pid_obsl > 0) {
      if (kill(pid_obsl, SIGUSR1) != 0 && errno != ESRCH)
        LOGE_ERRNO("kill(SIGUSR1) obsluga");
    }
    // LOGI("Kierownik wysyła SIGUSR1 do obsługi (PID %d)\n", pid_obsl);
  } else if (random_value == 2) // ~0.0001% chance for SIGUSR2
  {
    if (pid_obsl > 0) {
      if (kill(pid_obsl, SIGUSR2) != 0 && errno != ESRCH)
        LOGE_ERRNO("kill(SIGUSR2) obsluga");
    }
    // LOGI("Kierownik wysyła SIGUSR2 do obsługi (PID %d)\n", pid_obsl);
  } else if (random_value == 3) // ~0.0001% chance to close restaurant
  {
    if (!disable_close) {
      kierownik_zamknij_restauracje_i_zakoncz_klientow();
      LOGP("Kierownik zamyka restaurację (bez sygnału do obsługi).\n");
    } else {
      LOGP("Kierownik: zamykanie wyłączone "
           "(RESTAURACJA_DISABLE_MANAGER_CLOSE=1)\n");
    }
  }
  // // }
  // else
  {
    // No action
  }
}

// Main manager function
void kierownik(void) {
  if (pid_kierownik_shm)
    *pid_kierownik_shm = getpid();

  zainicjuj_losowosc();

  // Ustaw handler sygnału
  if (signal(SIGTERM, kierownik_obsluz_sigterm) == SIG_ERR)
    LOGE_ERRNO("signal(SIGTERM)");

  ustaw_shutdown_flag(&shutdown_requested);

  // Okresowo wykonuj akcje kierownika po wybudzeniu przez SEM_KIEROWNIK.
  // Restauracja (lub inny kontroler) może zasygnalizować SEM_KIEROWNIK, aby
  // wywołać `kierownik_wyslij_sygnal()`, które wysyła okazjonalne sygnały do
  // `obsluga`.
  while (*restauracja_otwarta && !shutdown_requested) {
    LOGD("kierownik: pid=%d waiting SEM_KIEROWNIK\n", (int)getpid());
    sem_operacja(SEM_KIEROWNIK, -1);
    LOGD("kierownik: pid=%d woke SEM_KIEROWNIK\n", (int)getpid());
    kierownik_wyslij_sygnal();
    if (shutdown_requested)
      break;
  }

  czekaj_na_ture(3, &shutdown_requested);

  LOGS("Kierownik kończy pracę.\n");

  fsync(STDOUT_FILENO); // Wymuś zapis logów
  exit(0);
}
// Główny punkt wejścia
int main(int argc, char **argv) {
  if (argc != 4) {
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
