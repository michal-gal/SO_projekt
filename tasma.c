#include "procesy.h"

#include <stdio.h>

void dodaj_danie(struct Talerzyk *tasma_local, int cena)
{
    do
    {
        struct Talerzyk ostatni = tasma_local[MAX_TASMA - 1];

        for (int i = MAX_TASMA - 1; i > 0; i--)
        {
            tasma_local[i] = tasma_local[i - 1];
        }

        tasma_local[0] = ostatni; // WRACA NA POCZĄTEK
    } while (tasma_local[0].cena != 0);

    tasma_local[0].cena = cena;
    tasma_local[0].stolik_specjalny = 0;

    if (cena == p40 || cena == p50 || cena == p60)
        printf("Danie specjalne za %d zł dodane na taśmę.\n", cena);
    else
        printf("Danie za %d zł dodane na taśmę.\n", cena);
}
