#include "restauracja.h"

// ====== GLOBALNE ZMIENNE WSPÓŁDZIELONE ======
extern int shm_id, sem_id;                           // ID pamięci współdzielonej i semaforów
extern struct Kolejka *kolejka;                      // wskaźnik na kolejkę
extern struct Stolik *stoliki;                       // wskaźnik na tablicę stolików
extern struct Tasma *tasma;                          // wskaźnik na taśmę
extern int *sygnal_kierownika;                       // wskaźnik na sygnał kierownika
extern int *restauracja_otwarta;                     // wskaźnik na stan restauracji
static const int ILOSC_STOLIKOW[4] = {5, 5, 3, 2};   // liczba stolików o pojemności 1,2,3,4
static const int CENY_DAN[6] = {15, 20, 40, 50, 60}; // ceny dań;
extern int *kuchnia_dania_wydane;                    // liczba wydanych dań przez kuchnię
extern int *kasa_dania_sprzedane;                    // liczba sprzedanych dań przez kasę

// ====== PROCES KLIENT ======
void klient()
{
    struct Grupa g;

    g.osoby = rand() % 4 + 1;
    g.dorosli = rand() % g.osoby + 1;
    g.dzieci = g.osoby - g.dorosli;
    memset(g.pobrane_dania, 0, sizeof(g.pobrane_dania));
    g.stolik_przydzielony = -1;

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
                printf("VIP Grupa usadzona: %d osób (dorosłych: %d, dzieci: %d) przy stoliku: %d\n", g.osoby, g.dorosli, g.dzieci, i);
                g.stolik_przydzielony = i;
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

    // === CZEKANIE NA DANIA ===
    int dania_pobrane = 0;
    int dania_do_pobrania = rand() % 3 + 2; // losowa liczba dań do pobrania (2-4)
    time_t czas_start = time(NULL);

    while (dania_pobrane < dania_do_pobrania && (time(NULL) - czas_start) < 15) // czekaj max 15 sekund
    {
        sem_op(SEM_TASMA, -1);
        if (tasma->ilosc > 0 && tasma->talerze[0] != 0)
        {
            // pobierz danie z pozycji 0 taśmy
            int cena = tasma->talerze[0];
            if (cena == 15)
                g.pobrane_dania[0]++;
            else if (cena == 20)
                g.pobrane_dania[1]++;
            else if (cena == 40)
                g.pobrane_dania[2]++;
            else if (cena == 50)
                g.pobrane_dania[3]++;
            else if (cena == 60)
                g.pobrane_dania[4]++;

            printf("Grupa przy stoliku %d pobrała danie za %d zł (pobrane: %d/%d)\n",
                   g.stolik_przydzielony, cena, dania_pobrane + 1, dania_do_pobrania);
            dania_pobrane++;
        }
        sem_op(SEM_TASMA, 1);

        if (dania_pobrane < dania_do_pobrania)
            sleep(1); // czekaj na kolejne danie
    }

    printf("Grupa przy stoliku %d gotowa do płatności (pobrała %d dań)\n", g.stolik_przydzielony, dania_pobrane);

    // === KONSUMPCJA ===
    sleep(rand() % 3 + 1);

    // === PŁATNOŚĆ ===
    sem_op(SEM_TASMA, -1);
    // zaplacaenie za pobrane dania
    for (int i = 0; i < 6; i++)
    {
        if (g.pobrane_dania[i] == 0)
            continue;
        int ilosc_dan = g.pobrane_dania[i];
        int kwota = ilosc_dan * CENY_DAN[i];
        kasa_dania_sprzedane[i] += ilosc_dan;
        printf("Grupa płaci za %d dań za %d zł każde, łącznie: %d zł\n", ilosc_dan, CENY_DAN[i], kwota);
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
        // sprawdzanie sygnału kierownika //
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

        // obsługa grup w kolejce //

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
                    g.stolik_przydzielony = i;
                    break;
                }
            }
            sem_op(SEM_STOLIKI, 1);
        }
        // podawanie dań na taśmę //
        for (int i = 0; i < wydajnosc && tasma->ilosc < MAX_TASMA; i++)
        {
            int ceny[] = {p10, p15, p20};
            int c = ceny[rand() % 3];
            sem_op(SEM_TASMA, -1);
            dodaj_danie(tasma->talerze, c);
            tasma->ilosc++;
            sem_op(SEM_TASMA, 1);
            printf("Danie za %d zł podane na taśmę\n", c);
            kuchnia_dania_wydane[c / 10 - 1]++;
        }

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
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        printf("Kuchnia - liczba wydanych dań za %d zł: %d\n", CENY_DAN[i], kuchnia_dania_wydane[i]);

        kuchnia_suma += kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    printf("\n=== PODSUMOWANIE KUCHNI ===\nSuma: %d zł\n", kuchnia_suma);
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

void generator_stolikow(struct Stolik *stoliki)
{
    int idx = 1;

    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < ILOSC_STOLIKOW[i]; j++)
        {
            stoliki[idx].numer_stolika = idx;
            stoliki[idx].pojemnosc = i + 1;
            stoliki[idx].zajety = 0;

            printf("Stolik %d o pojemności %d utworzony.\n",
                   stoliki[idx].numer_stolika,
                   stoliki[idx].pojemnosc);

            idx++; // przechodzimy do kolejnego stolika
        }
    }
}

void przesun_tasme_cyklicznie(int *tasma)
{
    int ostatni = tasma[MAX_STOLIKI - 1];

    for (int i = MAX_STOLIKI - 1; i > 0; i--)
    {
        tasma[i] = tasma[i - 1];
    }

    tasma[0] = ostatni; // WRACA NA POCZĄTEK
}

void dodaj_danie(int *tasma, int cena)
{
    przesun_tasme_cyklicznie(tasma);
    tasma[0] = cena;
    printf("Danie za %d zł dodane na taśmę.\n", cena);
}

void klient_sprawdz_i_bierz(struct Grupa *g, int *tasma)
{
    int s = g->stolik_przydzielony;

    if (tasma[s] != 0)
    {
        int cena = tasma[s];

        if (cena == 10)
            g->pobrane_dania[0]++;
        else if (cena == 15)
            g->pobrane_dania[1]++;
        else if (cena == 20)
            g->pobrane_dania[2]++;
        tasma[s] = 0; // talerz zabrany
        printf("Grupa przy stoliku %d pobrała danie za %d zł\n", s, cena);
    }
}
