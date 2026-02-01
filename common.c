#define _GNU_SOURCE
#include "common.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

// `struct CommonCtx` is defined in common.h; instantiate storage here.
struct CommonCtx common_ctx_storage = {0};
struct CommonCtx *common_ctx = &common_ctx_storage;

static void init_shared_mutex(pthread_mutex_t *mutex, const char *err)
{
    pthread_mutexattr_t mattr;
    if (pthread_mutexattr_init(&mattr) != 0 ||
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutex_init(mutex, &mattr) != 0)
    {
        LOGE("%s", err);
        exit(1);
    }
    (void)pthread_mutexattr_destroy(&mattr);
}

static void init_shared_cond(pthread_cond_t *cond, const char *err)
{
    pthread_condattr_t cattr;
    if (pthread_condattr_init(&cattr) != 0 ||
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_cond_init(cond, &cattr) != 0)
    {
        LOGE("%s", err);
        exit(1);
    }
    (void)pthread_condattr_destroy(&cattr);
}

static void assign_shared_layout(void *base)
{
    common_ctx->stoliki = (struct Stolik *)base;
    common_ctx->tasma = (struct Talerzyk *)(common_ctx->stoliki + MAX_STOLIKI);
    common_ctx->kuchnia_dania_wydane = (int *)(common_ctx->tasma + MAX_TASMA);
    common_ctx->kasa_dania_sprzedane = common_ctx->kuchnia_dania_wydane + 6;
    common_ctx->restauracja_otwarta = common_ctx->kasa_dania_sprzedane + 6;
    common_ctx->klienci_w_kolejce = common_ctx->restauracja_otwarta + 1;
    common_ctx->klienci_przyjeci = common_ctx->klienci_w_kolejce + 1;
    common_ctx->klienci_opuscili = common_ctx->klienci_przyjeci + 1;

    common_ctx->pid_obsluga_shm = (pid_t *)(common_ctx->klienci_opuscili + 1);
    common_ctx->pid_kierownik_shm = common_ctx->pid_obsluga_shm + 1;
    common_ctx->stoliki_sync = (struct StolikiSync *)(common_ctx->pid_kierownik_shm + 1);
    common_ctx->tasma_sync = (struct TasmaSync *)(common_ctx->stoliki_sync + 1);
    common_ctx->queue_sync = (struct QueueSync *)(common_ctx->tasma_sync + 1);
}

static void init_semaphores(void)
{
    common_ctx->sem_id = semget(IPC_PRIVATE, 7, IPC_CREAT | 0600);
    int sem_idxs[] = {SEM_TURA, SEM_KIEROWNIK, SEM_TURA_TURN1,
                      SEM_TURA_TURN2, SEM_TURA_TURN3, SEM_PARENT_NOTIFY2,
                      SEM_PARENT_NOTIFY3};
    for (size_t i = 0; i < sizeof(sem_idxs) / sizeof(sem_idxs[0]); i++)
        semctl(common_ctx->sem_id, sem_idxs[i], SETVAL, 0);
}

/* Unified default for number of random groups is provided at runtime
 * (RESTAURACJA_LICZBA_KLIENTOW). Initialize to 0 here so the
 * runtime initializer (`init_restauracja`) sets the actual value. */
/* Single authoritative client count; initialized at runtime by
 * `init_restauracja()` (from argv/env), default 0 meaning "not set yet". */
int liczba_klientow = 0;
int czas_pracy_domyslny = CZAS_PRACY;

// ====== ZMIENNE GLOBALNE  ======

const int ILOSC_STOLIKOW[4] = {X1, X2, X3,
                               X4};                     // liczba stolików o pojemności 1,2,3,4
const int CENY_DAN[6] = {p10, p15, p20, p40, p50, p60}; // ceny dań

// ====== INICJALIZACJA ======
void zainicjuj_losowosc(void) // inicjalizuje generator liczb losowych
{
    log_init_from_env();
    const char *seed_env = getenv("RESTAURACJA_SEED");
    if (seed_env && *seed_env)
    {
        errno = 0;
        char *end = NULL;
        unsigned long v = strtoul(seed_env, &end, 10);
        if (errno == 0 && end && *end == '\0')
        {
            srand((unsigned)v);
            return;
        }
    }
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
}

// ====== PRZELICZENIA ======
int cena_na_indeks(int cena)
{
    switch (cena)
    {
    case p10:
        return 0;
    case p15:
        return 1;
    case p20:
        return 2;
    case p40:
        return 3;
    case p50:
        return 4;
    case p60:
        return 5;
    default:
        return -1;
    }
}

// ====== PARSOWANIE ======
int parsuj_int_lub_zakoncz(
    const char *what,
    const char *s) // parsuje int z napisu lub kończy proces przy błędzie
{
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || (end && *end != '\0'))
    {
        LOGE("Nieprawidłowa wartość %s=%s\n", what, s ? s : "(null)");
        exit(1);
    }
    return (int)v;
}

// ====== SYNCHRONIZACJA ======
void ustaw_shutdown_flag(volatile sig_atomic_t *flag)
{
    common_ctx->shutdown_flag_ptr = flag;
}

void czekaj_na_ture(int turn,
                    volatile sig_atomic_t *
                        shutdown) // czeka na turę wskazaną przez wartość 'turn'
{
    int sem_idx;
    if (turn == 1)
        sem_idx = SEM_TURA_TURN1;
    else if (turn == 2)
        sem_idx = SEM_TURA_TURN2;
    else
        sem_idx = SEM_TURA_TURN3;

    if (!*shutdown)
    {
        sem_operacja(sem_idx, -1);
    }
}

// Try to decrement semaphore without blocking. Returns 0 on success,
// -1 if would block, exits on unrecoverable error.
int sem_trywait(int sem_idx)
{
    struct sembuf sb = {sem_idx, -1, IPC_NOWAIT};
    if (semop(common_ctx->sem_id, &sb, 1) == 0)
        return 0;
    if (errno == EAGAIN || errno == EINTR)
        return -1;
    if (errno == EIDRM || errno == EINVAL)
        exit(0);
    exit(1);
}

// Wait up to `seconds` for semaphore to be available. Returns 0 on success,
// -1 on timeout.
int sem_timedwait_seconds(int sem_idx, int seconds)
{
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;)
    {
        if (sem_trywait(sem_idx) == 0)
            return 0;
        (void)sleep_ms(POLL_MS_MED);
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - start.tv_sec) >= seconds)
            break;
    }
    return -1;
}

/* legacy wrapper removed: use sygnalizuj_ture_na(turn) directly */

void sygnalizuj_ture_na(int turn)
{
    switch (turn)
    {
    case 1:
        sem_operacja(SEM_TURA_TURN1, 1);
        break;
    case 2:
        sem_operacja(SEM_TURA_TURN2, 1);
        break;
    case 3:
        sem_operacja(SEM_TURA_TURN3, 1);
        break;
    default:
        sem_operacja(SEM_TURA, 1);
        break;
    }
}

// ====== STOLIKI ======
int znajdz_stolik_dla_grupy_zablokowanej(
    const struct Grupa *g) // znajduje odpowiedni stolik dla grupy (zakłada, że
                           // semafor stolików jest zablokowany)
{
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        if (common_ctx->stoliki[i].zajete_miejsca + g->osoby <= common_ctx->stoliki[i].pojemnosc &&
            common_ctx->stoliki[i].liczba_grup < MAX_GRUP_NA_STOLIKU)
        {
            return i;
        }
    }
    return -1;
}

// ====== OPERACJE IPC ======
void sem_operacja(int sem, int val) // wykonuje operację na semaforze
{
    struct sembuf sb = {sem, val, SEM_UNDO}; // SEM_UNDO aby uniknąć deadlocka
                                             // przy nagłym zakończeniu procesu
    for (;;)
    {
        if (semop(common_ctx->sem_id, &sb, 1) == 0) // operacja zakończona sukcesem
        {
            return;
        }

        if (errno == EINTR) // przerwane przez sygnał
        {
            if (common_ctx->shutdown_flag_ptr && *common_ctx->shutdown_flag_ptr)
            {
                exit(0);
            }
            continue;
        }

        if (errno == EIDRM ||
            errno == EINVAL) // zasób IPC usunięty lub nieprawidłowy
            exit(0);

        exit(1);
    }
}

void stworz_ipc(void) // tworzy zasoby IPC (pamięć współdzieloną i semafory)
{
    int bufor_size =
        sizeof(struct Stolik) * MAX_STOLIKI + // pamięć na stoliki
        sizeof(struct Talerzyk) * MAX_TASMA + // pamięć na taśmę
        sizeof(int) *
            (6 * 2 + 2 + 3 + 3) +    // pamięć na liczniki dań, flagi i statystyki
        sizeof(pid_t) * 2 +          // pamięć na PID-y procesów
        sizeof(struct StolikiSync) + // synchronizacja stolików
        sizeof(struct TasmaSync) +   // synchronizacja taśmy
        sizeof(struct QueueSync);    // synchronizacja kolejki

    common_ctx->shm_id = shmget(IPC_PRIVATE, bufor_size,
                                IPC_CREAT | 0600); // utwórz pamięć współdzieloną
    void *pamiec_wspoldzielona =
        shmat(common_ctx->shm_id, NULL, 0); // dołącz pamięć współdzieloną
    if (pamiec_wspoldzielona == (void *)-1)
    {
        LOGE_ERRNO("shmat");
        exit(1);
    }
    memset(pamiec_wspoldzielona, 0, bufor_size); // wyczyść pamięć współdzieloną
    assign_shared_layout(pamiec_wspoldzielona);

    common_ctx->tasma_sync->count = 0;
    init_shared_mutex(&common_ctx->tasma_sync->mutex,
                      "Nie udało się zainicjalizować mutexa taśmy\n");
    init_shared_cond(&common_ctx->tasma_sync->not_full,
                     "Nie udało się zainicjalizować cond taśmy\n");
    init_shared_cond(&common_ctx->tasma_sync->not_empty,
                     "Nie udało się zainicjalizować cond taśmy\n");

    /* Zainicjalizuj mutex/cond stolików (współdzielone między procesami) */
    init_shared_mutex(&common_ctx->stoliki_sync->mutex,
                      "Nie udało się zainicjalizować mutexa stolików\n");
    init_shared_cond(&common_ctx->stoliki_sync->cond,
                     "Nie udało się zainicjalizować cond stolików\n");

    /* Zainicjalizuj synchronizację kolejki (współdzielona między procesami) */
    common_ctx->queue_sync->count = 0;
    common_ctx->queue_sync->max = MAX_KOLEJKA_MSG - KOLEJKA_REZERWA;
    init_shared_mutex(&common_ctx->queue_sync->mutex,
                      "Nie udało się zainicjalizować mutexa kolejki\n");
    init_shared_cond(&common_ctx->queue_sync->not_full,
                     "Nie udało się zainicjalizować cond kolejki\n");
    init_shared_cond(&common_ctx->queue_sync->not_empty,
                     "Nie udało się zainicjalizować cond kolejki\n");

    /* Semafory (otwarcie, kierownik, tury, powiadomienia) */
    init_semaphores();

    common_ctx->msgq_id = msgget(IPC_PRIVATE, IPC_CREAT | 0600); // utwórz kolejkę komunikatów
    if (common_ctx->msgq_id < 0)
    {
        LOGE_ERRNO("msgget");
        exit(1);
    }
}

void dolacz_ipc(
    int shm_id_existing,
    int sem_id_existing) // dołącza do istniejących zasobów IPC po exec()
{
    common_ctx->shm_id = shm_id_existing; // dołącz istniejącą pamięć współdzieloną
    common_ctx->sem_id = sem_id_existing; // dołącz istniejące semafory

    void *pamiec_wspoldzielona =
        shmat(common_ctx->shm_id, NULL, 0); // dołącz pamięć współdzieloną
    if (pamiec_wspoldzielona == (void *)-1)
    {
        LOGE_ERRNO("shmat");
        exit(1);
    }
    assign_shared_layout(pamiec_wspoldzielona);
}

int dolacz_ipc_z_argv(int argc, char **argv, int potrzebuje_grupy,
                      int *out_numer_grupy)
{
    int oczekiwane = potrzebuje_grupy ? 5 : 4;
    if (argc != oczekiwane)
    {
        if (potrzebuje_grupy)
            LOGE("Użycie: %s <shm_id> <sem_id> <msgq_id> <numer_grupy>\n",
                 argv[0]);
        else
            LOGE("Użycie: %s <shm_id> <sem_id> <msgq_id>\n", argv[0]);
        return 1;
    }

    int shm = parsuj_int_lub_zakoncz("shm_id", argv[1]);
    int sem = parsuj_int_lub_zakoncz("sem_id", argv[2]);
    common_ctx->msgq_id = parsuj_int_lub_zakoncz("msgq_id", argv[3]);
    if (potrzebuje_grupy && out_numer_grupy)
        *out_numer_grupy = parsuj_int_lub_zakoncz("numer_grupy", argv[4]);

    dolacz_ipc(shm, sem);
    return 0;
}

// Funkcja dla kierownika do zamknięcia restauracji
void kierownik_zamknij_restauracje_i_zakoncz_klientow(void)
{
    if (common_ctx->restauracja_otwarta)
        *common_ctx->restauracja_otwarta = 0;
}
