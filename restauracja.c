#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/sem.h>
#include "procesy.h"

// Definicje zmiennych globalnych
Kolejka *kolejka_klientow;
Tasma *tasma;
Stolik *stoly;
int *wszyscy_klienci;
int *vip_licznik;
int *sygnal_kierownika;

int kolejka_sem_id;
int tasma_sem_id;

int P, X1, X2, X3, X4, N, Tp, Tk;
FILE *raport;

int main()
{
    pid_t pid;
    srand((unsigned int)time(NULL));
    raport = fopen("raport.txt", "w");
    if (!raport)
    {
        perror("fopen raport.txt");
        exit(EXIT_FAILURE);
    }

    // pamięć współdzielona
    int shm_kolejka = shmget(IPC_PRIVATE, sizeof(Kolejka), IPC_CREAT | 0666);
    int shm_tasma = shmget(IPC_PRIVATE, sizeof(Tasma), IPC_CREAT | 0666);
    int shm_stoly = shmget(IPC_PRIVATE, sizeof(Stolik) * MAX_STOLIKI, IPC_CREAT | 0666);
    int shm_total = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_vip = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_signal = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    // mapowanie pamięci współdzielonej
    kolejka_klientow = (Kolejka *)shmat(shm_kolejka, NULL, 0);
    tasma = (Tasma *)shmat(shm_tasma, NULL, 0);
    stoly = (Stolik *)shmat(shm_stoly, NULL, 0);
    wszyscy_klienci = (int *)shmat(shm_total, NULL, 0);
    vip_licznik = (int *)shmat(shm_vip, NULL, 0);
    sygnal_kierownika = (int *)shmat(shm_signal, NULL, 0);

    // semafory System V
    kolejka_sem_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    tasma_sem_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
    union semun
    {
        int val;
        struct semid_ds *buf;
        unsigned short *array;
    } arg;
    arg.val = 1;
    semctl(kolejka_sem_id, 0, SETVAL, arg);
    semctl(tasma_sem_id, 0, SETVAL, arg);

    inicjuj_kolejka();
    inicjuj_tasma();

    *wszyscy_klienci = *vip_licznik = *sygnal_kierownika = 0;

    X1 = X2 = X3 = X4 = 10;
    N = X1 + 2 * X2 + 3 * X3 + 4 * X4;
    P = 1000;

    // Inicjalizacja stolików
    int idx = 0;
    for (int i = 0; i < X1; i++)
        stoly[idx++].pojemnosc = 1;
    printf("Utworzono %d stolików 1-osobowych.\n", X1);
    for (int i = 0; i < X2; i++)
        stoly[idx++].pojemnosc = 2;
    printf("Utworzono %d stolików 2-osobowych.\n", X2);
    for (int i = 0; i < X3; i++)
        stoly[idx++].pojemnosc = 3;
    printf("Utworzono %d stolików 3-osobowych.\n", X3);
    for (int i = 0; i < X4; i++)
        stoly[idx++].pojemnosc = 4;
    printf("Utworzono %d stolików 4-osobowych.\n", X4);

    // Tworzenie 4 procesów potomnych: klient, obsługa, kucharz, kierownik
    pid_t proces[4];
    int licznik_procesow = 0;

    pid = fork(); // proces klienta
    if (pid < 0)
    {
        perror("fork - klient_proces");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        printf("Rozpoczynanie procesu klienta\n");
        klient_proces();
        _exit(0);
    }
    else
    {
        proces[licznik_procesow++] = pid;
    }

    pid = fork(); // proces obsługi
    if (pid < 0)
    {
        perror("fork - obsluga_proces");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        printf("Rozpoczynanie procesu obsługi\n");
        obsluga_proces();
        _exit(0);
    }
    else
    {
        proces[licznik_procesow++] = pid;
    }

    pid = fork(); // proces kucharza
    if (pid < 0)
    {
        perror("fork - kucharz_proces");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        printf("Rozpoczynanie procesu kucharza\n");
        kucharz_proces();
        _exit(0);
    }
    else
    {
        proces[licznik_procesow++] = pid;
    }

    pid = fork(); // proces kierownika
    if (pid < 0)
    {
        perror("fork - kierownik_proces");
        exit(EXIT_FAILURE);
    }
    else if (pid == 0)
    {
        printf("Rozpoczynanie procesu kierownika\n");
        kierownik_proces();
        _exit(0);
    }
    else
    {
        proces[licznik_procesow++] = pid;
    }

    sleep(300); // Czas działania restauracji

    // zakończenie pracy procesów i zebranie ich
    for (int i = 0; i < licznik_procesow; i++)
    {
        kill(proces[i], SIGTERM);
    }
    for (int i = 0; i < licznik_procesow; i++)
    {
        waitpid(proces[i], NULL, 0);
    }

    fprintf(raport, "Podsumowanie: Taśma ma %d talerzyków.\n", tasma->licznik);

    // sprzątanie
    shmdt(kolejka_klientow);
    shmctl(shm_kolejka, IPC_RMID, NULL);
    shmdt(tasma);
    shmctl(shm_tasma, IPC_RMID, NULL);
    shmdt(stoly);
    shmctl(shm_stoly, IPC_RMID, NULL);
    shmdt(wszyscy_klienci);
    shmctl(shm_total, IPC_RMID, NULL);
    shmdt(vip_licznik);
    shmctl(shm_vip, IPC_RMID, NULL);
    shmdt(sygnal_kierownika);
    shmctl(shm_signal, IPC_RMID, NULL);
    semctl(kolejka_sem_id, 0, IPC_RMID);
    semctl(tasma_sem_id, 0, IPC_RMID);

    fclose(raport);
    return 0;
}
