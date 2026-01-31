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

int max_losowych_grup = MAX_LOSOWYCH_GRUP;
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
    while (*common_ctx->kolej_podsumowania != turn && !*shutdown)
    {
        sem_operacja(SEM_TURA, -1);
    }
}

void sygnalizuj_ture(void) // sygnalizuje zmianę tury podsumowania
{
    sem_operacja(SEM_TURA, 1);
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

void generator_stolikow(
    struct Stolik *stoliki_local) // generuje stoliki w restauracji
{
    int idx = 0;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < ILOSC_STOLIKOW[i]; j++)
        {
            int suma_poprzednich = 0;
            for (int k = 0; k < i; k++)
                suma_poprzednich += ILOSC_STOLIKOW[k];

            idx = suma_poprzednich + j;
            stoliki_local[idx].numer_stolika = idx + 1;
            stoliki_local[idx].pojemnosc = i + 1;
            stoliki_local[idx].liczba_grup = 0;
            stoliki_local[idx].zajete_miejsca = 0;
            memset(stoliki_local[idx].grupy, 0, sizeof(stoliki_local[idx].grupy));

            LOGP("Stolik %d o pojemności %d utworzony.\n",
                 stoliki_local[idx].numer_stolika, stoliki_local[idx].pojemnosc);
        }
    }
}

// ====== TASMA ======
void dodaj_danie(struct Talerzyk *tasma_local,
                 int cena) // dodaje danie na taśmę
{
    while (common_ctx->tasma_sync->count >= MAX_TASMA)
    {
        (void)pthread_cond_wait(&common_ctx->tasma_sync->not_full, &common_ctx->tasma_sync->mutex);
    }

    do
    {
        struct Talerzyk ostatni = tasma_local[MAX_TASMA - 1];

        for (int i = MAX_TASMA - 1; i > 0; i--)
        {
            tasma_local[i] = tasma_local[i - 1];
        }

        tasma_local[0] = ostatni; // WRACA NA POCZĄTEK
    } while (tasma_local[0].cena != 0);

    tasma_local[0].cena = cena;
    tasma_local[0].stolik_specjalny = 0;
    common_ctx->tasma_sync->count++;
    pthread_cond_signal(&common_ctx->tasma_sync->not_empty);
    LOGD("dodaj_danie: wydano danie za %d zł na taśmę (count=%d)\n", cena,
         common_ctx->tasma_sync->count);
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

    common_ctx->stoliki = (struct Stolik *)pamiec_wspoldzielona;                // wskaźnik na stoliki
    common_ctx->tasma = (struct Talerzyk *)(common_ctx->stoliki + MAX_STOLIKI); // wskaźnik na taśmę
    common_ctx->kuchnia_dania_wydane = (int *)(common_ctx->tasma + MAX_TASMA);  // kuchnia - liczba wydanych dań
    common_ctx->kasa_dania_sprzedane = common_ctx->kuchnia_dania_wydane + 6;    // kasa - liczba sprzedanych dań
    common_ctx->restauracja_otwarta = common_ctx->kasa_dania_sprzedane + 6;     // flaga czy restauracja jest otwarta
    common_ctx->kolej_podsumowania = common_ctx->restauracja_otwarta + 1;       // kolejka podsumowania
    common_ctx->klienci_w_kolejce = common_ctx->kolej_podsumowania + 1;         // statystyka: klienci w kolejce
    common_ctx->klienci_przyjeci = common_ctx->klienci_w_kolejce + 1;           // statystyka: klienci przyjęci
    common_ctx->klienci_opuscili = common_ctx->klienci_przyjeci + 1;            // statystyka: klienci którzy opuścili

    common_ctx->pid_obsluga_shm = (pid_t *)(common_ctx->klienci_opuscili + 1); // wskaźnik na PID procesu obsługi w pamięci współdzielonej
    common_ctx->pid_kierownik_shm = common_ctx->pid_obsluga_shm + 1;           // wskaźnik na PID procesu kierownika w pamięci współdzielonej
    common_ctx->stoliki_sync = (struct StolikiSync *)(common_ctx->pid_kierownik_shm + 1);
    common_ctx->tasma_sync = (struct TasmaSync *)(common_ctx->stoliki_sync + 1);
    common_ctx->queue_sync = (struct QueueSync *)(common_ctx->tasma_sync + 1);

    common_ctx->tasma_sync->count = 0;
    pthread_mutexattr_t mattr;
    pthread_condattr_t cattr;
    if (pthread_mutexattr_init(&mattr) != 0 ||
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutex_init(&common_ctx->tasma_sync->mutex, &mattr) != 0)
    {
        LOGE("Nie udało się zainicjalizować mutexa taśmy\n");
        exit(1);
    }
    if (pthread_condattr_init(&cattr) != 0 ||
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_cond_init(&common_ctx->tasma_sync->not_full, &cattr) != 0 ||
        pthread_cond_init(&common_ctx->tasma_sync->not_empty, &cattr) != 0)
    {
        LOGE("Nie udało się zainicjalizować cond taśmy\n");
        exit(1);
    }
    (void)pthread_mutexattr_destroy(&mattr);
    (void)pthread_condattr_destroy(&cattr);

    /* Zainicjalizuj mutex stolików (współdzielony między procesami) */
    if (pthread_mutexattr_init(&mattr) != 0 ||
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutex_init(&common_ctx->stoliki_sync->mutex, &mattr) != 0)
    {
        LOGE("Nie udało się zainicjalizować mutexa stolików\n");
        exit(1);
    }
    /* Zainicjalizuj cond dla stolików (współdzielony między procesami) */
    if (pthread_condattr_init(&cattr) != 0 ||
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_cond_init(&common_ctx->stoliki_sync->cond, &cattr) != 0)
    {
        LOGE("Nie udało się zainicjalizować cond stolików\n");
        exit(1);
    }
    (void)pthread_mutexattr_destroy(&mattr);
    (void)pthread_condattr_destroy(&cattr);

    /* Zainicjalizuj synchronizację kolejki (współdzielona między procesami) */
    common_ctx->queue_sync->count = 0;
    common_ctx->queue_sync->max = MAX_KOLEJKA_MSG - KOLEJKA_REZERWA;
    if (pthread_mutexattr_init(&mattr) != 0 ||
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutex_init(&common_ctx->queue_sync->mutex, &mattr) != 0)
    {
        LOGE("Nie udało się zainicjalizować mutexa kolejki\n");
        exit(1);
    }
    if (pthread_condattr_init(&cattr) != 0 ||
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_cond_init(&common_ctx->queue_sync->not_full, &cattr) != 0 ||
        pthread_cond_init(&common_ctx->queue_sync->not_empty, &cattr) != 0)
    {
        LOGE("Nie udało się zainicjalizować cond kolejki\n");
        exit(1);
    }
    (void)pthread_mutexattr_destroy(&mattr);
    (void)pthread_condattr_destroy(&cattr);

    common_ctx->sem_id = semget(IPC_PRIVATE, 2,
                                IPC_CREAT | 0600);        // utwórz semafory (tura + kierownik)
    semctl(common_ctx->sem_id, SEM_TURA, SETVAL, 0);      // semafor sygnalizujący zmianę tury
    semctl(common_ctx->sem_id, SEM_KIEROWNIK, SETVAL, 0); // semafor wybudzający kierownika

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

    common_ctx->stoliki = (struct Stolik *)pamiec_wspoldzielona;
    common_ctx->tasma = (struct Talerzyk *)(common_ctx->stoliki + MAX_STOLIKI);
    common_ctx->kuchnia_dania_wydane = (int *)(common_ctx->tasma + MAX_TASMA);
    common_ctx->kasa_dania_sprzedane = common_ctx->kuchnia_dania_wydane + 6;
    common_ctx->restauracja_otwarta = common_ctx->kasa_dania_sprzedane + 6;
    common_ctx->kolej_podsumowania = common_ctx->restauracja_otwarta + 1;
    common_ctx->klienci_w_kolejce = common_ctx->kolej_podsumowania + 1;
    common_ctx->klienci_przyjeci = common_ctx->klienci_w_kolejce + 1;
    common_ctx->klienci_opuscili = common_ctx->klienci_przyjeci + 1;

    common_ctx->pid_obsluga_shm = (pid_t *)(common_ctx->klienci_opuscili + 1);
    common_ctx->pid_kierownik_shm = common_ctx->pid_obsluga_shm + 1;
    common_ctx->stoliki_sync = (struct StolikiSync *)(common_ctx->pid_kierownik_shm + 1);
    common_ctx->tasma_sync = (struct TasmaSync *)(common_ctx->stoliki_sync + 1);
    common_ctx->queue_sync = (struct QueueSync *)(common_ctx->tasma_sync + 1);
}

// ====== KOLEJKA ======
typedef struct // komunikat kolejki
{
    long mtype;
    struct Grupa grupa;
} QueueMsg;

void kolejka_dodaj(struct Grupa g) // dodaje grupę do kolejki
{
    QueueMsg msg;
    msg.mtype = 1;
    msg.grupa = g;
    for (;;)
    {
        if (!*common_ctx->restauracja_otwarta)
            return;

        /* Zarezerwuj slot używając queue_sync. Blokuj z timeoutem podobnym do
         * semtimedop. */
        struct timespec now, abstime;
        clock_gettime(CLOCK_REALTIME, &now);
        abstime = now;
        abstime.tv_sec += 1; /* 1s timeout */

        if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) != 0)
            continue;
        while (common_ctx->queue_sync->count >= common_ctx->queue_sync->max)
        {
            int rc = pthread_cond_timedwait(&common_ctx->queue_sync->not_full, &common_ctx->queue_sync->mutex,
                                            &abstime);
            if (rc == ETIMEDOUT)
            {
                if (!*common_ctx->restauracja_otwarta)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    return;
                }
                /* przeliczenie abstime dla następnego czekania */
                clock_gettime(CLOCK_REALTIME, &now);
                abstime = now;
                abstime.tv_sec += 1;
                continue;
            }
            if (rc != 0)
            {
                /* przerwane lub błąd */
                if (common_ctx->shutdown_flag_ptr && *common_ctx->shutdown_flag_ptr)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    exit(0);
                }
                /* spróbuj ponownie */
                continue;
            }
        }

        /* Mamy zarezerwowany slot logicznie; spróbuj wysłać wiadomość. */
        if (msgsnd(common_ctx->msgq_id, &msg, sizeof(msg.grupa), IPC_NOWAIT) == 0)
        {
            common_ctx->queue_sync->count++;
            /* Zliczamy rzeczywistą liczbę klientów (osób), nie tylko grup. */
            (*common_ctx->klienci_w_kolejce) += msg.grupa.osoby;
            pthread_cond_signal(&common_ctx->queue_sync->not_empty);
            pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            return;
        }

        /* Nie udało się wysłać - zwolnij zarezerwowany slot. */
        pthread_mutex_unlock(&common_ctx->queue_sync->mutex);

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            continue; /* kolejka pełna - spróbuj ponownie */
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return;
    }
}

struct Grupa kolejka_pobierz(void) // pobiera grupę z kolejki
{
    struct Grupa g = {0};
    QueueMsg msg;
    /* Użyj nieblokującego odbioru, aby szybko reagować na zamknięcie.
       Jeśli nie ma wiadomości, śpij krótko i spróbuj ponownie. Nadal
       zwalniamy slot semafora po pomyślnym odbiorze. */
    /* Użyj semafora licznika wiadomości, aby czekać na wiadomości bez aktywnego
       czekania. Próbujemy nieblokującego decrementu na SEM_MSGS; jeśli się
       powiedzie, wiadomość powinna być dostępna i pobieramy ją z nieblokującym
       msgrcv. Jeśli nie ma wiadomości lub restauracja się zamyka, zwracamy. */
    /* Blokuj na semaforze licznika wiadomości (czekaj na producenta). Używanie
       blokującego semop pozwala konsumentom spać tutaj, dopóki wiadomość nie
       będzie dostępna, jednocześnie obsługując EINTR i zamknięcie czysto. */
    for (;;)
    {
        if (!*common_ctx->restauracja_otwarta)
            return g;

        if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) != 0)
            continue;
        struct timespec now, abstime;
        clock_gettime(CLOCK_REALTIME, &now);
        abstime = now;
        abstime.tv_sec += 1; /* 1s timeout */
        while (common_ctx->queue_sync->count <= 0)
        {
            if (!*common_ctx->restauracja_otwarta)
            {
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                return g;
            }
            int rc = pthread_cond_timedwait(&common_ctx->queue_sync->not_empty,
                                            &common_ctx->queue_sync->mutex, &abstime);
            if (rc == ETIMEDOUT)
            {
                if (!*common_ctx->restauracja_otwarta ||
                    (common_ctx->shutdown_flag_ptr && *common_ctx->shutdown_flag_ptr))
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    return g;
                }
                clock_gettime(CLOCK_REALTIME, &now);
                abstime = now;
                abstime.tv_sec += 1;
                continue;
            }
            if (rc != 0)
            {
                if (common_ctx->shutdown_flag_ptr && *common_ctx->shutdown_flag_ptr)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    exit(0);
                }
                continue;
            }
        }

        /* Zarezerwuj jeden token wiadomości i spróbuj odebrać bez blokowania. */
        common_ctx->queue_sync->count--;
        pthread_cond_signal(&common_ctx->queue_sync->not_full);
        pthread_mutex_unlock(&common_ctx->queue_sync->mutex);

        ssize_t r = msgrcv(common_ctx->msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
        if (r >= 0)
        {
            if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) == 0)
            {
                if (*common_ctx->klienci_w_kolejce >= msg.grupa.osoby)
                    *common_ctx->klienci_w_kolejce -= msg.grupa.osoby;
                else
                    *common_ctx->klienci_w_kolejce = 0;
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            }
            return msg.grupa;
        }

        /* Jeśli odbiór się nie udał, przywróć token i obsłuż błędy. */
        LOGD("kolejka_pobierz: pid=%d msgrcv failed errno=%d\n", (int)getpid(),
             errno);
        if (errno == EINTR)
        {
            /* Przywróć count, ponieważ wiadomość nie została zużyta. */
            if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) == 0)
            {
                common_ctx->queue_sync->count++;
                pthread_cond_signal(&common_ctx->queue_sync->not_empty);
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            }
            continue;
        }
        if (errno == ENOMSG)
        {
            /* Count był przestarzały; przywróć licznik i spróbuj ponownie. */
            if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) == 0)
            {
                common_ctx->queue_sync->count++;
                pthread_cond_signal(&common_ctx->queue_sync->not_empty);
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            }
            continue;
        }

        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return g;
    }
}

// ====== CZYSZCZENIE ======
void zakoncz_klientow_i_wyczysc_stoliki_i_kolejke(
    void) // kończy wszystkich klientów i czyści stan stolików i kolejki
{
    LOGD("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d start\n",
         (int)getpid());

    // Klienci usadzeni przy stolikach.
    int stoliki_locked = 0;
    struct timespec lock_deadline;
    if (clock_gettime(CLOCK_REALTIME, &lock_deadline) == 0)
    {
        lock_deadline.tv_sec += 1;
        if (common_ctx->stoliki_sync && pthread_mutex_timedlock(&common_ctx->stoliki_sync->mutex, &lock_deadline) == 0)
            stoliki_locked = 1;
    }
    if (!stoliki_locked)

        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            for (int j = 0; j < common_ctx->stoliki[i].liczba_grup; j++)
            {
                pid_t pid = common_ctx->stoliki[i].grupy[j].proces_id;
                if (pid > 0)
                {
                    (void)kill(pid, SIGTERM);
                }
            }

            memset(common_ctx->stoliki[i].grupy, 0, sizeof(common_ctx->stoliki[i].grupy));
            common_ctx->stoliki[i].liczba_grup = 0;
            common_ctx->stoliki[i].zajete_miejsca = 0;
        }
    if (stoliki_locked)
        pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

    // Klienci w kolejce wejściowej.
    LOGD("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d cleaning queue\n",
         (int)getpid());
    QueueMsg msg;
    for (;;)
    {
        ssize_t r = msgrcv(common_ctx->msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
        if (r < 0)
        {
            if (errno == ENOMSG)
                break;
            if (errno == EINTR)
                continue;
            if (errno == EIDRM || errno == EINVAL)
                return;
            break;
        }

        pid_t pid = msg.grupa.proces_id;
        LOGD("zakoncz_klientow: pid=%d popped queued client pid=%d\n",
             (int)getpid(), (int)pid);
        if (pid > 0)
        {
            LOGD("zakoncz_klientow: pid=%d killing queued client pid=%d\n",
                 (int)getpid(), (int)pid);
            (void)kill(pid, SIGTERM);
        }

        // Zdjęliśmy komunikat z kolejki, więc zwalniamy slot w liczniku queue_sync.
        /* Zaktualizuj queue_sync, aby zwolnić slot */
        if (pthread_mutex_trylock(&common_ctx->queue_sync->mutex) == 0)
        {
            if (common_ctx->queue_sync->count > 0)
                common_ctx->queue_sync->count--; /* defensywnie: upewnij się, że nie ujemne */
            /* Zmniejsz liczbę klientów w kolejce o rozmiar tej grupy. */
            if (common_ctx->klienci_w_kolejce && msg.grupa.osoby > 0)
            {
                if (*common_ctx->klienci_w_kolejce >= msg.grupa.osoby)
                    *common_ctx->klienci_w_kolejce -= msg.grupa.osoby;
                else
                    *common_ctx->klienci_w_kolejce = 0;
            }
            pthread_cond_signal(&common_ctx->queue_sync->not_full);
            pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
        }
        else
        {
        }
    }
    LOGD("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d done\n",
         (int)getpid());
}

// Funkcja dla kierownika do zamknięcia restauracji
void kierownik_zamknij_restauracje_i_zakoncz_klientow(void)
{
    if (common_ctx->restauracja_otwarta)
        *common_ctx->restauracja_otwarta = 0;
}
