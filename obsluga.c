#include "common.h"

#include <sched.h>
#include <unistd.h>
#include <stdlib.h>

// Per-module context for obsluga
struct ObslugaCtx
{
    volatile sig_atomic_t wydajnosc; // 1=wolno, 2=normalnie, 4=szybko
    volatile sig_atomic_t shutdown_requested;
};

static struct ObslugaCtx obsl_ctx_storage = {.wydajnosc = 2, .shutdown_requested = 0};
static struct ObslugaCtx *obsl_ctx = &obsl_ctx_storage;

// Deklaracje wstępne
static void obsluga_podaj_dania_normalne(double wydajnosc);
static void *watek_specjalne(void *arg);
static void *watek_podsumowanie(void *arg);
static void obsluz_sygnal(int signo);
static void wypisz_podsumowanie(void);

// Wątek obsługi kolejki (usadzanie)
static void *watek_kolejki(void *arg)
{
    (void)arg;
    while (*common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
    {
        struct Grupa g = kolejka_pobierz();
        LOGD("obsluga: pid=%d kolejka_pobierz returned group=%d\n", (int)getpid(),
             g.numer_grupy);
        if (g.numer_grupy != 0)
        {
            int stolik_idx = -1;
            int usadzono = 0;
            int numer_stolika = 0;
            int zajete = 0;
            int pojemnosc = 0;
            int group_num = g.numer_grupy;

            pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
            stolik_idx = znajdz_stolik_dla_grupy_zablokowanej(&g);
            if (stolik_idx >= 0)
            {
                common_ctx->stoliki[stolik_idx].grupy[common_ctx->stoliki[stolik_idx].liczba_grup] = g;
                common_ctx->stoliki[stolik_idx].zajete_miejsca += g.osoby;
                common_ctx->stoliki[stolik_idx].liczba_grup++;
                usadzono = 1;
                numer_stolika = common_ctx->stoliki[stolik_idx].numer_stolika;
                zajete = common_ctx->stoliki[stolik_idx].zajete_miejsca;
                pojemnosc = common_ctx->stoliki[stolik_idx].pojemnosc;
            }
            pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

            if (usadzono)
            {
                LOGP("Grupa usadzona: %d przy stoliku: %d (%d/%d miejsc zajętych)\n",
                     group_num, numer_stolika, zajete, pojemnosc);
                /* Zliczamy osoby (klientów), a nie grupy. */
                (*common_ctx->klienci_przyjeci) += g.osoby;
                if (g.proces_id > 0)
                    (void)kill(g.proces_id, SIGUSR1);
            }

            if (!usadzono && *common_ctx->restauracja_otwarta)
                kolejka_dodaj(g);
        }

        sched_yield();
    }

    return NULL;
}

// Wątek podawania dań normalnych
static void *watek_podawania(void *arg)
{
    (void)arg;
    sig_atomic_t ostatnia_wydajnosc = obsl_ctx->wydajnosc;

    while (*common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
    {
        sig_atomic_t biezaca_wydajnosc = obsl_ctx->wydajnosc;
        if (biezaca_wydajnosc != ostatnia_wydajnosc)
        {
            if (biezaca_wydajnosc >= 4)
                LOGP("Zwiększona wydajność obsługi (SIGUSR1)!\n");
            else if (biezaca_wydajnosc <= 1)
                LOGP("Zmniejszona wydajność obsługi (SIGUSR2)!\n");
            else
                LOGP("Restauracja działa normalnie.\n");
            ostatnia_wydajnosc = biezaca_wydajnosc;
        }

        double wydajnosc = (double)biezaca_wydajnosc;
        obsluga_podaj_dania_normalne(wydajnosc);
        sched_yield();
    }

    return NULL;
}

// Wątek obsługi zamówień specjalnych
static void *watek_specjalne(void *arg)
{
    (void)arg;
    while (*common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
    {
        // Obsługa zamówień specjalnych: najpierw rezerwuj przy stolikach,
        // potem dodaj na taśmę, następnie usuń rezerwacje
        int spec_ceny[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_numer_stolika[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_stolik_idx[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_grupa_idx[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_cnt = 0;

        /* Wait for a change in stoliki (special order placed) or shutdown. */
        if (pthread_mutex_lock(&common_ctx->stoliki_sync->mutex) != 0)
            continue;

        /* Jednorazowe skanowanie: zarezerwuj nowo zgłoszone zamówienia specjalne.
         */
        for (int stolik = 0; stolik < MAX_STOLIKI; stolik++)
        {
            for (int grupa = 0; grupa < common_ctx->stoliki[stolik].liczba_grup; grupa++)
            {
                int cena_specjalna = common_ctx->stoliki[stolik].grupy[grupa].danie_specjalne;
                if (cena_specjalna > 0 &&
                    spec_cnt < (MAX_STOLIKI * MAX_GRUP_NA_STOLIKU))
                {
                    common_ctx->stoliki[stolik].grupy[grupa].danie_specjalne =
                        -cena_specjalna; // reserve
                    spec_ceny[spec_cnt] = cena_specjalna;
                    spec_numer_stolika[spec_cnt] = common_ctx->stoliki[stolik].numer_stolika;
                    spec_stolik_idx[spec_cnt] = stolik;
                    spec_grupa_idx[spec_cnt] = grupa;
                    spec_cnt++;
                }
            }
        }

        if (spec_cnt == 0 && *common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
        {
            /* Brak zamówień specjalnych – poczekaj na sygnał zmiany. */
            (void)pthread_cond_wait(&common_ctx->stoliki_sync->cond, &common_ctx->stoliki_sync->mutex);
            pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
            continue;
        }

        pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

        for (int i = 0; i < spec_cnt; i++)
        {
            pthread_mutex_lock(&common_ctx->tasma_sync->mutex);
            dodaj_danie(common_ctx->tasma, spec_ceny[i]);
            common_ctx->tasma[0].stolik_specjalny = spec_numer_stolika[i];
            int idx = cena_na_indeks(spec_ceny[i]);
            if (idx >= 0)
                common_ctx->kuchnia_dania_wydane[idx]++;
            pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);

            LOGP("Obsługa dodała danie specjalne za %d zł dla stolika %d\n",
                 spec_ceny[i], spec_numer_stolika[i]);
        }

        if (spec_cnt > 0)
        {
            pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
            for (int i = 0; i < spec_cnt; i++)
            {
                int *slot = &common_ctx->stoliki[spec_stolik_idx[i]]
                                 .grupy[spec_grupa_idx[i]]
                                 .danie_specjalne;
                if (*slot == -spec_ceny[i])
                    *slot = 0;
            }
            pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
        }
    }

    return NULL;
}

// Wątek drukujący podsumowanie na końcu
static void *watek_podsumowanie(void *arg)
{
    (void)arg;
    static volatile sig_atomic_t ignore_shutdown = 0;
    static volatile sig_atomic_t already_printed = 0;

    czekaj_na_ture(1, &ignore_shutdown);

    /* Poczekaj na zamknięcie restauracji; użyj stoliki_sync.cond aby uniknąć
     * aktywnego oczekiwania. */
    if (pthread_mutex_lock(&common_ctx->stoliki_sync->mutex) == 0)
    {
        while (*common_ctx->restauracja_otwarta)
            (void)pthread_cond_wait(&common_ctx->stoliki_sync->cond,
                                    &common_ctx->stoliki_sync->mutex);
        pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
    }

    if (already_printed)
        return NULL;

    if (common_ctx->pid_obsluga_shm && *common_ctx->pid_obsluga_shm != getpid())
        return NULL;

    already_printed = 1;
    wypisz_podsumowanie();

    *common_ctx->kolej_podsumowania = 2;
    sygnalizuj_ture();

    fsync(STDOUT_FILENO); // Wymuś zapis wszystkich logów podsumowania
    return NULL;
}

// Obsługa sygnałów
static void obsluz_sygnal(int signo)
{
    switch (signo)
    {
    case SIGUSR1:
        obsl_ctx->wydajnosc = 4;
        break;
    case SIGUSR2:
        obsl_ctx->wydajnosc = 1;
        break;
    case SIGTERM:
        obsl_ctx->shutdown_requested = 1;
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

        pthread_mutex_lock(&common_ctx->tasma_sync->mutex);
        dodaj_danie(common_ctx->tasma, c);
        int idx = cena_na_indeks(c);
        if (idx >= 0)
            common_ctx->kuchnia_dania_wydane[idx]++;
        pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);
    }
}

// Print summary
static void wypisz_podsumowanie(void)
{
    LOGS("\n=== PODSUMOWANIE KASY ===\n");
    int kasa_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        LOGS("Kasa - liczba sprzedanych dań za %d zł: %d\n", CENY_DAN[i],
             common_ctx->kasa_dania_sprzedane[i]);
        kasa_suma += common_ctx->kasa_dania_sprzedane[i] * CENY_DAN[i];
    }
    LOGS("Suma: %d zł\n", kasa_suma);

    LOGS("\n=== PODSUMOWANIE OBSŁUGI ===\n");
    int tasma_dania_niesprzedane[6] = {0};
    pthread_mutex_lock(&common_ctx->tasma_sync->mutex);
    for (int i = 0; i < MAX_TASMA; i++)
    {
        if (common_ctx->tasma[i].cena != 0)
        {
            int idx = cena_na_indeks(common_ctx->tasma[i].cena);
            if (idx >= 0)
                tasma_dania_niesprzedane[idx]++;
        }
    }
    pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);
    int tasma_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        LOGS("Taśma - liczba niesprzedanych dań za %d zł: %d\n", CENY_DAN[i],
             tasma_dania_niesprzedane[i]);
        tasma_suma += tasma_dania_niesprzedane[i] * CENY_DAN[i];
    }
    LOGS("===Suma: %d zł===\n", tasma_suma);
    LOGS("\n\nObsługa kończy pracę.\n");
    LOGS("Klienci w kolejce: %d\n", *common_ctx->klienci_w_kolejce);
    LOGS("======================\n");

    fsync(STDOUT_FILENO); // Ensure all summary logs are flushed
}

// Main service function
void obsluga(void)
{
    if (common_ctx->pid_obsluga_shm)
        *common_ctx->pid_obsluga_shm = getpid();

    zainicjuj_losowosc();
    // Set signal handlers
    if (signal(SIGUSR1, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR1)");
    if (signal(SIGUSR2, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR2)");
    if (signal(SIGTERM, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    ustaw_shutdown_flag(&obsl_ctx->shutdown_requested);

    // Create threads
    pthread_t t_kolejka, t_podawanie, t_specjalne, t_podsumowanie;
    if (pthread_create(&t_kolejka, NULL, watek_kolejki, NULL) != 0)
        LOGE_ERRNO("pthread_create(kolejka)");
    if (pthread_create(&t_podawanie, NULL, watek_podawania, NULL) != 0)
        LOGE_ERRNO("pthread_create(podawanie)");
    if (pthread_create(&t_specjalne, NULL, watek_specjalne, NULL) != 0)
        LOGE_ERRNO("pthread_create(specjalne)");
    if (pthread_create(&t_podsumowanie, NULL, watek_podsumowanie, NULL) != 0)
        LOGE_ERRNO("pthread_create(podsumowanie)");

    // Wait while restaurant is open; use stoliki_sync cond to avoid polling
    if (pthread_mutex_lock(&common_ctx->stoliki_sync->mutex) == 0)
    {
        while (*common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
            (void)pthread_cond_wait(&common_ctx->stoliki_sync->cond,
                                    &common_ctx->stoliki_sync->mutex);
        pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
    }

    // Shutdown threads
    obsl_ctx->shutdown_requested = 1;
    (void)pthread_kill(t_kolejka, SIGTERM);
    (void)pthread_kill(t_podawanie, SIGTERM);
    (void)pthread_kill(t_specjalne, SIGTERM);
    (void)pthread_join(t_kolejka, NULL);
    (void)pthread_join(t_podawanie, NULL);
    (void)pthread_join(t_specjalne, NULL);

    // Wait for summary thread to finish
    (void)pthread_join(t_podsumowanie, NULL);
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
    common_ctx->msgq_id = parsuj_int_lub_zakoncz("msgq_id", argv[3]);
    dolacz_ipc(shm, sem);
    obsluga();
    return 0;
}
