#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>

_EXTERN_C_BEGIN

typedef struct RWLock RWLock;

RWLock *rwlockCreate();
bool rwlockAcquireRead(RWLock *m);
bool rwlockAcquireWrite(RWLock *m);
bool rwlockTryAcquireRead(RWLock *m);
bool rwlockTryAcquireWrite(RWLock *m);
bool rwlockReleaseRead(RWLock *m);
bool rwlockReleaseWrite(RWLock *m);
void rwlockDestroy(RWLock *m);

_EXTERN_C_END
