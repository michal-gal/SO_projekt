#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <time.h>
#include <unistd.h>

struct SzatniaCtx
{
    volatile sig_atomic_t shutdown_requested;
};

static struct SzatniaCtx szat_ctx_storage = {.shutdown_requested = 0};
static struct SzatniaCtx *szat_ctx = &szat_ctx_storage;

static void kolejka_dodaj_local(struct Grupa g);
static struct Grupa kolejka_pobierz_local(void);

static void szatnia_obsluz_sigterm(int signo)
{
    (void)signo;
    szat_ctx->shutdown_requested = 1;
}

static int usadz_grupe(const struct Grupa *g, int *numer_stolika,
                       int *zajete, int *pojemnosc)
{
    int stolik_idx = -1;
    int usadzono = 0;

    pthread_mutex_lock(&common_ctx->stoliki_sync->mutex);
    stolik_idx = znajdz_stolik_dla_grupy_zablokowanej(g);
    if (stolik_idx >= 0)
    {
        struct Stolik *st = &common_ctx->stoliki[stolik_idx];
        st->grupy[st->liczba_grup] = *g;
        st->zajete_miejsca += g->osoby;
        st->liczba_grup++;
        usadzono = 1;
        if (numer_stolika)
            *numer_stolika = st->numer_stolika;
        if (zajete)
            *zajete = st->zajete_miejsca;
        if (pojemnosc)
            *pojemnosc = st->pojemnosc;
    }
    pthread_mutex_unlock(&common_ctx->stoliki_sync->mutex);

    return usadzono;
}

static void szatnia_petla(void)
{
    while (*common_ctx->restauracja_otwarta && !szat_ctx->shutdown_requested)
    {
        struct Grupa g = kolejka_pobierz_local();
        LOGD("szatnia: pid=%d kolejka_pobierz returned group=%d\n",
             (int)getpid(), g.numer_grupy);
        if (g.numer_grupy == 0)
            continue;

        int numer_stolika = 0;
        int zajete = 0;
        int pojemnosc = 0;
        if (usadz_grupe(&g, &numer_stolika, &zajete, &pojemnosc))
        {
            LOGP("Grupa usadzona: %d przy stoliku: %d (%d/%d miejsc zajętych)\n",
                 g.numer_grupy, numer_stolika, zajete, pojemnosc);
            /* Zliczamy osoby (klientów), a nie grupy. */
            (*common_ctx->klienci_przyjeci) += g.osoby;
            if (g.proces_id > 0)
                (void)kill(g.proces_id, SIGUSR1);
        }
        else if (*common_ctx->restauracja_otwarta)
        {
            kolejka_dodaj_local(g);
        }

        sched_yield();
    }
}

static void kolejka_dodaj_local(struct Grupa g)
{
    QueueMsg msg;
    msg.mtype = 1;
    msg.grupa = g;
    for (;;)
    {
        if (!*common_ctx->restauracja_otwarta)
            return;

        struct timespec now, abstime;
        clock_gettime(CLOCK_REALTIME, &now);
        abstime = now;
        abstime.tv_sec += 1;

        if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) != 0)
            continue;
        while (common_ctx->queue_sync->count >= common_ctx->queue_sync->max)
        {
            int rc = pthread_cond_timedwait(
                &common_ctx->queue_sync->not_full, &common_ctx->queue_sync->mutex,
                &abstime);
            if (rc == ETIMEDOUT)
            {
                if (!*common_ctx->restauracja_otwarta)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    return;
                }
                clock_gettime(CLOCK_REALTIME, &now);
                abstime = now;
                abstime.tv_sec += 1;
                continue;
            }
            if (rc != 0)
            {
                if (common_ctx->shutdown_flag_ptr &&
                    *common_ctx->shutdown_flag_ptr)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    exit(0);
                }
                continue;
            }
        }

        if (msgsnd(common_ctx->msgq_id, &msg, sizeof(msg.grupa), IPC_NOWAIT) == 0)
        {
            common_ctx->queue_sync->count++;
            (*common_ctx->klienci_w_kolejce) += msg.grupa.osoby;
            pthread_cond_signal(&common_ctx->queue_sync->not_empty);
            pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            return;
        }

        pthread_mutex_unlock(&common_ctx->queue_sync->mutex);

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            continue;
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return;
    }
}

static struct Grupa kolejka_pobierz_local(void)
{
    struct Grupa g = {0};
    QueueMsg msg;
    for (;;)
    {
        if (!*common_ctx->restauracja_otwarta)
            return g;

        if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) != 0)
            continue;
        struct timespec now, abstime;
        clock_gettime(CLOCK_REALTIME, &now);
        abstime = now;
        abstime.tv_sec += 1;
        while (common_ctx->queue_sync->count <= 0)
        {
            if (!*common_ctx->restauracja_otwarta)
            {
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                return g;
            }
            int rc = pthread_cond_timedwait(&common_ctx->queue_sync->not_empty,
                                            &common_ctx->queue_sync->mutex,
                                            &abstime);
            if (rc == ETIMEDOUT)
            {
                if (!*common_ctx->restauracja_otwarta ||
                    (common_ctx->shutdown_flag_ptr &&
                     *common_ctx->shutdown_flag_ptr))
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    return g;
                }
                clock_gettime(CLOCK_REALTIME, &now);
                abstime = now;
                abstime.tv_sec += 1;
                continue;
            }
            if (rc != 0)
            {
                if (common_ctx->shutdown_flag_ptr &&
                    *common_ctx->shutdown_flag_ptr)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    exit(0);
                }
                continue;
            }
        }

        common_ctx->queue_sync->count--;
        pthread_cond_signal(&common_ctx->queue_sync->not_full);
        pthread_mutex_unlock(&common_ctx->queue_sync->mutex);

        ssize_t r = msgrcv(common_ctx->msgq_id, &msg, sizeof(msg.grupa), 1,
                           IPC_NOWAIT);
        if (r >= 0)
        {
            if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) == 0)
            {
                if (*common_ctx->klienci_w_kolejce >= msg.grupa.osoby)
                    *common_ctx->klienci_w_kolejce -= msg.grupa.osoby;
                else
                    *common_ctx->klienci_w_kolejce = 0;
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            }
            return msg.grupa;
        }

        LOGD("kolejka_pobierz: pid=%d msgrcv failed errno=%d\n", (int)getpid(),
             errno);
        if (errno == EINTR || errno == ENOMSG)
        {
            if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) == 0)
            {
                common_ctx->queue_sync->count++;
                pthread_cond_signal(&common_ctx->queue_sync->not_empty);
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            }
            continue;
        }

        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return g;
    }
}

int main(int argc, char **argv)
{
    if (dolacz_ipc_z_argv(argc, argv, 0, NULL) != 0)
        return 1;

    if (signal(SIGTERM, szatnia_obsluz_sigterm) == SIG_ERR)
        LOGE_ERRNO("signal(SIGTERM)");
    ustaw_shutdown_flag(&szat_ctx->shutdown_requested);

    szatnia_petla();
    return 0;
}
