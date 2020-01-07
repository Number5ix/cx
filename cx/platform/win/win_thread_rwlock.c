#include "cx/cx.h"
#include "cx/thread/mutex.h"
#include "cx/platform/win.h"

#ifdef _ARCH_X64

#include <synchapi.h>

typedef struct RWLock {
    SRWLOCK srw;
} RWLock;

RWLock *rwlockCreate()
{
    RWLock *ret = (RWLock*)xaAlloc(sizeof(RWLock), Zero);
    InitializeSRWLock(&ret->srw);
    return ret;
}

bool rwlockAcquireRead(RWLock *m)
{
    AcquireSRWLockShared(&m->srw);
    return true;
}

bool rwlockAcquireWrite(RWLock *m)
{
    AcquireSRWLockExclusive(&m->srw);
    return true;
}

bool rwlockTryAcquireRead(RWLock *m)
{
    return TryAcquireSRWLockShared(&m->srw);
}

bool rwlockTryAcquireWrite(RWLock *m)
{
    return TryAcquireSRWLockExclusive(&m->srw);
}

bool rwlockReleaseRead(RWLock *m)
{
    ReleaseSRWLockShared(&m->srw);
    return true;
}

bool rwlockReleaseWrite(RWLock *m)
{
    ReleaseSRWLockExclusive(&m->srw);
    return true;
}

void rwlockDestroy(RWLock *m)
{
    xaFree(m);
}

#else

// Windows XP doesn't have built-in reader-writer locks
// For now we just use critical sections, which makes this equivalent to a mutex.
// This is less performant as readers block on each other, but acceptable from a
// code-correctness perspective.

// If it becomes an issue on 32-bit builds, we'll replace this with a custom RW
// lock implementaiton using atomics.

typedef struct RWLock {
    CRITICAL_SECTION cs;
} RWLock;

RWLock *rwlockCreate()
{
    RWLock *ret = (RWLock*)xaAlloc(sizeof(RWLock), Zero);
    InitializeCriticalSectionAndSpinCount(&ret->cs, 5000);
    return ret;
}

bool rwlockAcquireRead(RWLock *m)
{
    EnterCriticalSection(&m->cs);
    return true;
}

bool rwlockAcquireWrite(RWLock *m)
{
    EnterCriticalSection(&m->cs);
    return true;
}

bool rwlockTryAcquireRead(RWLock *m)
{
    return TryEnterCriticalSection(&m->cs);
}

bool rwlockTryAcquireWrite(RWLock *m)
{
    return TryEnterCriticalSection(&m->cs);
}

bool rwlockReleaseRead(RWLock *m)
{
    LeaveCriticalSection(&m->cs);
    return true;
}

bool rwlockReleaseWrite(RWLock *m)
{
    LeaveCriticalSection(&m->cs);
    return true;
}

void rwlockDestroy(RWLock *m)
{
    DeleteCriticalSection(&m->cs);
    xaFree(m);
}

#endif
