#include "procesy_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct Grupa init_group(void)
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

static void seat_vip_group(struct Grupa *g)
{
    sem_op(SEM_STOLIKI, -1);
    int i = find_table_for_group_locked(g);
    if (i >= 0)
    {
        stoliki[i].grupy[stoliki[i].liczba_grup] = *g;
        stoliki[i].zajete_miejsca += g->osoby;
        stoliki[i].liczba_grup++;
        printf("Grupa VIP %d usadzona: %d osób (dorosłych: %d, dzieci: %d) przy stoliku: %d (miejsc zajete: %d/%d)\n",
               g->proces_id,
               g->osoby,
               g->dorosli,
               g->dzieci,
               stoliki[i].numer_stolika,
               stoliki[i].zajete_miejsca,
               stoliki[i].pojemnosc);
        g->stolik_przydzielony = i;
    }
    sem_op(SEM_STOLIKI, 1);
}

static int wait_for_table_assignment(struct Grupa *g)
{
    push(*g);
    printf("Grupa %d dodana do kolejki: %d osób (dorosłych: %d, dzieci: %d)%s\n",
           g->proces_id,
           g->osoby,
           g->dorosli,
           g->dzieci,
           g->vip ? " [VIP]" : "");

    pid_t moj_proces_id = g->proces_id;
    while (g->stolik_przydzielony == -1 && *restauracja_otwarta)
    {
        sem_op(SEM_STOLIKI, -1);
        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            for (int j = 0; j < stoliki[i].liczba_grup; j++)
            {
                if (stoliki[i].grupy[j].proces_id == moj_proces_id)
                {
                    g->stolik_przydzielony = i;
                    printf("Grupa %d znalazała swój stolik: %d\n", g->proces_id, stoliki[i].numer_stolika);
                    break;
                }
            }
            if (g->stolik_przydzielony != -1)
                break;
        }
        sem_op(SEM_STOLIKI, 1);
        sleep(1);
    }

    if (g->stolik_przydzielony == -1)
    {
        printf("Grupa %d opuszcza kolejkę - restauracja zamknięta\n", g->proces_id);
        return -1;
    }
    return 0;
}

static void order_special_if_needed(struct Grupa *g, int *dania_do_pobrania, time_t *czas_start_dania, int timeout_dania)
{
    if (time(NULL) - *czas_start_dania <= timeout_dania)
        return;
    if (g->danie_specjalne != 0)
        return;

    int ceny[] = {p40, p50, p60};
    int c = ceny[rand() % 3];
    g->danie_specjalne = c;
    (*dania_do_pobrania)++;

    sem_op(SEM_STOLIKI, -1);
    for (int j = 0; j < stoliki[g->stolik_przydzielony].liczba_grup; j++)
    {
        if (stoliki[g->stolik_przydzielony].grupy[j].proces_id == g->proces_id)
        {
            stoliki[g->stolik_przydzielony].grupy[j].danie_specjalne = c;
            break;
        }
    }
    sem_op(SEM_STOLIKI, 1);

    printf("Grupa %d zamawia danie specjalne za: %d zł. \n", g->proces_id, g->danie_specjalne);
    *czas_start_dania = time(NULL);
}

typedef enum
{
    TAKE_NONE = 0,
    TAKE_TAKEN = 1,
    TAKE_SKIPPED_OTHER_TABLE = 2,
} TakeDishResult;

static TakeDishResult try_take_dish(struct Grupa *g, int *dania_pobrane, int dania_do_pobrania, time_t *czas_start_dania)
{
    sem_op(SEM_TASMA, -1);
    if (tasma[g->stolik_przydzielony].cena != 0)
    {
        if (tasma[g->stolik_przydzielony].stolik_specjalny != 0 &&
            tasma[g->stolik_przydzielony].stolik_specjalny != stoliki[g->stolik_przydzielony].numer_stolika)
        {
            sem_op(SEM_TASMA, 1);
            return TAKE_SKIPPED_OTHER_TABLE;
        }

        int cena = tasma[g->stolik_przydzielony].cena;
        int idx = price_to_index(cena);
        if (idx >= 0)
            g->pobrane_dania[idx]++;

        printf("Grupa %d przy stoliku %d pobrała danie za %d zł (pobrane: %d/%d)\n",
               g->proces_id,
               stoliki[g->stolik_przydzielony].numer_stolika,
               cena,
               *dania_pobrane + 1,
               dania_do_pobrania);

        (*dania_pobrane)++;
        tasma[g->stolik_przydzielony].cena = 0;
        tasma[g->stolik_przydzielony].stolik_specjalny = 0;
        *czas_start_dania = time(NULL);

        sem_op(SEM_TASMA, 1);
        return TAKE_TAKEN;
    }

    sem_op(SEM_TASMA, 1);
    return TAKE_NONE;
}

static void pay_for_dishes(const struct Grupa *g)
{
    printf("Grupa %d przy stoliku %d gotowa do płatności\n", g->proces_id, stoliki[g->stolik_przydzielony].numer_stolika);
    sem_op(SEM_TASMA, -1);
    for (int i = 0; i < 6; i++)
    {
        if (g->pobrane_dania[i] == 0)
            continue;
        int kwota = g->pobrane_dania[i] * CENY_DAN[i];
        kasa_dania_sprzedane[i] += g->pobrane_dania[i];
        printf("Grupa %d płaci za %d dań za %d zł każde, łącznie: %d zł\n",
               g->proces_id,
               g->pobrane_dania[i],
               CENY_DAN[i],
               kwota);
    }
    sem_op(SEM_TASMA, 1);
}

static void leave_table(const struct Grupa *g)
{
    sem_op(SEM_STOLIKI, -1);
    printf("Grupa %d przy stoliku %d opuszcza restaurację.\n", g->proces_id, stoliki[g->stolik_przydzielony].numer_stolika);

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
    sem_op(SEM_STOLIKI, 1);
}

static void wait_for_dishes_loop(struct Grupa *g)
{
    int dania_pobrane = 0;
    int dania_do_pobrania = rand() % 8 + 3;
    time_t czas_start_dania = time(NULL);
    int timeout_dania = 5;

    while (dania_pobrane < dania_do_pobrania && *restauracja_otwarta)
    {
        if (time(NULL) - czas_start_dania > timeout_dania * 4)
        {
            printf("Grupa %d timeout czekania na dania - kończy się\n", g->proces_id);
            break;
        }

        order_special_if_needed(g, &dania_do_pobrania, &czas_start_dania, timeout_dania);

        TakeDishResult take_res = try_take_dish(g, &dania_pobrane, dania_do_pobrania, &czas_start_dania);
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
    struct Grupa g = init_group();

    if (g.vip)
    {
        seat_vip_group(&g);
    }
    else
    {
        if (wait_for_table_assignment(&g) != 0)
            exit(0);
    }

    wait_for_dishes_loop(&g);
    pay_for_dishes(&g);
    leave_table(&g);

    exit(0);
}
