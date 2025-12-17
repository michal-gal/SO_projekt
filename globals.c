#include "procesy.h"
#include <stdio.h>

// Definicje zmiennych globalnych
Kolejka *kolejka_klientow;
Tasma *tasma;
Stolik *stoly;
int *wszyscy_klienci;
int *vip_licznik;
int *sygnal_kierownika;

sem_t *kolejka_sem;
sem_t *tasma_sem;

int P, X1, X2, X3, X4, N, Tp, Tk;
FILE *raport;