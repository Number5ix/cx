#include "workthreadold.h"
#include "cx/debug/assert.h"
#include <process.h>

static _Thread_local WorkThread *cur_thread = 0;

static _Thread_local HWND hDialog = 0;
static CRITICAL_SECTION cstl;
size_t szThreadList = 0;
static WorkThread **threadList = 0;

typedef struct ThreadCreateInfo {
    ThreadProc proc;
    WorkThread *thread;
    bool suspend;
} ThreadCreateInfo;

static inline size_t findHandlerForInsert(ThreadNotifyHandler *h, size_t nmax, uint32_t id)
{
    size_t nmin = 0, nmid;
    while (nmin < nmax) {               // specialized binary search
        nmid = (nmin + nmax) / 2;
        if (h[nmid].id < id)
            nmin = nmid + 1;
        else
            nmax = nmid;
    }
    return nmin;
}

static inline ThreadNotifyHandler *findHandler(ThreadNotifyHandler *h, size_t count, uint32_t id)
{
    size_t nmin = 0, nmid, nmax = count;

    while (nmin < nmax) {               // specialized binary search
        nmid = (nmin + nmax) / 2;
        if (h[nmid].id < id)
            nmin = nmid + 1;
        else
            nmax = nmid;
    }
    if (nmin == count || h[nmin].id != id)
        return NULL;
    return &h[nmin];
}

static void registerHandlerInternal(WorkThread *thr, uint32_t id, ThreadNotifyCallback cb)
{
    size_t pos = findHandlerForInsert(thr->handlers, thr->nHandlers, id);
    if (thr->nHandlers && thr->handlers[pos].id == id) {
        // handlers must be unique, overwrite!
        thr->handlers[pos].callback = cb;
        return;
    }

    if (thr->szHandlers == thr->nHandlers) {
        thr->szHandlers += thr->szHandlers / 2;
        thr->handlers = xa_realloc(thr->handlers, thr->szHandlers * sizeof(ThreadNotifyHandler));
    }
    if (pos != thr->nHandlers)
        memmove(&thr->handlers[pos + 1], &thr->handlers[pos], (thr->nHandlers - pos) * sizeof(ThreadNotifyHandler));
    thr->handlers[pos].id = id;
    thr->handlers[pos].callback = cb;
    thr->nHandlers++;
}

static WorkThread *workThreadCreate()
{
    WorkThread *thr = xa_calloc(1, sizeof(WorkThread));

    thr->szHandlers = 32;
    thr->handlers = xa_calloc(1, thr->szHandlers * sizeof(ThreadNotifyHandler));

    thr->szQueue = 32;
    thr->queue = xa_calloc(1, thr->szQueue * sizeof(ThreadNotify));

    InitializeCriticalSectionAndSpinCount(&thr->cs, 5000);
    thr->hNotify = CreateEvent(NULL, TRUE, FALSE, NULL);

    EnterCriticalSection(&cstl);
    threadList = xa_realloc(threadList, (szThreadList + 1) * sizeof(WorkThread*));
    threadList[szThreadList++] = thr;
    LeaveCriticalSection(&cstl);

    return thr;
}

static void workThreadDestroy(WorkThread *thr)
{
    size_t i;

    EnterCriticalSection(&cstl);
    for (i = 0; i < szThreadList; i++) {
        if (threadList[i] == thr)
            break;
    }
    if (i < szThreadList) {
        memmove(&threadList[i], &threadList[i + 1], (szThreadList - i - 1) * sizeof(WorkThread*));
        szThreadList--;
    }
    LeaveCriticalSection(&cstl);

    DeleteCriticalSection(&thr->cs);
    CloseHandle(thr->hNotify);
    CloseHandle(thr->hThread);
    xa_free(thr->handlers);
    xa_free(thr->queue);
    xa_free(thr);
}

static int defaultShutdownHandler(WorkThread *from, uint32_t id, uint32_t param, void *data)
{
    // just exit the thread
    return THREAD_SHOULD_EXIT;
}

static unsigned __stdcall workThreadProc(void *pvtci)
{
    ThreadCreateInfo *tci = (ThreadCreateInfo*)pvtci;
    int ret = 0;

    cur_thread = tci->thread;

    if (tci->suspend)
        SuspendThread(tci->thread->hThread);

    if (tci->proc) {
        ret = tci->proc(tci->thread);
        tci->thread->shutdown = true;
    } else {
        // no main thread procedure provided, just process notifications
        while (owthrHandleAll())
            owthrWait(-1);
    }

    xa_free(tci);
    _endthreadex(ret);
    return ret;
}

WorkThread *owthrCreateEx(ThreadProc proc, ThreadNotifyHandler *handlers, bool suspended,
                        uint32_t param, void *data, MemoryDestructor dtor)
{
    WorkThread *thr = workThreadCreate();
    ThreadCreateInfo *tci = xa_calloc(1, sizeof(ThreadCreateInfo));
    ThreadNotifyHandler *hp = handlers;

    tci->proc = proc;
    tci->suspend = suspended;
    tci->thread = thr;

    // register a default shutdown handler, the table can override if it wants
    registerHandlerInternal(thr, NGLOBAL_SHUTDOWN, defaultShutdownHandler);
    while (hp && hp->id) {
        registerHandlerInternal(thr, hp->id, hp->callback);
        ++hp;
    }

    // statically populate initial create event
    thr->queue[0].from = cur_thread;
    thr->queue[0].id = NGLOBAL_CREATE;
    thr->queue[0].param = param;
    thr->queue[0].data = data;
    thr->queue[0].dtor = dtor;
    thr->nQueue = 1;
    SetEvent(thr->hNotify);

    thr->hThread = (HANDLE)_beginthreadex(NULL, 0, workThreadProc, tci, 0, 0);

    return thr;
}

WorkThread *owthrCreate(ThreadProc proc, ThreadNotifyHandler *handlers)
{
    return owthrCreateEx(proc, handlers, false, 0, NULL, NULL);
}

void owthrDestroy(WorkThread *thr)
{
    // try to let it shut down first if possible
    if (!thr->shutdown)
        owthrNotify(thr, NGLOBAL_SHUTDOWN, 0);

    // Make sure the thread has exited
    if (WaitForSingleObject(thr->hThread, 10000) == WAIT_TIMEOUT)
        TerminateThread(thr->hThread, 0);           // well crap

    workThreadDestroy(thr);
}

bool owthrNotifyEx(WorkThread *dest, uint32_t id, uint32_t param, void *data, MemoryDestructor dtor)
{
    ThreadNotify *pq;

    if (!dest) {
        size_t i;
        // broadcast to all threads
        devAssert(dtor == NULL);            // dtor on a broadcast would be stupid
        EnterCriticalSection(&cstl);
        for (i = 0; i < szThreadList; i++) {
            if (threadList[i] != cur_thread)        // don't send to self
                owthrNotifyEx(threadList[i], id, param, data, NULL);
        }
        LeaveCriticalSection(&cstl);
        return true;
    }

    if (dest->shutdown)
        return false;

    EnterCriticalSection(&dest->cs);
    if (dest->szQueue == dest->nQueue) {
        dest->szQueue += dest->szQueue / 2;
        dest->queue = xa_realloc(dest->queue, dest->szQueue * sizeof(ThreadNotify));
    }
    pq = &dest->queue[dest->nQueue++];
    pq->from = cur_thread;
    pq->id = id;
    pq->param = param;
    pq->data = data;
    pq->dtor = dtor;
    pq->rcall = NULL;
    SetEvent(dest->hNotify);
    LeaveCriticalSection(&dest->cs);

    return true;
}

bool owthrNotify(WorkThread *dest, uint32_t id, uint32_t param)
{
    return owthrNotifyEx(dest, id, param, NULL, NULL);
}

bool owthrRemoteCall(WorkThread *dest, ThreadRemoteCall func, uint32_t param, void *data, MemoryDestructor dtor)
{
    ThreadNotify *pq;

    if (!dest || dest->shutdown)
        return false;

    EnterCriticalSection(&dest->cs);
    if (dest->szQueue == dest->nQueue) {
        dest->szQueue += dest->szQueue / 2;
        dest->queue = xa_realloc(dest->queue, dest->szQueue * sizeof(ThreadNotify));
    }
    pq = &dest->queue[dest->nQueue++];
    pq->from = cur_thread;
    pq->id = NGLOBAL_REMOTECALL;
    pq->param = param;
    pq->data = data;
    pq->dtor = dtor;
    pq->rcall = func;
    SetEvent(dest->hNotify);
    LeaveCriticalSection(&dest->cs);

    return true;
}

WorkThread *owthrCurrent()
{
    return cur_thread;
}

bool owthrRegisterHandler(uint32_t id, ThreadNotifyCallback cb)
{
    registerHandlerInternal(cur_thread, id, cb);
    return true;
}

static void runWinQueue(WorkThread *thr)
{
    MSG msg, *msgcopy;

    // keep the windows message queue going
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            owthrNotify(thr, NGLOBAL_WMQUIT, (uint32_t)msg.wParam);
        } else if (msg.hwnd == NULL) {
            msgcopy = xa_malloc(sizeof(MSG));
            memcpy(msgcopy, &msg, sizeof(MSG));
            owthrNotifyEx(thr, NGLOBAL_WMTHREAD, msg.message, msgcopy, xa_free);
        } else {
            if (!hDialog || !IsDialogMessage(hDialog, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
}

HWND owthrSetDialog(HWND dialog)
{
    HWND old = hDialog;
    hDialog = dialog;
    return old;
}

bool owthrWait(int32_t ms)
{
    WorkThread *thr = cur_thread;
    HANDLE *hptr = &thr->hNotify;
    DWORD wait = ms;
    bool ret = true;

    if (ms == -1)
        wait = INFINITE;

    if (MsgWaitForMultipleObjectsEx(1, hptr, ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_TIMEOUT)
        return false;

    return true;
}

static bool handleEventsInternal(WorkThread *thr, uint32_t lookfor)
{
    bool ret = false;
    ThreadNotify *nc = 0, *ncp;
    ThreadNotifyHandler *h;
    size_t i, n;

    // copy the queue so lock can be released
    EnterCriticalSection(&thr->cs);
    n = thr->nQueue;
    if (n > 0) {
        nc = xa_malloc(n * sizeof(ThreadNotify));
        memcpy(nc, thr->queue, n * sizeof(ThreadNotify));
        thr->nQueue = 0;
    }
    ResetEvent(thr->hNotify);
    LeaveCriticalSection(&thr->cs);

    for (i = 0, ncp = nc; i < n; ++i, ++ncp) {
        if (ncp->id == lookfor)
            ret = true;

        if (!ncp->rcall) {
            h = findHandler(thr->handlers, thr->nHandlers, ncp->id);
            if (h && h->callback(ncp->from, ncp->id, ncp->param, ncp->data) == THREAD_SHOULD_EXIT)
                thr->shutdown = true;
        } else {
            ncp->rcall(ncp->from, ncp->param, ncp->data);
        }

        if (ncp->dtor && ncp->data) {
            ncp->dtor(ncp->data);
            ncp->data = NULL;
        }
    }

    // let the Windows message queue have a go at it
    runWinQueue(thr);

    return ret;
}

bool owthrHandleAll()
{
    WorkThread *thr = cur_thread;
    if (thr->shutdown)
        return false;
    handleEventsInternal(thr, 0);
    return !thr->shutdown;
}

bool owthrWaitFor(uint32_t id, int32_t ms)
{
    WorkThread *thr = cur_thread;
    DWORD start = GetTickCount(), cur, elapsed;

    if (thr->shutdown)
        return false;

    do {
        if (handleEventsInternal(cur_thread, id))
            return true;
        if (thr->shutdown)
            return false;

        owthrWait(ms);                // this does Win32 message loop processing too

        if (ms > 0) {
            cur = GetTickCount();
            if (cur > start)
                elapsed = cur - start;
            else
                elapsed = cur + (UINT_MAX - start) + 1;     // wraparound
            start = cur;
            ms = max(0, ms - elapsed);
        }
    } while (ms == -1 || ms > 0);

    return false;                   // timed out
}

bool owthrShutdown(int32_t ms, bool force)
{
    HANDLE *hlist;
    bool ret = true;
    DWORD wait = ms;
    size_t i, n;

    // give threads a chance to shut down gracefully
    owthrNotify(NULL, NGLOBAL_SHUTDOWN, 0);

    if (ms == -1)               // TODO: warn because it's a bad idea to wait
        wait = INFINITE;        // forever on this?

    EnterCriticalSection(&cstl);
    hlist = xa_malloc(szThreadList * sizeof(HANDLE));
    for (i = 0, n = 0; i < szThreadList; i++)
        if (threadList[i] != cur_thread) {
            hlist[n++] = threadList[i]->hThread;
        }
    LeaveCriticalSection(&cstl);
    WaitForMultipleObjects((DWORD)n, hlist, TRUE, wait);

    // destroy any threads that terminated
    EnterCriticalSection(&cstl);
    for (i = szThreadList - 1; i != -1; --i) {
        if (threadList[i] == cur_thread)            // can't shut down self
            continue;

        if (WaitForSingleObject(threadList[i]->hThread, 0) == WAIT_OBJECT_0) {
            workThreadDestroy(threadList[i]);       // it's okay, we're going backwards
        } else if (force) {
            TerminateThread(threadList[i]->hThread, 0);
            workThreadDestroy(threadList[i]);
        } else {
            ret = false;
        }
    }
    LeaveCriticalSection(&cstl);

    return ret;
}

WorkThread *initMasterThread(ThreadNotifyHandler *handlers)
{
    WorkThread *thr;
    ThreadNotifyHandler *hp = handlers;

    // must init critical section before calling workThreadCreate!
    InitializeCriticalSectionAndSpinCount(&cstl, 5000);
    thr = workThreadCreate();

    while (hp && hp->id) {
        registerHandlerInternal(thr, hp->id, hp->callback);
        ++hp;
    }

    thr->hThread = GetCurrentThread();
    cur_thread = thr;
    return thr;
}
