#pragma once

#include <cx/cx.h>
#include <cx/core/stvar.h>
#include <cx/platform/base.h>
#include <cx/thread/atomic.h>

// args is an sarray
typedef struct Thread Thread;
typedef int(*threadFunc)(Thread *self);

// not the entire structure; platform specific data may be after it
typedef struct Thread {
    threadFunc entry;
    stvlist args;
    stvar *_argsa;          // sarray, should use the stvlist instead where possible

    atomic(bool) running;
    atomic(bool) requestExit;
} Thread;

// trick to get an opaque pointer-sized identifier that synchronization primitives
// can use to uniquely identify a thread (inlcuding ones created by native methods)
extern _Thread_local uintptr _thrCurrentHelper;
#define thrCurrent() ((uintptr)&_thrCurrentHelper)

Thread* _thrCreate(threadFunc func, int n, stvar args[]);
#define thrCreate(func, ...) _thrCreate(func, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

_meta_inline bool thrRunning(Thread *thread)
{
    return atomicLoad(bool, &thread->running, Acquire);
}

bool thrRequestExit(Thread *thread);
bool thrWait(Thread *thread, int64 timeout);
void thrDestroy(Thread **thread);
