#include "procesy_internal.h"

#include <stdio.h>
#include <string.h>

int find_table_for_group_locked(const struct Grupa *g)
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

void generator_stolikow(struct Stolik *stoliki_local)
{
    int idx = 0;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < ILOSC_STOLIKOW[i]; j++)
        {
            int suma_poprzednich = 0;
            for (int k = 0; k < i; k++)
                suma_poprzednich += ILOSC_STOLIKOW[k];

            idx = suma_poprzednich + j;
            stoliki_local[idx].numer_stolika = idx + 1;
            stoliki_local[idx].pojemnosc = i + 1;
            stoliki_local[idx].liczba_grup = 0;
            stoliki_local[idx].zajete_miejsca = 0;
            memset(stoliki_local[idx].grupy, 0, sizeof(stoliki_local[idx].grupy));

            printf("Stolik %d o pojemności %d utworzony.\n",
                   stoliki_local[idx].numer_stolika,
                   stoliki_local[idx].pojemnosc);
        }
    }
}
