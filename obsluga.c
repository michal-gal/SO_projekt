#include "common.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/msg.h>
#include <unistd.h>

static volatile sig_atomic_t obsluga_mode = 0; // 0=normalnie, 1=+wydajność, -1=-wydajność
static volatile sig_atomic_t shutdown_requested = 0;

static void close_restaurant_and_kill_clients(void)
{
    printf("\n===Kierownik zamyka restaurację!===\n");
    *restauracja_otwarta = 0;

    sem_op(SEM_STOLIKI, -1);
    for (int i = 0; i < MAX_STOLIKI; i++)
    {
        while (stoliki[i].liczba_grup > 0)
        {
            int j = stoliki[i].liczba_grup - 1;
            if (stoliki[i].grupy[j].proces_id != 0)
            {
                pid_t pid = stoliki[i].grupy[j].proces_id;
                printf("Zamykanie procesu klienta %d przy stoliku %d\n", pid, i);
                kill(pid, SIGTERM);
                stoliki[i].zajete_miejsca -= stoliki[i].grupy[j].osoby;
            }
            memset(&stoliki[i].grupy[j], 0, sizeof(struct Grupa));
            stoliki[i].liczba_grup--;
        }
        stoliki[i].zajete_miejsca = 0;
    }
    sem_op(SEM_STOLIKI, 1);

    // Opróżnij kolejkę komunikatów (klienci oczekujący) i ubij ich procesy.
    struct
    {
        long mtype;
        struct Grupa grupa;
    } msg;

    for (;;)
    {
        ssize_t r = msgrcv(msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
        if (r < 0)
        {
            if (errno == ENOMSG)
                break;
            if (errno == EINTR)
                continue;
            break;
        }

        if (msg.grupa.proces_id != 0)
        {
            printf("Zamykanie procesu klienta %d z kolejki\n", msg.grupa.proces_id);
            kill(msg.grupa.proces_id, SIGTERM);
        }
    }
}

static void on_signal(int signo)
{
    switch (signo)
    {
    case SIGUSR1:
        obsluga_mode = 1;
        break;
    case SIGUSR2:
        obsluga_mode = -1;
        break;
    case SIGTERM:
        shutdown_requested = 1;
        break;
    default:
        break;
    }
}

static void obsluga_install_signal_handlers(void)
{
    if (signal(SIGUSR1, on_signal) == SIG_ERR)
        perror("signal(SIGUSR1)");
    if (signal(SIGUSR2, on_signal) == SIG_ERR)
        perror("signal(SIGUSR2)");
    if (signal(SIGTERM, on_signal) == SIG_ERR)
        perror("signal(SIGTERM)");
}

static double obsluga_get_wydajnosc_and_handle_signal(void)
{
    double wydajnosc = 2.0;

    static sig_atomic_t last_mode = 999;
    sig_atomic_t mode = obsluga_mode;

    if (mode == 1)
        wydajnosc = 4.0;
    else if (mode == -1)
        wydajnosc = 1.0;

    if (mode != last_mode)
    {
        if (mode == 1)
            printf("Zwiększona wydajność obsługi (SIGUSR1)!\n");
        else if (mode == -1)
            printf("Zmniejszona wydajność obsługi (SIGUSR2)!\n");
        else
            printf("Restauracja działa normalnie.\n");
        last_mode = mode;
    }

    return wydajnosc;
}

static void obsluga_seat_one_group_from_queue(void)
{
    struct Grupa g = pop();
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
        for (int grupa = 0; grupa < stoliki[stolik].liczba_grup; grupa++)
        {
            if (stoliki[stolik].grupy[grupa].danie_specjalne != 0)
            {
                int cena_specjalna = stoliki[stolik].grupy[grupa].danie_specjalne;
                int numer_stolika = stoliki[stolik].numer_stolika;
                sem_op(SEM_TASMA, -1);
                dodaj_danie(tasma, cena_specjalna);
                tasma[0].stolik_specjalny = numer_stolika;
                sem_op(SEM_TASMA, 1);
                stoliki[stolik].grupy[grupa].danie_specjalne = 0;

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
    int serves = (int)wydajnosc;
    for (int iter = 0; iter < serves; iter++)
    {
        int ceny[] = {p10, p15, p20};
        int c = ceny[rand() % 3];

        sem_op(SEM_TASMA, -1);
        dodaj_danie(tasma, c);
        sem_op(SEM_TASMA, 1);

        obsluga_serve_special_dishes_if_needed();

        int idx = price_to_index(c);
        if (idx >= 0)
            kuchnia_dania_wydane[idx]++;
    }
}

void obsluga(void)
{
    if (pid_obsluga_shm)
        *pid_obsluga_shm = getpid();
    obsluga_install_signal_handlers();

    while (*restauracja_otwarta)
    {
        if (shutdown_requested)
        {
            shutdown_requested = 0;
            close_restaurant_and_kill_clients();
            break;
        }

        double wydajnosc = obsluga_get_wydajnosc_and_handle_signal();
        obsluga_seat_one_group_from_queue();
        obsluga_serve_dishes(wydajnosc);
        sleep(1);
    }

    wait_for_turn(1);

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

    int shm = parse_int_or_die("shm_id", argv[1]);
    int sem = parse_int_or_die("sem_id", argv[2]);
    msgq_id = parse_int_or_die("msgq_id", argv[3]);
    dolacz_ipc(shm, sem);
    obsluga();
    return 0;
}
