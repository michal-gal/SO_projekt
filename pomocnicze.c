#include "restauracja.h"

// ====== SEMAFORY ======
void sem_op(int sem, int val)
{
    struct sembuf sb = {sem, val, 0};
    semop(sem_id, &sb, 1);
}

// ====== KOLEJKA ======
void push(Grupa g)
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

int pop(Grupa *g)
{
    sem_op(SEM_KOLEJKA, -1);
    if (kolejka->ilosc == 0)
    {
        sem_op(SEM_KOLEJKA, 1);
        return 0;
    }
    *g = kolejka->q[kolejka->przod];
    kolejka->przod = (kolejka->przod + 1) % MAX_KOLEJKA;
    kolejka->ilosc--;
    sem_op(SEM_KOLEJKA, 1);
    return 1;
}
