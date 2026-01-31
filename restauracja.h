#ifndef RESTAURACJA_H
#define RESTAURACJA_H

// ====== INKLUDY ======
#include "common.h"

// Header dla modułu programu głównego (restauracja.c).

// Exported API
// Initialize the restaurant subsystem. On success returns 0 and sets
// `*out_czas_pracy` to the configured runtime; non-zero indicates error.
int init_restauracja(int argc, char **argv, int *out_czas_pracy);

// Run the main restaurant loop for `czas_pracy` seconds. Returns 0.
int run_restauracja(int czas_pracy);

// Perform shutdown and cleanup; accepts pointer to status for reuse.
int shutdown_restauracja(int *status);

#endif // RESTAURACJA_H
