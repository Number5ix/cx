#include "cx/cx.h"
#include "cx/thread/mutex.h"
#include "cx/platform/win.h"

#ifdef _ARCH_X64

#include <synchapi.h>

typedef struct Mutex {
    SRWLOCK srw;
} Mutex;

Mutex *mutexCreate()
{
    Mutex *ret = (Mutex*)xaAlloc(sizeof(Mutex), XA_ZERO);
    InitializeSRWLock(&ret->srw);
    return ret;
}

bool mutexAcquire(Mutex *m)
{
    AcquireSRWLockExclusive(&m->srw);
    return true;
}

bool mutexTryAcquire(Mutex *m)
{
    return TryAcquireSRWLockExclusive(&m->srw);
}

bool mutexRelease(Mutex *m)
{
    ReleaseSRWLockExclusive(&m->srw);
    return true;
}

void mutexDestroy(Mutex *m)
{
    xaFree(m);
}

#else

typedef struct Mutex {
    CRITICAL_SECTION cs;
} Mutex;

Mutex *mutexCreate()
{
    Mutex *ret = (Mutex*)xaAlloc(sizeof(Mutex), XA_ZERO);
    InitializeCriticalSectionAndSpinCount(&ret->cs, 5000);
    return ret;
}

bool mutexAcquire(Mutex *m)
{
    EnterCriticalSection(&m->cs);
    return true;
}

bool mutexTryAcquire(Mutex *m)
{
    return TryEnterCriticalSection(&m->cs);
}

bool mutexRelease(Mutex *m)
{
    LeaveCriticalSection(&m->cs);
    return true;
}

void mutexDestroy(Mutex *m)
{
    DeleteCriticalSection(&m->cs);
    xaFree(m);
}

#endif
