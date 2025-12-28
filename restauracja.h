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
#define MAX_STOLIKI 20
#define MAX_KOLEJKA 50
#define MAX_TASMA 100
#define CZAS_PRACY 30

// ====== SEMAPHORE IDS ======
#define SEM_KOLEJKA 0
#define SEM_STOLIKI 1
#define SEM_TASMA 2

// ====== STRUCTURES ======
typedef struct
{
    int osoby;
    int dzieci;
    int dorosli;
    int vip;
    time_t wejscie;
} Grupa;

typedef struct
{
    Grupa q[MAX_KOLEJKA];
    int przod, tyl, ilosc;
} Kolejka;

typedef struct
{
    int pojemnosc;
    int zajety;
    Grupa grupa;
} Stolik;

typedef struct
{
    int talerze[MAX_TASMA];
    int ilosc;
} Tasma;

typedef struct
{
    int p10;
    int p15, p20;
    int p40, p50, p60;
    int suma;
} Statystyki;

// ====== GLOBAL VARIABLES ======
extern int shm_id, sem_id;
extern Kolejka *kolejka;
extern Stolik *stoliki;
extern Tasma *tasma;
extern Statystyki *kuchnia, *kasa;
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
void push(Grupa g);

/**
 * Removes a group from the queue
 * @param g - pointer to group structure
 * @return 1 if successful, 0 if queue is empty
 */
int pop(Grupa *g);

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
