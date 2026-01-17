#include "procesy_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void active_clients_inc(void)
{
    sem_op(SEM_KOLEJKA, -1);
    (*aktywni_klienci)++;
    sem_op(SEM_KOLEJKA, 1);
}

static void active_clients_dec_if_positive(void)
{
    sem_op(SEM_KOLEJKA, -1);
    if (*aktywni_klienci > 0)
        (*aktywni_klienci)--;
    sem_op(SEM_KOLEJKA, 1);
}

static void generator_reap_children_nonblocking(void)
{
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0)
        active_clients_dec_if_positive();
}

static void generator_reap_children_blocking(void)
{
    int status;
    while (waitpid(-1, &status, 0) > 0)
        active_clients_dec_if_positive();
}

static void generator_spawn_one_group(void)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        execl("./klient", "klient", (char *)NULL);
        perror("execl ./klient");
        _exit(127);
    }
    if (pid > 0)
    {
        active_clients_inc();
        return;
    }

    perror("fork");
}

void generator_klientow(void)
{
    srand(getpid());

    while (*restauracja_otwarta)
    {
        generator_reap_children_nonblocking();
        generator_spawn_one_group();
        sleep(rand() % 3 + 1);
    }

    generator_reap_children_blocking();

    wait_for_turn(0);

    printf("Generator klientów kończy pracę.\n");
    fflush(stdout);

    *kolej_podsumowania = 1;

    exit(0);
}
