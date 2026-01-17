#include "common.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t shutdown_requested = 0;

static void klient_obsluz_sigterm(int signo)
{
    (void)signo;
    shutdown_requested = 1;
}

static struct Grupa inicjalizuj_grupe(void)
{
    struct Grupa g;
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

static void usadz_grupe_vip(struct Grupa *g)
{
    int log_usadzono = 0;
    int log_numer_stolika = 0;
    int log_zajete = 0;
    int log_pojemnosc = 0;

    sem_operacja(SEM_STOLIKI, -1);
    int i = znajdz_stolik_dla_grupy_zablokowanej(g);
    if (i >= 0)
    {
        stoliki[i].grupy[stoliki[i].liczba_grup] = *g;
        stoliki[i].zajete_miejsca += g->osoby;
        stoliki[i].liczba_grup++;
        log_usadzono = 1;
        log_numer_stolika = stoliki[i].numer_stolika;
        log_zajete = stoliki[i].zajete_miejsca;
        log_pojemnosc = stoliki[i].pojemnosc;
        g->stolik_przydzielony = i;
    }
    sem_operacja(SEM_STOLIKI, 1);

    if (log_usadzono)
        LOGI("Grupa VIP %d usadzona: %d osób (dorosłych: %d, dzieci: %d) przy stoliku: %d (miejsc zajete: %d/%d)\n",
             g->proces_id,
             g->osoby,
             g->dorosli,
             g->dzieci,
             log_numer_stolika,
             log_zajete,
             log_pojemnosc);
}

static int czekaj_na_przydzial_stolika(struct Grupa *g)
{
    kolejka_dodaj(*g);
    LOGI("Grupa %d dodana do kolejki: %d osób (dorosłych: %d, dzieci: %d)%s\n",
         g->proces_id,
         g->osoby,
         g->dorosli,
         g->dzieci,
         g->vip ? " [VIP]" : "");

    pid_t moj_proces_id = g->proces_id;
    while (g->stolik_przydzielony == -1 && *restauracja_otwarta && !shutdown_requested)
    {
        int log_znaleziono = 0;
        int log_numer_stolika = 0;
        sem_operacja(SEM_STOLIKI, -1);
        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            for (int j = 0; j < stoliki[i].liczba_grup; j++)
            {
                if (stoliki[i].grupy[j].proces_id == moj_proces_id)
                {
                    g->stolik_przydzielony = i;
                    log_znaleziono = 1;
                    log_numer_stolika = stoliki[i].numer_stolika;
                    break;
                }
            }
            if (g->stolik_przydzielony != -1)
                break;
        }
        sem_operacja(SEM_STOLIKI, 1);

        if (log_znaleziono)
            LOGI("Grupa %d znalazała swój stolik: %d\n", g->proces_id, log_numer_stolika);
        sleep(1);
    }

    if (g->stolik_przydzielony == -1)
    {
        LOGI("Grupa %d opuszcza kolejkę - restauracja zamknięta\n", g->proces_id);
        return -1;
    }
    return 0;
}

static void zamow_specjalne_jesli_trzeba(struct Grupa *g, int *dania_do_pobrania, time_t *czas_start_dania, int timeout_dania)
{
    if (time(NULL) - *czas_start_dania <= timeout_dania)
        return;
    if (g->danie_specjalne != 0)
        return;

    int ceny[] = {p40, p50, p60};
    int c = ceny[rand() % 3];
    g->danie_specjalne = c;
    (*dania_do_pobrania)++;

    sem_operacja(SEM_STOLIKI, -1);
    for (int j = 0; j < stoliki[g->stolik_przydzielony].liczba_grup; j++)
    {
        if (stoliki[g->stolik_przydzielony].grupy[j].proces_id == g->proces_id)
        {
            stoliki[g->stolik_przydzielony].grupy[j].danie_specjalne = c;
            break;
        }
    }
    sem_operacja(SEM_STOLIKI, 1);

    LOGI("Grupa %d zamawia danie specjalne za: %d zł. \n", g->proces_id, g->danie_specjalne);
    *czas_start_dania = time(NULL);
}

typedef enum
{
    TAKE_NONE = 0,
    TAKE_TAKEN = 1,
    TAKE_SKIPPED_OTHER_TABLE = 2,
} TakeDishResult;

static TakeDishResult sprobuj_pobrac_danie(struct Grupa *g, int *dania_pobrane, int dania_do_pobrania, time_t *czas_start_dania)
{
    int log_pobrano = 0;
    int log_cena = 0;
    int log_numer_stolika = g->stolik_przydzielony + 1;
    int log_pobrane = 0;
    int log_do_pobrania = dania_do_pobrania;
    pid_t log_pid = g->proces_id;

    sem_operacja(SEM_TASMA, -1);
    if (tasma[g->stolik_przydzielony].cena != 0)
    {
        int numer_stolika = g->stolik_przydzielony + 1;
        if (tasma[g->stolik_przydzielony].stolik_specjalny != 0 &&
            tasma[g->stolik_przydzielony].stolik_specjalny != numer_stolika)
        {
            sem_operacja(SEM_TASMA, 1);
            return TAKE_SKIPPED_OTHER_TABLE;
        }

        int cena = tasma[g->stolik_przydzielony].cena;
        int idx = cena_na_indeks(cena);
        if (idx >= 0)
            g->pobrane_dania[idx]++;

        (*dania_pobrane)++;
        log_pobrano = 1;
        log_cena = cena;
        log_pobrane = *dania_pobrane;

        tasma[g->stolik_przydzielony].cena = 0;
        tasma[g->stolik_przydzielony].stolik_specjalny = 0;
        *czas_start_dania = time(NULL);

        sem_operacja(SEM_TASMA, 1);

        if (log_pobrano)
            LOGI("Grupa %d przy stoliku %d pobrała danie za %d zł (pobrane: %d/%d)\n",
                 log_pid,
                 log_numer_stolika,
                 log_cena,
                 log_pobrane,
                 log_do_pobrania);
        return TAKE_TAKEN;
    }

    sem_operacja(SEM_TASMA, 1);
    return TAKE_NONE;
}

static void zaplac_za_dania(const struct Grupa *g)
{
    LOGI("Grupa %d przy stoliku %d gotowa do płatności\n", g->proces_id, g->stolik_przydzielony + 1);

    int log_ilosc[6] = {0};
    int log_cena[6] = {0};
    int log_kwota[6] = {0};

    sem_operacja(SEM_TASMA, -1);
    for (int i = 0; i < 6; i++)
    {
        if (g->pobrane_dania[i] == 0)
            continue;
        int kwota = g->pobrane_dania[i] * CENY_DAN[i];
        kasa_dania_sprzedane[i] += g->pobrane_dania[i];

        log_ilosc[i] = g->pobrane_dania[i];
        log_cena[i] = CENY_DAN[i];
        log_kwota[i] = kwota;
    }
    sem_operacja(SEM_TASMA, 1);

    for (int i = 0; i < 6; i++)
    {
        if (log_ilosc[i] == 0)
            continue;
        LOGI("Grupa %d płaci za %d dań za %d zł każde, łącznie: %d zł\n",
             g->proces_id,
             log_ilosc[i],
             log_cena[i],
             log_kwota[i]);
    }
}

static void opusc_stolik(const struct Grupa *g)
{
    pid_t log_pid = g->proces_id;
    int log_numer_stolika = g->stolik_przydzielony + 1;

    sem_operacja(SEM_STOLIKI, -1);
    for (int j = 0; j < stoliki[g->stolik_przydzielony].liczba_grup; j++)
    {
        if (stoliki[g->stolik_przydzielony].grupy[j].proces_id == g->proces_id)
        {
            for (int k = j; k < stoliki[g->stolik_przydzielony].liczba_grup - 1; k++)
            {
                stoliki[g->stolik_przydzielony].grupy[k] = stoliki[g->stolik_przydzielony].grupy[k + 1];
            }
            memset(&stoliki[g->stolik_przydzielony].grupy[stoliki[g->stolik_przydzielony].liczba_grup - 1], 0, sizeof(struct Grupa));
            stoliki[g->stolik_przydzielony].liczba_grup--;
            stoliki[g->stolik_przydzielony].zajete_miejsca -= g->osoby;
            break;
        }
    }
    sem_operacja(SEM_STOLIKI, 1);

    LOGI("Grupa %d przy stoliku %d opuszcza restaurację.\n", log_pid, log_numer_stolika);
}

static void petla_czekania_na_dania(struct Grupa *g)
{
    int dania_pobrane = 0;
    int dania_do_pobrania = rand() % 8 + 3;
    time_t czas_start_dania = time(NULL);
    int timeout_dania = 5;

    while (dania_pobrane < dania_do_pobrania && *restauracja_otwarta && !shutdown_requested)
    {
        if (time(NULL) - czas_start_dania > timeout_dania * 4)
        {
            LOGI("Grupa %d timeout czekania na dania - kończy się\n", g->proces_id);
            break;
        }

        zamow_specjalne_jesli_trzeba(g, &dania_do_pobrania, &czas_start_dania, timeout_dania);

        TakeDishResult take_res = sprobuj_pobrac_danie(g, &dania_pobrane, dania_do_pobrania, &czas_start_dania);
        if (take_res == TAKE_SKIPPED_OTHER_TABLE)
        {
            sleep(1);
            continue;
        }

        sleep(1);
    }
}

void klient(void)
{
    if (signal(SIGTERM, klient_obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

    struct Grupa g = inicjalizuj_grupe();

    if (g.vip)
    {
        usadz_grupe_vip(&g);
    }
    else
    {
        if (czekaj_na_przydzial_stolika(&g) != 0)
            exit(0);
    }

    if (shutdown_requested || !*restauracja_otwarta)
    {
        if (g.stolik_przydzielony != -1)
            opusc_stolik(&g);
        exit(0);
    }

    petla_czekania_na_dania(&g);

    if (shutdown_requested || !*restauracja_otwarta)
    {
        opusc_stolik(&g);
        exit(0);
    }

    zaplac_za_dania(&g);
    opusc_stolik(&g);

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
    zainicjuj_losowosc();
    klient();
    return 0;
}
