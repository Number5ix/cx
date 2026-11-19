#include "cxui_private.h"

#include <cx/sys.h>
#include <cxui/entry.h>
#include <cx/utils/lazyinit.h>

LogChannel* CxUiLogChannel;
CxUiRuntime _cxui;
cchain cxuiQuitRequestNotify;
cchain cxuiShutdownNotify;

#if !defined(CXUI_BACKEND_APPKIT)
_Use_decl_annotations_
int _cxuiEntryRunner(int (*entry)(void))
{
    return _entryRunDirect(entry);
}
#endif

enum CxUiState {
    UISTATE_Stopped,
    UISTATE_Running,
    UISTATE_Stopping,
};

// startLock serializes whole start and shutdown sequences, including the wait for the UI thread
// to exit. The UI thread never takes it: from there, start only counts a caller and shutdown is
// refused, so a shutdown waiting for the UI thread can't deadlock against it. stateLock is held
// only briefly and guards the fields below and _cxui.queue.
static Mutex startLock;
static Mutex stateLock;
static atomic(uint32) state;
static int32 starts;
static Thread* uiThread;
static atomic(intptr) uiThreadId;   // 0 when there is no UI thread
// A session hosted on the main thread signals this when it has torn down; cxuiShutdown waits on it.
static SharedEvent* hostedEnded;

#define CXUI_DEFAULT_TICK_BUDGET timeMS(8)

static LazyInitState initState;

static void cxuiInitOnce(void* unused)
{
    unused_noeval(unused);
    CxUiLogChannel = logChan(_SL("cx/ui"));
    mutexInit(&startLock);
    mutexInit(&stateLock);
    _cxuiWidgetInit();
}

static void cxuiInit(void)
{
    lazyInit(&initState, cxuiInitOnce, NULL);
}

static const CxUiBackend* cxuiPlatformBackend(void)
{
#if defined(CXUI_BACKEND_WIN32)
    return &_cxuiWin32Backend;
#elif defined(CXUI_BACKEND_GTK4)
    return &_cxuiGtk4Backend;
#elif defined(CXUI_BACKEND_APPKIT)
    return &_cxuiAppKitBackend;
#else
    return NULL;
#endif
}

static bool cxuiWantsNull(flags_t flags)
{
    if (flags & CXUI_Start_Null)
        return true;

    string env  = 0;
    bool isNull = envGet(&env, _SL("CXUI_BACKEND")) && strEqi(env, _SL("null"));
    strDestroy(&env);
    return isNull;
}

typedef struct CxUiStartSync {
    const CxUiBackend* backend;
    flags_t flags;
    bool ok;
} CxUiStartSync;

// UI thread, once its loop has ended.
static void sessionTeardown(void)
{
    // While everything still works, so listeners can take down what they added. The listeners
    // move to a local chain so the global one is ready for the next session's.
    cchain listeners = NULL;
    cchainTransfer(&listeners, &cxuiShutdownNotify);
    cchainReset(&cxuiShutdownNotify);
    _cxuiNativeEnter();
    cchainCallOnce(&listeners, stvNone);
    // Windows that closing would destroy are destroyed, so their widgets' destroyNotify is called.
    _cxuiWindowsEndSession();
    _cxuiInputEndSession();
    _cxuiNativeLeave();

    // Drop queued work here, so closures captured with UI objects are destroyed on the UI thread,
    // and before the backend that those objects may still refer to goes away. Peers go after the
    // queue: a queued teardown may still name one.
    tqShutdown(_cxui.queue, 0);
    _cxuiFlushDeferred();
    _cxuiPeersDestroyAll();
    _cxuiWindowsRelease();
    _cxui.capture = NULL;
    memset(&_cxui.keysDown, 0, sizeof(_cxui.keysDown));
    _cxui.backend->shutdown();
}

// On the UI thread: initializes the backend and tells the starting thread how it went.
static bool sessionBegin(CxUiStartSync* sync, SharedEvent* ready)
{
    atomicStore(intptr, &uiThreadId, thrCurrentOSThreadID(), Release);
    bool ok  = sync->backend->init(sync->flags);
    sync->ok = ok;
    if (!ok)
        atomicStore(intptr, &uiThreadId, 0, Release);
    // sync belongs to the starting thread and is gone once it wakes
    eventSignal(&ready->ev);
    sheventRelease(&ready);
    return ok;
}

static int cxuiThreadMain(Thread* self)
{
    bool ok = sessionBegin(stvlAtPtr(&self->args, 0), stvlAtPtr(&self->args, 1));
    if (ok) {
        _cxui.backend->runOuter(&_cxui.exitRequested);
        sessionTeardown();
        atomicStore(intptr, &uiThreadId, 0, Release);
    }
    return ok ? 0 : 1;
}

typedef struct CxUiHostedStart {
    CxUiStartSync* sync;
    SharedEvent* ready;
} CxUiHostedStart;

static void hostedBegin(void* ctx)
{
    CxUiHostedStart* hs = ctx;
    sessionBegin(hs->sync, hs->ready);
}

void _cxuiHostedEnd(void)
{
    sessionTeardown();
    atomicStore(intptr, &uiThreadId, 0, Release);
    // The waiting cxuiShutdown releases its reference as soon as it wakes.
    SharedEvent* ended = sheventAcquire(hostedEnded);
    eventSignal(&ended->ev);
    sheventRelease(&ended);
}

static bool cxuiStartWith(const CxUiBackend* backend, const CxUiStartDesc* desc, flags_t flags)
{
    _cxui.backend       = backend;
    _cxui.tickBudget    = (desc && desc->tickBudget > 0) ? desc->tickBudget : CXUI_DEFAULT_TICK_BUDGET;
    _cxui.dispatchDepth = 0;
    _cxui.session++;
    atomicStore(bool, &_cxui.exitRequested, false, Release);
    atomicStore(bool, &_cxui.wakePending, false, Release);
    atomicStore(uint64, &_cxui.ticks, 0, Relaxed);
    atomicStore(int64, &_cxui.nextWake, 0, Relaxed);

    TaskQueueConfig conf;
    tqPresetManual(&conf);
    conf.flags |= TQ_Oneshot;   // one task per tqTick, so _cxuiTick can stop at its budget
    conf.manual.wake    = _cxuiWakeHook;
    conf.manual.wakeCtx = (void*)backend;   // static, so a late wake from another thread is safe
    TaskQueue* queue    = tqCreate(_SL("cxui"), &conf);
    if (!queue || !tqStart(queue)) {
        logStr(Error, _SL("cxui couldn't start its UI queue"));
        if (queue)
            tqRelease(&queue);
        _cxui.backend = NULL;
        return false;
    }
    _cxui.queue = queue;

    CxUiStartSync sync = { .backend = backend, .flags = flags };
    SharedEvent* ready = sheventCreate(0);
    bool launched;
    if (backend->hostRun) {
        hostedEnded            = sheventCreate(0);
        CxUiHostedStart hstart = { .sync = &sync, .ready = sheventAcquire(ready) };
        launched               = backend->hostRun(hostedBegin, &hstart);
        if (launched)
            eventWait(&ready->ev);
        else
            sheventRelease(&hstart.ready);
    } else {
        strref name = (desc && !strEmpty(desc->name)) ? desc->name : _SL("UI");
        uiThread    = thrCreate(cxuiThreadMain, name, stvar(ptr, &sync), stvar(ptr, sheventAcquire(ready)));
        launched    = uiThread != NULL;
        if (launched)
            eventWait(&ready->ev);
    }
    sheventRelease(&ready);

    if (!launched || !sync.ok) {
        logFmt(Info, _SL("cxui couldn't start the ${string} backend"), stvar(strref, backend->name));
        if (uiThread) {
            thrWait(uiThread, timeForever);
            thrRelease(&uiThread);
        }
        sheventRelease(&hostedEnded);
        tqShutdown(queue, 0);
        _cxui.queue = NULL;
        tqRelease(&queue);
        _cxui.backend = NULL;
        return false;
    }

    mutexAcquire(&stateLock);
    starts = 1;
    atomicStore(uint32, &state, UISTATE_Running, Release);
    mutexRelease(&stateLock);
    return true;
}

static bool cxuiStartSession(const CxUiStartDesc* desc)
{
    flags_t flags              = desc ? desc->flags : 0;
    bool headless              = flags & CXUI_Start_AllowHeadless;
    const CxUiBackend* backend = cxuiWantsNull(flags) ? &_cxuiNullBackend : cxuiPlatformBackend();

    if (!backend && !headless) {
        logStr(Error, _SL("cxui has no backend for this platform; only CXUI_Start_Null is available"));
        return false;
    }
    if (backend && cxuiStartWith(backend, desc, flags))
        return true;
    if (!headless || backend == &_cxuiNullBackend) {
        logStr(Error, _SL("cxui couldn't start"));
        return false;
    }

    logStr(Info, _SL("cxui is running without a display"));
    return cxuiStartWith(&_cxuiNullBackend, desc, flags);
}

_Use_decl_annotations_
bool cxuiStart(const CxUiStartDesc* desc)
{
    cxuiInit();

    if (cxuiIsUIThread()) {
        // The UI is running, or this thread wouldn't be the UI thread. Only count the caller.
        mutexAcquire(&stateLock);
        bool running = atomicLoad(uint32, &state, Acquire) == UISTATE_Running;
        if (running)
            starts++;
        mutexRelease(&stateLock);
        return running;
    }

    mutexAcquire(&startLock);
    mutexAcquire(&stateLock);
    bool running = atomicLoad(uint32, &state, Acquire) == UISTATE_Running;
    if (running)
        starts++;
    mutexRelease(&stateLock);

    bool ok = running || cxuiStartSession(desc);
    mutexRelease(&startLock);
    return ok;
}

void cxuiShutdown(void)
{
    cxuiInit();

    if (cxuiIsUIThread()) {
        // Matching a start made from here is fine; stopping would mean waiting for this thread.
        mutexAcquire(&stateLock);
        bool counted = starts > 1;
        if (counted)
            starts--;
        mutexRelease(&stateLock);
        if (!counted) {
            devAssertMsg(false, "the last cxuiShutdown was called on the UI thread");
            logStr(Error, _SL("The cxuiShutdown that stops the UI can't be called on the UI thread"));
        }
        return;
    }

    mutexAcquire(&startLock);
    mutexAcquire(&stateLock);
    bool stop = atomicLoad(uint32, &state, Acquire) == UISTATE_Running && --starts == 0;
    if (stop)
        atomicStore(uint32, &state, UISTATE_Stopping, Release);
    mutexRelease(&stateLock);

    if (stop) {
        atomicStore(bool, &_cxui.exitRequested, true, Release);
        _cxui.backend->wake();
        if (hostedEnded) {
            eventWait(&hostedEnded->ev);
            sheventRelease(&hostedEnded);
        } else {
            thrWait(uiThread, timeForever);
            thrRelease(&uiThread);
        }

        mutexAcquire(&stateLock);
        TaskQueue* queue = _cxui.queue;
        _cxui.queue      = NULL;
        atomicStore(uint32, &state, UISTATE_Stopped, Release);
        mutexRelease(&stateLock);

        tqRelease(&queue);
        _cxui.backend = NULL;
    }
    mutexRelease(&startLock);
}

bool cxuiIsRunning(void)
{
    return atomicLoad(uint32, &state, Acquire) == UISTATE_Running;
}

bool cxuiIsUIThread(void)
{
    intptr id = atomicLoad(intptr, &uiThreadId, Acquire);
    return id != 0 && id == thrCurrentOSThreadID();
}

TaskQueue* cxuiQueue(void)
{
    cxuiInit();

    TaskQueue* queue = NULL;
    mutexAcquire(&stateLock);
    if (atomicLoad(uint32, &state, Acquire) == UISTATE_Running)
        queue = objAcquire(_cxui.queue);
    mutexRelease(&stateLock);
    return queue;
}

_Use_decl_annotations_
bool cxuiPost(closure cls)
{
    TaskQueue* queue = cxuiQueue();
    if (!queue) {
        closureDestroy(&cls);
        return false;
    }

    bool ok = tqCall(queue, cls);
    tqRelease(&queue);
    return ok;
}

typedef struct CxUiSyncCall {
    closure cls;
    bool result;
} CxUiSyncCall;

static bool cxuiSyncRun(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiSyncCall* sc = stvlAtPtr(cvars, 0);
    sc->result       = closureCall(sc->cls, stvNone);
    return true;
}

// Runs whether or not the call ran, including when the queue drops it at shutdown, so the
// caller always wakes.
static void cxuiSyncDestroy(stvlist* cvars)
{
    CxUiSyncCall* sc = stvlAtPtr(cvars, 0);
    SharedEvent* ev  = stvlAtPtr(cvars, 1);
    closureDestroy(&sc->cls);
    // sc belongs to the waiting thread and is gone once it wakes
    eventSignal(&ev->ev);
    sheventRelease(&ev);
}

_Use_decl_annotations_
bool cxuiCallSync(closure cls)
{
    if (cxuiIsUIThread()) {
        bool result = closureCall(cls, stvNone);
        closureDestroy(&cls);
        return result;
    }

    CxUiSyncCall sc   = { .cls = cls };
    SharedEvent* done = sheventCreate(0);
    closure wrap      = closureCreate(cxuiSyncRun, stvar(ptr, &sc), stvar(ptr, sheventAcquire(done)));
    closureSetDestroy(wrap, cxuiSyncDestroy);

    cxuiPost(wrap);
    eventWait(&done->ev);
    sheventRelease(&done);
    return sc.result;
}
