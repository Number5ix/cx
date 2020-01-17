#include <cx/thread.h>
#include <pthread.h>
#include <pthread_np.h>

typedef struct UnixThread {
    Thread base;

    pthread_t pthr;
} UnixThread;

static void _thrCancelCleanup(void *data)
{
    UnixThread *thr = (UnixThread*)data;
    atomic_store_bool(&thr->base.running, false, ATOMIC_RELEASE);
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
    return xaAlloc(sizeof(UnixThread), Zero);
}

bool _thrPlatformStart(Thread *thread)
{
    UnixThread *thr = (UnixThread*)thread;

    if (thr->pthr)
        return 0;

    return !pthread_create(&thr->pthr, NULL, _thrEntryPoint, thr);
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

    if (timeout == timeForever)
        return !pthread_join(thr->pthr, NULL);
    else {
        struct timespec ts;
        timeToRelTimespec(&ts, timeout);
        return !pthread_timedjoin_np(thr->pthr, NULL, &ts);
    }
}
