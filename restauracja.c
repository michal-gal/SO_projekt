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
#include "procesy.h"

#define MAX_KOLEJKA 500
#define MAX_TASMA 500
#define MAX_STOLIKI 100

// Deklaracje zmiennych - definicje są w globals.c
extern Kolejka *kolejka_klientow;
extern Tasma *tasma;
extern Stolik *stoly;
extern int *wszyscy_klienci;
extern int *vip_licznik;
extern int *sygnal_kierownika;

extern sem_t *kolejka_sem;
extern sem_t *tasma_sem;

extern int P, X1, X2, X3, X4, N, Tp, Tk;
extern FILE *raport;

int main()
{
    srand(time(NULL));
    raport = fopen("raport.txt", "w");

    int shm_kolejka = shmget(IPC_PRIVATE, sizeof(Kolejka), IPC_CREAT | 0666);
    int shm_tasma = shmget(IPC_PRIVATE, sizeof(Tasma), IPC_CREAT | 0666);
    int shm_stoly = shmget(IPC_PRIVATE, sizeof(Stolik) * MAX_STOLIKI, IPC_CREAT | 0666);
    int shm_total = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_vip = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    int shm_signal = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);

    kolejka_klientow = (Kolejka *)shmat(shm_kolejka, NULL, 0);
    tasma = (Tasma *)shmat(shm_tasma, NULL, 0);
    stoly = (Stolik *)shmat(shm_stoly, NULL, 0);
    wszyscy_klienci = (int *)shmat(shm_total, NULL, 0);
    vip_licznik = (int *)shmat(shm_vip, NULL, 0);
    sygnal_kierownika = (int *)shmat(shm_signal, NULL, 0);

    kolejka_sem = sem_open("/kolejka_sem", O_CREAT, 0644, 1);
    tasma_sem = sem_open("/tasma_sem", O_CREAT, 0644, 1);

    inicjuj_kolejka();
    inicjuj_tasma();

    *wszyscy_klienci = *vip_licznik = *sygnal_kierownika = 0;

    X1 = X2 = X3 = X4 = 10;
    N = X1 + 2 * X2 + 3 * X3 + 4 * X4;
    P = 1000;

    int idx = 0;
    for (int i = 0; i < X1; i++)
        stoly[idx++].pojemnosc = 1;
    for (int i = 0; i < X2; i++)
        stoly[idx++].pojemnosc = 2;
    for (int i = 0; i < X3; i++)
        stoly[idx++].pojemnosc = 3;
    for (int i = 0; i < X4; i++)
        stoly[idx++].pojemnosc = 4;

    if (!fork()) //
        klient_proces();
    if (!fork())
        obsluga_proces();
    if (!fork())
        kucharz_proces();
    if (!fork())
        kierownik_proces();

    sleep(300);

    fprintf(raport, "Podsumowanie: Taśma ma %d talerzyków.\n", tasma->licznik);

    shmdt(kolejka_klientow);             // Odłączanie pamięci współdzielonej
    shmctl(shm_kolejka, IPC_RMID, NULL); // Usuwanie segmentu pamięci
    shmdt(tasma);                        // Odłączanie pamięci współdzielonej
    shmctl(shm_tasma, IPC_RMID, NULL);   // Usuwanie segmentu pamięci
    shmdt(stoly);                        // Odłączanie pamięci współdzielonej
    shmctl(shm_stoly, IPC_RMID, NULL);   // Usuwanie segmentu pamięci
    shmdt(wszyscy_klienci);              // Odłączanie pamięci współdzielonej
    shmctl(shm_total, IPC_RMID, NULL);   // Usuwanie segmentu pamięci
    shmdt(vip_licznik);                  // Odłączanie pamięci współdzielonej
    shmctl(shm_vip, IPC_RMID, NULL);     // Usuwanie segmentu pamięci
    shmdt(sygnal_kierownika);            // Odłączanie pamięci współdzielonej
    shmctl(shm_signal, IPC_RMID, NULL);  // Usuwanie segmentu pamięci
    sem_close(kolejka_sem);              // Zamknięcie semafora
    sem_unlink("/kolejka_sem");          // Usunięcie semafora
    sem_close(tasma_sem);                // Zamknięcie semafora
    sem_unlink("/tasma_sem");            // Usunięcie semafora

    fclose(raport);
    return 0;
}
