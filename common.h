#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h> // pid_t
#include <time.h>      // time_t

// ====== STAŁE ======
#define X1 5                                          // liczba stolików o pojemności 1
#define X2 5                                          // liczba stolików o pojemności 2
#define X3 5                                          // liczba stolików o pojemności 3
#define X4 5                                          // liczba stolików o pojemności 4
#define p10 10                                        // ceny dań 1
#define p15 15                                        // ceny dań 2
#define p20 20                                        // ceny dań 3
#define p40 40                                        // ceny dań 4
#define p50 50                                        // ceny dań 5
#define p60 60                                        // ceny dań 6
#define MAX_OSOBY (X1 * 1 + X2 * 2 + X3 * 3 + X4 * 4) // maksymalna liczba osób przy stolikach
#define MAX_STOLIKI (X1 + X2 + X3 + X4)               // maksymalna liczba stolików
#define MAX_TASMA 100                                 // maksymalna długość taśmy
#define MAX_GRUP_NA_STOLIKU 4                         // maksymalna liczba grup na jednym stoliku
#define TP 10                                         // godzina otwarcia restauracji
#define TK 22                                         // godzina zamknięcia restauracji
#define CZAS_PRACY (TK - TP) * 5                      // czas otwarcia restauracji w sekundach
#define REZERWA_TASMA 50
#define LADA (MAX_TASMA - MAX_OSOBY - REZERWA_TASMA)

// ====== ZMIENNE GLOBALNE ======
extern int shm_id, sem_id;          // ID pamięci współdzielonej i semaforów
extern int msgq_id;                 // ID kolejki komunikatów (System V)
extern struct Stolik *stoliki;      // wskaźnik na tablicę stolików
extern int *restauracja_otwarta;    // wskaźnik na stan restauracji
extern int *kuchnia_dania_wydane;   // liczba wydanych dań przez kuchnię
extern int *kasa_dania_sprzedane;   // liczba sprzedanych dań przez kasę
extern struct Talerzyk *tasma;      // tablica reprezentująca taśmę
extern int *kolej_podsumowania;     // czyja kolej na podsumowanie (1=obsługa, 2=kucharz, 3=kierownik)
extern const int ILOSC_STOLIKOW[4]; // liczba stolików o pojemności 1,2,3,4
extern const int CENY_DAN[6];       // ceny dań
extern pid_t pid_obsluga, pid_kucharz, pid_kierownik;
// PID-y procesów w pamięci współdzielonej (potrzebne po exec(), np. do wysyłania sygnałów)
extern pid_t *pid_obsluga_shm;
extern pid_t *pid_kierownik_shm;

#define SEM_STOLIKI 0
#define SEM_TASMA 1

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
    int danie_specjalne;  // jeśli zamówiono danie specjalne to jest cena dania, 0 jeśli nie
};

struct Stolik
{
    int numer_stolika;
    int pojemnosc;
    struct Grupa grupy[MAX_GRUP_NA_STOLIKU]; // tablica grup przy stoliku
    int liczba_grup;                         // liczba grup przy stoliku
    int zajete_miejsca;                      // liczba osób przy stoliku
};

struct Talerzyk
{
    int cena;
    int stolik_specjalny; // 0 = normalne danie, >0 = numer stolika dla zamówienia specjalnego
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
 */
void push(struct Grupa g);

/**
 * Removes a group from the queue
 * @return struct Grupa (empty if queue is empty - proces_id == 0)
 */
struct Grupa pop(void);

/**
 * Client process
 */
void klient(void);

/**
 * Service process
 */
void obsluga(void);

/**
 * Cook process
 */
void kucharz(void);

/**
 * Manager process
 */
void kierownik(void);

/**
 * Table generator - initializes tables
 */
void generator_stolikow(struct Stolik *stoliki);

/**
 * Adds a dish to the conveyor belt
 */
void dodaj_danie(struct Talerzyk *tasma, int cena);

/**
 * Creates IPC resources (shared memory and semaphores)
 */
void stworz_ipc(void);

/**
 * Attaches to existing IPC resources after exec().
 */
void dolacz_ipc(int shm_id_existing, int sem_id_existing);

// ====== HELPERY (wspólne) ======

int price_to_index(int cena);

// Wywoływać tylko przy trzymanym SEM_STOLIKI.
int find_table_for_group_locked(const struct Grupa *g);

void wait_for_turn(int turn);

int parse_int_or_die(const char *what, const char *s);

#endif // COMMON_H
