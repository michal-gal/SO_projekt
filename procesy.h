#ifndef PROCESY_H
#define PROCESY_H

#include <semaphore.h>
#include <stdio.h>

#define MAX_KOLEJKA 500
#define MAX_TASMA 500
#define MAX_STOLIKI 100

// Struktury
typedef struct
{
    int rozmiar;
    int is_vip;
    int ma_dzieci;
    int opiekunowie;
} Grupa;

typedef struct
{
    Grupa kolejka[500];
    int przod, tyl, licznik;
} Kolejka;

typedef struct
{
    int cena;
    int dla_stolika;
} Talerz;

typedef struct
{
    Talerz tasma[500];
    int przod, tyl, licznik;
} Tasma;

typedef struct
{
    int pojemnosc;
    int zajety;
    Grupa grupa;
} Stolik;

// Deklaracje zmiennych globalnych (definicje w globals.c)
extern Kolejka *kolejka_klientow;
extern Tasma *tasma;
extern Stolik *stoly;
extern int *wszyscy_klienci;
extern int *vip_licznik;
extern int *sygnal_kierownika;

extern sem_t *kolejka_sem;
extern sem_t *tasma_sem;

extern int P, X1, X2, X3, X4, N, Tp, Tk;
extern FILE *raport;

// Deklaracje funkcji
void inicjuj_kolejka();
void inicjuj_tasma();
void dodaj_do_kolejki(Grupa g);
Grupa usun_z_kolejki();
void dodaj_do_tasmy(Talerz t);
Talerz usun_z_tasmy();

void klient_proces();
void obsluga_proces();
void kucharz_proces();
void kierownik_proces();

#endif
