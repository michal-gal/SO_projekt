#include "procesy.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

// ====== ZMIENNE GLOBALNE (DEFINICJE) ======
int shm_id, sem_id;        // ID pamięci współdzielonej i semaforów
struct Kolejka *kolejka;   // wskaźnik na kolejkę
struct Stolik *stoliki;    // wskaźnik na tablicę stolików
int *sygnal_kierownika;    // wskaźnik na sygnał kierownika
int *restauracja_otwarta;  // wskaźnik na stan restauracji
int *aktywni_klienci;      // wskaźnik na liczbę aktywnych klientów
int *kuchnia_dania_wydane; // liczba wydanych dań przez kuchnię
int *kasa_dania_sprzedane; // liczba sprzedanych dań przez kasę
struct Talerzyk *tasma;    // tablica reprezentująca taśmę
int *kolej_podsumowania;   // czyja kolej na podsumowanie
pid_t pid_obsluga, pid_kucharz, pid_kierownik, pid_generator;

const int ILOSC_STOLIKOW[4] = {X1, X2, X3, X4};
const int CENY_DAN[6] = {p10, p15, p20, p40, p50, p60};

static int price_to_index(int cena)
{
    switch (cena)
    {
    case p10:
        return 0;
    case p15:
        return 1;
    case p20:
        return 2;
    case p40:
        return 3;
    case p50:
        return 4;
    case p60:
        return 5;
    default:
        return -1;
    }
}

static void active_clients_inc(void)
{
    sem_op(SEM_KOLEJKA, -1);
    (*aktywni_klienci)++;
    sem_op(SEM_KOLEJKA, 1);
}

static void active_clients_dec_if_positive(void)
{
    sem_op(SEM_KOLEJKA, -1);
    if (*aktywni_klienci > 0)
        (*aktywni_klienci)--;
    sem_op(SEM_KOLEJKA, 1);
}

static void wait_for_turn(int turn)
{
    while (*kolej_podsumowania != turn)
        usleep(50000);
}

static int find_table_for_group_locked(const struct Grupa *g)
{
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        if (stoliki[i].zajete_miejsca + g->osoby <= stoliki[i].pojemnosc &&
            stoliki[i].liczba_grup < MAX_GRUP_NA_STOLIKU)
        {
            return i;
        }
    }
    return -1;
}

static struct Grupa init_group(void)
{
    struct Grupa g;
    g.proces_id = getpid();
    g.osoby = rand() % 4 + 1;
    g.dorosli = rand() % g.osoby + 1; // co najmniej 1 dorosły
    g.dzieci = g.osoby - g.dorosli;
    g.stolik_przydzielony = -1;
    g.vip = (rand() % 100 < 2);
    g.wejscie = time(NULL);
    memset(g.pobrane_dania, 0, sizeof(g.pobrane_dania));
    g.danie_specjalne = 0;
    return g;
}

static void seat_vip_group(struct Grupa *g)
{
    sem_op(SEM_STOLIKI, -1);
    int i = find_table_for_group_locked(g);
    if (i >= 0)
    {
        stoliki[i].grupy[stoliki[i].liczba_grup] = *g;
        stoliki[i].zajete_miejsca += g->osoby;
        stoliki[i].liczba_grup++;
        printf("Grupa VIP %d usadzona: %d osób (dorosłych: %d, dzieci: %d) przy stoliku: %d (miejsc zajete: %d/%d)\n",
               g->proces_id,
               g->osoby,
               g->dorosli,
               g->dzieci,
               stoliki[i].numer_stolika,
               stoliki[i].zajete_miejsca,
               stoliki[i].pojemnosc);
        g->stolik_przydzielony = i;
    }
    sem_op(SEM_STOLIKI, 1);
}

static int wait_for_table_assignment(struct Grupa *g)
{
    push(*g); // kolejka FIFO
    printf("Grupa %d dodana do kolejki: %d osób (dorosłych: %d, dzieci: %d)%s\n", g->proces_id, g->osoby, g->dorosli, g->dzieci, g->vip ? " [VIP]" : "");

    // oczekiwanie na przydział stolika
    pid_t moj_proces_id = g->proces_id;
    while (g->stolik_przydzielony == -1 && *restauracja_otwarta)
    {
        sem_op(SEM_STOLIKI, -1);
        for (int i = 0; i < MAX_STOLIKI; i++)
        {
            // Szukaj grupy (procesu) w tablicy grup stolika
            for (int j = 0; j < stoliki[i].liczba_grup; j++)
            {
                if (stoliki[i].grupy[j].proces_id == moj_proces_id)
                {
                    g->stolik_przydzielony = i;
                    printf("Grupa %d znalaza\u0142a swój stolik: %d\n", g->proces_id, stoliki[i].numer_stolika);
                    break;
                }
            }
            if (g->stolik_przydzielony != -1)
                break;
        }
        sem_op(SEM_STOLIKI, 1);
        sleep(1);
    }

    // Jeśli restauracja została zamknięta przed przydziałem stolika
    if (g->stolik_przydzielony == -1)
    {
        printf("Grupa %d opuszcza kolejkę - restauracja zamknięta\n", g->proces_id);
        return -1;
    }
    return 0;
}

static void order_special_if_needed(struct Grupa *g, int *dania_do_pobrania, time_t *czas_start_dania, int timeout_dania)
{
    if (time(NULL) - *czas_start_dania <= timeout_dania)
        return;
    if (g->danie_specjalne != 0)
        return;

    int ceny[] = {p40, p50, p60};
    int c = ceny[rand() % 3];
    g->danie_specjalne = c;
    (*dania_do_pobrania)++; // dodaj oczekiwane specjalne danie, żeby grupa na nie poczekała

    // Zapisz zamówienie w stoliku, żeby obsługa wiedziała
    sem_op(SEM_STOLIKI, -1);
    for (int j = 0; j < stoliki[g->stolik_przydzielony].liczba_grup; j++)
    {
        if (stoliki[g->stolik_przydzielony].grupy[j].proces_id == g->proces_id)
        {
            stoliki[g->stolik_przydzielony].grupy[j].danie_specjalne = c;
            break;
        }
    }
    sem_op(SEM_STOLIKI, 1);

    printf("Grupa %d zamawia danie specjalne za: %d zł. \n", g->proces_id, g->danie_specjalne);
    *czas_start_dania = time(NULL); // reset timera po zamówieniu
}

typedef enum
{
    TAKE_NONE = 0,
    TAKE_TAKEN = 1,
    TAKE_SKIPPED_OTHER_TABLE = 2,
} TakeDishResult;

static TakeDishResult try_take_dish(struct Grupa *g, int *dania_pobrane, int dania_do_pobrania, time_t *czas_start_dania)
{
    sem_op(SEM_TASMA, -1);
    if (tasma[g->stolik_przydzielony].cena != 0)
    {
        if (tasma[g->stolik_przydzielony].stolik_specjalny != 0 &&
            tasma[g->stolik_przydzielony].stolik_specjalny != stoliki[g->stolik_przydzielony].numer_stolika)
        {
            // danie specjalne dla innego stolika, pomiń
            sem_op(SEM_TASMA, 1);
            return TAKE_SKIPPED_OTHER_TABLE;
        }

        // pobierz danie z pozycji 0 taśmy
        int cena = tasma[g->stolik_przydzielony].cena;
        int idx = price_to_index(cena);
        if (idx >= 0)
            g->pobrane_dania[idx]++;

        printf("Grupa %d przy stoliku %d pobrała danie za %d zł (pobrane: %d/%d)\n",
               g->proces_id,
               stoliki[g->stolik_przydzielony].numer_stolika,
               cena,
               *dania_pobrane + 1,
               dania_do_pobrania);

        (*dania_pobrane)++;
        tasma[g->stolik_przydzielony].cena = 0; // talerz zabrany
        tasma[g->stolik_przydzielony].stolik_specjalny = 0;
        *czas_start_dania = time(NULL); // reset timera po pobraniu dania

        sem_op(SEM_TASMA, 1);
        return TAKE_TAKEN;
    }

    sem_op(SEM_TASMA, 1);
    return TAKE_NONE;
}

static void pay_for_dishes(const struct Grupa *g, int dania_pobrane)
{
    printf("Grupa %d przy stoliku %d gotowa do płatności (pobrała %d dań)\n", g->proces_id, stoliki[g->stolik_przydzielony].numer_stolika, dania_pobrane);
    sem_op(SEM_TASMA, -1);
    // zaplacaenie za pobrane dania
    for (int i = 0; i < 6; i++)
    {
        if (g->pobrane_dania[i] == 0)
            continue;
        int kwota = g->pobrane_dania[i] * CENY_DAN[i];
        kasa_dania_sprzedane[i] += g->pobrane_dania[i];
        printf("Grupa %d płaci za %d dań za %d zł każde, łącznie: %d zł\n", g->proces_id, g->pobrane_dania[i], CENY_DAN[i], kwota);
    }
    sem_op(SEM_TASMA, 1);
}

static void leave_table(const struct Grupa *g)
{
    sem_op(SEM_STOLIKI, -1);
    printf("Grupa %d przy stoliku %d opuszcza restaurację.\n", g->proces_id, stoliki[g->stolik_przydzielony].numer_stolika);

    // Znajdź i usuń grupę ze stolika
    for (int j = 0; j < stoliki[g->stolik_przydzielony].liczba_grup; j++)
    {
        if (stoliki[g->stolik_przydzielony].grupy[j].proces_id == g->proces_id)
        {
            // Przesuń grupy w tablicy
            for (int k = j; k < stoliki[g->stolik_przydzielony].liczba_grup - 1; k++)
            {
                stoliki[g->stolik_przydzielony].grupy[k] = stoliki[g->stolik_przydzielony].grupy[k + 1];
            }
            memset(&stoliki[g->stolik_przydzielony].grupy[stoliki[g->stolik_przydzielony].liczba_grup - 1], 0, sizeof(struct Grupa));
            stoliki[g->stolik_przydzielony].liczba_grup--;
            stoliki[g->stolik_przydzielony].zajete_miejsca -= g->osoby;
            break;
        }
    }
    sem_op(SEM_STOLIKI, 1);
}

static void close_restaurant_and_kill_clients(void)
{
    printf("\n===Kierownik zamyka restaurację!===\n");
    *restauracja_otwarta = 0;

    // Zamknięcie wszystkich procesów klientów przy stolikach
    int killed_przy_stolikach = 0;
    sem_op(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        // Iteruj w wstecznym kierunku aby nie pomijać grup przy zmniejszaniu liczba_grup
        while (stoliki[i].liczba_grup > 0)
        {
            int j = stoliki[i].liczba_grup - 1;
            if (stoliki[i].grupy[j].proces_id != 0)
            {
                pid_t pid = stoliki[i].grupy[j].proces_id;
                printf("Zamykanie procesu klienta %d przy stoliku %d\n", pid, i);
                kill(pid, SIGTERM);
                killed_przy_stolikach++;
                stoliki[i].zajete_miejsca -= stoliki[i].grupy[j].osoby;
            }
            memset(&stoliki[i].grupy[j], 0, sizeof(struct Grupa));
            stoliki[i].liczba_grup--;
        }
        stoliki[i].zajete_miejsca = 0;
    }
    sem_op(SEM_STOLIKI, 1);

    if (killed_przy_stolikach > 0)
    {
        // Licznik aktywnych klientów jest aktualizowany przez generator przy waitpid().
    }

    // Zamknięcie wszystkich procesów w kolejce
    sem_op(SEM_KOLEJKA, -1);
    for (int i = 0; i < kolejka->ilosc; i++)
    {
        int j = (kolejka->przod + i) % MAX_KOLEJKA;
        if (kolejka->q[j].proces_id != 0)
        {
            printf("Zamykanie procesu klienta %d z kolejki\n", kolejka->q[j].proces_id);
            kill(kolejka->q[j].proces_id, SIGTERM);
        }
    }
    kolejka->ilosc = 0;
    sem_op(SEM_KOLEJKA, 1);
}

static double obsluga_get_wydajnosc_and_handle_signal(void)
{
    // bazowo 2 dania na iterację, spowolnienie -> 1, przyspieszenie -> 4
    double wydajnosc = 2.0;

    if (*sygnal_kierownika == 1)
    {
        wydajnosc = 4.0;
        printf("Zwiększona wydajność obsługi!\n");
    }
    if (*sygnal_kierownika == 2)
    {
        wydajnosc = 1.0;
        printf("Zmniejszona wydajność obsługi!\n");
    }
    if (*sygnal_kierownika == 3) // sygnał do zamknięcia restauracji
    {
        close_restaurant_and_kill_clients();
    }
    else
    {
        printf("Restauracja działa normalnie.\n");
    }

    return wydajnosc;
}

static void obsluga_seat_one_group_from_queue(void)
{
    struct Grupa g = pop(); // obsługa grup z kolejki
    if (g.proces_id == 0)
        return;

    sem_op(SEM_STOLIKI, -1);
    int stolik_idx = find_table_for_group_locked(&g);
    if (stolik_idx >= 0)
    {
        stoliki[stolik_idx].grupy[stoliki[stolik_idx].liczba_grup] = g;
        stoliki[stolik_idx].zajete_miejsca += g.osoby;
        stoliki[stolik_idx].liczba_grup++;
        printf("Grupa usadzona: PID %d przy stoliku: %d (%d/%d miejsc zajętych)\n",
               g.proces_id,
               stoliki[stolik_idx].numer_stolika,
               stoliki[stolik_idx].zajete_miejsca,
               stoliki[stolik_idx].pojemnosc);
    }
    sem_op(SEM_STOLIKI, 1);
}

static void obsluga_serve_special_dishes_if_needed(void)
{
    for (int stolik = 0; stolik < MAX_STOLIKI; stolik++)
    {
        // Iteruj przez wszystkie grupy przy stoliku
        for (int grupa = 0; grupa < stoliki[stolik].liczba_grup; grupa++)
        {
            if (stoliki[stolik].grupy[grupa].danie_specjalne != 0)
            {
                int cena_specjalna = stoliki[stolik].grupy[grupa].danie_specjalne;
                int numer_stolika = stoliki[stolik].numer_stolika;
                sem_op(SEM_TASMA, -1);
                dodaj_danie(tasma, cena_specjalna);
                tasma[0].stolik_specjalny = numer_stolika; // oznacz dla którego stolika jest danie
                sem_op(SEM_TASMA, 1);
                stoliki[stolik].grupy[grupa].danie_specjalne = 0; // zresetuj po dodaniu

                // Zwiększ licznik wydanych dań specjalnych
                int idx = price_to_index(cena_specjalna);
                if (idx >= 0)
                    kuchnia_dania_wydane[idx]++;

                printf("Obsługa dodała danie specjalne za %d zł dla stolika %d\n",
                       cena_specjalna,
                       numer_stolika);
            }
        }
    }
}

static void obsluga_serve_dishes(double wydajnosc)
{
    // podawanie dań na taśmę (wydajność całkowita: 1/2/4)
    int serves = (int)wydajnosc;
    for (int iter = 0; iter < serves; iter++)
    {
        int ceny[] = {p10, p15, p20};
        int c = ceny[rand() % 3];

        sem_op(SEM_TASMA, -1);
        dodaj_danie(tasma, c);
        sem_op(SEM_TASMA, 1);

        // obsługa dań specjalnych
        obsluga_serve_special_dishes_if_needed();

        int idx = price_to_index(c);
        if (idx >= 0)
            kuchnia_dania_wydane[idx]++;
    }
}

static void wait_until_no_active_clients(void)
{
    while (*aktywni_klienci > 0)
        sleep(1);
}

static void wait_until_closed_and_no_active_clients(void)
{
    while (*restauracja_otwarta || *aktywni_klienci > 0)
        sleep(1);
}

static void print_kitchen_summary(void)
{
    printf("\n=== PODSUMOWANIE KUCHNI ===\n");
    int kuchnia_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        printf("Kuchnia - liczba wydanych dań za %d zł: %d\n", CENY_DAN[i], kuchnia_dania_wydane[i]);
        kuchnia_suma += kuchnia_dania_wydane[i] * CENY_DAN[i];
    }
    printf("\nSuma: %d zł\n", kuchnia_suma);
}

static void kierownik_update_signal(void)
{
    *sygnal_kierownika = rand() % 50; // losowa zmiana sygnału kierownika
    printf("Kierownik zmienia sygnał na: %d\n", *sygnal_kierownika);
}

static int klient_wait_for_dishes(struct Grupa *g)
{
    // === CZEKANIE NA DANIA ===
    int dania_pobrane = 0;                  // licznik pobranych dań
    int dania_do_pobrania = rand() % 8 + 3; // losowa liczba dań do pobrania (3-10)
    time_t czas_start_dania = time(NULL);
    int timeout_dania = 5; // 5 sekund na pobranie dań

    while (dania_pobrane < dania_do_pobrania && *restauracja_otwarta) // wyjdź jeśli restauracja zamknięta lub pobrano wszystkie dania
    {
        if (time(NULL) - czas_start_dania > timeout_dania * 4) // maksymalny czas oczekiwania na dania (2x timeout)
        {
            printf("Grupa %d timeout czekania na dania - kończy się\n", g->proces_id);
            break;
        }

        order_special_if_needed(g, &dania_do_pobrania, &czas_start_dania, timeout_dania);

        TakeDishResult take_res = try_take_dish(g, &dania_pobrane, dania_do_pobrania, &czas_start_dania);
        if (take_res == TAKE_SKIPPED_OTHER_TABLE)
        {
            sleep(1);
            continue;
        }

        sleep(1); // czekaj na kolejne danie
    }

    return dania_pobrane;
}

static void generator_reap_children_nonblocking(void)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        active_clients_dec_if_positive();
}

static void generator_reap_children_blocking(void)
{
    int status;
    while (waitpid(-1, &status, 0) > 0)
        active_clients_dec_if_positive();
}

static void generator_spawn_one_group(void)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        klient();
        _exit(0);
    }
    if (pid > 0)
    {
        // Zliczaj tylko w procesie rodzica
        active_clients_inc();
        return;
    }

    perror("fork");
}

// ====== PROCES KLIENT ======
void klient()
{
    // === INICJALIZACJA GRUPY ===
    struct Grupa g = init_group();

    // === PRZYDZIAŁ STOLIKA ===
    if (g.vip)
    {
        // === VIP ===
        seat_vip_group(&g);
    }
    else
    {
        // === NORMALNY ===
        if (wait_for_table_assignment(&g) != 0)
            exit(0);
    }

    int dania_pobrane = klient_wait_for_dishes(&g);

    // === PŁATNOŚĆ ===
    pay_for_dishes(&g, dania_pobrane);

    // === WYJŚCIE ===
    leave_table(&g);

    exit(0); // koniec procesu klienta
}

void generator_klientow()
{
    srand(getpid()); // inicjalizacja generatora liczb losowych dla procesu klienta

    while (*restauracja_otwarta)
    {
        // Nie blokuj generatora, ale sprzątaj zakończone dzieci, żeby nie powstawały zombie.
        generator_reap_children_nonblocking();

        generator_spawn_one_group();
        sleep(rand() % 3 + 1); // losowy czas między generowaniem grup (1-3 sekundy)
    }

    // Po zamknięciu restauracji: poczekaj aż wszystkie dzieci (klienci) się zakończą
    // i dopiero wtedy oddaj kolejkę do podsumowań.
    generator_reap_children_blocking();

    // Czekaj na swoją kolej (generator = 0)
    wait_for_turn(0);

    printf("Generator klientów kończy pracę.\n");
    fflush(stdout);

    // Przekaż kolejkę do obsługi
    *kolej_podsumowania = 1;

    exit(0); // zakończenie generatora klientów
}

// ====== OBSŁUGA ======
void obsluga()
{
    while (*restauracja_otwarta)
    {
        // sprawdzanie sygnału kierownika //
        double wydajnosc = obsluga_get_wydajnosc_and_handle_signal();

        // obsługa grup w kolejce //
        obsluga_seat_one_group_from_queue();

        // podawanie dań na taśmę
        obsluga_serve_dishes(wydajnosc);

        sleep(1);
    }

    wait_until_no_active_clients();

    // Czekaj na swoją kolej (obsługa = 1)
    wait_for_turn(1);

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
    int tasma_dania_niesprzedane[6] = {0};
    for (int i = 0; i < MAX_TASMA; i++)
    {
        if (tasma[i].cena != 0)
        {
            int idx = price_to_index(tasma[i].cena);
            if (idx >= 0)
                tasma_dania_niesprzedane[idx]++;
        }
    }
    int tasma_suma = 0;
    for (int i = 0; i < 6; i++)
    {
        printf("Taśma - liczba niesprzedanych dań za %d zł: %d\n", CENY_DAN[i], tasma_dania_niesprzedane[i]);
        tasma_suma += tasma_dania_niesprzedane[i] * CENY_DAN[i];
    }
    printf("===Suma: %d zł===\n", tasma_suma);

    printf("\n");

    printf("Obsługa kończy pracę.\n");

    printf("======================\n");
    fflush(stdout);

    // Przekaz kolejkę do kucharza
    *kolej_podsumowania = 2;

    exit(0);
}

// ====== KUCHARZ ======
void kucharz()
{
    wait_until_closed_and_no_active_clients();

    // Czekaj na swoją kolej (kucharz = 2)
    wait_for_turn(2);

    // PODSUMOWANIE KUCHNI
    print_kitchen_summary();

    printf("Kucharz kończy pracę.\n");
    printf("======================\n");
    fflush(stdout);

    // Przekaz kolejkę do kierownika
    *kolej_podsumowania = 3;

    exit(0);
}

// ====== KIEROWNIK ======
void kierownik()
{
    while (*restauracja_otwarta)
    {
        kierownik_update_signal();
        sleep(1);
    }

    wait_until_no_active_clients();

    // Czekaj na swoją kolej (kierownik = 3)
    wait_for_turn(3);

    printf("Kierownik kończy pracę.\n");
    fflush(stdout);
    exit(0);
}

// ====== SEMAFORY ======
void sem_op(int sem, int val) // wykonanie operacji na semaforze, val = +1 (zwolnienie), -1 (zablokowanie)
{
    struct sembuf sb = {sem, val, SEM_UNDO}; // SEM_UNDO: kernel cofnie zmianę jeśli proces zakończy się niepoprawnie
    for (;;)
    {
        if (semop(sem_id, &sb, 1) == 0) // wykonanie operacji na semaforze
            return;

        if (errno == EINTR)
            continue;

        // IPC usunięte/nieprawidłowe (np. podczas sprzątania) — zakończ proces bez hałasu
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        perror("semop");
        exit(1);
    }
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

// ====== GENERATOR STOLIKÓW ======
void generator_stolikow(struct Stolik *stoliki)
{

    //    stoliki[0].numer_stolika = 0;
    //    stoliki[0].pojemnosc = LADA;

    int idx = 0;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < ILOSC_STOLIKOW[i]; j++)
        {
            // Oblicz indeks jako sumę wszystkich poprzednich stolików + obecny
            int suma_poprzednich = 0;
            for (int k = 0; k < i; k++)
                suma_poprzednich += ILOSC_STOLIKOW[k];

            idx = suma_poprzednich + j;
            stoliki[idx].numer_stolika = idx + 1;
            stoliki[idx].pojemnosc = i + 1;
            stoliki[idx].liczba_grup = 0;
            stoliki[idx].zajete_miejsca = 0;
            memset(stoliki[idx].grupy, 0, sizeof(stoliki[idx].grupy));

            printf("Stolik %d o pojemności %d utworzony.\n",
                   stoliki[idx].numer_stolika,
                   stoliki[idx].pojemnosc);
        }
    }
}

// ====== TAŚMA ======
void dodaj_danie(struct Talerzyk *tasma, int cena)
{
    do
    {
        struct Talerzyk ostatni = tasma[MAX_TASMA - 1];

        for (int i = MAX_TASMA - 1; i > 0; i--)
        {
            tasma[i] = tasma[i - 1];
        }

        tasma[0] = ostatni; // WRACA NA POCZĄTEK
    } while (tasma[0].cena != 0);

    tasma[0].cena = cena;
    // domyślnie brak stolika specjalnego; jeśli trzeba, caller ustawi po wywołaniu
    tasma[0].stolik_specjalny = 0;

    if (cena == p40 || cena == p50 || cena == p60)
        printf("Danie specjalne za %d zł dodane na taśmę.\n", cena);
    else
        printf("Danie za %d zł dodane na taśmę.\n", cena);
}

// ====== IPC ======
void stworz_ipc(void)
{
    srand(time(NULL));
    int bufor = sizeof(struct Kolejka) +              // kolejka
                sizeof(struct Stolik) * MAX_STOLIKI + // stoliki
                sizeof(struct Talerzyk) * MAX_TASMA + // taśma
                sizeof(int) * 6 * 2 +                 // kuchnia i kasa - liczba dań
                sizeof(int) * 4 +                     // sygnał kierownika, stan restauracji, aktywni klienci, kolej_podsumowania
                sizeof(int) * 6;                      // pobrane dania

    shm_id = shmget(IPC_PRIVATE, bufor, IPC_CREAT | 0666); // utworzenie pamięci współdzielonej
    void *pamiec_wspoldzielona = shmat(shm_id, NULL, 0);   // dołączenie pamięci współdzielonej
    memset(pamiec_wspoldzielona, 0, bufor);                // wyzerowanie pamięci

    kolejka = pamiec_wspoldzielona;
    stoliki = (void *)(kolejka + 1);
    tasma = (struct Talerzyk *)(stoliki + MAX_STOLIKI);
    kuchnia_dania_wydane = (int *)(tasma + MAX_TASMA);
    kasa_dania_sprzedane = kuchnia_dania_wydane + 6;
    sygnal_kierownika = kasa_dania_sprzedane + 6;
    restauracja_otwarta = sygnal_kierownika + 1;
    aktywni_klienci = restauracja_otwarta + 1;
    kolej_podsumowania = aktywni_klienci + 1;

    sem_id = semget(IPC_PRIVATE, 3, IPC_CREAT | 0666); // utworzenie zestawu semaforów
    semctl(sem_id, SEM_KOLEJKA, SETVAL, 1);            // inicjalizacja semaforów
    semctl(sem_id, SEM_STOLIKI, SETVAL, 1);
    semctl(sem_id, SEM_TASMA, SETVAL, 1);
}
