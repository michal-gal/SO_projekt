#include "common.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t shutdown_requested = 0;

static void obsluz_sigterm(int signo)
{
    (void)signo;
    shutdown_requested = 1;
}

static void drukuj_podsumowanie_kuchni(void)
{
    printf("\n=== PODSUMOWANIE KUCHNI ===\n");
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        printf("Kuchnia - liczba wydanych dań za %d zł: %d\n", CENY_DAN[i], kuchnia_dania_wydane[i]);
        kuchnia_suma += kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    printf("\nSuma: %d zł\n", kuchnia_suma);
}

void kucharz(void)
{
    if (signal(SIGTERM, obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    while (*restauracja_otwarta && !shutdown_requested)
        rest_sleep(1);

    czekaj_na_ture(2);

    drukuj_podsumowanie_kuchni();

    printf("Kucharz kończy pracę.\n");
    printf("======================\n");
    fflush(stdout);

    *kolej_podsumowania = 3;

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
    kucharz();
    return 0;
}
