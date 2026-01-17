#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

void kucharz(void)
{
    while (*restauracja_otwarta)
        sleep(1);

    wait_for_turn(2);

    print_kitchen_summary();

    printf("Kucharz kończy pracę.\n");
    printf("======================\n");
    fflush(stdout);

    *kolej_podsumowania = 3;

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
    kucharz();
    return 0;
}
