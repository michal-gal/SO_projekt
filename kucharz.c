#include "kucharz.h"

#include <unistd.h>
#include <stdlib.h>

// Per-module context
struct KucharzCtx
{
    volatile sig_atomic_t shutdown_requested;
};

static struct KucharzCtx kuch_ctx_storage = {.shutdown_requested = 0};
static struct KucharzCtx *kuch_ctx = &kuch_ctx_storage;

// Deklaracje wstępne
static void drukuj_podsumowanie_kuchni(void);
static void czekaj_na_otwarcie_i_podsumowanie(void);

// Drukuj podsumowanie kuchni
static void drukuj_podsumowanie_kuchni(void)
{
    LOGS("\n=== PODSUMOWANIE KUCHNI ===\n");
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        LOGS("Kuchnia - liczba wydanych dań za %d zł: %d\n", CENY_DAN[i],
             common_ctx->kuchnia_dania_wydane[i]);
        kuchnia_suma += common_ctx->kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    LOGS("\nSuma: %d zł\n", kuchnia_suma);

    fsync(STDOUT_FILENO); // Wymuś zapis wszystkich logów podsumowania
}

// Główna funkcja kucharza
void kucharz(void)
{
    // Ustaw handler sygnału
    common_install_sigterm_handler(&kuch_ctx->shutdown_requested);

    ustaw_shutdown_flag(&kuch_ctx->shutdown_requested);

    czekaj_na_otwarcie_i_podsumowanie();

    drukuj_podsumowanie_kuchni();

    LOGS("Kucharz kończy pracę.\n");
    LOGS("======================\n");

    sygnalizuj_ture_na(3);
    sem_operacja(SEM_PARENT_NOTIFY3, 1);

    fsync(STDOUT_FILENO); // Wymuś zapis logów
    exit(0);
}

static void czekaj_na_otwarcie_i_podsumowanie(void)
{
    // Czekaj na otwarcie restauracji sygnalizowane przez SEM_TURA zamiast
    // aktywnego czekania. Rodzic wywołuje `sygnalizuj_ture_na(1)` gdy
    // restauracja się otwiera, więc zużyj token semafora tutaj.
    LOGD("kucharz: pid=%d waiting SEM_TURA (open)\n", (int)getpid());
    sem_operacja(SEM_TURA, -1);
    LOGD("kucharz: pid=%d woke SEM_TURA (open)\n", (int)getpid());

    // Po uruchomieniu, czekaj na turę podsumowania (2) lub zamknięcie.
    czekaj_na_ture(2, &kuch_ctx->shutdown_requested);
}

// Główny punkt wejścia
int main(int argc, char **argv)
{
    if (dolacz_ipc_z_argv(argc, argv, 0, NULL) != 0)
        return 1;
    kucharz();
    return 0;
}
