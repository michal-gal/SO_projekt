#include "restauracja.h"

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
