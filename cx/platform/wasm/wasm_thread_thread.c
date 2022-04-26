#include <cx/thread.h>
#define _GNU_SOURCE
#define __USE_GNU
#include <pthread.h>
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

// WebAssembly doesn't support thread priority at all
bool _thrPlatformSetPriority(Thread *thread, int prio) {
    return false;
}
