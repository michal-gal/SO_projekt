#ifndef PROCESY_INTERNAL_H
#define PROCESY_INTERNAL_H

#include "procesy.h"

// Wspólne helpery używane pomiędzy modułami procesów.

int price_to_index(int cena);

// Wywoływać tylko przy trzymanym SEM_STOLIKI.
int find_table_for_group_locked(const struct Grupa *g);

void wait_for_turn(int turn);
void wait_until_no_active_clients(void);
void wait_until_closed_and_no_active_clients(void);

#endif
