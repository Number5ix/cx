#include "thread_private.h"
#include <cx/container/sarray.h>
#include <cx/time/time.h>

_Thread_local uintptr _thrCurrentHelper;
#define THREAD_DESTORY_TIMEOUT timeFromSeconds(30)

Thread* _thrCreate(threadFunc func, int n, stvar args[])
{
    Thread *ret = _thrPlatformAlloc();
    if (!ret)
        return NULL;

    ret->entry = func;
    ret->args = saCreate(stvar, 1);
    for (int i = 0; i < n; i++) {
        saPush(&ret->args, stvar, args[i]);
    }

    atomicStore(bool, &ret->running, true, Relaxed);
    if (!_thrPlatformStart(ret)) {
        _thrDestroy(ret);
        return NULL;
    }

    return ret;
}

void _thrDestroy(Thread *thread)
{
    _thrPlatformDestroy(thread);
    saDestroy(&thread->args);
    xaFree(thread);
}

bool thrWait(Thread *thread, int64 timeout)
{
    if (!atomicLoad(bool, &thread->running, Acquire))
        return true;

    bool ret = _thrPlatformWait(thread, timeout);

    if (ret) {
        devAssert(!atomicLoad(bool, &thread->running, Acquire));
    }

    return ret;
}

void thrDestroy(Thread **thread)
{
    if (!*thread)
        return;

    if (atomicLoad(bool, &(*thread)->running, Acquire)) {
        thrRequestExit(*thread);
        if (!thrWait(*thread, THREAD_DESTORY_TIMEOUT))
            _thrPlatformKill(*thread);
    }

    // UNIX pthreads needs to clean up exited threads by joining (waiting on) them
    _thrPlatformWait(*thread, THREAD_DESTORY_TIMEOUT);
    _thrDestroy(*thread);
    *thread = NULL;
}

bool thrRequestExit(Thread *thread)
{
    return atomicExchange(bool, &thread->requestExit, true, Release);
}
