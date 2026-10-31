#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include <cx/string.h>
#include <cx/thread.h>
#include <cx/utils/lazyinit.h>
#include <unistd.h>
#if defined(_PLATFORM_FBSD)
#include <sys/thr.h>
#include <pthread.h>
#include <pthread_np.h>
#elif defined(_PLATFORM_LINUX)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <sys/syscall.h>
#include <pthread.h>
#elif defined(_PLATFORM_MACOS)
#include <pthread.h>
#include <cx/platform/os.h>
#include <cx/time/clock.h>
#include <cx/utils.h>
#endif
#include <sched.h>
#include "cx/platform/unix/unix_thread_threadobj.h"

#include "cx/thread/tlscleanup_private.h"

STR_CONST(kMainThreadName, "Main");

static int getThreadId()
{
#if defined(_PLATFORM_LINUX)
    return syscall(SYS_gettid);
#elif defined(_PLATFORM_FBSD)
    long tid;
    thr_self(&tid);
    return (int)tid;
#elif defined(_PLATFORM_MACOS)
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    return (int)tid;
#else
    return getpid();
#endif
}

extern void _cx_tls_cleanup_init(void);

static UnixThread* mainthread;
static _Thread_local UnixThread* curthread;

static LazyInitState platformThreadInitState;
static void platformThreadInit(void* dummy)
{
    // synthesize a Thread structure for the main thread, so thrCurrent can work

    // We assume that the first thread that creates another thread is the main thread.
    // This may not be a correct assumption, but is the best we can do consistently
    // across all platforms without shenanigans.

    mainthread = _unixthrobjCreate();

    mainthread->pthr = pthread_self();
    mainthread->id   = getThreadId();
    strDup(&mainthread->name, kMainThreadName);
    atomicStore(bool, &mainthread->running, true, Relaxed);

    curthread = mainthread;
}

static void _thrCancelCleanup(void* data)
{
    UnixThread* thr = (UnixThread*)data;
    atomicStore(bool, &thr->running, false, Release);
}

static void* _thrEntryPoint(void* data)
{
    UnixThread* thr = (UnixThread*)data;
    if (!thr)
        goto out;

    thr->id   = getThreadId();
    curthread = thr;

    pthread_cleanup_push(_thrCancelCleanup, thr);
#if defined(_PLATFORM_MACOS)
    pthread_setname_np(strC(thr->name));   // can only name the calling thread
#else
    pthread_setname_np(thr->pthr, strC(thr->name));
#endif

    thr->exitCode = thr->entry(Thread(thr));
    saClear(&thr->_argsa);
    thr->args.count = 0;

    pthread_cleanup_pop(true);   // calls _thrCancelCleanup
    objRelease(&thr);

out:
    pthread_exit(NULL);
    return NULL;
}

Thread* _thrPlatformCreate()
{
    UnixThread* uthr = _unixthrobjCreate();
    return Thread(uthr);
}

bool _thrPlatformStart(Thread* thread)
{
    lazyInit(&platformThreadInitState, &platformThreadInit, NULL);

    UnixThread* thr = objDynCast(UnixThread, thread);

    if (!thr || thr->pthr)
        return 0;

    // Temporarily increment the reference count of the thread object. This is to work around a race
    // condition that can happen if the thread terminates and releases the only reference (e.g. in
    // the thrRun case) before we can call _thrPlatformSetPriority below.
    objAcquire(thr);
#if defined(_PLATFORM_MACOS)
    // Secondary threads get only 512 KB of stack by default here, against 8 MB on Linux. Match
    // Linux so code that runs fine there doesn't overflow here. It is reserved, not committed.
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
    bool ret = !pthread_create(&thr->pthr, &attr, _thrEntryPoint, thr);
    pthread_attr_destroy(&attr);
    // Not adjusted for Normal: setting a scheduling priority explicitly would take the thread out
    // of the default QoS class the scheduler uses to choose between performance and efficiency
    // cores.
#else
    bool ret = !pthread_create(&thr->pthr, NULL, _thrEntryPoint, thr);
    // pthreads has annoying limitations so it may be necessary to adjust
    // thready priority even for "Normal" threads as a result...
    if (ret)
        _thrPlatformSetPriority(thread, THREAD_Normal);
#endif
    objRelease(&thr);
    return ret;
}

bool _thrPlatformWait(Thread* thread, int64 timeout)
{
    UnixThread* thr = objDynCast(UnixThread, thread);
    if (!thr)
        return false;

    // some pthreads implementations will crash if you try to join
    // a thread that's already been joined
    if (thr->joined)
        return true;

    if (timeout == timeForever)
        thr->joined = !pthread_join(thr->pthr, NULL);
#if defined(_PLATFORM_MACOS)
    else {
        // There is no pthread_timedjoin_np. 'running' clears in the thread's cleanup handler, just
        // before it exits, so wait for that and then join; the join only waits out the last few
        // instructions of the thread.
        int64 now      = clockTimer();
        int64 deadline = (timeout > timeForever - now) ? timeForever : now + timeout;
        int64 nap      = 100;   // microseconds
        while (atomicLoad(bool, &thr->running, Acquire)) {
            now = clockTimer();
            if (now >= deadline)
                return false;
            osSleep(min(nap, deadline - now));
            nap = min(nap * 2, timeMS(10));
        }
        thr->joined = !pthread_join(thr->pthr, NULL);
    }
#else
    else {
        struct timespec ts;
        void* unused;
        timeToAbsTimespec(&ts, clockWall() + timeout);
        thr->joined = !pthread_timedjoin_np(thr->pthr, &unused, &ts);
    }
#endif
    return thr->joined;
}

#ifdef _PLATFORM_LINUX
// Linux pthreads sucks because it doesn't have any higher-than-normal
// priority levels other than realtime. It can do per-thread nice (maybe,
// seems to depend on kernel version and particular implementation) which
// would work except that there isn't a way to set it for anything other
// than the current thread. That makes things awkward and doesn't fit well
// with our API. For now we just don't support THREAD_High or Higher.
bool _thrPlatformSetPriority(Thread* thread, int prio)
{
    UnixThread* thr = objDynCast(UnixThread, thread);
    if (!thr)
        return false;

    struct sched_param param = { 0 };
    int policy               = SCHED_OTHER;

    switch (prio) {
    case THREAD_Normal:
        policy = SCHED_OTHER;
        break;
    case THREAD_Batch:
        policy = SCHED_BATCH;
        break;
    case THREAD_Low:
        policy = SCHED_IDLE;
        break;
    case THREAD_Idle:
        policy = SCHED_IDLE;
        break;
    case THREAD_High:
        policy = SCHED_OTHER;
        break;
    case THREAD_Higher:
        policy = SCHED_OTHER;
        break;
    case THREAD_Realtime:
        policy               = SCHED_RR;
        param.sched_priority = 1;
        break;
    }

    return !pthread_setschedparam(thr->pthr, policy, &param);
}
#endif

#ifdef _PLATFORM_FBSD
// FreeBSD pthreads sucks because it doesn't have any *lower*-than-normal
// priority levels. It does support priority levels on SCHED_OTHER though,
// so we can sort of fake it with that.
bool _thrPlatformSetPriority(Thread* thread, int prio)
{
    UnixThread* thr = objDynCast(UnixThread, thread);
    if (!thr)
        return false;

    struct sched_param param = { 0 };
    int policy               = SCHED_OTHER;
    int maxprio              = sched_get_priority_max(SCHED_OTHER);

    switch (prio) {
    case THREAD_Normal:
        param.sched_priority = maxprio / 2;
        break;
    case THREAD_Batch:
        param.sched_priority = maxprio / 2 - 1;
        break;
    case THREAD_Low:
        param.sched_priority = maxprio / 4;
        break;
    case THREAD_Idle:
        param.sched_priority = sched_get_priority_min(SCHED_OTHER);
        break;
    case THREAD_High:
        param.sched_priority = maxprio / 4 * 3;
        break;
    case THREAD_Higher:
        param.sched_priority = maxprio;
        break;
    case THREAD_Realtime:
        policy               = SCHED_RR;
        param.sched_priority = 1;
        break;
    }

    return !pthread_setschedparam(thr->pthr, policy, &param);
}
#endif

#ifdef _PLATFORM_MACOS
// SCHED_OTHER priorities here run from 15 to 47, with 31 the default every thread starts at.
bool _thrPlatformSetPriority(Thread* thread, int prio)
{
    UnixThread* thr = objDynCast(UnixThread, thread);
    if (!thr)
        return false;

    struct sched_param param = { 0 };
    int policy               = SCHED_OTHER;
    int minprio              = sched_get_priority_min(SCHED_OTHER);
    int maxprio              = sched_get_priority_max(SCHED_OTHER);
    int normal               = (minprio + maxprio) / 2;

    switch (prio) {
    case THREAD_Normal:
        param.sched_priority = normal;
        break;
    case THREAD_Batch:
        param.sched_priority = normal - 4;
        break;
    case THREAD_Low:
        param.sched_priority = (minprio + normal) / 2;
        break;
    case THREAD_Idle:
        param.sched_priority = minprio;
        break;
    case THREAD_High:
        param.sched_priority = (normal + maxprio) / 2;
        break;
    case THREAD_Higher:
        param.sched_priority = maxprio;
        break;
    case THREAD_Realtime:
        policy               = SCHED_RR;
        param.sched_priority = sched_get_priority_max(SCHED_RR);
        break;
    }

    return !pthread_setschedparam(thr->pthr, policy, &param);
}
#endif

Thread* thrCurrent(void)
{
    return Thread(curthread);
}

intptr thrOSThreadID(Thread* thread)
{
    UnixThread* thr = objDynCast(UnixThread, thread);
    if (!thr)
        return 0;

    return thr->id;
}

intptr thrCurrentOSThreadID(void)
{
    return getThreadId();
}
