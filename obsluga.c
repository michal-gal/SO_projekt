#include "common.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t obsluga_wydajnosc = 2; // 1=wolno, 2=normalnie, 4=szybko
static volatile sig_atomic_t shutdown_requested = 0;

static void obsluz_sygnal(int signo)
{
    switch (signo)
    {
    case SIGUSR1:
        obsluga_wydajnosc = 4;
        break;
    case SIGUSR2:
        obsluga_wydajnosc = 1;
        break;
    case SIGTERM:
        shutdown_requested = 1;
        break;
    default:
        break;
    }
}

static void obsluga_podaj_dania(double wydajnosc)
{
    int serves = (int)wydajnosc;
    for (int iter = 0; iter < serves; iter++)
    {
        int ceny[] = {p10, p15, p20};
        int c = ceny[rand() % 3];

        sem_operacja(SEM_TASMA, -1);
        dodaj_danie(tasma, c);
        sem_operacja(SEM_TASMA, 1);

        for (int stolik = 0; stolik < MAX_STOLIKI; stolik++)
        {
            for (int grupa = 0; grupa < stoliki[stolik].liczba_grup; grupa++)
            {
                if (stoliki[stolik].grupy[grupa].danie_specjalne != 0)
                {
                    int cena_specjalna = stoliki[stolik].grupy[grupa].danie_specjalne;
                    int numer_stolika = stoliki[stolik].numer_stolika;
                    sem_operacja(SEM_TASMA, -1);
                    dodaj_danie(tasma, cena_specjalna);
                    tasma[0].stolik_specjalny = numer_stolika;
                    sem_operacja(SEM_TASMA, 1);
                    stoliki[stolik].grupy[grupa].danie_specjalne = 0;

                    int idx = cena_na_indeks(cena_specjalna);
                    if (idx >= 0)
                        kuchnia_dania_wydane[idx]++;

                    printf("Obsługa dodała danie specjalne za %d zł dla stolika %d\n",
                           cena_specjalna,
                           numer_stolika);
                }
            }
        }

        int idx = cena_na_indeks(c);
        if (idx >= 0)
            kuchnia_dania_wydane[idx]++;
    }
}

void obsluga(void)
{
    if (pid_obsluga_shm)
        *pid_obsluga_shm = getpid();

    if (signal(SIGUSR1, obsluz_sygnal) == SIG_ERR)
        perror("signal(SIGUSR1)");
    if (signal(SIGUSR2, obsluz_sygnal) == SIG_ERR)
        perror("signal(SIGUSR2)");
    if (signal(SIGTERM, obsluz_sygnal) == SIG_ERR)
        perror("signal(SIGTERM)");

    sig_atomic_t ostatnia_wydajnosc = obsluga_wydajnosc;

    while (*restauracja_otwarta)
    {
        if (shutdown_requested)
        {
            *restauracja_otwarta = 0;
            break;
        }

        sig_atomic_t biezaca_wydajnosc = obsluga_wydajnosc;
        if (biezaca_wydajnosc != ostatnia_wydajnosc)
        {
            if (biezaca_wydajnosc >= 4)
                printf("Zwiększona wydajność obsługi (SIGUSR1)!\n");
            else if (biezaca_wydajnosc <= 1)
                printf("Zmniejszona wydajność obsługi (SIGUSR2)!\n");
            else
                printf("Restauracja działa normalnie.\n");
            ostatnia_wydajnosc = biezaca_wydajnosc;
        }

        double wydajnosc = (double)biezaca_wydajnosc;

        struct Grupa g = kolejka_pobierz();
        if (g.proces_id != 0)
        {
            sem_operacja(SEM_STOLIKI, -1);
            int stolik_idx = znajdz_stolik_dla_grupy_zablokowanej(&g);
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
            sem_operacja(SEM_STOLIKI, 1);
        }

        obsluga_podaj_dania(wydajnosc);
        sleep(1);
    }

    czekaj_na_ture(1);

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
            int idx = cena_na_indeks(tasma[i].cena);
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

    printf("\n\nObsługa kończy pracę.\n");
    printf("======================\n");
    fflush(stdout);

    *kolej_podsumowania = 2;

    exit(0);
}

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf(stderr, "Użycie: %s <shm_id> <sem_id> <msgq_id>\n", argv[0]);
        return 1;
    }

    int shm = parsuj_int_lub_zakoncz("shm_id", argv[1]);
    int sem = parsuj_int_lub_zakoncz("sem_id", argv[2]);
    msgq_id = parsuj_int_lub_zakoncz("msgq_id", argv[3]);
    dolacz_ipc(shm, sem);
    obsluga();
    return 0;
}
