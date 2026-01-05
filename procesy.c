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

    g.proces_id = getpid();
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
        przydziel_stolik(&g);
    }
    else
    {
        // === NORMALNY ===
        push(g.proces_id); // kolejka FIFO
        printf("Grupa %d dodana do kolejki: %d osób (dorosłych: %d, dzieci: %d)%s\n", g.proces_id, g.osoby, g.dorosli, g.dzieci, g.vip ? " [VIP]" : "");
        // oczekiwanie na przydział stolika
        pid_t moj_proces_id = g.proces_id;
        while (g.stolik_przydzielony == -1)
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (stoliki[i].zajety && stoliki[i].proces_id == moj_proces_id)
                {
                    g.stolik_przydzielony = i;
                    printf("Grupa %d znalazła swój stolik: %d\n", g.proces_id, i);
                    break;
                }
            }
            sem_op(SEM_STOLIKI, 1);
            if (g.stolik_przydzielony == -1)
                sleep(1);
        }
    }

    // === CZEKANIE NA DANIA ===
    int dania_pobrane = 0;
    int dania_do_pobrania = rand() % 8 + 3; // losowa liczba dań do pobrania (3-10)
    // time_t czas_start = time(NULL);

    while (dania_pobrane < dania_do_pobrania)
    {
        sem_op(SEM_TASMA, -1);
        if (tasma->talerze[0] != 0)
        {
            // pobierz danie z pozycji 0 taśmy
            int cena = tasma->talerze[0];
            if (cena == 10)
                g.pobrane_dania[0]++;
            else if (cena == 15)
                g.pobrane_dania[1]++;
            else if (cena == 20)
                g.pobrane_dania[2]++;

            printf("Grupa %d przy stoliku %d pobrała danie za %d zł (pobrane: %d/%d)\n",
                   g.proces_id, g.stolik_przydzielony, cena, dania_pobrane + 1, dania_do_pobrania);
            dania_pobrane++;
        }
        sem_op(SEM_TASMA, 1);
        sleep(1); // czekaj na kolejne danie
    }

    printf("Grupa %d przy stoliku %d gotowa do płatności (pobrała %d dań)\n", g.proces_id, g.stolik_przydzielony, dania_pobrane);

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
        printf("Grupa %d płaci za %d dań za %d zł każde, łącznie: %d zł\n", g.proces_id, ilosc_dan, CENY_DAN[i], kwota);
    }

    sem_op(SEM_TASMA, 1);

    // === WYJŚCIE ===
    sem_op(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        if (stoliki[i].zajety &&
            stoliki[i].proces_id == g.proces_id)
        {
            printf("Grupa %d przy stoliku %d opuszcza restaurację.\n", g.proces_id, i);
            stoliki[i].proces_id = 0;
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

        pid_t pid_z_kolejki;     // obsługa grup z kolejki
        if (pop(&pid_z_kolejki)) // jeśli jest grupa w kolejce
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (!stoliki[i].zajety)
                {
                    stoliki[i].zajety = 1;
                    stoliki[i].proces_id = pid_z_kolejki;
                    printf("Grupa usadzona: PID %d przy stoliku: %d\n", pid_z_kolejki, i);
                    break;
                }
            }
            sem_op(SEM_STOLIKI, 1);
        }
        // podawanie dań na taśmę //
        for (int i = 0; i < wydajnosc; i++)
        {
            int ceny[] = {p10, p15, p20};
            int c = ceny[rand() % 3];
            sem_op(SEM_TASMA, -1);
            dodaj_danie(tasma->talerze, c);
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
void push(pid_t pid)
{
    sem_op(SEM_KOLEJKA, -1);          // zablokowanie semafora
    if (kolejka->ilosc < MAX_KOLEJKA) // sprawdzenie czy jest miejsce w kolejce
    {
        kolejka->q[kolejka->tyl] = pid;                  // dodanie pid na koniec kolejki
        kolejka->tyl = (kolejka->tyl + 1) % MAX_KOLEJKA; // przesunięcie tylu
        kolejka->ilosc++;                                // zwiększenie ilości grup w kolejce
    }
    sem_op(SEM_KOLEJKA, 1); // zwolnienie semafora
}

int pop(pid_t *pid)
{
    sem_op(SEM_KOLEJKA, -1); // zablokowanie semafora
    if (kolejka->ilosc == 0) // sprawdzenie czy kolejka jest pusta
    {
        sem_op(SEM_KOLEJKA, 1); // zwolnienie semafora
        return 0;
    }
    *pid = kolejka->q[kolejka->przod];                   // pobranie pid z przodu kolejki
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
        printf("Grupa %d przy stoliku %d pobrała danie za %d zł\n", g->proces_id, s, cena);
    }
}

void przydziel_stolik(struct Grupa *g)
{
    sem_op(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        if (!stoliki[i].zajety && stoliki[i].pojemnosc >= g->osoby)
        {
            stoliki[i].zajety = 1;
            stoliki[i].proces_id = g->proces_id;
            printf("Grupa VIP %d usadzona: %d osób (dorosłych: %d, dzieci: %d) przy stoliku: %d\n", g->proces_id, g->osoby, g->dorosli, g->dzieci, i);
            g->stolik_przydzielony = i;
            break;
        }
    }
    sem_op(SEM_STOLIKI, 1);
}
