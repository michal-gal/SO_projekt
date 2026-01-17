#include "procesy_internal.h"

#include <unistd.h>

void wait_for_turn(int turn)
{
    while (*kolej_podsumowania != turn)
        usleep(50000);
}

void wait_until_no_active_clients(void)
{
    while (*aktywni_klienci > 0)
        sleep(1);
}

void wait_until_closed_and_no_active_clients(void)
{
    while (*restauracja_otwarta || *aktywni_klienci > 0)
        sleep(1);
}
