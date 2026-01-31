#ifndef COMMON_H
#define COMMON_H

// ====== INKLUDY ======
#include "log.h"

#include <pthread.h>   // pthread_mutex_t, pthread_cond_t
#include <signal.h>    // sig_atomic_t
#include <sys/types.h> // pid_t
#include <time.h>      // time_t, nanosleep

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


#define SUMMARY_WAIT_SECONDS  2 /* ile sekund czekamy na podsumowania na koniec */
#define SHUTDOWN_TERM_TIMEOUT 4 /* seconds to wait after SIGTERM */
#define SHUTDOWN_KILL_TIMEOUT 2 /* seconds to wait after SIGKILL */
/* Central defaults for various timeouts (seconds). Adjust here to affect whole
 * program. */
#define KIEROWNIK_INTERVAL_DEFAULT 1 /* seconds between manager wakes */

#define MAX_AKTYWNYCH_KLIENTOW_DEFAULT 5000 /* default cap of active clients */
#define POLL_MS_SHORT 50 /* short polling interval in ms */
#define POLL_MS_MED 100 /* medium polling interval in ms */
#define POLL_MS_LONG 200 /* long polling interval in ms */

#define X1 10  // liczba stolików o pojemności 1
#define X2 10  // liczba stolików o pojemności 2
#define X3 10  // liczba stolików o pojemności 3
#define X4 10  // liczba stolików o pojemności 4
#define p10 10 // ceny dań 1
#define p15 15 // ceny dań 2
#define p20 20 // ceny dań 3
#define p40 40 // ceny dań 4
#define p50 50 // ceny dań 5
#define p60 60 // ceny dań 6
#define MAX_OSOBY \
  (X1 * 1 + X2 * 2 + X3 * 3 + X4 * 4)   // maksymalna liczba osób przy stolikach
#define MAX_STOLIKI (X1 + X2 + X3 + X4) // maksymalna liczba stolików
#define MAX_TASMA 150                   // maksymalna długość taśmy
#define MAX_GRUP_NA_STOLIKU 4           // maksymalna liczba grup na jednym stoliku
#define TP 10                           // godzina otwarcia restauracji
#define TK 20                           // godzina zamknięcia restauracji
#ifndef CZAS_PRACY
#define CZAS_PRACY (TK - TP) // czas otwarcia restauracji w sekundach
#endif
extern int czas_pracy_domyslny;
#define LOG_LEVEL_DEFAULT 1
extern int current_log_level;
#define REZERWA_TASMA 50
#define REZERWA_STOLIKI 5
#define REZERWA_KOLEJKA 5
/* `liczba_klientow` is the single runtime-controlled source for how many
 * client groups to create. It may be set from program argument (first
 * argument) or from `RESTAURACJA_LICZBA_KLIENTOW` environment variable. */
extern int liczba_klientow;

// ====== COMMON CONTEXT ======
// Centralized storage for IPC/shared data. Use `common_ctx->...` internally
// and the macro aliases below for backward compatibility with legacy code.
struct CommonCtx
{
  int shm_id;
  int sem_id;
  int msgq_id;
  struct Stolik *stoliki;
  int *restauracja_otwarta;
  int *kuchnia_dania_wydane;
  int *kasa_dania_sprzedane;
  struct Talerzyk *tasma;
  struct TasmaSync *tasma_sync;
  struct StolikiSync *stoliki_sync;
  struct QueueSync *queue_sync;
  int *kolej_podsumowania;
  int *klienci_w_kolejce;
  int *klienci_przyjeci;
  int *klienci_opuscili;
  pid_t pid_obsluga;
  pid_t pid_kucharz;
  pid_t pid_kierownik;
  pid_t *pid_obsluga_shm;
  pid_t *pid_kierownik_shm;
  int disable_close;
  volatile sig_atomic_t *shutdown_flag_ptr;
};

extern struct CommonCtx *common_ctx;

/* Legacy macro aliases removed. Access shared data via `common_ctx->...`. */
extern const int ILOSC_STOLIKOW[4]; // liczba stolików o pojemności 1,2,3,4
extern const int CENY_DAN[6];       // ceny dań
// ====== SEMAFORY (INDEKSY) ======

// Semafor sygnalizujący zmianę tury podsumowania
#define SEM_TURA 0
// Semafor wybudzający kierownika do okresowych działań
#define SEM_KIEROWNIK 1

// Maksymalna liczba komunikatów w kolejce wejściowej (ograniczana semaforem),
// żeby nie doprowadzić do przepełnienia kolejki System V przy dużej liczbie
// klientów. Dodatkowo rezerwujemy kilka slotów, aby kolejka nigdy nie była
// całkowicie zapełniona — to pozwala na operacje "cofnij do kolejki" lub inne
// priorytetowe wpisy administracyjne.
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
  int danie_specjalne;  // jeśli zamówiono danie specjalne to jest cena dania, 0
                        // jeśli nie
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
  int stolik_specjalny; // 0 = normalne danie, >0 = numer stolika dla zamówienia
                        // specjalnego
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
  pthread_cond_t
      cond; // sygnalizuje zmiany w stanie stolików (np. danie_specjalne)
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
 * @return Struktura `struct Grupa` (pusta, jeśli kolejka jest pusta: proces_id
 * == 0).
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
void generator_stolikow(struct Stolik *stoliki_ptr);

/**
 * Funkcja dla kierownika do zamknięcia restauracji.
 */
void kierownik_zamknij_restauracje_i_zakoncz_klientow(void);

/**
 * Dodaje danie na taśmę.
 * Wymaga zablokowanego tasma_sync->mutex.
 */
void dodaj_danie(struct Talerzyk *tasma_ptr, int cena);

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
 * Znajduje odpowiedni stolik dla grupy (zakłada, że semafor stolików jest
 * zablokowany).
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
