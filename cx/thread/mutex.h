#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>
#include "sema.h"

#ifdef CX_LOCK_DEBUG
#include <cx/log/log.h>
#endif

CX_C_BEGIN

typedef struct Mutex {
    atomic(int32) waiting;
    Semaphore sema;
} Mutex;

bool mutexInit(Mutex *m);
bool _mutexContendedAcquire(Mutex *m, int64 timeout);
_meta_inline bool mutexTryAcquire(Mutex *m)
{
    if (atomicLoad(int32, &m->waiting, Relaxed) != 0)
        return false;
    int nowait = 0;
    return atomicCompareExchange(int32, strong, &m->waiting, &nowait, 1, Acquire, Acquire);
}

_meta_inline bool mutexAcquire(Mutex *m)
{
    // try lightweight no-contention path inline
    if (mutexTryAcquire(m))
        return true;

    return _mutexContendedAcquire(m, timeForever);
}

#ifdef CX_LOCK_DEBUG
#define _logFmtMutexArgComp2(level, fmt, nargs, args) _logFmt_##level(LOG_##level, LogDefault, fmt, nargs, args)
#define _logFmtMutexArgComp(level, fmt, ...)          _logFmtMutexArgComp2(level, fmt, count_macro_args(__VA_ARGS__), (stvar[]){ __VA_ARGS__ })
_meta_inline bool mutexLogAndAcquire(Mutex *m, const char *name, const char *filename, int line)
{
    _logFmtMutexArgComp(CX_LOCK_DEBUG, _S"Locking mutex ${string} at ${string}:${int}",
                        stvar(string, (string)name), stvar(string, (string)filename), stvar(int32, line));
    return mutexAcquire(m);
}

#define mutexAcquire(m) mutexLogAndAcquire(m, #m, __FILE__, __LINE__)
#endif

_meta_inline bool mutexTryAcquireTimeout(Mutex *m, int64 timeout)
{
    // try lightweight no-contention path inline
    if (mutexTryAcquire(m))
        return true;

    return _mutexContendedAcquire(m, timeout);
}

_meta_inline bool mutexRelease(Mutex *m)
{
    int waiters = atomicFetchSub(int32, &m->waiting, 1, Release);
    devAssert(waiters > 0);
    if (waiters > 1) {
        semaInc(&m->sema, 1);
        return true;
    }
    return false;
}

#ifdef CX_LOCK_DEBUG
_meta_inline bool mutexLogAndRelease(Mutex *m, const char *name, const char *filename, int line)
{
    _logFmtMutexArgComp(CX_LOCK_DEBUG, _S"Releasing mutex ${string} at ${string}:${int}",
                        stvar(string, (string)name), stvar(string, (string)filename), stvar(int32, line));
    return mutexRelease(m);
}

#define mutexRelease(m) mutexLogAndRelease(m, #m, __FILE__, __LINE__)
#endif

void mutexDestroy(Mutex *m);

CX_C_END
