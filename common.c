#define _GNU_SOURCE
#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>

// ====== ZMIENNE GLOBALNE (DEFINICJE) ======
int shm_id, sem_id;                            // pamięć współdzielona i semafory
int msgq_id;                                   // kolejka komunikatów
struct Stolik *stoliki;                        // stoły
int *restauracja_otwarta;                      // czy restauracja jest otwarta
int *kuchnia_dania_wydane;                     // kuchnia - liczba wydanych dań
int *kasa_dania_sprzedane;                     // kasa - liczba sprzedanych dań
struct Talerzyk *tasma;                        // taśma, reprezentowana jako tablica
int *kolej_podsumowania;                       // kolejka podsumowania
pid_t pid_obsluga, pid_kucharz, pid_kierownik; // pid-y procesów

pid_t *pid_obsluga_shm;   // wskaźnik na PID procesu obsługi w pamięci współdzielonej
pid_t *pid_kierownik_shm; // wskaźnik na PID procesu kierownika w pamięci współdzielonej

const int ILOSC_STOLIKOW[4] = {X1, X2, X3, X4};         // liczba stolików o pojemności 1,2,3,4
const int CENY_DAN[6] = {p10, p15, p20, p40, p50, p60}; // ceny dań

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

// ====== przeliczenia ======
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
int parsuj_int_lub_zakoncz(const char *what, const char *s) // parsuje int z napisu lub kończy proces przy błędzie
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
static volatile sig_atomic_t *shutdown_flag_ptr = NULL;

void ustaw_shutdown_flag(volatile sig_atomic_t *flag)
{
    shutdown_flag_ptr = flag;
}

void czekaj_na_ture(int turn, volatile sig_atomic_t *shutdown) // czeka na turę wskazaną przez wartość 'turn'
{
    LOGI("czekaj_na_ture: pid=%d waiting for turn=%d\n", (int)getpid(), turn);
    while (*kolej_podsumowania != turn && !*shutdown)
    {
        sem_operacja(SEM_TURA, -1);
    }
    LOGI("czekaj_na_ture: pid=%d done waiting for turn=%d current=%d\n", (int)getpid(), turn, *kolej_podsumowania);
}

void sygnalizuj_ture(void) // sygnalizuje zmianę tury podsumowania
{
    sem_operacja(SEM_TURA, 1);
}

// ====== STOLIKI ======
int znajdz_stolik_dla_grupy_zablokowanej(const struct Grupa *g) // znajduje odpowiedni stolik dla grupy (zakłada, że semafor stolików jest zablokowany)
{
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        if (stoliki[i].zajete_miejsca + g->osoby <= stoliki[i].pojemnosc &&
            stoliki[i].liczba_grup < MAX_GRUP_NA_STOLIKU)
        {
            return i;
        }
    }
    return -1;
}

void generator_stolikow(struct Stolik *stoliki_local) // generuje stoliki w restauracji
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

            LOGI("Stolik %d o pojemności %d utworzony.\n",
                 stoliki_local[idx].numer_stolika,
                 stoliki_local[idx].pojemnosc);
        }
    }
}

// ====== TASMA ======
void dodaj_danie(struct Talerzyk *tasma_local, int cena) // dodaje danie na taśmę
{
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
}

// ====== operacje IPC ======
void sem_operacja(int sem, int val) // zrobienie operacji na semaforze
{
    struct sembuf sb = {sem, val, SEM_UNDO}; // SEM_UNDO aby uniknąć deadlocka przy nagłym zakończeniu procesu
    for (;;)
    {
        LOGI("sem_operacja: pid=%d sem=%d val=%d\n", (int)getpid(), sem, val);
        if (semop(sem_id, &sb, 1) == 0) // operacja zakończona sukcesem
        {
            LOGI("sem_operacja: pid=%d sem=%d val=%d done\n", (int)getpid(), sem, val);
            return;
        }

        if (errno == EINTR) // przerwane przez sygnał
        {
            if (shutdown_flag_ptr && *shutdown_flag_ptr)
            {
                LOGI("sem_operacja: pid=%d interrupted by signal during shutdown, exiting\n", (int)getpid());
                exit(0);
            }
            continue;
        }

        if (errno == EIDRM || errno == EINVAL) // zasób IPC usunięty lub nieprawidłowy
            exit(0);

        exit(1);
    }
}

void stworz_ipc(void) // tworzy zasoby IPC (pamięć współdzieloną i semafory)
{
    int bufor_size = sizeof(struct Stolik) * MAX_STOLIKI + // pamięć na stoliki
                     sizeof(struct Talerzyk) * MAX_TASMA + // pamięć na taśmę
                     sizeof(int) * (6 * 2 + 2) +           // pamięć na liczniki dań i flagi
                     sizeof(pid_t) * 2;                    // pamięć na PID-y procesów

    shm_id = shmget(IPC_PRIVATE, bufor_size, IPC_CREAT | 0600); // utwórz pamięć współdzieloną
    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);        // dołącz pamięć współdzieloną
    if (pamiec_wspoldzielona == (void *)-1)
    {
        LOGE_ERRNO("shmat");
        exit(1);
    }
    memset(pamiec_wspoldzielona, 0, bufor_size); // wyczyść pamięć współdzieloną

    stoliki = (struct Stolik *)pamiec_wspoldzielona;    // wskaźnik na stoliki
    tasma = (struct Talerzyk *)(stoliki + MAX_STOLIKI); // wskaźnik na taśmę
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);  // kuchnia - liczba wydanych dań
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;    // kasa - liczba sprzedanych dań
    restauracja_otwarta = kasa_dania_sprzedane + 6;     // flaga czy restauracja jest otwarta
    kolej_podsumowania = restauracja_otwarta + 1;       // kolejka podsumowania

    pid_obsluga_shm = (pid_t *)(kolej_podsumowania + 1); // wskaźnik na PID procesu obsługi w pamięci współdzielonej
    pid_kierownik_shm = pid_obsluga_shm + 1;             // wskaźnik na PID procesu kierownika w pamięci współdzielonej

    sem_id = semget(IPC_PRIVATE, 6, IPC_CREAT | 0600);    // utwórz semafory (dodatkowy do licznika wiadomości i kierownika)
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);               // semafor do ochrony dostępu do stolików
    semctl(sem_id, SEM_TASMA, SETVAL, 1);                 // semafor do ochrony dostępu do taśmy
    // Ustawiamy limit pojemności kolejki, ale rezerwujemy kilka slotów
    // (KOLEJKA_REZERWA) aby kolejka nigdy nie była całkowicie zapełniona.
    semctl(sem_id, SEM_KOLEJKA, SETVAL, MAX_KOLEJKA_MSG - KOLEJKA_REZERWA); // semafor-limit pojemności kolejki komunikatów
    semctl(sem_id, SEM_TURA, SETVAL, 0);                  // semafor sygnalizujący zmianę tury
    semctl(sem_id, SEM_MSGS, SETVAL, 0);                  // liczba wiadomości w kolejce (prod/cons)
    semctl(sem_id, SEM_KIEROWNIK, SETVAL, 0);             // semafor wybudzający kierownika

    msgq_id = msgget(IPC_PRIVATE, IPC_CREAT | 0600); // utwórz kolejkę komunikatów
    if (msgq_id < 0)
    {
        LOGE_ERRNO("msgget");
        exit(1);
    }
}

void dolacz_ipc(int shm_id_existing, int sem_id_existing) // dołącza do istniejących zasobów IPC po exec()
{
    shm_id = shm_id_existing; // dołącz istniejącą pamięć współdzieloną
    sem_id = sem_id_existing; // dołącz istniejące semafory

    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0); // dołącz pamięć współdzieloną
    if (pamiec_wspoldzielona == (void *)-1)
    {
        LOGE_ERRNO("shmat");
        exit(1);
    }

    stoliki = (struct Stolik *)pamiec_wspoldzielona;
    tasma = (struct Talerzyk *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    restauracja_otwarta = kasa_dania_sprzedane + 6;
    kolej_podsumowania = restauracja_otwarta + 1;

    pid_obsluga_shm = (pid_t *)(kolej_podsumowania + 1);
    pid_kierownik_shm = pid_obsluga_shm + 1;
}

// ====== kolejka ======

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
        if (!*restauracja_otwarta)
            return;

        // Najpierw zarezerwuj miejsce w kolejce (ograniczenie liczby komunikatów).
        // Używamy semtimedop, aby okresowo sprawdzać flagę zamknięcia.
        struct sembuf sb = {SEM_KOLEJKA, -1, SEM_UNDO};
        struct timespec ts = {.tv_sec = 1, .tv_nsec = 0};
        if (semtimedop(sem_id, &sb, 1, &ts) != 0)
        {
            if (errno == EINTR)
            {
                if (shutdown_flag_ptr && *shutdown_flag_ptr)
                    exit(0);
                continue;
            }
            if (errno == EAGAIN)
            {
                if (!*restauracja_otwarta)
                    return;
                continue;
            }
            if (errno == EIDRM || errno == EINVAL)
                exit(0);
            return;
        }

        if (msgsnd(msgq_id, &msg, sizeof(msg.grupa), 0) == 0)
        {
            /* Informujemy konsumentów, że jest nowa wiadomość. */
            LOGI("kolejka_dodaj: pid=%d sent message pid=%d\n", (int)getpid(), msg.grupa.proces_id);
            sem_operacja(SEM_MSGS, 1);
            return;
        }

        // Nie wysłano komunikatu, więc zwolnij zarezerwowany slot.
        sem_operacja(SEM_KOLEJKA, 1);

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            continue; // kolejka pełna - spróbuj ponownie
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return;
    }
}

struct Grupa kolejka_pobierz(void) // pobiera grupę z kolejki
{
    struct Grupa g = {0};
    QueueMsg msg;
    /* Use non-blocking receive so we can react quickly to shutdown.
       If no message is available, sleep briefly and retry. We still
       release the slot semaphore after successful receive. */
    /* Use a message-count semaphore to wait for messages without busy-polling.
       We attempt a non-blocking decrement on SEM_MSGS; if it succeeds, a
       message should be available and we retrieve it with non-blocking msgrcv.
       If no message is available or the restaurant is closing, we return. */
    /* Block on the message-count semaphore (wait for a producer). Using a
        blocking semop lets consumers sleep here until a message is available
        while still handling EINTR and shutdown cleanly. */
    struct sembuf sb = {SEM_MSGS, -1, SEM_UNDO};
    for (;;)
    {
        LOGI("kolejka_pobierz: pid=%d waiting SEM_MSGS\n", (int)getpid());
        if (semop(sem_id, &sb, 1) == 0)
        {
            LOGI("kolejka_pobierz: pid=%d got SEM_MSGS\n", (int)getpid());
            /* We reserved one message token; try to receive the message. */
            ssize_t r = msgrcv(msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
            if (r >= 0)
            {
                /* Zwalniamy miejsce w kolejce dopiero po zdjęciu komunikatu. */
                LOGI("kolejka_pobierz: pid=%d received message pid=%d\n", (int)getpid(), msg.grupa.proces_id);
                sem_operacja(SEM_KOLEJKA, 1);
                return msg.grupa;
            }

            /* If receive failed, restore the SEM_MSGS token and handle errors. */
            LOGI("kolejka_pobierz: pid=%d msgrcv failed errno=%d\n", (int)getpid(), errno);
            sem_operacja(SEM_MSGS, 1);

            if (errno == EINTR)
                continue;

            if (errno == ENOMSG)
                continue; /* unexpected, but retry */

            if (errno == EIDRM || errno == EINVAL)
                exit(0);

            return g;
        }

        if (errno == EINTR)
        {
            /* Interrupted by signal — check if we should exit. */
            if (!*restauracja_otwarta)
                return g;
            continue;
        }

        if (errno == EAGAIN)
        {
            /* No messages presently — if restaurant closed, return; otherwise sleep briefly. */
            if (!*restauracja_otwarta)
                return g;

            struct timespec req = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000}; // 10ms
            (void)rest_nanosleep(&req, NULL);
            continue;
        }

        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return g;
    }
}

void zakoncz_klientow_i_wyczysc_stoliki_i_kolejke(void) // kończy wszystkich klientów i czyści stan stolików i kolejki
{
    LOGI("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d start\n", (int)getpid());

    // Klienci usadzeni przy stolikach.
    LOGI("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d locking SEM_STOLIKI\n", (int)getpid());
    sem_operacja(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        for (int j = 0; j < stoliki[i].liczba_grup; j++)
        {
            pid_t pid = stoliki[i].grupy[j].proces_id;
            if (pid > 0)
            {
                LOGI("zakoncz_klientow: pid=%d killing seated client pid=%d at stolik=%d\n", (int)getpid(), (int)pid, i + 1);
                (void)kill(pid, SIGTERM);
            }
        }

        memset(stoliki[i].grupy, 0, sizeof(stoliki[i].grupy));
        stoliki[i].liczba_grup = 0;
        stoliki[i].zajete_miejsca = 0;
    }
    sem_operacja(SEM_STOLIKI, 1);

    // Klienci w kolejce wejściowej.
    LOGI("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d cleaning queue\n", (int)getpid());
    QueueMsg msg;
    for (;;)
    {
        ssize_t r = msgrcv(msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
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
        LOGI("zakoncz_klientow: pid=%d popped queued client pid=%d\n", (int)getpid(), (int)pid);
        if (pid > 0)
        {
            LOGI("zakoncz_klientow: pid=%d killing queued client pid=%d\n", (int)getpid(), (int)pid);
            (void)kill(pid, SIGTERM);
        }

        // Zdjęliśmy komunikat z kolejki, więc zwalniamy slot semafora.
        LOGI("zakoncz_klientow: pid=%d releasing SEM_KOLEJKA slot\n", (int)getpid());
        sem_operacja(SEM_KOLEJKA, 1);
    }
    LOGI("zakoncz_klientow_i_wyczysc_stoliki_i_kolejke: pid=%d done\n", (int)getpid());
}
