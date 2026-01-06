#ifndef RESTAURACJA_H
#define RESTAURACJA_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

// ====== CONSTANTS ======
#define X1 10
#define X2 8
#define X3 5
#define X4 3
#define p10 10
#define p15 15
#define p20 20
#define p40 40
#define p50 50
#define p60 60
#define MAX_OSOBY (X1 * 1 + X2 * 2 + X3 * 3 + X4 * 4)
#define MAX_STOLIKI (X1 + X2 + X3 + X4)
#define MAX_KOLEJKA 500
#define MAX_TASMA 50
#define CZAS_PRACY 20

// ====== ZMIENNE GLOBALNE ======
extern int shm_id, sem_id;                                     // ID pamięci współdzielonej i semaforów
extern struct Kolejka *kolejka;                                // wskaźnik na kolejkę
extern struct Stolik *stoliki;                                 // wskaźnik na tablicę stolików
extern int *sygnal_kierownika;                                 // wskaźnik na sygnał kierownika
extern int *restauracja_otwarta;                               // wskaźnik na stan restauracji
extern int *aktywni_klienci;                                   // wskaźnik na liczbę aktywnych klientów
extern int *kuchnia_dania_wydane;                              // liczba wydanych dań przez kuchnię
extern int *kasa_dania_sprzedane;                              // liczba sprzedanych dań przez kasę
extern int *tasma;                                             // tablica reprezentująca taśmę
static const int ILOSC_STOLIKOW[4] = {X1, X2, X3, X4};         // liczba stolików o pojemności 1,2,3,4
static const int CENY_DAN[6] = {p10, p15, p20, p40, p50, p60}; // ceny dań;

// ====== SEMAPHORE IDS ======
#define SEM_KOLEJKA 0
#define SEM_STOLIKI 1
#define SEM_TASMA 2

// ====== STRUCTURES ======
struct Grupa
{
    pid_t proces_id;
    int osoby;
    int dzieci;
    int dorosli;
    int vip;
    int stolik_przydzielony;
    time_t wejscie;
    int pobrane_dania[6]; // liczba pobranych dań
};

struct Kolejka
{
    struct Grupa q[MAX_KOLEJKA];
    int przod, tyl, ilosc;
};

struct Stolik
{
    int numer_stolika;
    int pojemnosc;
    pid_t proces_id;
};

// ====== GLOBAL VARIABLES ======
extern int shm_id, sem_id;
extern struct Kolejka *kolejka;
extern struct Stolik *stoliki;
extern int *sygnal_kierownika;
extern int *restauracja_otwarta;
extern int *tasma;

// ====== FUNCTION DECLARATIONS ======

/**
 * Performs a semaphore operation
 * @param sem - semaphore ID
 * @param val - operation value
 */
void sem_op(int sem, int val);

/**
 * Adds a group to the queue
 * @param pid - process ID of group
 */
void push(struct Grupa g);

/**
 * Removes a group from the queue
 * @return struct Grupa (empty if queue is empty - proces_id == 0)
 */
struct Grupa pop(void);

/**
 * Client process - generates customer groups
 */
void klient(void);

/**
 * Service process - seats groups and manages dishes
 */
void obsluga(void);

/**
 * Cook process - prepares dishes
 */
void kucharz(void);

/**
 * Manager process - sends signals to adjust productivity
 */
void kierownik(void);

void generator_klientow(void);
void generator_stolikow(struct Stolik *stoliki);
void dodaj_danie(int *tasma, int cena);
void przydziel_stolik(struct Grupa *g);

#endif // RESTAURACJA_H
