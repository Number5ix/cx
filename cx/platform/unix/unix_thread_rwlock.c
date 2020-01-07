#include "cx/cx.h"
#include "cx/thread/rwlock.h"

#include <pthread.h>

typedef struct RWLock {
    pthread_rwlock_t ptrwl;
} RWLock;

RWLock *rwlockCreate()
{
    RWLock *ret = (RWLock*)xaAlloc(sizeof(RWLock), Zero);

    if (pthread_rwlock_init(&ret->ptrwl, NULL) != 0) {
        xaFree(ret);
        ret = NULL;
    }

    return ret;
}

bool rwlockAcquireRead(RWLock *m)
{
    return !pthread_rwlock_rdlock(&m->ptrwl);
}

bool rwlockAcquireWrite(RWLock *m)
{
    return !pthread_rwlock_wrlock(&m->ptrwl);
}

bool rwlockTryAcquireRead(RWLock *m)
{
    return !pthread_rwlock_tryrdlock(&m->ptrwl);
}

bool rwlockTryAcquireWrite(RWLock *m)
{
    return !pthread_rwlock_trywrlock(&m->ptrwl);
}

bool rwlockReleaseRead(RWLock *m)
{
    return !pthread_rwlock_unlock(&m->ptrwl);
}

bool rwlockReleaseWrite(RWLock *m)
{
    return !pthread_rwlock_unlock(&m->ptrwl);
}

void rwlockDestroy(RWLock *m)
{
    pthread_rwlock_destroy(&m->ptrwl);
    xaFree(m);
}
