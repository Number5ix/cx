#pragma once

#include <cx/cx.h>
#include "futex.h"
#include "aspin.h"
#include "mutex.h"

// Condition variable: A cross between an event and a mutex

enum CONDVAR_Flags {
    CONDVAR_NoSpin = 1,     // do not use adaptive spin, use kernel futex only
};

typedef struct CondVar {
    Futex seq;
    atomic(uint32) lastseq;
    AdaptiveSpin aspin;
} CondVar;

bool _cvarInit(CondVar *cv, uint32 flags);
#define cvarInit(cv, ...) _cvarInit(cv, opt_flags(__VA_ARGS__))

void cvarDestroy(CondVar *cv);
bool cvarWaitTimeout(CondVar *cv, Mutex *m, int64 timeout);
_meta_inline bool cvarWait(CondVar *cv, Mutex *m)
{
    return cvarWaitTimeout(cv, m, timeForever);
}
bool cvarSignal(CondVar *cv);
bool cvarBroadcast(CondVar *cv);
