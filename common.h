#ifndef COMMON_H
#define COMMON_H

// ====== INKLUDY ======
#include "log.h"

#include <pthread.h>   // pthread_mutex_t, pthread_cond_t
#include <signal.h>    // sig_atomic_t
#include <stdio.h>     // printf
#include <sys/types.h> // pid_t
#include <time.h>      // time_t
#include <unistd.h>    // sleep

// ====== INLINE FUNKCJE ======
/* removed unused wrappers rest_sleep/rest_nanosleep; use `sleep_ms()` or
   direct `nanosleep()` when needed */

// ====== STAŁE ======
#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_SEC 1000000000L

/**
 * Sleep for given milliseconds. Returns 0 on success, -1 on error.
 */
static inline int sleep_ms(unsigned ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * NSEC_PER_MSEC;
    return nanosleep(&req, NULL);
}

/* Centralne ustawienia czasowe używane w symulacji. Można je zmieniać globalnie tutaj. */
#ifndef SIMULATION_SECONDS_DEFAULT
#define SIMULATION_SECONDS_DEFAULT 20 /* domyślny czas symulacji (sekundy) */
#endif

#ifndef SUMMARY_WAIT_SECONDS
#define SUMMARY_WAIT_SECONDS 2 /* ile sekund czekamy na podsumowania na koniec */
#endif

#ifndef SHUTDOWN_TERM_TIMEOUT
#define SHUTDOWN_TERM_TIMEOUT 4 /* seconds to wait after SIGTERM */
#endif

#ifndef SHUTDOWN_KILL_TIMEOUT
#define SHUTDOWN_KILL_TIMEOUT 2 /* seconds to wait after SIGKILL */
#endif

/* Central defaults for various timeouts (seconds). Adjust here to affect whole program. */
#ifndef KIEROWNIK_INTERVAL_DEFAULT
#define KIEROWNIK_INTERVAL_DEFAULT 30 /* seconds between manager wakes */
#endif

#ifndef MAX_AKTYWNYCH_KLIENTOW_DEFAULT
#define MAX_AKTYWNYCH_KLIENTOW_DEFAULT 5000 /* default cap of active clients */
#endif

#ifndef POLL_MS_SHORT
#define POLL_MS_SHORT 50 /* short polling interval in ms */
#endif

#ifndef POLL_MS_MED
#define POLL_MS_MED 100 /* medium polling interval in ms */
#endif

#ifndef POLL_MS_LONG
#define POLL_MS_LONG 200 /* long polling interval in ms */
#endif

#define X1 10                                         // liczba stolików o pojemności 1
#define X2 10                                         // liczba stolików o pojemności 2
#define X3 10                                         // liczba stolików o pojemności 3
#define X4 10                                         // liczba stolików o pojemności 4
#define p10 10                                        // ceny dań 1
#define p15 15                                        // ceny dań 2
#define p20 20                                        // ceny dań 3
#define p40 40                                        // ceny dań 4
#define p50 50                                        // ceny dań 5
#define p60 60                                        // ceny dań 6
#define MAX_OSOBY (X1 * 1 + X2 * 2 + X3 * 3 + X4 * 4) // maksymalna liczba osób przy stolikach
#define MAX_STOLIKI (X1 + X2 + X3 + X4)               // maksymalna liczba stolików
#define MAX_TASMA 150                                 // maksymalna długość taśmy
#define MAX_GRUP_NA_STOLIKU 4                         // maksymalna liczba grup na jednym stoliku
#define TP 10                                         // godzina otwarcia restauracji
#define TK 20                                         // godzina zamknięcia restauracji
#ifndef CZAS_PRACY
#define CZAS_PRACY (TK - TP) // czas otwarcia restauracji w sekundach
#endif

extern int czas_pracy_domyslny;
#define REZERWA_TASMA 50
#define REZERWA_STOLIKI 5
#define REZERWA_KOLEJKA 5
#ifndef MAX_LOSOWYCH_GRUP
#define MAX_LOSOWYCH_GRUP 5000 // maksymalna liczba losowych grup do wygenerowania
#endif

extern int max_losowych_grup;

// ====== ZMIENNE GLOBALNE ======
extern int shm_id, sem_id;        // ID pamięci współdzielonej i semaforów
extern int msgq_id;               // ID kolejki komunikatów (System V)
extern struct Stolik *stoliki;    // wskaźnik na tablicę stolików
extern int *restauracja_otwarta;  // wskaźnik na stan restauracji
extern int *kuchnia_dania_wydane; // liczba wydanych dań przez kuchnię
extern int *kasa_dania_sprzedane; // liczba sprzedanych dań przez kasę
extern struct Talerzyk *tasma;    // tablica reprezentująca taśmę
struct TasmaSync;
extern struct TasmaSync *tasma_sync;     // synchronizacja taśmy (mutex/cond + licznik)
extern struct StolikiSync *stoliki_sync; // synchronizacja dostępu do tablicy stolików
extern struct QueueSync *queue_sync;     // synchronizacja kolejki (licznik + cond)
extern int *kolej_podsumowania;          // czyja kolej na podsumowanie (1=obsługa, 2=kucharz, 3=kierownik)
extern int *klienci_w_kolejce;           // statystyka: liczba klientów w kolejce
extern int *klienci_przyjeci;            // statystyka: liczba przyjętych klientów
extern int *klienci_opuscili;            // statystyka: liczba klientów którzy opuścili restaurację
extern const int ILOSC_STOLIKOW[4];      // liczba stolików o pojemności 1,2,3,4
extern const int CENY_DAN[6];            // ceny dań
extern pid_t pid_obsluga, pid_kucharz, pid_kierownik;
extern int disable_close; // czy wyłączyć zamykanie restauracji przez kierownika
// PID-y procesów w pamięci współdzielonej (potrzebne po exec(), np. do wysyłania sygnałów)
extern pid_t *pid_obsluga_shm;   // wskaźnik na PID procesu obsługi w pamięci współdzielonej
extern pid_t *pid_kierownik_shm; // wskaźnik na PID procesu kierownika w pamięci współdzielonej
// ====== SEMAFORY (INDEKSY) ======

// Semafor sygnalizujący zmianę tury podsumowania
#define SEM_TURA 0
// Semafor wybudzający kierownika do okresowych działań
#define SEM_KIEROWNIK 1

// Maksymalna liczba komunikatów w kolejce wejściowej (ograniczana semaforem),
// żeby nie doprowadzić do przepełnienia kolejki System V przy dużej liczbie klientów.
// Dodatkowo rezerwujemy kilka slotów, aby kolejka nigdy nie była całkowicie
// zapełniona — to pozwala na operacje "cofnij do kolejki" lub inne priorytetowe
// wpisy administracyjne.
#define MAX_KOLEJKA_MSG 128
#define KOLEJKA_REZERWA 2 // ile slotów rezerwujemy (domyślnie 1)

// ====== STRUKTURY DANYCH ======

struct Grupa // struktura reprezentująca grupę klientów
{
    int numer_grupy;         // numer grupy (sekwencyjny)
    pid_t proces_id;         // PID procesu klienta
    int osoby;               // liczba osób w grupie
    int dzieci;              // liczba dzieci
    int dorosli;             // liczba dorosłych
    int vip;                 // 1 jeśli VIP, 0 jeśli normalny
    int stolik_przydzielony; // indeks stolika w tablicy stolików, -1 jeśli brak
    time_t wejscie;
    int pobrane_dania[6]; // liczba pobranych dań
    int danie_specjalne;  // jeśli zamówiono danie specjalne to jest cena dania, 0 jeśli nie
};

struct Stolik // struktura reprezentująca stolik
{
    int numer_stolika;
    int pojemnosc;
    struct Grupa grupy[MAX_GRUP_NA_STOLIKU]; // tablica grup przy stoliku
    int liczba_grup;                         // liczba grup przy stoliku
    int zajete_miejsca;                      // liczba osób przy stoliku
};

struct Talerzyk // struktura reprezentująca danie na taśmie
{
    int cena;
    int stolik_specjalny; // 0 = normalne danie, >0 = numer stolika dla zamówienia specjalnego
};

struct TasmaSync
{
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    int count; // liczba zajętych talerzyków na taśmie
};

struct StolikiSync
{
    pthread_mutex_t mutex; // chroni dostęp do tablicy `stoliki`
    pthread_cond_t cond;   // sygnalizuje zmiany w stanie stolików (np. danie_specjalne)
};

struct QueueSync
{
    pthread_mutex_t mutex;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
    int count; // liczba wiadomości obecnie w kolejce
    int max;   // maksymalna dozwolona pojemność
};

// ====== DEKLARACJE FUNKCJI ======

/**
 * Wykonuje operację na semaforze.
 * @param sem - indeks semafora
 * @param val - wartość operacji
 */
void sem_operacja(int sem, int val);

/**
 * Ustawia wskaźnik na flagę shutdown, aby sem_operacja mogła przerwać
 * oczekiwanie po sygnale (EINTR) i zakończyć proces.
 */
void ustaw_shutdown_flag(volatile sig_atomic_t *flag);

/**
 * Dodaje grupę do kolejki.
 */
void kolejka_dodaj(struct Grupa g);

/**
 * Pobiera grupę z kolejki.
 * @return Struktura `struct Grupa` (pusta, jeśli kolejka jest pusta: proces_id == 0).
 */
struct Grupa kolejka_pobierz(void);

/**
 * Proces klienta.
 */
void klient(int numer_grupy);

/**
 * Proces obsługi.
 */
void obsluga(void);

/**
 * Proces kucharza.
 */
void kucharz(void);

/**
 * Proces kierownika.
 */
void kierownik(void);

/**
 * Generator stolików – inicjalizuje tablicę stolików.
 */
void generator_stolikow(struct Stolik *stoliki);

/**
 * Funkcja dla kierownika do zamknięcia restauracji.
 */
void kierownik_zamknij_restauracje_i_zakoncz_klientow(void);

/**
 * Dodaje danie na taśmę.
 * Wymaga zablokowanego tasma_sync->mutex.
 */
void dodaj_danie(struct Talerzyk *tasma, int cena);

/**
 * Tworzy zasoby IPC (pamięć współdzielona i semafory).
 */
void stworz_ipc(void);

/**
 * Dołącza do istniejących zasobów IPC po exec().
 */
void dolacz_ipc(int shm_id_existing, int sem_id_existing);

/**
 * Zamienia cenę na indeks w tablicach.
 */
int cena_na_indeks(int cena);

/**
 * Znajduje odpowiedni stolik dla grupy (zakłada, że semafor stolików jest zablokowany).
 */
int znajdz_stolik_dla_grupy_zablokowanej(const struct Grupa *g);

/**
 * Czeka na turę wskazaną przez wartość 'turn'.
 */
void czekaj_na_ture(int turn, volatile sig_atomic_t *shutdown);

/**
 * Sygnalizuje zmianę tury podsumowania.
 */
void sygnalizuj_ture(void);

/**
 * Parsuje int z napisu lub kończy proces przy błędzie.
 */
int parsuj_int_lub_zakoncz(const char *what, const char *s);

/**
 * Inicjalizuje generator liczb pseudolosowych (rand()) dla bieżącego procesu.
 */
void zainicjuj_losowosc(void);

/**
 * Zamyka wszystkie procesy klientów i czyści stan: stoliki + kolejkę wejściową.
 * Przydatne przy nagłym zamknięciu (Ctrl+C / decyzja kierownika).
 */
void zakoncz_klientow_i_wyczysc_stoliki_i_kolejke(void);

#endif // COMMON_H
