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

// Definicje stałych
#define MAX_KOLEJKA 500
#define MAX_TASMA 500
#define MAX_STOLIKI 100

// Struktury danych
typedef struct Grupa
{
    int wielkosc;
    int VIP;
    int dzieci;
    int dorosli;
    int wielkosc;
    int VIP;
    int dzieci;
    int dorosli;
} Grupa;

typedef struct Talerz
{
    int cena;
    int do_stolika;
    int cena;
    int do_stolika;
} Talerz;

typedef struct Stolik
{
    int zajety;
    int pojemnosc;
    int zajety;
    int pojemnosc;
    Grupa grupa;
} Stolik;

typedef struct
{
    Grupa kolejka[MAX_KOLEJKA];
    int przod;
    int tyl;
    int licznik;
    Grupa kolejka[MAX_KOLEJKA];
    int przod;
    int tyl;
    int licznik;
} Kolejka;

typedef struct
{
    Talerz tasma[MAX_TASMA];
    int przod;
    int tyl;
    int licznik;
    Talerz tasma[MAX_TASMA];
    int przod;
    int tyl;
    int licznik;
} Tasma;

// Globalne zmienne - teraz współdzielone
Kolejka *kolejka_klientow;
Tasma *tasma;
Stolik *stoly;
int *wszyscy_klienci;
int *vip_licznik;
int *sygnal_kierownika; // Sygnał kierownika
sem_t *kolejka_sem;     // Semafor dla kolejki
sem_t *tasma_sem;       // Semafor dla taśmy
sem_t *kolejka_sem;     // Semafor dla stolików
int P;                  // Maksymalna pojemność taśmy
int X1;                 // Liczba stolików jednoosobowych
int X2;                 // Liczba stolików dwuosobowych
int X3;                 // Liczba stolików trzyosobowych
int X4;                 // Liczba stolików czteroosobowych
int N;                  // Całkowita liczba miejsc
int Tp;                 // Czas otwarcia restauracji
int Tk;                 // Czas zamknięcia restauracji
FILE *raport;           // Plik raportu

// Funkcje pomocnicze - dostosuj do wskaźników
void inicjuj_tasma() void inicjuj_tasma()
{
    tasma->przod = 0;
    tasma->tyl = 0;
    tasma->licznik = 0;
    tasma->przod = 0;
    tasma->tyl = 0;
    tasma->licznik = 0;
}

void dodaj_do_tasma(Talerz p) void dodaj_do_tasma(Talerz p)
{
    sem_wait(tasma_sem);
    if (tasma->licznik < P)
        sem_wait(tasma_sem);
    if (tasma->licznik < P)
    {
        tasma->tasma[tasma->tyl] = p;
        tasma->tyl = (tasma->tyl + 1) % MAX_TASMA;
        tasma->licznik++;
        tasma->tasma[tasma->tyl] = p;
        tasma->tyl = (tasma->tyl + 1) % MAX_TASMA;
        tasma->licznik++;
    }
    sem_post(tasma_sem);
    sem_post(tasma_sem);
}

Talerz usun_z_tasma()
    Talerz usun_z_tasma()
{
    Talerz p = {0, -1};
    sem_wait(tasma_sem);
    if (tasma->licznik > 0)
        sem_wait(tasma_sem);
    if (tasma->licznik > 0)
    {
        p = tasma->tasma[tasma->przod];
        tasma->przod = (tasma->przod + 1) % MAX_TASMA;
        tasma->licznik--;
        p = tasma->tasma[tasma->przod];
        tasma->przod = (tasma->przod + 1) % MAX_TASMA;
        tasma->licznik--;
    }
    sem_post(tasma_sem);
    sem_post(tasma_sem);
    return p;
}

void inicjuj_kolejka() void inicjuj_kolejka()
{
    kolejka_klientow->przod = 0;
    kolejka_klientow->tyl = 0;
    kolejka_klientow->licznik = 0;
    kolejka_klientow->przod = 0;
    kolejka_klientow->tyl = 0;
    kolejka_klientow->licznik = 0;
}

void zakolejkuj(Grupa g) void zakolejkuj(Grupa g)
{
    sem_wait(kolejka_sem);
    if (kolejka_klientow->licznik < MAX_KOLEJKA)
        sem_wait(kolejka_sem);
    if (kolejka_klientow->licznik < MAX_KOLEJKA)
    {
        kolejka_klientow->kolejka[kolejka_klientow->tyl] = g;
        kolejka_klientow->tyl = (kolejka_klientow->tyl + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik++;
        kolejka_klientow->kolejka[kolejka_klientow->tyl] = g;
        kolejka_klientow->tyl = (kolejka_klientow->tyl + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik++;
    }
    sem_post(kolejka_sem);
    sem_post(kolejka_sem);
}

Grupa wykolejkuj()
    Grupa wykolejkuj()
{
    Grupa g = {0, 0, 0, 0};
    sem_wait(kolejka_sem);
    if (kolejka_klientow->licznik > 0)
        sem_wait(kolejka_sem);
    if (kolejka_klientow->licznik > 0)
    {
        g = kolejka_klientow->kolejka[kolejka_klientow->przod];
        kolejka_klientow->przod = (kolejka_klientow->przod + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik--;
        g = kolejka_klientow->kolejka[kolejka_klientow->przod];
        kolejka_klientow->przod = (kolejka_klientow->przod + 1) % MAX_KOLEJKA;
        kolejka_klientow->licznik--;
    }
    sem_post(kolejka_sem);
    sem_post(kolejka_sem);
    return g;
}

void klient_proces()
{
    while (1)
    {
        // sleep(rand() % 10);
        sleep(1);
        (*wszyscy_klienci)++;
        Grupa g;
        g.wielkosc = rand() % 4 + 1;
        g.VIP = (rand() % 100 < 2) ? 1 : 0;
        g.dzieci = rand() % 2;
        g.dorosli = g.dzieci ? rand() % 3 + 1 : 0;
        g.wielkosc = rand() % 4 + 1;
        g.VIP = (rand() % 100 < 2) ? 1 : 0;
        g.dzieci = rand() % 2;
        g.dorosli = g.dzieci ? rand() % 3 + 1 : 0;

        if (g.VIP)
            if (g.VIP)
            {
                (*vip_licznik)++;
                fprintf(raport, "VIP grupa %d osób przybyła.\n", g.wielkosc);
                sem_wait(kolejka_sem);
                for (int i = 0; i < MAX_STOLIKI; i++)
                {
                    if (stoly[i].zajety == 0 && stoly[i].pojemnosc >= g.wielkosc)
                        if (stoly[i].zajety == 0 && stoly[i].pojemnosc >= g.wielkosc)
                        {
                            stoly[i].zajety = g.wielkosc;
                            stoly[i].grupa = g;
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
            if (kolejka_klientow->licznik > 0)
            {
                Grupa g = wykolejkuj();
                int dolacz = 0;
                sem_wait(kolejka_sem);
                for (int i = 0; i < MAX_STOLIKI; i++)
                {
                    if (stoly[i].zajety == 0 && stoly[i].pojemnosc >= g.wielkosc)
                        if (stoly[i].zajety == 0 && stoly[i].pojemnosc >= g.wielkosc)
                        {
                            stoly[i].zajety = g.wielkosc;
                            stoly[i].grupa = g;
                            dolacz = 1;
                            fprintf(raport, "Grupa %d osób przy stoliku %d.\n", g.wielkosc, i);
                            stoly[i].zajety = g.wielkosc;
                            stoly[i].grupa = g;
                            dolacz = 1;
                            fprintf(raport, "Grupa %d osób przy stoliku %d.\n", g.wielkosc, i);
                            break;
                        }
                }
                sem_post(kolejka_sem);
                if (!dolacz)
                {
                    fprintf(raport, "Brak miejsca dla grupy %d osób.\n", g.wielkosc);
                    fprintf(raport, "Brak miejsca dla grupy %d osób.\n", g.wielkosc);
                }
            }

        int speed = 1;
        if (*sygnal_kierownika == 1)
            speed = 2;
        else if (*sygnal_kierownika == 2)
            speed = 0.5;

        for (int i = 0; i < speed; i++)
        {
            Talerz p;
            p.cena = (rand() % 3 + 1) * 10;
            p.do_stolika = -1;
            dodaj_do_tasma(p);
            fprintf(raport, "Dodano talerzyk %d zł na taśmę.\n", p.cena);
            p.cena = (rand() % 3 + 1) * 10;
            p.do_stolika = -1;
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
        Talerz p;
        p.cena = (rand() % 3 + 4) * 10;
        p.do_stolika = rand() % MAX_STOLIKI;
        dodaj_do_tasma(p);
        fprintf(raport, "Specjalne danie %d zł dla stolika %d.\n", p.cena, p.do_stolika);
        p.cena = (rand() % 3 + 4) * 10;
        p.do_stolika = rand() % MAX_STOLIKI;
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
            {
                stoly[i].zajety = 0;
                stoly[i].zajety = 0;
            }
            sem_wait(kolejka_sem);
            kolejka_klientow->licznik = 0;
            sem_post(kolejka_sem);
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
    raport = fopen("raport.txt", "w");

    // Tworzenie współdzielonej pamięci
    int shm_kolejka = shmget(IPC_PRIVATE, sizeof(Kolejka), IPC_CREAT | 0666);
    int shm_tasma = shmget(IPC_PRIVATE, sizeof(Tasma), IPC_CREAT | 0666);
    int shm_stoly = shmget(IPC_PRIVATE, sizeof(Stolik) * MAX_STOLIKI, IPC_CREAT | 0666);
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

    // Inicjalizacja semaforów
    kolejka_sem = sem_open("/kolejka_sem", O_CREAT, 0644, 1);
    tasma_sem = sem_open("/tasma_sem", O_CREAT, 0644, 1);
    kolejka_sem = sem_open("/kolejka_sem", O_CREAT, 0644, 1);

    // Inicjalizacja
    inicjuj_tasma();
    inicjuj_kolejka();
    *wszyscy_klienci = 0;
    *vip_licznik = 0;
    *sygnal_kierownika = 0;
    X1 = 10;
    X2 = 10;
    X3 = 10;
    X4 = 10;
    N = X1 + 2 * X2 + 3 * X3 + 4 * X4;
    P = 1000;
    Tp = 10;
    Tk = 22;

    int idx = 0;
    for (int i = 0; i < X1; i++)
        stoly[idx++].pojemnosc = 1;
    stoly[idx++].pojemnosc = 1;
    for (int i = 0; i < X2; i++)
        stoly[idx++].pojemnosc = 2;
    stoly[idx++].pojemnosc = 2;
    for (int i = 0; i < X3; i++)
        stoly[idx++].pojemnosc = 3;
    stoly[idx++].pojemnosc = 3;
    for (int i = 0; i < X4; i++)
        stoly[idx++].pojemnosc = 4;
    stoly[idx++].pojemnosc = 4;

    // Tworzenie procesów
    pid_t pid_klient = fork(); // Proces klienta
    if (pid_klient == 0)
    {
        klient_proces();
        exit(0);
    }

    pid_t pid_obsluga = fork();
    if (pid_obsluga == 0)
    {
        obsluga_proces();
        exit(0);
    }

    pid_t pid_kucharz = fork();
    if (pid_kucharz == 0)
    {
        kucharz_proces();
        exit(0);
    }

    pid_t pid_kierownik = fork();
    if (pid_kierownik == 0)
    {
        kierownik_proces();
        exit(0);
    }

    // Główny proces: symulacja przez 60 sekund
    sleep(300);

    // Zabij procesy potomne
    kill(pid_klient, SIGTERM);
    kill(pid_obsluga, SIGTERM);
    kill(pid_kucharz, SIGTERM);
    kill(pid_kierownik, SIGTERM);

    // Podsumowanie
    fprintf(raport, "Podsumowanie: Taśma ma %d talerzyków.\n", tasma->licznik);
    fprintf(raport, "Podsumowanie: Taśma ma %d talerzyków.\n", tasma->licznik);

    // Czyszczenie
    shmdt(kolejka_klientow);
    shmctl(shm_kolejka, IPC_RMID, NULL);
    shmdt(tasma);
    shmctl(shm_tasma, IPC_RMID, NULL);
    shmdt(stoly);
    shmctl(shm_stoly, IPC_RMID, NULL);
    shmdt(wszyscy_klienci);
    shmdt(kolejka_klientow);
    shmctl(shm_kolejka, IPC_RMID, NULL);
    shmdt(tasma);
    shmctl(shm_tasma, IPC_RMID, NULL);
    shmdt(stoly);
    shmctl(shm_stoly, IPC_RMID, NULL);
    shmdt(wszyscy_klienci);
    shmctl(shm_total, IPC_RMID, NULL);
    shmdt(vip_licznik);
    shmdt(vip_licznik);
    shmctl(shm_vip, IPC_RMID, NULL);
    shmdt(signal);
    shmctl(shm_signal, IPC_RMID, NULL);
    sem_close(kolejka_sem);
    sem_unlink("/kolejka_sem");
    sem_close(tasma_sem);
    sem_unlink("/tasma_sem");
    sem_close(kolejka_sem);
    sem_unlink("/kolejka_sem");

    fclose(raport);
    fclose(raport);
    return 0;
}