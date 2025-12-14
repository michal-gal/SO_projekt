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

#define MAX_KOLEJKA 500
#define MAX_TASMA 500
#define MAX_STOLIKI 100

typedef struct
{
    int wielkosc;
    int VIP;
    int dzieci;
    int dorosli;
} Grupa;

typedef struct
{
    int cena;
    int do_stolika;
} Talerz;

typedef struct
{
    int zajety;
    int pojemnosc;
    Grupa grupa;
} Stolik;

typedef struct
{
    Grupa kolejka[MAX_KOLEJKA];
    int przod, tyl, licznik;
} Kolejka;

typedef struct
{
    Talerz tasma[MAX_TASMA];
    int przod, tyl, licznik;
} Tasma;

Kolejka *kolejka_klientow;
Tasma *tasma;
Stolik *stoly;
int *wszyscy_klienci;
int *vip_licznik;
int *sygnal_kierownika;

sem_t *kolejka_sem;
sem_t *tasma_sem;

int P, X1, X2, X3, X4, N, Tp, Tk;
FILE *raport;

void inicjuj_tasma()
{
    tasma->przod = tasma->tyl = tasma->licznik = 0;
}

void inicjuj_kolejka()
{
    kolejka_klientow->przod = kolejka_klientow->tyl = kolejka_klientow->licznik = 0;
}

void dodaj_do_tasma(Talerz p)
{
    sem_wait(tasma_sem);
    if (tasma->licznik < P)
    {
        tasma->tasma[tasma->tyl] = p;
        tasma->tyl = (tasma->tyl + 1) % MAX_TASMA;
        tasma->licznik++;
    }
    sem_post(tasma_sem);
}

Talerz usun_z_tasma()
{
    Talerz p = {0, -1};
    sem_wait(tasma_sem);
    if (tasma->licznik > 0)
    {
        p = tasma->tasma[tasma->przod];
        tasma->przod = (tasma->przod + 1) % MAX_TASMA;
        tasma->licznik--;
    }
    sem_post(tasma_sem);
    return p;
}

void zakolejkuj(Grupa g)
{
    sem_wait(kolejka_sem);
    if (kolejka_klientow->licznik < MAX_KOLEJKA)
    {
        kolejka_klientow->kolejka[kolejka_klientow->tyl] = g;
        kolejka_klientow->tyl = (kolejka_klientow->tyl + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik++;
    }
    sem_post(kolejka_sem);
}

Grupa wykolejkuj()
{
    Grupa g = {0};
    sem_wait(kolejka_sem);
    if (kolejka_klientow->licznik > 0)
    {
        g = kolejka_klientow->kolejka[kolejka_klientow->przod];
        kolejka_klientow->przod = (kolejka_klientow->przod + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik--;
    }
    sem_post(kolejka_sem);
    return g;
}

/* --- Procesy --- */

void klient_proces()
{
    while (1)
    {
        sleep(1);
        (*wszyscy_klienci)++;
        Grupa g;
        g.wielkosc = rand() % 4 + 1;
        g.VIP = (rand() % 100 < 2);
        g.dzieci = rand() % 2;
        g.dorosli = g.dzieci ? rand() % 3 + 1 : 0;

        if (g.VIP)
        {
            (*vip_licznik)++;
            fprintf(raport, "VIP grupa %d osób przybyła.\n", g.wielkosc);
            sem_wait(kolejka_sem);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (!stoly[i].zajety && stoly[i].pojemnosc >= g.wielkosc)
                {
                    stoly[i].zajety = g.wielkosc;
                    stoly[i].grupa = g;
                    break;
                }
            }
            sem_post(kolejka_sem);
        }
        else
        {
            zakolejkuj(g);
            fprintf(raport, "Grupa %d osób w kolejce.\n", g.wielkosc);
        }
    }
}

void obsluga_proces()
{
    while (1)
    {
        if (kolejka_klientow->licznik > 0)
        {
            Grupa g = wykolejkuj();
            int dolacz = 0;
            sem_wait(kolejka_sem);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (!stoly[i].zajety && stoly[i].pojemnosc >= g.wielkosc)
                {
                    stoly[i].zajety = g.wielkosc;
                    stoly[i].grupa = g;
                    fprintf(raport, "Grupa %d osób przy stoliku %d.\n", g.wielkosc, i);
                    dolacz = 1;
                    break;
                }
            }
            sem_post(kolejka_sem);
            if (!dolacz)
                fprintf(raport, "Brak miejsca dla grupy %d osób.\n", g.wielkosc);
        }

        int speed = (*sygnal_kierownika == 1) ? 2 : (*sygnal_kierownika == 2) ? 0.5
                                                                              : 1;
        for (int i = 0; i < speed; i++)
        {
            Talerz p = {(rand() % 3 + 1) * 10, -1};
            dodaj_do_tasma(p);
            fprintf(raport, "Dodano talerzyk %d zł na taśmę.\n", p.cena);
        }
        sleep(1);
    }
}

void kucharz_proces()
{
    while (1)
    {
        sleep(3);
        Talerz p = {(rand() % 3 + 4) * 10, rand() % MAX_STOLIKI};
        dodaj_do_tasma(p);
        fprintf(raport, "Specjalne danie %d zł dla stolika %d.\n", p.cena, p.do_stolika);
    }
}

void kierownik_proces()
{
    while (1)
    {
        sleep(9);
        *sygnal_kierownika = rand() % 4;
        if (*sygnal_kierownika == 3)
        {
            sem_wait(kolejka_sem);
            for (int i = 0; i < MAX_STOLIKI; i++)
                stoly[i].zajety = 0;
            kolejka_klientow->licznik = 0;
            sem_post(kolejka_sem);
            fprintf(raport, "Sygnał 3: Wszyscy opuszczają restaurację.\n");
        }
        else
        {
            fprintf(raport, "Sygnał kierownika: %d\n", *sygnal_kierownika);
        }
    }
}

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

    kolejka_klientow = shmat(shm_kolejka, NULL, 0);
    tasma = shmat(shm_tasma, NULL, 0);
    stoly = shmat(shm_stoly, NULL, 0);
    wszyscy_klienci = shmat(shm_total, NULL, 0);
    vip_licznik = shmat(shm_vip, NULL, 0);
    sygnal_kierownika = shmat(shm_signal, NULL, 0);

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

    if (!fork())
        klient_proces();
    if (!fork())
        obsluga_proces();
    if (!fork())
        kucharz_proces();
    if (!fork())
        kierownik_proces();

    sleep(300);

    fprintf(raport, "Podsumowanie: Taśma ma %d talerzyków.\n", tasma->licznik);

    fclose(raport);
    return 0;
}
