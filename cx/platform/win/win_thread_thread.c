#include <cx/thread/thread_private.h>
#include <cx/platform/win.h>
#include <cx/time/time.h>
#include <process.h>

typedef struct WinThread {
    Thread base;
    bool background;

    HANDLE handle;
} WinThread;

static unsigned __stdcall _thrEntryPoint(void *data)
{
    WinThread *thr = (WinThread*)data;

    thr->base.entry(&thr->base);

    atomicStore(bool, &thr->base.running, false, AcqRel);
    _endthreadex(0);
    return 0;
}

Thread *_thrPlatformAlloc() {
    return xaAlloc(sizeof(WinThread), Zero);
}

bool _thrPlatformStart(Thread *thread)
{
    WinThread *thr = (WinThread*)thread;

    if (thr->handle)
        return false;

    thr->handle = (HANDLE)_beginthreadex(NULL, 0, _thrEntryPoint, thr, 0, NULL);
    return !!thr->handle;
}

void _thrPlatformDestroy(Thread *thread)
{
    WinThread *thr = (WinThread*)thread;
    CloseHandle(thr->handle);
}

bool _thrPlatformKill(Thread *thread)
{
    WinThread *thr = (WinThread*)thread;
    return TerminateThread(thr->handle, -1);
}

bool _thrPlatformWait(Thread *thread, int64 timeout)
{
    WinThread *thr = (WinThread*)thread;

    return WaitForSingleObject(thr->handle, (timeout == timeForever) ? INFINITE : (DWORD)timeToMsec(timeout)) == WAIT_OBJECT_0;
}

bool _thrPlatformSetPriority(Thread *thread, int prio)
{
    WinThread *thr = (WinThread*)thread;

    if (prio == THREAD_Batch) {
        // background mode only works on the current thread, so also adjust priority below
        SetThreadPriority(thr->handle, THREAD_MODE_BACKGROUND_BEGIN);
        thr->background = true;
    } else if (thr->background && prio) {
        SetThreadPriority(thr->handle, THREAD_MODE_BACKGROUND_END);
        thr->background = false;
    }

    int winprio = THREAD_PRIORITY_NORMAL;
    switch (prio) {
    case THREAD_Normal:
        winprio = THREAD_PRIORITY_NORMAL;
        break;
    case THREAD_Batch:
        winprio = THREAD_PRIORITY_BELOW_NORMAL;
        break;
    case THREAD_Low:
        winprio = THREAD_PRIORITY_LOWEST;
        break;
    case THREAD_Idle:
        winprio = THREAD_PRIORITY_IDLE;
        break;
    case THREAD_High:
        winprio = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
    case THREAD_Higher:
        winprio = THREAD_PRIORITY_HIGHEST;
        break;
    case THREAD_Realtime:
        winprio = THREAD_PRIORITY_TIME_CRITICAL;
        break;
    }

    return SetThreadPriority(thr->handle, winprio);
}
