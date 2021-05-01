#pragma once

#include <cx/cx.h>
#include <cx/core/stvar.h>
#include <cx/platform/base.h>
#include <cx/thread/atomic.h>
#include <cx/container/sarray.h>

// args is an sarray
typedef struct Thread Thread;
typedef int(*threadFunc)(Thread *self);

// not the entire structure; platform specific data may be after it
typedef struct Thread {
    threadFunc entry;
    stvlist args;
    sa_stvar _argsa;            // should use the stvlist instead where possible

    atomic(bool) running;
    atomic(bool) requestExit;
} Thread;

enum ThreadPriority {
    THREAD_Normal = 0,
    THREAD_Batch,
    THREAD_Low,
    THREAD_Idle,
    THREAD_High,
    THREAD_Higher,
    THREAD_Realtime
};

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
bool _thrPlatformSetPriority(Thread *thread, int prio);
#define thrSetPriority(thread, prio) _thrPlatformSetPriority(thread, THREAD_##prio)
#define thrSetPriorityV(thread, prio) _thrPlatformSetPriority(thread, prio)

typedef struct Event Event;
// Registers a thread as a system thread. System threads are background threads that run independently
// of the main program and are typically library-created. They are notified at program exit and given
// an opportunity to perform cleanup before being destroyed.
void thrRegisterSysThread(Thread *thread, Event **notify_out);

// stype glue for custom type
saDeclarePtr(Thread);
extern STypeOps _thread_ops;
#define SType_Thread Thread*
#define STStorageType_Thread Thread*
#define stType_Thread _stype_mkcustom(stType_ptr)
#define stFullType_Thread _stype_mkcustom(stType_ptr), (&_thread_ops)
#define STypeCheck_Thread(type, val) ((val) && &((val)->requestExit), (val))
#define STypeCheckPtr_Thread(type, val) ((val) && (*val) && &((*val)->requestExit), (val))
#define STypeArg_Thread(type, val) stgeneric_unchecked(ptr, stCheck(Thread, val))
#define STypeArgPtr_Thread(type, val) (stgeneric*)stCheckPtr(Thread, val)
#define STypeCheckedArg_Thread(type, val) stType_ptr, stArg(type, val)
#define STypeCheckedPtrArg_Thread(type, val) stType_ptr, stArgPtr(type, val)
