#include "procesy.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/msg.h>

typedef struct
{
    long mtype;
    struct Grupa grupa;
} QueueMsg;

void push(struct Grupa g)
{
    QueueMsg msg;
    msg.mtype = 1;
    msg.grupa = g;

    for (;;)
    {
        if (msgsnd(msgq_id, &msg, sizeof(msg.grupa), IPC_NOWAIT) == 0)
            return;

        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
            return; // kolejka pełna - zachowanie jak wcześniej (drop)
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        perror("msgsnd");
        return;
    }
}

struct Grupa pop(void)
{
    struct Grupa g = {0};
    QueueMsg msg;

    for (;;)
    {
        ssize_t r = msgrcv(msgq_id, &msg, sizeof(msg.grupa), 1, IPC_NOWAIT);
        if (r >= 0)
            return msg.grupa;

        if (errno == EINTR)
            continue;
        if (errno == ENOMSG)
            return g;
        if (errno == EIDRM || errno == EINVAL)
            exit(0);

        perror("msgrcv");
        return g;
    }
}
