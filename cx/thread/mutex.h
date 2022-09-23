#pragma once

#include <cx/cx.h>
#include <cx/time/time.h>
#include <cx/utils/macros.h>
#include "futex.h"
#include "aspin.h"

#ifdef CX_LOCK_DEBUG
#include <cx/log/log.h>
#endif

CX_C_BEGIN

enum MUTEX_Flags {
    MUTEX_NoSpin = 1,           // do not use adaptive spin, use kernel futex only
};

typedef struct Mutex {
    // Futex values:
    // 0 - Lock is not held
    // 1 - Lock is held
    // 2 - Lock is held and there is contention
    Futex ftx;
    AdaptiveSpin aspin;
} Mutex;

bool _mutexInit(Mutex *m, uint32 flags);
#define mutexInit(m, ...) _mutexInit(m, opt_flags(__VA_ARGS__))
bool mutexTryAcquireTimeout(Mutex *m, int64 timeout);
bool mutexRelease(Mutex *m);
_meta_inline bool mutexTryAcquire(Mutex *m)
{
    int32 curstate = atomicLoad(int32, &m->ftx.val, Relaxed);
    if (curstate == 0 && atomicCompareExchange(int32, strong, &m->ftx.val, &curstate, 1, Acquire, Relaxed)) {
        aspinRecordUncontended(&m->aspin);
        return true;
    }
    return false;
}

_meta_inline bool mutexAcquire(Mutex *m)
{
    return mutexTryAcquireTimeout(m, timeForever);
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
