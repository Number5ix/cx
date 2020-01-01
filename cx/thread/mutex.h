#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>

_EXTERN_C_BEGIN

typedef struct Mutex Mutex;

Mutex *mutexCreate();
bool mutexAcquire(Mutex *m);
bool mutexTryAcquire(Mutex *m);
bool mutexRelease(Mutex *m);
void mutexDestroy(Mutex *m);

_EXTERN_C_END
