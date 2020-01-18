#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>
#include "sema.h"

CX_C_BEGIN

typedef struct Mutex {
    atomic_int32 waiting;
    Semaphore sema;
} Mutex;

bool mutexInit(Mutex *m);
bool _mutexContendedAcquire(Mutex *m, int64 timeout);
_meta_inline bool mutexTryAcquire(Mutex *m)
{
    if (atomic_load_int32(&m->waiting, ATOMIC_RELAXED) != 0)
        return false;
    int nowait = 0;
    return atomic_compare_exchange_strong_int32(&m->waiting, &nowait, 1, ATOMIC_ACQUIRE, ATOMIC_ACQUIRE);
}

_meta_inline bool mutexAcquire(Mutex *m)
{
    // try lightweight no-contention path inline
    if (mutexTryAcquire(m))
        return true;

    return _mutexContendedAcquire(m, timeForever);
}

_meta_inline bool mutexTryAcquireTimeout(Mutex *m, int64 timeout)
{
    // try lightweight no-contention path inline
    if (mutexTryAcquire(m))
        return true;

    return _mutexContendedAcquire(m, timeout);
}

_meta_inline bool mutexRelease(Mutex *m)
{
    int waiters = atomic_fetch_sub_int32(&m->waiting, 1, ATOMIC_RELEASE);
    devAssert(waiters > 0);
    if (waiters > 1) {
        semaInc(&m->sema, 1);
        return true;
    }
    return false;
}

void mutexDestroy(Mutex *m);

CX_C_END
