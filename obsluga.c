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

        LOGI("Danie za %d zł dodane na taśmę.\n", c);

        // Zamówienia specjalne: nie trzymaj naraz SEM_STOLIKI i SEM_TASMA.
        // Najpierw "zarezerwuj" zamówienia w stolikach (oznaczając ujemną ceną),
        // potem dodaj dania na taśmę, a na końcu wyczyść rezerwacje.
        int spec_ceny[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_numer_stolika[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_stolik_idx[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_grupa_idx[MAX_STOLIKI * MAX_GRUP_NA_STOLIKU];
        int spec_cnt = 0;

        sem_operacja(SEM_STOLIKI, -1);
        for (int stolik = 0; stolik < MAX_STOLIKI; stolik++)
        {
            for (int grupa = 0; grupa < stoliki[stolik].liczba_grup; grupa++)
            {
                int cena_specjalna = stoliki[stolik].grupy[grupa].danie_specjalne;
                if (cena_specjalna > 0 && spec_cnt < (MAX_STOLIKI * MAX_GRUP_NA_STOLIKU))
                {
                    stoliki[stolik].grupy[grupa].danie_specjalne = -cena_specjalna; // claim
                    spec_ceny[spec_cnt] = cena_specjalna;
                    spec_numer_stolika[spec_cnt] = stoliki[stolik].numer_stolika;
                    spec_stolik_idx[spec_cnt] = stolik;
                    spec_grupa_idx[spec_cnt] = grupa;
                    spec_cnt++;
                }
            }
        }
        sem_operacja(SEM_STOLIKI, 1);

        for (int i = 0; i < spec_cnt; i++)
        {
            sem_operacja(SEM_TASMA, -1);
            dodaj_danie(tasma, spec_ceny[i]);
            tasma[0].stolik_specjalny = spec_numer_stolika[i];
            sem_operacja(SEM_TASMA, 1);

            int idx = cena_na_indeks(spec_ceny[i]);
            if (idx >= 0)
                kuchnia_dania_wydane[idx]++;

            LOGI("Obsługa dodała danie specjalne za %d zł dla stolika %d\n",
                 spec_ceny[i],
                 spec_numer_stolika[i]);
        }

        if (spec_cnt > 0)
        {
            sem_operacja(SEM_STOLIKI, -1);
            for (int i = 0; i < spec_cnt; i++)
            {
                int *slot = &stoliki[spec_stolik_idx[i]].grupy[spec_grupa_idx[i]].danie_specjalne;
                if (*slot == -spec_ceny[i])
                    *slot = 0;
            }
            sem_operacja(SEM_STOLIKI, 1);
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

    zainicjuj_losowosc();

    if (signal(SIGUSR1, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR1)");
    if (signal(SIGUSR2, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGUSR2)");
    if (signal(SIGTERM, obsluz_sygnal) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");

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
                LOGI("Zwiększona wydajność obsługi (SIGUSR1)!\n");
            else if (biezaca_wydajnosc <= 1)
                LOGI("Zmniejszona wydajność obsługi (SIGUSR2)!\n");
            else
                LOGI("Restauracja działa normalnie.\n");
            ostatnia_wydajnosc = biezaca_wydajnosc;
        }

        double wydajnosc = (double)biezaca_wydajnosc;

        struct Grupa g = kolejka_pobierz();
        if (g.proces_id != 0)
        {
            int stolik_idx = -1;
            int log_usadzono = 0;
            int log_numer_stolika = 0;
            int log_zajete = 0;
            int log_pojemnosc = 0;
            pid_t log_pid = g.proces_id;

            sem_operacja(SEM_STOLIKI, -1);
            stolik_idx = znajdz_stolik_dla_grupy_zablokowanej(&g);
            if (stolik_idx >= 0)
            {
                stoliki[stolik_idx].grupy[stoliki[stolik_idx].liczba_grup] = g;
                stoliki[stolik_idx].zajete_miejsca += g.osoby;
                stoliki[stolik_idx].liczba_grup++;
                log_usadzono = 1;
                log_numer_stolika = stoliki[stolik_idx].numer_stolika;
                log_zajete = stoliki[stolik_idx].zajete_miejsca;
                log_pojemnosc = stoliki[stolik_idx].pojemnosc;
            }
            sem_operacja(SEM_STOLIKI, 1);

            if (log_usadzono)
                LOGI("Grupa usadzona: PID %d przy stoliku: %d (%d/%d miejsc zajętych)\n",
                     log_pid,
                     log_numer_stolika,
                     log_zajete,
                     log_pojemnosc);

            if (stolik_idx < 0 && *restauracja_otwarta)
                kolejka_dodaj(g);
        }

        obsluga_podaj_dania(wydajnosc);
        sleep(1);
    }

    // Czekaj na zakończenie obsługi wszystkich grup
    czekaj_na_ture(1);

    // Podsumowanie
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
        LOGE("Użycie: %s <shm_id> <sem_id> <msgq_id>\n", argv[0]);
        return 1;
    }

    int shm = parsuj_int_lub_zakoncz("shm_id", argv[1]);
    int sem = parsuj_int_lub_zakoncz("sem_id", argv[2]);
    msgq_id = parsuj_int_lub_zakoncz("msgq_id", argv[3]);
    dolacz_ipc(shm, sem);
    obsluga();
    return 0;
}
