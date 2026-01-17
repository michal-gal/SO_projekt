#include "procesy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <time.h>

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
    int bufor = sizeof(struct Kolejka) +
                sizeof(struct Stolik) * MAX_STOLIKI +
                sizeof(struct Talerzyk) * MAX_TASMA +
                sizeof(int) * 6 * 2 +
                sizeof(int) * 4 +
                sizeof(int) * 6;

    shm_id = shmget(IPC_PRIVATE, bufor, IPC_CREAT | 0600);
    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);
    if (pamiec_wspoldzielona == (void *)-1)
    {
        perror("shmat");
        exit(1);
    }
    memset(pamiec_wspoldzielona, 0, bufor);

    kolejka = pamiec_wspoldzielona;
    stoliki = (void *)(kolejka + 1);
    tasma = (struct Talerzyk *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    sygnal_kierownika = kasa_dania_sprzedane + 6;
    restauracja_otwarta = sygnal_kierownika + 1;
    aktywni_klienci = restauracja_otwarta + 1;
    kolej_podsumowania = aktywni_klienci + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666);
    semctl(sem_id, SEM_KOLEJKA, SETVAL, 1);
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);
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

    kolejka = pamiec_wspoldzielona;
    stoliki = (void *)(kolejka + 1);
    tasma = (struct Talerzyk *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    sygnal_kierownika = kasa_dania_sprzedane + 6;
    restauracja_otwarta = sygnal_kierownika + 1;
    aktywni_klienci = restauracja_otwarta + 1;
    kolej_podsumowania = aktywni_klienci + 1;
}
