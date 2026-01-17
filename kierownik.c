#include "procesy_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void kierownik_update_signal(void)
{
    *sygnal_kierownika = rand() % 50;
    printf("Kierownik zmienia sygnał na: %d\n", *sygnal_kierownika);
}

void kierownik(void)
{
    while (*restauracja_otwarta)
    {
        kierownik_update_signal();
        sleep(1);
    }

    wait_until_no_active_clients();

    wait_for_turn(3);

    printf("Kierownik kończy pracę.\n");
    fflush(stdout);
    exit(0);
}
