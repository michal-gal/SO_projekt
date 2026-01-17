#include "procesy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static int env_int_or_die(const char *name)
{
    const char *s = getenv(name);
    if (!s || !*s)
    {
        fprintf(stderr, "Brak zmiennej środowiskowej: %s\n", name);
        exit(1);
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || (end && *end != '\0'))
    {
        fprintf(stderr, "Nieprawidłowa wartość %s=%s\n", name, s);
        exit(1);
    }
    return (int)v;
}

int main(void)
{
    int shm = env_int_or_die("RESTAURACJA_SHM_ID");
    int sem = env_int_or_die("RESTAURACJA_SEM_ID");
    dolacz_ipc(shm, sem);
    kierownik();
    return 0;
}
