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

// ====== CONSTANTS ======
#define X1 40
#define X2 30
#define X3 20
#define X4 10
#define p15 15
#define p20 20
#define p40 40
#define p50 50
#define p60 60
#define CENY_DAN [6] = {p15, p20, p40, p50, p60}
#define ILOSC_STOLIKOW [4] = {X1, X2, X3, X4}
#define LADA 50
#define MAX_OSOBY (LADA + X1 * 1 + X2 * 2 + X3 * 3 + X4 * 4)
#define MAX_STOLIKI (LADA + X1 + X2 + X3 + X4)
#define MAX_KOLEJKA 500
#define MAX_TASMA 500
#define CZAS_PRACY 30

// ====== SEMAPHORE IDS ======
#define SEM_KOLEJKA 0
#define SEM_STOLIKI 1
#define SEM_TASMA 2

// ====== STRUCTURES ======
struct Grupa
{
    int osoby;
    int dzieci;
    int dorosli;
    int vip;
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
    int zajety;
    struct Grupa grupa;
};

struct Tasma
{
    int talerze[MAX_TASMA];
    int ilosc; // liczba talerzy na taśmie
};

struct Statystyki
{
    int ilosc_dan[6];
    int suma;
};

// ====== GLOBAL VARIABLES ======
extern int shm_id, sem_id;
extern struct Kolejka *kolejka;
extern struct Stolik *stoliki;
extern struct Tasma *tasma;
extern struct Statystyki *kuchnia, *kasa;
extern int *sygnal_kierownika;
extern int *restauracja_otwarta;

// ====== FUNCTION DECLARATIONS ======

/**
 * Performs a semaphore operation
 * @param sem - semaphore ID
 * @param val - operation value
 */
void sem_op(int sem, int val);

/**
 * Adds a group to the queue
 * @param g - group structure
 */
void push(struct Grupa g);

/**
 * Removes a group from the queue
 * @param g - pointer to group structure
 * @return 1 if successful, 0 if queue is empty
 */
int pop(struct Grupa *g);

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

#endif // RESTAURACJA_H
