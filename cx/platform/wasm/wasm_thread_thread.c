#include <cx/thread.h>
#if defined(_PLATFORM_FBSD)
#include <pthread.h>
#include <pthread_np.h>
#elif defined(_PLATFORM_LINUX)
#define _GNU_SOURCE
#define __USE_GNU
#include <pthread.h>
#endif
#include <sched.h>

typedef struct UnixThread {
    Thread base;

    pthread_t pthr;
    bool joined;
} UnixThread;

static void _thrCancelCleanup(void *data)
{
    UnixThread *thr = (UnixThread*)data;
    atomicStore(bool, &thr->base.running, false, Release);
}

static void* _thrEntryPoint(void *data)
{
    UnixThread *thr = (UnixThread*)data;

    pthread_cleanup_push(_thrCancelCleanup, thr);
    thr->base.entry(&thr->base);
    pthread_cleanup_pop(true);

    pthread_exit(NULL);
    return NULL;
}

Thread *_thrPlatformAlloc() {
    return xaAlloc(sizeof(UnixThread), XA_Zero);
}

bool _thrPlatformStart(Thread *thread)
{
    UnixThread *thr = (UnixThread*)thread;

    if (thr->pthr)
        return 0;

    bool ret = !pthread_create(&thr->pthr, NULL, _thrEntryPoint, thr);
    // pthreads has annoying limitations so it may be necessary to adjust
    // thready priority even for "Normal" threads as a result...
    if (ret)
        _thrPlatformSetPriority(thread, THREAD_Normal);
    return ret;
}

void _thrPlatformDestroy(Thread *thread)
{
    // cleanup happens in _thrPlatformWait on UNIX
}

bool _thrPlatformKill(Thread *thread)
{
    UnixThread *thr = (UnixThread*)thread;
    return !pthread_cancel(thr->pthr);
}

bool _thrPlatformWait(Thread *thread, int64 timeout)
{
    UnixThread *thr = (UnixThread*)thread;
    // some pthreads implementations will crash if you try to join
    // a thread that's already been joined
    if (thr->joined)
        return true;

    if (timeout == timeForever)
        thr->joined = !pthread_join(thr->pthr, NULL);
    else {
        struct timespec ts;
	void *unused;
        timeToRelTimespec(&ts, timeout);
        thr->joined = !pthread_timedjoin_np(thr->pthr, &unused, &ts);
    }
    return thr->joined;
}

#ifdef _PLATFORM_LINUX
// Linux pthreads sucks because it doesn't have any higher-than-normal
// priority levels other than realtime. It can do per-thread nice (maybe,
// seems to depend on kernel version and particular implementation) which
// would work except that there isn't a way to set it for anthing other
// than the current thread. That makes things awkward and doesn't fit well
// with our API. For now we just don't support THREAD_High or Higher.
bool _thrPlatformSetPriority(Thread *thread, int prio) {
    UnixThread *thr = (UnixThread*)thread;

    struct sched_param param = {0};
    int policy = SCHED_OTHER;

    switch(prio) {
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
            policy = SCHED_RR;
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
bool _thrPlatformSetPriority(Thread *thread, int prio) {
    UnixThread *thr = (UnixThread*)thread;

    struct sched_param param = {0};
    int policy = SCHED_OTHER;
    int maxprio = sched_get_priority_max(SCHED_OTHER);

    switch(prio) {
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
            policy = SCHED_RR;
            param.sched_priority = 1;
            break;
    }

    return !pthread_setschedparam(thr->pthr, policy, &param);
}
#endif
