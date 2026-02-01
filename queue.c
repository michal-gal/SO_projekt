#define _POSIX_C_SOURCE 200809L
#include "queue.h"

#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/msg.h>

void kolejka_dodaj(struct Grupa g) // dodaje grupę do kolejki
{
    QueueMsg msg;
    msg.mtype = 1;
    msg.grupa = g;
    for (;;)
    {
        if (!*common_ctx->restauracja_otwarta)
            return;

        /* Zarezerwuj slot używając queue_sync. Blokuj z timeoutem podobnym do
         * semtimedop. */
        struct timespec now, abstime;
        clock_gettime(CLOCK_REALTIME, &now);
        abstime = now;
        abstime.tv_sec += 1; /* 1s timeout */

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
                /* przeliczenie abstime dla następnego czekania */
                clock_gettime(CLOCK_REALTIME, &now);
                abstime = now;
                abstime.tv_sec += 1;
                continue;
            }
            if (rc != 0)
            {
                /* przerwane lub błąd */
                if (common_ctx->shutdown_flag_ptr &&
                    *common_ctx->shutdown_flag_ptr)
                {
                    pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
                    exit(0);
                }
                /* spróbuj ponownie */
                continue;
            }
        }

        /* Mamy zarezerwowany slot logicznie; spróbuj wysłać wiadomość. */
        if (msgsnd(common_ctx->msgq_id, &msg, sizeof(msg.grupa), IPC_NOWAIT) == 0)
        {
            common_ctx->queue_sync->count++;
            /* Zliczamy rzeczywistą liczbę klientów (osób), nie tylko grup. */
            (*common_ctx->klienci_w_kolejce) += msg.grupa.osoby;
            pthread_cond_signal(&common_ctx->queue_sync->not_empty);
            pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            return;
        }

        /* Nie udało się wysłać - zwolnij zarezerwowany slot. */
        pthread_mutex_unlock(&common_ctx->queue_sync->mutex);

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            continue; /* kolejka pełna - spróbuj ponownie */
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        return;
    }
}

struct Grupa kolejka_pobierz(void) // pobiera grupę z kolejki
{
    struct Grupa g = {0};
    QueueMsg msg;
    /* Użyj nieblokującego odbioru, aby szybko reagować na zamknięcie.
       Jeśli nie ma wiadomości, śpij krótko i spróbuj ponownie. Nadal
       zwalniamy slot semafora po pomyślnym odbiorze. */
    /* Użyj semafora licznika wiadomości, aby czekać na wiadomości bez aktywnego
       czekania. Próbujemy nieblokującego decrementu na SEM_MSGS; jeśli się
       powiedzie, wiadomość powinna być dostępna i pobieramy ją z nieblokującym
       msgrcv. Jeśli nie ma wiadomości lub restauracja się zamyka, zwracamy. */
    /* Blokuj na semaforze licznika wiadomości (czekaj na producenta). Używanie
       blokującego semop pozwala konsumentom spać tutaj, dopóki wiadomość nie
       będzie dostępna, jednocześnie obsługując EINTR i zamknięcie czysto. */
    for (;;)
    {
        if (!*common_ctx->restauracja_otwarta)
            return g;

        if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) != 0)
            continue;
        struct timespec now, abstime;
        clock_gettime(CLOCK_REALTIME, &now);
        abstime = now;
        abstime.tv_sec += 1; /* 1s timeout */
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

        /* Zarezerwuj jeden token wiadomości i spróbuj odebrać bez blokowania. */
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

        /* Jeśli odbiór się nie udał, przywróć token i obsłuż błędy. */
        LOGD("kolejka_pobierz: pid=%d msgrcv failed errno=%d\n", (int)getpid(),
             errno);
        if (errno == EINTR)
        {
            /* Przywróć count, ponieważ wiadomość nie została zużyta. */
            if (pthread_mutex_lock(&common_ctx->queue_sync->mutex) == 0)
            {
                common_ctx->queue_sync->count++;
                pthread_cond_signal(&common_ctx->queue_sync->not_empty);
                pthread_mutex_unlock(&common_ctx->queue_sync->mutex);
            }
            continue;
        }
        if (errno == ENOMSG)
        {
            /* Count był przestarzały; przywróć licznik i spróbuj ponownie. */
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
