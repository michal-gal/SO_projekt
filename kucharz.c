#include "common.h"

#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Globalna flaga zamknięcia
static volatile sig_atomic_t shutdown_requested = 0;

// Deklaracje wstępne
static void obsluz_sigterm(int signo);
static void drukuj_podsumowanie_kuchni(void);

// Handler sygnału SIGTERM
static void obsluz_sigterm(int signo)
{
    (void)signo;
    shutdown_requested = 1;
}

// Drukuj podsumowanie kuchni
static void drukuj_podsumowanie_kuchni(void)
{
    LOGS("\n=== PODSUMOWANIE KUCHNI ===\n");
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        LOGS("Kuchnia - liczba wydanych dań za %d zł: %d\n", CENY_DAN[i], kuchnia_dania_wydane[i]);
        kuchnia_suma += kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    LOGS("\nSuma: %d zł\n", kuchnia_suma);

        fsync(STDOUT_FILENO); // Wymuś zapis wszystkich logów podsumowania
}

// Główna funkcja kucharza
void kucharz(void)
{
    // Ustaw handler sygnału
    if (signal(SIGTERM, obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    ustaw_shutdown_flag(&shutdown_requested);

    // Czekaj na otwarcie restauracji sygnalizowane przez SEM_TURA zamiast aktywnego czekania.
    // Rodzic ustawia *kolej_podsumowania i wywołuje sygnalizuj_ture()
    // gdy restauracja się otwiera, więc zużyj ten token semafora tutaj.
    LOGD("kucharz: pid=%d waiting SEM_TURA (open)\n", (int)getpid());
    sem_operacja(SEM_TURA, -1);
    LOGD("kucharz: pid=%d woke SEM_TURA (open)\n", (int)getpid());

    // Po uruchomieniu, czekaj na turę podsumowania (2) lub zamknięcie.
    czekaj_na_ture(2, &shutdown_requested);

    drukuj_podsumowanie_kuchni();

    LOGS("Kucharz kończy pracę.\n");
    LOGS("======================\n");

    *kolej_podsumowania = 3;
    sygnalizuj_ture();

    fsync(STDOUT_FILENO); // Wymuś zapis logów
    exit(0);
}

// Główny punkt wejścia
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
    kucharz();
    return 0;
}
