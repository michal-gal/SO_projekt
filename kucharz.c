#include "common.h"

#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t shutdown_requested = 0; // czy został otrzymany SIGTERM

static void obsluz_sigterm(int signo) // handler dla SIGTERM
{
    (void)signo;
    shutdown_requested = 1;
}

static void drukuj_podsumowanie_kuchni(void) // drukuje podsumowanie kuchni
{
    LOGS("\n=== PODSUMOWANIE KUCHNI ===\n");
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        LOGS("Kuchnia - liczba wydanych dań za %d zł: %d\n", CENY_DAN[i], kuchnia_dania_wydane[i]);
        kuchnia_suma += kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    LOGS("\nSuma: %d zł\n", kuchnia_suma);
}

void kucharz(void) // główna funkcja procesu kucharza
{
    if (signal(SIGTERM, obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    ustaw_shutdown_flag(&shutdown_requested);

    /* Wait for restaurant opening signalled via SEM_TURA instead of busy-waiting.
       The parent sets `*kolej_podsumowania` and calls `sygnalizuj_ture()`
       when the restaurant opens, so consume that semaphore token here. */
    LOGI("kucharz: pid=%d waiting SEM_TURA (open)\n", (int)getpid());
    sem_operacja(SEM_TURA, -1);
    LOGI("kucharz: pid=%d woke SEM_TURA (open)\n", (int)getpid());

    /* After being started, wait for the summary turn (2) or shutdown. */
    czekaj_na_ture(2, &shutdown_requested);

    drukuj_podsumowanie_kuchni();

    LOGS("Kucharz kończy pracę.\n");
    LOGS("======================\n");

    *kolej_podsumowania = 3;
    sygnalizuj_ture();

    exit(0);
}

int main(int argc, char **argv) // punkt wejścia procesu kucharza
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
