#include "mutex.h"

bool mutexInit(Mutex *m)
{
    memset(m, 0, sizeof(Mutex));
    semaInit(&m->sema, 0);
    return true;
}

bool _mutexContendedAcquire(Mutex *m, int64 timeout)
{
    if (atomic_fetch_add_int32(&m->waiting, 1, ATOMIC_ACQUIRE) > 0) {
        if (timeout == timeForever)
            return semaDec(&m->sema);
        else
            return semaTryDecTimeout(&m->sema, timeout);
    }
    return true;
}

void mutexDestroy(Mutex *m)
{
    semaDestroy(&m->sema);
}
