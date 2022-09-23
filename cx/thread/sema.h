#pragma once

#include <cx/cx.h>
#include <cx/thread/atomic.h>
#include <cx/time/time.h>
#include <cx/utils/macros.h>
#include "futex.h"
#include "aspin.h"

enum SEMA_Flags {
    SEMA_NoSpin            = 0x00000001,
};

typedef struct Semaphore {
    Futex ftx;
    AdaptiveSpin aspin;
} Semaphore;

bool _semaInit(Semaphore *sema, int32 count, uint32 flags);
#define semaInit(sema, count, ...) _semaInit(sema, count, opt_flags(__VA_ARGS__))
bool semaDestroy(Semaphore *sema);
bool semaTryDecTimeout(Semaphore *sema, int64 timeout);

_meta_inline bool semaTryDec(Semaphore *sema)
{
    int32 curcount = atomicLoad(int32, &sema->ftx.val, Relaxed);
    bool ret = (curcount > 0 && atomicCompareExchange(int32, strong, &sema->ftx.val, &curcount,
                                                      curcount - 1, Acquire, Relaxed));
    if (ret)
        aspinRecordUncontended(&sema->aspin);
    return ret;
}

_meta_inline bool semaDec(Semaphore *sema)
{
    return semaTryDecTimeout(sema, timeForever);
}

_meta_inline bool semaInc(Semaphore *sema, int32 count)
{
    atomicFetchAdd(int32, &sema->ftx.val, count, Release);
    futexWakeMany(&sema->ftx, count);
    return true;
}
