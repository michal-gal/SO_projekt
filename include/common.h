
#ifndef COMMON_H
#define COMMON_H

/* Wspólne definicje, typy i prototypy używane w modułach. Plik centralizuje
 * konfigurację oraz `CommonCtx` używany przez procesy współpracujące przez
 * pamięć współdzieloną i semafory. Trzymaj ten nagłówek możliwie minimalny.
 */

#include "log.h"

#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <stddef.h>

/* Małe pomocniki i stałe */
#define NSEC_PER_MSEC 1000000L

static inline int usypiaj_ms(unsigned ms)
{
  struct timespec req;
  req.tv_sec = ms / 1000;
  req.tv_nsec = (long)(ms % 1000) * NSEC_PER_MSEC;
  return nanosleep(&req, NULL);
}

#define SUMMARY_WAIT_SECONDS 2
#define SHUTDOWN_TERM_TIMEOUT 4
#define SHUTDOWN_KILL_TIMEOUT 2
#define KIEROWNIK_INTERVAL_DEFAULT 1

#define POLL_MS_SHORT 50
#define POLL_MS_MED 100
#define POLL_MS_LONG 200

#define X1 10
#define X2 10
#define X3 10
#define X4 10

#define MAX_STOLIKI (X1 + X2 + X3 + X4)

#define MAX_TASMA 150
#define MAX_KOLEJKA_MSG 1024
#define KOLEJKA_REZERWA 5
#define p10 10
#define p15 15
#define p20 20
#define p40 40
#define p50 50
#define p60 60
#define MAX_GRUP_NA_STOLIKU 4
#define TP 10
#define TK 20

#define LICZBA_GRUP_DEFAULT 5000
#define CZAS_PRACY_DEFAULT (TK - TP)
#define CZAS_PRACY (TK - TP)
#define LOG_LEVEL_DEFAULT 1

extern int liczba_klientow;
extern int czas_pracy_domyslny;
extern int current_log_level;

/* Podstawowe typy współdzielone między modułami. */
struct Grupa
{
  int numer_grupy;
  pid_t proces_id;
  int osoby;
  int dzieci;
  int dorosli;
  int vip;
  int stolik_przydzielony;
  time_t wejscie;
  int pobrane_dania[6];
  int danie_specjalne;
};

struct Stolik
{
  int numer_stolika;
  int pojemnosc;
  struct Grupa grupy[MAX_GRUP_NA_STOLIKU];
  int liczba_grup;
  int zajete_miejsca;
};

struct Talerzyk
{
  int cena;
  int stolik_specjalny;
};

struct TasmaSync
{
  pthread_mutex_t mutex;
  pthread_cond_t not_full;
  pthread_cond_t not_empty;
  int count;
};

struct StolikiSync
{
  pthread_mutex_t mutex;
  pthread_cond_t cond;
};

struct QueueSync
{
  pthread_mutex_t mutex;
  pthread_cond_t not_full;
  pthread_cond_t not_empty;
  int count;
  int max;
};

typedef struct // komunikat kolejki
{
  long mtype;
  struct Grupa grupa;
} QueueMsg;

/* Centralny kontekst uruchomienia współdzielony przez wskaźniki w shm. */
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
  /* Usunięto: int *kolej_podsumowania; używamy semaforów tur. */
  int *klienci_w_kolejce;
  int *klienci_przyjeci;
  int *klienci_opuscili;
  pid_t pid_obsluga;
  pid_t pid_kucharz;
  pid_t pid_kierownik;
  pid_t pid_szatnia;
  pid_t *pid_obsluga_shm;
  pid_t *pid_kierownik_shm;
  int disable_close;
  volatile sig_atomic_t *shutdown_flag_ptr;
};

extern struct CommonCtx *common_ctx;

extern const int ILOSC_STOLIKOW[4];
extern const int CENY_DAN[6];

/* Indeksy semaforów używane w modułach */
#define SEM_TURA 0
#define SEM_KIEROWNIK 1
/* Dodatkowe semafory dla tur (1..3). */
#define SEM_TURA_TURN1 2
#define SEM_TURA_TURN2 3
#define SEM_TURA_TURN3 4
/* Semafory powiadomień dla rodzica (tury 2/3). */
#define SEM_PARENT_NOTIFY2 5
#define SEM_PARENT_NOTIFY3 6

/* Prototypy funkcji używanych między modułami. */
void sem_operacja(int sem, int val);
void ustaw_shutdown_flag(volatile sig_atomic_t *flag);
void ustaw_obsluge_sigterm(volatile sig_atomic_t *flag);
void kierownik_zamknij_restauracje_i_zakoncz_klientow(void);
void stworz_ipc(void);
void dolacz_ipc(int shm_id_existing, int sem_id_existing);
int dolacz_ipc_z_argv(int argc, char **argv, int potrzebuje_grupy,
                      int *out_numer_grupy);
int cena_na_indeks(int cena);
int znajdz_stolik_dla_grupy_zablokowanej(const struct Grupa *g);
void czekaj_na_ture(int turn, volatile sig_atomic_t *shutdown);
void sygnalizuj_ture_na(int turn);
int sem_czekaj_sekund(int sem_idx, int seconds);
int parsuj_int_lub_zakoncz(const char *what, const char *s);
void zainicjuj_losowosc(void);

#endif /* COMMON_H */
