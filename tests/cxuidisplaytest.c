#include <cx/closure.h>
#include <cx/format.h>
#include <cx/string.h>
#include <cx/sys.h>
#include <cx/thread.h>
#include <cx/time.h>
#include <cxui.h>
#include <cx/fs.h>
#include <cxui/test/cxuitest.h>
#include "tqtestobj.h"

#if defined(_PLATFORM_WIN)
#include <cx/platform/win.h>
#endif

#define TEST_FILE cxuidisplaytest
#define TEST_FUNCS cxuidisplaytest_funcs
#include "common.h"

// Display tests run on the platform's own backend and skip where there is no display.

static bool uiEnvHas(strref name)
{
    string val = 0;
    bool set   = envGet(&val, name) && !strEmpty(val);
    strDestroy(&val);
    return set;
}

// 0 when started, CX_TEST_SKIP without a display, 1 on failure.
static int uiStartNative(void)
{
    if (uiEnvHas(_SL("CXUI_TEST_NODISPLAY")))
        return CX_TEST_SKIP;
    if (cxuiStart(&(CxUiStartDesc){ .name = _S"Display test UI" }))
        return 0;
#if defined(_PLATFORM_MACOS)
    // test_runner can't hand AppKit the main thread; the cxuidisplay program runs these instead.
    logStr(Info, _SL("on macOS, run display tests with cxuidisplay from the desktop session"));
    return CX_TEST_SKIP;
#elif defined(_PLATFORM_UNIX)
    if (!uiEnvHas(_SL("WAYLAND_DISPLAY")) && !uiEnvHas(_SL("DISPLAY")))
        return CX_TEST_SKIP;
#endif
    return 1;
}

#define UI_START_NATIVE()                                               \
    do {                                                                \
        int _startRet = uiStartNative();                                \
        if (_startRet == CX_TEST_SKIP)                                  \
            TEST_SKIP(_SL("no display"), stvNone);                      \
        if (_startRet)                                                  \
            TEST_FAIL(1, _SL("cxuiStart failed with a display"), stvNone); \
    } while (0)

typedef struct UiCounter {
    Event done;
    atomic(int32) ran;
    atomic(int32) offThread;
    int32 total;
} UiCounter;

static bool uiCount(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCounter* c = stvlAtPtr(cvars, 0);
    if (!cxuiIsUIThread())
        atomicFetchAdd(int32, &c->offThread, 1, AcqRel);
    if (stvlAt(cvars, 1, bool)) {
        // spin: a sub-millisecond sleep can round down to nothing on Windows
        int64 until = clockTimer() + 500;
        while (clockTimer() < until)
            ;
    }
    if (atomicFetchAdd(int32, &c->ran, 1, AcqRel) + 1 == c->total)
        eventSignal(&c->done);
    return true;
}

static bool uiReturnTrue(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    return cxuiIsUIThread();
}

// Polls a flag the UI thread sets, for up to limit.
static bool uiWaitFlag(atomic(bool)* flag, int64 limit)
{
    int64 end = clockTimer() + limit;
    while (!atomicLoad(bool, flag, Acquire)) {
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(5));
    }
    return true;
}

// Start, run work of each kind, and shut down, three times: each session gets a new UI thread.
static int test_cxuid_sessions(void)
{
    for (int32 round = 0; round < 3; round++) {
        UI_START_NATIVE();

        UiCounter c = { .total = 1 };
        eventInit(&c.done);
        cxuiPost(closureCreate(uiCount, stvar(ptr, &c), stvar(bool, false)));
        bool posted = eventWaitTimeout(&c.done, timeS(10));
        eventDestroy(&c.done);

        bool synced = cxuiCallSync(closureCreate(uiReturnTrue, stvNone));

        // scheduled work: five runs 20 ms apart, so the backend's timer has to fire
        WakeTestState wts = { 0 };
        TaskQueue* q      = cxuiQueue();
        TQWakeResched* t  = tqwakereschedCreate(&wts, 5, timeMS(20));
        tqAdd(q, t);
        bool scheduled = uiWaitFlag(&wts.done, timeS(10));
        int32 runs     = t->runs;
        tqRelease(&q);

        cxuiShutdown();
        objRelease(&t);

        if (!posted || atomicLoad(int32, &c.offThread, Acquire))
            TEST_FAIL(1, _SL("round ${int}: posted closure ran=${int}, off the UI thread=${int}"), stvar(int32, round),
                      stvar(int32, atomicLoad(int32, &c.ran, Acquire)), stvar(int32, atomicLoad(int32, &c.offThread, Acquire)));
        if (!synced)
            TEST_FAIL(1, _SL("round ${int}: cxuiCallSync didn't run on the UI thread"), stvar(int32, round));
        if (!scheduled)
            TEST_FAIL(1, _SL("round ${int}: the scheduled task stopped after ${int} of 5 runs"), stvar(int32, round),
                      stvar(int32, runs));
    }
    return 0;
}

static int uiFloodThread(Thread* self)
{
    UiCounter* c = stvlAtPtr(&self->args, 0);
    for (int32 i = 0; i < c->total; i++)
        cxuiPost(closureCreate(uiCount, stvar(ptr, c), stvar(bool, false)));
    return 0;
}

static bool uiPostBusy(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCounter* c = stvlAtPtr(cvars, 0);
    for (int32 i = 0; i < c->total; i++)
        cxuiPost(closureCreate(uiCount, stvar(ptr, c), stvar(bool, true)));
    return true;
}

// 1000 tasks posted from another thread all run, and so does more work than one tick's budget
// posted from the UI thread itself, which relies on the backend coming back after input.
static int test_cxuid_flood(void)
{
    UI_START_NATIVE();

    // First, while nothing else is queued: on Win32 the flood below leaves wake messages behind,
    // which would carry this along even if the backend never came back on its own.
    UiCounter busy = { .total = 200 };
    eventInit(&busy.done);
    cxuiPost(closureCreate(uiPostBusy, stvar(ptr, &busy)));
    bool busyDone = eventWaitTimeout(&busy.done, timeS(20));
    eventDestroy(&busy.done);

    UiCounter flood = { .total = 1000 };
    eventInit(&flood.done);
    Thread* thr     = thrCreate(uiFloodThread, _S"Flood", stvar(ptr, &flood));
    bool floodDone  = eventWaitTimeout(&flood.done, timeS(20));
    thrWait(thr, timeS(5));
    thrRelease(&thr);
    eventDestroy(&flood.done);

    cxuiShutdown();

    if (!floodDone || atomicLoad(int32, &flood.offThread, Acquire))
        TEST_FAIL(1, _SL("flood: ${int} of ${int} ran, ${int} off the UI thread"),
                  stvar(int32, atomicLoad(int32, &flood.ran, Acquire)), stvar(int32, flood.total),
                  stvar(int32, atomicLoad(int32, &flood.offThread, Acquire)));
    if (!busyDone)
        TEST_FAIL(1, _SL("over budget: ${int} of ${int} ran"), stvar(int32, atomicLoad(int32, &busy.ran, Acquire)),
                  stvar(int32, busy.total));
    return 0;
}

// ---- windows ----

typedef struct UiWinTest {
    CxUiWindow* w;
    int32 darkBeforeShow;   // -1 where the platform can't tell
    int32 resizes;
    CxUiSize lastPx;
    int32 closes;
    CxUiResult closeAnswer;
    CxUiPoint posSeen;
} UiWinTest;

static CxUiResult uiWinOnResize(stvlist* cvars, CxUiResizeEvent* ev)
{
    UiWinTest* t = stvlAtPtr(cvars, 0);
    t->resizes++;
    t->lastPx = ev->size;
    return CXUI_Default;
}

static CxUiResult uiWinOnClose(stvlist* cvars, CxUiCloseEvent* ev)
{
    unused_noeval(ev);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    t->closes++;
    return t->closeAnswer;
}

static bool uiWinBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title       = _S"cxui display test",
                                                       .size        = { 320, 200 },
                                                       .theme       = CXUI_Theme_Dark,
                                                       .closeAction = CXUI_OnClose_Destroy });
    cxuiOnResize(t->w, uiWinOnResize, stvar(ptr, t));
    cxuiOnClose(t->w, uiWinOnClose, stvar(ptr, t));
    if (!cxuiWindowRealize(t->w))
        return false;
    bool dark         = false;
    t->darkBeforeShow = cxuiTestFrameDark(t->w, &dark) ? dark : -1;
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return cxuiWindowIsVisible(t->w);
}

static bool uiWinResize(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    cxuiWindowSetSize(t->w, 400, 300);
    return true;
}

static bool uiWinIsResized(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    CxUiSize sz;
    return t->resizes > 0 && t->lastPx.w > 0 && cxuiWindowSize(t->w, &sz) && sz.w == 400 && sz.h == 300;
}

static bool uiWinMove(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    return cxuiWindowSetPosition(t->w, 100, 120);
}

#if defined(_PLATFORM_WIN) || defined(_PLATFORM_MACOS)
static bool uiWinIsMoved(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    return cxuiWindowPosition(t->w, &t->posSeen) && t->posSeen.x == 100 && t->posSeen.y == 120;
}
#endif

static bool uiWinClose(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t   = stvlAtPtr(cvars, 0);
    t->closeAnswer = (CxUiResult)stvlAt(cvars, 1, int32);
    cxuiTestClose(t->w);
    return true;
}

static bool uiWinIsShown(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    return cxuiWindowIsVisible(t->w);
}

static bool uiWinIsDead(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWinTest* t = stvlAtPtr(cvars, 0);
    return (CxUiWidget(t->w)->state & CXUI_WS_Dead) && !CxUiWidget(t->w)->peer;
}

// Calls fn on the UI thread until it returns true, for up to limit.
static bool uiPollSync(bool (*fn)(stvlist*, stvlist*), void* t, int64 limit)
{
    int64 end = clockTimer() + limit;
    for (;;) {
        if (cxuiCallSync(closureCreate(fn, stvar(ptr, t))))
            return true;
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
}

static bool uiPollSyncWithInt(bool (*fn)(stvlist*, stvlist*), void* a, int32 what, int64 limit)
{
    int64 end = clockTimer() + limit;
    for (;;) {
        if (cxuiCallSync(closureCreate(fn, stvar(ptr, a), stvar(int32, what), stvar(int32, 0))))
            return true;
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
}

static bool uiPollSyncWith(bool (*fn)(stvlist*, stvlist*), void* a, void* b, int64 limit)
{
    int64 end = clockTimer() + limit;
    for (;;) {
        if (cxuiCallSync(closureCreate(fn, stvar(ptr, a), stvar(ptr, b))))
            return true;
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
}

// A native window in two sessions: dark before its first show, resized with an event, moved where
// the platform allows it, a close vetoed and then allowed to destroy it.
static int test_cxuid_window(void)
{
    for (int32 round = 0; round < 2; round++) {
        UI_START_NATIVE();

        UiWinTest t = { 0 };
        bool built  = cxuiCallSync(closureCreate(uiWinBuild, stvar(ptr, &t)));
        cxuiCallSync(closureCreate(uiWinResize, stvar(ptr, &t)));
        bool resized = uiPollSync(uiWinIsResized, &t, timeS(5));
        bool placed  = cxuiCallSync(closureCreate(uiWinMove, stvar(ptr, &t)));
#if defined(_PLATFORM_WIN) || defined(_PLATFORM_MACOS)
        bool moved = placed && uiPollSync(uiWinIsMoved, &t, timeS(5));
#else
        bool moved = !placed;   // Linux can't place windows
#endif
        cxuiCallSync(closureCreate(uiWinClose, stvar(ptr, &t), stvar(int32, CXUI_Handled)));
        bool vetoed = t.closes == 1 && cxuiCallSync(closureCreate(uiWinIsShown, stvar(ptr, &t)));
        cxuiCallSync(closureCreate(uiWinClose, stvar(ptr, &t), stvar(int32, CXUI_Default)));
        bool destroyed = t.closes == 2 && cxuiCallSync(closureCreate(uiWinIsDead, stvar(ptr, &t)));

        cxuiShutdown();
        objRelease(&t.w);

        if (!built)
            TEST_FAIL(1, _SL("round ${int}: the window wasn't realized and shown"), stvar(int32, round));
        if (t.darkBeforeShow == 0)
            TEST_FAIL(1, _SL("round ${int}: the frame wasn't dark before the first show"), stvar(int32, round));
        if (t.darkBeforeShow < 0)
            TEST_INFO(_SL("round ${int}: the platform can't report whether the frame is dark"), stvar(int32, round));
        if (!resized)
            TEST_FAIL(1, _SL("round ${int}: resizing gave ${int} events, last ${int}x${int} px"), stvar(int32, round),
                      stvar(int32, t.resizes), stvar(int32, t.lastPx.w), stvar(int32, t.lastPx.h));
        if (!moved)
            TEST_FAIL(1, _SL("round ${int}: setting the position returned ${int}; read back ${int},${int} (want 100,120)"),
                      stvar(int32, round), stvar(int32, placed), stvar(int32, t.posSeen.x), stvar(int32, t.posSeen.y));
        if (!vetoed || !destroyed)
            TEST_FAIL(1, _SL("round ${int}: closes ${int}, veto kept it ${int}, then destroyed ${int}"),
                      stvar(int32, round), stvar(int32, t.closes), stvar(int32, vetoed), stvar(int32, destroyed));
    }
    return 0;
}

// ---- popups and message boxes ----

typedef struct UiBoxFlood {
    UiCounter tasks;
    atomic(bool) closed;
    int32 result;
    int32 ranAtAnswer;
    int64 openedAt;
} UiBoxFlood;

// Answers the box once every task has run, or after 20 s so a failure can't hang.
static CxUiResult uiAnswerWhenDone(stvlist* cvars, CxUiTimerEvent* ev)
{
    UiBoxFlood* bf = stvlAtPtr(cvars, 0);
    int32 ran      = atomicLoad(int32, &bf->tasks.ran, Acquire);
    if ((ran == bf->tasks.total || clockTimer() - bf->openedAt > timeS(20)) && cxuiTestAnswerMessageBox(1)) {
        bf->ranAtAnswer = ran;
        cxuiTimerStop(ev->timer);
    }
    return CXUI_Default;
}

static bool uiOpenFloodBox(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiBoxFlood* bf = stvlAtPtr(cvars, 0);
    CxUiTimer* t   = cxuiTimerCreate(timeMS(10), 0, uiAnswerWhenDone, stvar(ptr, bf));
    cxuiTimerStart(t);
    bf->openedAt = clockTimer();
    bf->result   = cxuiMessageBox(&(CxUiMessageBoxDesc){ .title = _S"cxui display test",
                                                         .text  = _S"Tasks from another thread are running." });
    objRelease(&t);
    atomicStore(bool, &bf->closed, true, Release);
    return true;
}

static bool uiBoxesOpen(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    return cxuiTestMessageBoxes() > 0;
}

// Polls the UI thread, which only answers if the UI queue runs inside whatever modal is open.
static bool uiWaitBoxOpen(int64 limit)
{
    int64 end = clockTimer() + limit;
    while (!cxuiCallSync(closureCreate(uiBoxesOpen, stvNone))) {
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
    return true;
}

static CxUiResult uiAnswerAny(stvlist* cvars, CxUiTimerEvent* ev)
{
    if (cxuiTestAnswerMessageBox(stvlAt(cvars, 0, int32)))
        cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

static bool uiStartAnswerer(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTimer** out = stvlAtPtr(cvars, 0);
    *out            = cxuiTimerCreate(timeMS(10), 0, uiAnswerAny, stvar(int32, stvlAt(cvars, 1, int32)));
    return cxuiTimerStart(*out);
}

typedef struct UiBoxShutdown {
    atomic(bool) returned;
    int32 result;
} UiBoxShutdown;

static bool uiBoxUntilShutdown(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiBoxShutdown* bs = stvlAtPtr(cvars, 0);
    bs->result = cxuiMessageBox(&(CxUiMessageBoxDesc){ .text = _S"cxui shuts down under this box",
                                                       .buttons = CXUI_MB_OkCancel });
    atomicStore(bool, &bs->returned, true, Release);
    return true;
}

// A native message box opened from a UI task while another thread posts 1000 tasks: every task runs
// while the box is open, and a timer closes it. Then a box from another thread, and a box that
// cxui shuts down under, and one with custom buttons.
static int test_cxuid_msgbox(void)
{
    UI_START_NATIVE();

    UiBoxFlood bf = { .tasks.total = 1000, .result = -1 };
    eventInit(&bf.tasks.done);
    cxuiPost(closureCreate(uiOpenFloodBox, stvar(ptr, &bf)));
    bool opened = uiWaitBoxOpen(timeS(10));
    Thread* thr = opened ? thrCreate(uiFloodThread, _S"Flood", stvar(ptr, &bf.tasks)) : NULL;
    bool ran    = opened && eventWaitTimeout(&bf.tasks.done, timeS(20));
    int64 end   = clockTimer() + timeS(30);
    while (!atomicLoad(bool, &bf.closed, Acquire) && clockTimer() < end)
        osSleep(timeMS(10));
    bool closed = atomicLoad(bool, &bf.closed, Acquire);
    if (thr) {
        thrWait(thr, timeS(5));
        thrRelease(&thr);
    }
    eventDestroy(&bf.tasks.done);

    CxUiTimer* answerer = NULL;
    cxuiCallSync(closureCreate(uiStartAnswerer, stvar(ptr, &answerer), stvar(int32, 2)));
    int32 fromThread = cxuiMessageBox(&(CxUiMessageBoxDesc){ .title   = _S"cxui display test",
                                                             .text    = _S"Asked from another thread.",
                                                             .kind    = CXUI_MB_Question,
                                                             .buttons = CXUI_MB_YesNo });
    objRelease(&answerer);
    cxuiCallSync(closureCreate(uiStartAnswerer, stvar(ptr, &answerer), stvar(int32, 3)));
    int32 custom = cxuiMessageBox(&(CxUiMessageBoxDesc){ .title         = _S"cxui display test",
                                                         .text          = _S"Custom buttons.",
                                                         .buttons       = CXUI_MB_Custom,
                                                         .custom        = { _S"&Install", _S"&Later", _S"&Never" },
                                                         .defaultButton = 2,
                                                         .cancelButton  = 2 });
    objRelease(&answerer);

    UiBoxShutdown bs = { .result = -1 };
    cxuiPost(closureCreate(uiBoxUntilShutdown, stvar(ptr, &bs)));
    bool shutdownOpened = uiWaitBoxOpen(timeS(10));
    cxuiShutdown();

    if (!opened || !ran || !closed || bf.result != 1 || bf.ranAtAnswer != bf.tasks.total)
        TEST_FAIL(1, _SL("box opened ${int}, closed ${int} with ${int}; ${int} of ${int} tasks ran while it was open"),
                  stvar(int32, opened), stvar(int32, closed), stvar(int32, bf.result), stvar(int32, bf.ranAtAnswer),
                  stvar(int32, bf.tasks.total));
    if (atomicLoad(int32, &bf.tasks.offThread, Acquire))
        TEST_FAIL(1, _SL("${int} tasks ran off the UI thread"), stvar(int32, atomicLoad(int32, &bf.tasks.offThread, Acquire)));
    if (fromThread != 2 || custom != 3)
        TEST_FAIL(1, _SL("a box from another thread returned ${int} (want 2), a custom box ${int} (want 3)"),
                  stvar(int32, fromThread), stvar(int32, custom));
    if (!shutdownOpened || !atomicLoad(bool, &bs.returned, Acquire) || bs.result != 0)
        TEST_FAIL(1, _SL("shutting down under a box: opened ${int}, returned ${int} with ${int}"),
                  stvar(int32, shutdownOpened), stvar(int32, atomicLoad(bool, &bs.returned, Acquire)),
                  stvar(int32, bs.result));
    return 0;
}

typedef struct UiPopupNest {
    CxUiWindow* owner;
    CxUiWindow* outer;
    CxUiWindow* inner;
    int32 order[2];
    int32 n;
    int32 outerResult;
    int32 innerResult;
    atomic(bool) done;
    // Windows: the outer popup's minimize and maximize boxes before it is modal, while, and after
    uint32 beforeBoxes, modalBoxes, afterBoxes;
} UiPopupNest;

#if defined(_PLATFORM_WIN)
static uint32 uiPopupBoxes(const wchar_t* title)
{
    HWND hwnd = FindWindowW(NULL, title);
    return hwnd ? (uint32)GetWindowLongW(hwnd, GWL_STYLE) & (WS_MINIMIZEBOX | WS_MAXIMIZEBOX) : 0xFFFFFFFF;
}
#endif

static CxUiResult uiEndPopupTimer(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    cxuiWindowEndPopup(stvlAtObj(cvars, 0, CxUiWindow), stvlAt(cvars, 1, int32));
    return CXUI_Default;
}

static CxUiResult uiOpenInner(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    UiPopupNest* pn = stvlAtPtr(cvars, 0);
#if defined(_PLATFORM_WIN)
    pn->modalBoxes = uiPopupBoxes(L"Outer popup");
#endif
    CxUiTimer* a    = cxuiTimerCreate(timeMS(50), CXUI_Timer_OneShot, uiEndPopupTimer, stvar(object, pn->outer), stvar(int32, 1));
    CxUiTimer* b    = cxuiTimerCreate(timeMS(150), CXUI_Timer_OneShot, uiEndPopupTimer, stvar(object, pn->inner), stvar(int32, 2));
    cxuiTimerStart(a);
    cxuiTimerStart(b);
    pn->innerResult       = cxuiWindowPopup(pn->inner, pn->outer);
    pn->order[pn->n++] = 2;
    objRelease(&a);
    objRelease(&b);
    return CXUI_Default;
}

static bool uiRunPopups(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiPopupNest* pn = stvlAtPtr(cvars, 0);
    pn->owner = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui display test: owner", .size = { 400, 300 } });
    pn->outer = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Outer popup", .size = { 300, 200 } });
    pn->inner = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Inner popup", .size = { 200, 120 } });
    cxuiWindowShow(pn->owner, CXUI_SHOW_Normal);

    CxUiTimer* t       = cxuiTimerCreate(timeMS(50), CXUI_Timer_OneShot, uiOpenInner, stvar(ptr, pn));
    cxuiTimerStart(t);
#if defined(_PLATFORM_WIN)
    cxuiWindowShow(pn->outer, CXUI_SHOW_Normal);
    pn->beforeBoxes = uiPopupBoxes(L"Outer popup");
#endif
    pn->outerResult    = cxuiWindowPopup(pn->outer, pn->owner);
    pn->order[pn->n++] = 1;
#if defined(_PLATFORM_WIN)
    pn->afterBoxes = uiPopupBoxes(L"Outer popup");
#endif
    objRelease(&t);
    cxuiDestroy(pn->owner);
    atomicStore(bool, &pn->done, true, Release);
    return true;
}

// Native popups nested two deep, the outer one ended first: the inner call returns first. On
// Windows a popup has no minimize or maximize box while it is modal, as dialogs don't.
static int test_cxuid_popup(void)
{
    UI_START_NATIVE();
    UiPopupNest pn = { 0 };
    cxuiPost(closureCreate(uiRunPopups, stvar(ptr, &pn)));
    int64 end = clockTimer() + timeS(10);
    while (!atomicLoad(bool, &pn.done, Acquire) && clockTimer() < end)
        osSleep(timeMS(10));
    bool done = atomicLoad(bool, &pn.done, Acquire);
    cxuiShutdown();
    objRelease(&pn.inner);
    objRelease(&pn.outer);
    objRelease(&pn.owner);

    if (!done || pn.n != 2 || pn.order[0] != 2 || pn.outerResult != 1 || pn.innerResult != 2)
        TEST_FAIL(1, _SL("finished ${int}; first to return ${int}; outer ${int}, inner ${int}"), stvar(int32, done),
                  stvar(int32, pn.order[0]), stvar(int32, pn.outerResult), stvar(int32, pn.innerResult));
#if defined(_PLATFORM_WIN)
    if (!pn.beforeBoxes || pn.beforeBoxes == 0xFFFFFFFF || pn.modalBoxes != 0 || pn.afterBoxes != pn.beforeBoxes)
        TEST_FAIL(1, _SL("the outer popup's boxes: 0x${uint(hex)} before, 0x${uint(hex)} while modal, 0x${uint(hex)} after"),
                  stvar(uint32, pn.beforeBoxes), stvar(uint32, pn.modalBoxes), stvar(uint32, pn.afterBoxes));
#endif
    return 0;
}

// ---- canvases ----

typedef struct MouseRecD {
    uint32 action, button, buttons, flags;
    int32 x, y;
} MouseRecD;

typedef struct UiCanvasTest {
    CxUiWindow* w;
    CxUiCanvas* cv;
    int32 paints;
    CxUiRect dirty;
    CxUiSize size;
    int32 paintsBefore;
    int32 paintsAtExpose;
    bool exposed;
    int32 paintNowPaints;
    uint32 engine;   // CxUiDrawBackends asked for
    string used;     // the engine the canvas got
} UiCanvasTest;

static CxUiResult uiCvPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    UiCanvasTest* t = stvlAtPtr(cvars, 0);
    t->paints++;
    t->dirty = ev->dirty;
    t->size  = cxuiDrawSize(ev->draw);
    cxuiDrawFill(ev->draw, 0, 0, t->size.w, t->size.h, cxuiHex(0x203040));
    cxuiDrawFrame(ev->draw, 10, 10, t->size.w - 20, t->size.h - 20, 2, cxuiHex(0xE0E0E0));
    cxuiDrawLine(ev->draw, 10, 10, t->size.w - 10, t->size.h - 10, 1, cxuiHex(0xFFC040));
    return CXUI_Handled;
}

static bool uiCvBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCanvasTest* t = stvlAtPtr(cvars, 0);
    t->w            = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui canvas test", .size = { 320, 200 } });
    t->cv           = cxuiCanvasCreate(&(CxUiCanvasDesc){ .draw = t->engine });
    cxuiOnPaint(t->cv, uiCvPaint, stvar(ptr, t));
    cxuiContainerAppend(t->w, t->cv);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    strDup(&t->used, cxuiTestCanvasEngine(t->cv));
    return CxUiWidget(t->cv)->peer != NULL;
}

static bool uiCvPainted(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCanvasTest* t = stvlAtPtr(cvars, 0);
    CxUiSize sz;
    return t->paints > 0 && cxuiCanvasSize(t->cv, &sz) && sz.w > 0 && t->size.w == sz.w && t->size.h == sz.h;
}

static bool uiCvPaintedAgain(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCanvasTest* t = stvlAtPtr(cvars, 0);
    return t->paints > t->paintsBefore;
}

static bool uiCvInvalidate(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCanvasTest* t = stvlAtPtr(cvars, 0);
    t->paintsBefore = t->paints;
    cxuiCanvasInvalidate(t->cv, 10, 10, 5, 5);
    cxuiCanvasInvalidate(t->cv, 100, 50, 10, 10);
    cxuiCanvasInvalidate(t->cv, 30, 30, 2, 2);
    return true;
}

static bool uiCvExpose(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCanvasTest* t   = stvlAtPtr(cvars, 0);
    t->paintsAtExpose = t->paints;
    t->exposed        = cxuiTestExpose(t->cv);
    return true;
}

static bool uiCvPaintNow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCanvasTest* t = stvlAtPtr(cvars, 0);
    int32 before    = t->paints;
    cxuiCanvasInvalidate(t->cv, 0, 0, 4, 4);
    cxuiCanvasPaintNow(t->cv);
    t->paintNowPaints = t->paints - before;
    return true;
}

// A native canvas paints once shown; invalidations coalesce into one paint of their bounding box;
// an exposure the program didn't ask for is copied from the canvas's buffer without a paint; and
// paint-now paints before it returns.
static int cxuidCanvasWith(uint32 engine, strref want)
{
    UI_START_NATIVE();
    UiCanvasTest t = { .engine = engine };
    bool built     = cxuiCallSync(closureCreate(uiCvBuild, stvar(ptr, &t)));
    bool painted   = built && uiPollSync(uiCvPainted, &t, timeS(5));

    cxuiCallSync(closureCreate(uiCvInvalidate, stvar(ptr, &t)));
    uiPollSync(uiCvPaintedAgain, &t, timeS(5));
    osSleep(timeMS(200));
    cxuiCallSync(closureCreate(uiReturnTrue, stvNone));
    int32 coalesced = t.paints - t.paintsBefore;
    CxUiRect dirty  = t.dirty;

    cxuiCallSync(closureCreate(uiCvExpose, stvar(ptr, &t)));
    osSleep(timeMS(200));
    cxuiCallSync(closureCreate(uiReturnTrue, stvNone));
    int32 exposePaints = t.paints - t.paintsAtExpose;

    cxuiCallSync(closureCreate(uiCvPaintNow, stvar(ptr, &t)));

    cxuiShutdown();
    objRelease(&t.cv);
    objRelease(&t.w);
    TEST_INFO(_SL("canvas asked for engine ${uint}, got ${string}"), stvar(uint32, engine), stvar(string, t.used));
    int ret = 0;
    if (!strEmpty(want) && !strEq(t.used, want))
        TEST_FAILV(ret, 1, _SL("canvas asked for engine ${uint} got ${string}, expected ${string}"), stvar(uint32, engine),
                   stvar(string, t.used), stvar(strref, want));
    strDestroy(&t.used);
    if (ret)
        return ret;

#if !defined(_PLATFORM_WIN)
    if (!built)
        TEST_SKIP(_SL("no native canvases on this platform yet"), stvNone);
#endif
    if (!built || !painted)
        TEST_FAIL(1, _SL("built ${int}; ${int} paints at ${int}x${int}"), stvar(int32, built), stvar(int32, t.paints),
                  stvar(int32, t.size.w), stvar(int32, t.size.h));
    // At least their bounding box; GTK grows it to whole tiles.
    if (coalesced != 1 || dirty.x > 10 || dirty.y > 10 || dirty.x + dirty.w < 110 || dirty.y + dirty.h < 60)
        TEST_FAIL(1, _SL("three invalidations gave ${int} paints, the last of ${int},${int} ${int}x${int}"),
                  stvar(int32, coalesced), stvar(int32, dirty.x), stvar(int32, dirty.y), stvar(int32, dirty.w),
                  stvar(int32, dirty.h));
    if (!t.exposed)
        TEST_INFO(_SL("the platform can't be asked to expose a canvas"), stvNone);
    else if (exposePaints != 0)
        TEST_FAIL(1, _SL("an exposure ran the paint handler ${int} times"), stvar(int32, exposePaints));
    if (t.paintNowPaints != 1)
        TEST_FAIL(1, _SL("paint-now painted ${int} times before returning"), stvar(int32, t.paintNowPaints));
    return 0;
}

// GDI and Direct2D on Windows; on other platforms each choice gets the platform's one engine.
static int test_cxuid_canvas(void)
{
#if defined(_PLATFORM_WIN)
    bool wine = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != NULL;
    int ret   = cxuidCanvasWith(CXUI_Draw_GDI, _S"gdi");
    if (ret == 0)
        ret = cxuidCanvasWith(CXUI_Draw_D2D, NULL);
    if (ret == 0)
        ret = cxuidCanvasWith(CXUI_Draw_Auto, wine ? _S"gdi" : NULL);
    return ret;
#else
    return cxuidCanvasWith(CXUI_Draw_Auto, NULL);
#endif
}

// ---- mouse capture (Windows) ----

#if defined(_PLATFORM_WIN)
typedef struct UiCaptureTest {
    CxUiWindow* w;
    CxUiCanvas* cv;
    CxUiPoint origin;
    POINT saved;
    MouseRecD ev[64];
    int32 n;
    int32 keyDowns, keyUps;   // the A key
    float64 wheelDy;
    int32 wheels;
} UiCaptureTest;

static CxUiResult uiCapKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    UiCaptureTest* t = stvlAtPtr(cvars, 0);
    if (ev->key == CXUI_KEY_A) {
        if (ev->down)
            t->keyDowns++;
        else
            t->keyUps++;
    }
    return CXUI_Handled;
}

static CxUiResult uiCapWheel(stvlist* cvars, CxUiWheelEvent* ev)
{
    UiCaptureTest* t = stvlAtPtr(cvars, 0);
    t->wheels++;
    t->wheelDy += ev->dy;
    return CXUI_Handled;
}

static CxUiResult uiCapMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    UiCaptureTest* t = stvlAtPtr(cvars, 0);
    if (t->n < 64)
        t->ev[t->n] = (MouseRecD){ ev->action, ev->button, ev->buttons, ev->flags, ev->x, ev->y };
    t->n++;
    return CXUI_Handled;
}

static CxUiResult uiCapPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    unused_noeval(cvars);
    cxuiDrawFill(ev->draw, 0, 0, 10000, 10000, cxuiHex(0x305070));
    return CXUI_Handled;
}

static bool uiCapBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCaptureTest* t = stvlAtPtr(cvars, 0);
    t->w             = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui capture test", .size = { 200, 150 } });
    t->cv            = cxuiCanvasCreate(NULL);
    cxuiOnMouse(t->cv, uiCapMouse, stvar(ptr, t));
    cxuiOnKey(t->cv, uiCapKey, stvar(ptr, t));
    cxuiOnWheel(t->cv, uiCapWheel, stvar(ptr, t));
    cxuiOnPaint(t->cv, uiCapPaint, stvNone);
    cxuiContainerAppend(t->w, t->cv);
    cxuiWindowSetPosition(t->w, 100, 100);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    GetCursorPos(&t->saved);
    return cxuiTestCanvasScreenOrigin(t->cv, &t->origin);
}

// Moves the real cursor, or presses or releases the left button, relative to the canvas. On the
// UI thread, whose DPI awareness makes these physical pixels.
static bool uiCapInput(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCaptureTest* t = stvlAtPtr(cvars, 0);
    int32 what       = stvlAt(cvars, 1, int32);
    if (what == 3)
        return ReleaseCapture();   // as when something else takes the mouse
    INPUT in = { 0 };
    if (what == 4 || what == 5) {
        in.type       = INPUT_KEYBOARD;
        in.ki.wVk     = 'A';
        in.ki.dwFlags = what == 5 ? KEYEVENTF_KEYUP : 0;
        return SendInput(1, &in, sizeof(in)) == 1;
    }
    if (what == 6) {
        in.type         = INPUT_MOUSE;
        in.mi.dwFlags   = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = WHEEL_DELTA;   // away from the user: up
        return SendInput(1, &in, sizeof(in)) == 1;
    }
    if (what == 0)
        return SetCursorPos(t->origin.x + stvlAt(cvars, 2, int32), t->origin.y + stvlAt(cvars, 3, int32));
    in.type       = INPUT_MOUSE;
    in.mi.dwFlags = what == 1 ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    return SendInput(1, &in, sizeof(in)) == 1;
}

static bool uiCapRestore(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCaptureTest* t = stvlAtPtr(cvars, 0);
    SetCursorPos(t->saved.x, t->saved.y);
    return true;
}

static void uiCapStep(UiCaptureTest* t, int32 what, int32 x, int32 y)
{
    cxuiCallSync(closureCreate(uiCapInput, stvar(ptr, t), stvar(int32, what), stvar(int32, x), stvar(int32, y)));
    osSleep(timeMS(150));   // for the input to come round as messages
    cxuiCallSync(closureCreate(uiReturnTrue, stvNone));
}

// With the real mouse and keyboard: a drag that leaves the canvas keeps reporting to it, outside its
// bounds, and Leave waits for the release; the click focuses it for keys; the wheel reaches it;
// losing capture ends the drag.
static int test_cxuid_capture(void)
{
    UI_START_NATIVE();
    UiCaptureTest t = { 0 };
    int32 lostFrom  = 0;
    bool built      = cxuiCallSync(closureCreate(uiCapBuild, stvar(ptr, &t)));
    osSleep(timeMS(300));
    if (built) {
        uiCapStep(&t, 0, 50, 50);
        uiCapStep(&t, 1, 0, 0);
        uiCapStep(&t, 0, 30, 60);
        uiCapStep(&t, 0, -30, 250);
        uiCapStep(&t, 2, 0, 0);
        // The click focused the canvas; a key, and the wheel over it
        uiCapStep(&t, 0, 50, 50);
        uiCapStep(&t, 4, 0, 0);
        uiCapStep(&t, 5, 0, 0);
        uiCapStep(&t, 6, 0, 0);
        // Pressed again, then the capture is taken away before the release
        lostFrom = t.n;
        uiCapStep(&t, 0, 50, 50);
        uiCapStep(&t, 1, 0, 0);
        uiCapStep(&t, 3, 0, 0);
        uiCapStep(&t, 2, 0, 0);
        cxuiCallSync(closureCreate(uiCapRestore, stvar(ptr, &t)));
    }
    cxuiShutdown();
    objRelease(&t.cv);
    objRelease(&t.w);
    if (!built)
        TEST_FAIL(1, _SL("the canvas wasn't shown"), stvNone);

    // Find the down, an outside move, the up there, and a leave only after it
    int32 down = -1, outside = -1, up = -1, leave = -1;
    for (int32 i = 0; i < t.n && i < 64; i++) {
        MouseRecD* e = &t.ev[i];
        if (e->action == CXUI_MOUSE_Down && down < 0 && e->x == 50 && e->y == 50)
            down = i;
        else if (e->action == CXUI_MOUSE_Move && down >= 0 && e->x == -30 && e->y == 250)
            outside = i;
        else if (e->action == CXUI_MOUSE_Up && e->x == -30 && e->y == 250)
            up = i;
        else if (e->action == CXUI_MOUSE_Leave && leave < 0)
            leave = i;
    }
    if (down < 0 || outside < down || up < outside || leave < up)
        TEST_FAIL(1, _SL("${int} events: down at ${int}, move outside at ${int}, up at ${int}, first leave at ${int}"),
                  stvar(int32, t.n), stvar(int32, down), stvar(int32, outside), stvar(int32, up), stvar(int32, leave));

    // Exactly one Up after the second press, marked as lost
    int32 ups = 0, lost = 0;
    for (int32 i = lostFrom; i < t.n && i < 64; i++) {
        if (t.ev[i].action == CXUI_MOUSE_Up) {
            ups++;
            if (t.ev[i].flags & CXUI_MOUSE_CaptureLost)
                lost++;
        }
    }
    if (ups != 1 || lost != 1)
        TEST_FAIL(1, _SL("after the capture was taken: ${int} ups, ${int} marked lost"), stvar(int32, ups),
                  stvar(int32, lost));
    if (t.keyDowns != 1 || t.keyUps != 1 || t.wheels != 1 || t.wheelDy != -1)
        TEST_FAIL(1, _SL("A went down ${int} and up ${int} times; ${int} wheel events, dy ${float}"),
                  stvar(int32, t.keyDowns), stvar(int32, t.keyUps), stvar(int32, t.wheels), stvar(float64, t.wheelDy));
    return 0;
}
#endif

// ---- scale change ----

typedef struct UiDpiTest {
    CxUiWindow* w;
    CxUiCanvas* cv;
    float32 startScale;
    float32 seenScale, seenOld;
    int32 dpiEvents;
    int32 paints;
    CxUiSize paintSize;
    CxUiSize before;
    bool forced;
} UiDpiTest;

static CxUiResult uiDpiPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    UiDpiTest* t = stvlAtPtr(cvars, 0);
    t->paints++;
    t->paintSize = cxuiDrawSize(ev->draw);
    cxuiDrawFill(ev->draw, 0, 0, t->paintSize.w, t->paintSize.h, cxuiHex(0x404040));
    return CXUI_Handled;
}

static CxUiResult uiDpiChanged(stvlist* cvars, CxUiDpiChangedEvent* ev)
{
    UiDpiTest* t = stvlAtPtr(cvars, 0);
    t->dpiEvents++;
    t->seenScale = ev->scale;
    t->seenOld   = ev->oldScale;
    return CXUI_Default;
}

static bool uiDpiBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDpiTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui scale test", .size = { 240, 160 } });
    t->cv        = cxuiCanvasCreate(NULL);
    cxuiOnPaint(t->cv, uiDpiPaint, stvar(ptr, t));
    cxuiOnDpiChanged(t->w, uiDpiChanged, stvar(ptr, t));
    cxuiContainerAppend(t->w, t->cv);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    t->startScale = cxuiCanvasScale(t->cv);
    return CxUiWidget(t->cv)->peer != NULL;
}

static bool uiDpiPainted(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDpiTest* t = stvlAtPtr(cvars, 0);
    return t->paints > 0;
}

static bool uiDpiForce(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDpiTest* t = stvlAtPtr(cvars, 0);
    cxuiCanvasSize(t->cv, &t->before);
    t->paints = 0;
    t->forced = cxuiTestDpi(t->w, stvlAt(cvars, 1, float32));
    return t->forced;
}

static bool uiDpiRepainted(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDpiTest* t = stvlAtPtr(cvars, 0);
    return t->dpiEvents > 0 && t->paints > 0;
}

// A scale change from the platform: the event with the old and new scales, the canvas resized in
// proportion, and painted again at its new size.
static int test_cxuid_dpi(void)
{
    UI_START_NATIVE();
    UiDpiTest t = { 0 };
    bool built  = cxuiCallSync(closureCreate(uiDpiBuild, stvar(ptr, &t))) && uiPollSync(uiDpiPainted, &t, timeS(5));
    float32 to  = t.startScale * 1.5f;
    bool forced = built && cxuiCallSync(closureCreate(uiDpiForce, stvar(ptr, &t), stvar(float32, to)));
    bool done   = forced && uiPollSync(uiDpiRepainted, &t, timeS(5));
    CxUiSize start = t.before;
    float32 seen = t.seenScale, seenOld = t.seenOld;
    int32 events = t.dpiEvents;
    // Back to where it started; t.before is then the size at the forced scale.
    if (forced) {
        t.dpiEvents = 0;
        cxuiCallSync(closureCreate(uiDpiForce, stvar(ptr, &t), stvar(float32, t.startScale)));
        uiPollSync(uiDpiRepainted, &t, timeS(5));
    }
    CxUiSize scaled = t.before;
    cxuiShutdown();
    objRelease(&t.cv);
    objRelease(&t.w);

    if (!built)
        TEST_FAIL(1, _SL("the canvas wasn't shown and painted"), stvNone);
    if (!forced)
        TEST_SKIP(_SL("this platform can't be told its scale changed"), stvNone);
    TEST_INFO(_SL("scale ${float} to ${float}: canvas ${int}x${int} to ${int}x${int}"), stvar(float64, t.startScale),
              stvar(float64, to), stvar(int32, start.w), stvar(int32, start.h), stvar(int32, scaled.w),
              stvar(int32, scaled.h));
    if (!done || events != 1 || seen != to || seenOld != t.startScale)
        TEST_FAIL(1, _SL("${int} scale events, last ${float} from ${float}; repainted ${int}"), stvar(int32, events),
                  stvar(float64, seen), stvar(float64, seenOld), stvar(int32, done));
    if (abs(scaled.w - (int32)(start.w * 1.5f + 0.5f)) > 3 || abs(scaled.h - (int32)(start.h * 1.5f + 0.5f)) > 3)
        TEST_FAIL(1, _SL("the canvas went from ${int}x${int} to ${int}x${int}, not half as big again"),
                  stvar(int32, start.w), stvar(int32, start.h), stvar(int32, scaled.w), stvar(int32, scaled.h));
    return 0;
}

// ---- layout ----

typedef struct UiLayoutTest {
    CxUiWindow* w;
    CxUiBox* row;
    CxUiCanvas* cv[3];
    CxUiSize painted[3];   // the size each canvas last painted at
    CxUiSize want;         // the client size being waited for, in DIPs
    CxUiSize size;         // the window's, as last seen
} UiLayoutTest;

static CxUiResult uiLayPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    CxUiSize* painted = stvlAtPtr(cvars, 0);
    *painted          = cxuiDrawSize(ev->draw);
    cxuiDrawFill(ev->draw, 0, 0, painted->w, painted->h, cxuiHex(0x305070));
    return CXUI_Handled;
}

// A window sized to its content: a 200x50 canvas over a row of an expanding canvas and a 50x40 one.
static bool uiLayBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLayoutTest* t = stvlAtPtr(cvars, 0);
    t->w            = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui layout test" });
    CxUiBox* col    = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
    t->row          = cxuiBoxCreateH();
    for (int i = 0; i < 3; i++) {
        t->cv[i] = cxuiCanvasCreate(NULL);
        cxuiOnPaint(t->cv[i], uiLayPaint, stvar(ptr, &t->painted[i]));
    }
    cxuiWidgetSetExpand(t->cv[0], CXUI_EXPAND_None);
    cxuiWidgetSetNaturalSize(t->cv[0], cxuiLen(200), cxuiLen(50));
    cxuiWidgetSetNaturalSize(t->cv[1], cxuiLen(0), cxuiLen(40));
    cxuiWidgetSetExpand(t->cv[2], CXUI_EXPAND_None);
    cxuiWidgetSetNaturalSize(t->cv[2], cxuiLen(50), cxuiLen(40));
    cxuiContainerAppend(col, t->cv[0]);
    cxuiContainerAppend(col, t->row);
    cxuiContainerAppend(t->row, t->cv[1]);
    cxuiContainerAppend(t->row, t->cv[2]);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    t->want = (CxUiSize){ 200, 94 };
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// Whether the window has the size waited for, and each shown canvas painted at the bounds layout
// gave it.
static bool uiLaySettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLayoutTest* t = stvlAtPtr(cvars, 0);
    // The size set is recorded at once; the client area is laid out once the platform resizes.
    CxUiRect client;
    cxuiWindowSize(t->w, &t->size);
    if (t->size.w != t->want.w || t->size.h != t->want.h || !cxuiTestBounds(CxUiWidget(t->w), &client) ||
        client.w != cxuiPx(t->w, t->want.w) || client.h != cxuiPx(t->w, t->want.h))
        return false;
    for (int i = 0; i < 3; i++) {
        CxUiRect b;
        bool want = i == 0 || cxuiWidgetVisible(t->row), shown;
        if (!cxuiTestShown(CxUiWidget(t->cv[i]), &shown) || shown != want)
            return false;
        if (!want)
            continue;
        if (!cxuiTestBounds(CxUiWidget(t->cv[i]), &b) || b.w != t->painted[i].w || b.h != t->painted[i].h)
            return false;
    }
    return true;
}

static bool uiLayResize(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLayoutTest* t = stvlAtPtr(cvars, 0);
    t->want         = (CxUiSize){ 400, 200 };
    cxuiWindowSetSize(t->w, 400, 200);
    return true;
}

// Hidden, the row takes no space, so the minimum shrinks to the top canvas; asked to be smaller
// than that, the window stays at it.
static bool uiLayHideRow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLayoutTest* t = stvlAtPtr(cvars, 0);
    cxuiWidgetSetVisible(t->row, false);
    cxuiRefresh(t->w);
    t->want = (CxUiSize){ 200, 50 };
    cxuiWindowSetSize(t->w, 10, 10);
    return true;
}

static bool uiLayShowRow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLayoutTest* t = stvlAtPtr(cvars, 0);
    cxuiWidgetSetVisible(t->row, true);
    t->want = (CxUiSize){ 200, 94 };
    return true;
}

static bool uiLayCheck(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLayoutTest* t = stvlAtPtr(cvars, 0);
    // In pixels, each edge rounded from DIPs; the window is 400x200 here.
    CxUiRect b[3];
    for (int i = 0; i < 3; i++)
        cxuiTestBounds(CxUiWidget(t->cv[i]), &b[i]);
    int32 px200 = cxuiPx(t->w, 200), px50 = cxuiPx(t->w, 50), px54 = cxuiPx(t->w, 54), px350 = cxuiPx(t->w, 350),
          px400 = cxuiPx(t->w, 400), pxH = cxuiPx(t->w, 200);
    return b[0].x == 0 && b[0].y == 0 && b[0].w == px200 && b[0].h == px50 && b[1].x == 0 && b[1].y == px54 &&
           b[1].w == px350 && b[1].h == pxH - px54 && b[2].x == px350 && b[2].w == px400 - px350;
}

// Canvases nested in boxes in a native window: the window fits them, each is placed and painted
// at its bounds, a resize gives the spare space to the expanding one, a hidden row lowers the
// window's minimum, and shown again its canvases have content of their new sizes. (One whose size
// didn't change keeps its buffer and needn't be painted.)
static int test_cxuid_layout(void)
{
    UI_START_NATIVE();
    UiLayoutTest t = { 0 };
    cxuiCallSync(closureCreate(uiLayBuild, stvar(ptr, &t)));
    bool fitted = uiPollSync(uiLaySettled, &t, timeS(5));
    CxUiSize fit = t.size;
    bool resized = false, placed = false, minimum = false, again = false;
    if (fitted) {
        cxuiCallSync(closureCreate(uiLayResize, stvar(ptr, &t)));
        resized = uiPollSync(uiLaySettled, &t, timeS(5));
        placed  = resized && cxuiCallSync(closureCreate(uiLayCheck, stvar(ptr, &t)));
    }
    if (placed) {
        cxuiCallSync(closureCreate(uiLayHideRow, stvar(ptr, &t)));
        minimum = uiPollSync(uiLaySettled, &t, timeS(5));
    }
    if (minimum) {
        cxuiCallSync(closureCreate(uiLayShowRow, stvar(ptr, &t)));
        again = uiPollSync(uiLaySettled, &t, timeS(5));
    }
    CxUiSize last = t.size;
    cxuiShutdown();
    for (int i = 0; i < 3; i++)
        objRelease(&t.cv[i]);
    objRelease(&t.row);
    objRelease(&t.w);

    if (!fitted)
        TEST_FAIL(1, _SL("the window didn't fit its content: ${int}x${int}, wanted 200x94"), stvar(int32, fit.w),
                  stvar(int32, fit.h));
    if (!resized || !placed)
        TEST_FAIL(1, _SL("after resizing to 400x200: ${int}x${int}, placed as expected ${int}"), stvar(int32, last.w),
                  stvar(int32, last.h), stvar(int32, placed));
    if (!minimum)
        TEST_FAIL(1, _SL("with the row hidden, a window asked to be 10x10 is ${int}x${int}, wanted 200x50"),
                  stvar(int32, last.w), stvar(int32, last.h));
    if (!again)
        TEST_FAIL(1, _SL("the row shown again: ${int}x${int}, canvases painted at ${int} and ${int} wide"),
                  stvar(int32, last.w), stvar(int32, last.h), stvar(int32, t.painted[1].w), stvar(int32, t.painted[2].w));
    return 0;
}

// ---- containers ----

typedef struct UiContTest {
    CxUiWindow* w;
    CxUiZBox* z;
    CxUiCanvas* cv[5];      // in the frame, the zbox's two pages, the split's two panes
    CxUiSize painted[5];
} UiContTest;

// A panel holding a frame holding a canvas, then a zbox of two canvases, then a split of two.
static bool uiContBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiContTest* t   = stvlAtPtr(cvars, 0);
    t->w            = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui containers test", .size = { 300, 240 } });
    CxUiBox* col    = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
    CxUiPanel* pan  = cxuiPanelCreate(&(CxUiPanelDesc){ .margin = { 6, 6, 6, 6 } });
    CxUiFrame* fr   = cxuiFrameCreate(&(CxUiPanelDesc){ .margin = { 3, 3, 3, 3 } });
    t->z            = cxuiZBoxCreate(NULL);
    CxUiSplit* sp   = cxuiSplitCreate(NULL);
    for (int i = 0; i < 5; i++) {
        t->cv[i] = cxuiCanvasCreate(NULL);
        cxuiOnPaint(t->cv[i], uiLayPaint, stvar(ptr, &t->painted[i]));
    }
    cxuiContainerAppend(fr, t->cv[0]);
    cxuiContainerAppend(pan, fr);
    cxuiContainerAppend(t->z, t->cv[1]);
    cxuiContainerAppend(t->z, t->cv[2]);
    cxuiContainerAppend(sp, t->cv[3]);
    cxuiContainerAppend(sp, t->cv[4]);
    cxuiContainerAppend(col, pan);
    cxuiContainerAppend(col, t->z);
    cxuiContainerAppend(col, sp);
    cxuiContainerAppend(t->w, col);
    objRelease(&pan);
    objRelease(&fr);
    objRelease(&sp);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// Every canvas that should be shown is, natively, and painted at its bounds; the others aren't
// shown.
static bool uiContSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiContTest* t = stvlAtPtr(cvars, 0);
    int32 page    = cxuiZBoxActive(t->z);
    for (int i = 0; i < 5; i++) {
        bool want = !(i == 1 && page != 0) && !(i == 2 && page != 1), shown = false;
        if (CxUiWidget(t->cv[i])->peer && (!cxuiTestShown(CxUiWidget(t->cv[i]), &shown) || shown != want))
            return false;
        if (!want)
            continue;
        CxUiRect b;
        if (!shown || !cxuiTestBounds(CxUiWidget(t->cv[i]), &b) || b.w <= 0 || b.w != t->painted[i].w ||
            b.h != t->painted[i].h)
            return false;
    }
    return true;
}

static bool uiContSwitch(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiContTest* t = stvlAtPtr(cvars, 0);
    cxuiZBoxSetActive(t->z, 1);
    return true;
}

#if defined(_PLATFORM_WIN) || defined(_PLATFORM_MACOS)
// A canvas inside a panel and a frame is where layout put it within the window.
static bool uiContPlaced(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiContTest* t = stvlAtPtr(cvars, 0);
    CxUiPoint inFrame, direct;
    CxUiRect b0, b3;
    return cxuiTestCanvasScreenOrigin(t->cv[0], &inFrame) && cxuiTestCanvasScreenOrigin(t->cv[3], &direct) &&
           cxuiTestBounds(CxUiWidget(t->cv[0]), &b0) && cxuiTestBounds(CxUiWidget(t->cv[3]), &b3) &&
           inFrame.x - direct.x == b0.x - b3.x && inFrame.y - direct.y == b0.y - b3.y;
}
#endif

// Canvases inside panels, frames, a zbox and a split on a native window: each is shown and
// painted at its bounds, the zbox shows one page at a time, and (on Windows and macOS, which can
// tell) a canvas inside nested panels is where layout put it.
static int test_cxuid_containers(void)
{
    UI_START_NATIVE();
    UiContTest t = { 0 };
    cxuiCallSync(closureCreate(uiContBuild, stvar(ptr, &t)));
    bool first = uiPollSync(uiContSettled, &t, timeS(5));
    bool placed = true;
#if defined(_PLATFORM_WIN) || defined(_PLATFORM_MACOS)
    placed = first && cxuiCallSync(closureCreate(uiContPlaced, stvar(ptr, &t)));
#endif
    bool switched = false;
    if (first) {
        cxuiCallSync(closureCreate(uiContSwitch, stvar(ptr, &t)));
        switched = uiPollSync(uiContSettled, &t, timeS(5));
    }
    cxuiShutdown();
    for (int i = 0; i < 5; i++)
        objRelease(&t.cv[i]);
    objRelease(&t.z);
    objRelease(&t.w);
    if (!first || !placed || !switched)
        TEST_FAIL(1, _SL("shown and painted ${int}; placed inside the panel ${int}; after switching pages ${int}"),
                  stvar(int32, first), stvar(int32, placed), stvar(int32, switched));
    return 0;
}

// ---- text fields ----

// The find and select-all shortcuts: Ctrl, or Command on macOS, where Ctrl+F and Ctrl+A move the
// caret in a native field
#if defined(_PLATFORM_MACOS)
#define TX_PRIMARY CXUI_MOD_Meta
#else
#define TX_PRIMARY CXUI_MOD_Ctrl
#endif

typedef struct UiTextTest {
    CxUiWindow* w;
    CxUiCanvas* cv;
    CxUiTextField* find;
    CxUiTextField* cmd;
    CxUiTextField* other;
    int32 tabs;         // Tabs the command line's handler took
    int32 cmdKeys;      // keys it saw
    int32 changes;      // edits to the command line
    int32 findChanges;  // edits to the find box
    int32 commits;      // commits of the find box
    int32 finds;        // Ctrl+F at the window
    int32 f3s;          // F3 at the window, which nothing else takes
    bool native;        // keys went through the platform
    CxUiKey key;        // the next key to press
    uint32 mods;
    CxUiWidget* keyAt;
} UiTextTest;

static CxUiResult uiTxCmdKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    UiTextTest* t = stvlAtPtr(cvars, 0);
    t->cmdKeys++;
    if (ev->down && ev->key == CXUI_KEY_Tab && !(ev->mods & ~CXUI_MOD_Shift)) {
        t->tabs++;
        return CXUI_Handled;
    }
    // A letter the field never sees
    return ev->key == CXUI_KEY_B ? CXUI_Handled : CXUI_Default;
}

static CxUiResult uiTxCmdChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((UiTextTest*)stvlAtPtr(cvars, 0))->changes++;
    return CXUI_Default;
}

static CxUiResult uiTxFindChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((UiTextTest*)stvlAtPtr(cvars, 0))->findChanges++;
    return CXUI_Default;
}

static bool uiTxTypeFind(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTextTest* t = stvlAtPtr(cvars, 0);
    cxuiTextFieldSetCaret(t->find, -1);
    return cxuiTestType(t->find, _S"s");
}

static CxUiResult uiTxCommit(stvlist* cvars, CxUiCommitEvent* ev)
{
    unused_noeval(ev);
    ((UiTextTest*)stvlAtPtr(cvars, 0))->commits++;
    return CXUI_Default;
}

static CxUiResult uiTxWinKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    UiTextTest* t = stvlAtPtr(cvars, 0);
    if (ev->down && ev->key == CXUI_KEY_F3)
        t->f3s++;
    if (!ev->down || !cxuiKeyMatches(CXUI_KEY_F, CXUI_MOD_Primary, ev->key, ev->mods))
        return CXUI_Default;
    t->finds++;
    cxuiFocus(t->find);
    cxuiTextFieldSelectAll(t->find);
    return CXUI_Handled;
}

// A canvas, a framed find box, a command line of its own with a key handler that takes Tab, and
// another framed field.
static bool uiTxBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTextTest* t = stvlAtPtr(cvars, 0);
    t->w          = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui text test" });
    CxUiBox* col  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->cv         = cxuiCanvasCreate(NULL);
    cxuiWidgetSetNaturalSize(t->cv, cxuiLen(200), cxuiLen(40));
    t->find  = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = _S"needle", .cue = _S"Find" });
    t->cmd   = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = _S"Command" });
    t->other = cxuiTextFieldCreate(NULL);
    cxuiWidgetSetFont(t->cmd, &(CxUiFontDesc){ .faces = _S"monospace" });
    cxuiOnKey(t->cmd, uiTxCmdKey, stvar(ptr, t));
    cxuiOnChange(t->cmd, uiTxCmdChange, stvar(ptr, t));
    cxuiOnCommit(t->find, uiTxCommit, stvar(ptr, t));
    cxuiOnChange(t->find, uiTxFindChange, stvar(ptr, t));
    cxuiOnKey(t->w, uiTxWinKey, stvar(ptr, t));
    CxUiTextFrame* ff = cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = t->find });
    CxUiTextFrame* of = cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = t->other, .noBorder = true });
    cxuiContainerAppend(col, t->cv);
    cxuiContainerAppend(col, ff);
    cxuiContainerAppend(col, t->cmd);
    cxuiContainerAppend(col, of);
    cxuiContainerAppend(t->w, col);
    objRelease(&ff);
    objRelease(&of);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiTxShown(CxUiTextField* f, bool framed, strref text, strref cue)
{
    CxUiTestTextInfo ti;
    bool shown = false;
    bool ok    = cxuiTestTextInfo(f, &ti) && cxuiTestShown(CxUiWidget(f), &shown) && shown && ti.framed == framed &&
              ti.hasColors == framed && strEq(ti.text, text) && strEq(ti.cue, cue) && ti.enabled && !ti.readOnly;
    cxuiTestTextInfoDestroy(&ti);
    return ok;
}

// Every field is on screen with what it was given, and the window has sized itself around them.
static bool uiTxSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTextTest* t = stvlAtPtr(cvars, 0);
    CxUiRect b;
    return uiTxShown(t->find, true, _S"needle", _S"Find") && uiTxShown(t->cmd, false, NULL, _S"Command") &&
           uiTxShown(t->other, true, NULL, NULL) && cxuiTestBounds(CxUiWidget(t->other), &b) && b.h > 0;
}

static bool uiTxFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiFocus((CxUiWidget*)stvlAtPtr(cvars, 0));
    return true;
}

// The platform has given the widget focus, and the core knows it.
static bool uiTxHasFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWidget* w   = stvlAtPtr(cvars, 0);
    CxUiWindow* win = stvlAtPtr(cvars, 1);
    if (cxuiFocused(win) != w)
        return false;
    CxUiTextField* f = objDynCast(CxUiTextField, w);
    if (!f)
        return true;
    CxUiTestTextInfo ti;
    bool ok = cxuiTestTextInfo(f, &ti) && ti.focused;
    cxuiTestTextInfoDestroy(&ti);
    return ok;
}

// Presses t->key through the platform where it can be driven, else through the core.
static bool uiTxPress(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTextTest* t = stvlAtPtr(cvars, 0);
    if (cxuiTestNativeKey(t->keyAt, t->key, t->mods)) {
        t->native = true;
        return true;
    }
    cxuiTestKey(t->keyAt, t->key, t->mods, true);
    cxuiTestKey(cxuiFocused(t->w) ? cxuiFocused(t->w) : CxUiWidget(t->w), t->key, t->mods, false);
    return true;
}

static bool uiTxReadCmd(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTextTest* t = stvlAtPtr(cvars, 0);
    string* out   = stvlAtPtr(cvars, 1);
    strDup(out, cxuiTextFieldText(t->cmd));
    return true;
}

static bool uiTxActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool active = true;
    cxuiTestWindowActive(((UiTextTest*)stvlAtPtr(cvars, 0))->w, &active);
    return active;
}

static bool uiTxFindSelected(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTextTest* t = stvlAtPtr(cvars, 0);
    CxUiTestTextInfo ti;
    bool ok = cxuiFocused(t->w) == CxUiWidget(t->find) && cxuiTestTextInfo(t->find, &ti) && ti.focused &&
              ti.selStart == 0 && ti.selEnd == 6;
    cxuiTestTextInfoDestroy(&ti);
    return ok;
}

static bool uiTxCaretHome(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiTextFieldSetCaret(((UiTextTest*)stvlAtPtr(cvars, 0))->find, 0);
    return true;
}

static bool uiTxAllSelected(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTextTest* t = stvlAtPtr(cvars, 0);
    CxUiTestTextInfo ti;
    bool ok = cxuiTestTextInfo(t->find, &ti) && ti.selStart == 0 && ti.selEnd > 0 &&
              ti.selEnd == (int32)strLen(cxuiTextFieldText(t->find));
    cxuiTestTextInfoDestroy(&ti);
    return ok;
}

static void uiTxPressSync(UiTextTest* t, void* at, CxUiKey key, uint32 mods)
{
    t->keyAt = at;
    t->key   = key;
    t->mods  = mods;
    cxuiCallSync(closureCreate(uiTxPress, stvar(ptr, t)));
}

static bool uiTxFocusSync(UiTextTest* t, void* w)
{
    cxuiCallSync(closureCreate(uiTxFocus, stvar(ptr, w)));
    return cxuiCallSync(closureCreate(uiTxHasFocus, stvar(ptr, w), stvar(ptr, t->w))) ||
           uiPollSyncWith(uiTxHasFocus, w, t->w, timeS(5));
}

// Whether the window shows its focus cues, after hiding them if asked: 1 or 0, or -1 where the
// platform can't tell
static bool uiTxCues(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow* w = stvlAtPtr(cvars, 0);
    int32* out    = stvlAtPtr(cvars, 2);
    bool shown    = false;
    *out          = cxuiTestFocusCues(w, stvlAt(cvars, 1, int32) != 0, &shown) ? shown : -1;
    return true;
}

static int32 uiTxCuesSync(UiTextTest* t, bool hide)
{
    int32 cues = -1;
    cxuiCallSync(closureCreate(uiTxCues, stvar(ptr, t->w), stvar(int32, hide), stvar(ptr, &cues)));
    return cues;
}

static bool uiTxCounted(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    int32* n = stvlAtPtr(cvars, 0);
    return *n >= stvlAt(cvars, 1, int32);
}

static bool uiTxWaitCount(int32* n, int32 want)
{
    int64 end = clockTimer() + timeS(5);
    while (!cxuiCallSync(closureCreate(uiTxCounted, stvar(ptr, n), stvar(int32, want)))) {
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
    return true;
}

// Native text fields in a window: each shows what it was given; a key handler takes Tab before
// the field and focus navigation see it, leaving the focus and text alone; typing reaches the
// field and raises changes; Tab without a handler moves the focus and shows the platform's focus
// cues; Ctrl+F from a canvas and from
// a field reaches the window, which focuses the find box with its text selected, and the field's
// Ctrl+F character never lands in it; Enter commits. Keys go through the platform where it can be
// driven (Windows, X11), else through the core.
static int test_cxuid_text(void)
{
    UI_START_NATIVE();
    UiTextTest t = { 0 };
    string cmdText = 0;
    cxuiCallSync(closureCreate(uiTxBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiTxSettled, &t, timeS(5));
    bool focused = settled && uiTxFocusSync(&t, t.cmd);
    // A display without a keyboard never activates the window, so nothing in it can have focus.
    bool noKeyboard = settled && !focused && !cxuiCallSync(closureCreate(uiTxActive, stvar(ptr, &t)));

    // Tab, taken by the command line's handler
    bool tabTaken = false, typed = false;
    if (focused) {
        uiTxPressSync(&t, t.cmd, CXUI_KEY_Tab, 0);
        tabTaken = uiTxWaitCount(&t.tabs, 1);
        // Then a letter the handler takes, and one it doesn't, which shows the Tab and the first
        // letter left nothing behind and the field still has focus.
        uiTxPressSync(&t, t.cmd, CXUI_KEY_B, 0);
        uiTxPressSync(&t, t.cmd, CXUI_KEY_A, 0);
        if (t.native) {
            typed = uiTxWaitCount(&t.changes, 1);
            cxuiCallSync(closureCreate(uiTxReadCmd, stvar(ptr, &t), stvar(ptr, &cmdText)));
            typed = typed && strEq(cmdText, _S"a");
        } else {
            typed = true;
        }
        tabTaken = tabTaken && cxuiCallSync(closureCreate(uiTxHasFocus, stvar(ptr, t.cmd), stvar(ptr, t.w)));
    }

    // Tab from the find box, which has no handler, goes on to the command line, and shows the
    // focus cues the platform hid.
    bool navigated = false, cues = false;
    if (focused && uiTxFocusSync(&t, t.find)) {
        int32 hidden = uiTxCuesSync(&t, true);
        uiTxPressSync(&t, t.find, CXUI_KEY_Tab, 0);
        navigated = uiPollSyncWith(uiTxHasFocus, t.cmd, t.w, timeS(5));
        int32 after = uiTxCuesSync(&t, false);
        // Where they couldn't be hidden (Wine shows them always), there is nothing to check.
        cues = hidden != 0 || after == 1;
        TEST_INFO(_SL("focus cues: ${int} hidden, ${int} after Tab"), stvar(int32, hidden), stvar(int32, after));
    }

    // Ctrl+F from the canvas, then from the command line
    bool fromCanvas = false, fromField = false;
    if (focused && uiTxFocusSync(&t, t.cv)) {
        uiTxPressSync(&t, t.cv, CXUI_KEY_F, TX_PRIMARY);
        fromCanvas = uiTxWaitCount(&t.finds, 1) && uiPollSync(uiTxFindSelected, &t, timeS(5));
    }
    if (fromCanvas && uiTxFocusSync(&t, t.cmd)) {
        int32 changes = t.changes;
        uiTxPressSync(&t, t.cmd, CXUI_KEY_F, TX_PRIMARY);
        fromField = uiTxWaitCount(&t.finds, 2) && uiPollSync(uiTxFindSelected, &t, timeS(5)) && t.changes == changes;
    }

    // A key nothing takes reaches the window once, through the field it was pressed in.
    bool once = false;
    if (fromField) {
        uiTxPressSync(&t, t.find, CXUI_KEY_F3, 0);
        once = uiTxWaitCount(&t.f3s, 1);
        uiTxPressSync(&t, t.find, CXUI_KEY_F2, 0);
        // F2 goes the same way; by the time it has, a second F3 would have too.
        osSleep(timeMS(200));
        once = once && t.f3s == 1;
    }

    // Enter in the find box, after typing into it, which a framed field reports through its frame
    bool committed = false, findTyped = false;
    if (fromCanvas) {
        cxuiCallSync(closureCreate(uiTxTypeFind, stvar(ptr, &t)));
        findTyped = uiTxWaitCount(&t.findChanges, 1);
        uiTxPressSync(&t, t.find, CXUI_KEY_Enter, 0);
        committed = uiTxWaitCount(&t.commits, 1);
    }

    // Ctrl+A selects all of it, whether or not the platform's own field does that
    bool selectAll = !t.native;
    if (committed && t.native) {
        cxuiCallSync(closureCreate(uiTxCaretHome, stvar(ptr, &t)));
        uiTxPressSync(&t, t.find, CXUI_KEY_A, TX_PRIMARY);
        selectAll = uiPollSync(uiTxAllSelected, &t, timeS(5));
    }

    cxuiShutdown();
    objRelease(&t.cv);
    objRelease(&t.find);
    objRelease(&t.cmd);
    objRelease(&t.other);
    objRelease(&t.w);
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, t.native));
    if (noKeyboard) {
        strDestroy(&cmdText);
        TEST_SKIP(_SL("the window never became active; only the fields' creation was checked"), stvNone);
    }
    if (!settled || !focused || !tabTaken || !typed || !navigated || !cues || !fromCanvas || !fromField || !once ||
        !findTyped || !committed || !selectAll)
        TEST_FAIL(1, _SLL("shown ${int}, focused ${int}, Tab taken ${int} (${int} keys), typed ${int} \"${string}\", "
                         "navigated ${int}, cues ${int}, Ctrl+F from canvas ${int} and field ${int}, F3 once ${int} (${int}), "
                         "typed in the frame ${int}, committed ${int}, Ctrl+A ${int}"),
                  stvar(int32, settled), stvar(int32, focused), stvar(int32, tabTaken), stvar(int32, t.cmdKeys),
                  stvar(int32, typed), stvar(strref, cmdText), stvar(int32, navigated), stvar(int32, cues), stvar(int32, fromCanvas),
                  stvar(int32, fromField), stvar(int32, once), stvar(int32, t.f3s), stvar(int32, findTyped),
                  stvar(int32, committed), stvar(int32, selectAll));
    strDestroy(&cmdText);
    return 0;
}

// ---- grids ----

typedef struct UiGridTest {
    CxUiWindow* w;
    CxUiGrid* g;
    CxUiListStore* store;
    bool native;   // keys went through the platform
    bool front;    // the platform could say what is in front
} UiGridTest;

static bool uiGrBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t = stvlAtPtr(cvars, 0);
    t->store      = cxuiListStoreCreate(2);
    string name = 0, note = 0;
    for (int32 i = 0; i < 40; i++) {
        strFormat(&name, _S"row ${int}", stvar(int32, i));
        strFormat(&note, _S"note ${int}", stvar(int32, i));
        cxuiListStoreAppend(t->store, name, note);
    }
    strDestroy(&name);
    strDestroy(&note);
    t->g = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .flags = CXUI_COL_Stretch | CXUI_COL_Editable },
                                        { .title = _S"Note", .width = cxuiLen(100), .flags = CXUI_COL_Editable } },
        .ncolumns = 2 });
    cxuiGridSetSource(t->g, t->store);
    t->w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui grid test", .size = { 400, 300 } });
    cxuiContainerAppend(t->w, t->g);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiGrSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t = stvlAtPtr(cvars, 0);
    CxUiRect b;
    return CxUiWidget(t->g->_body)->peer && cxuiTestCanvasEngine(CxUiCanvas(t->g->_body)) &&
           cxuiTestBounds(CxUiWidget(t->g->_body), &b) && b.h > 0;
}

static bool uiGrActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool active = true;
    cxuiTestWindowActive(((UiGridTest*)stvlAtPtr(cvars, 0))->w, &active);
    return active;
}

static bool uiGrEdit(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t = stvlAtPtr(cvars, 0);
    return cxuiGridEdit(t->g, (uint64)stvlAt(cvars, 1, int32), stvlAt(cvars, 2, int32));
}

// The editor is open over a row's cell, in front of the rows, with all its text selected, and has
// focus unless that isn't asked for.
static bool uiGrEditing(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t     = stvlAtPtr(cvars, 0);
    int32 row         = stvlAt(cvars, 1, int32);
    int32 col         = stvlAt(cvars, 2, int32);
    bool focus        = stvlAt(cvars, 3, bool);
    CxUiTextField* ed = cxuiGridEditor(t->g);
    if (!ed || t->g->_editItem != (uint64)row || t->g->_editCol != col)
        return false;
    strref want = cxuiListStoreText(t->store, row, col);
    CxUiTestTextInfo ti;
    bool ok = cxuiTestTextInfo(ed, &ti) && (ti.focused || !focus) && strEq(ti.text, want) && ti.selStart == 0 &&
              ti.selEnd == (int32)strLen(want);
    cxuiTestTextInfoDestroy(&ti);
    CxUiRect b;
    bool front = false;
    if (ok && cxuiTestBounds(CxUiWidget(t->g->_editFrame), &b) &&
        cxuiTestFrontmost(CxUiWidget(t->g->_editFrame), b.x + b.w / 2, b.y + b.h / 2, &front)) {
        t->front = true;
        ok       = front;
    }
    return ok;
}

// The editor is closed, the rows have focus back, and the cell shows want
static bool uiGrClosed(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t = stvlAtPtr(cvars, 0);
    int32 row     = stvlAt(cvars, 1, int32);
    strref want   = stvlAt(cvars, 2, strref);
    return !cxuiGridEditor(t->g) && cxuiFocused(t->w) == CxUiWidget(t->g->_body) &&
           strEq(cxuiListStoreText(t->store, row, 0), want);
}

static bool uiGrKey(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t     = stvlAtPtr(cvars, 0);
    CxUiKey key       = (CxUiKey)stvlAt(cvars, 1, int32);
    CxUiTextField* ed = cxuiGridEditor(t->g);
    if (!ed)
        return false;
    objAcquire(ed);
    if (cxuiTestNativeKey(CxUiWidget(ed), key, 0)) {
        t->native = true;
    } else {
        cxuiTestKey(CxUiWidget(ed), key, 0, true);
        cxuiTestKey(cxuiFocused(t->w) ? cxuiFocused(t->w) : CxUiWidget(t->w), key, 0, false);
    }
    objRelease(&ed);
    return true;
}

static bool uiGrType(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t     = stvlAtPtr(cvars, 0);
    CxUiTextField* ed = cxuiGridEditor(t->g);
    return ed && cxuiTestType(ed, stvlAt(cvars, 1, strref));
}

// Rows away from the editor are the body's, in front of nothing else
static bool uiGrBodyFront(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGridTest* t = stvlAtPtr(cvars, 0);
    CxUiRect b;
    bool front = false;
    return !cxuiTestBounds(CxUiWidget(t->g->_body), &b) ||
           !cxuiTestFrontmost(CxUiWidget(t->g->_body), b.x + b.w / 2, b.y + b.h - 4, &front) || front;
}

static bool uiGrWaitEditing(UiGridTest* t, int32 row, int32 col, bool focus)
{
    int64 end = clockTimer() + timeS(5);
    for (;;) {
        if (cxuiCallSync(closureCreate(uiGrEditing, stvar(ptr, t), stvar(int32, row), stvar(int32, col), stvar(bool, focus))))
            return true;
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
}

static bool uiGrWaitClosed(UiGridTest* t, int32 row, strref want)
{
    int64 end = clockTimer() + timeS(5);
    for (;;) {
        if (cxuiCallSync(closureCreate(uiGrClosed, stvar(ptr, t), stvar(int32, row), stvar(strref, want))))
            return true;
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
}

static bool uiGrEditSync(UiGridTest* t, int32 row, int32 col)
{
    return cxuiCallSync(closureCreate(uiGrEdit, stvar(ptr, t), stvar(int32, row), stvar(int32, col))) &&
           uiGrWaitEditing(t, row, col, true);
}

static void uiGrPress(UiGridTest* t, CxUiKey key)
{
    cxuiCallSync(closureCreate(uiGrKey, stvar(ptr, t), stvar(int32, key)));
}

// The editor over a grid's cell is a native field in front of the rows, focused with its text
// selected; typing and Enter through the platform keep the text, Esc drops it, Tab goes on to the
// next cell; the rows have focus again after.
static int test_cxuid_grid(void)
{
    UI_START_NATIVE();
    UiGridTest t = { 0 };
    cxuiCallSync(closureCreate(uiGrBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiGrSettled, &t, timeS(5));
    // The platform may place things a little after the layout does.
    bool bodyFront = settled && uiPollSync(uiGrBodyFront, &t, timeS(5));
    // Open, in front and showing the cell's text, whether or not the window can give it focus
    bool opened     = settled && cxuiCallSync(closureCreate(uiGrEdit, stvar(ptr, &t), stvar(int32, 3), stvar(int32, 0))) &&
                      uiGrWaitEditing(&t, 3, 0, false);
    bool editing    = opened && uiGrWaitEditing(&t, 3, 0, true);
    bool noKeyboard = opened && !editing && !cxuiCallSync(closureCreate(uiGrActive, stvar(ptr, &t)));

    bool kept = false, dropped = false, tabbed = false;
    if (editing) {
        cxuiCallSync(closureCreate(uiGrType, stvar(ptr, &t), stvar(strref, _S"renamed")));
        uiGrPress(&t, CXUI_KEY_Enter);
        kept = uiGrWaitClosed(&t, 3, _S"renamed");
    }
    if (kept && uiGrEditSync(&t, 4, 0)) {
        cxuiCallSync(closureCreate(uiGrType, stvar(ptr, &t), stvar(strref, _S"dropped")));
        uiGrPress(&t, CXUI_KEY_Escape);
        dropped = uiGrWaitClosed(&t, 4, _S"row 4");
    }
    if (dropped && uiGrEditSync(&t, 5, 0)) {
        uiGrPress(&t, CXUI_KEY_Tab);
        tabbed = uiGrWaitEditing(&t, 5, 1, true);
        uiGrPress(&t, CXUI_KEY_Escape);
    }

    cxuiShutdown();
    objRelease(&t.g);
    objRelease(&t.store);
    objRelease(&t.w);
    TEST_INFO(_SL("keys through the platform: ${int}; front checked: ${int}"), stvar(int32, t.native), stvar(int32, t.front));
    if (!settled || !bodyFront || !opened)
        TEST_FAIL(1, _SL("shown ${int}, rows in front ${int}, editor opened in front ${int}"), stvar(int32, settled),
                  stvar(int32, bodyFront), stvar(int32, opened));
    if (noKeyboard)
        TEST_SKIP(_SL("the window never became active, so only the editor's opening was checked"), stvNone);
    if (!editing || !kept || !dropped || !tabbed)
        TEST_FAIL(1, _SL("focused ${int}, Enter kept ${int}, Esc dropped ${int}, Tab ${int}"), stvar(int32, editing),
                  stvar(int32, kept), stvar(int32, dropped), stvar(int32, tabbed));
    return 0;
}

typedef struct UiGrBoxTest {
    CxUiWindow* w;
    CxUiScrollBox* sb;
    CxUiGrid* g;
    CxUiListStore* store;
    CxUiPoint pos;
    int32 visible;   // pixels of the rows shown in the scroll box
    int64 top;       // the grid's
    int64 end;       // its top scrolled to the end
} UiGrBoxTest;

static bool uiGbBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    t->store         = cxuiListStoreCreate(1);
    string name      = 0;
    for (int32 i = 0; i < 40; i++) {
        strFormat(&name, _S"row ${int}", stvar(int32, i));
        cxuiListStoreAppend(t->store, name);
    }
    strDestroy(&name);
    t->g = cxuiGridCreate(&(CxUiGridDesc){ .lp.size[1] = cxuiLen(300) });
    cxuiGridSetSource(t->g, t->store);
    t->sb = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .view = { 0, 150 }, .gap = 4,
                                                       .margin = { 4, 4, 4, 4 } });
    CxUiLabel* above = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Above the grid" });
    cxuiContainerAppend(t->sb, above);
    objRelease(&above);
    cxuiContainerAppend(t->sb, t->g);
    t->w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui grid in a scroll box test" });
    cxuiContainerAppend(t->w, t->sb);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// Placed with its top part shown and the rest below the scroll box's view
static bool uiGbSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    CxUiRect sb, body;
    if (!CxUiWidget(t->g->_body)->peer || !cxuiTestCanvasEngine(CxUiCanvas(t->g->_body)) ||
        !cxuiTestBounds(CxUiWidget(t->sb), &sb) || !cxuiTestBounds(CxUiWidget(t->g->_body), &body) ||
        !cxuiTestScrollPos(t->sb, &t->pos))
        return false;
    t->visible = sb.y + sb.h - body.y;
    return t->pos.y == 0 && t->visible > 20 && body.y + body.h > sb.y + sb.h;
}

static bool uiGbClick(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    return cxuiTestNativeClick(CxUiCanvas(t->g->_body), 20, t->visible / 2);
}

static bool uiGbFocused(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    return cxuiFocused(t->w) == CxUiWidget(t->g->_body) && cxuiTestScrollPos(t->sb, &t->pos);
}

static bool uiGbPos(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    t->top         = cxuiGridTop(t->g);
    return cxuiTestScrollPos(t->sb, &t->pos);
}

// A new turn of the wheel over the rows, from the top or the end
static bool uiGbWheel(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    cxuiGridSetTop(t->g, stvlAt(cvars, 1, bool) ? INT64_MAX : 0);
    t->end = cxuiGridTop(t->g);
    cxuiTestWheelPause();
    return cxuiTestNativeWheel(CxUiCanvas(t->g->_body), 20, t->visible / 2, stvlAt(cvars, 2, int32));
}

// The rows scrolled a notch and the box didn't
static bool uiGbRowsTook(stvlist* cvars, stvlist* args)
{
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    return uiGbPos(cvars, args) && t->top > 0 && t->pos.y == 0;
}

// The box scrolled and the rows didn't
static bool uiGbBoxTook(stvlist* cvars, stvlist* args)
{
    UiGrBoxTest* t = stvlAtPtr(cvars, 0);
    return uiGbPos(cvars, args) && t->top == t->end && t->pos.y > 0;
}

// A grid partly below a scroll box's view, through the platform: a click focuses it without
// scrolling the box; the wheel scrolls its rows while they can, and is passed on to the box when
// they can't.
static int test_cxuid_gridbox(void)
{
    UI_START_NATIVE();
    UiGrBoxTest t = { 0 };
    cxuiCallSync(closureCreate(uiGbBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiGbSettled, &t, timeS(5));
    bool native  = settled && cxuiCallSync(closureCreate(uiGbClick, stvar(ptr, &t)));
    bool focused = native && uiPollSync(uiGbFocused, &t, timeS(5));
    // Anything the focus would scroll has had time to.
    int32 moved = 0;
    for (int32 i = 0; focused && i < 20 && !moved; i++) {
        osSleep(timeMS(10));
        if (cxuiCallSync(closureCreate(uiGbPos, stvar(ptr, &t))))
            moved = t.pos.y;
    }
    bool wheel = focused && !moved &&
                 cxuiCallSync(closureCreate(uiGbWheel, stvar(ptr, &t), stvar(bool, false), stvar(int32, 1)));
    bool rows  = wheel && uiPollSync(uiGbRowsTook, &t, timeS(5));
    bool box   = rows && cxuiCallSync(closureCreate(uiGbWheel, stvar(ptr, &t), stvar(bool, true), stvar(int32, 2))) &&
               uiPollSync(uiGbBoxTook, &t, timeS(5));

    cxuiShutdown();
    objRelease(&t.g);
    objRelease(&t.store);
    objRelease(&t.sb);
    objRelease(&t.w);
    if (!settled)
        TEST_FAIL(1, _SL("the grid was never placed partly shown at the top"), stvNone);
    if (!native)
        TEST_SKIP(_SL("the platform can't click a canvas for a test"), stvNone);
    if (!focused || moved)
        TEST_FAIL(1, _SL("focused ${int}, scrolled to ${int}"), stvar(int32, focused), stvar(int32, moved));
    if (!wheel)
        TEST_SKIP(_SL("the platform can't turn the wheel over a canvas for a test"), stvNone);
    if (!rows || !box)
        TEST_FAIL(1, _SL("the wheel scrolled the rows ${int}, then the box ${int} (rows at ${int} of ${int}, box at ${int})"),
                  stvar(int32, rows), stvar(int32, box), stvar(int64, t.top), stvar(int64, t.end), stvar(int32, t.pos.y));
    return 0;
}

#if defined(_PLATFORM_WIN)
static bool uiSysPanel(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiPalette* light = cxuiPaletteLight();
    *(CxUiColor*)stvlAtPtr(cvars, 0) = cxuiPaletteColor(light, CXUI_PAL_Panel);
    objRelease(&light);
    return true;
}
#endif

// Windows and panels in the light theme take the platform's dialog color where it has one.
static int test_cxuid_syscolors(void)
{
#if defined(_PLATFORM_WIN)
    UI_START_NATIVE();
    CxUiColor panel = { 0 };
    cxuiCallSync(closureCreate(uiSysPanel, stvar(ptr, &panel)));
    cxuiShutdown();
    COLORREF face = GetSysColor(COLOR_3DFACE);
    if (panel.bits != cxuiRGB(GetRValue(face), GetGValue(face), GetBValue(face)).bits)
        TEST_FAIL(1, _SL("the light palette's panels are ${uint(hex)}, the system's dialogs 0x${uint(hex)}"),
                  stvar(uint32, panel.bits), stvar(uint32, (uint32)face));
    return 0;
#else
    TEST_SKIP(_SL("the light palette keeps cxui's colors here"), stvNone);
#endif
}

// ---- labels and buttons ----

typedef struct UiCtlTest {
    CxUiWindow* w;
    CxUiLabel* label;
    CxUiLabel* para;   // wraps
    CxUiTextField* name;
    CxUiTextField* other;
    CxUiButton* ok;
    CxUiButton* cancel;
    CxUiButton* go;
    int32 oks, cancels, goes;
    int32 goKeys;  // keys Go's own handler saw
    CxUiRect paraFirst;
    bool native;   // keys went through the platform
    string why;
} UiCtlTest;

static CxUiResult uiCtlCount(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return CXUI_Default;
}

static CxUiResult uiCtlKeySeen(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(ev);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return CXUI_Default;
}

static bool uiCtlBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCtlTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui controls test" });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 6 });
    t->label     = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Name:" });
    t->name      = cxuiTextFieldCreate(NULL);
    t->other     = cxuiTextFieldCreate(NULL);
    t->ok        = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"OK" });
    t->cancel    = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Cancel" });
    t->go        = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Go" });
    cxuiOnActivate(t->ok, uiCtlCount, stvar(ptr, &t->oks));
    cxuiOnActivate(t->cancel, uiCtlCount, stvar(ptr, &t->cancels));
    cxuiOnActivate(t->go, uiCtlCount, stvar(ptr, &t->goes));
    cxuiOnKey(t->go, uiCtlKeySeen, stvar(ptr, &t->goKeys));
    t->para = cxuiLabelCreate(&(CxUiLabelDesc){
        .text = _S"A label long enough to need several lines at its natural width, which it breaks at words.",
        .wrap = true, .columns = 20 });
    cxuiContainerAppend(col, t->para);
    cxuiContainerAppend(col, t->label);
    cxuiContainerAppend(col, t->name);
    cxuiContainerAppend(col, t->other);
    cxuiContainerAppend(row, t->go);
    cxuiContainerAppend(row, t->ok);
    cxuiContainerAppend(row, t->cancel);
    cxuiContainerAppend(col, row);
    cxuiContainerAppend(t->w, col);
    objRelease(&row);
    objRelease(&col);
    cxuiWindowSetDefaultEnter(t->w, t->ok);
    cxuiWindowSetDefaultEsc(t->w, t->cancel);
    cxuiWindowSetStartFocus(t->w, t->other);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// The wrapped label takes more than one line, and is at least as tall as the platform needs for
// its text at the width it has.
static void uiCtlWrapFits(UiCtlTest* t, string* why)
{
    CxUiTestControlInfo pi = { 0 };
    CxUiRect one;
    if (!cxuiTestControlInfo(CxUiWidget(t->para), &pi) || !cxuiTestBounds(CxUiWidget(t->label), &one))
        strCopy(why, _S"the wrapped label couldn't be read back");
    else if (pi.neededHeight <= 0 || pi.bounds.h < pi.neededHeight || pi.bounds.h < one.h * 2)
        strFormat(why, _SL("the wrapped label is ${int}x${int} and needs ${int} down, a line being ${int}"),
                  stvar(int32, pi.bounds.w), stvar(int32, pi.bounds.h), stvar(int32, pi.neededHeight), stvar(int32, one.h));
    cxuiTestControlInfoDestroy(&pi);
}

static bool uiCtlWiden(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCtlTest* t = stvlAtPtr(cvars, 0);
    CxUiSize sz;
    cxuiWindowSize(t->w, &sz);
    cxuiWindowSetSize(t->w, sz.w + 200, sz.h);
    return true;
}

// Wider, the label needs fewer lines, and still fits them.
static bool uiCtlWidened(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCtlTest* t = stvlAtPtr(cvars, 0);
    CxUiTestControlInfo pi = { 0 };
    bool ok = cxuiTestControlInfo(CxUiWidget(t->para), &pi) && pi.bounds.w > t->paraFirst.w &&
              pi.bounds.h < t->paraFirst.h && pi.neededHeight > 0 && pi.bounds.h >= pi.neededHeight;
    strClear(&t->why);
    if (!ok)
        strFormat(&t->why, _SL("widened, the wrapped label is ${int}x${int} (was ${int}x${int}) and needs ${int}"),
                  stvar(int32, pi.bounds.w), stvar(int32, pi.bounds.h), stvar(int32, t->paraFirst.w),
                  stvar(int32, t->paraFirst.h), stvar(int32, pi.neededHeight));
    cxuiTestControlInfoDestroy(&pi);
    return ok;
}

static bool uiCtlParaBounds(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCtlTest* t = stvlAtPtr(cvars, 0);
    return cxuiTestBounds(CxUiWidget(t->para), &t->paraFirst);
}

// The platform shows each with its mnemonic its own way, the default as the default, and a button
// at least as big as its title.
static bool uiCtlSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCtlTest* t = stvlAtPtr(cvars, 0);
#if defined(_PLATFORM_WIN)
    strref goShown = _S"&Go", nameShown = _S"&Name:";
#else
    strref goShown = _S"Go", nameShown = _S"Name:";
#endif
    CxUiTestControlInfo li = { 0 }, gi = { 0 }, oi = { 0 };
    bool shown = false;
    bool ok    = cxuiTestControlInfo(CxUiWidget(t->label), &li) && cxuiTestControlInfo(CxUiWidget(t->go), &gi) &&
              cxuiTestControlInfo(CxUiWidget(t->ok), &oi) && cxuiTestShown(CxUiWidget(t->go), &shown) && shown;
    strClear(&t->why);
    if (!ok)
        strCopy(&t->why, _S"a control's state couldn't be read back, or it isn't shown");
    else if (!strEq(li.text, nameShown) || !strEq(gi.text, goShown))
        strFormat(&t->why, _SL("shown as \"${string}\" and \"${string}\""), stvar(string, li.text), stvar(string, gi.text));
    else if (!oi.isDefault || gi.isDefault)
        strFormat(&t->why, _SL("the default shows as such ${int}, another ${int}"), stvar(int32, oi.isDefault),
                  stvar(int32, gi.isDefault));
    cxuiTestControlInfoDestroy(&li);
    cxuiTestControlInfoDestroy(&gi);
    cxuiTestControlInfoDestroy(&oi);
    CxUiRect b;
    CxUiFont* f = cxuiWidgetFont(t->cancel);
    int32 title = cxuiFontAdvance(f, _S"Cancel");
    CxUiSize n  = cxuiNaturalSize(t->cancel);
    if (strEmpty(t->why) && (!cxuiTestBounds(CxUiWidget(t->cancel), &b) || b.w <= 0 || n.w * cxuiFontScale(f) < title))
        strFormat(&t->why, _SL("the button is ${int} wide for a title ${int} pixels wide"), stvar(int32, n.w),
                  stvar(int32, title));
    if (strEmpty(t->why))
        uiCtlWrapFits(t, &t->why);
    return strEmpty(t->why);
}

static bool uiCtlClick(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestClick(stvlAtPtr(cvars, 0));
}

static bool uiCtlDisable(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWidgetSetEnabled((CxUiWidget*)stvlAtPtr(cvars, 0), stvlAt(cvars, 1, bool));
    cxuiFlush();
    return true;
}

static bool uiCtlNativeEnabled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTestControlInfo info;
    bool enabled = cxuiTestControlInfo((CxUiWidget*)stvlAtPtr(cvars, 0), &info) && info.enabled;
    cxuiTestControlInfoDestroy(&info);
    return enabled;
}

static bool uiCtlFocused(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCtlTest* t  = stvlAtPtr(cvars, 0);
    CxUiWidget* w = stvlAtPtr(cvars, 1);
    return cxuiFocused(t->w) == w;
}

static bool uiCtlFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiFocus((CxUiWidget*)stvlAtPtr(cvars, 0));
    return true;
}

static bool uiCtlActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool active = true;
    cxuiTestWindowActive(((UiCtlTest*)stvlAtPtr(cvars, 0))->w, &active);
    return active;
}

// A key through the platform; false where it can't be driven
static bool uiCtlKey(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiCtlTest* t = stvlAtPtr(cvars, 0);
    if (!cxuiTestNativeKey(stvlAtPtr(cvars, 1), (CxUiKey)stvlAt(cvars, 2, int32), stvlAt(cvars, 3, uint32)))
        return false;
    t->native = true;
    return true;
}

static bool uiCtlPress(UiCtlTest* t, void* at, CxUiKey key, uint32 mods)
{
    return cxuiCallSync(closureCreate(uiCtlKey, stvar(ptr, t), stvar(ptr, at), stvar(int32, key), stvar(uint32, mods)));
}

static bool uiCtlFocusSync(UiCtlTest* t, void* w)
{
    cxuiCallSync(closureCreate(uiCtlFocus, stvar(ptr, w)));
    return uiPollSyncWith(uiCtlFocused, t, w, timeS(5));
}

// Native labels and buttons: each shows its mnemonic the platform's way and the default as the
// default; a click raises activate, a disabled button takes none; the start widget has focus;
// through the platform's keys, Enter and Esc press the defaults, a mnemonic presses its button or
// focuses what follows its label (not on macOS), and Space presses a button with focus.
static int test_cxuid_controls(void)
{
    UI_START_NATIVE();
    UiCtlTest t = { 0 };
    cxuiCallSync(closureCreate(uiCtlBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiCtlSettled, &t, timeS(5));
    bool widened = false;
    if (settled) {
        cxuiCallSync(closureCreate(uiCtlParaBounds, stvar(ptr, &t)));
        cxuiCallSync(closureCreate(uiCtlWiden, stvar(ptr, &t)));
        widened = uiPollSync(uiCtlWidened, &t, timeS(5));
    }
    bool clicked = false, disabled = false, started = false, keys = false;
    if (settled) {
        // Before clicking, which on Windows moves focus to the button
        started = uiPollSyncWith(uiCtlFocused, &t, t.other, timeS(5));
        cxuiCallSync(closureCreate(uiCtlClick, stvar(ptr, t.go)));
        clicked = uiTxWaitCount(&t.goes, 1);
        cxuiCallSync(closureCreate(uiCtlDisable, stvar(ptr, t.go), stvar(bool, false)));
        cxuiCallSync(closureCreate(uiCtlClick, stvar(ptr, t.go)));
        osSleep(timeMS(200));
        disabled = t.goes == 1 && !cxuiCallSync(closureCreate(uiCtlNativeEnabled, stvar(ptr, t.go)));
        cxuiCallSync(closureCreate(uiCtlDisable, stvar(ptr, t.go), stvar(bool, true)));
    }
    bool noKeyboard = settled && !started && !cxuiCallSync(closureCreate(uiCtlActive, stvar(ptr, &t)));

    bool enter = false, esc = false, mnemonics = false, space = false;
    if (started && uiCtlFocusSync(&t, t.other) && uiCtlPress(&t, t.other, CXUI_KEY_Enter, 0)) {
        keys  = true;
        enter = uiTxWaitCount(&t.oks, 1) && t.cancels == 0;
        uiCtlPress(&t, t.other, CXUI_KEY_Escape, 0);
        esc = uiTxWaitCount(&t.cancels, 1) && t.oks == 1;
#if defined(_PLATFORM_MACOS)
        mnemonics = true;
#else
        uiCtlPress(&t, t.other, CXUI_KEY_G, CXUI_MOD_Alt);
        mnemonics = uiTxWaitCount(&t.goes, 2) && uiPollSyncWith(uiCtlFocused, &t, t.other, timeS(5));
        uiCtlPress(&t, t.other, CXUI_KEY_N, CXUI_MOD_Alt);
        mnemonics = mnemonics && uiPollSyncWith(uiCtlFocused, &t, t.name, timeS(5));
#endif
        // Where buttons take focus: Space presses it, and Enter presses it rather than the default.
        if (cxuiWidgetCanFocus(t.go)) {
            int32 goes = t.goes;
            space      = uiCtlFocusSync(&t, t.go);
            uiCtlPress(&t, t.go, CXUI_KEY_Space, 0);
            uiCtlPress(&t, t.go, CXUI_KEY_Enter, 0);
            // The button's handler saw them first.
            space = space && uiTxWaitCount(&t.goes, goes + 2) && t.oks == 1 && t.goKeys >= 2;
        } else {
            space = true;
            TEST_INFO(_SL("buttons don't take focus here"), stvNone);
        }
    }
    cxuiShutdown();
    objRelease(&t.label);
    objRelease(&t.para);
    objRelease(&t.name);
    objRelease(&t.other);
    objRelease(&t.ok);
    objRelease(&t.cancel);
    objRelease(&t.go);
    objRelease(&t.w);
    string why = t.why;
    t.why      = NULL;
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, keys));
    if (!settled || !widened)
        TEST_FAIL(1, _SL("${string}"), stvar(string, why));
    strDestroy(&why);
    if (!clicked || !disabled)
        TEST_FAIL(1, _SL("a click raised activate ${int}; a disabled button took none ${int}"), stvar(int32, clicked),
                  stvar(int32, disabled));
    if (noKeyboard)
        TEST_SKIP(_SL("the window never became active; only showing and clicking were checked"), stvNone);
    if (!started)
        TEST_FAIL(1, _SL("the start widget didn't get focus"), stvNone);
    if (keys && (!enter || !esc || !mnemonics || !space))
        TEST_FAIL(1, _SL("Enter ${int}, Esc ${int}, mnemonics ${int}, Space and Enter on a button ${int} (OK ${int}, "
                         "Cancel ${int}, Go ${int})"),
                  stvar(int32, enter), stvar(int32, esc), stvar(int32, mnemonics), stvar(int32, space), stvar(int32, t.oks),
                  stvar(int32, t.cancels), stvar(int32, t.goes));
    return 0;
}

// ---- check boxes and radio buttons ----

typedef struct UiChkTest {
    CxUiWindow* w;
    CxUiCheck* wrap;    // starts checked
    CxUiCheck* three;   // three-state, mixed from the start
    CxUiRadioGroup* g1;
    CxUiRadioGroup* g2;
    CxUiRadio* a[3];   // in g1; a[1] inside a row
    CxUiRadio* b[2];   // in g2
    bool dark;         // the window's theme; Windows draws them itself there
    int32 wrapChanges, threeChanges, g1Changes, g2Changes;
    strref when;           // what uiChkExpect checks after
    const uint32* want;    // and the states it wants
    string why;
} UiChkTest;

static CxUiResult uiChkCount(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return CXUI_Default;
}

static CxUiRadio* uiChkRadio(void* box, strref title)
{
    CxUiRadio* r = cxuiRadioCreate(&(CxUiRadioDesc){ .title = title });
    cxuiContainerAppend((CxUiWidget*)box, r);
    return r;
}

static bool uiChkBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiChkTest* t = stvlAtPtr(cvars, 0);
    t->w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui check box test",
                                                .theme = t->dark ? CXUI_Theme_Dark : CXUI_Theme_Light });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->wrap      = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"&Wrap long lines", .state = CXUI_CHECK_On });
    t->three = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"&Three states", .state = CXUI_CHECK_Mixed, .threeState = true });
    cxuiOnChange(t->wrap, uiChkCount, stvar(ptr, &t->wrapChanges));
    cxuiOnChange(t->three, uiChkCount, stvar(ptr, &t->threeChanges));
    cxuiContainerAppend(col, t->wrap);
    cxuiContainerAppend(col, t->three);
    CxUiBox* groups = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 16 });
    t->g1           = cxuiRadioGroupCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
    t->g2           = cxuiRadioGroupCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
    t->a[0]         = uiChkRadio(t->g1, _S"&Small");
    CxUiBox* row    = cxuiBoxCreateH();
    t->a[1]         = uiChkRadio(row, _S"&Medium");
    cxuiContainerAppend(t->g1, row);
    objRelease(&row);
    t->a[2] = uiChkRadio(t->g1, _S"&Large");
    t->b[0] = uiChkRadio(t->g2, _S"&Red");
    t->b[1] = uiChkRadio(t->g2, _S"&Blue");
    cxuiOnChange(t->g1, uiChkCount, stvar(ptr, &t->g1Changes));
    cxuiOnChange(t->g2, uiChkCount, stvar(ptr, &t->g2Changes));
    cxuiRadioGroupSetIndex(t->g1, 2);
    cxuiRadioGroupSetIndex(t->g2, 0);
    cxuiContainerAppend(groups, t->g1);
    cxuiContainerAppend(groups, t->g2);
    cxuiContainerAppend(col, groups);
    objRelease(&groups);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowSetStartFocus(t->w, t->wrap);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static uint32 uiChkShown(void* w)
{
    CxUiTestControlInfo info = { 0 };
    uint32 st                = cxuiTestControlInfo((CxUiWidget*)w, &info) ? info.checkState : 99;
    cxuiTestControlInfoDestroy(&info);
    return st;
}

// What each shows, against what it should: wrap, three, a[0..2], b[0..1]
static bool uiChkStates(UiChkTest* t, const uint32 want[7], strref when)
{
    void* ws[7] = { t->wrap, t->three, t->a[0], t->a[1], t->a[2], t->b[0], t->b[1] };
    uint32 got[7];
    bool ok = true;
    for (int32 i = 0; i < 7; i++) {
        got[i] = uiChkShown(ws[i]);
        ok     = ok && got[i] == want[i];
    }
    if (!ok)
        strFormat(&t->why, _SL("${string}: shown ${uint} ${uint} / ${uint} ${uint} ${uint} / ${uint} ${uint}"),
                  stvar(strref, when), stvar(uint32, got[0]), stvar(uint32, got[1]), stvar(uint32, got[2]),
                  stvar(uint32, got[3]), stvar(uint32, got[4]), stvar(uint32, got[5]), stvar(uint32, got[6]));
    return ok;
}

// Each shows the state it was given before it was realized, and is at least as wide as its title.
static bool uiChkSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiChkTest* t = stvlAtPtr(cvars, 0);
    strClear(&t->why);
    static const uint32 want[7] = { CXUI_CHECK_On, CXUI_CHECK_Mixed, 0, 0, CXUI_CHECK_On, CXUI_CHECK_On, 0 };
    if (!uiChkStates(t, want, _S"at first"))
        return false;
    CxUiFont* f = cxuiWidgetFont(t->wrap);
    int32 title = cxuiFontAdvance(f, _S"Wrap long lines");
    CxUiRect b;
    if (!cxuiTestBounds(CxUiWidget(t->wrap), &b) || b.w < title) {
        strFormat(&t->why, _SL("the check box is ${int} wide for a title ${int} pixels wide"), stvar(int32, b.w),
                  stvar(int32, title));
        return false;
    }
    return true;
}

static bool uiChkClick(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestClick(stvlAtPtr(cvars, 0));
}

static bool uiChkClickSync(void* w)
{
    return cxuiCallSync(closureCreate(uiChkClick, stvar(ptr, w)));
}

static bool uiChkExpect(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiChkTest* t = stvlAtPtr(cvars, 0);
    strClear(&t->why);
    return uiChkStates(t, t->want, t->when);
}

static bool uiChkExpectSync(UiChkTest* t, const uint32 want[7], strref when)
{
    t->want = want;
    t->when = when;
    return uiPollSync(uiChkExpect, t, timeS(5));
}

static bool uiChkSetEnabled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWidgetSetEnabled((CxUiWidget*)stvlAtPtr(cvars, 0), stvlAt(cvars, 1, bool));
    cxuiFlush();
    return true;
}

static bool uiChkNativeEnabled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTestControlInfo info;
    bool enabled = cxuiTestControlInfo((CxUiWidget*)stvlAtPtr(cvars, 0), &info) && info.enabled;
    cxuiTestControlInfoDestroy(&info);
    return enabled;
}

static bool uiChkFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiFocus((CxUiWidget*)stvlAtPtr(cvars, 0));
    return true;
}

static bool uiChkFocused(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiChkTest* t = stvlAtPtr(cvars, 0);
    return cxuiFocused(t->w) == (CxUiWidget*)stvlAtPtr(cvars, 1);
}

static bool uiChkKey(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestNativeKey(stvlAtPtr(cvars, 0), (CxUiKey)stvlAt(cvars, 1, int32), stvlAt(cvars, 2, uint32));
}

static bool uiChkPress(void* at, CxUiKey key, uint32 mods)
{
    return cxuiCallSync(closureCreate(uiChkKey, stvar(ptr, at), stvar(int32, key), stvar(uint32, mods)));
}

static bool uiChkActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool active = true;
    cxuiTestWindowActive(((UiChkTest*)stvlAtPtr(cvars, 0))->w, &active);
    return active;
}

static bool uiChkCanFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiWidgetCanFocus((CxUiWidget*)stvlAtPtr(cvars, 0));
}

// Native check boxes and radio buttons: each shows the state it was given before it existed; a
// click changes it the way the core says, a radio's click clearing the others in its group and
// none in the group beside it; a disabled one takes no click and is disabled natively; through the
// platform's keys, Space changes a check box with focus, Down chooses the next radio, and Alt with a
// mnemonic chooses its radio (not on macOS). In a light window, then a dark one.
static int uiChkRun(bool dark)
{
    UI_START_NATIVE();
    UiChkTest t = { .dark = dark };
    cxuiCallSync(closureCreate(uiChkBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiChkSettled, &t, timeS(5));
    bool clicks = false, disabled = false, keys = false, keyed = false;
    if (settled) {
        static const uint32 afterA0[7]   = { CXUI_CHECK_On, CXUI_CHECK_Mixed, CXUI_CHECK_On, 0, 0, CXUI_CHECK_On, 0 };
        static const uint32 afterB1[7]   = { CXUI_CHECK_On, CXUI_CHECK_Mixed, CXUI_CHECK_On, 0, 0, 0, CXUI_CHECK_On };
        static const uint32 afterWrap[7] = { 0, CXUI_CHECK_Mixed, CXUI_CHECK_On, 0, 0, 0, CXUI_CHECK_On };
        static const uint32 afterThree[7] = { 0, 0, CXUI_CHECK_On, 0, 0, 0, CXUI_CHECK_On };
        uiChkClickSync(t.a[0]);
        clicks = uiChkExpectSync(&t, afterA0, _S"a click on Small");
        uiChkClickSync(t.a[0]);
        uiChkClickSync(t.b[1]);
        clicks = clicks && uiChkExpectSync(&t, afterB1, _S"a click on Blue");
        uiChkClickSync(t.wrap);
        clicks = clicks && uiChkExpectSync(&t, afterWrap, _S"a click on Wrap");
        uiChkClickSync(t.three);
        clicks = clicks && uiChkExpectSync(&t, afterThree, _S"a click on Three");
        clicks = clicks && uiTxWaitCount(&t.g1Changes, 1) && uiTxWaitCount(&t.g2Changes, 1) &&
                 uiTxWaitCount(&t.wrapChanges, 1) && uiTxWaitCount(&t.threeChanges, 1);

        cxuiCallSync(closureCreate(uiChkSetEnabled, stvar(ptr, t.wrap), stvar(bool, false)));
        uiChkClickSync(t.wrap);
        osSleep(timeMS(200));
        disabled = t.wrapChanges == 1 && uiChkShown(t.wrap) == CXUI_CHECK_Off &&
                   !cxuiCallSync(closureCreate(uiChkNativeEnabled, stvar(ptr, t.wrap)));
        cxuiCallSync(closureCreate(uiChkSetEnabled, stvar(ptr, t.wrap), stvar(bool, true)));

        // Keys, where the window takes them and check boxes take focus
        if (cxuiCallSync(closureCreate(uiChkActive, stvar(ptr, &t))) &&
            cxuiCallSync(closureCreate(uiChkCanFocus, stvar(ptr, t.wrap)))) {
            cxuiCallSync(closureCreate(uiChkFocus, stvar(ptr, t.wrap)));
            if (uiPollSyncWith(uiChkFocused, &t, t.wrap, timeS(5)) && uiChkPress(t.wrap, CXUI_KEY_Space, 0)) {
                keys = true;
                static const uint32 spaced[7] = { CXUI_CHECK_On, 0, CXUI_CHECK_On, 0, 0, 0, CXUI_CHECK_On };
                static const uint32 downed[7] = { CXUI_CHECK_On, 0, 0, CXUI_CHECK_On, 0, 0, CXUI_CHECK_On };
                static const uint32 named[7]  = { CXUI_CHECK_On, 0, 0, CXUI_CHECK_On, 0, CXUI_CHECK_On, 0 };
                keyed = uiChkExpectSync(&t, spaced, _S"Space on Wrap") && uiTxWaitCount(&t.wrapChanges, 2);
                cxuiCallSync(closureCreate(uiChkFocus, stvar(ptr, t.a[0])));
                keyed = keyed && uiPollSyncWith(uiChkFocused, &t, t.a[0], timeS(5));
                uiChkPress(t.a[0], CXUI_KEY_Down, 0);
                keyed = keyed && uiChkExpectSync(&t, downed, _S"Down from Small") &&
                        uiPollSyncWith(uiChkFocused, &t, t.a[1], timeS(5));
#if !defined(_PLATFORM_MACOS)
                uiChkPress(t.a[1], CXUI_KEY_R, CXUI_MOD_Alt);
                keyed = keyed && uiChkExpectSync(&t, named, _S"Alt+R") && uiPollSyncWith(uiChkFocused, &t, t.b[0], timeS(5));
#else
                unused_noeval(named);
#endif
            }
        }
    }
    cxuiShutdown();
    objRelease(&t.wrap);
    objRelease(&t.three);
    objRelease(&t.g1);
    objRelease(&t.g2);
    for (int32 i = 0; i < 3; i++)
        objRelease(&t.a[i]);
    for (int32 i = 0; i < 2; i++)
        objRelease(&t.b[i]);
    objRelease(&t.w);
    string why = t.why;
    t.why      = NULL;
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, keys));
    if (!settled || !clicks) {
        logFmt(Error, _SL("dark ${int}: shown ${int}, clicks ${int} (changes ${int} ${int} ${int} ${int}): ${string}"),
               stvar(int32, dark), stvar(int32, settled), stvar(int32, clicks), stvar(int32, t.wrapChanges), stvar(int32, t.threeChanges),
               stvar(int32, t.g1Changes), stvar(int32, t.g2Changes), stvar(string, why));
        strDestroy(&why);
        return 1;
    }
    strDestroy(&why);
    if (!disabled)
        TEST_FAIL(1, _SL("a disabled check box took a click, or isn't disabled natively"), stvNone);
    if (keys && !keyed)
        TEST_FAIL(1, _SL("keys: Space, Down or a mnemonic didn't change what they should (wrap ${int}, groups ${int} ${int})"),
                  stvar(int32, t.wrapChanges), stvar(int32, t.g1Changes), stvar(int32, t.g2Changes));
    return 0;
}

static int test_cxuid_checks(void)
{
    int r = uiChkRun(false);
    if (r == 0)
        r = uiChkRun(true);
    return r;
}

// ---- lists and dropdowns ----

typedef struct UiLstTest {
    CxUiWindow* w;
    CxUiList* list;
    CxUiDropdown* drop;
    CxUiButton* ok;
    int32 listChanges, dropChanges, activates, oks;
    uint64 item;
    // What uiLstExpect wants: each one's items and selection
    strref listItems, dropItems;
    int32 listValue, dropValue;
    string why;
} UiLstTest;

static CxUiResult uiLstChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return CXUI_Default;
}

static CxUiResult uiLstActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    UiLstTest* t = stvlAtPtr(cvars, 0);
    t->activates++;
    t->item = ev->item;
    return CXUI_Default;
}

static CxUiResult uiLstOk(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    ((UiLstTest*)stvlAtPtr(cvars, 0))->oks++;
    return CXUI_Default;
}

static bool uiLstBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    static const strref planets[] = { _S"Mercury", _S"Venus", _S"Earth", _S"Mars", NULL };
    static const strref sizes[]   = { _S"Small", _S"Medium", _S"Large", NULL };
    UiLstTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui list test" });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->list      = cxuiListCreate(&(CxUiListDesc){ .items = planets, .value = 1, .visibleItems = 6 });
    t->drop      = cxuiDropdownCreate(&(CxUiDropdownDesc){ .items = sizes, .value = 2 });
    t->ok        = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"OK" });
    cxuiListAppend(t->list, _S"Jupiter");
    cxuiOnChange(t->list, uiLstChange, stvar(ptr, &t->listChanges));
    cxuiOnChange(t->drop, uiLstChange, stvar(ptr, &t->dropChanges));
    cxuiOnActivate(t->list, uiLstActivate, stvar(ptr, t));
    cxuiOnActivate(t->ok, uiLstOk, stvar(ptr, t));
    cxuiContainerAppend(col, t->list);
    cxuiContainerAppend(col, t->drop);
    cxuiContainerAppend(col, t->ok);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowSetDefaultEnter(t->w, t->ok);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiLstExpect(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLstTest* t           = stvlAtPtr(cvars, 0);
    CxUiTestControlInfo li = { 0 }, di = { 0 };
    bool ok = cxuiTestControlInfo(CxUiWidget(t->list), &li) && cxuiTestControlInfo(CxUiWidget(t->drop), &di) &&
              strEq(li.items, t->listItems) && li.value == t->listValue && strEq(di.items, t->dropItems) &&
              di.value == t->dropValue;
    strClear(&t->why);
    if (!ok)
        strFormat(&t->why, _SL("list \"${string}\" at ${int}, dropdown \"${string}\" at ${int}"),
                  stvar(strref, li.items), stvar(int32, li.value), stvar(strref, di.items), stvar(int32, di.value));
    cxuiTestControlInfoDestroy(&li);
    cxuiTestControlInfoDestroy(&di);
    return ok;
}

static bool uiLstExpectSync(UiLstTest* t, strref listItems, int32 listValue, strref dropItems, int32 dropValue)
{
    t->listItems = listItems;
    t->listValue = listValue;
    t->dropItems = dropItems;
    t->dropValue = dropValue;
    return uiPollSync(uiLstExpect, t, timeS(5));
}

// The list taller than the dropdown, and each at least as wide as its widest item
static bool uiLstSized(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLstTest* t = stvlAtPtr(cvars, 0);
    CxUiRect lb, db;
    CxUiFont* f  = cxuiWidgetFont(t->list);
    bool ok      = cxuiTestBounds(CxUiWidget(t->list), &lb) && cxuiTestBounds(CxUiWidget(t->drop), &db) &&
              lb.h > db.h * 3 && lb.w >= cxuiFontAdvance(f, _S"Mercury") && db.w >= cxuiFontAdvance(f, _S"Medium");
    if (!ok)
        strFormat(&t->why, _SL("the list is ${int}x${int}, the dropdown ${int}x${int}"), stvar(int32, lb.w),
                  stvar(int32, lb.h), stvar(int32, db.w), stvar(int32, db.h));
    return ok;
}

static bool uiLstEdit(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLstTest* t = stvlAtPtr(cvars, 0);
    cxuiListInsert(t->list, 0, _S"Sun");
    cxuiListRemove(t->list, 3);
    cxuiListSetItem(t->list, 1, _S"MERCURY");
    cxuiDropdownRemove(t->drop, 0);
    cxuiDropdownAppend(t->drop, _S"Huge");
    cxuiListShow(t->list, 4);
    return true;
}

static bool uiLstChoose(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestChoose(stvlAtPtr(cvars, 0), stvlAt(cvars, 1, int32), stvlAt(cvars, 2, bool));
}

static void uiLstChooseSync(void* w, int32 index, bool activate)
{
    cxuiCallSync(closureCreate(uiLstChoose, stvar(ptr, w), stvar(int32, index), stvar(bool, activate)));
}

static bool uiLstEnable(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWidgetSetEnabled((CxUiWidget*)stvlAtPtr(cvars, 0), stvlAt(cvars, 1, bool));
    cxuiFlush();
    return true;
}

static bool uiLstNativeEnabled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTestControlInfo info;
    bool on = cxuiTestControlInfo((CxUiWidget*)stvlAtPtr(cvars, 0), &info) && info.enabled;
    cxuiTestControlInfoDestroy(&info);
    return on;
}

static bool uiLstActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool active = true;
    cxuiTestWindowActive(((UiLstTest*)stvlAtPtr(cvars, 0))->w, &active);
    return active;
}

static bool uiLstFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiFocus((CxUiWidget*)stvlAtPtr(cvars, 0));
    return true;
}

static bool uiLstFocused(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLstTest* t = stvlAtPtr(cvars, 0);
    return cxuiFocused(t->w) == (CxUiWidget*)stvlAtPtr(cvars, 1);
}

static bool uiLstKey(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestNativeKey(stvlAtPtr(cvars, 0), (CxUiKey)stvlAt(cvars, 1, int32), 0);
}

static bool uiLstPress(void* at, CxUiKey key)
{
    return cxuiCallSync(closureCreate(uiLstKey, stvar(ptr, at), stvar(int32, key)));
}

// Native lists and dropdowns: items and a selection given before they exist are shown, and so are
// later edits, with the selection kept on its item; the user's choice raises one change, a double
// click an activate; disabled, a list takes no choice; through the platform's keys, Down moves a
// list's selection with a change, Enter activates it, and Enter on a dropdown presses the default.
static int test_cxuid_lists(void)
{
    UI_START_NATIVE();
    UiLstTest t = { 0 };
    cxuiCallSync(closureCreate(uiLstBuild, stvar(ptr, &t)));
    bool shown = uiLstExpectSync(&t, _S"Mercury\nVenus\nEarth\nMars\nJupiter\n", 1, _S"Small\nMedium\nLarge\n", 2) &&
                 uiPollSync(uiLstSized, &t, timeS(5));
    bool edited = false, chosen = false, disabled = false, keys = false, keyed = false;
    if (shown) {
        cxuiCallSync(closureCreate(uiLstEdit, stvar(ptr, &t)));
        edited = uiLstExpectSync(&t, _S"Sun\nMERCURY\nVenus\nMars\nJupiter\n", 2, _S"Medium\nLarge\nHuge\n", 1);
    }
    if (edited) {
        uiLstChooseSync(t.list, 3, false);
        uiLstChooseSync(t.drop, 2, false);
        chosen = uiLstExpectSync(&t, _S"Sun\nMERCURY\nVenus\nMars\nJupiter\n", 3, _S"Medium\nLarge\nHuge\n", 2) &&
                 uiTxWaitCount(&t.listChanges, 1) && uiTxWaitCount(&t.dropChanges, 1);
        uiLstChooseSync(t.list, 0, true);
        chosen = chosen && uiTxWaitCount(&t.activates, 1) && t.item == 0 && uiTxWaitCount(&t.listChanges, 2);
        cxuiCallSync(closureCreate(uiLstEnable, stvar(ptr, t.list), stvar(bool, false)));
        uiLstChooseSync(t.list, 4, false);
        osSleep(timeMS(200));
        disabled = t.listChanges == 2 && !cxuiCallSync(closureCreate(uiLstNativeEnabled, stvar(ptr, t.list)));
        cxuiCallSync(closureCreate(uiLstEnable, stvar(ptr, t.list), stvar(bool, true)));
    }
    if (chosen && cxuiCallSync(closureCreate(uiLstActive, stvar(ptr, &t)))) {
        cxuiCallSync(closureCreate(uiLstFocus, stvar(ptr, t.list)));
        if (uiPollSyncWith(uiLstFocused, &t, t.list, timeS(5)) && uiLstPress(t.list, CXUI_KEY_Down)) {
            keys  = true;
            keyed = uiLstExpectSync(&t, _S"Sun\nMERCURY\nVenus\nMars\nJupiter\n", 1, _S"Medium\nLarge\nHuge\n", 2) &&
                    uiTxWaitCount(&t.listChanges, 3);
            uiLstPress(t.list, CXUI_KEY_Enter);
            keyed = keyed && uiTxWaitCount(&t.activates, 2) && t.item == 1 && t.oks == 0;
            if (cxuiWidgetCanFocus(t.drop)) {
                cxuiCallSync(closureCreate(uiLstFocus, stvar(ptr, t.drop)));
                keyed = keyed && uiPollSyncWith(uiLstFocused, &t, t.drop, timeS(5));
                uiLstPress(t.drop, CXUI_KEY_Enter);
                keyed = keyed && uiTxWaitCount(&t.oks, 1);
            }
        }
    }
    cxuiShutdown();
    objRelease(&t.list);
    objRelease(&t.drop);
    objRelease(&t.ok);
    objRelease(&t.w);
    string why = t.why;
    t.why      = NULL;
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, keys));
    if (!shown || !edited || !chosen) {
        logFmt(Error, _SL("shown ${int}, edited ${int}, chosen ${int} (changes ${int} ${int}, activates ${int}): ${string}"),
               stvar(int32, shown), stvar(int32, edited), stvar(int32, chosen), stvar(int32, t.listChanges),
               stvar(int32, t.dropChanges), stvar(int32, t.activates), stvar(string, why));
        strDestroy(&why);
        return 1;
    }
    strDestroy(&why);
    if (!disabled)
        TEST_FAIL(1, _SL("a disabled list took a choice, or isn't disabled natively"), stvNone);
    if (keys && !keyed)
        TEST_FAIL(1, _SL("keys: changes ${int}, activates ${int} (item ${uint}), OK ${int}"), stvar(int32, t.listChanges),
                  stvar(int32, t.activates), stvar(uint64, t.item), stvar(int32, t.oks));
    return 0;
}

// ---- lists where any number can be selected ----

typedef struct UiLmTest {
    CxUiWindow* w;
    CxUiList* list;
    int32 changes, activates, deletes;
    uint64 item;
    strref selection;   // what the platform should show selected
    int32 top;          // and as its first item, or -1 for any
    int32 count;        // and how many items
    string why;
} UiLmTest;

static CxUiResult uiLmChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((UiLmTest*)stvlAtPtr(cvars, 0))->changes++;
    return CXUI_Default;
}

static CxUiResult uiLmActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    UiLmTest* t = stvlAtPtr(cvars, 0);
    t->activates++;
    t->item = ev->item;
    return CXUI_Default;
}

static CxUiResult uiLmKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    if (ev->key != CXUI_KEY_Delete || !ev->down)
        return CXUI_Default;
    ((UiLmTest*)stvlAtPtr(cvars, 0))->deletes++;
    return CXUI_Handled;
}

// Fills the list with n items, held back from the platform until done, selecting two
static void uiLmFill(UiLmTest* t, int32 n, strref prefix, int32 sel)
{
    string text = 0;
    cxuiListBeginUpdate(t->list);
    cxuiListClear(t->list);
    for (int32 i = 0; i < n; i++) {
        strFormat(&text, _S"${string} ${int}", stvar(strref, prefix), stvar(int32, i));
        cxuiListAppend(t->list, text);
    }
    cxuiListSetSelected(t->list, sel, 1, true);
    cxuiListSetSelected(t->list, sel + 2, 1, true);
    cxuiListEndUpdate(t->list);
    strDestroy(&text);
}

static bool uiLmBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLmTest* t  = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui multiple list test" });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->list      = cxuiListCreate(&(CxUiListDesc){ .select = CXUI_SELECT_Multi, .visibleItems = 6, .columns = 20 });
    cxuiOnChange(t->list, uiLmChange, stvar(ptr, t));
    cxuiOnActivate(t->list, uiLmActivate, stvar(ptr, t));
    cxuiOnKey(t->list, uiLmKey, stvar(ptr, t));
    uiLmFill(t, 200, _S"Item", 3);
    cxuiContainerAppend(col, t->list);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiLmExpect(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLmTest* t            = stvlAtPtr(cvars, 0);
    CxUiTestControlInfo li = { 0 };
    bool ok                = cxuiTestControlInfo(CxUiWidget(t->list), &li);
    int32 lines            = 0;
    for (int32 i = 0; i < (int32)strLen(li.items); i++)
        lines += strGetChar(li.items, i) == '\n';
    ok = ok && strEq(li.selection, t->selection) && lines == t->count && (t->top < 0 || li.top == t->top) &&
         cxuiListTopItem(t->list) == li.top;
    strClear(&t->why);
    if (!ok)
        strFormat(&t->why, _SL("${int} items, \"${string}\" selected, ${int} at the top"), stvar(int32, lines),
                  stvar(strref, li.selection), stvar(int32, li.top));
    cxuiTestControlInfoDestroy(&li);
    return ok;
}

static bool uiLmExpectSync(UiLmTest* t, int32 count, strref selection, int32 top)
{
    t->count     = count;
    t->selection = selection;
    t->top       = top;
    return uiPollSync(uiLmExpect, t, timeS(5));
}

static bool uiLmDo(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiLmTest* t = stvlAtPtr(cvars, 0);
    int32 what  = stvlAt(cvars, 1, int32);
    int32 index = stvlAt(cvars, 2, int32);
    switch (what) {
    case 0: return cxuiTestChoose(CxUiWidget(t->list), index, false);
    case 1: return cxuiTestToggle(t->list, index);
    case 2: return cxuiTestChoose(CxUiWidget(t->list), index, true);
    case 3: cxuiListSetTopItem(t->list, index); return true;
    case 4: uiLmFill(t, 100, _S"Again", index); return true;
    case 5: cxuiFocus(t->list); return true;
    case 6: return cxuiFocused(t->w) == CxUiWidget(t->list);
    case 7: return cxuiTestNativeKey(CxUiWidget(t->list), CXUI_KEY_Delete, 0);
    case 8:
        cxuiListBeginUpdate(t->list);
        cxuiListRemove(t->list, index);
        cxuiListEndUpdate(t->list);
        return true;
    }
    return false;
}

static bool uiLmDoSync(UiLmTest* t, int32 what, int32 index)
{
    return cxuiCallSync(closureCreate(uiLmDo, stvar(ptr, t), stvar(int32, what), stvar(int32, index)));
}

// A native list where any number can be selected: filled while held back, it shows the items and
// selection at once; a click selects one item alone and Ctrl-clicks add and take away, one change
// each; a double click opens its item; the top item set from code is the first shown, as is
// read back, and stays so through an update that removes an item; a second fill replaces it all; Delete through the platform's keys reaches the
// list's key handler.
static int test_cxuid_listmulti(void)
{
    UI_START_NATIVE();
    UiLmTest t = { 0 };
    cxuiCallSync(closureCreate(uiLmBuild, stvar(ptr, &t)));
    bool shown = uiLmExpectSync(&t, 200, _S"3 5 ", -1);
    bool clicked = false, opened = false, topped = false, refilled = false, keys = false, deleted = false;
    if (shown) {
        uiLmDoSync(&t, 0, 7);
        uiLmDoSync(&t, 1, 9);
        clicked = uiLmExpectSync(&t, 200, _S"7 9 ", -1) && uiTxWaitCount(&t.changes, 2);
        uiLmDoSync(&t, 1, 7);
        clicked = clicked && uiLmExpectSync(&t, 200, _S"9 ", -1) && uiTxWaitCount(&t.changes, 3);
        uiLmDoSync(&t, 2, 12);
        opened = uiTxWaitCount(&t.activates, 1) && t.item == 12;
        uiLmDoSync(&t, 3, 50);
        topped = uiLmExpectSync(&t, 200, _S"12 ", 50);
        uiLmDoSync(&t, 8, 60);
        topped = topped && uiLmExpectSync(&t, 199, _S"12 ", 50);
        uiLmDoSync(&t, 4, 1);
        refilled = uiLmExpectSync(&t, 100, _S"1 3 ", -1);
        osSleep(timeMS(100));
        refilled = refilled && t.changes == 4;
        uiLmDoSync(&t, 5, 0);
        if (uiPollSyncWithInt(uiLmDo, &t, 6, timeS(5)) && uiLmDoSync(&t, 7, 0)) {
            keys    = true;
            deleted = uiTxWaitCount(&t.deletes, 1);
        }
    }
    cxuiShutdown();
    objRelease(&t.list);
    objRelease(&t.w);
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, keys));
    int ret = 0;
    if (!shown || !clicked || !opened || !topped || !refilled || (keys && !deleted))
        TEST_FAILV(ret, 1, _SL("shown ${int}, clicked ${int} (${int} changes), opened ${int}, top ${int}, refilled ${int}, Delete ${int}: ${string}"),
                   stvar(int32, shown), stvar(int32, clicked), stvar(int32, t.changes), stvar(int32, opened),
                   stvar(int32, topped), stvar(int32, refilled), stvar(int32, deleted), stvar(string, t.why));
    strDestroy(&t.why);
    return ret;
}

// ---- render views ----

static bool uiDestroyWindow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiDestroy((CxUiWindow*)stvlAtPtr(cvars, 0));
    return true;
}

typedef struct UiRvTest {
    CxUiWindow* w;
    CxUiBox* col;
    CxUiRenderView* view;
    CxUiLabel* label;
    int32 available, going, paints, presses;
    bool drawNull;
    bool readInGoing;
    CxUiNativeSurface surface;
    string why;
} UiRvTest;

static CxUiResult uiRvSurface(stvlist* cvars, CxUiSurfaceEvent* ev)
{
    UiRvTest* t = stvlAtPtr(cvars, 0);
    if (ev->available) {
        t->available++;
        cxuiRenderViewSurface(t->view, &t->surface);
    } else {
        t->going++;
        CxUiNativeSurface s;
        t->readInGoing = cxuiRenderViewSurface(t->view, &s) && s.native == t->surface.native;
    }
    return CXUI_Default;
}

static CxUiResult uiRvPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    UiRvTest* t = stvlAtPtr(cvars, 0);
    t->paints++;
    t->drawNull = t->drawNull && !ev->draw;
    return CXUI_Handled;
}

static CxUiResult uiRvMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    if (ev->action == CXUI_MOUSE_Down)
        ((UiRvTest*)stvlAtPtr(cvars, 0))->presses++;
    return CXUI_Default;
}

static bool uiRvBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRvTest* t = stvlAtPtr(cvars, 0);
    t->drawNull = true;
    t->w        = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui render view test", .size = { 360, 300 } });
    t->col      = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->label    = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Above the view" });
    t->view     = cxuiRenderViewCreate(NULL);
    cxuiWidgetSetNaturalSize(t->view, cxuiLen(200), cxuiLen(150));
    cxuiOnSurface(t->view, uiRvSurface, stvar(ptr, t));
    cxuiOnPaint(t->view, uiRvPaint, stvar(ptr, t));
    cxuiOnMouse(t->view, uiRvMouse, stvar(ptr, t));
    cxuiContainerAppend(t->col, t->label);
    cxuiContainerAppend(t->col, t->view);
    CxUiButton* b = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Below the view" });
    cxuiContainerAppend(t->col, b);
    objRelease(&b);
    cxuiContainerAppend(t->w, t->col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// The surface is the platform's, as large as the view, where the view is, and shown as asked; with
// the mark, it shows the mark, where the platform can tell.
static bool uiRvPlaced(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRvTest* t  = stvlAtPtr(cvars, 0);
    bool shown   = *(bool*)stvlAtPtr(cvars, 1);
    bool marked  = *(bool*)stvlAtPtr(cvars, 2);
    CxUiNativeSurface s;
    CxUiTestSurfaceInfo si;
    CxUiRect b;
    strClear(&t->why);
    if (!cxuiRenderViewSurface(t->view, &s) || !s.native || !cxuiTestSurfaceInfo(t->view, &si) ||
        !cxuiTestBounds(CxUiWidget(t->view), &b)) {
        strCopy(&t->why, _S"no surface, or it can't be read back");
        return false;
    }
#if defined(_PLATFORM_WIN)
    uint32 kind = CXUI_SURFACE_Win32;
#elif defined(_PLATFORM_MACOS)
    uint32 kind = CXUI_SURFACE_Metal;
#else
    uint32 kind = s.kind == CXUI_SURFACE_Wayland ? CXUI_SURFACE_Wayland : CXUI_SURFACE_X11;
#endif
    bool ok = s.kind == kind && s.w == b.w && s.h == b.h && si.shown == shown && (!shown || (si.bounds.x == b.x &&
              si.bounds.y == b.y && si.bounds.w == b.w && si.bounds.h == b.h)) && (!marked || !si.canMark || si.marked);
    if (!ok)
        strFormat(&t->why, _SL("surface kind ${uint} ${int}x${int}, at ${int},${int} ${int}x${int} shown ${int} marked ${int}; the view at ${int},${int} ${int}x${int}"),
                  stvar(uint32, s.kind), stvar(int32, s.w), stvar(int32, s.h), stvar(int32, si.bounds.x),
                  stvar(int32, si.bounds.y), stvar(int32, si.bounds.w), stvar(int32, si.bounds.h), stvar(int32, si.shown),
                  stvar(int32, si.marked), stvar(int32, b.x), stvar(int32, b.y), stvar(int32, b.w), stvar(int32, b.h));
    return ok;
}

static bool uiRvPlacedSync(UiRvTest* t, bool shown, bool marked)
{
    int64 end = clockTimer() + timeS(5);
    for (;;) {
        if (cxuiCallSync(closureCreate(uiRvPlaced, stvar(ptr, t), stvar(ptr, &shown), stvar(ptr, &marked))))
            return true;
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
}

static bool uiRvDo(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRvTest* t = stvlAtPtr(cvars, 0);
    switch (stvlAt(cvars, 1, int32)) {
    case 0: return cxuiTestSurfaceMark(t->view);
    case 1:
        // Everything that repaints around it, and the view asked for a frame
        cxuiLabelSetText(t->label, _S"Above the view, a longer label that lays the window out again");
        cxuiCanvasInvalidateAll(t->view);
        cxuiWindowSetSize(t->w, 380, 320);
        return true;
    case 2: cxuiWidgetSetVisible(t->view, false); return true;
    case 3: cxuiWidgetSetVisible(t->view, true); return true;
    case 4: return cxuiTestNativeClick(CxUiCanvas(t->view), 20, 20);
    case 5: {
        CxUiRenderView* held = objAcquire(t->view);
        cxuiContainerDetach(t->col, t->view);
        cxuiContainerInsert(t->col, t->view, 1);
        objRelease(&held);
        return true;
    }
    }
    return false;
}

static bool uiRvDoSync(UiRvTest* t, int32 what)
{
    return cxuiCallSync(closureCreate(uiRvDo, stvar(ptr, t), stvar(int32, what)));
}

// A native render view: its surface comes once with the platform's kind and the view's size, where
// the view is; a renderer's frame stays through a relayout, a resize and a paint, which comes
// without a draw; hidden and shown, the surface follows; a click through the platform reaches the
// view, where it can be clicked; taken out and put back, the surface goes, readable while it does, and comes again; the
// window destroyed, it goes first.
static int test_cxuid_renderview(void)
{
    UI_START_NATIVE();
    UiRvTest t = { 0 };
    cxuiCallSync(closureCreate(uiRvBuild, stvar(ptr, &t)));
    bool came = uiTxWaitCount(&t.available, 1) && uiRvPlacedSync(&t, true, false);
    bool kept = false, hidden = false, clicked = false, again = false;
    int32 paints = 0;
    if (came) {
        bool marked = uiRvDoSync(&t, 0);
        paints      = t.paints;
        uiRvDoSync(&t, 1);
        osSleep(timeMS(300));
        kept = uiRvPlacedSync(&t, true, marked) && t.paints > paints && t.drawNull;
        uiRvDoSync(&t, 2);
        hidden = uiRvPlacedSync(&t, false, false);
        uiRvDoSync(&t, 3);
        hidden = hidden && uiRvPlacedSync(&t, true, false);
        // Where the platform can't click a canvas, nothing to check
        clicked = !uiRvDoSync(&t, 4) || uiTxWaitCount(&t.presses, 1);
        uiRvDoSync(&t, 5);
        again = t.going == 1 && t.readInGoing && uiTxWaitCount(&t.available, 2) && uiRvPlacedSync(&t, true, false);
    }
    t.readInGoing = false;
    cxuiCallSync(closureCreate(uiDestroyWindow, stvar(ptr, t.w)));
    bool gone = t.going == 2 && t.readInGoing;
    cxuiShutdown();
    objRelease(&t.label);
    objRelease(&t.view);
    objRelease(&t.col);
    objRelease(&t.w);
    int ret = 0;
    if (!came || !kept || !hidden || !clicked || !again || !gone)
        TEST_FAILV(ret, 1, _SL("came ${int}, kept through repaints ${int} (${int} paints, all without a draw ${int}), hidden and shown ${int}, clicked ${int}, came again ${int}, went with the window ${int}: ${string}"),
                   stvar(int32, came), stvar(int32, kept), stvar(int32, t.paints - paints), stvar(int32, t.drawNull),
                   stvar(int32, hidden), stvar(int32, clicked), stvar(int32, again), stvar(int32, gone), stvar(string, t.why));
    strDestroy(&t.why);
    return ret;
}

// ---- text areas ----

typedef struct UiAreaTest {
    CxUiWindow* w;
    CxUiTextArea* area;
    CxUiTextArea* framed;   // inside a CxUiTextFrame
    CxUiTextField* field;
    CxUiButton* ok;
    int32 oks, changes;
    string why;
} UiAreaTest;

static CxUiResult uiAreaOk(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    ((UiAreaTest*)stvlAtPtr(cvars, 0))->oks++;
    return CXUI_Default;
}

static CxUiResult uiAreaChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((UiAreaTest*)stvlAtPtr(cvars, 0))->changes++;
    return CXUI_Default;
}

static bool uiAreaBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t = stvlAtPtr(cvars, 0);
    t->w          = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui text area test" });
    CxUiBox* col  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->area       = cxuiTextAreaCreate(&(CxUiTextAreaDesc){ .text = _S"one\ntwo", .lines = 5, .columns = 30 });
    t->framed     = cxuiTextAreaCreate(&(CxUiTextAreaDesc){ .lines = 3, .wrap = true });
    t->field      = cxuiTextFieldCreate(NULL);
    t->ok         = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"OK" });
    cxuiOnActivate(t->ok, uiAreaOk, stvar(ptr, t));
    cxuiOnChange(t->area, uiAreaChange, stvar(ptr, t));
    cxuiContainerAppend(col, t->area);
    CxUiTextFrame* frame = cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = CxUiTextField(t->framed) });
    cxuiContainerAppend(col, frame);
    objRelease(&frame);
    cxuiContainerAppend(col, t->field);
    cxuiContainerAppend(col, t->ok);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowSetDefaultEnter(t->w, t->ok);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiAreaSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t        = stvlAtPtr(cvars, 0);
    CxUiTestTextInfo ai  = { 0 }, fi = { 0 };
    CxUiRect ab, fb;
    bool ok = cxuiTestTextInfo(CxUiTextField(t->area), &ai) && cxuiTestTextInfo(CxUiTextField(t->framed), &fi) &&
              cxuiTestBounds(CxUiWidget(t->area), &ab) && cxuiTestBounds(CxUiWidget(t->field), &fb);
    strClear(&t->why);
    if (!ok)
        strCopy(&t->why, _S"an area couldn't be read back");
    else if (!strEq(ai.text, _S"one\ntwo") || !ai.multiline || !fi.multiline || !fi.wrap || !fi.framed)
        strFormat(&t->why, _SL("shows \"${string}\", multi-line ${int} ${int}, framed ${int}"), stvar(strref, ai.text),
                  stvar(int32, ai.multiline), stvar(int32, fi.multiline), stvar(int32, fi.framed));
    CxUiFontMetrics fm;
    cxuiFontMetrics(cxuiWidgetFont(t->area), &fm);
    if (strEmpty(t->why) && (ab.h < 5 * fm.height || ab.h <= fb.h * 2))
        strFormat(&t->why, _SL("five lines are ${int} high, a line ${int} and a field ${int}"), stvar(int32, ab.h),
                  stvar(int32, fm.height), stvar(int32, fb.h));
    cxuiTestTextInfoDestroy(&ai);
    cxuiTestTextInfoDestroy(&fi);
    return strEmpty(t->why);
}

static bool uiAreaFill(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t = stvlAtPtr(cvars, 0);
    string line   = 0;
    for (int32 i = 0; i < 60; i++) {
        strFormat(&line, _SL("\nline ${int}"), stvar(int32, i));
        cxuiTextAreaAppend(t->area, line);
    }
    strDestroy(&line);
    cxuiTextAreaScrollToEnd(t->area);
    return true;
}

static bool uiAreaAtEnd(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t       = stvlAtPtr(cvars, 0);
    CxUiTestTextInfo ai = { 0 };
    bool ok = cxuiTestTextInfo(CxUiTextField(t->area), &ai) && ai.atEnd && strEndsWith(ai.text, _S"\nline 59") &&
              strEq(cxuiTextFieldText(CxUiTextField(t->area)), ai.text);
    cxuiTestTextInfoDestroy(&ai);
    return ok;
}

static bool uiAreaFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTextField* f = stvlAtPtr(cvars, 0);
    cxuiFocus(f);
    cxuiTextFieldSetCaret(f, 3);
    return true;
}

static bool uiAreaFocused(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t = stvlAtPtr(cvars, 0);
    return cxuiFocused(t->w) == (CxUiWidget*)stvlAtPtr(cvars, 1);
}

static bool uiAreaActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool active = true;
    cxuiTestWindowActive(((UiAreaTest*)stvlAtPtr(cvars, 0))->w, &active);
    return active;
}

static bool uiAreaKey(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestNativeKey(stvlAtPtr(cvars, 0), (CxUiKey)stvlAt(cvars, 1, int32), 0);
}

static bool uiAreaBroke(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t = stvlAtPtr(cvars, 0);
    return strBeginsWith(cxuiTextFieldText(CxUiTextField(t->area)), _S"one\n\ntwo");
}

static bool uiAreaType(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t = stvlAtPtr(cvars, 0);
    return cxuiTestType(CxUiTextField(t->framed), _S"a line\nand another");
}

static bool uiAreaTyped(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t       = stvlAtPtr(cvars, 0);
    CxUiTestTextInfo fi = { 0 };
    bool ok = cxuiTestTextInfo(CxUiTextField(t->framed), &fi) && strEq(fi.text, _S"a line\nand another") &&
              strEq(cxuiTextFieldText(CxUiTextField(t->framed)), fi.text);
    cxuiTestTextInfoDestroy(&fi);
    return ok;
}

static bool uiAreaSetTheme(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWindowSetThemeMode(((UiAreaTest*)stvlAtPtr(cvars, 0))->w, stvlAt(cvars, 1, uint32));
    return true;
}

static bool uiAreaSameColor(CxUiColor a, CxUiColor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

// A field without a frame shows the palette's colors in a dark window where the platform's own
// field doesn't follow it (Windows), and the platform's colors otherwise.
static bool uiAreaPlainColors(UiAreaTest* t, CxUiTextField* f, bool dark, strref name)
{
    CxUiTestTextInfo ti = { 0 };
    bool want           = false;
#if defined(_PLATFORM_WIN)
    want = dark;
#endif
    CxUiPalette* pal = cxuiWidgetPalette(f);
    bool ok          = cxuiTestTextInfo(f, &ti) && ti.hasColors == want &&
              (!want || (uiAreaSameColor(ti.bg, cxuiPaletteColor(pal, CXUI_PAL_Input)) &&
                         uiAreaSameColor(ti.textColor, cxuiPaletteColor(pal, CXUI_PAL_InputText))));
    if (!ok && strEmpty(t->why))
        strFormat(&t->why, _SL("dark ${int}: the ${string} has colors ${int}, background ${uint}"), stvar(int32, dark),
                  stvar(strref, name), stvar(int32, ti.hasColors), stvar(uint32, ti.bg.bits));
    cxuiTestTextInfoDestroy(&ti);
    return ok;
}

static bool uiAreaThemed(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiAreaTest* t = stvlAtPtr(cvars, 0);
    bool dark     = *(bool*)stvlAtPtr(cvars, 1);
    strClear(&t->why);
    if (cxuiWindowResolvedTheme(t->w) != (dark ? CXUI_Theme_Dark : CXUI_Theme_Light))
        return false;
    CxUiTestTextInfo fi = { 0 };
    bool framed = cxuiTestTextInfo(CxUiTextField(t->framed), &fi) && fi.hasColors;
    cxuiTestTextInfoDestroy(&fi);
    if (!framed)
        strCopy(&t->why, _S"the framed area lost its colors");
    bool field = uiAreaPlainColors(t, t->field, dark, _S"field");
    bool area  = uiAreaPlainColors(t, CxUiTextField(t->area), dark, _S"area");
    return field && area && framed;
}

// Native text areas: lines set before they exist are shown, the area several lines tall; lines
// appended are shown, the model agreeing, and scrolling to the end gets there; typed text with a
// line break reaches the model with `\n`; through the platform's keys, Enter breaks the line where
// the caret is without pressing the default button, and Tab moves on to the next widget. In a dark
// window, then a light one, fields without a frame take the colors uiAreaPlainColors says.
static int test_cxuid_textarea(void)
{
    UI_START_NATIVE();
    UiAreaTest t = { 0 };
    cxuiCallSync(closureCreate(uiAreaBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiAreaSettled, &t, timeS(5));
    bool filled = false, typed = false, keys = false, broke = false, tabbed = false, themed = false;
    if (settled) {
        cxuiCallSync(closureCreate(uiAreaFill, stvar(ptr, &t)));
        filled = uiPollSync(uiAreaAtEnd, &t, timeS(5));
        cxuiCallSync(closureCreate(uiAreaType, stvar(ptr, &t)));
        typed = uiPollSync(uiAreaTyped, &t, timeS(5));
        static bool dark = true, light = false;
        cxuiCallSync(closureCreate(uiAreaSetTheme, stvar(ptr, &t), stvar(uint32, CXUI_Theme_Dark)));
        themed = uiPollSyncWith(uiAreaThemed, &t, &dark, timeS(5));
        cxuiCallSync(closureCreate(uiAreaSetTheme, stvar(ptr, &t), stvar(uint32, CXUI_Theme_Light)));
        themed = themed && uiPollSyncWith(uiAreaThemed, &t, &light, timeS(5));
    }
    if (settled && cxuiCallSync(closureCreate(uiAreaActive, stvar(ptr, &t)))) {
        cxuiCallSync(closureCreate(uiAreaFocus, stvar(ptr, t.area)));
        if (uiPollSyncWith(uiAreaFocused, &t, t.area, timeS(5)) &&
            cxuiCallSync(closureCreate(uiAreaKey, stvar(ptr, t.area), stvar(int32, CXUI_KEY_Enter)))) {
            keys  = true;
            broke = uiPollSync(uiAreaBroke, &t, timeS(5));
            osSleep(timeMS(200));
            broke = broke && t.oks == 0;
            cxuiCallSync(closureCreate(uiAreaKey, stvar(ptr, t.area), stvar(int32, CXUI_KEY_Tab)));
            tabbed = uiPollSyncWith(uiAreaFocused, &t, t.framed, timeS(5)) &&
                     cxuiCallSync(closureCreate(uiAreaBroke, stvar(ptr, &t)));
        }
    }
    cxuiShutdown();
    int32 changes = t.changes, oks = t.oks;
    objRelease(&t.area);
    objRelease(&t.framed);
    objRelease(&t.field);
    objRelease(&t.ok);
    objRelease(&t.w);
    string why = t.why;
    t.why      = NULL;
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, keys));
    if (!settled || !themed) {
        logFmt(Error, _SL("${string}"), stvar(string, why));
        strDestroy(&why);
        return 1;
    }
    strDestroy(&why);
    if (!filled || !typed)
        TEST_FAIL(1, _SL("appended and scrolled to the end ${int}, typed ${int}"), stvar(int32, filled),
                  stvar(int32, typed));
    if (keys && (!broke || !tabbed))
        TEST_FAIL(1, _SL("Enter broke the line ${int} (OK pressed ${int}, changes ${int}), Tab moved on ${int}"),
                  stvar(int32, broke), stvar(int32, oks), stvar(int32, changes), stvar(int32, tabbed));
    return 0;
}

// ---- sliders and progress bars ----

typedef struct UiSldTest {
    CxUiWindow* w;
    CxUiSlider* slider;
    CxUiSlider* vertical;
    CxUiProgress* bar;
    CxUiProgress* busy;
    int32 changes, commits;
    string why;
} UiSldTest;

static CxUiResult uiSldChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((UiSldTest*)stvlAtPtr(cvars, 0))->changes++;
    return CXUI_Default;
}

static CxUiResult uiSldCommit(stvlist* cvars, CxUiCommitEvent* ev)
{
    unused_noeval(ev);
    ((UiSldTest*)stvlAtPtr(cvars, 0))->commits++;
    return CXUI_Default;
}

static bool uiSldBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiSldTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui slider test" });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->slider    = cxuiSliderCreate(&(CxUiSliderDesc){ .min = 0, .max = 10, .step = 1, .value = 3, .ticks = 11 });
    t->vertical  = cxuiSliderCreate(&(CxUiSliderDesc){ .orient = CXUI_ORIENT_V, .value = 25, .length = 100 });
    t->bar       = cxuiProgressCreate(&(CxUiProgressDesc){ .value = 40 });
    t->busy      = cxuiProgressCreate(&(CxUiProgressDesc){ .indeterminate = true });
    cxuiOnChange(t->slider, uiSldChange, stvar(ptr, t));
    cxuiOnCommit(t->slider, uiSldCommit, stvar(ptr, t));
    cxuiContainerAppend(col, t->slider);
    cxuiContainerAppend(col, t->vertical);
    cxuiContainerAppend(col, t->bar);
    cxuiContainerAppend(col, t->busy);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiSldInfo(void* w, CxUiTestControlInfo* out)
{
    return cxuiTestControlInfo((CxUiWidget*)w, out);
}

static bool uiSldSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiSldTest* t = stvlAtPtr(cvars, 0);
    CxUiTestControlInfo si = { 0 }, vi = { 0 }, bi = { 0 }, ui = { 0 };
    CxUiRect vb;
    bool ok = uiSldInfo(t->slider, &si) && uiSldInfo(t->vertical, &vi) && uiSldInfo(t->bar, &bi) &&
              uiSldInfo(t->busy, &ui) && cxuiTestBounds(CxUiWidget(t->vertical), &vb);
    strClear(&t->why);
    if (!ok)
        strCopy(&t->why, _S"couldn't be read back");
    else if (si.pos != 3 || si.range != 10 || vi.pos != 25 || vi.range != 100 || !vi.vertical || si.vertical)
        strFormat(&t->why, _SL("sliders at ${int}/${int} and ${int}/${int}, vertical ${int}"), stvar(int32, si.pos),
                  stvar(int32, si.range), stvar(int32, vi.pos), stvar(int32, vi.range), stvar(int32, vi.vertical));
    else if (bi.pos != 4000 || bi.indeterminate || !ui.indeterminate)
        strFormat(&t->why, _SL("the bar at ${int}, activity ${int} and ${int}"), stvar(int32, bi.pos),
                  stvar(int32, bi.indeterminate), stvar(int32, ui.indeterminate));
    else if (vb.h <= vb.w)
        strFormat(&t->why, _SL("the vertical slider is ${int}x${int}"), stvar(int32, vb.w), stvar(int32, vb.h));
    cxuiTestControlInfoDestroy(&si);
    cxuiTestControlInfoDestroy(&vi);
    cxuiTestControlInfoDestroy(&bi);
    cxuiTestControlInfoDestroy(&ui);
    return strEmpty(t->why);
}

static bool uiSldSlide(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestSlide(stvlAtPtr(cvars, 0), stvlAt(cvars, 1, float64), stvlAt(cvars, 2, bool));
}

static void uiSldSlideSync(CxUiSlider* s, float64 v, bool release)
{
    cxuiCallSync(closureCreate(uiSldSlide, stvar(ptr, s), stvar(float64, v), stvar(bool, release)));
}

static bool uiSldAt(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiSlider* s          = stvlAtPtr(cvars, 0);
    CxUiTestControlInfo si = { 0 };
    bool ok = uiSldInfo(s, &si) && si.pos == stvlAt(cvars, 1, int32) && cxuiSliderValue(s) == stvlAt(cvars, 1, int32);
    cxuiTestControlInfoDestroy(&si);
    return ok;
}

static bool uiSldAtSync(CxUiSlider* s, int32 pos)
{
    int64 end = clockTimer() + timeS(5);
    while (!cxuiCallSync(closureCreate(uiSldAt, stvar(ptr, s), stvar(int32, pos)))) {
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
    return true;
}

static bool uiSldEnable(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWidgetSetEnabled((CxUiWidget*)stvlAtPtr(cvars, 0), stvlAt(cvars, 1, bool));
    cxuiFlush();
    return true;
}

static bool uiSldNativeEnabled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTestControlInfo info;
    bool on = cxuiTestControlInfo((CxUiWidget*)stvlAtPtr(cvars, 0), &info) && info.enabled;
    cxuiTestControlInfoDestroy(&info);
    return on;
}

static bool uiSldFocusable(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiSldTest* t = stvlAtPtr(cvars, 0);
    bool active  = true;
    cxuiTestWindowActive(t->w, &active);
    if (!active || !cxuiWidgetCanFocus(t->slider))
        return false;
    cxuiFocus(t->slider);
    return true;
}

static bool uiSldFocused(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiSldTest* t = stvlAtPtr(cvars, 0);
    return cxuiFocused(t->w) == CxUiWidget(t->slider);
}

static bool uiSldKey(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestNativeKey(stvlAtPtr(cvars, 0), (CxUiKey)stvlAt(cvars, 1, int32), 0);
}

// Native sliders and progress bars: each shows the range, value or activity it was given before
// it existed, a vertical slider taller than wide; dragging raises changes, letting go one commit;
// a disabled slider doesn't move and is disabled natively; through the platform's keys, Right
// moves the slider a step with a change and a commit.
static int test_cxuid_sliders(void)
{
    UI_START_NATIVE();
    UiSldTest t = { 0 };
    cxuiCallSync(closureCreate(uiSldBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiSldSettled, &t, timeS(5));
    bool slid = false, disabled = false, keys = false, keyed = false;
    if (settled) {
        uiSldSlideSync(t.slider, 6, false);
        uiSldSlideSync(t.slider, 7, false);
        slid = uiSldAtSync(t.slider, 7) && uiTxWaitCount(&t.changes, 2) && t.commits == 0;
        uiSldSlideSync(t.slider, 7, true);
        slid = slid && uiTxWaitCount(&t.commits, 1);
        cxuiCallSync(closureCreate(uiSldEnable, stvar(ptr, t.slider), stvar(bool, false)));
        uiSldSlideSync(t.slider, 2, true);
        osSleep(timeMS(200));
        disabled = uiSldAtSync(t.slider, 7) && !cxuiCallSync(closureCreate(uiSldNativeEnabled, stvar(ptr, t.slider)));
        cxuiCallSync(closureCreate(uiSldEnable, stvar(ptr, t.slider), stvar(bool, true)));
    }
    if (slid && cxuiCallSync(closureCreate(uiSldFocusable, stvar(ptr, &t))) && uiPollSync(uiSldFocused, &t, timeS(5)) &&
        cxuiCallSync(closureCreate(uiSldKey, stvar(ptr, t.slider), stvar(int32, CXUI_KEY_Right)))) {
        keys  = true;
        keyed = uiSldAtSync(t.slider, 8) && uiTxWaitCount(&t.changes, 3) && uiTxWaitCount(&t.commits, 2);
    }
    cxuiShutdown();
    int32 changes = t.changes, commits = t.commits;
    objRelease(&t.slider);
    objRelease(&t.vertical);
    objRelease(&t.bar);
    objRelease(&t.busy);
    objRelease(&t.w);
    string why = t.why;
    t.why      = NULL;
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, keys));
    if (!settled) {
        logFmt(Error, _SL("${string}"), stvar(string, why));
        strDestroy(&why);
        return 1;
    }
    strDestroy(&why);
    if (!slid || !disabled)
        TEST_FAIL(1, _SL("a drag ${int} (changes ${int}, commits ${int}), disabled ${int}"), stvar(int32, slid),
                  stvar(int32, changes), stvar(int32, commits), stvar(int32, disabled));
    if (keys && !keyed)
        TEST_FAIL(1, _SL("Right: changes ${int}, commits ${int}"), stvar(int32, changes), stvar(int32, commits));
    return 0;
}

// ---- menus ----

typedef struct UiMenuTest {
    CxUiWindow* w;
    CxUiLabel* content;
    CxUiMenu* bar;
    CxUiMenu* file;
    CxUiMenu* recent;
    CxUiMenuItem* open;
    CxUiMenuItem* grid;
    CxUiMenuItem* radio[2];
    int32 opens, grids, radios, recents;
    CxUiRect before;   // the content's bounds before the bar
    string why;
} UiMenuTest;

static CxUiResult uiMnCount(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return CXUI_Default;
}

static CxUiResult uiMnRecent(stvlist* cvars, CxUiActivateEvent* ev);

static void uiMnFillRecent(UiMenuTest* t, int32 n)
{
    cxuiMenuClear(t->recent);
    for (int32 i = 0; i < n; i++) {
        string s = 0;
        strFormat(&s, _SL("&${int} recent"), stvar(int32, i + 1));
        cxuiOnActivate(cxuiMenuAppendItem(t->recent, .title = s), uiMnRecent, stvar(ptr, t));
        strDestroy(&s);
    }
}

// Rebuilds the menu it is in, itself included
static CxUiResult uiMnRecent(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    t->recents++;
    uiMnFillRecent(t, 4);
    return CXUI_Default;
}

static bool uiMnBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    t->w          = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui menu test", .size = { 360, 200 } });
    t->content    = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Menus" });
    cxuiWidgetSetExpand(t->content, CXUI_EXPAND_Both);
    cxuiContainerAppend(t->w, t->content);
    t->file = cxuiMenuCreate();
    t->open = cxuiMenuAppendItem(t->file, .title = _S"&Open...", .hint = _S"Ctrl+O");
    cxuiOnActivate(t->open, uiMnCount, stvar(ptr, &t->opens));
    t->recent = cxuiMenuCreate();
    cxuiMenuAppendItem(t->file, .title = _S"Open &Recent", .submenu = t->recent);
    cxuiMenuAppendSeparator(t->file);
    t->grid = cxuiMenuAppendItem(t->file, .title = _S"&Grid", .kind = CXUI_MENU_Check, .checked = true);
    cxuiOnActivate(t->grid, uiMnCount, stvar(ptr, &t->grids));
    for (int32 i = 0; i < 2; i++) {
        t->radio[i] = cxuiMenuAppendItem(t->file, .title = i ? _S"&Wire" : _S"&Solid", .kind = CXUI_MENU_Radio,
                                         .checked = i == 0);
        cxuiOnActivate(t->radio[i], uiMnCount, stvar(ptr, &t->radios));
    }
    uiMnFillRecent(t, 2);
    t->bar = cxuiMenuCreate();
    cxuiMenuAppendItem(t->bar, .title = _S"&File", .submenu = t->file);
    CxUiMenu* help = cxuiMenuCreate();
    cxuiMenuAppendItem(help, .title = _S"&About");
    cxuiMenuAppendItem(t->bar, .title = _S"&Help", .submenu = help);
    objRelease(&help);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiMnPlaced(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    return cxuiTestBounds(CxUiWidget(t->content), &t->before) && t->before.h > 0;
}

static bool uiMnSetBar(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    cxuiWindowSetMenu(t->w, t->bar);
    return cxuiTestMenuCount(t->bar) >= 0;
}

// The content kept its size under the new bar
static bool uiMnSameSize(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    CxUiRect now;
    return cxuiTestWindowMenu(t->w, t->bar) && cxuiTestBounds(CxUiWidget(t->content), &now) && now.w == t->before.w &&
           now.h == t->before.h;
}

// A title as shown without its mnemonic's `&`
static void uiNoMnemonic(string* out, strref title)
{
    strClear(out);
    for (int32 i = 0, n = strLen(title); i < n; i++) {
        uint8 ch = strGetChar(title, i);
        if (ch == '&' && i + 1 < n)
            ch = strGetChar(title, ++i);
        strAppendChar(out, ch);
    }
}

static bool uiMnShows(UiMenuTest* t, CxUiMenu* m, int32 at, strref title, strref hint, bool checked, bool enabled,
                      bool submenu)
{
    CxUiTestMenuItemInfo mi;
    string want = 0;
#if defined(_PLATFORM_MACOS)
    // macOS has no mnemonics.
    uiNoMnemonic(&want, title);
#else
    strDup(&want, title);
#endif
    bool ok = cxuiTestMenuItem(m, at, &mi) && strEq(mi.title, want) && strEq(mi.hint, hint) && mi.checked == checked &&
              mi.enabled == enabled && mi.submenu == submenu;
    strDestroy(&want);
    if (!ok)
        strFormat(&t->why, _SL("${string} at ${int}: '${string}' '${string}' checked ${int} enabled ${int} submenu ${int}"),
                  stvar(strref, title), stvar(int32, at), stvar(string, mi.title), stvar(string, mi.hint),
                  stvar(int32, mi.checked), stvar(int32, mi.enabled), stvar(int32, mi.submenu));
    cxuiTestMenuItemInfoDestroy(&mi);
    return ok;
}

static bool uiMnRead(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    return cxuiTestMenuCount(t->bar) == 2 && cxuiTestMenuCount(t->file) == 6 && cxuiTestMenuCount(t->recent) == 2 &&
           uiMnShows(t, t->bar, 0, _S"&File", NULL, false, true, true) &&
           uiMnShows(t, t->file, 0, _S"&Open...", _S"Ctrl+O", false, true, false) &&
           uiMnShows(t, t->file, 3, _S"&Grid", NULL, true, true, false) &&
           uiMnShows(t, t->file, 4, _S"&Solid", NULL, true, true, false) &&
           uiMnShows(t, t->recent, 1, _S"&2 recent", NULL, false, true, false);
}

static bool uiMnChoose(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestMenuChoose(stvlAtPtr(cvars, 0));
}

static bool uiMnChooseAt(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    return cxuiTestMenuChoose((CxUiMenuItem*)cxuiContainerChild(t->recent, stvlAt(cvars, 1, int32)));
}

static bool uiMnAfterChoices(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    return t->opens == 1 && t->grids == 1 && t->radios == 1 && t->recents == 1 && cxuiTestMenuCount(t->recent) == 4 &&
           uiMnShows(t, t->file, 3, _S"&Grid", NULL, false, true, false) &&
           uiMnShows(t, t->file, 4, _S"&Solid", NULL, false, true, false) &&
           uiMnShows(t, t->file, 5, _S"&Wire", NULL, true, true, false) &&
           uiMnShows(t, t->recent, 3, _S"&4 recent", NULL, false, true, false);
}

static bool uiMnDisable(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    cxuiWidgetSetEnabled(t->open, false);
    cxuiTestMenuChoose(t->open);
    return uiMnShows(t, t->file, 0, _S"&Open...", _S"Ctrl+O", false, false, false);
}

static bool uiMnDisabledHeld(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    return t->opens == 1;
}

static bool uiMnTakeBar(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    cxuiWindowSetMenu(t->w, CxUiMenuNone);
    return true;
}

static bool uiMnBarGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiMenuTest* t = stvlAtPtr(cvars, 0);
    CxUiRect now;
    return cxuiTestWindowMenu(t->w, CxUiMenuNone) && cxuiTestMenuCount(t->bar) == -1 &&
           cxuiTestBounds(CxUiWidget(t->content), &now) && now.h == t->before.h;
}

// A menu bar through the platform: its items read back as built; the area inside the frame keeps
// its size as the bar comes and goes; items chosen through the platform raise their events, a
// check item turns over and a radio group moves; a menu rebuilt from its own item's handler; a
// disabled item can't be chosen.
static int test_cxuid_menus(void)
{
    UI_START_NATIVE();
    UiMenuTest t = { 0 };
    cxuiCallSync(closureCreate(uiMnBuild, stvar(ptr, &t)));
    bool placed = uiPollSync(uiMnPlaced, &t, timeS(5));
    bool menus  = placed && cxuiCallSync(closureCreate(uiMnSetBar, stvar(ptr, &t)));
    bool kept   = menus && uiPollSync(uiMnSameSize, &t, timeS(5));
    bool read   = menus && cxuiCallSync(closureCreate(uiMnRead, stvar(ptr, &t)));
    bool chosen = false, disabled = false, gone = false;
    if (read) {
        cxuiCallSync(closureCreate(uiMnChoose, stvar(ptr, t.open)));
        cxuiCallSync(closureCreate(uiMnChoose, stvar(ptr, t.grid)));
        cxuiCallSync(closureCreate(uiMnChoose, stvar(ptr, t.radio[1])));
        cxuiCallSync(closureCreate(uiMnChooseAt, stvar(ptr, &t), stvar(int32, 0)));
        chosen   = uiPollSync(uiMnAfterChoices, &t, timeS(5));
        disabled = cxuiCallSync(closureCreate(uiMnDisable, stvar(ptr, &t)));
        // Anything the platform would deliver has had time to.
        osSleep(timeMS(200));
        disabled = disabled && cxuiCallSync(closureCreate(uiMnDisabledHeld, stvar(ptr, &t)));
        cxuiCallSync(closureCreate(uiMnTakeBar, stvar(ptr, &t)));
        gone = uiPollSync(uiMnBarGone, &t, timeS(5));
    }

    cxuiShutdown();
    objRelease(&t.content);
    objRelease(&t.bar);
    objRelease(&t.file);
    objRelease(&t.recent);
    objRelease(&t.w);
    if (!placed)
        TEST_FAIL(1, _SL("the window was never placed"), stvNone);
    if (!menus)
        TEST_SKIP(_SL("the platform has no menus here"), stvNone);
    int ret = 0;
    if (!kept || !read || !chosen || !disabled || !gone)
        TEST_FAILV(ret, 1, _SL("client size kept ${int}, read back ${int}, chosen ${int}, disabled held ${int}, bar taken away ${int}: ${string}"),
                   stvar(int32, kept), stvar(int32, read), stvar(int32, chosen), stvar(int32, disabled), stvar(int32, gone),
                   stvar(string, t.why));
    strDestroy(&t.why);
    return ret;
}

// ---- tabs ----

typedef struct UiTabTest {
    CxUiWindow* w;
    CxUiTabs* tabs;
    CxUiButton* first;     // on the first page
    CxUiButton* filled;    // put on the second page by the change handler
    CxUiTextField* field;  // on the third page
    int32 changes;
    bool realized;         // the page picked had a peer when the handler ran
    string titles;         // what the platform should show
    int32 value;
    string why;
} UiTabTest;

static CxUiResult uiTabChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    UiTabTest* t   = stvlAtPtr(cvars, 0);
    CxUiWidget* pg = cxuiContainerChild(t->tabs, cxuiZBoxActive(t->tabs));
    t->changes++;
    t->realized = pg->peer != NULL;
    if (!t->filled && cxuiZBoxActive(t->tabs) == 1) {
        t->filled = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Filled" });
        cxuiContainerAppend(pg, t->filled);
    }
    return CXUI_Default;
}

static bool uiTabBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTabTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui tabs test" });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->tabs      = cxuiTabsCreate(NULL);
    cxuiWidgetSetExpand(t->tabs, CXUI_EXPAND_Both);
    strref titles[3] = { _S"&General", _S"&Advanced", _S"Th&ird" };
    for (int k = 0; k < 3; k++) {
        CxUiBox* pg = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4, .margin = { 8, 8, 8, 8 } });
        if (k == 0) {
            CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"On the first page" });
            cxuiContainerAppend(pg, l);
            objRelease(&l);
            t->first = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"First" });
            cxuiContainerAppend(pg, t->first);
        } else if (k == 2) {
            t->field = cxuiTextFieldCreate(NULL);
            cxuiContainerAppend(pg, t->field);
        }
        cxuiTabsAdd(t->tabs, pg, titles[k]);
        objRelease(&pg);
    }
    cxuiOnChange(t->tabs, uiTabChanged, stvar(ptr, t));
    cxuiContainerAppend(col, t->tabs);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// The platform shows t->titles with t->value picked, and the shown page's button is in front
static bool uiTabExpect(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTabTest* t           = stvlAtPtr(cvars, 0);
    CxUiTestControlInfo ci = { 0 };
    bool ok = cxuiTestControlInfo(CxUiWidget(t->tabs), &ci) && strEq(ci.items, t->titles) && ci.value == t->value;
    strClear(&t->why);
    if (!ok)
        strFormat(&t->why, _SL("tabs \"${string}\" at ${int}"), stvar(strref, ci.items), stvar(int32, ci.value));
    cxuiTestControlInfoDestroy(&ci);
    CxUiWidget* b = t->value == 0 ? CxUiWidget(t->first) : t->value == 1 ? CxUiWidget(t->filled) : NULL;
    CxUiRect r;
    bool front = false, shown = false;
    if (ok && b && cxuiTestBounds(b, &r) && cxuiTestFrontmost(b, r.x + r.w / 2, r.y + r.h / 2, &front) &&
        (!front || !cxuiTestShown(b, &shown) || !shown)) {
        strFormat(&t->why, _SL("page ${int}'s button isn't in front"), stvar(int32, t->value));
        ok = false;
    }
    return ok;
}

// The platforms show titles without their `&`.
static bool uiTabExpectSync(UiTabTest* t, strref titles, int32 value)
{
    uiNoMnemonic(&t->titles, titles);
    t->value = value;
    return uiPollSync(uiTabExpect, t, timeS(5));
}

static bool uiTabRetitle(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiTabsSetTitle(((UiTabTest*)stvlAtPtr(cvars, 0))->tabs, 1, _S"&Advanced \xE2\x80\xA2");
    return true;
}

static bool uiTabPick(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestChoose(CxUiWidget(((UiTabTest*)stvlAtPtr(cvars, 0))->tabs), stvlAt(cvars, 1, int32), false);
}

static bool uiTabPickSync(UiTabTest* t, int32 index)
{
    return cxuiCallSync(closureCreate(uiTabPick, stvar(ptr, t), stvar(int32, index)));
}

// The first page's button is hidden once another page shows.
static bool uiTabFirstHidden(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool shown = true;
    return cxuiTestShown(CxUiWidget(((UiTabTest*)stvlAtPtr(cvars, 0))->first), &shown) && !shown;
}

static bool uiTabSetActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiZBoxSetActive(((UiTabTest*)stvlAtPtr(cvars, 0))->tabs, stvlAt(cvars, 1, int32));
    return true;
}

static bool uiTabEnable(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWidgetSetEnabled(((UiTabTest*)stvlAtPtr(cvars, 0))->tabs, stvlAt(cvars, 1, bool));
    cxuiFlush();
    return true;
}

static bool uiTabFocusField(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiFocus(((UiTabTest*)stvlAtPtr(cvars, 0))->field);
    return true;
}

static bool uiTabFieldFocused(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiTabTest* t = stvlAtPtr(cvars, 0);
    return cxuiFocused(t->w) == CxUiWidget(t->field);
}

static bool uiTabCtrlTab(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestNativeKey(CxUiWidget(((UiTabTest*)stvlAtPtr(cvars, 0))->field), CXUI_KEY_Tab, CXUI_MOD_Ctrl);
}

static bool uiTabActive(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    bool active = true;
    cxuiTestWindowActive(((UiTabTest*)stvlAtPtr(cvars, 0))->w, &active);
    return active;
}

static bool uiTabSetTheme(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWindowSetThemeMode(((UiTabTest*)stvlAtPtr(cvars, 0))->w, stvlAt(cvars, 1, uint32));
    return true;
}

// Native tabs: titles given before they exist are shown, the first page in front; a title changed
// later is shown; a pick through the platform raises one change before its page exists, and the
// page the handler fills is shown in front, the other page hidden; a pick from code reaches the
// platform without an event; disabled, the tabs take no pick; dark, the shown page stays in front;
// through the platform's keys, Ctrl+Tab from a field on a page goes on to the next, round to the
// first.
static int test_cxuid_tabs(void)
{
    UI_START_NATIVE();
    UiTabTest t = { 0 };
    cxuiCallSync(closureCreate(uiTabBuild, stvar(ptr, &t)));
    bool shown = uiTabExpectSync(&t, _S"&General\n&Advanced\nTh&ird\n", 0);
    bool retitled = false, picked = false, fromCode = false, disabled = false, dark = false, keys = false, keyed = false;
    if (shown) {
        cxuiCallSync(closureCreate(uiTabRetitle, stvar(ptr, &t)));
        retitled = uiTabExpectSync(&t, _S"&General\n&Advanced \xE2\x80\xA2\nTh&ird\n", 0);
    }
    if (retitled) {
        uiTabPickSync(&t, 1);
        picked = uiTxWaitCount(&t.changes, 1) && !t.realized && t.filled &&
                 uiTabExpectSync(&t, _S"&General\n&Advanced \xE2\x80\xA2\nTh&ird\n", 1) &&
                 uiPollSync(uiTabFirstHidden, &t, timeS(5));
    }
    if (picked) {
        cxuiCallSync(closureCreate(uiTabSetActive, stvar(ptr, &t), stvar(int32, 2)));
        fromCode = uiTabExpectSync(&t, _S"&General\n&Advanced \xE2\x80\xA2\nTh&ird\n", 2);
        osSleep(timeMS(200));
        fromCode = fromCode && t.changes == 1;
        cxuiCallSync(closureCreate(uiTabEnable, stvar(ptr, &t), stvar(bool, false)));
        uiTabPickSync(&t, 0);
        osSleep(timeMS(200));
        disabled = t.changes == 1 && uiTabExpectSync(&t, _S"&General\n&Advanced \xE2\x80\xA2\nTh&ird\n", 2);
        cxuiCallSync(closureCreate(uiTabEnable, stvar(ptr, &t), stvar(bool, true)));
        cxuiCallSync(closureCreate(uiTabSetTheme, stvar(ptr, &t), stvar(uint32, CXUI_Theme_Dark)));
        cxuiCallSync(closureCreate(uiTabSetActive, stvar(ptr, &t), stvar(int32, 1)));
        dark = uiTabExpectSync(&t, _S"&General\n&Advanced \xE2\x80\xA2\nTh&ird\n", 1);
        cxuiCallSync(closureCreate(uiTabSetActive, stvar(ptr, &t), stvar(int32, 2)));
        dark = dark && uiTabExpectSync(&t, _S"&General\n&Advanced \xE2\x80\xA2\nTh&ird\n", 2);
    }
    if (dark && cxuiCallSync(closureCreate(uiTabActive, stvar(ptr, &t)))) {
        cxuiCallSync(closureCreate(uiTabFocusField, stvar(ptr, &t)));
        if (uiPollSync(uiTabFieldFocused, &t, timeS(5)) && cxuiCallSync(closureCreate(uiTabCtrlTab, stvar(ptr, &t)))) {
            keys  = true;
            keyed = uiTxWaitCount(&t.changes, 2) && uiTabExpectSync(&t, _S"&General\n&Advanced \xE2\x80\xA2\nTh&ird\n", 0);
        }
    }
    cxuiShutdown();
    objRelease(&t.first);
    objRelease(&t.filled);
    objRelease(&t.field);
    objRelease(&t.tabs);
    objRelease(&t.w);
    TEST_INFO(_SL("keys through the platform: ${int}"), stvar(int32, keys));
    int ret = 0;
    if (!shown || !retitled || !picked || !fromCode || !disabled || !dark || (keys && !keyed))
        TEST_FAILV(ret, 1, _SL("shown ${int}, retitled ${int}, picked ${int} (${int} changes, realized first ${int}), from code ${int}, disabled ${int}, dark ${int}, keyed ${int}: ${string}"),
                   stvar(int32, shown), stvar(int32, retitled), stvar(int32, picked), stvar(int32, t.changes),
                   stvar(int32, t.realized), stvar(int32, fromCode), stvar(int32, disabled), stvar(int32, dark),
                   stvar(int32, keyed), stvar(string, t.why));
    strDestroy(&t.why);
    strDestroy(&t.titles);
    return ret;
}

// ---- scroll boxes ----

typedef struct UiScrTest {
    CxUiWindow* w;
    CxUiScrollBox* sb;
    CxUiScrollBox* inner; // inside sb, with room for what is in it
    CxUiLabel* first;
    CxUiLabel* last;
    CxUiButton* button;   // near the end
    CxUiTextField* field; // at the end
    CxUiTextField* outside;
    int32 presses;
    CxUiPoint pos;
} UiScrTest;

static CxUiResult uiScrPress(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    ((UiScrTest*)stvlAtPtr(cvars, 0))->presses++;
    return CXUI_Default;
}

static bool uiScrBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiScrTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui scroll box test" });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->outside   = cxuiTextFieldCreate(NULL);
    cxuiContainerAppend(col, t->outside);
    t->sb = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .view = { 0, 120 }, .gap = 4,
                                                       .margin = { 4, 4, 4, 4 } });
    t->inner = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V });
    for (int32 i = 0; i < 2; i++) {
        CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"In a scroll box that has room" });
        cxuiContainerAppend(t->inner, l);
        objRelease(&l);
    }
    for (int32 i = 0; i < 30; i++) {
        string text = 0;
        strFormat(&text, _SL("Line ${int} of the scrolled content"), stvar(int32, i));
        CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = text });
        strDestroy(&text);
        cxuiContainerAppend(t->sb, l);
        if (i == 0) {
            t->first = objAcquire(l);
            cxuiContainerAppend(t->sb, t->inner);
        }
        if (i == 29)
            t->last = objAcquire(l);
        objRelease(&l);
    }
    t->button = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Near the end" });
    cxuiOnActivate(t->button, uiScrPress, stvar(ptr, t));
    cxuiContainerAppend(t->sb, t->button);
    t->field = cxuiTextFieldCreate(NULL);
    cxuiContainerAppend(t->sb, t->field);
    cxuiContainerAppend(col, t->sb);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiScrAt(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiScrTest* t = stvlAtPtr(cvars, 0);
    return cxuiTestScrollPos(t->sb, &t->pos);
}

static bool uiScrShow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiScrTest* t = stvlAtPtr(cvars, 0);
    cxuiScrollBoxShow(t->sb, (CxUiWidget*)stvlAtPtr(cvars, 1));
    return true;
}

// The scroll position, once it is where it should be: at least min, or for min 0 back at the
// top, the first line's margin aside
static bool uiScrWait(UiScrTest* t, int32 min)
{
    int64 end = clockTimer() + timeS(5);
    for (;;) {
        bool ok = cxuiCallSync(closureCreate(uiScrAt, stvar(ptr, t)));
        if (ok && (min > 0 ? t->pos.y >= min : t->pos.y <= 10))
            return true;
        if (clockTimer() > end)
            return false;
        osSleep(timeMS(10));
    }
}

static bool uiScrSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiScrTest* t = stvlAtPtr(cvars, 0);
    CxUiRect sb, last;
    bool shown   = false;
    return cxuiTestBounds(CxUiWidget(t->sb), &sb) && cxuiTestBounds(CxUiWidget(t->last), &last) &&
           cxuiTestShown(CxUiWidget(t->last), &shown) && shown && sb.h > 0 && last.y > sb.y + sb.h &&
           cxuiTestScrollPos(t->sb, &t->pos);
}

static bool uiScrClick(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestClick(CxUiWidget(((UiScrTest*)stvlAtPtr(cvars, 0))->button));
}

static bool uiScrWheel(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestScrollWheel(((UiScrTest*)stvlAtPtr(cvars, 0))->sb, stvlAt(cvars, 1, int32));
}

#if defined(_PLATFORM_WIN)
static bool uiScrWheelInner(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestScrollWheel(((UiScrTest*)stvlAtPtr(cvars, 0))->inner, stvlAt(cvars, 1, int32));
}
#endif

static bool uiScrFocus(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiFocus((CxUiWidget*)stvlAtPtr(cvars, 0));
    return true;
}

// A native scroll box: content taller than its view lays out below it, and the box starts at the
// top; showing the last line scrolls down to it and showing the first back up; the wheel scrolls;
// a button near the end still takes a click; a field inside that gets focus is scrolled into view.
// On Windows, where the wheel is turned over the platform's own window for it, a scroll box with
// room for its content passes the wheel to the one around it.
static int test_cxuid_scrollbox(void)
{
    UI_START_NATIVE();
    UiScrTest t = { 0 };
    cxuiCallSync(closureCreate(uiScrBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiScrSettled, &t, timeS(5)) && t.pos.y == 0;
    bool shown = false, back = false, wheeled = false, clicked = false, focused = false, passed = true;
    if (settled) {
        cxuiCallSync(closureCreate(uiScrShow, stvar(ptr, &t), stvar(ptr, t.last)));
        shown = uiScrWait(&t, 1);
        cxuiCallSync(closureCreate(uiScrShow, stvar(ptr, &t), stvar(ptr, t.first)));
        back = uiScrWait(&t, 0);
#if defined(_PLATFORM_WIN)
        int32 top = t.pos.y;
        cxuiCallSync(closureCreate(uiScrWheelInner, stvar(ptr, &t), stvar(int32, 2)));
        passed = uiScrWait(&t, top + 20);
        cxuiCallSync(closureCreate(uiScrShow, stvar(ptr, &t), stvar(ptr, t.first)));
        uiScrWait(&t, 0);
#endif
        cxuiCallSync(closureCreate(uiScrWheel, stvar(ptr, &t), stvar(int32, 2)));
        wheeled = uiScrWait(&t, 1);
        cxuiCallSync(closureCreate(uiScrClick, stvar(ptr, &t)));
        clicked = uiTxWaitCount(&t.presses, 1);
        cxuiCallSync(closureCreate(uiScrShow, stvar(ptr, &t), stvar(ptr, t.first)));
        uiScrWait(&t, 0);
        cxuiCallSync(closureCreate(uiScrFocus, stvar(ptr, t.field)));
        focused = uiScrWait(&t, 1);
    }
    cxuiShutdown();
    CxUiPoint pos = t.pos;
    objRelease(&t.first);
    objRelease(&t.last);
    objRelease(&t.button);
    objRelease(&t.field);
    objRelease(&t.outside);
    objRelease(&t.inner);
    objRelease(&t.sb);
    objRelease(&t.w);
    if (!settled || !shown || !back || !wheeled || !clicked || !focused || !passed)
        TEST_FAIL(1, _SL("started at the top ${int}, showed the last line ${int} and the first ${int}, wheel ${int}, "
                         "click ${int}, focus ${int} (at ${int}), wheel passed on from a box with room ${int}"),
                  stvar(int32, settled), stvar(int32, shown), stvar(int32, back), stvar(int32, wheeled),
                  stvar(int32, clicked), stvar(int32, focused), stvar(int32, pos.y), stvar(int32, passed));
    return 0;
}

// ---- titled frames, links and tips ----

typedef struct UiFrmTest {
    CxUiWindow* w;
    CxUiFrame* frame;
    CxUiLabel* inside;
    CxUiLink* link;
    CxUiButton* button;
    CxUiTextField* field;
    CxUiList* list;
    int32 opened;
    string why;
} UiFrmTest;

static CxUiResult uiFrmOpened(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    ((UiFrmTest*)stvlAtPtr(cvars, 0))->opened++;
    return CXUI_Default;
}

static bool uiFrmBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    static const strref items[] = { _S"one", _S"two", NULL };
    UiFrmTest* t = stvlAtPtr(cvars, 0);
    t->w         = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui frame test" });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    t->frame     = cxuiFrameCreate(&(CxUiPanelDesc){ .orient = CXUI_ORIENT_V, .title = _S"Options",
                                                     .margin = { 6, 4, 6, 6 }, .gap = 4 });
    t->inside    = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Inside the frame" });
    t->link      = cxuiLinkCreate(&(CxUiLinkDesc){ .text = _S"Release notes", .url = _S"https://example.com" });
    t->button    = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Button" });
    t->field     = cxuiTextFieldCreate(NULL);
    t->list      = cxuiListCreate(&(CxUiListDesc){ .items = items, .visibleItems = 2 });
    cxuiOnActivate(t->link, uiFrmOpened, stvar(ptr, t));
    cxuiWidgetSetTip(t->button, _S"A button's tip");
    cxuiWidgetSetTip(t->link, _S"Opens the notes");
    cxuiWidgetSetTip(t->frame, _S"The frame's tip");
    cxuiContainerAppend(t->frame, t->inside);
    cxuiContainerAppend(t->frame, t->link);
    cxuiContainerAppend(col, t->frame);
    cxuiContainerAppend(col, t->button);
    cxuiContainerAppend(col, t->field);
    cxuiContainerAppend(col, t->list);
    cxuiContainerAppend(t->w, col);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiFrmTip(void* w, strref want)
{
    string got = 0;
    bool ok    = cxuiTestTip((CxUiWidget*)w, &got) && strEq(got, want);
    strDestroy(&got);
    return ok;
}

// The frame's content is a line down, the link shown; tips set before the peers existed are there,
// and ones set after reach them.
static bool uiFrmSettled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiFrmTest* t = stvlAtPtr(cvars, 0);
    CxUiRect fb, ib;
    bool shown   = false;
    strClear(&t->why);
    CxUiFontMetrics fm;
    cxuiFontMetrics(cxuiWidgetFont(t->frame), &fm);
    if (!cxuiTestBounds(CxUiWidget(t->frame), &fb) || !cxuiTestBounds(CxUiWidget(t->inside), &ib) ||
        !cxuiTestShown(CxUiWidget(t->link), &shown) || !shown)
        strCopy(&t->why, _S"not shown");
    else if (ib.y - fb.y < fm.height)
        strFormat(&t->why, _SL("the frame's content is ${int} down, a line ${int}"), stvar(int32, ib.y - fb.y),
                  stvar(int32, fm.height));
    else if (!uiFrmTip(t->button, _S"A button's tip") || !uiFrmTip(t->link, _S"Opens the notes") ||
             !uiFrmTip(t->frame, _S"The frame's tip"))
        strCopy(&t->why, _S"a tip set before realizing isn't there");
    return strEmpty(t->why);
}

static bool uiFrmLater(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiFrmTest* t = stvlAtPtr(cvars, 0);
    cxuiWidgetSetTip(t->field, _S"Type a name\nor a number");
    cxuiWidgetSetTip(t->list, _S"Pick one");
    cxuiWidgetSetTip(t->button, NULL);
    cxuiFrameSetTitle(t->frame, _S"Other options");
    return uiFrmTip(t->field, _S"Type a name\nor a number") && uiFrmTip(t->list, _S"Pick one") &&
           uiFrmTip(t->button, NULL);
}

static bool uiFrmClick(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiFrmTest* t   = stvlAtPtr(cvars, 0);
    CxUiCanvas* cv = CxUiCanvas(t->link);
    cxuiTestPointer(cv, CXUI_TESTPTR_Move, 0, 0, 0, 4, 4);
    cxuiTestPointer(cv, CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 4, 4);
    cxuiTestPointer(cv, CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 4, 4);
    return true;
}

// A titled frame, a link and tips on native peers: the frame's content sits a line down, a tip
// set before its peer existed or after it is the platform's, and one taken away is gone; a click
// opens the link.
static int test_cxuid_frames(void)
{
    UI_START_NATIVE();
    UiFrmTest t = { 0 };
    cxuiCallSync(closureCreate(uiFrmBuild, stvar(ptr, &t)));
    bool settled = uiPollSync(uiFrmSettled, &t, timeS(5));
    bool later = false, opened = false;
    if (settled) {
        later = cxuiCallSync(closureCreate(uiFrmLater, stvar(ptr, &t)));
        cxuiCallSync(closureCreate(uiFrmClick, stvar(ptr, &t)));
        opened = uiTxWaitCount(&t.opened, 1);
    }
    cxuiShutdown();
    objRelease(&t.frame);
    objRelease(&t.inside);
    objRelease(&t.link);
    objRelease(&t.button);
    objRelease(&t.field);
    objRelease(&t.list);
    objRelease(&t.w);
    string why = t.why;
    t.why      = NULL;
    if (!settled) {
        logFmt(Error, _SL("${string}"), stvar(string, why));
        strDestroy(&why);
        return 1;
    }
    strDestroy(&why);
    if (!later || !opened)
        TEST_FAIL(1, _SL("tips set later ${int}, the link opened ${int}"), stvar(int32, later), stvar(int32, opened));
    return 0;
}

// ---- theme changes (Wine) ----

#if defined(_PLATFORM_WIN)
#include <cx/platform/win/win_os.h>

typedef struct UiThemeTest {
    CxUiWindow* w;
    int32 events;
    uint32 theme;
} UiThemeTest;

static CxUiResult uiThOnTheme(stvlist* cvars, CxUiThemeChangedEvent* ev)
{
    UiThemeTest* t = stvlAtPtr(cvars, 0);
    t->events++;
    t->theme = ev->theme;
    return CXUI_Default;
}

static bool uiThBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiThemeTest* t = stvlAtPtr(cvars, 0);
    t->w           = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui theme test", .size = { 200, 100 } });
    cxuiOnThemeChanged(t->w, uiThOnTheme, stvar(ptr, t));
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

static bool uiThResolved(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiThemeTest* t = stvlAtPtr(cvars, 0);
    return cxuiWindowResolvedTheme(t->w) == CXUI_Theme_Dark;
}

// Sets the setting Windows keeps for apps' light or dark mode, and tells every window, as
// Windows does when the user changes it.
static void uiThSetSystem(DWORD light)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0,
                        NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(key, L"AppsUseLightTheme", 0, REG_DWORD, (const BYTE*)&light, sizeof(light));
        RegCloseKey(key);
    }
    DWORD_PTR unused;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet", SMTO_ABORTIFHUNG, 2000,
                        &unused);
}

// A window that follows the system resolves again when Windows says the setting changed. Wine
// only: on Windows it would switch the user's whole desktop.
static int test_cxuid_themechange(void)
{
    if (!osIsWine())
        TEST_SKIP(_SL("changes the desktop's dark mode; only run under Wine"), stvNone);
    UI_START_NATIVE();
    UiThemeTest t = { 0 };
    uiThSetSystem(1);
    cxuiCallSync(closureCreate(uiThBuild, stvar(ptr, &t)));
    uiThSetSystem(0);
    bool dark = uiPollSync(uiThResolved, &t, timeS(5));
    int32 darkEvents = t.events;
    uint32 darkTheme = t.theme;
    uiThSetSystem(1);
    osSleep(timeMS(200));
    bool light = !cxuiCallSync(closureCreate(uiThResolved, stvar(ptr, &t)));
    cxuiShutdown();
    objRelease(&t.w);
    if (!dark || darkEvents != 1 || darkTheme != CXUI_Theme_Dark || !light || t.events != 2)
        TEST_FAIL(1, _SL("went dark ${int} (${int} events, theme ${uint}), back to light ${int} (${int} events)"),
                  stvar(int32, dark), stvar(int32, darkEvents), stvar(uint32, darkTheme), stvar(int32, light),
                  stvar(int32, t.events));
    return 0;
}
#endif

static int test_cxuid_grp_modal(void)
{
    TEST_CHAIN(test_cxuid_msgbox, test_cxuid_popup);
}

static int test_cxuid_grp_loop(void)
{
    TEST_CHAIN(test_cxuid_sessions, test_cxuid_flood);
}

// ---- clipboard, monitors and placement ----

typedef struct UiDeskTest {
    CxUiWindow* w;
    CxUiSize sizeBeforeShow;   // where the platform can't place windows, the size once placed
    bool shownWhilePlaced;     // shown by a placement made while hidden
    atomic(bool) queuedRan;    // work posted before reading the clipboard has run
    CxUiRect work;     // the primary monitor's work area
    CxUiRect want;     // the rectangle last placed
    flags_t caps;
    string why;        // the first check that failed on the UI thread
} UiDeskTest;

static bool uiDeskQueued(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    atomicStore(bool, (atomic(bool)*)stvlAtPtr(cvars, 0), true, Release);
    return true;
}

static bool uiDeskClipboard(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t = stvlAtPtr(cvars, 0);
    string got = 0, raw = 0;
    bool ok     = true;
    strref text = _SL("line one\nline two \xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    // The program's own text is read without running queued work.
    cxuiPost(closureCreate(uiDeskQueued, stvar(ptr, &t->queuedRan)));
    if (!cxuiClipboardSetText(text) || !cxuiClipboardGetText(&got) || !strEq(got, text)) {
        strFormat(&t->why, _SL("own text came back as '${string}'"), stvar(string, got));
        ok = false;
    }
    if (ok && atomicLoad(bool, &t->queuedRan, Acquire)) {
        strFormat(&t->why, _SL("reading the program's own text ran queued work"), stvNone);
        ok = false;
    }
    // Other programs see the platform's line ends.
    cxuiTestClipboardRaw(&raw);
#if defined(_PLATFORM_WIN)
    strref wantRaw = _SL("line one\r\nline two \xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
#else
    strref wantRaw = text;
#endif
    if (ok && !strEq(raw, wantRaw)) {
        strFormat(&t->why, _SL("other programs would see '${string}'"), stvar(string, raw));
        ok = false;
    }

    strref foreign = _SL("from\r\nanother program\n");
    if (ok && cxuiTestClipboardForeign(foreign)) {
        bool read = cxuiClipboardGetText(&got);
#if defined(_PLATFORM_WIN)
        strref wantForeign = _SL("from\nanother program\n");
#else
        strref wantForeign = foreign;
#endif
        if (!read || !strEq(got, wantForeign)) {
            strFormat(&t->why, _SL("another program's text came back as '${string}' (${int})"), stvar(string, got),
                      stvar(int32, read));
            ok = false;
        }
    } else if (ok) {
        logStr(Info, _SL("the platform can't stand in for another program's clipboard here"));
    }
    strDestroy(&got);
    strDestroy(&raw);
    return ok;
}

static bool uiDeskMonitors(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t       = stvlAtPtr(cvars, 0);
    sa_CxUiMonitor mons = { 0 };
    int32 n             = cxuiMonitors(&mons);
    int32 primaries     = 0;
    bool ok             = n > 0 && n == saSize(mons) && (mons.a[0].flags & CXUI_MON_Primary);
    for (int32 i = 0; ok && i < n; i++) {
        CxUiMonitor* m = &mons.a[i];
        primaries += (m->flags & CXUI_MON_Primary) ? 1 : 0;
        if (m->scale <= 0 || m->bounds.w <= 0 || m->bounds.h <= 0 || m->workArea.w <= 0 || m->workArea.h <= 0 ||
            m->workArea.x < m->bounds.x || m->workArea.y < m->bounds.y ||
            m->workArea.x + m->workArea.w > m->bounds.x + m->bounds.w ||
            m->workArea.y + m->workArea.h > m->bounds.y + m->bounds.h)
            ok = false;
    }
    if (n > 0) {
        t->work = mons.a[0].workArea;
        logFmt(Info, _SL("${int} monitors; primary ${int},${int} ${int}x${int} at scale ${float}, flags ${uint}"),
               stvar(int32, n), stvar(int32, mons.a[0].bounds.x), stvar(int32, mons.a[0].bounds.y),
               stvar(int32, mons.a[0].bounds.w), stvar(int32, mons.a[0].bounds.h), stvar(float64, mons.a[0].scale),
               stvar(uint32, mons.a[0].flags));
    }
    saDestroy(&mons);
    if (!ok || primaries != 1) {
        strFormat(&t->why, _SL("${int} monitors, ${int} primary, or one with no size"), stvar(int32, n), stvar(int32, primaries));
        return false;
    }
    return true;
}

static void deskPlace(UiDeskTest* t, CxUiRect r, uint32 state, flags_t valid)
{
    CxUiPlacement pl = { .normal = r, .state = state, .valid = valid };
    cxuiWindowSetPlacement(t->w, &pl);
}

// Placed before it is realized; a window that fits its content keeps the size placed.
static bool uiDeskBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t = stvlAtPtr(cvars, 0);
    t->caps       = cxuiPlacementCaps();
    t->w          = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui placement", .size = { 200, 150 } });
    t->want       = (CxUiRect){ t->work.x + 60, t->work.y + 50, 500, 400 };
    deskPlace(t, t->want, CXUI_WINDOW_Normal, CXUI_PLACE_Position | CXUI_PLACE_Size);
    cxuiWindowRealize(t->w);
    // Content arriving after the placement doesn't refit the window.
    CxUiSpace* content = cxuiSpaceCreate(cxuiLen(100), cxuiLen(80));
    cxuiContainerAppend(t->w, content);
    objRelease(&content);
    cxuiFlush();
    cxuiWindowSize(t->w, &t->sizeBeforeShow);
    bool shown = false;
    t->shownWhilePlaced = cxuiTestShown(CxUiWidget(t->w), &shown) && shown;
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return cxuiWindowIsVisible(t->w);
}

// The rectangle is where it was put; where the platform can't place windows, only the size.
static bool deskRectIs(UiDeskTest* t, CxUiRect r)
{
    CxUiPlacement pl;
    if (!cxuiWindowPlacement(t->w, &pl))
        return false;
    bool sized = pl.normal.w == r.w && pl.normal.h == r.h;
    if (!(t->caps & CXUI_PLACE_Position))
        return sized;
    return sized && pl.normal.x == r.x && pl.normal.y == r.y;
}

static bool uiDeskPlaced(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t = stvlAtPtr(cvars, 0);
    CxUiSize sz;
    bool clientOk = (t->caps & CXUI_PLACE_Position) || (cxuiWindowSize(t->w, &sz) && sz.w == 500 && sz.h == 400);
    return clientOk && deskRectIs(t, t->want);
}

static bool uiDeskState(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t = stvlAtPtr(cvars, 0);
    deskPlace(t, (CxUiRect){ 0 }, stvlAt(cvars, 1, uint32), CXUI_PLACE_State);
    return true;
}

static bool uiDeskStateIs(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t = stvlAtPtr(cvars, 0);
    uint32 state  = (uint32)(uintptr)stvlAtPtr(cvars, 1);
    CxUiPlacement pl;
    CxUiSize sz;
    // Maximized once it has grown past its 500x400 frame too (every test display is bigger); a
    // restore asked for before then can be lost on Wayland.
    bool grown = cxuiWindowSize(t->w, &sz) && sz.w > 500 && sz.h > 400;
    return cxuiWindowPlacement(t->w, &pl) && pl.state == state && deskRectIs(t, t->want) &&
           (state != CXUI_WINDOW_Maximized || grown);
}

static bool uiDeskHideMaximized(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t = stvlAtPtr(cvars, 0);
    cxuiWindowHide(t->w);
    deskPlace(t, (CxUiRect){ 0 }, CXUI_WINDOW_Maximized, CXUI_PLACE_State);
    bool shown = false;
    t->shownWhilePlaced |= cxuiTestShown(CxUiWidget(t->w), &shown) && shown;
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// Far off every monitor: it comes back onto the primary one, or with only a size, no bigger than it
static bool uiDeskFarOff(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiDeskTest* t = stvlAtPtr(cvars, 0);
    deskPlace(t, (CxUiRect){ -30000, -30000, 40000, 40000 }, CXUI_WINDOW_Normal, CXUI_PLACE_Position | CXUI_PLACE_Size);
    CxUiPlacement pl;
    cxuiWindowPlacement(t->w, &pl);
    CxUiRect r = pl.normal;
    if (r.w <= 0 || r.h <= 0 || r.w > t->work.w || r.h > t->work.h) {
        strFormat(&t->why, _SL("placed far off and too big: ${int}x${int}"), stvar(int32, r.w), stvar(int32, r.h));
        return false;
    }
    if ((t->caps & CXUI_PLACE_Position) &&
        (r.x < t->work.x || r.y < t->work.y || r.x + r.w > t->work.x + t->work.w || r.y + r.h > t->work.y + t->work.h)) {
        strFormat(&t->why, _SL("placed far off: ended at ${int},${int} ${int}x${int}"), stvar(int32, r.x), stvar(int32, r.y),
                  stvar(int32, r.w), stvar(int32, r.h));
        return false;
    }
    return true;
}

// Clipboard text both ways, the monitors, and a window's placement before and after it is shown.
static int test_cxuid_desktop(void)
{
    UI_START_NATIVE();
    UiDeskTest t = { 0 };
    bool clip    = cxuiCallSync(closureCreate(uiDeskClipboard, stvar(ptr, &t)));
    bool mons    = cxuiCallSync(closureCreate(uiDeskMonitors, stvar(ptr, &t)));
    bool built   = mons && cxuiCallSync(closureCreate(uiDeskBuild, stvar(ptr, &t)));
    bool placed  = built && uiPollSync(uiDeskPlaced, &t, timeS(5));

    // Without a window manager, X11 has nothing to maximize a window.
    bool maxed = false, restored = false, pending = false, farOff = false;
    if (placed) {
        cxuiCallSync(closureCreate(uiDeskState, stvar(ptr, &t), stvar(uint32, CXUI_WINDOW_Maximized)));
        maxed = uiPollSyncWith(uiDeskStateIs, &t, (void*)(uintptr)CXUI_WINDOW_Maximized, timeS(3));
    }
    if (maxed) {
        cxuiCallSync(closureCreate(uiDeskState, stvar(ptr, &t), stvar(uint32, CXUI_WINDOW_Normal)));
        restored = uiPollSyncWith(uiDeskStateIs, &t, (void*)(uintptr)CXUI_WINDOW_Normal, timeS(3)) &&
                   uiPollSync(uiDeskPlaced, &t, timeS(3));
        cxuiCallSync(closureCreate(uiDeskHideMaximized, stvar(ptr, &t)));
        pending = uiPollSyncWith(uiDeskStateIs, &t, (void*)(uintptr)CXUI_WINDOW_Maximized, timeS(3));
        cxuiCallSync(closureCreate(uiDeskState, stvar(ptr, &t), stvar(uint32, CXUI_WINDOW_Normal)));
        uiPollSyncWith(uiDeskStateIs, &t, (void*)(uintptr)CXUI_WINDOW_Normal, timeS(3));
    }
    if (placed)
        farOff = cxuiCallSync(closureCreate(uiDeskFarOff, stvar(ptr, &t)));
    cxuiShutdown();
    objRelease(&t.w);

    string why = t.why;
    t.why      = NULL;
    if (!clip || !mons)
        TEST_FAIL(1, _SL("clipboard ${int}, monitors ${int}: ${string}"), stvar(int32, clip), stvar(int32, mons),
                  stvar(string, why));
    if (!built || !placed)
        TEST_FAIL(1, _SL("shown ${int}; placed at ${int},${int} ${int}x${int} before showing ${int}"), stvar(int32, built),
                  stvar(int32, t.want.x), stvar(int32, t.want.y), stvar(int32, t.want.w), stvar(int32, t.want.h),
                  stvar(int32, placed));
    if (!(t.caps & CXUI_PLACE_Position) && (t.sizeBeforeShow.w != 500 || t.sizeBeforeShow.h != 400))
        TEST_FAIL(1, _SL("placed but not yet shown, the size was ${int}x${int}"), stvar(int32, t.sizeBeforeShow.w),
                  stvar(int32, t.sizeBeforeShow.h));
    if (t.shownWhilePlaced)
        TEST_FAIL(1, _SL("a placement showed a hidden window"), stvNone);
    // CXUI_TEST_WM says a window manager is running, so a window that isn't maximized is a failure.
    if (!maxed && placed && uiEnvHas(_SL("CXUI_TEST_WM")))
        TEST_FAIL(1, _SL("the window wasn't maximized when asked"), stvNone);
    if (!maxed)
        TEST_INFO(_SL("the window wasn't maximized when asked; no window manager?"), stvNone);
    else if (!restored || !pending)
        TEST_FAIL(1, _SL("restored ${int}; maximized on showing after a placement while hidden ${int}"),
                  stvar(int32, restored), stvar(int32, pending));
    if (!farOff)
        TEST_FAIL(1, _SL("${string}"), stvar(string, why));
    strDestroy(&why);
    return 0;
}

// ---- borderless windows ----

typedef struct UiBorderTest {
    CxUiWindow* w;
    string why;
} UiBorderTest;

static bool uiBorderBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiBorderTest* t = stvlAtPtr(cvars, 0);
    t->w            = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui borderless test", .flags = CXUI_WINDOW_Borderless });
    CxUiBox* col    = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    CxUiLabel* l    = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"No frame" });
    CxUiCanvas* cv  = cxuiCanvasCreate(NULL);
    cxuiWidgetSetNaturalSize(cv, cxuiLen(200), cxuiLen(60));
    cxuiContainerAppend(col, l);
    cxuiContainerAppend(col, cv);
    cxuiContainerAppend(t->w, col);
    objRelease(&l);
    objRelease(&cv);
    objRelease(&col);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// Shown at its content's size with no frame; on Windows, where nothing else places it, centered
// on the work area of the monitor it's on, with the frame the same size as the area inside it.
static bool uiBorderShown(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiBorderTest* t = stvlAtPtr(cvars, 0);
    strClear(&t->why);
    bool shown = false, borderless = false;
    CxUiSize size;
    if (!cxuiTestShown(CxUiWidget(t->w), &shown) || !shown || !cxuiWindowSize(t->w, &size) || size.w < 216 ||
        size.h < 76) {
        strCopy(&t->why, _S"not shown at its content's size");
        return false;
    }
    if (!cxuiTestBorderless(t->w, &borderless) || !borderless) {
        strCopy(&t->why, _S"the native window has a frame");
        return false;
    }
#if defined(_PLATFORM_WIN)
    CxUiPlacement pl;
    sa_CxUiMonitor mons = { 0 };
    cxuiMonitors(&mons);
    bool ok = cxuiWindowPlacement(t->w, &pl) && saSize(mons) > 0;
    CxUiRect r      = pl.normal;
    CxUiMonitor* on = ok ? &mons.a[0] : NULL;
    for (int32 i = 0; ok && i < saSize(mons); i++) {
        CxUiRect b = mons.a[i].bounds;
        if (r.x + r.w / 2 >= b.x && r.x + r.w / 2 < b.x + b.w && r.y + r.h / 2 >= b.y && r.y + r.h / 2 < b.y + b.h)
            on = &mons.a[i];
    }
    CxUiRect wa = on ? on->workArea : (CxUiRect){ 0 };
    float32 s   = on ? on->scale : 1;
    saDestroy(&mons);
    int32 cx = wa.x + (wa.w - r.w) / 2, cy = wa.y + (wa.h - r.h) / 2;
    if (!ok || abs(r.w - (int32)(size.w * s + 0.5f)) > 1 || abs(r.h - (int32)(size.h * s + 0.5f)) > 1 ||
        abs(r.x - cx) > 1 || abs(r.y - cy) > 1) {
        strFormat(&t->why, _SL("the frame is ${int},${int} ${int}x${int} for ${int}x${int} inside; centered would be "
                               "${int},${int}"),
                  stvar(int32, r.x), stvar(int32, r.y), stvar(int32, r.w), stvar(int32, r.h), stvar(int32, size.w),
                  stvar(int32, size.h), stvar(int32, cx), stvar(int32, cy));
        return false;
    }
#endif
    return true;
}

// A window without a frame or title bar, sized to its content.
static int test_cxuid_borderless(void)
{
    UI_START_NATIVE();
    UiBorderTest t = { 0 };
    cxuiCallSync(closureCreate(uiBorderBuild, stvar(ptr, &t)));
    bool shown = uiPollSync(uiBorderShown, &t, timeS(5));
    cxuiShutdown();
    objRelease(&t.w);
    string why = t.why;
    t.why      = NULL;
    if (!shown) {
        logFmt(Error, _SL("${string}"), stvar(string, why));
        strDestroy(&why);
        return 1;
    }
    strDestroy(&why);
    return 0;
}

// ---- progress dialogs ----

typedef struct UiProgTest {
    CxUiWindow* owner;
    CxUiProgressDialog* d;
    int32 ended;
    string why;
} UiProgTest;

static bool uiProgEnded(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    ((UiProgTest*)stvlAtPtr(cvars, 0))->ended++;
    return true;
}

static bool uiProgBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiProgTest* t = stvlAtPtr(cvars, 0);
    t->owner      = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui progress test", .size = { 300, 200 } });
    cxuiWindowShow(t->owner, CXUI_SHOW_Normal);
    t->d = cxuiProgressDialogCreate(&(CxUiProgressDialogDesc){ .title = _S"Copying", .text = _S"Copying files",
                                                               .total = 4, .cancel = true });
    cxuiProgressDialogSetCount(t->d, 3);
    return cxuiWindowShowModal(t->d, t->owner, closureCreate(uiProgEnded, stvar(ptr, t)));
}

static CxUiWidget* uiProgChild(UiProgTest* t, int32 i)
{
    return cxuiContainerChild(cxuiContainerChild(t->d, 0), i);
}

// Shown over its owner with the count on the bar, and a Cancel button that can be clicked
static bool uiProgShown(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiProgTest* t          = stvlAtPtr(cvars, 0);
    CxUiTestControlInfo bi = { 0 };
    bool shown = false, ok = cxuiTestShown(CxUiWidget(t->d), &shown) && shown &&
                             cxuiTestControlInfo(uiProgChild(t, 1), &bi) && bi.range > 0 &&
                             bi.pos * 4 == bi.range * 3 && !bi.indeterminate;
    strClear(&t->why);
    if (!ok)
        strFormat(&t->why, _SL("shown ${int}; the bar at ${int} of ${int}"), stvar(int32, shown), stvar(int32, bi.pos),
                  stvar(int32, bi.range));
    cxuiTestControlInfoDestroy(&bi);
    return ok;
}

static bool uiProgCancel(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    return cxuiTestClick(uiProgChild(stvlAtPtr(cvars, 0), 2));
}

static bool uiProgCancelled(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiProgTest* t = stvlAtPtr(cvars, 0);
    return cxuiProgressDialogCancelled(t->d);
}

static bool uiProgEnd(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiProgTest* t = stvlAtPtr(cvars, 0);
    cxuiDestroy(t->d);
    return t->ended == 1;
}

// A progress dialog shown modal to a window: the bar shows the count, its Cancel button cancels it,
// and destroying it ends the modal show.
static int test_cxuid_progress(void)
{
    UI_START_NATIVE();
    UiProgTest t = { 0 };
    bool built   = cxuiCallSync(closureCreate(uiProgBuild, stvar(ptr, &t)));
    bool shown   = built && uiPollSync(uiProgShown, &t, timeS(5));
    bool cancelled = false, ended = false;
    if (shown) {
        cxuiCallSync(closureCreate(uiProgCancel, stvar(ptr, &t)));
        cancelled = uiPollSync(uiProgCancelled, &t, timeS(5));
        ended     = cxuiCallSync(closureCreate(uiProgEnd, stvar(ptr, &t)));
    }
    cxuiShutdown();
    objRelease(&t.d);
    objRelease(&t.owner);
    string why = t.why;
    t.why      = NULL;
    if (!shown) {
        logFmt(Error, _SL("built ${int}; ${string}"), stvar(int32, built), stvar(string, why));
        strDestroy(&why);
        return 1;
    }
    strDestroy(&why);
    if (!cancelled || !ended)
        TEST_FAIL(1, _SL("Cancel cancelled it ${int}, destroying it ended the modal show ${int}"), stvar(int32, cancelled),
                  stvar(int32, ended));
    return 0;
}

// ---- file dialogs ----

// Answers the innermost file dialog once one is open, with a path or NULL to cancel
static CxUiResult uiAnswerFile(stvlist* cvars, CxUiTimerEvent* ev)
{
    bool cancel = stvlAt(cvars, 1, bool);
    if (cxuiTestFileDialogs() > 0 && cxuiTestAnswerFileDialog(cancel ? NULL : stvlAt(cvars, 0, strref)))
        cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

static bool uiStartFileAnswer(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTimer** out = stvlAtPtr(cvars, 0);
    *out = cxuiTimerCreate(timeMS(200), 0, uiAnswerFile, stvar(strref, stvlAt(cvars, 1, strref)), stvar(bool, stvlAt(cvars, 2, bool)));
    return cxuiTimerStart(*out);
}

// Asked from the test thread, answered on the UI thread
static uint32 uiFileAsk(const CxUiFileDialogDesc* desc, strref answer, string* path)
{
    CxUiTimer* t = NULL;
    cxuiCallSync(closureCreate(uiStartFileAnswer, stvar(ptr, &t), stvar(strref, answer), stvar(bool, answer == NULL)));
    uint32 r = cxuiFileDialog(desc, path);
    objRelease(&t);
    return r;
}

// The platform's file dialogs, each answered as the user would answer it: a folder, an existing
// file, a new file saved without the default extension, and one cancelled.
static int test_cxuid_filedialog(void)
{
    UI_START_NATIVE();
    static const CxUiFileFilter filters[] = { { _S"Text", _S"*.txt" }, { _S"All files", _S"*" } };
    string cur = 0, file = 0, fresh = 0, want = 0, path = 0;
    fsCurDir(&cur);
    pathJoin(&file, cur, _S"cxui-filedialog.txt");
    pathJoin(&fresh, cur, _S"cxui-filedialog-new");
    strConcat(&want, fresh, _S".txt");
    FSFile* f = fsOpen(file, FS_Overwrite);
    if (f)
        fileClose(&f);
    fsDelete(want);

    uint32 folder = uiFileAsk(&(CxUiFileDialogDesc){ .title = _S"Folder", .mode = CXUI_FILE_Folder, .initialDir = cur },
                              cur, &path);
    bool folderOk = folder == CXUI_FILE_Existing && strEqi(path, cur);
    uint32 open   = uiFileAsk(&(CxUiFileDialogDesc){ .title = _S"Open", .filters = filters, .nfilters = 2, .initialDir = cur },
                              file, &path);
    bool openOk   = open == CXUI_FILE_Existing && strEqi(path, file);
    uint32 save   = uiFileAsk(&(CxUiFileDialogDesc){ .title = _S"Save", .mode = CXUI_FILE_Save, .filters = filters,
                                                     .nfilters = 2, .defaultExt = _S"txt", .initialDir = cur },
                              fresh, &path);
    bool saveOk   = save == CXUI_FILE_New && strEqi(path, want);
    uint32 cancel = uiFileAsk(&(CxUiFileDialogDesc){ .title = _S"Cancel" }, NULL, &path);
    bool cancelOk = cancel == CXUI_FILE_Cancelled && strEmpty(path);
    cxuiShutdown();

    fsDelete(file);
    if (!folderOk || !openOk || !saveOk || !cancelOk)
        TEST_FAIL(1, _SL("folder ${uint} ${int}, open ${uint} ${int}, save ${uint} ${int} (want ${string}), cancelled "
                         "${uint}; last path ${string}"),
                  stvar(uint32, folder), stvar(int32, folderOk), stvar(uint32, open), stvar(int32, openOk),
                  stvar(uint32, save), stvar(int32, saveOk), stvar(string, want), stvar(uint32, cancel),
                  stvar(string, path));
    strDestroy(&cur);
    strDestroy(&file);
    strDestroy(&fresh);
    strDestroy(&want);
    strDestroy(&path);
    return 0;
}

// ---- tabs on several lines ----

typedef struct UiWrapTest {
    CxUiWindow* w;
    CxUiTabs* tabs;
    CxUiCanvas* cv;   // at the right of the first page
    int32 paints;
    int32 paintsBefore;
    int32 wideTop;    // where the platform put the pages in the wide window
    string why;
} UiWrapTest;

static CxUiResult uiWrapPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    ((UiWrapTest*)stvlAtPtr(cvars, 0))->paints++;
    CxUiSize sz = cxuiDrawSize(ev->draw);
    cxuiDrawFill(ev->draw, 0, 0, sz.w, sz.h, cxuiHex(0x203040));
    return CXUI_Handled;
}

static bool uiWrapBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWrapTest* t = stvlAtPtr(cvars, 0);
    string title  = 0;
    t->w          = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui tabs on several lines", .size = { 1200, 300 },
                                                        .sizing = CXUI_SIZE_Start });
    t->tabs       = cxuiTabsCreate(&(CxUiTabsDesc){ .flags = CXUI_TABS_Multiline });
    for (int32 i = 0; i < 10; i++) {
        strFormat(&title, _SL("Page number ${int}"), stvar(int32, i + 1));
        CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = title });
        if (i == 0) {
            CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_H });
            CxUiFill* fill = cxuiFillCreate();
            t->cv = cxuiCanvasCreate(NULL);
            cxuiWidgetSetNaturalSize(t->cv, cxuiLen(40), cxuiLen(40));
            cxuiOnPaint(t->cv, uiWrapPaint, stvar(ptr, t));
            cxuiContainerAppend(row, l);
            cxuiContainerAppend(row, fill);
            cxuiContainerAppend(row, t->cv);
            cxuiTabsAdd(t->tabs, row, title);
            objRelease(&fill);
            objRelease(&row);
        } else {
            cxuiTabsAdd(t->tabs, l, title);
        }
        objRelease(&l);
    }
    strDestroy(&title);
    cxuiContainerAppend(t->w, t->tabs);
    cxuiWindowShow(t->w, CXUI_SHOW_Normal);
    return true;
}

// The pages are where the platform's control puts them, so they are clear of every line of tabs;
// `narrow` wants more lines than the wide window had.
static bool uiWrapPlaced(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWrapTest* t = stvlAtPtr(cvars, 0);
    bool narrow   = stvlAt(cvars, 1, int32) != 0;
    CxUiTestControlInfo ci;
    strClear(&t->why);
    if (!cxuiTestControlInfo(CxUiWidget(t->tabs), &ci)) {
        strCopy(&t->why, _S"the tabs can't be read back");
        return false;
    }
    bool ok = true;
    // Windows and macOS wrap tabs and say where they put the pages.
#if defined(_PLATFORM_WIN) || defined(_PLATFORM_MACOS)
    bool wraps = true;
#else
    bool wraps = false;
#endif
    if (ci.display.h > 0) {
        ok = ci.page.x == ci.display.x && ci.page.y == ci.display.y && ci.page.w == ci.display.w &&
             ci.page.h == ci.display.h && (!narrow || !wraps || ci.display.y > t->wideTop);
        if (!ok)
            strFormat(&t->why, _SL("the pages at ${int},${int} ${int}x${int}, the control's own at ${int},${int} ${int}x${int}; wide it was ${int} down"),
                      stvar(int32, ci.page.x), stvar(int32, ci.page.y), stvar(int32, ci.page.w), stvar(int32, ci.page.h),
                      stvar(int32, ci.display.x), stvar(int32, ci.display.y), stvar(int32, ci.display.w),
                      stvar(int32, ci.display.h), stvar(int32, t->wideTop));
        if (!narrow)
            t->wideTop = ci.display.y;
    }
    cxuiTestControlInfoDestroy(&ci);
    return ok;
}

static bool uiWrapPainted(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWrapTest* t = stvlAtPtr(cvars, 0);
    return t->paints > t->paintsBefore;
}

// The canvas is to be drawn again, and before it is, the tabs are measured narrower than they are
static bool uiWrapMeasureNarrow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWrapTest* t   = stvlAtPtr(cvars, 0);
    t->paintsBefore = t->paints;
    cxuiCanvasInvalidate(t->cv, 0, 0, 40, 40);
    cxuiWidgetHeightFor(t->tabs, 200);
    return true;
}

static bool uiWrapPick(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiWrapTest* t   = stvlAtPtr(cvars, 0);
    t->paintsBefore = t->paints;
    bool picked     = cxuiTestChoose(CxUiWidget(t->tabs), stvlAt(cvars, 1, int32), false);
    // Drawn only if its page is on screen
    cxuiCanvasInvalidate(t->cv, 0, 0, 40, 40);
    return picked;
}

static bool uiWrapNarrow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWindowSetSize(((UiWrapTest*)stvlAtPtr(cvars, 0))->w, 260, 300);
    return true;
}

// Tabs on several lines through the platform: the pages go where the control puts them, wide and
// narrow, and a narrow window gives the tabs more lines (Windows and macOS; GTK scrolls them). Measuring them narrow doesn't lose a drawing waiting on their pages, and a page picked
// is drawn.
static int test_cxuid_tabswrap(void)
{
    UI_START_NATIVE();
    UiWrapTest t = { 0 };
    cxuiCallSync(closureCreate(uiWrapBuild, stvar(ptr, &t)));
    bool wide   = uiPollSyncWithInt(uiWrapPlaced, &t, 0, timeS(5)) && uiPollSync(uiWrapPainted, &t, timeS(5));
    bool drawn  = false;
    bool narrow = false;
    if (wide) {
        cxuiCallSync(closureCreate(uiWrapMeasureNarrow, stvar(ptr, &t)));
        drawn = uiPollSync(uiWrapPainted, &t, timeS(5));
    }
    if (drawn) {
        drawn = cxuiCallSync(closureCreate(uiWrapPick, stvar(ptr, &t), stvar(int32, 1))) &&
                cxuiCallSync(closureCreate(uiWrapPick, stvar(ptr, &t), stvar(int32, 0))) &&
                uiPollSync(uiWrapPainted, &t, timeS(5));
    }
    if (drawn) {
        cxuiCallSync(closureCreate(uiWrapNarrow, stvar(ptr, &t)));
        narrow = uiPollSyncWithInt(uiWrapPlaced, &t, 1, timeS(5));
    }
    cxuiShutdown();
    objRelease(&t.cv);
    objRelease(&t.tabs);
    objRelease(&t.w);
    int ret = 0;
    if (!wide || !drawn || !narrow)
        TEST_FAILV(ret, 1, _SL("wide ${int}, drawn after a narrow measure and picked ${int}, narrow ${int}: ${string}"),
                   stvar(int32, wide), stvar(int32, drawn), stvar(int32, narrow), stvar(string, t.why));
    strDestroy(&t.why);
    return ret;
}

// ---- color dialogs ----

static CxUiResult uiAnswerColor(stvlist* cvars, CxUiTimerEvent* ev)
{
    if (cxuiTestColorDialogs() > 0 && cxuiTestAnswerColorDialog(stvlAtPtr(cvars, 0)))
        cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

static bool uiStartColorAnswer(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTimer** out = stvlAtPtr(cvars, 0);
    *out            = cxuiTimerCreate(timeMS(200), 0, uiAnswerColor, stvar(ptr, stvlAtPtr(cvars, 1)));
    return cxuiTimerStart(*out);
}

static bool uiColorParent(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow** out = stvlAtPtr(cvars, 0);
    *out = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui color dialog test", .size = { 300, 200 } });
    cxuiWindowShow(*out, CXUI_SHOW_Normal);
    return true;
}

// Asked from the test thread, answered on the UI thread
static bool uiColorAsk(const CxUiColorDialogDesc* desc, const CxUiColor* answer, CxUiColor* color)
{
    CxUiTimer* t = NULL;
    cxuiCallSync(closureCreate(uiStartColorAnswer, stvar(ptr, &t), stvar(ptr, (void*)answer)));
    bool chosen = cxuiColorDialog(desc, color);
    objRelease(&t);
    return chosen;
}

// The platform's color dialog, answered as the user would: a color chosen, over a window, keeping
// the alpha; then one cancelled.
static int test_cxuid_colordialog(void)
{
    UI_START_NATIVE();
    CxUiWindow* parent = NULL;
    cxuiCallSync(closureCreate(uiColorParent, stvar(ptr, &parent)));
    CxUiColor answer = cxuiRGB(12, 160, 240);
    CxUiColor c      = cxuiRGBA(200, 100, 50, 77);
    bool chosen      = uiColorAsk(&(CxUiColorDialogDesc){ .parent = parent, .title = _S"Tint" }, &answer, &c);
    CxUiColor kept   = c;
    bool cancelled   = !uiColorAsk(&(CxUiColorDialogDesc){ 0 }, NULL, &kept) && kept.bits == c.bits;
    cxuiShutdown();
    objRelease(&parent);
    if (!chosen || c.bits != cxuiRGBA(12, 160, 240, 77).bits || !cancelled)
        TEST_FAIL(1, _SL("chosen ${int}: ${uint(hex)}; cancelled ${int}"), stvar(int32, chosen), stvar(uint32, c.bits),
                  stvar(int32, cancelled));
    return 0;
}

// ---- owned windows ----

typedef struct UiOwnTest {
    CxUiWindow* main;
    CxUiWindow* tool;
    CxUiWindow* other;
    bool moved;
    bool inPopup;
    string why;
} UiOwnTest;

static bool ownedBy(CxUiWindow* w, CxUiWindow* owner)
{
    bool owned = false;
    return cxuiTestOwnedBy(w, owner, &owned) && owned;
}

static bool uiOwnBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiOwnTest* t = stvlAtPtr(cvars, 0);
    t->main  = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui owner test", .size = { 400, 300 }, .sizing = CXUI_SIZE_Start });
    t->tool  = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui owned window", .size = { 200, 150 }, .sizing = CXUI_SIZE_Start });
    t->other = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui other window", .size = { 200, 100 }, .sizing = CXUI_SIZE_Start });
    cxuiWindowSetPosition(t->main, 100, 100);
    cxuiWindowShow(t->main, CXUI_SHOW_Normal);
    cxuiWindowShow(t->other, CXUI_SHOW_NoActivate);
    cxuiWindowSetOwner(t->tool, t->main);
    t->moved = cxuiWindowMoveNear(t->tool, CxUiWindowNone, CXUI_NEAR_Right);
    cxuiWindowShow(t->tool, CXUI_SHOW_NoActivate);
    return true;
}

// Owned by the main window, and beside it where windows can be placed
static bool uiOwnPlaced(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiOwnTest* t = stvlAtPtr(cvars, 0);
    strClear(&t->why);
    if (!ownedBy(t->tool, t->main)) {
        strCopy(&t->why, _S"the tool window isn't owned by the main one");
        return false;
    }
    bool placeable = (cxuiPlacementCaps() & CXUI_PLACE_Position) != 0;
    if (t->moved != placeable) {
        strFormat(&t->why, _SL("moved ${int} where windows can be placed ${int}"), stvar(int32, t->moved),
                  stvar(int32, placeable));
        return false;
    }
    if (!placeable)
        return true;
    CxUiPlacement m, o;
    cxuiWindowPlacement(t->main, &m);
    cxuiWindowPlacement(t->tool, &o);
    // Frames can have invisible borders, which are taken off where the platform reports them.
    int32 gap  = o.normal.x - (m.normal.x + m.normal.w);
    int32 skew = (o.normal.y + o.normal.h / 2) - (m.normal.y + m.normal.h / 2);
    if (gap < -32 || gap > 32 || skew < -8 || skew > 8) {
        strFormat(&t->why, _SL("the main window at ${int},${int} ${int}x${int}, the tool window at ${int},${int} ${int}x${int}"),
                  stvar(int32, m.normal.x), stvar(int32, m.normal.y), stvar(int32, m.normal.w), stvar(int32, m.normal.h),
                  stvar(int32, o.normal.x), stvar(int32, o.normal.y), stvar(int32, o.normal.w), stvar(int32, o.normal.h));
        return false;
    }
    return true;
}

static CxUiResult uiOwnInPopup(stvlist* cvars, CxUiTimerEvent* ev)
{
    UiOwnTest* t = stvlAtPtr(cvars, 0);
    t->inPopup   = ownedBy(t->tool, t->other);
    cxuiWindowEndPopup(t->tool, 1);
    cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

// A popup over the other window, then the main window destroyed
static bool uiOwnAfter(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiOwnTest* t = stvlAtPtr(cvars, 0);
    CxUiTimer* timer = cxuiTimerCreate(timeMS(100), 0, uiOwnInPopup, stvar(ptr, t));
    cxuiTimerStart(timer);
    cxuiWindowPopup(t->tool, t->other);
    objRelease(&timer);
    bool back = ownedBy(t->tool, t->main);
    cxuiWindowShow(t->tool, CXUI_SHOW_NoActivate);
    cxuiDestroy(t->main);
    bool shown = false;
    bool kept  = cxuiTestShown(CxUiWidget(t->tool), &shown) && shown && ownedBy(t->tool, CxUiWindowNone);
    if (!t->inPopup || !back || !kept)
        strFormat(&t->why, _SL("owned by the popup's parent ${int}, then its own owner again ${int}; open and owned by "
                               "nothing once that was destroyed ${int}"),
                  stvar(int32, t->inPopup), stvar(int32, back), stvar(int32, kept));
    return t->inPopup && back && kept;
}

// A window owned by another, through the platform: beside it where windows can be placed, owned
// by a popup's parent while the popup runs and then by its owner again, and left open, owned by
// nothing, when its owner is destroyed.
static int test_cxuid_owned(void)
{
    UI_START_NATIVE();
    UiOwnTest t = { 0 };
    cxuiCallSync(closureCreate(uiOwnBuild, stvar(ptr, &t)));
    bool placed = uiPollSync(uiOwnPlaced, &t, timeS(5));
    bool after  = placed && cxuiCallSync(closureCreate(uiOwnAfter, stvar(ptr, &t)));
    cxuiShutdown();
    objRelease(&t.main);
    objRelease(&t.tool);
    objRelease(&t.other);
    int ret = 0;
    if (!placed || !after)
        TEST_FAILV(ret, 1, _SL("placed ${int}, popup and owner destroyed ${int}: ${string}"), stvar(int32, placed),
                   stvar(int32, after), stvar(string, t.why));
    strDestroy(&t.why);
    return ret;
}

// ---- a window realized without being shown ----

static bool uiHiddenBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRvTest* t = stvlAtPtr(cvars, 0);
    t->drawNull = true;
    t->w        = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui hidden window test", .size = { 320, 240 } });
    t->view     = cxuiRenderViewCreate(NULL);
    cxuiOnSurface(t->view, uiRvSurface, stvar(ptr, t));
    cxuiContainerAppend(t->w, t->view);
    return cxuiWindowRealize(t->w);
}

static bool uiHiddenState(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRvTest* t = stvlAtPtr(cvars, 0);
    CxUiNativeSurface s;
    CxUiSize size;
    bool shown = true;
    strClear(&t->why);
    bool ok = cxuiTestShown(CxUiWidget(t->w), &shown) && !shown && cxuiWindowSize(t->w, &size) && size.w == 320 &&
              size.h == 240 && !cxuiWindowIsVisible(t->w);
#if defined(_PLATFORM_WIN)
    // A renderer can start on a window that is never shown.
    ok = ok && t->available == 1 && cxuiRenderViewSurface(t->view, &s) && s.native && IsWindow((HWND)s.native) &&
         s.w == (int32)(320 * cxuiWidgetScale(t->view) + 0.5f);
#elif defined(_PLATFORM_MACOS)
    ok = ok && t->available == 1 && cxuiRenderViewSurface(t->view, &s) && s.native && s.kind == CXUI_SURFACE_Metal &&
         s.w == (int32)(320 * cxuiWidgetScale(t->view) + 0.5f);
#else
    ok = ok && t->available == 0 && !cxuiRenderViewSurface(t->view, &s);
#endif
    if (!ok)
        strFormat(&t->why, _SL("shown ${int} natively ${int}, ${int}x${int}; ${int} surfaces"),
                  stvar(int32, cxuiWindowIsVisible(t->w)), stvar(int32, shown), stvar(int32, size.w),
                  stvar(int32, size.h), stvar(int32, t->available));
    return ok;
}

static bool uiHiddenShow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    cxuiWindowShow(((UiRvTest*)stvlAtPtr(cvars, 0))->w, CXUI_SHOW_Normal);
    return true;
}

// A window realized and never shown lays its content out at its size; on Windows and macOS a
// render view in it has its surface, for a renderer to start without the window appearing, and
// elsewhere it comes when the window is shown.
static int test_cxuid_hidden(void)
{
    UI_START_NATIVE();
    UiRvTest t    = { 0 };
    bool realized = cxuiCallSync(closureCreate(uiHiddenBuild, stvar(ptr, &t)));
    osSleep(timeMS(300));
    bool hidden = realized && uiPollSync(uiHiddenState, &t, timeS(5));
    bool came   = false;
    if (hidden) {
        cxuiCallSync(closureCreate(uiHiddenShow, stvar(ptr, &t)));
        came = uiTxWaitCount(&t.available, 1) && t.available == 1;
    }
    cxuiShutdown();
    objRelease(&t.view);
    objRelease(&t.w);
    int ret = 0;
    if (!hidden || !came)
        TEST_FAILV(ret, 1, _SL("realized ${int}, hidden as expected ${int}, the surface once shown ${int}: ${string}"),
                   stvar(int32, realized), stvar(int32, hidden), stvar(int32, came), stvar(string, t.why));
    strDestroy(&t.why);
    return ret;
}

// ---- idle ----

static CxUiResult uiIdlePaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    unused_noeval(cvars);
    CxUiSize sz = cxuiDrawSize(ev->draw);
    cxuiDrawFill(ev->draw, 0, 0, sz.w, sz.h, cxuiHex(0x203040));
    return CXUI_Handled;
}

// A window with most kinds of widget: a focused text field, a list, a dropdown, a check box, a
// slider, a progress bar, a drawn button, a canvas, tabs, a grid in a scroll box and a menu bar
static bool uiIdleBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow** w             = stvlAtPtr(cvars, 0);
    static const strref items[] = { _S"One", _S"Two", _S"Three", NULL };
    *w           = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"cxui idle test", .size = { 520, 520 } });
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 6, .margin = { 8, 8, 8, 8 } });
    CxUiTextField* field = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = _S"Typing goes here" });
    cxuiContainerAppend(col, field);
    CxUiWidget* parts[] = {
        CxUiWidget(cxuiListCreate(&(CxUiListDesc){ .items = items, .value = 1, .visibleItems = 3 })),
        CxUiWidget(cxuiDropdownCreate(&(CxUiDropdownDesc){ .items = items, .value = 0 })),
        CxUiWidget(cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"A check box", .state = CXUI_CHECK_On })),
        CxUiWidget(cxuiSliderCreate(&(CxUiSliderDesc){ .min = 0, .max = 10, .step = 1, .value = 3 })),
        CxUiWidget(cxuiProgressCreate(&(CxUiProgressDesc){ .value = 40 })),
        CxUiWidget(cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"Drawn" })),
    };
    for (int32 i = 0; i < (int32)(sizeof(parts) / sizeof(parts[0])); i++) {
        cxuiContainerAppend(col, parts[i]);
        objRelease(&parts[i]);
    }

    CxUiCanvas* cv = cxuiCanvasCreate(NULL);
    cxuiWidgetSetNaturalSize(cv, cxuiLen(120), cxuiLen(40));
    cxuiOnPaint(cv, uiIdlePaint, stvNone);
    cxuiContainerAppend(col, cv);
    objRelease(&cv);

    CxUiTabs* tabs = cxuiTabsCreate(NULL);
    for (int32 i = 0; i < 2; i++) {
        CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = i ? _S"Second page" : _S"First page" });
        cxuiTabsAdd(tabs, l, i ? _S"Second" : _S"First");
        objRelease(&l);
    }
    cxuiContainerAppend(col, tabs);
    objRelease(&tabs);

    CxUiListStore* store = cxuiListStoreCreate(1);
    string row           = 0;
    for (int32 i = 0; i < 30; i++) {
        strFormat(&row, _S"row ${int}", stvar(int32, i));
        cxuiListStoreAppend(store, row);
    }
    strDestroy(&row);
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){ .lp.size[1] = cxuiLen(300) });
    cxuiGridSetSource(g, store);
    objRelease(&store);
    CxUiScrollBox* sb = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .view = { 0, 100 } });
    cxuiContainerAppend(sb, g);
    objRelease(&g);
    cxuiContainerAppend(col, sb);
    objRelease(&sb);

    CxUiMenu* bar  = cxuiMenuCreate();
    CxUiMenu* file = cxuiMenuCreate();
    cxuiMenuAppendItem(file, .title = _S"&Open...", .hint = _S"Ctrl+O");
    cxuiMenuAppendItem(bar, .title = _S"&File", .submenu = file);
    objRelease(&file);
    cxuiWindowSetMenu(*w, bar);
    objRelease(&bar);

    cxuiContainerAppend(*w, col);
    objRelease(&col);
    cxuiWindowShow(*w, CXUI_SHOW_Normal);
    cxuiFocus(field);
    objRelease(&field);
    return true;
}

static int64 uiIdleCpu(void)
{
    ProcCPUTimes cpu;
    return procCPUTimesByID(&cpu, procCurrentID()) ? cpu.total : -1;
}

// Nothing wakes the UI thread while a window with most kinds of widget sits idle: once it has
// settled, its queue doesn't run for a second, asks to be run at no time, and the process uses
// next to no processor time. The platform's own caret blinking runs outside cxui.
static int test_cxuid_idle(void)
{
    UI_START_NATIVE();
    CxUiWindow* w = NULL;
    cxuiCallSync(closureCreate(uiIdleBuild, stvar(ptr, &w)));

    // Settled: the queue hasn't run for half a second
    CxUiTestLoopStats before, after;
    int64 end = clockTimer() + timeS(10);
    cxuiTestLoopStats(&before);
    for (;;) {
        osSleep(timeMS(500));
        cxuiTestLoopStats(&after);
        if (after.ticks == before.ticks || clockTimer() > end)
            break;
        before = after;
    }
    bool settled = after.ticks == before.ticks;
    int64 cpu0   = uiIdleCpu();
    int64 t0     = clockTimer();
    cxuiTestLoopStats(&before);
    osSleep(timeS(1));
    cxuiTestLoopStats(&after);
    int64 cpu = uiIdleCpu() - cpu0, wall = clockTimer() - t0;

    cxuiCallSync(closureCreate(uiDestroyWindow, stvar(ptr, w)));
    cxuiShutdown();
    objRelease(&w);
    int ret = 0;
    // Under 5% of one processor: a caret blinking under Rosetta takes about 2%, a platform redrawing
    // every frame far more.
    if (!settled || after.ticks != before.ticks || after.next != timeForever || cpu0 < 0 || cpu * 20 > wall)
        TEST_FAILV(ret, 1, _SL("settled ${int}; the queue ran ${uint} times in ${int} ms idle, last asking to wait ${int} us; ${int} us of processor time"),
                   stvar(int32, settled), stvar(uint64, after.ticks - before.ticks), stvar(int64, wall / 1000),
                   stvar(int64, after.next), stvar(int64, cpu));
    return ret;
}

testfunc cxuidisplaytest_funcs[] = {
    { "sessions", test_cxuid_sessions },
    { "flood", test_cxuid_flood },
    { "grp_loop", test_cxuid_grp_loop },
    { "window", test_cxuid_window },
    { "canvas", test_cxuid_canvas },
    { "layout", test_cxuid_layout },
    { "containers", test_cxuid_containers },
    { "text", test_cxuid_text },
    { "grid", test_cxuid_grid },
    { "gridbox", test_cxuid_gridbox },
    { "menus", test_cxuid_menus },
    { "tabs", test_cxuid_tabs },
    { "tabswrap", test_cxuid_tabswrap },
    { "syscolors", test_cxuid_syscolors },
    { "controls", test_cxuid_controls },
    { "checks", test_cxuid_checks },
    { "lists", test_cxuid_lists },
    { "listmulti", test_cxuid_listmulti },
    { "renderview", test_cxuid_renderview },
    { "textarea", test_cxuid_textarea },
    { "sliders", test_cxuid_sliders },
    { "scrollbox", test_cxuid_scrollbox },
    { "frames", test_cxuid_frames },
    { "desktop", test_cxuid_desktop },
    { "borderless", test_cxuid_borderless },
    { "progress", test_cxuid_progress },
    { "filedialog", test_cxuid_filedialog },
    { "colordialog", test_cxuid_colordialog },
    { "owned", test_cxuid_owned },
    { "hidden", test_cxuid_hidden },
    { "idle", test_cxuid_idle },
#if defined(_PLATFORM_WIN)
    { "themechange", test_cxuid_themechange },
#endif
    { "dpi", test_cxuid_dpi },
#if defined(_PLATFORM_WIN)
    { "capture", test_cxuid_capture },
#endif
    { "msgbox", test_cxuid_msgbox },
    { "popup", test_cxuid_popup },
    { "grp_modal", test_cxuid_grp_modal },
    { 0, 0 }
};
