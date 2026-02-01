#include "kucharz.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Kontekst modułu kuchni
struct KucharzCtx
{
    volatile sig_atomic_t shutdown_requested;
};

static struct KucharzCtx kuch_ctx_storage = {.shutdown_requested = 0};
static struct KucharzCtx *kuch_ctx = &kuch_ctx_storage;

// Deklaracje wstępne
static void drukuj_podsumowanie_kuchni(void);
static void czekaj_na_otwarcie_i_podsumowanie(void);
static void dopisz_do_bufora(char *buf, size_t rozmiar, size_t *offset,
                             const char *fmt, ...);

// Drukuj podsumowanie kuchni
static void drukuj_podsumowanie_kuchni(void)
{
    char buf[2048];
    size_t offset = 0;

    dopisz_do_bufora(buf, sizeof(buf), &offset, "\n\n========== PODSUMOWANIE KUCHNI =================\n");
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        dopisz_do_bufora(buf, sizeof(buf), &offset,
                         "Kuchnia - liczba wydanych dań za %d zł: %d\n",
                         CENY_DAN[i], common_ctx->kuchnia_dania_wydane[i]);
        kuchnia_suma += common_ctx->kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    dopisz_do_bufora(buf, sizeof(buf), &offset, "================================================\nSuma: %d zł\n\n", kuchnia_suma);
    dopisz_do_bufora(buf, sizeof(buf), &offset, "Kucharz kończy pracę.\n");
    dopisz_do_bufora(buf, sizeof(buf), &offset, "================================================\n");

    loguj_blokiem('I', buf);

    fsync(STDOUT_FILENO); // Wymuś zapis wszystkich logów podsumowania
}

static void dopisz_do_bufora(char *buf, size_t rozmiar, size_t *offset,
                             const char *fmt, ...)
{
    if (!buf || !offset || *offset >= rozmiar)
        return;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *offset, rozmiar - *offset, fmt, ap);
    va_end(ap);

    if (n <= 0)
        return;

    size_t dodano = (size_t)n;
    if (dodano >= rozmiar - *offset)
        *offset = rozmiar - 1;
    else
        *offset += dodano;
}

// Główna funkcja kucharza
void kucharz(void)
{
    // Ustaw obsługę sygnału
    ustaw_obsluge_sigterm(&kuch_ctx->shutdown_requested);
    ustaw_shutdown_flag(&kuch_ctx->shutdown_requested);
    czekaj_na_otwarcie_i_podsumowanie();
    drukuj_podsumowanie_kuchni();
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
    LOGD("kucharz: pid=%d czeka na turę 1 (otwarcie)\n", (int)getpid());
    czekaj_na_ture(1, &kuch_ctx->shutdown_requested);
    LOGD("kucharz: pid=%d wybudzony na turę 1 (otwarcie)\n", (int)getpid());

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
