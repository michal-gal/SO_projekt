#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/sem.h>
#include "procesy.h"

// Deklaracje zmiennych współdzielonych (z restauracja.c)
extern Kolejka *kolejka_klientow;
extern Tasma *tasma;

extern Stolik *stoly;
extern int *wszyscy_klienci;
extern int *vip_licznik;
extern int *sygnal_kierownika;
extern int P, X1, X2, X3, X4, N, Tp, Tk;
extern FILE *raport;

static struct sembuf P_OP = {0, -1, 0};
static struct sembuf V_OP = {0, 1, 0};

static void sem_p(int semid) { semop(semid, &P_OP, 1); }
static void sem_v(int semid) { semop(semid, &V_OP, 1); }

void inicjuj_kolejka() // Inicjalizacja kolejki klientów
{
    kolejka_klientow->przod = 0;
    kolejka_klientow->tyl = 0;
    kolejka_klientow->licznik = 0;
}

void inicjuj_tasma() // Inicjalizacja taśmy z daniami
{
    tasma->przod = 0;
    tasma->tyl = 0;
    tasma->licznik = 0;
}

void dodaj_do_kolejki(Grupa g) // Dodanie grupy do kolejki
{
    sem_p(kolejka_sem_id);
    if (kolejka_klientow->licznik < MAX_KOLEJKA)
    {
        kolejka_klientow->kolejka[kolejka_klientow->tyl] = g;
        kolejka_klientow->tyl = (kolejka_klientow->tyl + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik++;
    }
    sem_v(kolejka_sem_id);
}

Grupa usun_z_kolejki()
{
    Grupa g = {0, 0, 0, 0};
    sem_p(kolejka_sem_id);
    if (kolejka_klientow->licznik > 0)
    {
        g = kolejka_klientow->kolejka[kolejka_klientow->przod];
        kolejka_klientow->przod = (kolejka_klientow->przod + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik--;
    }
    sem_v(kolejka_sem_id);
    return g;
}

void dodaj_do_tasmy(Talerz t)
{
    sem_p(tasma_sem_id);
    if (tasma->licznik < MAX_TASMA)
    {
        tasma->tasma[tasma->tyl] = t;
        tasma->tyl = (tasma->tyl + 1) % MAX_TASMA;
        tasma->licznik++;
    }
    sem_v(tasma_sem_id);
}

Talerz usun_z_tasmy()
{
    Talerz t = {0, -1};
    sem_p(tasma_sem_id);
    if (tasma->licznik > 0)
    {
        t = tasma->tasma[tasma->przod];
        tasma->przod = (tasma->przod + 1) % MAX_TASMA;
        tasma->licznik--;
    }
    sem_v(tasma_sem_id);
    return t;
}

void klient_proces()
{
    while (1)
    {
        sleep(rand() % 10 + 1);
        Grupa g;
        g.rozmiar = rand() % 4 + 1;
        g.vip = (rand() % 100 < 2) ? 1 : 0;
        g.liczba_dzieci = rand() % 2; // 0 lub 1 dziecko dla prostoty
        g.opiekunowie = g.liczba_dzieci ? rand() % 3 + 1 : 0;

        if (g.vip)
        {
            (*vip_licznik)++;
            fprintf(raport, "VIP grupa %d osób przybyła.\n", g.rozmiar);
        }
        else
        {
            dodaj_do_kolejki(g);
            fprintf(raport, "Grupa %d osób w kolejce.\n", g.rozmiar);
        }
    }
}

void obsluga_proces()
{
    while (1)
    {
        if (kolejka_klientow->licznik > 0)
        {
            Grupa g = usun_z_kolejki();
            fprintf(raport, "Grupa %d osób obsłużona.\n", g.rozmiar);
        }
        sleep(1);
    }
}

void kucharz_proces()
{
    while (1)
    {
        sleep(5);
        Talerz t;
        t.cena = (rand() % 3 + 4) * 10;
        t.dla_stolika = rand() % MAX_STOLIKI;
        dodaj_do_tasmy(t);
        fprintf(raport, "Danie %d zł dla stolika %d.\n", t.cena, t.dla_stolika);
    }
}

void kierownik_proces()
{
    while (1)
    {
        sleep(20);
        *sygnal_kierownika = rand() % 4;
        fprintf(raport, "Sygnał kierownika: %d\n", *sygnal_kierownika);
    }
}
