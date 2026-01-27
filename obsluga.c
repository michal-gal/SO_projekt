#include "common.h"

#include <sched.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

// Global variables for performance and shutdown
static volatile sig_atomic_t obsluga_wydajnosc = 2; // 1=slow, 2=normal, 4=fast
static volatile sig_atomic_t shutdown_requested = 0;

// Forward declarations
static void obsluga_podaj_dania_normalne(double wydajnosc);
static void *watek_specjalne(void *arg);
static void obsluz_sygnal(int signo);
static void wypisz_podsumowanie(void);

// Thread for handling queue seating
static void *watek_kolejki(void *arg)
{
    (void)arg;
    while (*restauracja_otwarta && !shutdown_requested)
    {
        struct Grupa g = kolejka_pobierz();
        LOGD("obsluga: pid=%d kolejka_pobierz returned group=%d\n", (int)getpid(), g.numer_grupy);
        if (g.numer_grupy != 0)
        {
            int stolik_idx = -1;
            int usadzono = 0;
            int numer_stolika = 0;
            int zajete = 0;
            int pojemnosc = 0;
            int group_num = g.numer_grupy;

            pthread_mutex_lock(&stoliki_sync->mutex);
            stolik_idx = znajdz_stolik_dla_grupy_zablokowanej(&g);
            if (stolik_idx >= 0)
            {
                stoliki[stolik_idx].grupy[stoliki[stolik_idx].liczba_grup] = g;
                stoliki[stolik_idx].zajete_miejsca += g.osoby;
                stoliki[stolik_idx].liczba_grup++;
                usadzono = 1;
                numer_stolika = stoliki[stolik_idx].numer_stolika;
                zajete = stoliki[stolik_idx].zajete_miejsca;
                pojemnosc = stoliki[stolik_idx].pojemnosc;
            }
            pthread_mutex_unlock(&stoliki_sync->mutex);

            if (usadzono)
            {
                LOGS("Grupa usadzona: %d przy stoliku: %d (%d/%d miejsc zajętych)\n",
                     group_num, numer_stolika, zajete, pojemnosc);
                (*klienci_przyjeci)++;
                if (g.proces_id > 0)
                    (void)kill(g.proces_id, SIGUSR1);
            }

            if (!usadzono && *restauracja_otwarta)
                kolejka_dodaj(g);
        }

        sched_yield();
    }

    return NULL;
}

// Thread for serving normal dishes
static void *watek_podawania(void *arg)
{
    (void)arg;
    sig_atomic_t ostatnia_wydajnosc = obsluga_wydajnosc;

    while (*restauracja_otwarta && !shutdown_requested)
    {
        sig_atomic_t biezaca_wydajnosc = obsluga_wydajnosc;
        if (biezaca_wydajnosc != ostatnia_wydajnosc)
        {
            if (biezaca_wydajnosc >= 4)
                LOGS("Zwiększona wydajność obsługi (SIGUSR1)!\n");
            else if (biezaca_wydajnosc <= 1)
                LOGS("Zmniejszona wydajność obsługi (SIGUSR2)!\n");
            else
                LOGS("Restauracja działa normalnie.\n");
            ostatnia_wydajnosc = biezaca_wydajnosc;
        }

        double wydajnosc = (double)biezaca_wydajnosc;
        obsluga_podaj_dania_normalne(wydajnosc);
        sched_yield();
    }

    return NULL;
}

// Thread for handling special orders
static void *watek_specjalne(void *arg)
{
    (void)arg;
    while (*restauracja_otwarta && !shutdown_requested)
    {
        // Handle special orders: reserve in tables first, then add to belt, then clear reservations
        int spec_ceny[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_numer_stolika[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_stolik_idx[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_grupa_idx[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_cnt = 0;

        pthread_mutex_lock(&stoliki_sync->mutex);
        for (int stolik = 0; stolik < MAX_STOLIKI; stolik++)
        {
            for (int grupa = 0; grupa < stoliki[stolik].liczba_grup; grupa++)
            {
                int cena_specjalna = stoliki[stolik].grupy[grupa].danie_specjalne;
                if (cena_specjalna > 0 && spec_cnt < (MAX_STOLIKI * MAX_GRUP_NA_STOLIKU))
                {
                    stoliki[stolik].grupy[grupa].danie_specjalne = -cena_specjalna; // reserve
                    spec_ceny[spec_cnt] = cena_specjalna;
                    spec_numer_stolika[spec_cnt] = stoliki[stolik].numer_stolika;
                    spec_stolik_idx[spec_cnt] = stolik;
                    spec_grupa_idx[spec_cnt] = grupa;
                    spec_cnt++;
                }
            }
        }
        pthread_mutex_unlock(&stoliki_sync->mutex);

        for (int i = 0; i < spec_cnt; i++)
        {
            pthread_mutex_lock(&tasma_sync->mutex);
            dodaj_danie(tasma, spec_ceny[i]);
            tasma[0].stolik_specjalny = spec_numer_stolika[i];
            int idx = cena_na_indeks(spec_ceny[i]);
            if (idx >= 0)
                kuchnia_dania_wydane[idx]++;
            pthread_mutex_unlock(&tasma_sync->mutex);

            LOGS("Obsługa dodała danie specjalne za %d zł dla stolika %d\n",
                 spec_ceny[i], spec_numer_stolika[i]);
        }

        if (spec_cnt > 0)
        {
            pthread_mutex_lock(&stoliki_sync->mutex);
            for (int i = 0; i < spec_cnt; i++)
            {
                int *slot = &stoliki[spec_stolik_idx[i]].grupy[spec_grupa_idx[i]].danie_specjalne;
                if (*slot == -spec_ceny[i])
                    *slot = 0;
            }
            pthread_mutex_unlock(&stoliki_sync->mutex);
        }

        struct timespec req = {.tv_sec = 0, .tv_nsec = 50 * 1000 * 1000}; // 50ms
        (void)rest_nanosleep(&req, NULL);
    }

    return NULL;
}

// Signal handler
static void obsluz_sygnal(int signo)
{
    switch (signo)
    {
    case SIGUSR1:
        obsluga_wydajnosc = 4;
        break;
    case SIGUSR2:
        obsluga_wydajnosc = 1;
        break;
    case SIGTERM:
        shutdown_requested = 1;
        break;
    default:
        break;
    }
}

// Serve normal dishes
static void obsluga_podaj_dania_normalne(double wydajnosc)
{
    int serves = (int)wydajnosc;
    for (int iter = 0; iter < serves; iter++)
    {
        int ceny[] = {p10, p15, p20};
        int c = ceny[rand() % 3];

        pthread_mutex_lock(&tasma_sync->mutex);
        dodaj_danie(tasma, c);
        int idx = cena_na_indeks(c);
        if (idx >= 0)
            kuchnia_dania_wydane[idx]++;
        pthread_mutex_unlock(&tasma_sync->mutex);

        LOGD("Danie za %d zł dodane na taśmę.\n", c);
    }
}

// Print summary
static void wypisz_podsumowanie(void)
{
    LOGS("\n=== PODSUMOWANIE KASY ===\n");
    int kasa_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        LOGS("Kasa - liczba sprzedanych dań za %d zł: %d\n", CENY_DAN[i], kasa_dania_sprzedane[i]);
        kasa_suma += kasa_dania_sprzedane[i] * CENY_DAN[i];
    }
    LOGS("Suma: %d zł\n", kasa_suma);

    LOGS("\n=== PODSUMOWANIE OBSŁUGI ===\n");
    int tasma_dania_niesprzedane[6] = {0};
    pthread_mutex_lock(&tasma_sync->mutex);
    for (int i = 0; i < MAX_TASMA; i++)
    {
        if (tasma[i].cena != 0)
        {
            int idx = cena_na_indeks(tasma[i].cena);
            if (idx >= 0)
                tasma_dania_niesprzedane[idx]++;
        }
    }
    pthread_mutex_unlock(&tasma_sync->mutex);
    int tasma_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        LOGS("Taśma - liczba niesprzedanych dań za %d zł: %d\n", CENY_DAN[i], tasma_dania_niesprzedane[i]);
        tasma_suma += tasma_dania_niesprzedane[i] * CENY_DAN[i];
    }
    LOGS("===Suma: %d zł===\n", tasma_suma);

    LOGS("\n\nObsługa kończy pracę.\n");
    LOGS("======================\n");
}

// Main service function
void obsluga(void)
{
    if (pid_obsluga_shm)
        *pid_obsluga_shm = getpid();

    zainicjuj_losowosc();
    // Set signal handlers
    if (signal(SIGUSR1, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR1)");
    if (signal(SIGUSR2, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR2)");
    if (signal(SIGTERM, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    ustaw_shutdown_flag(&shutdown_requested);

    // Create threads
    pthread_t t_kolejka, t_podawanie, t_specjalne;
    if (pthread_create(&t_kolejka, NULL, watek_kolejki, NULL) != 0)
        LOGE_ERRNO("pthread_create(kolejka)");
    if (pthread_create(&t_podawanie, NULL, watek_podawania, NULL) != 0)
        LOGE_ERRNO("pthread_create(podawanie)");
    if (pthread_create(&t_specjalne, NULL, watek_specjalne, NULL) != 0)
        LOGE_ERRNO("pthread_create(specjalne)");

    // Wait while restaurant is open
    while (*restauracja_otwarta && !shutdown_requested)
    {
        struct timespec req = {.tv_sec = 0, .tv_nsec = 50 * 1000 * 1000}; // 50ms
        (void)rest_nanosleep(&req, NULL);
    }

    // Shutdown threads
    shutdown_requested = 1;
    (void)pthread_kill(t_kolejka, SIGTERM);
    (void)pthread_kill(t_podawanie, SIGTERM);
    (void)pthread_kill(t_specjalne, SIGTERM);
    (void)pthread_join(t_kolejka, NULL);
    (void)pthread_join(t_podawanie, NULL);
    (void)pthread_join(t_specjalne, NULL);

    // Wait for all groups to finish
    czekaj_na_ture(1, &shutdown_requested);

    // Print summary
    wypisz_podsumowanie();

    *kolej_podsumowania = 2;
    sygnalizuj_ture();

    exit(0);
}

// Main entry point
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
    obsluga();
    return 0;
}
