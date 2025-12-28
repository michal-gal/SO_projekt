#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>

#define MAX_STOLIKI 20
#define MAX_KOLEJKA 50
#define MAX_TASMA 100
#define CZAS_PRACY 30

// ====== SEMAFORY ======
#define SEM_KOLEJKA 0
#define SEM_STOLIKI 1
#define SEM_TASMA 2

// ====== STRUKTURY ======
typedef struct
{
    int osoby;
    int dzieci;
    int dorosli;
    int vip;
    time_t wejscie;
} Grupa;

typedef struct
{
    Grupa q[MAX_KOLEJKA];
    int przod, tyl, ilosc;
} Kolejka;

typedef struct
{
    int pojemnosc;
    int zajety;
    Grupa grupa;
} Stolik;

typedef struct
{
    int talerze[MAX_TASMA];
    int ilosc;
} Tasma;

typedef struct
{
    int p10, p15, p20;
    int p40, p50, p60;
    int suma;
} Statystyki;

// ====== ZMIENNE GLOBALNE ======
int shm_id, sem_id;
Kolejka *kolejka;
Stolik *stoliki;
Tasma *tasma;
Statystyki *kuchnia, *kasa;
int *sygnal_kierownika;
int *restauracja_otwarta;

// ====== SEMAFORY ======
void sem_op(int sem, int val)
{
    struct sembuf sb = {sem, val, 0};
    semop(sem_id, &sb, 1);
}

// ====== KOLEJKA ======
void push(Grupa g)
{
    sem_op(SEM_KOLEJKA, -1);
    if (kolejka->ilosc < MAX_KOLEJKA)
    {
        kolejka->q[kolejka->tyl] = g;
        kolejka->tyl = (kolejka->tyl + 1) % MAX_KOLEJKA;
        kolejka->ilosc++;
    }
    sem_op(SEM_KOLEJKA, 1);
}

int pop(Grupa *g)
{
    sem_op(SEM_KOLEJKA, -1);
    if (kolejka->ilosc == 0)
    {
        sem_op(SEM_KOLEJKA, 1);
        return 0;
    }
    *g = kolejka->q[kolejka->przod];
    kolejka->przod = (kolejka->przod + 1) % MAX_KOLEJKA;
    kolejka->ilosc--;
    sem_op(SEM_KOLEJKA, 1);
    return 1;
}

// ====== PROCES KLIENT ======
void klient()
{
    srand(getpid());
    while (*restauracja_otwarta)
    {
        Grupa g;
        g.osoby = rand() % 4 + 1;
        g.dorosli = rand() % g.osoby + 1;
        g.dzieci = g.osoby - g.dorosli;
        if (g.dzieci > g.dorosli * 3)
            continue;
        g.vip = (rand() % 100 < 2);
        g.wejscie = time(NULL);

        if (g.vip)
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (!stoliki[i].zajety && stoliki[i].pojemnosc == g.osoby)
                {
                    stoliki[i].zajety = 1;
                    stoliki[i].grupa = g;
                    break;
                }
            }
            sem_op(SEM_STOLIKI, 1);
        }
        else
        {
            push(g);
        }
        sleep(rand() % 3 + 1);
    }
    exit(0);
}

// ====== OBSŁUGA ======
void obsluga()
{
    while (*restauracja_otwarta)
    {
        if (*sygnal_kierownika == 3)
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
                stoliki[i].zajety = 0;
            sem_op(SEM_STOLIKI, 1);

            sem_op(SEM_KOLEJKA, -1);
            kolejka->ilosc = 0;
            sem_op(SEM_KOLEJKA, 1);
        }

        Grupa g;
        if (pop(&g))
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (!stoliki[i].zajety && stoliki[i].pojemnosc == g.osoby)
                {
                    stoliki[i].zajety = 1;
                    stoliki[i].grupa = g;
                    break;
                }
            }
            sem_op(SEM_STOLIKI, 1);
        }

        int wydajnosc = 1;
        if (*sygnal_kierownika == 1)
            wydajnosc = 2;
        if (*sygnal_kierownika == 2)
            wydajnosc = 0.5;

        sem_op(SEM_TASMA, -1);
        for (int i = 0; i < wydajnosc && tasma->ilosc < MAX_TASMA; i++)
        {
            int ceny[] = {10, 15, 20};
            int c = ceny[rand() % 3];
            tasma->talerze[tasma->ilosc++] = c;
        }
        sem_op(SEM_TASMA, 1);

        sleep(1);
    }
    exit(0);
}

// ====== KUCHARZ ======
void kucharz()
{
    while (*restauracja_otwarta)
    {
        int ceny[] = {10, 15, 20, 40, 50, 60};
        int c = ceny[rand() % 6];

        sem_op(SEM_TASMA, -1);
        if (tasma->ilosc < MAX_TASMA)
        {
            tasma->talerze[tasma->ilosc++] = c;
            kuchnia->suma += c;
        }
        sem_op(SEM_TASMA, 1);

        sleep(2);
    }

    printf("\n=== PODSUMOWANIE KUCHNI ===\nSuma: %d zł\n", kuchnia->suma);
    exit(0);
}

// ====== KIEROWNIK ======
void kierownik()
{
    while (*restauracja_otwarta)
    {
        *sygnal_kierownika = rand() % 4;
        sleep(5);
    }
    exit(0);
}

// ====== MAIN ======
int main()
{
    srand(time(NULL));
    shm_id = shmget(IPC_PRIVATE, sizeof(Kolejka) + sizeof(Stolik) * MAX_STOLIKI + sizeof(Tasma) + sizeof(Statystyki) * 2 + sizeof(int) * 2, IPC_CREAT | 0666);
    void *mem = shmat(shm_id, NULL, 0);

    kolejka = mem;
    stoliki = (void *)(kolejka + 1);
    tasma = (void *)(stoliki + MAX_STOLIKI);
    kuchnia = (void *)(tasma + 1);
    kasa = kuchnia + 1;
    sygnal_kierownika = (void *)(kasa + 1);
    restauracja_otwarta = sygnal_kierownika + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666);
    semctl(sem_id, SEM_KOLEJKA, SETVAL, 1);
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);

    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        stoliki[i].pojemnosc = (i % 4) + 1;
        stoliki[i].zajety = 0;
    }

    *restauracja_otwarta = 1;

    if (!fork())
        klient();
    if (!fork())
        obsluga();
    if (!fork())
        kucharz();
    if (!fork())
        kierownik();

    sleep(CZAS_PRACY);
    *restauracja_otwarta = 0;

    sleep(3);
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);

    return 0;
}