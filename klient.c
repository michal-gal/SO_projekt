#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <unistd.h>

// ====== TYPY ======

typedef enum
{
    POBRANIE_BRAK = 0,
    POBRANIE_POBRANO = 1,
    POBRANIE_POMINIETO_INNY_STOLIK = 2,
} WynikPobraniaDania;

typedef struct
{
    struct Grupa *g;
    int is_lead;
    int is_adult;
    int *shared_dania_pobrane;
    int *dania_do_pobrania_ptr;
    struct timespec *czas_start_dania_ptr;
    int timeout_dania_ms;
} PersonArg;

// ====== ZMIENNE GLOBALNE ======

// Per-module context for klient
struct KlientCtx
{
    volatile sig_atomic_t prosba_zamkniecia;
    pthread_mutex_t klient_dania_mutex;
};

static struct KlientCtx klient_ctx_storage = {.prosba_zamkniecia = 0, .klient_dania_mutex = PTHREAD_MUTEX_INITIALIZER};
static struct KlientCtx *klient_ctx = &klient_ctx_storage;

static void kolejka_dodaj_local(struct Grupa g);

// ====== DEKLARACJE WSTĘPNE ======

static void klient_obsluz_sigterm(int signo);
static void klient_obsluz_sigusr1(int signo);
static struct Grupa inicjalizuj_grupe(int numer_grupy);
static void usadz_grupe_vip(struct Grupa *g);
static int czekaj_na_przydzial_stolika(struct Grupa *g);
static void zamow_specjalne_jesli_trzeba(struct Grupa *g,
                                         int *dania_do_pobrania,
                                         struct timespec *czas_start_dania,
                                         int timeout_dania_ms);
static WynikPobraniaDania
sprobuj_pobrac_danie(struct Grupa *g, int *dania_pobrane, int dania_do_pobrania,
                     struct timespec *czas_start_dania);
static void zaplac_za_dania(const struct Grupa *g);
static void opusc_stolik(const struct Grupa *g);
static void petla_czekania_na_dania(struct Grupa *g);
static void *person_thread(void *arg);

// Handler sygnału SIGTERM
static void klient_obsluz_sigterm(int signo)
{
    (void)signo;
    klient_ctx->prosba_zamkniecia = 1;
}

// Handler sygnału SIGUSR1
static void klient_obsluz_sigusr1(int signo) { (void)signo; }

static long diff_ms(const struct timespec *start, const struct timespec *end)
{
    long sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;
    return sec * 1000 + nsec / 1000000;
}

// Inicjalizuj grupę klientów
static struct Grupa inicjalizuj_grupe(int numer_grupy)
{
    struct Grupa g;
    g.numer_grupy = numer_grupy;
    g.proces_id = getpid();
    g.osoby = rand() % 4 + 1;
    g.dorosli = rand() % g.osoby + 1;
    g.dzieci = g.osoby - g.dorosli;
    g.stolik_przydzielony = -1;
    g.vip = (rand() % 100 < 2);
    g.wejscie = time(NULL);
    memset(g.pobrane_dania, 0, sizeof(g.pobrane_dania));
    g.danie_specjalne = 0;
    return g;
}

// Usadź grupę VIP
static void usadz_grupe_vip(struct Grupa *g)
{
    int log_usadzono = 0;
    int log_numer_stolika = 0;
    int log_zajete = 0;
    int log_pojemnosc = 0;

    pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
    int i = znajdz_stolik_dla_grupy_zablokowanej(g);
    if (i >= 0)
    {
        common_ctx->stoliki[i].grupy[common_ctx->stoliki[i].liczba_grup] = *g;
        common_ctx->stoliki[i].zajete_miejsca += g->osoby;
        common_ctx->stoliki[i].liczba_grup++;
        log_usadzono = 1;
        log_numer_stolika = common_ctx->stoliki[i].numer_stolika;
        log_zajete = common_ctx->stoliki[i].zajete_miejsca;
        log_pojemnosc = common_ctx->stoliki[i].pojemnosc;
        g->stolik_przydzielony = i;
    }
    pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

    if (log_usadzono)
        LOGI("Grupa VIP %d usadzona: %d osób (dorosłych: %d, dzieci: %d) przy "
             "stoliku: %d (miejsc zajete: %d/%d)\n",
             g->numer_grupy, g->osoby, g->dorosli, g->dzieci, log_numer_stolika,
             log_zajete, log_pojemnosc);
}

// Czekaj na przydział stolika
static int czekaj_na_przydzial_stolika(struct Grupa *g)
{
    pid_t moj_proces_id = g->proces_id;
    sigset_t block_set, old_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    // Zablokuj SIGUSR1 przed dodaniem do kolejki, aby uniknąć wyścigu, gdzie
    // manager/obsluga wysyła SIGUSR1 między enqueue a blokowaniem,
    // co mogłoby spowodować, że klient przegapi przebudzenie.
    sigprocmask(SIG_BLOCK, &block_set, &old_set);

    kolejka_dodaj_local(*g);
    /* LOGD("Grupa %d dodana do kolejki: %d osób (dorosłych: %d, dzieci: %d)%s\n",
         g->numer_grupy,
         g->osoby,
         g->dorosli,
         g->dzieci,
         g->vip ? " [VIP]" : "");
  */
    while (g->stolik_przydzielony == -1 && *common_ctx->restauracja_otwarta &&
           !klient_ctx->prosba_zamkniecia)
    {
        int log_znaleziono = 0;
        int log_numer_stolika = 0;
        pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            for (int j = 0; j < common_ctx->stoliki[i].liczba_grup; j++)
            {
                if (common_ctx->stoliki[i].grupy[j].proces_id == moj_proces_id)
                {
                    g->stolik_przydzielony = i;
                    log_znaleziono = 1;
                    log_numer_stolika = common_ctx->stoliki[i].numer_stolika;
                    break;
                }
            }
            if (g->stolik_przydzielony != -1)
                break;
        }
        pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

        if (log_znaleziono)
            LOGD("Grupa %d znalazała swój stolik: %d\n", g->numer_grupy,
                 log_numer_stolika);

        if (g->stolik_przydzielony == -1 && *common_ctx->restauracja_otwarta &&
            !klient_ctx->prosba_zamkniecia)
        {
            sigsuspend(&old_set);
        }
    }

    sigprocmask(SIG_SETMASK, &old_set, NULL);

    if (g->stolik_przydzielony == -1)
    {
        LOGI("Grupa %d opuszcza kolejkę - restauracja zamknięta\n", g->numer_grupy);
        return -1;
    }
    return 0;
}

static void kolejka_dodaj_local(struct Grupa g)
{
    QueueMsg msg;
    msg.mtype = 1;
    msg.grupa = g;
    for (;;)
    {
        if (!*common_ctx->restauracja_otwarta)
            return;

        struct timespec now, abstime;
        clock_gettime(CLOCK_REALTIME, &now);
        abstime = now;
        abstime.tv_sec += 1;

        if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) != 0)
            continue;
        while (common_ctx->queue_sync->count >= common_ctx->queue_sync->max)
        {
            int rc = pthread_cond_timedwait(
                &common_ctx->queue_sync->not_full, &common_ctx->queue_sync->mutex,
                &abstime);
            if (rc == ETIMEDOUT)
            {
                if (!*common_ctx->restauracja_otwarta)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &now);
                abstime = now;
                abstime.tv_sec += 1;
                continue;
            }
            if (rc != 0)
            {
                if (common_ctx->shutdown_flag_ptr &&
                    *common_ctx->shutdown_flag_ptr)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    exit(0);
                }
                continue;
            }
        }

        if (msgsnd(common_ctx->msgq_id, &msg, sizeof(msg.grupa), IPC_NOWAIT) == 0)
        {
            common_ctx->queue_sync->count++;
            (*common_ctx->klienci_w_kolejce) += msg.grupa.osoby;
            pthread_cond_signal(&common_ctx->queue_sync->not_empty);
            pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            return;
        }

        pthread_mutex_unlock(&common_ctx->queue_sync->mutex);

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            continue;
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return;
    }
}

// Zamów specjalne jeśli trzeba
static void zamow_specjalne_jesli_trzeba(struct Grupa *g,
                                         int *dania_do_pobrania,
                                         struct timespec *czas_start_dania,
                                         int timeout_dania_ms)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (diff_ms(czas_start_dania, &now) <= timeout_dania_ms)
        return;
    if (g->danie_specjalne != 0)
        return;

    int ceny[] = {p40, p50, p60};
    int c = ceny[rand() % 3];
    g->danie_specjalne = c;
    (*dania_do_pobrania)++;

    pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
    for (int j = 0; j < common_ctx->stoliki[g->stolik_przydzielony].liczba_grup; j++)
    {
        if (common_ctx->stoliki[g->stolik_przydzielony].grupy[j].numer_grupy ==
            g->numer_grupy)
        {
            common_ctx->stoliki[g->stolik_przydzielony].grupy[j].danie_specjalne = c;
            break;
        }
    }
    pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);
    /* Powiadom obsługę, że zamówiono danie specjalne (unikamy polling) */
    (void)pthread_cond_signal(&common_ctx->stoliki_sync->cond);

    LOGI("Grupa %d zamawia danie specjalne za: %d zł. \n", g->numer_grupy,
         g->danie_specjalne);
    clock_gettime(CLOCK_MONOTONIC, czas_start_dania);
}

// Wątek osoby
static void *person_thread(void *arg)
{
    PersonArg *pa = (PersonArg *)arg;
    struct Grupa *g = pa->g;
    int local_is_lead = pa->is_lead;
    int timeout = pa->timeout_dania_ms;

    for (;;)
    {
        pthread_mutex_lock(&klient_ctx->klient_dania_mutex);
        int done = *pa->shared_dania_pobrane;
        int target = *pa->dania_do_pobrania_ptr;
        pthread_mutex_unlock(&klient_ctx->klient_dania_mutex);

        if (done >= target || !*common_ctx->restauracja_otwarta || klient_ctx->prosba_zamkniecia)
            break;

        if (local_is_lead)
            zamow_specjalne_jesli_trzeba(g, pa->dania_do_pobrania_ptr,
                                         pa->czas_start_dania_ptr, timeout);

        WynikPobraniaDania wynik = sprobuj_pobrac_danie(
            g, pa->shared_dania_pobrane, target, pa->czas_start_dania_ptr);
        if (wynik == POBRANIE_POMINIETO_INNY_STOLIK)
        {
            sched_yield();
            continue;
        }

        sched_yield();
    }

    free(pa);
    return NULL;
}

// Spróbuj pobrać danie
static WynikPobraniaDania
sprobuj_pobrac_danie(struct Grupa *g, int *dania_pobrane, int dania_do_pobrania,
                     struct timespec *czas_start_dania)
{
    int log_pobrano = 0;
    int log_cena = 0;
    int log_numer_stolika = g->stolik_przydzielony + 1;
    int log_pobrane = 0;
    int log_do_pobrania = dania_do_pobrania;
    pid_t log_pid = g->numer_grupy;

    pthread_mutex_lock(&common_ctx->tasma_sync->mutex);
    int numer_stolika = g->stolik_przydzielony + 1;
    int idx_tasma = -1;
    int cena = 0;

    // Najpierw spróbuj znaleźć danie specjalne dla tego stolika gdziekolwiek na
    // taśmie.
    for (int i = 0; i < MAX_TASMA; i++)
    {
        if (common_ctx->tasma[i].cena != 0 && common_ctx->tasma[i].stolik_specjalny == numer_stolika)
        {
            idx_tasma = i;
            cena = common_ctx->tasma[i].cena;
            break;
        }
    }

    // Jeśli nie znaleziono specjalnego, sprawdź standardowe danie na pozycji
    // stolika.
    if (idx_tasma == -1 && common_ctx->tasma[g->stolik_przydzielony].cena != 0)
    {
        if (common_ctx->tasma[g->stolik_przydzielony].stolik_specjalny != 0 &&
            common_ctx->tasma[g->stolik_przydzielony].stolik_specjalny != numer_stolika)
        {
            pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);
            return POBRANIE_POMINIETO_INNY_STOLIK;
        }
        idx_tasma = g->stolik_przydzielony;
        cena = common_ctx->tasma[g->stolik_przydzielony].cena;
    }

    if (idx_tasma != -1)
    {
        int idx = cena_na_indeks(cena);
        pthread_mutex_lock(&klient_ctx->klient_dania_mutex);
        if (idx >= 0)
            g->pobrane_dania[idx]++;

        (*dania_pobrane)++;
        log_pobrano = 1;
        log_cena = cena;
        log_pobrane = *dania_pobrane;
        pthread_mutex_unlock(&klient_ctx->klient_dania_mutex);

        common_ctx->tasma[idx_tasma].cena = 0;
        common_ctx->tasma[idx_tasma].stolik_specjalny = 0;
        if (common_ctx->tasma_sync->count > 0)
            common_ctx->tasma_sync->count--;
        pthread_cond_signal(&common_ctx->tasma_sync->not_full);
        LOGD("sprobuj_pobrac_danie: grupa %d pobrała danie za %d zł z pozycji %d "
             "(count=%d)\n",
             log_pid, log_cena, idx_tasma, common_ctx->tasma_sync->count);
        clock_gettime(CLOCK_MONOTONIC, czas_start_dania);
        pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);

        if (log_pobrano)
            LOGI("Grupa %d przy stoliku %d pobrała danie za %d zł (pobrane: %d/%d)\n",
                 log_pid, log_numer_stolika, log_cena, log_pobrane, log_do_pobrania);
        return POBRANIE_POBRANO;
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
    {
        ts.tv_nsec += 10 * 1000 * 1000; // 10ms
        if (ts.tv_nsec >= 1000000000L)
        {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000L;
        }
        (void)pthread_cond_timedwait(&common_ctx->tasma_sync->not_empty,
                                     &common_ctx->tasma_sync->mutex, &ts);
    }

    pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);
    return POBRANIE_BRAK;
}

// Zapłać za dania
static void zaplac_za_dania(const struct Grupa *g)
{
    LOGI("Grupa %d przy stoliku %d gotowa do płatności\n", g->numer_grupy,
         g->stolik_przydzielony + 1);

    int log_ilosc[6] = {0};
    int log_cena[6] = {0};
    int log_kwota[6] = {0};

    pthread_mutex_lock(&common_ctx->tasma_sync->mutex);
    pthread_mutex_lock(&klient_ctx->klient_dania_mutex);
    for (int i = 0; i < 6; i++)
    {
        if (g->pobrane_dania[i] == 0)
            continue;
        int kwota = g->pobrane_dania[i] * CENY_DAN[i];
        common_ctx->kasa_dania_sprzedane[i] += g->pobrane_dania[i];

        log_ilosc[i] = g->pobrane_dania[i];
        log_cena[i] = CENY_DAN[i];
        log_kwota[i] = kwota;
    }
    pthread_mutex_unlock(&klient_ctx->klient_dania_mutex);
    pthread_mutex_unlock(&common_ctx->tasma_sync->mutex);

    for (int i = 0; i < 6; i++)
    {
        if (log_ilosc[i] == 0)
            continue;
        LOGI("Grupa %d płaci za %d dań za %d zł każde, łącznie: %d zł\n",
             g->numer_grupy, log_ilosc[i], log_cena[i], log_kwota[i]);
    }
}

// Opuść stolik
static void opusc_stolik(const struct Grupa *g)
{
    pid_t log_pid = g->numer_grupy;
    int log_numer_stolika = g->stolik_przydzielony + 1;

    pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
    for (int j = 0; j < common_ctx->stoliki[g->stolik_przydzielony].liczba_grup; j++)
    {
        if (common_ctx->stoliki[g->stolik_przydzielony].grupy[j].numer_grupy ==
            g->numer_grupy)
        {
            for (int k = j; k < common_ctx->stoliki[g->stolik_przydzielony].liczba_grup - 1;
                 k++)
            {
                common_ctx->stoliki[g->stolik_przydzielony].grupy[k] =
                    common_ctx->stoliki[g->stolik_przydzielony].grupy[k + 1];
            }
            memset(&common_ctx->stoliki[g->stolik_przydzielony]
                        .grupy[common_ctx->stoliki[g->stolik_przydzielony].liczba_grup - 1],
                   0, sizeof(struct Grupa));
            common_ctx->stoliki[g->stolik_przydzielony].liczba_grup--;
            common_ctx->stoliki[g->stolik_przydzielony].zajete_miejsca -= g->osoby;
            break;
        }
    }
    pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

    /* Zliczamy opuszczających klientów (osoby), nie tylko grupy. */
    (*common_ctx->klienci_opuscili) += g->osoby;
    LOGP("Grupa %d przy stoliku %d opuszcza restaurację.\n", log_pid,
         log_numer_stolika);
}

// Pętla czekania na dania
static void petla_czekania_na_dania(struct Grupa *g)
{
    // Wielowątkowa: uruchom jeden wątek na osobę (dorosły/dziecko). Główny wątek
    // będzie czekać na ich zakończenie. Jeden wyznaczony wątek lider będzie
    // obsługiwał zamówienia specjalne, aby uniknąć wyścigów. Wspólne liczniki są
    // chronione przez `klient_dania_mutex` (zakres pliku).

    int dania_do_pobrania = rand() % 8 + 3;
    int shared_dania_pobrane = 0;
    struct timespec czas_start_dania;
    clock_gettime(CLOCK_MONOTONIC, &czas_start_dania);
    int timeout_dania_ms = 10;

    int persons = g->osoby;
    pthread_t *threads = calloc(persons, sizeof(pthread_t));
    if (!threads)
    {
        LOGE("Brak pamięci na wątki\n");
        return;
    }

    for (int i = 0; i < persons; i++)
    {
        PersonArg *pa = malloc(sizeof(PersonArg));
        if (!pa)
        {
            LOGE("Brak pamięci na PersonArg\n");
            continue;
        }
        pa->g = g;
        pa->is_adult = (i < g->dorosli) ? 1 : 0;
        pa->is_lead =
            (i == 0) ? 1 : 0; // pierwsza osoba obsługuje zamówienia specjalne
        pa->shared_dania_pobrane = &shared_dania_pobrane;
        pa->dania_do_pobrania_ptr = &dania_do_pobrania;
        pa->czas_start_dania_ptr = &czas_start_dania;
        pa->timeout_dania_ms = timeout_dania_ms;

        if (pthread_create(&threads[i], NULL, person_thread, pa) != 0)
        {
            LOGE_ERRNO("pthread_create(person)");
            free(pa);
            threads[i] = 0;
        }
    }

    // Czekaj na zakończenie wszystkich wątków osób
    for (int i = 0; i < persons; i++)
    {
        if (threads[i])
            (void)pthread_join(threads[i], NULL);
    }

    free(threads);
}

// Główna funkcja klienta
void klient(int numer_grupy)
{
    if (signal(SIGTERM, klient_obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");
    ustaw_shutdown_flag(&klient_ctx->prosba_zamkniecia);
    if (signal(SIGUSR1, klient_obsluz_sigusr1) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR1)");

    struct Grupa g = inicjalizuj_grupe(numer_grupy);

    if (g.vip)
    {
        usadz_grupe_vip(&g);
        if (g.stolik_przydzielony == -1)
        {
            // Miejsce VIP niedostępne; powrót do normalnej obsługi kolejki.
            if (czekaj_na_przydzial_stolika(&g) != 0)
                exit(0);
        }
    }
    else
    {
        if (czekaj_na_przydzial_stolika(&g) != 0)
            exit(0);
    }

    if (klient_ctx->prosba_zamkniecia || !*common_ctx->restauracja_otwarta)
    {
        if (g.stolik_przydzielony != -1)
            opusc_stolik(&g);
        exit(0);
    }

    petla_czekania_na_dania(&g);

    if (klient_ctx->prosba_zamkniecia || !*common_ctx->restauracja_otwarta)
    {
        opusc_stolik(&g);
        exit(0);
    }

    zaplac_za_dania(&g);
    opusc_stolik(&g);

    exit(0);
}

// Główny punkt wejścia
int main(int argc, char **argv)
{
    int numer_grupy = 0;
    if (dolacz_ipc_z_argv(argc, argv, 1, &numer_grupy) != 0)
        return 1;
    zainicjuj_losowosc();
    klient(numer_grupy);
    return 0;
}
