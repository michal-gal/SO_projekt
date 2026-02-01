#include "obsluga.h"
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Kontekst modułu obsługi
struct ObslugaCtx
{
    volatile sig_atomic_t wydajnosc; // 1=wolno, 2=normalnie, 4=szybko
    volatile sig_atomic_t shutdown_requested;
};

static struct ObslugaCtx obsl_ctx_storage = {.wydajnosc = 2, .shutdown_requested = 0};
static struct ObslugaCtx *obsl_ctx = &obsl_ctx_storage;

struct SpecOrder
{
    int cena;
    int numer_stolika;
    int stolik_idx;
    int grupa_idx;
};

// Deklaracje wstępne
static void obsluga_podaj_dania_normalne(double wydajnosc);
static void *watek_specjalne(void *arg);
static void *watek_podsumowanie(void *arg);
static void obsluz_sygnal(int signo);
static void wypisz_podsumowanie(void);
static int zbierz_zamowienia_specjalne(struct SpecOrder *orders, int max);
static void wyczysc_rezerwacje_specjalne(const struct SpecOrder *orders,
                                         int count);
static void dodaj_danie(struct Talerzyk *tasma_local, int cena);
static void dopisz_do_bufora(char *buf, size_t rozmiar, size_t *offset,
                             const char *fmt, ...);

// Wątek podawania dań zwykłych
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

static void dodaj_danie(struct Talerzyk *tasma_local, int cena)
{
    while (common_ctx->tasma_sync->count >= MAX_TASMA)
    {
        (void)pthread_cond_wait(&common_ctx->tasma_sync->not_full,
                                &common_ctx->tasma_sync->mutex);
    }

    do
    {
        struct Talerzyk ostatni = tasma_local[MAX_TASMA - 1];

        for (int i = MAX_TASMA - 1; i > 0; i--)
        {
            tasma_local[i] = tasma_local[i - 1];
        }

        tasma_local[0] = ostatni; // WRACA NA POCZĄTEK
    } while (tasma_local[0].cena != 0);

    tasma_local[0].cena = cena;
    tasma_local[0].stolik_specjalny = 0;
    common_ctx->tasma_sync->count++;
    pthread_cond_signal(&common_ctx->tasma_sync->not_empty);
    LOGD("dodaj_danie: wydano danie za %d zł na taśmę (count=%d)\n", cena,
         common_ctx->tasma_sync->count);
}

// Wątek obsługi zamówień specjalnych
static void *watek_specjalne(void *arg)
{
    (void)arg;
    while (*common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
    {
        struct SpecOrder orders[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int count = zbierz_zamowienia_specjalne(orders,
                                                MAX_STOLIKI * MAX_GRUP_NA_STOLIKU);

        if (count == 0)
            continue;

        for (int i = 0; i < count; i++)
        {
            pthread_mutex_lock(&common_ctx->tasma_sync->mutex);
            dodaj_danie(common_ctx->tasma, orders[i].cena);
            common_ctx->tasma[0].stolik_specjalny = orders[i].numer_stolika;
            int idx = cena_na_indeks(orders[i].cena);
            if (idx >= 0)
                common_ctx->kuchnia_dania_wydane[idx]++;
            pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);

            LOGP("Obsługa dodała danie specjalne za %d zł dla stolika %d\n",
                 orders[i].cena, orders[i].numer_stolika);
        }

        wyczysc_rezerwacje_specjalne(orders, count);
    }

    return NULL;
}

static int zbierz_zamowienia_specjalne(struct SpecOrder *orders, int max)
{
    int count = 0;
    if (pthread_mutex_lock(&common_ctx->stoliki_sync->mutex) != 0)
        return 0;

    for (int stolik = 0; stolik < MAX_STOLIKI; stolik++)
    {
        for (int grupa = 0; grupa < common_ctx->stoliki[stolik].liczba_grup; grupa++)
        {
            int cena_specjalna = common_ctx->stoliki[stolik].grupy[grupa].danie_specjalne;
            if (cena_specjalna > 0 && count < max)
            {
                common_ctx->stoliki[stolik].grupy[grupa].danie_specjalne =
                    -cena_specjalna; // reserve
                orders[count].cena = cena_specjalna;
                orders[count].numer_stolika = common_ctx->stoliki[stolik].numer_stolika;
                orders[count].stolik_idx = stolik;
                orders[count].grupa_idx = grupa;
                count++;
            }
        }
    }

    if (count == 0 && *common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
        (void)pthread_cond_wait(&common_ctx->stoliki_sync->cond,
                                &common_ctx->stoliki_sync->mutex);

    pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
    return count;
}

static void wyczysc_rezerwacje_specjalne(const struct SpecOrder *orders,
                                         int count)
{
    if (count <= 0)
        return;

    pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
    for (int i = 0; i < count; i++)
    {
        int *slot = &common_ctx->stoliki[orders[i].stolik_idx]
                         .grupy[orders[i].grupa_idx]
                         .danie_specjalne;
        if (*slot == -orders[i].cena)
            *slot = 0;
    }
    pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
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

    sygnalizuj_ture_na(2);
    sem_operacja(SEM_PARENT_NOTIFY2, 1);

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
    default:
        break;
    }
}

// Podawanie dań zwykłych
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

// Wydruk podsumowania
static void wypisz_podsumowanie(void)
{
    char buf[4096];
    size_t offset = 0;

    dopisz_do_bufora(buf, sizeof(buf), &offset, "\n\n\n=========== PODSUMOWANIE KASY ==================\n");
    int kasa_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        dopisz_do_bufora(buf, sizeof(buf), &offset,
                         "Kasa - liczba sprzedanych dań za %d zł: %d\n",
                         CENY_DAN[i], common_ctx->kasa_dania_sprzedane[i]);
        kasa_suma += common_ctx->kasa_dania_sprzedane[i] * CENY_DAN[i];
    }
    dopisz_do_bufora(buf, sizeof(buf), &offset, "================================================\nSuma: %d zł\n", kasa_suma);

    dopisz_do_bufora(buf, sizeof(buf), &offset,
                     "\n=========== PODSUMOWANIE OBSŁUGI ===============\n");
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
        dopisz_do_bufora(buf, sizeof(buf), &offset,
                         "Taśma - liczba niesprzedanych dań za %d zł: %d\n",
                         CENY_DAN[i], tasma_dania_niesprzedane[i]);
        tasma_suma += tasma_dania_niesprzedane[i] * CENY_DAN[i];
    }
    dopisz_do_bufora(buf, sizeof(buf), &offset, "================================================\nSuma: %d zł\n",
                     tasma_suma);
    dopisz_do_bufora(buf, sizeof(buf), &offset,
                     "\n================================================\nObsługa kończy pracę.\n");
    dopisz_do_bufora(buf, sizeof(buf), &offset, "Nieobsłużeni klienci czekający w kolejce: %d\n",
                     *common_ctx->klienci_w_kolejce);
    dopisz_do_bufora(buf, sizeof(buf), &offset, "================================================\n");

    loguj_blokiem('I', buf);

    fsync(STDOUT_FILENO); // Dopilnuj, aby wszystkie logi podsumowania się zapisały
}

// Główna funkcja obsługi
void obsluga(void)
{
    if (common_ctx->pid_obsluga_shm)
        *common_ctx->pid_obsluga_shm = getpid();

    zainicjuj_losowosc();
    // Ustaw obsługę sygnałów
    if (signal(SIGUSR1, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR1)");
    if (signal(SIGUSR2, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR2)");
    ustaw_obsluge_sigterm(&obsl_ctx->shutdown_requested);

    ustaw_shutdown_flag(&obsl_ctx->shutdown_requested);

    // Utwórz wątki
    pthread_t t_podawanie, t_specjalne, t_podsumowanie;
    if (pthread_create(&t_podawanie, NULL, watek_podawania, NULL) != 0)
        LOGE_ERRNO("pthread_create(podawanie)");
    if (pthread_create(&t_specjalne, NULL, watek_specjalne, NULL) != 0)
        LOGE_ERRNO("pthread_create(specjalne)");
    if (pthread_create(&t_podsumowanie, NULL, watek_podsumowanie, NULL) != 0)
        LOGE_ERRNO("pthread_create(podsumowanie)");

    // Czekaj, aż restauracja będzie otwarta; użyj cond stoliki_sync bez aktywnego odpytywania
    if (pthread_mutex_lock(&common_ctx->stoliki_sync->mutex) == 0)
    {
        while (*common_ctx->restauracja_otwarta && !obsl_ctx->shutdown_requested)
            (void)pthread_cond_wait(&common_ctx->stoliki_sync->cond,
                                    &common_ctx->stoliki_sync->mutex);
        pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
    }

    // Zakończ wątki
    obsl_ctx->shutdown_requested = 1;
    (void)pthread_kill(t_podawanie, SIGTERM);
    (void)pthread_kill(t_specjalne, SIGTERM);
    (void)pthread_join(t_podawanie, NULL);
    (void)pthread_join(t_specjalne, NULL);

    // Poczekaj na zakończenie wątku podsumowania
    (void)pthread_join(t_podsumowanie, NULL);
    exit(0);
}

// Główny punkt wejścia
int main(int argc, char **argv)
{
    if (dolacz_ipc_z_argv(argc, argv, 0, NULL) != 0)
        return 1;
    obsluga();
    return 0;
}
