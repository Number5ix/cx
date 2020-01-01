#include "cx/cx.h"
#include "cx/thread/mutex.h"

#include <pthread.h>

typedef struct Mutex {
    pthread_mutex_t ptmtx;
} Mutex;

Mutex *mutexCreate()
{
    Mutex *ret = (Mutex*)xaAlloc(sizeof(Mutex), XA_ZERO);

    if (pthread_mutex_init(&ret->ptmtx, NULL) != 0) {
        xaFree(ret);
        ret = NULL;
    }

    return ret;
}

bool mutexAcquire(Mutex *m)
{
    return !pthread_mutex_lock(&m->ptmtx);
}

bool mutexTryAcquire(Mutex *m)
{
    return !pthread_mutex_trylock(&m->ptmtx);
}

bool mutexRelease(Mutex *m)
{
    return !pthread_mutex_unlock(&m->ptmtx);
}

void mutexDestroy(Mutex *m)
{
    pthread_mutex_destroy(&m->ptmtx);
    xaFree(m);
}
