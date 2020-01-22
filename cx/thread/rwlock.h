#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>
#include "sema.h"

CX_C_BEGIN

#define RWLOCK_READER_MAX 4095
#define RWLOCK_READWAIT_MAX 2047
#define RWLOCK_WRITER_MAX 511

#define RWLOCK_READER_MASK              (0x00000fff)
#define RWLOCK_READWAIT_MASK            (0x007ff000)
#define RWLOCK_WRITER_MASK              (0xff800000)
#define RWLOCK_READERS(state)   (state & RWLOCK_READER_MASK)
#define RWLOCK_READWAIT(state) ((state & RWLOCK_READWAIT_MASK) >> 12)
#define RWLOCK_WRITERS(state)  ((state & RWLOCK_WRITER_MASK)   >> 23)
#define RWLOCK_READ_ADD                  0x00000001
#define RWLOCK_READWAIT_ADD              0x00001000
#define RWLOCK_WRITE_ADD                 0x00800000

typedef struct RWLock {
    atomic(uint32) state;
    Semaphore rwait;
    Semaphore wwait;
} RWLock;

bool rwlockInit(RWLock *l);
bool _rwlockContendedAcquireRead(RWLock *l, int64 timeout);
bool _rwlockContendedAcquireWrite(RWLock *l, int64 timeout);

_meta_inline bool rwlockTryAcquireRead(RWLock *l)
{
    uint32 state = atomicLoad(uint32, &l->state, Relaxed);
    // only valid when no writer locks are held
    while (RWLOCK_WRITERS(state) == 0) {
        // cannot acquire if we are at the max
        if (RWLOCK_READERS(state) == RWLOCK_READER_MAX)
            return false;
        if (atomicCompareExchange(uint32, weak, &l->state, &state, state + RWLOCK_READ_ADD, Acquire, Relaxed))
            return true;        // got the lock
    }

    // no longer have valid conditions in which this lock can be acquired
    return false;
}

_meta_inline bool rwlockTryAcquireWrite(RWLock *l)
{
    uint32 state = atomicLoad(uint32, &l->state, Relaxed);
    // only valid when no other writer locks are held, and there are no (active) readers
    while (RWLOCK_WRITERS(state) == 0 && RWLOCK_READERS(state) == 0) {
        // make sure we didn't hit the limit
        if (RWLOCK_WRITERS(state) == RWLOCK_WRITER_MAX)
            return false;
        if (atomicCompareExchange(uint32, weak, &l->state, &state, state + RWLOCK_WRITE_ADD, Acquire, Relaxed))
            return true;        // got the lock
    }

    // no longer have valid conditions in which this lock can be acquired
    return false;
}

_meta_inline bool rwlockAcquireRead(RWLock *l)
{
    // try lightweight no-contention path inline
    if (rwlockTryAcquireRead(l))
        return true;

    return _rwlockContendedAcquireRead(l, timeForever);
}

_meta_inline bool rwlockTryAcquireReadTimeout(RWLock *l, int64 timeout)
{
    // try lightweight no-contention path inline
    if (rwlockTryAcquireRead(l))
        return true;

    return _rwlockContendedAcquireRead(l, timeout);
}

_meta_inline bool rwlockAcquireWrite(RWLock *l)
{
    // try lightweight no-contention path inline
    if (rwlockTryAcquireWrite(l))
        return true;

    return _rwlockContendedAcquireWrite(l, timeForever);
}

_meta_inline bool rwlockTryAcquireWriteTimeout(RWLock *l, int64 timeout)
{
    // try lightweight no-contention path inline
    if (rwlockTryAcquireWrite(l))
        return true;

    return _rwlockContendedAcquireWrite(l, timeout);
}

_meta_inline bool rwlockReleaseRead(RWLock *l)
{
    devAssert(RWLOCK_READERS(atomicLoad(uint32, &l->state, Relaxed)) > 0);
    uint32 oldstate = atomicFetchSub(uint32, &l->state, RWLOCK_READ_ADD, Release);

    // If we were the last reader and any writers are waiting, unblock one
    if (RWLOCK_READERS(oldstate) == 1 && RWLOCK_WRITERS(oldstate) > 0)
        semaInc(&l->wwait, 1);

    return true;
}

bool rwlockReleaseWrite(RWLock *l);
void rwlockDestroy(RWLock *l);

CX_C_END
