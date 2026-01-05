#include "restauracja.h"

// ====== PROCES KLIENT ======
void klient()
{
    // Ustaw timeout - jeśli proces będzie działał dłużej niż 30 sekund, wymuś jego zakończenie
    // alarm(30);

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
        sem_op(SEM_STOLIKI, -1);
        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            if (stoliki[i].proces_id == 0 && stoliki[i].pojemnosc >= g.osoby)
            {
                stoliki[i].proces_id = g.proces_id;
                printf("Grupa VIP %d usadzona: %d osób (dorosłych: %d, dzieci: %d) przy stoliku: %d\n", g.proces_id, g.osoby, g.dorosli, g.dzieci, i);
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
        printf("Grupa %d dodana do kolejki: %d osób (dorosłych: %d, dzieci: %d)%s\n", g.proces_id, g.osoby, g.dorosli, g.dzieci, g.vip ? " [VIP]" : "");
        // oczekiwanie na przydział stolika
        pid_t moj_proces_id = g.proces_id;
        while (g.stolik_przydzielony == -1 && *restauracja_otwarta)
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (stoliki[i].proces_id == moj_proces_id)
                {
                    g.stolik_przydzielony = i;
                    printf("Grupa %d znalazła swój stolik: %d\n", g.proces_id, i);
                    break;
                }
            }
            sem_op(SEM_STOLIKI, 1);
            sleep(1);
        }

        // Jeśli restauracja została zamknięta przed przydziałem stolika
        if (g.stolik_przydzielony == -1)
        {
            printf("Grupa %d opuszcza kolejkę - restauracja zamknięta\n", g.proces_id);
            exit(0);
        }
    }

    // === CZEKANIE NA DANIA ===
    int dania_pobrane = 0;
    int dania_do_pobrania = rand() % 8 + 3; // losowa liczba dań do pobrania (3-10)
    time_t czas_start_dania = time(NULL);
    int timeout_dania = 20; // 20 sekund na pobranie dań

    while (dania_pobrane < dania_do_pobrania && *restauracja_otwarta)
    {
        if (time(NULL) - czas_start_dania > timeout_dania)
        {
            printf("Grupa %d timeout czekania na dania - kończy się\n", g.proces_id);
            break;
        }

        sem_op(SEM_TASMA, -1);
        if (tasma[g.stolik_przydzielony] != 0)
        {
            // pobierz danie z pozycji 0 taśmy
            int cena = tasma[g.stolik_przydzielony];
            if (cena == 10)
                g.pobrane_dania[0]++;
            else if (cena == 15)
                g.pobrane_dania[1]++;
            else if (cena == 20)
                g.pobrane_dania[2]++;

            printf("Grupa %d przy stoliku %d pobrała danie za %d zł (pobrane: %d/%d)\n",
                   g.proces_id, g.stolik_przydzielony, cena, dania_pobrane + 1, dania_do_pobrania);
            dania_pobrane++;
            tasma[g.stolik_przydzielony] = 0; // talerz zabrany
            czas_start_dania = time(NULL);    // reset timera po pobraniu dania
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
        int kwota = g.pobrane_dania[i] * CENY_DAN[i];
        kasa_dania_sprzedane[i] += g.pobrane_dania[i];
        printf("Grupa %d płaci za %d dań za %d zł każde, łącznie: %d zł\n", g.proces_id, g.pobrane_dania[i], CENY_DAN[i], kwota);
    }
    sem_op(SEM_TASMA, 1);

    // === WYJŚCIE ===
    sem_op(SEM_STOLIKI, -1);
    printf("Grupa %d przy stoliku %d opuszcza restaurację.\n", g.proces_id, g.stolik_przydzielony);
    stoliki[g.stolik_przydzielony].proces_id = 0;
    sem_op(SEM_STOLIKI, 1);
    exit(0); // koniec procesu klienta
    aktywni_klienci--;
}

void generator_klientow()
{
    srand(getpid());

    while (*restauracja_otwarta)
    {
        pid_t pid = fork();
        aktywni_klienci++;

        if (pid == 0)
        {
            klient(); // ← JEDNA grupa
            exit(0);
        }

        // Zamiast jednego długiego sleep, robimy wiele krótkich aby szybciej reagować
        int czas_oczekiwania = rand() % 3 + 1;
        for (int i = 0; i < czas_oczekiwania * 10 && *restauracja_otwarta; i++)
        {
            usleep(100000); // 0.1 sekundy
        }
    }
    printf("Generator klientów kończy pracę.\n");
    exit(0); // zakończenie generatora klientów
}

// ====== OBSŁUGA ======
void obsluga()
{
    while (*aktywni_klienci > 0 || *restauracja_otwarta) // dopóki są aktywni klienci lub restauracja jest otwarta
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

            // Zamknięcie wszystkich procesów klientów przy stolikach
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (stoliki[i].proces_id != 0)
                {
                    printf("Zamykanie procesu klienta %d przy stoliku %d\n", stoliki[i].proces_id, i);
                    kill(stoliki[i].proces_id, SIGTERM);
                    stoliki[i].proces_id = 0;
                }
            }
            sem_op(SEM_STOLIKI, 1);

            // Zamknięcie wszystkich procesów w kolejce
            sem_op(SEM_KOLEJKA, -1);
            for (int i = 0; i < kolejka->ilosc; i++)
            {
                int idx = (kolejka->przod + i) % MAX_KOLEJKA;
                if (kolejka->q[idx].proces_id != 0)
                {
                    printf("Zamykanie procesu klienta %d z kolejki\n", kolejka->q[idx].proces_id);
                    kill(kolejka->q[idx].proces_id, SIGTERM);
                }
            }
            kolejka->ilosc = 0;
            sem_op(SEM_KOLEJKA, 1);
        }
        else
        {
            printf("Restauracja działa normalnie.\n");
        }

        // obsługa grup w kolejce //
        struct Grupa g = pop(); // obsługa grup z kolejki
        if (g.proces_id != 0)   // jeśli jest grupa w kolejce (proces_id != 0)
        {
            sem_op(SEM_STOLIKI, -1);
            for (int i = 0; i < MAX_STOLIKI; i++)
            {
                if (stoliki[i].proces_id == 0 && stoliki[i].pojemnosc == g.osoby) // przydzielamy stolik
                {
                    stoliki[i].proces_id = g.proces_id;
                    printf("Grupa usadzona: PID %d przy stoliku: %d\n", g.proces_id, stoliki[i].numer_stolika);
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
            dodaj_danie(tasma, c);
            sem_op(SEM_TASMA, 1);
            printf("Danie za %d zł podane na taśmę\n", c);

            // Inkrementuj licznik wydanych dań dla odpowiedniej ceny
            if (c == 10)
                kuchnia_dania_wydane[0]++;
            else if (c == 15)
                kuchnia_dania_wydane[1]++;
            else if (c == 20)
                kuchnia_dania_wydane[2]++;
        }

        sleep(1);
    }

    // PODSUMOWANIE KASY
    printf("\n=== PODSUMOWANIE KASY ===\n");
    int kasa_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        printf("Kasa - liczba sprzedanych dań za %d zł: %d\n", CENY_DAN[i], kasa_dania_sprzedane[i]);
        kasa_suma += kasa_dania_sprzedane[i] * CENY_DAN[i];
    }
    printf("Suma: %d zł\n", kasa_suma);

    printf("\n=== PODSUMOWANIE OBSŁUGI ===\n");

    for (int i = 0; i < MAX_TASMA; i++)
    {
        if (tasma[i] != 0)
        {
            printf("Na taśmie pozostało danie za %d zł na pozycji %d\n", tasma[i], i);
        }
    }
    printf("\n");

    printf("Obsługa kończy pracę.\n");

    exit(0);
}

// ====== KUCHARZ ======
void kucharz()
{
    while (*restauracja_otwarta)
    {
        sleep(2);
    }
    // PODSUMOWANIE KUCHNI
    printf("\n=== PODSUMOWANIE KUCHNI ===\n");
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        printf("Kuchnia - liczba wydanych dań za %d zł: %d\n", CENY_DAN[i], kuchnia_dania_wydane[i]);

        kuchnia_suma += kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    printf("\n=== PODSUMOWANIE KUCHNI ===\nSuma: %d zł\n", kuchnia_suma);

    printf("Kucharz kończy pracę.\n");
    exit(0);
}

// ====== KIEROWNIK ======
void kierownik()
{
    while (*restauracja_otwarta)
    {
        sleep(10);
        *sygnal_kierownika = rand() % 5; // losowa zmiana sygnału kierownika (0-4)
        printf("Kierownik zmienia sygnał na: %d\n", *sygnal_kierownika);
    }
    printf("Kierownik kończy pracę.\n");
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

struct Grupa pop(void)
{
    struct Grupa g = {0};    // Inicjalizacja do zera/puste
    sem_op(SEM_KOLEJKA, -1); // zablokowanie semafora
    if (kolejka->ilosc == 0) // sprawdzenie czy kolejka jest pusta
    {
        sem_op(SEM_KOLEJKA, 1); // zwolnienie semafora
        return g;               // Zwróć pusty struct
    }
    g = kolejka->q[kolejka->przod];                      // pobranie pid z przodu kolejki
    kolejka->przod = (kolejka->przod + 1) % MAX_KOLEJKA; // przesunięcie przodu
    kolejka->ilosc--;                                    // zmniejszenie ilości grup w kolejce
    sem_op(SEM_KOLEJKA, 1);                              // zwolnienie semafora
    return g;
}

void generator_stolikow(struct Stolik *stoliki)
{
    int idx = 0;

    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < ILOSC_STOLIKOW[i]; j++)
        {
            idx = i * ILOSC_STOLIKOW[i] + j;
            stoliki[idx].numer_stolika = idx + 1;
            stoliki[idx].pojemnosc = i + 1;
            stoliki[idx].proces_id = 0;

            printf("Stolik %d o pojemności %d utworzony.\n",
                   stoliki[idx].numer_stolika,
                   stoliki[idx].pojemnosc);
        }
    }
}

void dodaj_danie(int *tasma, int cena)
{
    do
    {
        int ostatni = tasma[MAX_STOLIKI - 1];

        for (int i = MAX_STOLIKI - 1; i > 0; i--)
        {
            tasma[i] = tasma[i - 1];
        }

        tasma[0] = ostatni; // WRACA NA POCZĄTEK
    } while (tasma[0] != 0);
    tasma[0] = cena;
    printf("Danie za %d zł dodane na taśmę.\n", cena);
}
