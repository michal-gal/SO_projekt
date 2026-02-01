#include "common.h"

#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/msg.h>

// Per-module context for kierownik
struct KierownikCtx
{
    volatile sig_atomic_t shutdown_requested;
};

static struct KierownikCtx kier_ctx_storage = {.shutdown_requested = 0};
static struct KierownikCtx *kier_ctx = &kier_ctx_storage;

// Deklaracje wstępne
static void kierownik_obsluz_sigterm(int signo);
static void kierownik_wyslij_sygnal(void);
static void kierownik_send_obsluga_signal(pid_t pid_obsl, int signo,
                                          const char *label);

// Handler sygnału SIGTERM
static void kierownik_obsluz_sigterm(int signo)
{
    (void)signo;
    kier_ctx->shutdown_requested = 1;
}

// Wysyłanie sygnału do `obsluga` lub zamknięcie restauracji
static void kierownik_wyslij_sygnal(void)
{
    // Używaj sygnałów do komunikacji z `obsluga`:
    // - SIGUSR1: zwiększ wydajność
    // - SIGUSR2: zmniejsz wydajność
    // - SIGTERM: zamknij restaurację i zakończ klientów
    // Inne wartości = brak akcji (normalne działanie).
    int random_value = rand() % 1000000; // Zmniejsz częstotliwość sygnałów ~10x
    pid_t pid_obsl =
        common_ctx->pid_obsluga_shm
            ? *common_ctx->pid_obsluga_shm
            : 0; // Pobierz PID procesu `obsluga` z pamięci współdzielonej

    if (random_value == 1) // ~0.0001% chance for SIGUSR1
    {
        kierownik_send_obsluga_signal(pid_obsl, SIGUSR1, "kill(SIGUSR1) obsluga");
    }
    else if (random_value == 2) // ~0.0001% chance for SIGUSR2
    {
        kierownik_send_obsluga_signal(pid_obsl, SIGUSR2, "kill(SIGUSR2) obsluga");
    }
    else if (random_value == 3) // ~0.0001% chance to close restaurant
    {
        if (!common_ctx->disable_close)
        {
            kierownik_zamknij_restauracje_i_zakoncz_klientow();
            LOGP("Kierownik zamyka restaurację (bez sygnału do obsługi).\n");
        }
        else
        {
            LOGP("Kierownik: zamykanie wyłączone "
                 "(RESTAURACJA_DISABLE_MANAGER_CLOSE=1)\n");
        }
    }
}

static void kierownik_send_obsluga_signal(pid_t pid_obsl, int signo,
                                          const char *label)
{
    if (pid_obsl <= 0)
        return;
    if (kill(pid_obsl, signo) != 0 && errno != ESRCH)
        LOGE_ERRNO(label);
}

// Main manager function
void kierownik(void)
{
    if (common_ctx->pid_kierownik_shm)
        *common_ctx->pid_kierownik_shm = getpid();

    zainicjuj_losowosc();

    // Ustaw handler sygnału
    if (signal(SIGTERM, kierownik_obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    ustaw_shutdown_flag(&kier_ctx->shutdown_requested);

    // Okresowo wykonuj akcje kierownika po wybudzeniu przez SEM_KIEROWNIK.
    // Restauracja (lub inny kontroler) może zasygnalizować SEM_KIEROWNIK, aby
    // wywołać `kierownik_wyslij_sygnal()`, które wysyła okazjonalne sygnały do
    // `obsluga`.
    while (*common_ctx->restauracja_otwarta && !kier_ctx->shutdown_requested)
    {
        LOGD("kierownik: pid=%d waiting SEM_KIEROWNIK\n", (int)getpid());
        sem_operacja(SEM_KIEROWNIK, -1);
        LOGD("kierownik: pid=%d woke SEM_KIEROWNIK\n", (int)getpid());
        kierownik_wyslij_sygnal();
        if (kier_ctx->shutdown_requested)
            break;
    }

    czekaj_na_ture(3, &kier_ctx->shutdown_requested);

    LOGS("Kierownik kończy pracę.\n");

    fsync(STDOUT_FILENO); // Wymuś zapis logów
    exit(0);
}
// Główny punkt wejścia
int main(int argc, char **argv)
{
    if (dolacz_ipc_z_argv(argc, argv, 0, NULL) != 0)
        return 1;
    kierownik();
    return 0;
}
