#include "procesy.h"

#include <string.h>

void push(struct Grupa g)
{
    sem_op(SEM_KOLEJKA, -1);
    if (kolejka->ilosc < MAX_KOLEJKA)
    {
        kolejka->q[kolejka->tyl] = g;
        kolejka->tyl = (kolejka->tyl + 1) % MAX_KOLEJKA;
        kolejka->ilosc++;
    }
    sem_op(SEM_KOLEJKA, 1);
}

struct Grupa pop(void)
{
    struct Grupa g = {0};
    sem_op(SEM_KOLEJKA, -1);
    if (kolejka->ilosc == 0)
    {
        sem_op(SEM_KOLEJKA, 1);
        return g;
    }
    g = kolejka->q[kolejka->przod];
    kolejka->przod = (kolejka->przod + 1) % MAX_KOLEJKA;
    kolejka->ilosc--;
    sem_op(SEM_KOLEJKA, 1);
    return g;
}
