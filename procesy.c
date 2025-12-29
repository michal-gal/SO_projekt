#include "restauracja.h"

// ====== PROCES KLIENT ======
void klient()
{
    srand(getpid()); // inicjalizacja generatora liczb losowych
    while (*restauracja_otwarta)
    {
        struct Grupa g;                   // generowanie grupy klientów
        g.osoby = rand() % 4 + 1;         // od 1 do 4 osób
        g.dorosli = rand() % g.osoby + 1; // co najmniej 1 dorosły
        g.dzieci = g.osoby - g.dorosli;   // reszta to dzieci
        if (g.dzieci > g.dorosli * 3)     // ograniczenie: max 3 dzieci na dorosłego
            continue;
        g.vip = (rand() % 100 < 2); // 2% szans na bycie VIPem
        g.wejscie = time(NULL);     // czas wejścia

        printf("Nowa grupa: %d osób (dorosłych: %d, dzieci: %d)%s\n", g.osoby, g.dorosli, g.dzieci, g.vip ? " [VIP]" : "");

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
        int wydajnosc = 1;
        if (*sygnal_kierownika == 1)
            wydajnosc = 2;
        printf("Zwiększona wydajność obsługi!\n");
        if (*sygnal_kierownika == 2)
            wydajnosc = 0.5;
        printf("Zmniejszona wydajność obsługi!\n");

        if (*sygnal_kierownika == 3) // sygnał do zamknięcia restauracji
        {
            printf("Kierownik zamyka restaurację!\n");
            *restauracja_otwarta = 0;
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
                stoliki[i].zajety = 0;
            sem_op(SEM_STOLIKI, 1);

            sem_op(SEM_KOLEJKA, -1);
            kolejka->ilosc = 0;
            sem_op(SEM_KOLEJKA, 1);
        }

        struct Grupa g; // obsługa grup z kolejki
        if (pop(&g))    // jeśli jest grupa w kolejce
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (!stoliki[i].zajety && stoliki[i].pojemnosc == g.osoby)
                {
                    stoliki[i].zajety = 1;
                    stoliki[i].grupa = g;
                    printf("Grupa usadzona: %d osób (dorosłych: %d, dzieci: %d)%s przy stoliku: %d\n", g.osoby, g.dorosli, g.dzieci, g.vip ? " [VIP]" : "", i);
                    break;
                }
            }
            sem_op(SEM_STOLIKI, 1);
        }

        sem_op(SEM_TASMA, -1);
        for (int i = 0; i < wydajnosc && tasma->ilosc < MAX_TASMA; i++)
        {
            int ceny[] = {10, 15, 20};
            int c = ceny[rand() % 3];
            tasma->talerze[tasma->ilosc++] = c;
            printf("Danie za %d zł podane na taśmę\n", c);
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

        sem_op(SEM_TASMA, -1); // dodanie dania na taśmę
        if (tasma->ilosc < MAX_TASMA)
        {
            tasma->talerze[tasma->ilosc++] = c;
            kuchnia->suma += c;
        }
        sem_op(SEM_TASMA, 1); // zwolnienie semafora
        printf("Danie za %d zł dodane na taśmę\n", c);

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
        printf("Kierownik zmienia sygnał na: %d\n", *sygnal_kierownika);
    }
    exit(0);
}

// ====== SEMAFORY ======
void sem_op(int sem, int val) // wykonanie operacji na semaforze, val = +1 (zwolnienie), -1 (zablokowanie)
{
    struct sembuf sb = {sem, val, 0}; // inicjalizacja struktury operacji semaforowej,
    semop(sem_id, &sb, 1);            // wykonanie operacji na semaforze,
}

// ====== KOLEJKA ======
void push(struct Grupa g)
{
    sem_op(SEM_KOLEJKA, -1);          // zablokowanie semafora
    if (kolejka->ilosc < MAX_KOLEJKA) // sprawdzenie czy jest miejsce w kolejce
    {
        kolejka->q[kolejka->tyl] = g;                    // dodanie grupy na koniec kolejki
        kolejka->tyl = (kolejka->tyl + 1) % MAX_KOLEJKA; // przesunięcie tylu
        kolejka->ilosc++;                                // zwiększenie ilości grup w kolejce
    }
    sem_op(SEM_KOLEJKA, 1); // zwolnienie semafora
}

int pop(struct Grupa *g)
{
    sem_op(SEM_KOLEJKA, -1); // zablokowanie semafora
    if (kolejka->ilosc == 0) // sprawdzenie czy kolejka jest pusta
    {
        sem_op(SEM_KOLEJKA, 1); // zwolnienie semafora
        return 0;
    }
    *g = kolejka->q[kolejka->przod];                     // pobranie grupy z przodu kolejki
    kolejka->przod = (kolejka->przod + 1) % MAX_KOLEJKA; // przesunięcie przodu
    kolejka->ilosc--;                                    // zmniejszenie ilości grup w kolejce
    sem_op(SEM_KOLEJKA, 1);                              // zwolnienie semafora
    return 1;
}