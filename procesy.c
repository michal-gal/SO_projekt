#include "restauracja.h"

// ====== PROCES KLIENT ======
void klient()
{
    struct Grupa g;

    g.osoby = rand() % 4 + 1;
    g.dorosli = rand() % g.osoby + 1;
    g.dzieci = g.osoby - g.dorosli;

    if (g.dzieci > g.dorosli * 3)
        exit(0); // warunek zadania

    g.vip = (rand() % 100 < 2);
    g.wejscie = time(NULL);

    if (g.vip)
    {
        // === VIP ===
        sem_op(SEM_STOLIKI, -1);
        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            if (!stoliki[i].zajety &&
                stoliki[i].pojemnosc == g.osoby)
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
        // === NORMALNY ===
        push(g); // kolejka FIFO
        // klient „czeka” logicznie
    }

    // === KONSUMPCJA ===
    sleep(rand() % 5 + 3);

    // === PŁATNOŚĆ ===
    sem_op(SEM_TASMA, -1);
    // zaplacaenie za pobrane dania
    for (int i = 0; i < 6; i++)
    {
        int ceny[] = {15, 20, 40, 50, 60};
        int ilosc_dan = g.pobrane_dania[i];
        int cena_dania = ceny[i];
        int kwota = ilosc_dan * cena_dania;
        // kasa->ilosc_dan[i] += ilosc_dan; --- IGNORE

        // kasa->suma += kwota; --- IGNORE ---
    }

    sem_op(SEM_TASMA, 1);

    // === WYJŚCIE ===
    sem_op(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        if (stoliki[i].zajety &&
            stoliki[i].grupa.wejscie == g.wejscie)
        {

            stoliki[i].zajety = 0;
            break;
        }
    }
    sem_op(SEM_STOLIKI, 1);

    exit(0);
}

void generator_klientow()
{
    srand(getpid());

    while (*restauracja_otwarta)
    {
        pid_t pid = fork();

        if (pid == 0)
        {
            klient(); // ← JEDNA grupa
            exit(0);
        }

        sleep(rand() % 3 + 1); // losowe przyjścia
    }
}

// ====== OBSŁUGA ======
void obsluga()
{
    while (*restauracja_otwarta)
    {
        int wydajnosc = 1;
        if (*sygnal_kierownika == 1)
        {
            wydajnosc = 2;
            printf("Zwiększona wydajność obsługi!\n");
        }
        if (*sygnal_kierownika == 2)
        {
            wydajnosc = 0.5;
            printf("Zmniejszona wydajność obsługi!\n");
        }
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
        else
        {
            printf("Restauracja działa normalnie.\n");
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
        sleep(2);
    }

    for (int i = 0; i < 6; i++)
    {
        // printf("Kuchnia - liczba wydanych dań za %d zł: %d\n", ceny[i], kuchnia->ilosc_dan[i]); --- IGNORE ---
    }
    printf("\n=== PODSUMOWANIE KUCHNI ===\nSuma: %d zł\n", 0 /* kuchnia->suma */); // --- IGNORE ---
    exit(0);
}

// ====== KIEROWNIK ======
void kierownik()
{
    while (*restauracja_otwarta)
    {
        *sygnal_kierownika = 0; // normalna praca

        sleep(10);
        *sygnal_kierownika = rand() % 4; // losowa zmiana sygnału
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