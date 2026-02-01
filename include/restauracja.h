#ifndef RESTAURACJA_H
#define RESTAURACJA_H

// ====== INKLUDY ======
#include "common.h"

// Nagłówek dla modułu programu głównego (restauracja.c).

// API eksportowane
// Inicjalizuje podsystem restauracji. Przy powodzeniu zwraca 0 i ustawia
// `*out_czas_pracy` na skonfigurowany czas pracy; przy błędzie zwraca wartość
// niezerową.
int inicjuj_restauracje(int argc, char **argv, int *out_czas_pracy);
int uruchom_restauracje(int czas_pracy);
int zamknij_restauracje(int *status);

#endif // RESTAURACJA_H
