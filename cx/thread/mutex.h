#pragma once

#include <cx/cx.h>
#include <cx/platform/base.h>

CX_C_BEGIN

typedef struct Mutex Mutex;

Mutex *mutexCreate();
bool mutexAcquire(Mutex *m);
bool mutexTryAcquire(Mutex *m);
bool mutexRelease(Mutex *m);
void mutexDestroy(Mutex *m);

CX_C_END
