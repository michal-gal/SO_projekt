#include "procesy_internal.h"

#include <stdio.h>
#include <stdlib.h>

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
    wait_until_closed_and_no_active_clients();

    wait_for_turn(2);

    print_kitchen_summary();

    printf("Kucharz kończy pracę.\n");
    printf("======================\n");
    fflush(stdout);

    *kolej_podsumowania = 3;

    exit(0);
}
