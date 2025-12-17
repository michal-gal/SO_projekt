#include "procesy.h"
#include <stdio.h>

// Definicje zmiennych globalnych
Kolejka *kolejka_klientow;
Tasma *tasma;
Stolik *stoly;
int *wszyscy_klienci;
int *vip_licznik;
int *sygnal_kierownika;

int kolejka_sem_id;
int tasma_sem_id;

int P, X1, X2, X3, X4, N, Tp, Tk;
FILE *raport;