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
#define CZAS_PRACY 30

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
    pid_t q[MAX_KOLEJKA];
    int przod, tyl, ilosc;
};

struct Stolik
{
    int numer_stolika;
    int pojemnosc;
    int zajety;
    pid_t proces_id;
};

struct Tasma
{
    int talerze[MAX_TASMA];
    int ilosc; // liczba talerzy na taśmie
};

// ====== GLOBAL VARIABLES ======
extern int shm_id, sem_id;
extern struct Kolejka *kolejka;
extern struct Stolik *stoliki;
extern struct Tasma *tasma;
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
 * @param pid - process ID of group
 */
void push(pid_t pid);

/**
 * Removes a group from the queue
 * @param pid - pointer to process ID
 * @return 1 if successful, 0 if queue is empty
 */
int pop(pid_t *pid);

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
void przesun_tasme_cyklicznie(int *tasma);
void dodaj_danie(int *tasma, int cena);
void klient_sprawdz_i_bierz(struct Grupa *g, int *tasma);
void przydziel_stolik(struct Grupa *g);

#endif // RESTAURACJA_H
