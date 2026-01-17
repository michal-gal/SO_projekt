#include "procesy.h"

#include <errno.h>
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
int shm_id, sem_id;
int msgq_id;
struct Stolik *stoliki;
int *restauracja_otwarta;
int *aktywni_klienci;
int *kuchnia_dania_wydane;
int *kasa_dania_sprzedane;
struct Talerzyk *tasma;
int *kolej_podsumowania;
pid_t pid_obsluga, pid_kucharz, pid_kierownik, pid_generator;

pid_t *pid_obsluga_shm;
pid_t *pid_kierownik_shm;

const int ILOSC_STOLIKOW[4] = {X1, X2, X3, X4};
const int CENY_DAN[6] = {p10, p15, p20, p40, p50, p60};

// ====== UTILS ======
int price_to_index(int cena)
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

// ====== SYNC ======
void wait_for_turn(int turn)
{
    while (*kolej_podsumowania != turn)
    {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 50L * 1000L * 1000L; // 50ms
        nanosleep(&ts, NULL);
    }
}

void wait_until_no_active_clients(void)
{
    while (*aktywni_klienci > 0)
        sleep(1);
}

void wait_until_closed_and_no_active_clients(void)
{
    while (*restauracja_otwarta || *aktywni_klienci > 0)
        sleep(1);
}

// ====== TABLES ======
int find_table_for_group_locked(const struct Grupa *g)
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

void generator_stolikow(struct Stolik *stoliki_local)
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

            printf("Stolik %d o pojemności %d utworzony.\n",
                   stoliki_local[idx].numer_stolika,
                   stoliki_local[idx].pojemnosc);
        }
    }
}

// ====== TASMA ======
void dodaj_danie(struct Talerzyk *tasma_local, int cena)
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

    if (cena == p40 || cena == p50 || cena == p60)
        printf("Danie specjalne za %d zł dodane na taśmę.\n", cena);
    else
        printf("Danie za %d zł dodane na taśmę.\n", cena);
}

// ====== IPC ======
void sem_op(int sem, int val)
{
    struct sembuf sb = {sem, val, SEM_UNDO};
    for (;;)
    {
        if (semop(sem_id, &sb, 1) == 0)
            return;

        if (errno == EINTR)
            continue;

        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        perror("semop");
        exit(1);
    }
}

void stworz_ipc(void)
{
    srand(time(NULL));
    int bufor = sizeof(struct Stolik) * MAX_STOLIKI +
                sizeof(struct Talerzyk) * MAX_TASMA +
                sizeof(int) * (6 * 2 + 3) +
                sizeof(pid_t) * 2;

    shm_id = shmget(IPC_PRIVATE, bufor, IPC_CREAT | 0600);
    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);
    if (pamiec_wspoldzielona == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }
    memset(pamiec_wspoldzielona, 0, bufor);

    stoliki = (struct Stolik *)pamiec_wspoldzielona;
    tasma = (struct Talerzyk *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    restauracja_otwarta = kasa_dania_sprzedane + 6;
    aktywni_klienci = restauracja_otwarta + 1;
    kolej_podsumowania = aktywni_klienci + 1;

    pid_obsluga_shm = (pid_t *)(kolej_podsumowania + 1);
    pid_kierownik_shm = pid_obsluga_shm + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666);
    semctl(sem_id, SEM_AKTYWNI_KLIENCI, SETVAL, 1);
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);

    msgq_id = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (msgq_id < 0)
    {
        perror("msgget");
        exit(1);
    }
}

void dolacz_ipc(int shm_id_existing, int sem_id_existing)
{
    shm_id = shm_id_existing;
    sem_id = sem_id_existing;

    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);
    if (pamiec_wspoldzielona == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }

    stoliki = (struct Stolik *)pamiec_wspoldzielona;
    tasma = (struct Talerzyk *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    restauracja_otwarta = kasa_dania_sprzedane + 6;
    aktywni_klienci = restauracja_otwarta + 1;
    kolej_podsumowania = aktywni_klienci + 1;

    pid_obsluga_shm = (pid_t *)(kolej_podsumowania + 1);
    pid_kierownik_shm = pid_obsluga_shm + 1;
}

// ====== QUEUE (System V message queue) ======

typedef struct
{
    long mtype;
    struct Grupa grupa;
} QueueMsg;

void push(struct Grupa g)
{
    QueueMsg msg;
    msg.mtype = 1;
    msg.grupa = g;

    for (;;)
    {
        if (msgsnd(msgq_id, &msg, sizeof(msg.grupa), IPC_NOWAIT) == 0)
            return;

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            return; // kolejka pełna - drop
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        perror("msgsnd");
        return;
    }
}

struct Grupa pop(void)
{
    struct Grupa g = {0};
    QueueMsg msg;

    for (;;)
    {
        ssize_t r = msgrcv(msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
        if (r >= 0)
            return msg.grupa;

        if (errno == EINTR)
            continue;
        if (errno == ENOMSG)
            return g;
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        perror("msgrcv");
        return g;
    }
}
