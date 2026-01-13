#ifndef PROCESY_H
#define PROCESY_H

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

// ====== STAŁE ======
#define X1 20                                         // liczba stolików o pojemności 1
#define X2 22                                         // liczba stolików o pojemności 2
#define X3 53                                         // liczba stolików o pojemności 3
#define X4 32                                         // liczba stolików o pojemności 4
#define p10 10                                        // ceny dań 1
#define p15 15                                        // ceny dań 2
#define p20 20                                        // ceny dań 3
#define p40 40                                        // ceny dań 4
#define p50 50                                        // ceny dań 5
#define p60 60                                        // ceny dań 6
#define MAX_OSOBY (X1 * 1 + X2 * 2 + X3 * 3 + X4 * 4) // maksymalna liczba osób przy stolikach
#define MAX_STOLIKI (X1 + X2 + X3 + X4)               // maksymalna liczba stolików
#define MAX_KOLEJKA 500                               // maksymalna liczba grup w kolejce
#define MAX_TASMA 500                                 // maksymalna długość taśmy
#define TP 10                                         // godzina otwarcia restauracji
#define TK 22                                         // godzina zamknięcia restauracji
#define CZAS_PRACY (TK - TP) * 5                      // czas otwarcia restauracji w sekundach
#define REZERWA_TASMA 50
#define LADA (MAX_TASMA - MAX_OSOBY - REZERWA_TASMA)

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
extern pid_t pid_obsluga, pid_kucharz, pid_kierownik, pid_generator;

// ====== SEMAFORY IDS ======
#define SEM_KOLEJKA 0
#define SEM_STOLIKI 1
#define SEM_TASMA 2

// ====== STRUKTURY ======
struct Grupa
{
    pid_t proces_id;         // PID procesu grupy
    int osoby;               // liczba osób w grupie
    int dzieci;              // liczba dzieci
    int dorosli;             // liczba dorosłych
    int vip;                 // 1 jeśli VIP, 0 jeśli normalny
    int stolik_przydzielony; // indeks stolika w tablicy stolików, -1 jeśli brak
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

/**
 * Client generator process - spawns client processes
 */
void generator_klientow(void);

/**
 * Table generator - initializes tables
 */
void generator_stolikow(struct Stolik *stoliki);

/**
 * Adds a dish to the conveyor belt
 * @param tasma - conveyor belt array
 * @param cena - price of the dish
 */
void dodaj_danie(int *tasma, int cena);

/**
 * Assigns a table to a group
 * @param g - pointer to the group structure
 */
void przydziel_stolik(struct Grupa *g);

/**
 * Creates IPC resources (shared memory and semaphores)
 */
void stworz_ipc(void);

#endif // PROCESY_H
