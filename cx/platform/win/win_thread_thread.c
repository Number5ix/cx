#include <cx/thread/thread_private.h>
#include <cx/platform/win.h>
#include <cx/time/time.h>
#include <process.h>

typedef struct WinThread {
    Thread base;

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
