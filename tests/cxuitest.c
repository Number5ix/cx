#include <cx/closure.h>
#include <cx/format.h>
#include <cx/fs.h>
#include <cx/sys.h>
#include <cx/string.h>
#include <cx/thread.h>
#include <cx/time.h>
#include <cxui.h>
#include <cxui/test/cxuitest.h>
#include "tqtestobj.h"

#define TEST_FILE cxuitest
#define TEST_FUNCS cxuitest_funcs
#include "common.h"
#include "cxuitestobj.h"

#include <math.h>
#include <stdlib.h>

static const CxUiStartDesc nullStart = { .name = _S"Test UI", .flags = CXUI_Start_Null };

// What a closure saw when it ran, for the test thread to check afterwards.
typedef struct UiRunRecord {
    Event done;
    atomic(int32) runs;
    atomic(int32) destroys;
    atomic(bool) onUIThread;
    atomic(bool) innerInline;
} UiRunRecord;

static void uiRecordInit(UiRunRecord* rec)
{
    memset(rec, 0, sizeof(*rec));
    eventInit(&rec->done);
}

static bool uiRecordRun(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRunRecord* rec = stvlAtPtr(cvars, 0);
    atomicStore(bool, &rec->onUIThread, cxuiIsUIThread(), Release);
    atomicFetchAdd(int32, &rec->runs, 1, AcqRel);
    eventSignal(&rec->done);
    return true;
}

static void uiRecordDestroy(stvlist* cvars)
{
    UiRunRecord* rec = stvlAtPtr(cvars, 0);
    atomicFetchAdd(int32, &rec->destroys, 1, AcqRel);
}

static closure uiRecordClosure(UiRunRecord* rec)
{
    closure cls = closureCreate(uiRecordRun, stvar(ptr, rec));
    closureSetDestroy(cls, uiRecordDestroy);
    return cls;
}

// Start and shutdown repeat, and every query agrees with the state.
static int test_cxui_startstop(void)
{
    for (int32 i = 0; i < 5; i++) {
        if (cxuiIsRunning())
            TEST_FAIL(1, _SL("round ${int}: running before cxuiStart"), stvar(int32, i));
        if (!cxuiStart(&nullStart))
            TEST_FAIL(1, _SL("round ${int}: cxuiStart failed"), stvar(int32, i));
        if (!cxuiIsRunning())
            TEST_FAIL(1, _SL("round ${int}: not running after cxuiStart"), stvar(int32, i));
        if (cxuiIsUIThread())
            TEST_FAIL(1, _SL("round ${int}: the test thread claims to be the UI thread"), stvar(int32, i));

        TaskQueue* q = cxuiQueue();
        if (!q)
            TEST_FAIL(1, _SL("round ${int}: no UI queue while running"), stvar(int32, i));
        tqRelease(&q);

        cxuiShutdown();
        if (cxuiIsRunning())
            TEST_FAIL(1, _SL("round ${int}: still running after cxuiShutdown"), stvar(int32, i));
        q = cxuiQueue();
        if (q) {
            tqRelease(&q);
            TEST_FAIL(1, _SL("round ${int}: a UI queue after cxuiShutdown"), stvar(int32, i));
        }
    }

    cxuiShutdown();   // unmatched: does nothing
    if (cxuiIsRunning())
        TEST_FAIL(1, _SL("an unmatched cxuiShutdown started the UI"), stvNone);
    return 0;
}

// Starts are counted: the UI stops only when the last one is matched.
static int test_cxui_startcount(void)
{
    if (!cxuiStart(&nullStart) || !cxuiStart(NULL))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    cxuiShutdown();
    if (!cxuiIsRunning())
        TEST_FAIL(1, _SL("stopped after the first of two cxuiShutdown calls"), stvNone);

    cxuiShutdown();
    if (cxuiIsRunning())
        TEST_FAIL(1, _SL("still running after both cxuiShutdown calls"), stvNone);
    return 0;
}

static bool uiStartFromUI(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRunRecord* rec = stvlAtPtr(cvars, 0);
    bool ok          = cxuiStart(NULL);
    if (ok)
        cxuiShutdown();   // not the last, so allowed here
    atomicStore(bool, &rec->innerInline, ok && cxuiIsRunning(), Release);
    eventSignal(&rec->done);
    return true;
}

// A library running on the UI thread can start and shut down as long as it isn't the last.
static int test_cxui_startonui(void)
{
    UiRunRecord rec;
    uiRecordInit(&rec);
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    cxuiPost(closureCreate(uiStartFromUI, stvar(ptr, &rec)));
    bool ran = eventWaitTimeout(&rec.done, timeS(10));
    cxuiShutdown();
    eventDestroy(&rec.done);

    if (!ran)
        TEST_FAIL(1, _SL("the posted closure never ran"), stvNone);
    if (!atomicLoad(bool, &rec.innerInline, Acquire))
        TEST_FAIL(1, _SL("cxuiStart/cxuiShutdown on the UI thread failed or stopped the UI"), stvNone);
    if (cxuiIsRunning())
        TEST_FAIL(1, _SL("still running after the last cxuiShutdown"), stvNone);
    return 0;
}

// Posted closures run on the UI thread, and are destroyed exactly once.
static int test_cxui_post(void)
{
    UiRunRecord rec;
    uiRecordInit(&rec);
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    bool queued = cxuiPost(uiRecordClosure(&rec));
    bool ran    = queued && eventWaitTimeout(&rec.done, timeS(10));
    cxuiShutdown();

    int ret = 0;
    if (!queued)
        TEST_FAILV(ret, 1, _SL("cxuiPost failed while running"), stvNone);
    else if (!ran)
        TEST_FAILV(ret, 1, _SL("the posted closure never ran"), stvNone);
    else if (!atomicLoad(bool, &rec.onUIThread, Acquire))
        TEST_FAILV(ret, 1, _SL("the posted closure ran off the UI thread"), stvNone);
    if (atomicLoad(int32, &rec.destroys, Acquire) != 1)
        TEST_FAILV(ret, 1, _SL("closure destroyed ${int} times"), stvar(int32, atomicLoad(int32, &rec.destroys, Acquire)));

    // not running: refused, and destroyed at once
    eventDestroy(&rec.done);
    uiRecordInit(&rec);
    if (cxuiPost(uiRecordClosure(&rec)))
        TEST_FAILV(ret, 1, _SL("cxuiPost succeeded with the UI stopped"), stvNone);
    if (atomicLoad(int32, &rec.runs, Acquire) != 0 || atomicLoad(int32, &rec.destroys, Acquire) != 1)
        TEST_FAILV(ret, 1, _SL("refused closure: ran ${int} times, destroyed ${int} times"),
                   stvar(int32, atomicLoad(int32, &rec.runs, Acquire)),
                   stvar(int32, atomicLoad(int32, &rec.destroys, Acquire)));
    eventDestroy(&rec.done);
    return ret;
}

static bool uiReturn(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRunRecord* rec = stvlAtPtr(cvars, 0);
    atomicStore(bool, &rec->onUIThread, cxuiIsUIThread(), Release);
    atomicFetchAdd(int32, &rec->runs, 1, AcqRel);
    return stvlAt(cvars, 1, bool);
}

static bool uiCallSyncInline(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiRunRecord* rec = stvlAtPtr(cvars, 0);
    UiRunRecord inner;
    uiRecordInit(&inner);
    // On the UI thread the call has run by the time cxuiCallSync returns.
    bool result = cxuiCallSync(closureCreate(uiReturn, stvar(ptr, &inner), stvar(bool, true)));
    atomicStore(bool, &rec->innerInline, result && atomicLoad(int32, &inner.runs, Acquire) == 1, Release);
    eventDestroy(&inner.done);
    eventSignal(&rec->done);
    return true;
}

// cxuiCallSync returns the closure's result, runs it on the UI thread, and runs it inline there.
static int test_cxui_callsync(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    int ret = 0;
    for (int32 want = 0; want < 2; want++) {
        UiRunRecord rec;
        uiRecordInit(&rec);
        bool got = cxuiCallSync(closureCreate(uiReturn, stvar(ptr, &rec), stvar(bool, want != 0)));
        if (got != (want != 0) || atomicLoad(int32, &rec.runs, Acquire) != 1)
            TEST_FAILV(ret, 1, _SL("returned ${int} (want ${int}), ran ${int} times"), stvar(int32, got),
                       stvar(int32, want), stvar(int32, atomicLoad(int32, &rec.runs, Acquire)));
        if (!atomicLoad(bool, &rec.onUIThread, Acquire))
            TEST_FAILV(ret, 1, _SL("the call ran off the UI thread"), stvNone);
        eventDestroy(&rec.done);
    }

    UiRunRecord rec;
    uiRecordInit(&rec);
    cxuiPost(closureCreate(uiCallSyncInline, stvar(ptr, &rec)));
    if (!eventWaitTimeout(&rec.done, timeS(10)))
        TEST_FAILV(ret, 1, _SL("the posted closure never ran"), stvNone);
    else if (!atomicLoad(bool, &rec.innerInline, Acquire))
        TEST_FAILV(ret, 1, _SL("cxuiCallSync on the UI thread didn't run the call inline"), stvNone);
    eventDestroy(&rec.done);

    cxuiShutdown();

    // not running: false, and the closure is still destroyed
    uiRecordInit(&rec);
    closure cls = closureCreate(uiReturn, stvar(ptr, &rec), stvar(bool, true));
    closureSetDestroy(cls, uiRecordDestroy);
    if (cxuiCallSync(cls) || atomicLoad(int32, &rec.runs, Acquire) != 0 ||
        atomicLoad(int32, &rec.destroys, Acquire) != 1)
        TEST_FAILV(ret, 1, _SL("with the UI stopped: ran ${int} times, destroyed ${int} times"),
                   stvar(int32, atomicLoad(int32, &rec.runs, Acquire)),
                   stvar(int32, atomicLoad(int32, &rec.destroys, Acquire)));
    eventDestroy(&rec.done);
    return ret;
}

typedef struct UiBudgetState {
    Event done;
    atomic(int32) ran;
    int32 total;
} UiBudgetState;

static bool uiBusyTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiBudgetState* st = stvlAtPtr(cvars, 0);
    osSleep(timeMS(1));
    if (atomicFetchAdd(int32, &st->ran, 1, AcqRel) + 1 == st->total)
        eventSignal(&st->done);
    return true;
}

static bool uiPostBusyTasks(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    UiBudgetState* st = stvlAtPtr(cvars, 0);
    // Posted from the UI thread, all of these share one wake, so only the over-budget re-arm can
    // bring the loop back for those that don't fit in this tick.
    for (int32 i = 0; i < st->total; i++)
        cxuiPost(closureCreate(uiBusyTask, stvar(ptr, st)));
    return true;
}

// More work than one tick's budget allows still all runs: the loop comes back for the rest.
static int test_cxui_budget(void)
{
    UiBudgetState st = { .total = 50 };
    eventInit(&st.done);
    CxUiStartDesc desc = nullStart;
    desc.tickBudget    = timeMS(2);
    if (!cxuiStart(&desc))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    cxuiPost(closureCreate(uiPostBusyTasks, stvar(ptr, &st)));
    bool all = eventWaitTimeout(&st.done, timeS(10));
    cxuiShutdown();
    eventDestroy(&st.done);

    if (!all)
        TEST_FAIL(1, _SL("only ${int} of ${int} tasks ran"), stvar(int32, atomicLoad(int32, &st.ran, Acquire)),
                  stvar(int32, st.total));
    return 0;
}

static bool uiSlowTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    osSleep(timeMS(50));
    return true;
}

// Work still queued at shutdown is dropped, and every closure is destroyed exactly once.
static int test_cxui_shutdowndrop(void)
{
    enum { NPOST = 20 };
    UiRunRecord rec;
    uiRecordInit(&rec);
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    cxuiPost(closureCreate(uiSlowTask, stvNone));
    for (int32 i = 0; i < NPOST; i++)
        cxuiPost(uiRecordClosure(&rec));
    cxuiShutdown();
    eventDestroy(&rec.done);

    int32 runs     = atomicLoad(int32, &rec.runs, Acquire);
    int32 destroys = atomicLoad(int32, &rec.destroys, Acquire);
    if (destroys != NPOST || runs > NPOST)
        TEST_FAIL(1, _SL("${int} closures: ran ${int}, destroyed ${int}"), stvar(int32, NPOST), stvar(int32, runs),
                  stvar(int32, destroys));
    return 0;
}

// Scheduled work runs on time: the loop sleeps until the backend's timer, then ticks.
static int test_cxui_schedule(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    WakeTestState wts = { 0 };
    TaskQueue* q      = cxuiQueue();
    TQWakeResched* t  = tqwakereschedCreate(&wts, 5, timeMS(20));
    int64 start       = clockTimer();
    tqAdd(q, t);
    while (!atomicLoad(bool, &wts.done, Acquire) && clockTimer() - start < timeS(10))
        osSleep(timeMS(5));
    int64 elapsed = clockTimer() - start;
    int32 runs    = t->runs;
    tqRelease(&q);
    cxuiShutdown();
    objRelease(&t);

    // four 20 ms gaps between five runs
    if (runs != 5 || elapsed < timeMS(80))
        TEST_FAIL(1, _SL("${int} of 5 runs in ${int} ms"), stvar(int32, runs), stvar(int64, elapsed / 1000));
    return 0;
}

// ---- widgets and dispatch ----
//
// The bodies run on the UI thread of a null-backend session. Afterwards every null peer must be
// gone, and none may have been destroyed off the UI thread.

static bool uiTestTramp(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    int (*body)(void) = (int (*)(void))stvlAtPtr(cvars, 0);
    int* ret          = stvlAtPtr(cvars, 1);
    *ret              = body();
    return true;
}

static int uiRunOnUI(int (*body)(void))
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    int ret = 1;
    if (!cxuiCallSync(closureCreate(uiTestTramp, stvar(ptr, (void*)body), stvar(ptr, &ret))))
        ret = 1;
    cxuiShutdown();
    if (!ret && cxuiTestPeersAlive() != 0)
        TEST_FAIL(1, _SL("${int} null peers left after shutdown"), stvar(int32, cxuiTestPeersAlive()));
    if (!ret && cxuiTestPeersOffThread() != 0)
        TEST_FAIL(1, _SL("${int} null peers destroyed off the UI thread"), stvar(int32, cxuiTestPeersOffThread()));
    return ret;
}

#define UI_TEST(name)                     \
    static int name##_body(void);         \
    static int test_cxui_##name(void)     \
    {                                     \
        return uiRunOnUI(name##_body);    \
    }                                     \
    static int name##_body(void)

// What handlers saw, in the order they ran.
typedef struct EvLog {
    CxUiWidget* current[16];
    CxUiWidget* target[16];
    CxUiWindow* window[16];
    int32 n;
    int32 destroyed;   // handler closures destroyed
} EvLog;

static EvLog evlog;

static void evlogReset(void)
{
    memset(&evlog, 0, sizeof(evlog));
}

static void evlogRecord(CxUiEvent* ev)
{
    if (evlog.n < 16) {
        evlog.current[evlog.n] = ev->current;
        evlog.target[evlog.n]  = ev->target;
        evlog.window[evlog.n]  = ev->window;
    }
    evlog.n++;
}

static void evlogClosureDestroyed(stvlist* cvars)
{
    unused_noeval(cvars);
    evlog.destroyed++;
}

// Records the event and returns the captured result.
static CxUiResult logKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    evlogRecord(&ev->base);
    return (CxUiResult)stvlAt(cvars, 0, int32);
}

static CxUiResult logWheel(stvlist* cvars, CxUiWheelEvent* ev)
{
    evlogRecord(&ev->base);
    return (CxUiResult)stvlAt(cvars, 0, int32);
}

static CxUiResult logFocus(stvlist* cvars, CxUiFocusEvent* ev)
{
    evlogRecord(&ev->base);
    return (CxUiResult)stvlAt(cvars, 0, int32);
}

static closure keyHandler(CxUiResult result)
{
    closure fn = closureCreateAs(CxUiKeyFn, logKey, stvar(int32, result));
    closureSetDestroy(fn, evlogClosureDestroyed);
    return fn;
}

static CxUiResult sendKey(CxUiWidget* target)
{
    CxUiKeyEvent ev = { .base.kind = CXUI_EV_Key, .key = CXUI_KEY_F3, .down = true };
    return cxuiTestDispatch(target, &ev.base);
}

#define ALL_EVENTS 0xffffffff

// One handler per slot: set, called, replaced, cleared; each replaced handler destroyed once.
UI_TEST(slots)
{
    evlogReset();
    CxUiTestWidget* w = cxuiTestWidgetCreate(ALL_EVENTS);

    if (sendKey(CxUiWidget(w)) != CXUI_Default || evlog.n != 0)
        TEST_FAIL(1, _SL("no handler: result or ${int} calls"), stvar(int32, evlog.n));

    cxuiSetHandler(w, CXUI_EV_Key, keyHandler(CXUI_Handled));
    CxUiResult r = sendKey(CxUiWidget(w));
    if (r != CXUI_Handled || evlog.n != 1 || evlog.current[0] != CxUiWidget(w) || evlog.target[0] != CxUiWidget(w))
        TEST_FAIL(1, _SL("first handler: result ${int}, ${int} calls"), stvar(int32, r), stvar(int32, evlog.n));

    cxuiSetHandler(w, CXUI_EV_Key, keyHandler(CXUI_Default));
    if (evlog.destroyed != 1)
        TEST_FAIL(1, _SL("replacing: ${int} handlers destroyed (want 1)"), stvar(int32, evlog.destroyed));
    r = sendKey(CxUiWidget(w));
    if (r != CXUI_Default || evlog.n != 2)
        TEST_FAIL(1, _SL("second handler: result ${int}, ${int} calls"), stvar(int32, r), stvar(int32, evlog.n));

    // another slot doesn't disturb this one
    cxuiOnFocus(w, logFocus, stvar(int32, CXUI_Handled));
    CxUiFocusEvent fev = { .base.kind = CXUI_EV_Focus, .gained = true };
    if (cxuiTestDispatch(CxUiWidget(w), &fev.base) != CXUI_Handled || sendKey(CxUiWidget(w)) != CXUI_Default ||
        evlog.n != 4)
        TEST_FAIL(1, _SL("two slots: ${int} calls (want 4)"), stvar(int32, evlog.n));

    cxuiSetHandler(w, CXUI_EV_Key, NULL);
    if (evlog.destroyed != 2 || sendKey(CxUiWidget(w)) != CXUI_Default || evlog.n != 4)
        TEST_FAIL(1, _SL("clearing: ${int} destroyed, ${int} calls"), stvar(int32, evlog.destroyed), stvar(int32, evlog.n));

    cxuiSetHandler(w, CXUI_EV_Key, keyHandler(CXUI_Handled));
    objRelease(&w);   // the last reference: the handler goes with the widget
    if (evlog.destroyed != 3)
        TEST_FAIL(1, _SL("releasing: ${int} destroyed (want 3)"), stvar(int32, evlog.destroyed));
    return 0;
}

// Builds a chain of n widgets, each the child of the one before; returns the root, and the
// deepest in *leaf. The root holds the only references below it.
static CxUiTestWidget* buildChain(int32 n, CxUiTestWidget** nodes)
{
    for (int32 i = 0; i < n; i++) {
        nodes[i] = cxuiTestWidgetCreate(ALL_EVENTS);
        if (i > 0) {
            cxuiContainerAppend(nodes[i - 1], CxUiWidget(nodes[i]));
            objRelease(&nodes[i]);
            nodes[i] = (CxUiTestWidget*)cxuiContainerChild(nodes[i - 1], 0);
        }
    }
    return nodes[0];
}

// Key and wheel events bubble from the target up until handled; others stay on the target.
UI_TEST(bubble)
{
    evlogReset();
    CxUiTestWidget* n[5];
    CxUiTestWidget* root = buildChain(5, n);

    for (int32 i = 0; i < 5; i++)
        cxuiSetHandler(n[i], CXUI_EV_Key, keyHandler(i == 1 ? CXUI_Handled : CXUI_Default));
    cxuiSetHandler(n[3], CXUI_EV_Key, NULL);   // a gap: no handler there

    // leaf (4), then 2, then 1 handles it; 3 is skipped and 0 never sees it
    CxUiResult r = sendKey(CxUiWidget(n[4]));
    if (r != CXUI_Handled || evlog.n != 3 || evlog.current[0] != CxUiWidget(n[4]) ||
        evlog.current[1] != CxUiWidget(n[2]) || evlog.current[2] != CxUiWidget(n[1]) ||
        evlog.target[2] != CxUiWidget(n[4]))
        TEST_FAIL(1, _SL("key: result ${int}, ${int} calls"), stvar(int32, r), stvar(int32, evlog.n));

    // nobody handles it: every handler runs, and the result is Default
    cxuiSetHandler(n[1], CXUI_EV_Key, keyHandler(CXUI_Default));
    evlog.n = 0;
    r       = sendKey(CxUiWidget(n[4]));
    if (r != CXUI_Default || evlog.n != 4)
        TEST_FAIL(1, _SL("unhandled key: result ${int}, ${int} calls (want 4)"), stvar(int32, r), stvar(int32, evlog.n));

    // wheel bubbles too
    evlog.n = 0;
    cxuiOnWheel(n[0], logWheel, stvar(int32, CXUI_Handled));
    CxUiWheelEvent wev = { .base.kind = CXUI_EV_Wheel, .dy = 1 };
    if (cxuiTestDispatch(CxUiWidget(n[4]), &wev.base) != CXUI_Handled || evlog.n != 1 ||
        evlog.current[0] != CxUiWidget(n[0]))
        TEST_FAIL(1, _SL("wheel: ${int} calls"), stvar(int32, evlog.n));

    // focus doesn't
    evlog.n = 0;
    cxuiOnFocus(n[0], logFocus, stvar(int32, CXUI_Handled));
    CxUiFocusEvent fev = { .base.kind = CXUI_EV_Focus, .gained = true };
    if (cxuiTestDispatch(CxUiWidget(n[4]), &fev.base) != CXUI_Default || evlog.n != 0)
        TEST_FAIL(1, _SL("focus bubbled: ${int} calls"), stvar(int32, evlog.n));

    objRelease(&root);
    return 0;
}

// Replaces or clears its own slot, then reads its captures: the running closure must still be
// intact, and be destroyed once it returns.
static CxUiResult selfReplace(stvlist* cvars, CxUiKeyEvent* ev)
{
    int32 mode = stvlAt(cvars, 0, int32);
    if (mode == 0)
        cxuiSetHandler(ev->base.current, CXUI_EV_Key, keyHandler(CXUI_Default));
    else if (mode == 1)
        cxuiSetHandler(ev->base.current, CXUI_EV_Key, NULL);
    else
        cxuiDestroy(ev->base.current);
    evlogRecord(&ev->base);
    if (evlog.destroyed != 0)
        return CXUI_Default;   // destroyed while still running
    return stvlAt(cvars, 1, int32) == 1234 ? CXUI_Handled : CXUI_Default;
}

UI_TEST(selfreplace)
{
    for (int32 mode = 0; mode < 3; mode++) {
        evlogReset();
        CxUiTestWidget* w = cxuiTestWidgetCreate(ALL_EVENTS);
        closure fn        = closureCreateAs(CxUiKeyFn, selfReplace, stvar(int32, mode), stvar(int32, 1234));
        closureSetDestroy(fn, evlogClosureDestroyed);
        cxuiSetHandler(w, CXUI_EV_Key, fn);

        CxUiResult r = sendKey(CxUiWidget(w));
        if (r != CXUI_Handled || evlog.destroyed != 1)
            TEST_FAIL(1, _SL("mode ${int}: result ${int}, ${int} destroyed after the call (want 1)"), stvar(int32, mode),
                      stvar(int32, r), stvar(int32, evlog.destroyed));
        objRelease(&w);
    }
    return 0;
}

static bool notifyDestroyed(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    CxUiWidget* w = stvlAtObj(args, 0, CxUiWidget);
    if (!(w->state & CXUI_WS_Dead))
        evlog.destroyed = -1000;   // must already be dead
    evlogRecord(&(CxUiEvent){ .current = w, .target = w });
    return true;
}

// cxuiDestroy: children first, notifications once each, removed from the parent, handlers and peers
// gone, later calls harmless.
UI_TEST(destroy)
{
    evlogReset();
    CxUiTestWidget* n[3];
    CxUiTestWidget* root = buildChain(3, n);
    CxUiTestWidget* mid  = objAcquire(n[1]);
    CxUiTestWidget* leaf = objAcquire(n[2]);
    for (int32 i = 0; i < 3; i++) {
        cchainAttach(&CxUiWidget(n[i])->destroyNotify, notifyDestroyed, stvNone);
        cxuiSetHandler(n[i], CXUI_EV_Key, keyHandler(CXUI_Default));
        cxuiTestRealize(CxUiWidget(n[i]));
    }
    if (cxuiTestPeersAlive() != 3)
        TEST_FAIL(1, _SL("${int} peers after realizing 3"), stvar(int32, cxuiTestPeersAlive()));

    cxuiDestroy(mid);
    if (evlog.n != 2 || evlog.current[0] != CxUiWidget(leaf) || evlog.current[1] != CxUiWidget(mid) || evlog.destroyed != 2)
        TEST_FAIL(1, _SL("notifications ${int} (want leaf then mid), handlers destroyed ${int} (want 2)"),
                  stvar(int32, evlog.n), stvar(int32, evlog.destroyed));
    if (!(CxUiWidget(mid)->state & CXUI_WS_Dead) || !(CxUiWidget(leaf)->state & CXUI_WS_Dead) ||
        (CxUiWidget(root)->state & CXUI_WS_Dead))
        TEST_FAIL(1, _SL("dead flags wrong after destroying the middle widget"), stvNone);
    if (cxuiContainerChildCount(root) != 0 || CxUiWidget(mid)->parent || cxuiTestPeersAlive() != 1)
        TEST_FAIL(1, _SL("root has ${int} children, ${int} peers alive (want 0, 1)"),
                  stvar(int32, cxuiContainerChildCount(root)), stvar(int32, cxuiTestPeersAlive()));

    // dead widgets ignore everything; the closure handed in is destroyed at once
    cxuiDestroy(mid);
    cxuiSetHandler(mid, CXUI_EV_Key, keyHandler(CXUI_Handled));
    if (evlog.n != 2 || evlog.destroyed != 3 || sendKey(CxUiWidget(leaf)) != CXUI_Default || evlog.n != 2)
        TEST_FAIL(1, _SL("a dead widget reacted: ${int} notifications, ${int} destroyed"), stvar(int32, evlog.n),
                  stvar(int32, evlog.destroyed));
    if (cxuiContainerAppend(root, CxUiWidget(leaf)))
        TEST_FAIL(1, _SL("a dead widget was appended"), stvNone);

    objRelease(&mid);
    objRelease(&leaf);
    objRelease(&root);   // released without cxuiDestroy: its peer goes with it
    if (cxuiTestPeersAlive() != 0)
        TEST_FAIL(1, _SL("${int} peers after releasing everything"), stvar(int32, cxuiTestPeersAlive()));
    return 0;
}

// A handler destroys its own widget and an ancestor: the rest of the path is skipped, and the
// peers survive until dispatch returns.
static CxUiResult destroyInHandler(stvlist* cvars, CxUiKeyEvent* ev)
{
    CxUiWidget* ancestor = stvlAtObj(cvars, 0, CxUiWidget);
    cxuiDestroy(ev->base.current);
    cxuiDestroy(ancestor);
    evlogRecord(&ev->base);
    evlog.destroyed = cxuiTestPeersAlive();   // reused: peers alive inside the handler
    return CXUI_Default;
}

UI_TEST(destroyindispatch)
{
    evlogReset();
    CxUiTestWidget* n[3];
    CxUiTestWidget* root = buildChain(3, n);
    for (int32 i = 0; i < 3; i++) {
        cxuiTestRealize(CxUiWidget(n[i]));
        cxuiSetHandler(n[i], CXUI_EV_Key, closureCreateAs(CxUiKeyFn, logKey, stvar(int32, CXUI_Default)));
    }
    cxuiOnKey(n[2], destroyInHandler, stvar(object, n[1]));

    CxUiResult r = sendKey(CxUiWidget(n[2]));
    // only the leaf's handler ran: its parent was destroyed, and the root still got it
    if (r != CXUI_Default || evlog.n != 2 || evlog.current[1] != CxUiWidget(root))
        TEST_FAIL(1, _SL("result ${int}, ${int} handlers ran (want the leaf's and the root's)"), stvar(int32, r),
                  stvar(int32, evlog.n));
    if (evlog.destroyed != 3)
        TEST_FAIL(1, _SL("${int} peers alive inside the handler (want all 3)"), stvar(int32, evlog.destroyed));
    if (cxuiTestPeersAlive() != 1)
        TEST_FAIL(1, _SL("${int} peers alive after dispatch (want the root's)"), stvar(int32, cxuiTestPeersAlive()));

    objRelease(&root);
    return 0;
}

static bool uiMakeRealized(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTestWidget** out = stvlAtPtr(cvars, 0);
    *out                 = cxuiTestWidgetCreate(ALL_EVENTS);
    return cxuiTestRealize(CxUiWidget(*out));
}

// A realized widget whose last reference drops on another thread has its peer destroyed on the
// UI thread; one released after shutdown has nothing left to destroy.
static int test_cxui_offthread(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    CxUiTestWidget* w = NULL;
    if (!cxuiCallSync(closureCreate(uiMakeRealized, stvar(ptr, &w))))
        TEST_FAIL(1, _SL("couldn't realize a widget"), stvNone);
    objRelease(&w);   // here, on the test thread
    int64 start = clockTimer();
    while (cxuiTestPeersAlive() != 0 && clockTimer() - start < timeS(10))
        osSleep(timeMS(1));
    int32 alive = cxuiTestPeersAlive();

    CxUiTestWidget* late = NULL;
    cxuiCallSync(closureCreate(uiMakeRealized, stvar(ptr, &late)));
    cxuiShutdown();
    int32 aliveAtShutdown = cxuiTestPeersAlive();
    objRelease(&late);

    if (alive != 0)
        TEST_FAIL(1, _SL("the peer of a widget released off the UI thread was never destroyed"), stvNone);
    if (aliveAtShutdown != 0)
        TEST_FAIL(1, _SL("${int} peers left after shutdown"), stvar(int32, aliveAtShutdown));
    if (cxuiTestPeersOffThread() != 0)
        TEST_FAIL(1, _SL("${int} peers destroyed off the UI thread"), stvar(int32, cxuiTestPeersOffThread()));
    return 0;
}

static CxUiResult capturesNone(stvlist* cvars, CxUiKeyEvent* ev)
{
    evlogRecord(&ev->base);
    // stvNone is kept as a single capture of type none
    return (cvars->count == 1 && stvarIs(&cvars->vars[0], none)) ? CXUI_Handled : CXUI_Default;
}

static CxUiResult capturesOne(stvlist* cvars, CxUiKeyEvent* ev)
{
    evlogRecord(&ev->base);
    return (cvars->count == 1 && stvlAt(cvars, 0, int32) == 7) ? CXUI_Handled : CXUI_Default;
}

static CxUiResult capturesThree(stvlist* cvars, CxUiKeyEvent* ev)
{
    evlogRecord(&ev->base);
    return (cvars->count == 3 && stvlAt(cvars, 0, int32) == 1 && strEq(stvlAt(cvars, 1, strref), _SL("two")) &&
            stvlAt(cvars, 2, int64) == 3)
               ? CXUI_Handled
               : CXUI_Default;
}

// The cxuiOn* wrappers pass 0, 1 and 3 captures through two macro levels intact.
UI_TEST(wrappers)
{
    evlogReset();
    CxUiTestWidget* w = cxuiTestWidgetCreate(ALL_EVENTS);

    cxuiOnKey(w, capturesNone, stvNone);
    CxUiResult r0 = sendKey(CxUiWidget(w));
    cxuiOnKey(w, capturesOne, stvar(int32, 7));
    CxUiResult r1 = sendKey(CxUiWidget(w));
    cxuiOnKey(w, capturesThree, stvar(int32, 1), stvar(strref, _SL("two")), stvar(int64, 3));
    CxUiResult r3 = sendKey(CxUiWidget(w));
    objRelease(&w);

    if (r0 != CXUI_Handled || r1 != CXUI_Handled || r3 != CXUI_Handled || evlog.n != 3)
        TEST_FAIL(1, _SL("captures arrived wrong: 0->${int}, 1->${int}, 3->${int}"), stvar(int32, r0),
                  stvar(int32, r1), stvar(int32, r3));
    return 0;
}

static CxUiResult benchMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    int64* sum = stvlAtPtr(cvars, 0);
    *sum += ev->x;
    return CXUI_Handled;
}

static CxUiResult benchKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(cvars);
    return ev->base.current == ev->base.target ? CXUI_Default : CXUI_Default;
}

// Not a pass/fail test: times the dispatcher and logs at Info. Run it with -log=Info in a Release
// build: 1M mouse moves to one widget, 1M key events bubbling through 5 levels, and 1M handler
// replacements.
UI_TEST(benchdispatch)
{
    enum { N = 1000000 };
    int64 sum             = 0;
    CxUiTestWidget* n[5];
    CxUiTestWidget* root = buildChain(5, n);
    cxuiOnMouse(n[4], benchMouse, stvar(ptr, &sum));
    for (int32 i = 0; i < 5; i++)
        cxuiOnKey(n[i], benchKey, stvNone);

    CxUiMouseEvent mev = { .base.kind = CXUI_EV_Mouse, .action = CXUI_MOUSE_Move };
    int64 t0           = clockTimer();
    for (int32 i = 0; i < N; i++) {
        mev.x = i & 1023;
        cxuiTestDispatch(CxUiWidget(n[4]), &mev.base);
    }
    int64 t1 = clockTimer();
    for (int32 i = 0; i < N; i++)
        sendKey(CxUiWidget(n[4]));
    int64 t2 = clockTimer();
    for (int32 i = 0; i < N; i++)
        cxuiOnKey(n[4], benchKey, stvNone);
    int64 t3 = clockTimer();

    TEST_INFO(_SL("dispatch: mouse ${int} ns, 5-level key bubble ${int} ns, handler replace ${int} ns"),
              stvar(int64, (t1 - t0) * 1000 / N), stvar(int64, (t2 - t1) * 1000 / N), stvar(int64, (t3 - t2) * 1000 / N));
    objRelease(&root);
    return sum > 0 ? 0 : 1;
}

// ---- keys ----

// Every key formats to a name that parses back to the same key and modifiers.
static int test_cxui_keyroundtrip(void)
{
    static const uint32 modSets[] = {
        0, CXUI_MOD_Ctrl, CXUI_MOD_Shift, CXUI_MOD_Ctrl | CXUI_MOD_Shift, CXUI_MOD_Alt | CXUI_MOD_Meta,
        CXUI_MOD_Primary | CXUI_MOD_Shift,
        CXUI_MOD_Primary | CXUI_MOD_Ctrl | CXUI_MOD_Alt | CXUI_MOD_Shift | CXUI_MOD_Meta,
    };
    string name = 0;
    int ret     = 0;
    for (int32 k = CXUI_KEY_None + 1; k < CXUI_KEY_Count && !ret; k++) {
        for (size_t m = 0; m < sizeof(modSets) / sizeof(modSets[0]); m++) {
            CxUiKey key = CXUI_KEY_None;
            uint32 mods = 0;
            if (!cxuiKeyFormat(&name, (CxUiKey)k, modSets[m]) || !cxuiKeyParse(&key, &mods, name) ||
                key != (CxUiKey)k || mods != modSets[m]) {
                TEST_FAILV(ret, 1, _SL("key ${int} mods ${uint}: \"${string}\" parsed as key ${int} mods ${uint}"),
                           stvar(int32, k), stvar(uint32, modSets[m]), stvar(string, name), stvar(int32, key),
                           stvar(uint32, mods));
                break;
            }
        }
    }
    if (cxuiKeyFormat(&name, CXUI_KEY_None, CXUI_MOD_Ctrl) || cxuiKeyFormat(&name, CXUI_KEY_Count, 0) ||
        strLen(name) != 0)
        TEST_FAILV(ret, 1, _SL("an invalid key formatted as \"${string}\""), stvar(string, name));
    strDestroy(&name);
    return ret;
}

// Names come out in the documented form, and parsing is forgiving about spelling but not syntax.
static int test_cxui_keynames(void)
{
    static const struct {
        CxUiKey key;
        uint32 mods;
        const char* name;
    } canon[] = {
        { CXUI_KEY_F3, CXUI_MOD_Ctrl | CXUI_MOD_Shift, "Ctrl+Shift+F3" },
        { CXUI_KEY_Up, CXUI_MOD_Alt, "Alt+Up" },
        { CXUI_KEY_W, CXUI_MOD_Shift, "Shift+W" },
        { CXUI_KEY_Space, 0, "Space" },
        { CXUI_KEY_S, CXUI_MOD_Primary, "Primary+S" },
        { CXUI_KEY_A, CXUI_MOD_Meta | CXUI_MOD_Shift | CXUI_MOD_Alt | CXUI_MOD_Ctrl | CXUI_MOD_Primary,
          "Primary+Ctrl+Alt+Shift+Meta+A" },
        { CXUI_KEY_Pad5, 0, "Pad5" },
        { CXUI_KEY_Shift, CXUI_MOD_Ctrl, "Ctrl+Shift" },
    };
    static const struct {
        const char* name;
        CxUiKey key;
        uint32 mods;
    } lenient[] = {
        { "ctrl+shift+f3", CXUI_KEY_F3, CXUI_MOD_Ctrl | CXUI_MOD_Shift },
        { " Ctrl + Shift + F3 ", CXUI_KEY_F3, CXUI_MOD_Ctrl | CXUI_MOD_Shift },
        { "Control+Esc", CXUI_KEY_Escape, CXUI_MOD_Ctrl },
        { "Cmd+Return", CXUI_KEY_Enter, CXUI_MOD_Meta },
        { "Option+PgDn", CXUI_KEY_PageDown, CXUI_MOD_Alt },
        { "primary+s", CXUI_KEY_S, CXUI_MOD_Primary },
        { "Shift", CXUI_KEY_Shift, 0 },
    };
    static const char* const bad[] = { "", "+", "Ctrl+", "+A", "Foo", "Ctrl+Foo", "A+B", "Ctrl++A", "F25" };

    string name = 0;
    int ret     = 0;
    for (size_t i = 0; i < sizeof(canon) / sizeof(canon[0]); i++) {
        cxuiKeyFormat(&name, canon[i].key, canon[i].mods);
        if (!strEq(name, (strref)canon[i].name))
            TEST_FAILV(ret, 1, _SL("formatted \"${string}\", want \"${string}\""), stvar(string, name),
                       stvar(strref, (strref)canon[i].name));
    }
    for (size_t i = 0; i < sizeof(lenient) / sizeof(lenient[0]); i++) {
        CxUiKey key;
        uint32 mods;
        if (!cxuiKeyParse(&key, &mods, (strref)lenient[i].name) || key != lenient[i].key || mods != lenient[i].mods)
            TEST_FAILV(ret, 1, _SL("\"${string}\" parsed as key ${int} mods ${uint}"),
                       stvar(strref, (strref)lenient[i].name), stvar(int32, key), stvar(uint32, mods));
    }
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CxUiKey key = CXUI_KEY_A;
        uint32 mods = 1;
        if (cxuiKeyParse(&key, &mods, (strref)bad[i]) || key != CXUI_KEY_None || mods != 0)
            TEST_FAILV(ret, 1, _SL("\"${string}\" was accepted, or left key ${int} mods ${uint}"),
                       stvar(strref, (strref)bad[i]), stvar(int32, key), stvar(uint32, mods));
    }
    strDestroy(&name);
    return ret;
}

// Shortcuts match exactly, with Primary standing for the platform's shortcut modifier.
static int test_cxui_keymatch(void)
{
#if defined(_PLATFORM_MACOS)
    uint32 primary = CXUI_MOD_Meta, other = CXUI_MOD_Ctrl;
#else
    uint32 primary = CXUI_MOD_Ctrl, other = CXUI_MOD_Meta;
#endif
    if (!cxuiKeyMatches(CXUI_KEY_S, CXUI_MOD_Primary, CXUI_KEY_S, primary) ||
        cxuiKeyMatches(CXUI_KEY_S, CXUI_MOD_Primary, CXUI_KEY_S, other) ||
        cxuiKeyMatches(CXUI_KEY_S, CXUI_MOD_Primary, CXUI_KEY_S, primary | CXUI_MOD_Shift) ||
        cxuiKeyMatches(CXUI_KEY_S, CXUI_MOD_Primary, CXUI_KEY_D, primary) ||
        !cxuiKeyMatches(CXUI_KEY_F3, CXUI_MOD_Ctrl | CXUI_MOD_Shift, CXUI_KEY_F3, CXUI_MOD_Shift | CXUI_MOD_Ctrl) ||
        cxuiKeyMatches(CXUI_KEY_None, 0, CXUI_KEY_None, 0))
        TEST_FAIL(1, _SL("shortcut matching is wrong"), stvNone);
    return 0;
}

// Labels show Primary as the platform's modifier, in the platform's order and spelling.
static int test_cxui_keylabel(void)
{
    static const struct {
        CxUiKey key;
        uint32 mods;
        const char* label;
    } want[] = {
#if defined(_PLATFORM_MACOS)
        { CXUI_KEY_F, CXUI_MOD_Primary, "\xE2\x8C\x98" "F" },
        { CXUI_KEY_Z, CXUI_MOD_Primary | CXUI_MOD_Shift, "\xE2\x87\xA7\xE2\x8C\x98" "Z" },
        { CXUI_KEY_Enter, CXUI_MOD_Meta | CXUI_MOD_Alt | CXUI_MOD_Ctrl, "\xE2\x8C\x83\xE2\x8C\xA5\xE2\x8C\x98\xE2\x86\xA9" },
        { CXUI_KEY_Backspace, 0, "\xE2\x8C\xAB" },
#else
        { CXUI_KEY_F, CXUI_MOD_Primary, "Ctrl+F" },
        { CXUI_KEY_Z, CXUI_MOD_Primary | CXUI_MOD_Shift, "Ctrl+Shift+Z" },
        { CXUI_KEY_PageDown, CXUI_MOD_Alt | CXUI_MOD_Ctrl, "Ctrl+Alt+Page Down" },
        { CXUI_KEY_Escape, 0, "Esc" },
#if defined(_PLATFORM_WIN)
        { CXUI_KEY_E, CXUI_MOD_Meta, "Win+E" },
#else
        { CXUI_KEY_E, CXUI_MOD_Meta, "Super+E" },
#endif
#endif
        { CXUI_KEY_F3, 0, "F3" },
        { CXUI_KEY_Slash, 0, "/" },
        { CXUI_KEY_Pad5, 0, "Num 5" },
    };
    string label = 0;
    int ret      = 0;
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        if (!cxuiKeyLabel(&label, want[i].key, want[i].mods) || !strEq(label, (strref)want[i].label))
            TEST_FAILV(ret, 1, _SL("key ${int} mods ${uint}: \"${string}\", want \"${string}\""), stvar(int32, want[i].key),
                       stvar(uint32, want[i].mods), stvar(string, label), stvar(strref, (strref)want[i].label));
    }
    if (cxuiKeyLabel(&label, CXUI_KEY_None, CXUI_MOD_Ctrl) || !strEmpty(label))
        TEST_FAILV(ret, 1, _SL("a missing key was labeled \"${string}\""), stvar(string, label));
    strDestroy(&label);
    return ret;
}

// ---- timers ----

typedef struct UiTimerState {
    atomic(int32) calls;
    int64 times[16];
    int32 stopAfter;   // the handler stops its own timer after this many calls (0: never)
    int64 work;        // how long each call takes
    Event done;
} UiTimerState;

static CxUiResult uiTimerTick(stvlist* cvars, CxUiTimerEvent* ev)
{
    UiTimerState* st = stvlNextPtr(cvars);
    int32 n          = atomicLoad(int32, &st->calls, Acquire);
    if (n < 16)
        st->times[n] = clockTimer();
    if (st->work)
        osSleep(st->work);
    atomicStore(int32, &st->calls, n + 1, Release);
    if (st->stopAfter && n + 1 == st->stopAfter) {
        cxuiTimerStop(ev->timer);
        eventSignal(&st->done);
    }
    return CXUI_Default;
}

static bool uiTimerOp(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTimer* t = stvlAtPtr(cvars, 0);
    switch (stvlAt(cvars, 1, int32)) {
    case 0:
        return cxuiTimerStart(t);
    case 1:
        cxuiTimerStop(t);
        return true;
    case 2:
        return cxuiTimerIsRunning(t);
    default:
        objRelease(&t);
        return true;
    }
}

enum { TIMER_Start, TIMER_Stop, TIMER_IsRunning, TIMER_Release };

static bool uiTimer(CxUiTimer* t, int32 op)
{
    return cxuiCallSync(closureCreate(uiTimerOp, stvar(ptr, t), stvar(int32, op)));
}

// Lock keys read back as the platform reports them.
UI_TEST(keylocks)
{
    uint32 none = cxuiKeyLocks();
    cxuiTestSetKeyLocks(CXUI_LOCK_Num | CXUI_LOCK_Scroll);
    uint32 set = cxuiKeyLocks();
    if (none != 0 || set != (CXUI_LOCK_Num | CXUI_LOCK_Scroll))
        TEST_FAIL(1, _SL("locks at start ${uint}, after setting Num and Scroll ${uint}"), stvar(uint32, none),
                  stvar(uint32, set));
    return 0;
}

// A repeating timer runs until its handler stops it, a period apart, timed from the end of each
// call so a slow handler never has calls queue up.
static int test_cxui_timerrepeat(void)
{
    for (int32 slow = 0; slow < 2; slow++) {
        UiTimerState st = { .stopAfter = 5, .work = slow ? timeMS(30) : 0 };
        eventInit(&st.done);
        if (!cxuiStart(&nullStart))
            TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

        int64 period  = timeMS(slow ? 10 : 20);
        CxUiTimer* t  = cxuiTimerCreate(period, 0, uiTimerTick, stvar(ptr, &st));
        int64 started = clockTimer();
        bool ok       = uiTimer(t, TIMER_Start) && eventWaitTimeout(&st.done, timeS(10));
        osSleep(timeMS(80));   // no call after the handler stopped it
        bool running = uiTimer(t, TIMER_IsRunning);
        uiTimer(t, TIMER_Release);
        cxuiShutdown();
        eventDestroy(&st.done);

        int32 calls = atomicLoad(int32, &st.calls, Acquire);
        if (!ok || calls != 5 || running)
            TEST_FAIL(1, _SL("slow=${int}: ${int} calls (want 5), still running ${int}"), stvar(int32, slow),
                      stvar(int32, calls), stvar(int32, running));
        // each call at least a period, plus the previous call's work, after the one before
        int64 minGap = period + st.work - timeMS(2);
        for (int32 i = 0; i < 5; i++) {
            int64 gap = st.times[i] - (i ? st.times[i - 1] : started);
            if (gap < (i ? minGap : period - timeMS(2)))
                TEST_FAIL(1, _SL("slow=${int}: call ${int} came ${int} us after the last (want at least ${int})"),
                          stvar(int32, slow), stvar(int32, i), stvar(int64, gap), stvar(int64, i ? minGap : period));
        }
    }
    return 0;
}

// A one-shot fires once; starting a running timer, or stopping it before it fires, leaves no
// extra calls.
static int test_cxui_timerstartstop(void)
{
    UiTimerState st = { 0 };
    eventInit(&st.done);
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    int ret          = 0;
    CxUiTimer* once  = cxuiTimerCreate(timeMS(10), CXUI_Timer_OneShot, uiTimerTick, stvar(ptr, &st));
    uiTimer(once, TIMER_Start);
    osSleep(timeMS(150));
    if (atomicLoad(int32, &st.calls, Acquire) != 1 || uiTimer(once, TIMER_IsRunning))
        TEST_FAILV(ret, 1, _SL("one-shot: ${int} calls (want 1), or still running"),
                   stvar(int32, atomicLoad(int32, &st.calls, Acquire)));
    // and it can be started again
    uiTimer(once, TIMER_Start);
    osSleep(timeMS(150));
    if (atomicLoad(int32, &st.calls, Acquire) != 2)
        TEST_FAILV(ret, 1, _SL("restarted one-shot: ${int} calls (want 2)"), stvar(int32, atomicLoad(int32, &st.calls, Acquire)));
    uiTimer(once, TIMER_Release);

    // stopped before it fires
    atomicStore(int32, &st.calls, 0, Release);
    CxUiTimer* t = cxuiTimerCreate(timeMS(20), 0, uiTimerTick, stvar(ptr, &st));
    uiTimer(t, TIMER_Start);
    uiTimer(t, TIMER_Stop);
    osSleep(timeMS(100));
    if (atomicLoad(int32, &st.calls, Acquire) != 0)
        TEST_FAILV(ret, 1, _SL("stopped timer: ${int} calls"), stvar(int32, atomicLoad(int32, &st.calls, Acquire)));

    // started twice: one timer's worth of calls, not two
    uiTimer(t, TIMER_Start);
    uiTimer(t, TIMER_Start);
    osSleep(timeMS(210));
    uiTimer(t, TIMER_Stop);
    int32 calls = atomicLoad(int32, &st.calls, Acquire);
    if (calls < 5 || calls > 11)
        TEST_FAILV(ret, 1, _SL("started twice: ${int} calls in 210 ms at 20 ms (want about 10)"), stvar(int32, calls));

    // released while running: no more calls
    uiTimer(t, TIMER_Start);
    uiTimer(t, TIMER_Release);
    calls = atomicLoad(int32, &st.calls, Acquire);
    osSleep(timeMS(100));
    if (atomicLoad(int32, &st.calls, Acquire) != calls)
        TEST_FAILV(ret, 1, _SL("released timer kept calling: ${int} more"),
                   stvar(int32, atomicLoad(int32, &st.calls, Acquire) - calls));

    cxuiShutdown();
    eventDestroy(&st.done);
    return ret;
}

// Shutdown stops every timer; after a restart one is stopped until started again.
static int test_cxui_timershutdown(void)
{
    UiTimerState st = { .stopAfter = 1 };
    eventInit(&st.done);
    CxUiTimer* t = cxuiTimerCreate(timeMS(10), 0, uiTimerTick, stvar(ptr, &st));

    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    uiTimer(t, TIMER_Start);
    cxuiShutdown();
    int32 callsAtShutdown = atomicLoad(int32, &st.calls, Acquire);

    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    bool runningAfter = uiTimer(t, TIMER_IsRunning);
    osSleep(timeMS(50));
    int32 callsBeforeStart = atomicLoad(int32, &st.calls, Acquire);
    bool fired             = uiTimer(t, TIMER_Start) && eventWaitTimeout(&st.done, timeS(10));
    uiTimer(t, TIMER_Release);
    cxuiShutdown();
    eventDestroy(&st.done);

    if (runningAfter || callsBeforeStart != callsAtShutdown)
        TEST_FAIL(1, _SL("after a restart the timer was running (${int}) or called (${int} calls)"),
                  stvar(int32, runningAfter), stvar(int32, callsBeforeStart - callsAtShutdown));
    if (!fired)
        TEST_FAIL(1, _SL("the timer didn't fire once started in the new session"), stvNone);
    return 0;
}

// ---- windows ----

static bool sizeIs(CxUiSize s, int32 w, int32 h)
{
    return s.w == w && s.h == h;
}

static bool pointIs(CxUiPoint p, int32 x, int32 y)
{
    return p.x == x && p.y == y;
}

// Settings made before realization reach the peer when it is created, later ones at once; dead
// windows ignore everything.
UI_TEST(windowmodel)
{
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"First", .size = { 300, 200 }, .name = _S"main" });
    CxUiSize sz;
    CxUiPoint pos;
    CxUiTestWindowInfo info;
    if (!strEq(cxuiWindowTitle(w), _SL("First")) || !strEq(CxUiWidget(w)->name, _SL("main")) ||
        !cxuiWindowSize(w, &sz) || !sizeIs(sz, 300, 200))
        TEST_FAIL(1, _SL("the descriptor wasn't applied: title '${string}', size ${int}x${int}"),
                  stvar(strref, cxuiWindowTitle(w)), stvar(int32, sz.w), stvar(int32, sz.h));
    if (cxuiWindowIsVisible(w) || CxUiWidget(w)->peer || cxuiWindowPosition(w, &pos))
        TEST_FAIL(1, _SL("a new window was visible, realized or had a position"), stvNone);

    cxuiWindowSetTitle(w, _SL("Second"));
    cxuiWindowSetSize(w, 320, 240);
    if (!cxuiWindowSetPosition(w, 40, 50) || !cxuiWindowPosition(w, &pos) || !pointIs(pos, 40, 50))
        TEST_FAIL(1, _SL("position before realization: ${int},${int}"), stvar(int32, pos.x), stvar(int32, pos.y));

    if (!cxuiWindowRealize(w) || cxuiWindowIsVisible(w) || !cxuiTestWindowInfo(w, &info) || info.visible)
        TEST_FAIL(1, _SL("realizing failed or showed the window"), stvNone);
    if (!strEq(info.title, _SL("Second")) || !sizeIs(info.size, 320, 240) || !pointIs(info.pos, 40, 50))
        TEST_FAIL(1, _SL("the peer was created with '${string}' ${int}x${int} at ${int},${int}"), stvar(strref, info.title),
                  stvar(int32, info.size.w), stvar(int32, info.size.h), stvar(int32, info.pos.x), stvar(int32, info.pos.y));

    int32 peers = cxuiTestPeersAlive();
    cxuiWindowSetTitle(w, _SL("Third"));
    cxuiWindowSetSize(w, 100, 90);
    cxuiWindowSetPosition(w, 5, 6);
    cxuiWindowRealize(w);
    cxuiTestWindowInfo(w, &info);
    if (!strEq(info.title, _SL("Third")) || !sizeIs(info.size, 100, 90) || !pointIs(info.pos, 5, 6) ||
        cxuiTestPeersAlive() != peers)
        TEST_FAIL(1, _SL("changes after realization: '${string}' ${int}x${int} at ${int},${int}, ${int} peers (want ${int})"),
                  stvar(strref, info.title), stvar(int32, info.size.w), stvar(int32, info.size.h),
                  stvar(int32, info.pos.x), stvar(int32, info.pos.y), stvar(int32, cxuiTestPeersAlive()),
                  stvar(int32, peers));

    for (uint32 mode = CXUI_SHOW_Normal; mode <= CXUI_SHOW_Maximized; mode++) {
        cxuiWindowShow(w, mode);
        cxuiTestWindowInfo(w, &info);
        if (!cxuiWindowIsVisible(w) || !info.visible || info.mode != mode)
            TEST_FAIL(1, _SL("show mode ${uint} reached the peer as ${uint}"), stvar(uint32, mode), stvar(uint32, info.mode));
    }
    cxuiWindowHide(w);
    cxuiTestWindowInfo(w, &info);
    if (cxuiWindowIsVisible(w) || info.visible)
        TEST_FAIL(1, _SL("hiding didn't"), stvNone);

    cxuiDestroy(w);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiWindowSetTitle(w, _SL("Dead"));
    if (cxuiWindowRealize(w) || cxuiWindowIsVisible(w) || !strEmpty(cxuiWindowTitle(w)) || cxuiWindowSize(w, &sz) ||
        cxuiWindowPosition(w, &pos) || cxuiWindowSetPosition(w, 1, 1) || CxUiWidget(w)->peer)
        TEST_FAIL(1, _SL("a dead window reacted"), stvNone);
    objRelease(&w);

    // with no size set, the platform's default is known once realized
    w = cxuiWindowCreate(NULL);
    bool sizedEarly = cxuiWindowSize(w, &sz);
    cxuiWindowRealize(w);
    if (sizedEarly || !cxuiWindowSize(w, &sz) || sz.w <= 0 || sz.h <= 0)
        TEST_FAIL(1, _SL("default size: known before realizing ${int}, after ${int}x${int}"), stvar(int32, sizedEarly),
                  stvar(int32, sz.w), stvar(int32, sz.h));
    objRelease(&w);
    return 0;
}

static bool frameDark(CxUiWindow* w)
{
    bool dark = false;
    return cxuiTestFrameDark(w, &dark) && dark;
}

// The theme is resolved when the window is realized and applied to the peer then, and again when
// the mode changes.
UI_TEST(windowtheme)
{
    cxuiTestSetSystemTheme(CXUI_Theme_Dark);
    CxUiWindow* sys   = cxuiWindowCreate(NULL);
    CxUiWindow* light = cxuiWindowCreate(&(CxUiWindowDesc){ .theme = CXUI_Theme_Light });
    if (cxuiWindowThemeMode(sys) != CXUI_Theme_System || cxuiWindowResolvedTheme(sys) != CXUI_Theme_Dark ||
        cxuiWindowResolvedTheme(light) != CXUI_Theme_Light)
        TEST_FAIL(1, _SL("before realizing: system window resolved ${uint}, light window ${uint}"),
                  stvar(uint32, cxuiWindowResolvedTheme(sys)), stvar(uint32, cxuiWindowResolvedTheme(light)));

    cxuiWindowRealize(sys);
    cxuiWindowRealize(light);
    if (!frameDark(sys) || frameDark(light))
        TEST_FAIL(1, _SL("realized frames: system window dark ${int}, light window dark ${int}"),
                  stvar(int32, frameDark(sys)), stvar(int32, frameDark(light)));

    cxuiWindowSetThemeMode(sys, CXUI_Theme_Light);
    cxuiWindowSetThemeMode(light, CXUI_Theme_Dark);
    cxuiWindowSetThemeMode(light, 7);
    if (frameDark(sys) || !frameDark(light) || cxuiWindowResolvedTheme(light) != CXUI_Theme_Dark ||
        cxuiWindowThemeMode(light) != CXUI_Theme_Dark)
        TEST_FAIL(1, _SL("after switching: first window dark ${int}, second dark ${int}"), stvar(int32, frameDark(sys)),
                  stvar(int32, frameDark(light)));

    objRelease(&sys);
    objRelease(&light);
    return 0;
}

typedef struct CloseLog {
    int32 closes;
    uint32 reason;
    CxUiWindow* window;
    CxUiResult answer;
    bool destroy;              // the handler destroys its window
    int32 shownAfterDestroy;   // 1 if its peer was still visible right after, -1 if it had none
} CloseLog;

static CxUiResult logClose(stvlist* cvars, CxUiCloseEvent* ev)
{
    CloseLog* log = stvlAtPtr(cvars, 0);
    log->closes++;
    log->reason = ev->reason;
    log->window = ev->base.window;
    if (log->destroy) {
        cxuiDestroy(ev->base.current);
        CxUiTestWindowInfo info;
        log->shownAfterDestroy = cxuiTestWindowInfo(ev->base.window, &info) ? info.visible : -1;
    }
    return log->answer;
}

// The close handler can veto; otherwise the close action runs.
UI_TEST(windowclose)
{
    evlogReset();
    CloseLog log  = { 0 };
    CxUiWindow* w = cxuiWindowCreate(NULL);
    cchainAttach(&CxUiWidget(w)->destroyNotify, notifyDestroyed, stvNone);
    cxuiOnClose(w, logClose, stvar(ptr, &log));
    cxuiWindowShow(w, CXUI_SHOW_Normal);

    log.answer = CXUI_Handled;
    cxuiTestClose(w);
    if (log.closes != 1 || log.reason != CXUI_CLOSE_User || log.window != w || !cxuiWindowIsVisible(w))
        TEST_FAIL(1, _SL("vetoed close: ${int} calls, reason ${uint}, visible ${int}"), stvar(int32, log.closes),
                  stvar(uint32, log.reason), stvar(int32, cxuiWindowIsVisible(w)));

    log.answer = CXUI_Default;
    cxuiWindowClose(w);
    if (log.closes != 2 || log.reason != CXUI_CLOSE_App || cxuiWindowIsVisible(w) || (CxUiWidget(w)->state & CXUI_WS_Dead))
        TEST_FAIL(1, _SL("closing to hide: ${int} calls, reason ${uint}, visible ${int}"), stvar(int32, log.closes),
                  stvar(uint32, log.reason), stvar(int32, cxuiWindowIsVisible(w)));

    cxuiWindowShow(w, CXUI_SHOW_Normal);
    w->closeAction = CXUI_OnClose_None;
    cxuiTestClose(w);
    if (log.closes != 3 || !cxuiWindowIsVisible(w))
        TEST_FAIL(1, _SL("CXUI_OnClose_None closed the window"), stvNone);

    w->closeAction = CXUI_OnClose_Destroy;
    cxuiTestClose(w);
    if (log.closes != 4 || !(CxUiWidget(w)->state & CXUI_WS_Dead) || evlog.n != 1 || cxuiTestPeersAlive() != 0)
        TEST_FAIL(1, _SL("closing to destroy: ${int} calls, ${int} notifications, ${int} peers"), stvar(int32, log.closes),
                  stvar(int32, evlog.n), stvar(int32, cxuiTestPeersAlive()));
    cxuiWindowClose(w);
    cxuiTestClose(w);
    if (log.closes != 4)
        TEST_FAIL(1, _SL("a dead window's close handler ran"), stvNone);
    objRelease(&w);

    // a handler that destroys its own window: its peer lasts until the handler returns, hidden
    log = (CloseLog){ .answer = CXUI_Default, .destroy = true };
    w   = cxuiWindowCreate(NULL);
    cxuiOnClose(w, logClose, stvar(ptr, &log));
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestClose(w);
    if (log.closes != 1 || log.shownAfterDestroy != 0 || !(CxUiWidget(w)->state & CXUI_WS_Dead) ||
        cxuiTestPeersAlive() != 0)
        TEST_FAIL(1, _SL("destroyed by its close handler: ${int} calls, peer shown inside ${int}, ${int} peers after"),
                  stvar(int32, log.closes), stvar(int32, log.shownAfterDestroy), stvar(int32, cxuiTestPeersAlive()));
    objRelease(&w);
    return 0;
}

// The content is the window's one child; events from it bubble to the window, and name it.
UI_TEST(windowcontent)
{
    evlogReset();
    CxUiWindow* w     = cxuiWindowCreate(NULL);
    CxUiWindow* other = cxuiWindowCreate(NULL);
    CxUiTestWidget* a = cxuiTestWidgetCreate(ALL_EVENTS);
    CxUiTestWidget* b = cxuiTestWidgetCreate(ALL_EVENTS);

    if (!cxuiContainerAppend(w, a) || cxuiContainerAppend(w, b) || cxuiContainerInsert(w, b, 0) ||
        cxuiContainerChildCount(w) != 1 || cxuiContainerChild(w, 0) != CxUiWidget(a) ||
        CxUiWidget(a)->parent != CxUiWidget(w))
        TEST_FAIL(1, _SL("a window took ${int} children"), stvar(int32, cxuiContainerChildCount(w)));
    if (cxuiContainerAppend(other, w))
        TEST_FAIL(1, _SL("a window became another window's content"), stvNone);

    cxuiSetHandler(w, CXUI_EV_Key, keyHandler(CXUI_Handled));
    if (sendKey(CxUiWidget(a)) != CXUI_Handled || evlog.n != 1 || evlog.current[0] != CxUiWidget(w) ||
        evlog.window[0] != w)
        TEST_FAIL(1, _SL("a key in the content reached ${int} handlers, or not the window's"), stvar(int32, evlog.n));

    if (!cxuiContainerDetach(w, a) || CxUiWidget(a)->parent || cxuiContainerChildCount(w) != 0 ||
        !cxuiContainerAppend(w, b))
        TEST_FAIL(1, _SL("replacing the content failed"), stvNone);

    cxuiDestroy(w);
    if (!(CxUiWidget(b)->state & CXUI_WS_Dead) || CxUiWidget(b)->parent || (CxUiWidget(a)->state & CXUI_WS_Dead))
        TEST_FAIL(1, _SL("destroying the window didn't destroy just its content"), stvNone);

    objRelease(&a);
    objRelease(&b);
    objRelease(&w);
    objRelease(&other);
    return 0;
}

static bool uiShowReleased(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    Weak(CxUiWindow)** weak = stvlAtPtr(cvars, 0);
    bool destroy            = stvlAt(cvars, 1, bool);
    CxUiWindow* w           = cxuiWindowCreate(NULL);
    *weak                   = objGetWeak(CxUiWindow, w);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    if (destroy)
        cxuiDestroy(w);
    objRelease(&w);
    return true;
}

static bool uiShowWindow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow* w = stvlAtObj(cvars, 0, CxUiWindow);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    return cxuiWindowIsVisible(w) && CxUiWidget(w)->peer;
}

static bool weakAlive(Weak(CxUiWindow)* weak)
{
    CxUiWindow* w = objAcquireFromWeak(CxUiWindow, weak);
    bool alive    = w != NULL;
    objRelease(&w);
    return alive;
}

// cxui keeps a shown window alive until it is destroyed or the session ends; a window the program
// still holds can be shown again in a later session.
static int test_cxui_windowlifetime(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    Weak(CxUiWindow)* shown     = NULL;
    Weak(CxUiWindow)* destroyed = NULL;
    CxUiWindow* kept            = cxuiWindowCreate(NULL);
    cxuiCallSync(closureCreate(uiShowReleased, stvar(ptr, &shown), stvar(bool, false)));
    cxuiCallSync(closureCreate(uiShowReleased, stvar(ptr, &destroyed), stvar(bool, true)));
    bool firstShow     = cxuiCallSync(closureCreate(uiShowWindow, stvar(object, kept)));
    bool shownAlive    = weakAlive(shown);
    bool destroyedGone = !weakAlive(destroyed);
    cxuiShutdown();

    bool shownGone = !weakAlive(shown);
    bool keptReset = !kept->_visible && !CxUiWidget(kept)->peer;

    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed the second time"), stvNone);
    bool secondShow = cxuiCallSync(closureCreate(uiShowWindow, stvar(object, kept)));
    cxuiShutdown();
    objRelease(&kept);
    objDestroyWeak(&shown);
    objDestroyWeak(&destroyed);

    if (!firstShow || !secondShow)
        TEST_FAIL(1, _SL("showing a window failed in session 1 (${int}) or 2 (${int})"), stvar(int32, firstShow),
                  stvar(int32, secondShow));
    if (!shownAlive || !shownGone)
        TEST_FAIL(1, _SL("a released window: alive while shown ${int}, freed at shutdown ${int}"),
                  stvar(int32, shownAlive), stvar(int32, shownGone));
    if (!destroyedGone)
        TEST_FAIL(1, _SL("a destroyed window the program released wasn't freed"), stvNone);
    if (!keptReset)
        TEST_FAIL(1, _SL("a window kept past shutdown was still visible or realized"), stvNone);
    if (cxuiTestPeersAlive() != 0 || cxuiTestPeersOffThread() != 0)
        TEST_FAIL(1, _SL("${int} peers alive, ${int} destroyed off the UI thread"), stvar(int32, cxuiTestPeersAlive()),
                  stvar(int32, cxuiTestPeersOffThread()));
    return 0;
}

static bool uiWindowState(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow* w        = stvlAtObj(cvars, 0, CxUiWindow);
    uint32* out          = stvlAtPtr(cvars, 1);
    CxUiTestWindowInfo info;
    // 0 dead, 1 hidden, 2 + mode visible
    if (CxUiWidget(w)->state & CXUI_WS_Dead)
        *out = 0;
    else if (!cxuiWindowIsVisible(w) || !cxuiTestWindowInfo(w, &info) || !info.visible)
        *out = 1;
    else
        *out = 2 + info.mode;
    return true;
}

static uint32 windowState(CxUiWindow* w)
{
    uint32 state = 99;
    cxuiCallSync(closureCreate(uiWindowState, stvar(object, w), stvar(ptr, &state)));
    return state;
}

// Show, hide and close from another thread run on the UI thread, in order.
static int test_cxui_windowoffthread(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);

    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .closeAction = CXUI_OnClose_Destroy });
    cxuiWindowShow(w, CXUI_SHOW_NoActivate);
    uint32 shown = windowState(w);
    cxuiWindowHide(w);
    uint32 hidden = windowState(w);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiWindowClose(w);
    uint32 closed = windowState(w);
    cxuiShutdown();
    objRelease(&w);

    if (shown != 2 + CXUI_SHOW_NoActivate || hidden != 1 || closed != 0)
        TEST_FAIL(1, _SL("states seen: ${uint} after show, ${uint} after hide, ${uint} after close"), stvar(uint32, shown),
                  stvar(uint32, hidden), stvar(uint32, closed));
    if (cxuiTestPeersAlive() != 0 || cxuiTestPeersOffThread() != 0)
        TEST_FAIL(1, _SL("${int} peers alive, ${int} destroyed off the UI thread"), stvar(int32, cxuiTestPeersAlive()),
                  stvar(int32, cxuiTestPeersOffThread()));
    return 0;
}

// ---- popups and message boxes ----

static CxUiResult timerEndPopup(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    cxuiWindowEndPopup(stvlAtObj(cvars, 0, CxUiWindow), stvlAt(cvars, 1, int32));
    return CXUI_Default;
}

static CxUiResult timerClose(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    cxuiTestClose(stvlAtObj(cvars, 0, CxUiWindow));
    return CXUI_Default;
}

static CxUiResult timerDestroy(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    cxuiDestroy(stvlAtObj(cvars, 0, CxUiWindow));
    return CXUI_Default;
}

static CxUiTimer* uiAfter(CxUiTimer* t)
{
    cxuiTimerStart(t);
    return t;
}

#define UI_AFTER(ms, fn, ...) uiAfter(cxuiTimerCreate(timeMS(ms), CXUI_Timer_OneShot, fn, __VA_ARGS__))

static CxUiTestWindowInfo winInfo(CxUiWindow* w)
{
    CxUiTestWindowInfo info;
    cxuiTestWindowInfo(w, &info);
    return info;
}

typedef struct PopupSeen {
    bool ownerEnabled;
    bool popupVisible;
    bool popupModal;
    bool ownerIsParent;
} PopupSeen;

static CxUiResult inspectPopup(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* popup = stvlAtObj(cvars, 0, CxUiWindow);
    CxUiWindow* owner = stvlAtObj(cvars, 1, CxUiWindow);
    PopupSeen* seen   = stvlAtPtr(cvars, 2);
    seen->ownerEnabled  = winInfo(owner).enabled;
    seen->popupVisible  = winInfo(popup).visible;
    seen->popupModal    = winInfo(popup).modal;
    seen->ownerIsParent = winInfo(popup).owner == owner;
    cxuiTestClose(owner);   // ignored while the popup is up
    cxuiWindowEndPopup(popup, 7);
    cxuiWindowEndPopup(popup, 8);   // the first end counts
    return CXUI_Default;
}

// A popup disables its owner, which ignores its close box meanwhile, and returns what ended it;
// closing or destroying the popup ends it with 0.
UI_TEST(popup)
{
    CloseLog ownerLog = { 0 };
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    CxUiWindow* popup = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Popup" });
    cxuiOnClose(owner, logClose, stvar(ptr, &ownerLog));
    cxuiWindowShow(owner, CXUI_SHOW_Normal);

    PopupSeen seen = { .ownerEnabled = true };
    CxUiTimer* t   = UI_AFTER(5, inspectPopup, stvar(object, popup), stvar(object, owner), stvar(ptr, &seen));
    int32 r        = cxuiWindowPopup(popup, owner);
    objRelease(&t);
    if (r != 7 || seen.ownerEnabled || !seen.popupVisible || !seen.popupModal || !seen.ownerIsParent)
        TEST_FAIL(1, _SL("popup returned ${int}; inside it the owner was enabled ${int}, popup visible ${int}, modal ${int}, owned ${int}"),
                  stvar(int32, r), stvar(int32, seen.ownerEnabled), stvar(int32, seen.popupVisible),
                  stvar(int32, seen.popupModal), stvar(int32, seen.ownerIsParent));
    if (ownerLog.closes != 0 || !cxuiWindowIsVisible(owner))
        TEST_FAIL(1, _SL("the owner's close box worked during the popup"), stvNone);
    if (!winInfo(owner).enabled || cxuiWindowIsVisible(popup) || winInfo(popup).visible || winInfo(popup).modal ||
        winInfo(popup).owner)
        TEST_FAIL(1, _SL("after the popup: owner enabled ${int}, popup visible ${int}, modal ${int}"),
                  stvar(int32, winInfo(owner).enabled), stvar(int32, winInfo(popup).visible),
                  stvar(int32, winInfo(popup).modal));
    if (cxuiWindowPopup(owner, owner) != 0)
        TEST_FAIL(1, _SL("a window popped up over itself"), stvNone);

    t = UI_AFTER(5, timerClose, stvar(object, popup));
    r = cxuiWindowPopup(popup, owner);
    objRelease(&t);
    if (r != 0 || cxuiWindowIsVisible(popup) || !winInfo(owner).enabled)
        TEST_FAIL(1, _SL("closing a popup: returned ${int}, still visible ${int}"), stvar(int32, r),
                  stvar(int32, cxuiWindowIsVisible(popup)));

    t = UI_AFTER(5, timerDestroy, stvar(object, popup));
    r = cxuiWindowPopup(popup, owner);
    objRelease(&t);
    if (r != 0 || !(CxUiWidget(popup)->state & CXUI_WS_Dead) || !winInfo(owner).enabled)
        TEST_FAIL(1, _SL("destroying a popup: returned ${int}, owner enabled ${int}"), stvar(int32, r),
                  stvar(int32, winInfo(owner).enabled));
    if (cxuiWindowPopup(popup, owner) != 0)
        TEST_FAIL(1, _SL("a dead window popped up"), stvNone);

    objRelease(&popup);
    objRelease(&owner);
    return 0;
}

typedef struct NestLog {
    int32 order[4];   // which popup returned: 1 for the outer, 2 for the inner
    int32 n;
    int32 innerResult;
    bool outerDisabledInInner;
    bool outerReturnedEarly;
    bool outerEnabledAfterInner;
} NestLog;

static CxUiResult endInner(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* outer = stvlAtObj(cvars, 0, CxUiWindow);
    CxUiWindow* inner = stvlAtObj(cvars, 1, CxUiWindow);
    NestLog* log      = stvlAtPtr(cvars, 2);
    log->outerDisabledInInner = !winInfo(outer).enabled;
    log->outerReturnedEarly   = log->n > 0;
    cxuiWindowEndPopup(inner, 2);
    return CXUI_Default;
}

static CxUiResult openInner(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* outer = stvlAtObj(cvars, 0, CxUiWindow);
    CxUiWindow* inner = stvlAtObj(cvars, 1, CxUiWindow);
    NestLog* log      = stvlAtPtr(cvars, 2);
    // the outer popup is ended first, while the inner one is still up
    CxUiTimer* endOuter = UI_AFTER(5, timerEndPopup, stvar(object, outer), stvar(int32, 1));
    CxUiTimer* end      = UI_AFTER(30, endInner, stvar(object, outer), stvar(object, inner), stvar(ptr, log));
    log->innerResult    = cxuiWindowPopup(inner, outer);
    log->order[log->n++] = 2;
    log->outerEnabledAfterInner = winInfo(outer).enabled;
    objRelease(&endOuter);
    objRelease(&end);
    return CXUI_Default;
}

// Ending an outer popup while an inner one is up: the outer call returns only after the inner one.
UI_TEST(popupnested)
{
    NestLog log       = { 0 };
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    CxUiWindow* outer = cxuiWindowCreate(NULL);
    CxUiWindow* inner = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);

    CxUiTimer* t = UI_AFTER(5, openInner, stvar(object, outer), stvar(object, inner), stvar(ptr, &log));
    int32 r      = cxuiWindowPopup(outer, owner);
    log.order[log.n++] = 1;
    objRelease(&t);

    if (r != 1 || log.innerResult != 2 || log.n != 2 || log.order[0] != 2 || log.order[1] != 1)
        TEST_FAIL(1, _SL("results outer ${int}, inner ${int}; first to return ${int}"), stvar(int32, r),
                  stvar(int32, log.innerResult), stvar(int32, log.order[0]));
    if (log.outerReturnedEarly || !log.outerDisabledInInner || !log.outerEnabledAfterInner)
        TEST_FAIL(1, _SL("outer returned early ${int}, disabled under the inner ${int}, enabled after it ${int}"),
                  stvar(int32, log.outerReturnedEarly), stvar(int32, log.outerDisabledInInner),
                  stvar(int32, log.outerEnabledAfterInner));
    if (!winInfo(owner).enabled || winInfo(outer).visible || winInfo(inner).visible)
        TEST_FAIL(1, _SL("after both: owner enabled ${int}, popups visible ${int} ${int}"),
                  stvar(int32, winInfo(owner).enabled), stvar(int32, winInfo(outer).visible),
                  stvar(int32, winInfo(inner).visible));

    objRelease(&inner);
    objRelease(&outer);
    objRelease(&owner);
    return 0;
}

typedef struct TickLog {
    int32 ticks;
    int32 endAt;
    CxUiWindow* popup;
} TickLog;

static CxUiResult tickAndEnd(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    TickLog* log = stvlAtPtr(cvars, 0);
    if (++log->ticks == log->endAt)
        cxuiWindowEndPopup(log->popup, 3);
    return CXUI_Default;
}

// A repeating timer runs inside a popup, can end it from its handler, and keeps running after.
UI_TEST(popuptimer)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    CxUiWindow* popup = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);

    TickLog log    = { .endAt = 5, .popup = popup };
    CxUiTimer* rep = cxuiTimerCreate(timeMS(2), 0, tickAndEnd, stvar(ptr, &log));
    cxuiTimerStart(rep);
    int32 r        = cxuiWindowPopup(popup, owner);
    int32 ticksAt  = log.ticks;
    bool running   = cxuiTimerIsRunning(rep);

    // a second popup, ended by a one-shot, while the repeating timer goes on; long enough for 4
    // ticks at XP's 15.6 ms timer resolution
    CxUiTimer* t = UI_AFTER(100, timerEndPopup, stvar(object, popup), stvar(int32, 4));
    int32 r2     = cxuiWindowPopup(popup, owner);
    int32 ticks2 = log.ticks;
    objRelease(&t);
    objRelease(&rep);

    if (r != 3 || ticksAt != 5 || !running)
        TEST_FAIL(1, _SL("popup ended by its timer returned ${int} after ${int} ticks (want 3 after 5), running ${int}"),
                  stvar(int32, r), stvar(int32, ticksAt), stvar(int32, running));
    if (r2 != 4 || ticks2 < ticksAt + 4)
        TEST_FAIL(1, _SL("second popup returned ${int}; the timer ticked ${int} times during it"), stvar(int32, r2),
                  stvar(int32, ticks2 - ticksAt));

    objRelease(&popup);
    objRelease(&owner);
    return 0;
}

typedef struct AppModalSeen {
    bool othersDisabled;
    bool popupEnabled;
    bool outerDisabledInInner;
    bool outerEnabledAfterInner;
    bool othersStillDisabled;
} AppModalSeen;

static CxUiResult inspectInner(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* outer  = stvlAtObj(cvars, 0, CxUiWindow);
    CxUiWindow* inner  = stvlAtObj(cvars, 1, CxUiWindow);
    AppModalSeen* seen = stvlAtPtr(cvars, 2);
    seen->outerDisabledInInner = !winInfo(outer).enabled;
    cxuiWindowEndPopup(inner, 1);
    return CXUI_Default;
}

static CxUiResult appModalInside(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* w1     = stvlAtObj(cvars, 0, CxUiWindow);
    CxUiWindow* w2     = stvlAtObj(cvars, 1, CxUiWindow);
    CxUiWindow* outer  = stvlAtObj(cvars, 2, CxUiWindow);
    CxUiWindow* inner  = stvlAtObj(cvars, 3, CxUiWindow);
    AppModalSeen* seen = stvlAtPtr(cvars, 4);
    seen->othersDisabled = !winInfo(w1).enabled && !winInfo(w2).enabled;
    seen->popupEnabled   = winInfo(outer).enabled;

    CxUiTimer* t = UI_AFTER(5, inspectInner, stvar(object, outer), stvar(object, inner), stvar(ptr, seen));
    cxuiWindowPopup(inner, CxUiWindowNone);
    objRelease(&t);
    seen->outerEnabledAfterInner = winInfo(outer).enabled;
    seen->othersStillDisabled    = !winInfo(w1).enabled && !winInfo(w2).enabled;
    cxuiWindowEndPopup(outer, 1);
    return CXUI_Default;
}

// A popup without a parent disables every other window, nested ones included, and each window
// comes back when the last popup disabling it ends.
UI_TEST(popupappmodal)
{
    AppModalSeen seen = { 0 };
    CxUiWindow* w1    = cxuiWindowCreate(NULL);
    CxUiWindow* w2    = cxuiWindowCreate(NULL);
    CxUiWindow* outer = cxuiWindowCreate(NULL);
    CxUiWindow* inner = cxuiWindowCreate(NULL);
    cxuiWindowShow(w1, CXUI_SHOW_Normal);
    cxuiWindowRealize(w2);

    CxUiTimer* t = UI_AFTER(5, appModalInside, stvar(object, w1), stvar(object, w2), stvar(object, outer),
                            stvar(object, inner), stvar(ptr, &seen));
    int32 r      = cxuiWindowPopup(outer, CxUiWindowNone);
    objRelease(&t);

    if (r != 1 || !seen.othersDisabled || !seen.popupEnabled || !seen.outerDisabledInInner ||
        !seen.outerEnabledAfterInner || !seen.othersStillDisabled)
        TEST_FAIL(1, _SL("others disabled ${int}, popup enabled ${int}, outer disabled under inner ${int}, outer back ${int}, others still disabled ${int}"),
                  stvar(int32, seen.othersDisabled), stvar(int32, seen.popupEnabled),
                  stvar(int32, seen.outerDisabledInInner), stvar(int32, seen.outerEnabledAfterInner),
                  stvar(int32, seen.othersStillDisabled));
    if (!winInfo(w1).enabled || !winInfo(w2).enabled || !winInfo(outer).enabled)
        TEST_FAIL(1, _SL("windows left disabled after the popups"), stvNone);

    objRelease(&inner);
    objRelease(&outer);
    objRelease(&w2);
    objRelease(&w1);
    return 0;
}

typedef struct BoxSeen {
    int32 boxes;
    bool ownerEnabled;
    bool badAnswer;
    bool answered;
    bool builtShown;   // a box cxui built was taken for the innermost
    int32 inPopupResult;
} BoxSeen;

static CxUiResult answerBox(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* owner = stvlAtObj(cvars, 0, CxUiWindow);
    BoxSeen* seen     = stvlAtPtr(cvars, 2);
    seen->boxes        = cxuiTestMessageBoxes();
    seen->ownerEnabled = winInfo(owner).enabled;
    seen->builtShown   = cxuiTestBuiltMessageBox() != NULL;
    seen->badAnswer    = cxuiTestAnswerMessageBox(9);
    seen->answered     = cxuiTestAnswerMessageBox(stvlAt(cvars, 1, int32));
    return CXUI_Default;
}

static CxUiResult boxInPopup(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* popup = stvlAtObj(cvars, 0, CxUiWindow);
    BoxSeen* seen     = stvlAtPtr(cvars, 1);
    BoxSeen inner     = { 0 };
    CxUiTimer* t      = UI_AFTER(5, answerBox, stvar(object, popup), stvar(int32, 1), stvar(ptr, &inner));
    seen->inPopupResult =
        cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent = popup, .text = _S"Inside a popup", .buttons = CXUI_MB_YesNo });
    objRelease(&t);
    cxuiWindowEndPopup(popup, 5);
    return CXUI_Default;
}

// A message box disables its parent until it is answered, and can open inside a popup.
UI_TEST(msgbox)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);

    BoxSeen seen = { .ownerEnabled = true };
    CxUiTimer* t = UI_AFTER(5, answerBox, stvar(object, owner), stvar(int32, 2), stvar(ptr, &seen));
    int32 r      = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent  = owner,
                                                         .title   = _S"Question",
                                                         .text    = _S"Go on?",
                                                         .kind    = CXUI_MB_Question,
                                                         .buttons = CXUI_MB_OkCancel });
    objRelease(&t);
    if (r != 2 || seen.boxes != 1 || seen.ownerEnabled || seen.badAnswer || !seen.answered)
        TEST_FAIL(1, _SL("box returned ${int}; inside it ${int} boxes, owner enabled ${int}, bad answer taken ${int}"),
                  stvar(int32, r), stvar(int32, seen.boxes), stvar(int32, seen.ownerEnabled), stvar(int32, seen.badAnswer));
    if (cxuiTestMessageBoxes() != 0 || !winInfo(owner).enabled || cxuiTestAnswerMessageBox(1))
        TEST_FAIL(1, _SL("after the box: ${int} open, owner enabled ${int}"), stvar(int32, cxuiTestMessageBoxes()),
                  stvar(int32, winInfo(owner).enabled));

    CxUiWindow* popup = cxuiWindowCreate(NULL);
    seen              = (BoxSeen){ 0 };
    t                 = UI_AFTER(5, boxInPopup, stvar(object, popup), stvar(ptr, &seen));
    r                 = cxuiWindowPopup(popup, owner);
    objRelease(&t);
    if (r != 5 || seen.inPopupResult != 1)
        TEST_FAIL(1, _SL("a box inside a popup returned ${int}, the popup ${int}"), stvar(int32, seen.inPopupResult),
                  stvar(int32, r));

    objRelease(&popup);
    objRelease(&owner);
    return 0;
}

typedef struct CustomSeen {
    bool ownerEnabled;
    bool badAnswer;
    bool vetoed;          // the close box left it open
    int32 nested;         // what a box opened inside it returned
    bool outerKept;       // the built box was still open after it
    int32 buttons;
    strref title;         // borrowed while open
    bool titled;
    bool defaultFocused;
} CustomSeen;

// Inspects the built box, then answers it: 0 with a button, 1 with Escape, 2 with Enter, 3 with
// the close box and then button 1.
static CxUiResult customAnswer(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    CxUiWindow* owner = stvlAtObj(cvars, 0, CxUiWindow);
    int32 how         = stvlAt(cvars, 1, int32);
    CustomSeen* seen  = stvlAtPtr(cvars, 2);
    CxUiWindow* box   = cxuiTestBuiltMessageBox();
    if (!box)
        return CXUI_Default;
    seen->ownerEnabled = winInfo(owner).enabled;
    CxUiWidget* row    = cxuiContainerChild(cxuiContainerChild(box, 0), 1);
    seen->buttons      = cxuiContainerChildCount(row) - 1;
    seen->titled       = strEq(cxuiWindowTitle(box), _S"Update");
    CxUiWidget* second = cxuiContainerChild(row, 2);
    seen->defaultFocused = second && cxuiFocused(box) == second && objDynCast(CxUiButton, second) &&
                           strEq(cxuiButtonTitle(objDynCast(CxUiButton, second)), _S"&Later");
    seen->badAnswer = cxuiTestAnswerMessageBox(9);
    switch (how) {
    case 0: {
        // An answer goes to the innermost box, here one the platform shows.
        BoxSeen inner     = { 0 };
        CxUiTimer* t      = UI_AFTER(5, answerBox, stvar(object, box), stvar(int32, 1), stvar(ptr, &inner));
        seen->nested      = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent = box, .text = _S"Sure?" });
        seen->outerKept   = cxuiTestBuiltMessageBox() == box && !inner.builtShown;
        objRelease(&t);
        cxuiTestAnswerMessageBox(1);
        break;
    }
    case 1:
        cxuiTestKey(CxUiWidget(box), CXUI_KEY_Escape, 0, true);
        break;
    case 2:
        cxuiTestKey(CxUiWidget(box), CXUI_KEY_Enter, 0, true);
        break;
    default:
        cxuiTestClose(box);
        seen->vetoed = cxuiTestBuiltMessageBox() == box;
        cxuiTestKey(CxUiWidget(box), CXUI_KEY_Escape, 0, true);
        seen->vetoed = seen->vetoed && cxuiTestBuiltMessageBox() == box;
        cxuiTestAnswerMessageBox(1);
        break;
    }
    cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

// Custom buttons where the platform has no box for them: cxui builds one with the titles, the
// default button focused, its parent disabled; a button, Escape (the cancel button), Enter (the
// default) and the close box answer it, and with no cancel button (or one out of range) Escape and
// the close box can't.
// A custom box with no titles is an OK box.
UI_TEST(msgboxcustom)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);
    static const int32 want[] = { 1, 3, 2, 1 };
    for (int32 how = 0; how < 4; how++) {
        CustomSeen seen = { .ownerEnabled = true };
        CxUiTimer* t    = cxuiTimerCreate(timeMS(5), 0, customAnswer, stvar(object, owner), stvar(int32, how),
                                          stvar(ptr, &seen));
        cxuiTimerStart(t);
        int32 r = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent        = owner,
                                                        .title         = _S"Update",
                                                        .text          = _S"A new version is ready.",
                                                        .buttons       = CXUI_MB_Custom,
                                                        .custom        = { _S"&Install", _S"&Later", _S"&Never" },
                                                        .defaultButton = 2,
                                                        .cancelButton  = how == 3 ? 7 : 3 });
        cxuiTimerStop(t);
        objRelease(&t);
        if (r != want[how] || seen.buttons != 3 || !seen.titled || !seen.defaultFocused || seen.ownerEnabled ||
            seen.badAnswer || (how == 3 && !seen.vetoed) || (how == 0 && (seen.nested != 1 || !seen.outerKept)))
            TEST_FAIL(1, _SL("answered by ${int}: returned ${int} (want ${int}), ${int} buttons, titled ${int}, default "
                             "focused ${int}, owner enabled ${int}, bad answer taken ${int}, close refused ${int}"),
                      stvar(int32, how), stvar(int32, r), stvar(int32, want[how]), stvar(int32, seen.buttons),
                      stvar(int32, seen.titled), stvar(int32, seen.defaultFocused), stvar(int32, seen.ownerEnabled),
                      stvar(int32, seen.badAnswer), stvar(int32, seen.vetoed));
    }
    if (cxuiTestMessageBoxes() != 0 || cxuiTestBuiltMessageBox() || !winInfo(owner).enabled)
        TEST_FAIL(1, _SL("after the boxes: ${int} open, owner enabled ${int}"), stvar(int32, cxuiTestMessageBoxes()),
                  stvar(int32, winInfo(owner).enabled));

    BoxSeen plain = { 0 };
    CxUiTimer* t  = UI_AFTER(5, answerBox, stvar(object, owner), stvar(int32, 1), stvar(ptr, &plain));
    int32 r       = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent = owner, .text = _S"No titles", .buttons = CXUI_MB_Custom });
    objRelease(&t);
    if (r != 1 || !plain.answered || plain.badAnswer)
        TEST_FAIL(1, _SL("a custom box without titles returned ${int}"), stvar(int32, r));

    objRelease(&owner);
    return 0;
}

typedef struct ModalDone {
    int32 calls;
    int32 result;
    CxUiWindow* w;
} ModalDone;

static bool modalDone(stvlist* cvars, stvlist* args)
{
    ModalDone* md = stvlAtPtr(cvars, 0);
    md->calls++;
    md->w      = stvlAtObj(args, 0, CxUiWindow);
    md->result = stvlAt(args, 1, int32);
    return true;
}

// A modal show returns at once with its parent disabled; ending it, Escape and destroying the
// window each call onDone once with the result and give the parent back; a second show while one
// runs fails without calling its onDone.
UI_TEST(showmodal)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    CxUiWindow* w     = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);

    ModalDone md = { 0 }, extra = { 0 };
    bool shown   = cxuiWindowShowModal(w, owner, closureCreate(modalDone, stvar(ptr, &md)));
    bool again   = cxuiWindowShowModal(w, owner, closureCreate(modalDone, stvar(ptr, &extra)));
    if (!shown || again || winInfo(owner).enabled || !winInfo(w).visible || !winInfo(w).modal || md.calls)
        TEST_FAIL(1, _SL("shown ${int}, shown again ${int}, owner enabled ${int}, visible ${int}, modal ${int}, "
                         "ended ${int} times"),
                  stvar(int32, shown), stvar(int32, again), stvar(int32, winInfo(owner).enabled),
                  stvar(int32, winInfo(w).visible), stvar(int32, winInfo(w).modal), stvar(int32, md.calls));
    cxuiWindowEndPopup(w, 4);
    if (md.calls != 1 || md.result != 4 || md.w != w || !winInfo(owner).enabled || winInfo(w).visible || winInfo(w).modal ||
        extra.calls)
        TEST_FAIL(1, _SL("ended: ${int} calls with ${int}, owner enabled ${int}, visible ${int}, modal ${int}"),
                  stvar(int32, md.calls), stvar(int32, md.result), stvar(int32, winInfo(owner).enabled),
                  stvar(int32, winInfo(w).visible), stvar(int32, winInfo(w).modal));

    md = (ModalDone){ .result = -1 };
    cxuiWindowShowModal(w, owner, closureCreate(modalDone, stvar(ptr, &md)));
    cxuiTestKey(CxUiWidget(w), CXUI_KEY_Escape, 0, true);
    if (md.calls != 1 || md.result != 0 || !winInfo(owner).enabled || winInfo(w).visible)
        TEST_FAIL(1, _SL("Escape: ${int} calls with ${int}, owner enabled ${int}"), stvar(int32, md.calls),
                  stvar(int32, md.result), stvar(int32, winInfo(owner).enabled));

    md = (ModalDone){ .result = -1 };
    cxuiWindowShowModal(w, CxUiWindowNone, closureCreate(modalDone, stvar(ptr, &md)));
    cxuiWindowEndPopup(w, 2);
    cxuiWindowShowModal(w, CxUiWindowNone, closureCreate(modalDone, stvar(ptr, &md)));
    cxuiDestroy(w);
    if (md.calls != 2 || md.result != 0 || !winInfo(owner).enabled)
        TEST_FAIL(1, _SL("destroyed: ${int} calls with ${int}, owner enabled ${int}"), stvar(int32, md.calls),
                  stvar(int32, md.result), stvar(int32, winInfo(owner).enabled));

    objRelease(&w);
    objRelease(&owner);
    return 0;
}

static CxUiResult vetoClose(stvlist* cvars, CxUiCloseEvent* ev)
{
    unused_noeval(ev);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return CXUI_Handled;
}

// A progress dialog shows its count as a share of the total, or activity without one; Cancel and
// Escape cancel it without closing it, unless its close handler takes them.
UI_TEST(progressdialog)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);
    CxUiProgressDialog* d = cxuiProgressDialogCreate(&(CxUiProgressDialogDesc){
        .title = _S"Copying", .text = _S"Copying files", .total = 8, .cancel = true, .cancelTitle = _S"&Stop" });
    cxuiWindowShowModal(d, owner, NULL);
    CxUiWidget* col    = cxuiContainerChild(d, 0);
    CxUiWidget* bar    = cxuiContainerChild(col, 1);
    CxUiWidget* cancel = cxuiContainerChild(col, 2);
    cxuiProgressDialogSetCount(d, 2);
    CxUiTestControlInfo bi = { 0 }, ci = { 0 };
    cxuiTestControlInfo(bar, &bi);
    cxuiTestControlInfo(cancel, &ci);
    if (bi.pos != 2500 || bi.indeterminate || !strEq(ci.text, _S"&Stop") || winInfo(owner).enabled)
        TEST_FAIL(1, _SL("2 of 8 shows ${int} of 10000, activity ${int}; Cancel titled ${string}; owner enabled ${int}"),
                  stvar(int32, bi.pos), stvar(int32, bi.indeterminate), stvar(string, ci.text),
                  stvar(int32, winInfo(owner).enabled));
    cxuiProgressDialogSetCount(d, 12);
    cxuiTestControlInfo(bar, &bi);
    int32 over = bi.pos;
    cxuiProgressDialogSetTotal(d, 0);
    cxuiTestControlInfo(bar, &bi);
    if (over != 10000 || !bi.indeterminate)
        TEST_FAIL(1, _SL("past the total shows ${int}; no total shows activity ${int}"), stvar(int32, over),
                  stvar(int32, bi.indeterminate));

    cxuiTestClick(cancel);
    cxuiTestControlInfo(cancel, &ci);
    if (!cxuiProgressDialogCancelled(d) || ci.enabled || !winInfo(CxUiWindow(d)).visible || winInfo(owner).enabled)
        TEST_FAIL(1, _SL("after Cancel: cancelled ${int}, Cancel enabled ${int}, shown ${int}, owner enabled ${int}"),
                  stvar(int32, cxuiProgressDialogCancelled(d)), stvar(int32, ci.enabled), stvar(int32, winInfo(CxUiWindow(d)).visible),
                  stvar(int32, winInfo(owner).enabled));
    cxuiDestroy(d);
    objRelease(&d);
    if (!winInfo(owner).enabled)
        TEST_FAIL(1, _SL("the owner stayed disabled after the dialog was destroyed"), stvNone);

    int32 vetoes = 0;
    d            = cxuiProgressDialogCreate(&(CxUiProgressDialogDesc){ .cancel = true });
    cxuiOnClose(d, vetoClose, stvar(ptr, &vetoes));
    cxuiWindowShowModal(d, owner, NULL);
    cxuiTestKey(CxUiWidget(d), CXUI_KEY_Escape, 0, true);
    cxuiTestClose(CxUiWindow(d));
    if (vetoes != 2 || cxuiProgressDialogCancelled(d) || !winInfo(CxUiWindow(d)).visible)
        TEST_FAIL(1, _SL("a close handler taking it: asked ${int} times, cancelled ${int}, shown ${int}"),
                  stvar(int32, vetoes), stvar(int32, cxuiProgressDialogCancelled(d)), stvar(int32, winInfo(CxUiWindow(d)).visible));
    cxuiTestControlInfoDestroy(&bi);
    cxuiTestControlInfoDestroy(&ci);
    cxuiDestroy(d);
    objRelease(&d);
    objRelease(&owner);
    return 0;
}

typedef struct FileSeen {
    bool open;
    bool ownerEnabled;
    int32 dialogs;
    CxUiTestFileDialogInfo info;
} FileSeen;

// Records what the open dialog was asked for, then answers it with the path given, or cancels it
static CxUiResult fileAnswer(stvlist* cvars, CxUiTimerEvent* ev)
{
    CxUiWindow* owner = stvlAtObj(cvars, 0, CxUiWindow);
    FileSeen* seen    = stvlAtPtr(cvars, 2);
    if (!cxuiTestFileDialogInfo(&seen->info))
        return CXUI_Default;
    seen->open         = true;
    seen->dialogs      = cxuiTestFileDialogs();
    seen->ownerEnabled = winInfo(owner).enabled;
    strref answer      = stvlAt(cvars, 1, strref);
    cxuiTestAnswerFileDialog(strEmpty(answer) ? NULL : answer);
    cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

static uint32 fileAsk(CxUiWindow* owner, const CxUiFileDialogDesc* desc, strref answer, FileSeen* seen, string* path)
{
    cxuiTestFileDialogInfoDestroy(&seen->info);
    *seen = (FileSeen){ .ownerEnabled = true };
    CxUiTimer* t = cxuiTimerCreate(timeMS(5), 0, fileAnswer, stvar(object, owner), stvar(strref, answer), stvar(ptr, seen));
    cxuiTimerStart(t);
    uint32 r = cxuiFileDialog(desc, path);
    objRelease(&t);
    return r;
}

// A file dialog gets platform paths and only filters it can use, disables its parent, and gives
// back a cx path chosen with whether it exists, the default extension added to a name saved
// without one but not to one with its own; cancelled, the path is cleared.
UI_TEST(filedialog)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);
    static const CxUiFileFilter filters[] = { { _S"Text", _S"*.txt" }, { _S"All files", _S"*" } };
    string cur = 0, native = 0, path = 0, want = 0;
    fsCurDir(&cur);
    pathToPlatform(&native, cur);
    FileSeen seen = { 0 };

    uint32 r = fileAsk(owner,
                       &(CxUiFileDialogDesc){ .parent = owner, .title = _S"Choose", .mode = 9, .filters = filters,
                                              .nfilters = 2, .filterIndex = 5, .initialDir = cur },
                       cur, &seen, &path);
    if (r != CXUI_FILE_Existing || !strEq(path, cur) || !seen.open || seen.dialogs != 1 || seen.ownerEnabled ||
        seen.info.mode != CXUI_FILE_Open || seen.info.nfilters != 2 || seen.info.filterIndex != 0 ||
        !strEq(seen.info.title, _S"Choose") || !strEq(seen.info.initialDir, native))
        TEST_FAIL(1, _SL("open: returned ${uint} with ${string}; asked mode ${uint}, ${int} filters from ${int}, in "
                         "${string} (want ${string}); ${int} open, owner enabled ${int}"),
                  stvar(uint32, r), stvar(string, path), stvar(uint32, seen.info.mode), stvar(int32, seen.info.nfilters),
                  stvar(int32, seen.info.filterIndex), stvar(string, seen.info.initialDir), stvar(string, native),
                  stvar(int32, seen.dialogs), stvar(int32, seen.ownerEnabled));

    pathJoin(&want, cur, _S"cxui-filedialog-new");
    r = fileAsk(owner,
                &(CxUiFileDialogDesc){ .parent = owner, .mode = CXUI_FILE_Save, .defaultExt = _S"txt",
                                       .initialFile = _S"notes.txt", .flags = CXUI_FILE_NoOverwritePrompt },
                want, &seen, &path);
    strAppend(&want, _S".txt");
    if (r != CXUI_FILE_New || !strEq(path, want) || seen.info.mode != CXUI_FILE_Save ||
        !strEq(seen.info.initialFile, _S"notes.txt") || seen.info.flags != CXUI_FILE_NoOverwritePrompt ||
        !strEmpty(seen.info.initialDir))
        TEST_FAIL(1, _SL("save: returned ${uint} with ${string} (want ${string}); asked to fill in ${string}, flags ${uint}"),
                  stvar(uint32, r), stvar(string, path), stvar(string, want), stvar(string, seen.info.initialFile),
                  stvar(uint32, seen.info.flags));

    strDestroy(&want);
    pathJoin(&want, cur, _S"notes.md");
    r = fileAsk(owner, &(CxUiFileDialogDesc){ .mode = CXUI_FILE_Save, .defaultExt = _S"txt" }, want, &seen, &path);
    if (!strEq(path, want))
        TEST_FAIL(1, _SL("a name saved with its own extension became ${string}"), stvar(string, path));

    r = fileAsk(owner, &(CxUiFileDialogDesc){ .mode = CXUI_FILE_Folder, .filters = filters, .nfilters = 2 }, NULL, &seen,
                &path);
    if (r != CXUI_FILE_Cancelled || !strEmpty(path) || seen.info.mode != CXUI_FILE_Folder || seen.info.nfilters != 0 ||
        cxuiTestFileDialogs() != 0 || !winInfo(owner).enabled)
        TEST_FAIL(1, _SL("cancelled folder: returned ${uint} with ${string}, ${int} filters; ${int} still open"),
                  stvar(uint32, r), stvar(string, path), stvar(int32, seen.info.nfilters), stvar(int32, cxuiTestFileDialogs()));
    if (cxuiTestAnswerFileDialog(NULL))
        TEST_FAIL(1, _SL("answered a file dialog when none was open"), stvNone);

    cxuiTestFileDialogInfoDestroy(&seen.info);
    strDestroy(&cur);
    strDestroy(&native);
    strDestroy(&path);
    strDestroy(&want);
    objRelease(&owner);
    return 0;
}

typedef struct ColorSeen {
    bool open;
    bool ownerEnabled;
    int32 dialogs;
    CxUiColor start;
    string title;
} ColorSeen;

// Records what the open dialog started with, then answers it with the color given, or cancels it
static CxUiResult colorAnswer(stvlist* cvars, CxUiTimerEvent* ev)
{
    CxUiWindow* owner = stvlAtObj(cvars, 0, CxUiWindow);
    ColorSeen* seen   = stvlAtPtr(cvars, 2);
    if (!cxuiTestColorDialogInfo(&seen->start, &seen->title))
        return CXUI_Default;
    seen->open         = true;
    seen->dialogs      = cxuiTestColorDialogs();
    seen->ownerEnabled = winInfo(owner).enabled;
    cxuiTestAnswerColorDialog(stvlAtPtr(cvars, 1));
    cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

static bool colorAsk(CxUiWindow* owner, const CxUiColorDialogDesc* desc, const CxUiColor* answer, ColorSeen* seen,
                     CxUiColor* color)
{
    strDestroy(&seen->title);
    *seen        = (ColorSeen){ .ownerEnabled = true };
    CxUiTimer* t = cxuiTimerCreate(timeMS(5), 0, colorAnswer, stvar(object, owner), stvar(ptr, (void*)answer),
                                   stvar(ptr, seen));
    cxuiTimerStart(t);
    bool chosen = cxuiColorDialog(desc, color);
    objRelease(&t);
    return chosen;
}

// A color dialog starts at the color given, disables its parent, and gives back the color chosen
// with the alpha it had; cancelled, the color is left as it was.
UI_TEST(colordialog)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);
    ColorSeen seen   = { 0 };
    CxUiColor answer = cxuiRGB(10, 200, 30);
    CxUiColor c      = cxuiRGBA(1, 2, 3, 128);

    bool chosen = colorAsk(owner, &(CxUiColorDialogDesc){ .parent = owner, .title = _S"Tint" }, &answer, &seen, &c);
    if (!chosen || c.bits != cxuiRGBA(10, 200, 30, 128).bits || !seen.open || seen.dialogs != 1 || seen.ownerEnabled ||
        seen.start.r != 1 || seen.start.g != 2 || seen.start.b != 3 || !strEq(seen.title, _S"Tint"))
        TEST_FAIL(1, _SL("chosen ${int}: ${uint(hex)}; it started at ${uint(hex)} titled ${string}; ${int} open, owner "
                         "enabled ${int}"),
                  stvar(int32, chosen), stvar(uint32, c.bits), stvar(uint32, seen.start.bits), stvar(string, seen.title),
                  stvar(int32, seen.dialogs), stvar(int32, seen.ownerEnabled));

    CxUiColor before = c;
    chosen           = colorAsk(owner, &(CxUiColorDialogDesc){ 0 }, NULL, &seen, &c);
    if (chosen || c.bits != before.bits || !seen.open || !strEmpty(seen.title) || cxuiTestColorDialogs() != 0 ||
        !winInfo(owner).enabled)
        TEST_FAIL(1, _SL("cancelled: chosen ${int}, ${uint(hex)} (was ${uint(hex)}); ${int} still open"),
                  stvar(int32, chosen), stvar(uint32, c.bits), stvar(uint32, before.bits),
                  stvar(int32, cxuiTestColorDialogs()));
    if (cxuiTestAnswerColorDialog(&answer))
        TEST_FAIL(1, _SL("answered a color dialog when none was open"), stvNone);

    strDestroy(&seen.title);
    objRelease(&owner);
    return 0;
}

static CxUiResult colorAnswerWhenOpen(stvlist* cvars, CxUiTimerEvent* ev)
{
    if (cxuiTestAnswerColorDialog(stvlAtPtr(cvars, 0)))
        cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

static bool uiStartColorAnswering(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTimer** out = stvlAtPtr(cvars, 0);
    *out            = cxuiTimerCreate(timeMS(5), 0, colorAnswerWhenOpen, stvar(ptr, stvlAtPtr(cvars, 1)));
    return cxuiTimerStart(*out);
}

// A color dialog from another thread waits there for the answer; with cxui stopped it chooses
// nothing.
static int test_cxui_colorthread(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    CxUiTimer* t     = NULL;
    CxUiColor answer = cxuiRGB(0, 64, 255);
    cxuiCallSync(closureCreate(uiStartColorAnswering, stvar(ptr, &t), stvar(ptr, &answer)));
    CxUiColor c = cxuiRGB(255, 255, 255);
    bool chosen = cxuiColorDialog(&(CxUiColorDialogDesc){ 0 }, &c);
    cxuiShutdown();
    objRelease(&t);
    CxUiColor after = c;
    bool stopped    = cxuiColorDialog(&(CxUiColorDialogDesc){ 0 }, &after);

    if (!chosen || c.bits != answer.bits || stopped || after.bits != c.bits)
        TEST_FAIL(1, _SL("from another thread: chosen ${int}, ${uint(hex)}; with cxui stopped chosen ${int}"),
                  stvar(int32, chosen), stvar(uint32, c.bits), stvar(int32, stopped));
    return 0;
}

#if defined(_PLATFORM_UNIX) && !defined(_PLATFORM_MACOS)
static bool uiHeadlessWindow(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiWindow* w = cxuiWindowCreate(NULL);
    CxUiTestWindowInfo info;
    *(bool*)stvlAtPtr(cvars, 0) = cxuiWindowRealize(w) && cxuiTestWindowInfo(w, &info) && !info.visible;
    cxuiDestroy(w);
    objRelease(&w);
    return true;
}
#endif

// Where no display can be reached, cxui doesn't start unless allowed to run headless, and then
// runs on the null backend, where a window can be realized without being shown. ctest runs it
// with the display variables cleared.
static int test_cxui_headless(void)
{
#if defined(_PLATFORM_UNIX) && !defined(_PLATFORM_MACOS)
    string x11 = 0, wayland = 0;
    envGet(&x11, _SL("DISPLAY"));
    envGet(&wayland, _SL("WAYLAND_DISPLAY"));
    bool display = !strEmpty(x11) || !strEmpty(wayland);
    strDestroy(&x11);
    strDestroy(&wayland);
    if (display)
        TEST_SKIP(_SL("a display can be reached"), stvNone);

    if (cxuiStart(&(CxUiStartDesc){ 0 })) {
        cxuiShutdown();
        TEST_FAIL(1, _SL("started without a display"), stvNone);
    }
    if (!cxuiStart(&(CxUiStartDesc){ .flags = CXUI_Start_AllowHeadless }))
        TEST_FAIL(1, _SL("didn't start headless"), stvNone);
    bool realized = false;
    cxuiCallSync(closureCreate(uiHeadlessWindow, stvar(ptr, &realized)));
    cxuiShutdown();
    if (!realized)
        TEST_FAIL(1, _SL("headless, a window wasn't realized hidden on the null backend"), stvNone);
    return 0;
#else
    TEST_SKIP(_SL("this platform always has a display"), stvNone);
#endif
}

typedef struct FamilySeen {
    bool mainEnabled, toolEnabled, otherEnabled;
} FamilySeen;

static CxUiResult inspectFamily(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    FamilySeen* fam   = stvlAtPtr(cvars, 4);
    fam->mainEnabled  = winInfo(stvlAtObj(cvars, 1, CxUiWindow)).enabled;
    fam->toolEnabled  = winInfo(stvlAtObj(cvars, 2, CxUiWindow)).enabled;
    fam->otherEnabled = winInfo(stvlAtObj(cvars, 3, CxUiWindow)).enabled;
    cxuiWindowEndPopup(stvlAtObj(cvars, 0, CxUiWindow), 1);
    return CXUI_Default;
}

// A window's owner reaches the platform when the window is realized, which realizes the owner
// too; a popup's parent stands in for it while the popup runs, and a popup over one window of an
// owner's makes the others wait too. A window can't be owned by one it owns. Destroying the owner
// leaves the windows it owned open and owned by nothing.
UI_TEST(windowowner)
{
    CxUiWindow* main  = cxuiWindowCreate(NULL);
    CxUiWindow* tool  = cxuiWindowCreate(NULL);
    CxUiWindow* other = cxuiWindowCreate(NULL);
    CxUiTestWindowInfo info;

    cxuiWindowSetOwner(tool, main);
    cxuiWindowShow(tool, CXUI_SHOW_Normal);
    if (cxuiWindowOwner(tool) != main || !cxuiTestWindowInfo(main, &info) || info.visible ||
        winInfo(tool).owner != main)
        TEST_FAIL(1, _SL("owned: owner ${int}, realized ${int} and shown ${int}, native owner ${int}"),
                  stvar(int32, cxuiWindowOwner(tool) == main), stvar(int32, cxuiTestWindowInfo(main, &info)),
                  stvar(int32, info.visible), stvar(int32, winInfo(tool).owner == main));

    cxuiWindowSetOwner(main, tool);
    cxuiWindowSetOwner(other, tool);
    cxuiWindowSetOwner(main, other);
    cxuiWindowSetOwner(tool, tool);
    if (cxuiWindowOwner(main) || cxuiWindowOwner(tool) != main || cxuiWindowOwner(other) != tool)
        TEST_FAIL(1, _SL("a cycle of owners was let through"), stvNone);
    cxuiWindowSetOwner(other, CxUiWindowNone);
    if (cxuiWindowOwner(other))
        TEST_FAIL(1, _SL("the owner wasn't taken away"), stvNone);

    cxuiWindowShow(other, CXUI_SHOW_Normal);
    PopupSeen seen = { 0 };
    CxUiTimer* t   = UI_AFTER(5, inspectPopup, stvar(object, tool), stvar(object, other), stvar(ptr, &seen));
    cxuiWindowPopup(tool, other);
    objRelease(&t);
    if (!seen.ownerIsParent || winInfo(tool).owner != main)
        TEST_FAIL(1, _SL("a popup over another window: owned by it ${int}, then by its own owner ${int}"),
                  stvar(int32, seen.ownerIsParent), stvar(int32, winInfo(tool).owner == main));

    // A popup over a window makes its owner, and the owner's other windows, wait too.
    CxUiWindow* popup = cxuiWindowCreate(NULL);
    FamilySeen fam    = { 0 };
    for (int32 over = 0; over < 2; over++) {
        CxUiWindow* parent = over ? main : tool;
        t                  = UI_AFTER(5, inspectFamily, stvar(object, popup), stvar(object, main),
                                      stvar(object, tool), stvar(object, other), stvar(ptr, &fam));
        cxuiWindowPopup(popup, parent);
        objRelease(&t);
        if (fam.mainEnabled || fam.toolEnabled || !fam.otherEnabled)
            TEST_FAIL(1, _SL("a popup over the ${string} window: main enabled ${int}, owned ${int}, other ${int}"),
                      stvar(strref, over ? _S"main" : _S"owned"), stvar(int32, fam.mainEnabled),
                      stvar(int32, fam.toolEnabled), stvar(int32, fam.otherEnabled));
    }
    if (!winInfo(main).enabled || !winInfo(tool).enabled)
        TEST_FAIL(1, _SL("the windows stayed disabled after the popup"), stvNone);
    cxuiDestroy(popup);
    objRelease(&popup);

    cxuiWindowSetOwner(tool, other);
    bool moved = winInfo(tool).owner == other;
    cxuiWindowSetOwner(tool, CxUiWindowNone);
    bool cleared = winInfo(tool).owner == NULL;
    if (!moved || !cleared)
        TEST_FAIL(1, _SL("a realized window's owner changed ${int}, cleared ${int}"), stvar(int32, moved),
                  stvar(int32, cleared));

    cxuiWindowSetOwner(tool, main);
    cxuiWindowShow(tool, CXUI_SHOW_Normal);
    cxuiDestroy(main);
    if (cxuiWindowOwner(tool) || winInfo(tool).owner || !winInfo(tool).visible || !cxuiWindowIsVisible(tool))
        TEST_FAIL(1, _SL("its owner destroyed: still owned ${int}, natively ${int}, shown ${int}"),
                  stvar(int32, cxuiWindowOwner(tool) != NULL), stvar(int32, winInfo(tool).owner != NULL),
                  stvar(int32, winInfo(tool).visible));
    cxuiWindowSetOwner(tool, main);
    if (cxuiWindowOwner(tool))
        TEST_FAIL(1, _SL("a destroyed window became an owner"), stvNone);

    cxuiDestroy(tool);
    cxuiDestroy(other);
    objRelease(&main);
    objRelease(&tool);
    objRelease(&other);
    return 0;
}

static CxUiRect frameOf(CxUiWindow* w)
{
    CxUiPlacement pl;
    cxuiWindowPlacement(w, &pl);
    return pl.normal;
}

// A window goes beside its owner, centered on it top to bottom, or on the other side where the
// screen has no room, and stays on the screen; centered on another window, or on the screen with
// nothing to go by. Asked for before realization, the move waits for the window's size; on a
// realized window it takes the size the content needs now. A later position replaces it, and a
// popup moved beforehand isn't centered again.
UI_TEST(windownear)
{
    // The null backend's screen: 1920x1080, its work area 1040 high
    CxUiWindow* main = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 800, 600 }, .sizing = CXUI_SIZE_Start });
    cxuiWindowSetPosition(main, 100, 200);
    cxuiWindowShow(main, CXUI_SHOW_Normal);
    CxUiBox* box     = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V });
    CxUiWindow* tool = cxuiWindowCreate(NULL);
    cxuiWidgetSetMinSize(box, cxuiLen(300), cxuiLen(200));
    cxuiContainerAppend(tool, box);
    cxuiWindowSetOwner(tool, main);

    if (cxuiWindowMoveNear(tool, main, 9) || cxuiWindowMoveNear(tool, tool, CXUI_NEAR_Center))
        TEST_FAIL(1, _SL("a move by nothing sensible was taken"), stvNone);
    if (!cxuiWindowMoveNear(tool, CxUiWindowNone, CXUI_NEAR_Right) || !cxuiWindowRealize(tool))
        TEST_FAIL(1, _SL("a move before realization wasn't taken"), stvNone);
    CxUiRect r = frameOf(tool);
    if (r.w != 300 || r.h != 200 || r.x != 900 || r.y != 200 + (600 - r.h) / 2)
        TEST_FAIL(1, _SL("beside its owner: ${int},${int} ${int}x${int} (want 900,${int} 300x200)"), stvar(int32, r.x),
                  stvar(int32, r.y), stvar(int32, r.w), stvar(int32, r.h), stvar(int32, 200 + (600 - r.h) / 2));

    // Grown and moved at once: the new size counts
    cxuiWidgetSetMinSize(box, cxuiLen(300), cxuiLen(400));
    cxuiWindowMoveNear(tool, CxUiWindowNone, CXUI_NEAR_Left);
    r = frameOf(tool);
    if (r.h != 400 || r.x != 900 || r.y != 300)
        TEST_FAIL(1, _SL("no room on the left: ${int},${int} ${int} high (want 900,300 400 high)"), stvar(int32, r.x),
                  stvar(int32, r.y), stvar(int32, r.h));

    cxuiWindowSetPosition(main, 1500, 900);
    cxuiWindowMoveNear(tool, main, CXUI_NEAR_Right);
    r = frameOf(tool);
    if (r.x != 1200 || r.y != 1040 - 400)
        TEST_FAIL(1, _SL("no room on the right, nor below: ${int},${int} (want 1200,640)"), stvar(int32, r.x),
                  stvar(int32, r.y));

    cxuiWindowSetSize(main, 1700, 600);
    cxuiWindowSetPosition(main, 150, 200);
    cxuiWindowMoveNear(tool, main, CXUI_NEAR_Right);
    if (frameOf(tool).x != 0)
        TEST_FAIL(1, _SL("room on neither side, the left with more: at ${int} (want 0)"), stvar(int32, frameOf(tool).x));
    cxuiWindowSetPosition(main, 50, 200);
    cxuiWindowMoveNear(tool, main, CXUI_NEAR_Left);
    if (frameOf(tool).x != 1620)
        TEST_FAIL(1, _SL("room on neither side, the right with more: at ${int} (want 1620)"), stvar(int32, frameOf(tool).x));
    cxuiWindowSetSize(main, 800, 600);
    cxuiWindowSetPosition(main, 1000, 200);
    cxuiWindowSetOwner(tool, CxUiWindowNone);
    if (cxuiWindowMoveNear(tool, CxUiWindowNone, CXUI_NEAR_Right))
        TEST_FAIL(1, _SL("moved beside nothing"), stvNone);
    cxuiWindowMoveNear(tool, main, CXUI_NEAR_Center);
    r = frameOf(tool);
    if (r.x != 1250 || r.y != 300)
        TEST_FAIL(1, _SL("centered on a window: ${int},${int} (want 1250,300)"), stvar(int32, r.x), stvar(int32, r.y));
    cxuiWindowMoveNear(tool, CxUiWindowNone, CXUI_NEAR_Center);
    r = frameOf(tool);
    if (r.x != 810 || r.y != 320)
        TEST_FAIL(1, _SL("centered on the screen: ${int},${int} (want 810,320)"), stvar(int32, r.x), stvar(int32, r.y));

    CxUiWindow* late = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200, 100 }, .sizing = CXUI_SIZE_Start });
    cxuiWindowMoveNear(late, main, CXUI_NEAR_Center);
    cxuiWindowSetPosition(late, 5, 6);
    cxuiWindowRealize(late);
    r = frameOf(late);
    if (r.x != 5 || r.y != 6)
        TEST_FAIL(1, _SL("a position after the move: ${int},${int} (want 5,6)"), stvar(int32, r.x), stvar(int32, r.y));

    // A placement and a move: the later one counts.
    CxUiWindow* placed[2];
    CxUiPlacement at = { .normal = { 30, 40, 200, 100 }, .valid = CXUI_PLACE_Position };
    for (int32 i = 0; i < 2; i++) {
        placed[i] = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200, 100 }, .sizing = CXUI_SIZE_Start });
        if (i == 1)
            cxuiWindowMoveNear(placed[i], main, CXUI_NEAR_Center);
        cxuiWindowSetPlacement(placed[i], &at);
        CxUiPlacement read = { 0 };
        if (i == 0) {
            cxuiWindowMoveNear(placed[i], main, CXUI_NEAR_Center);
            // The position isn't where it will be any more.
            if (cxuiWindowPlacement(placed[i], &read) || read.valid)
                TEST_FAIL(1, _SL("moved before realization, the placement still has ${uint}"), stvar(uint32, read.valid));
        }
        cxuiWindowRealize(placed[i]);
    }
    CxUiRect r0 = frameOf(placed[0]), r1 = frameOf(placed[1]);
    if (r0.x != 1300 || r0.y != 450 || r1.x != 30 || r1.y != 40)
        TEST_FAIL(1, _SL("moved after a placement: at ${int},${int} (want 1300,450); placed after a move: at "
                         "${int},${int} (want 30,40)"),
                  stvar(int32, r0.x), stvar(int32, r0.y), stvar(int32, r1.x), stvar(int32, r1.y));
    for (int32 i = 0; i < 2; i++) {
        cxuiDestroy(placed[i]);
        objRelease(&placed[i]);
    }

    CxUiWindow* popup = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200, 100 }, .sizing = CXUI_SIZE_Start });
    cxuiWindowMoveNear(popup, main, CXUI_NEAR_Left);
    CxUiTimer* t = UI_AFTER(5, timerClose, stvar(object, popup));
    cxuiWindowPopup(popup, main);
    objRelease(&t);
    r = frameOf(popup);
    if (r.x != 800 || r.y != 450)
        TEST_FAIL(1, _SL("a popup moved before: at ${int},${int} (want 800,450)"), stvar(int32, r.x), stvar(int32, r.y));
    cxuiDestroy(popup);
    objRelease(&popup);
    popup = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200, 100 }, .sizing = CXUI_SIZE_Start });
    t     = UI_AFTER(5, timerClose, stvar(object, popup));
    cxuiWindowPopup(popup, main);
    objRelease(&t);
    r = frameOf(popup);
    if (r.x != 1300 || r.y != 450)
        TEST_FAIL(1, _SL("a popup: at ${int},${int} (want 1300,450, centered on its parent)"), stvar(int32, r.x),
                  stvar(int32, r.y));

    cxuiDestroy(popup);
    cxuiDestroy(late);
    cxuiDestroy(tool);
    cxuiDestroy(main);
    objRelease(&popup);
    objRelease(&late);
    objRelease(&tool);
    objRelease(&main);
    objRelease(&box);
    return 0;
}

// cxui's widgets have properties by name, reaching the same state as their typed functions,
// including those of the classes they extend; read-only ones refuse a value.
UI_TEST(props)
{
    CxUiWindow* w      = cxuiWindowCreate(NULL);
    CxUiCheck* check   = cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"&Wrap" });
    static const strref items[] = { _S"one", _S"two", _S"three", NULL };
    CxUiList* list     = cxuiListCreate(&(CxUiListDesc){ .items = items });
    CxUiPalette* pal   = cxuiPaletteDark();
    stvar v            = { 0 };

    bool set = objPropSet(w, _SL("title"), stvar(strref, _SL("Props"))) &&
               objPropSet(check, _SL("value"), stvar(bool, true)) &&
               objPropSet(check, _SL("enabled"), stvar(bool, false)) &&
               objPropSet(list, _SL("value"), stvar(int64, 2)) &&
               objPropSet(list, _SL("palette"), stvar(object, pal)) &&
               objPropSet(check, _SL("tip"), stvar(strref, _SL("Wraps lines")));
    if (!set || !strEq(cxuiWindowTitle(w), _S"Props") || !cxuiCheckValue(check) || cxuiWidgetEnabled(check) ||
        cxuiListValue(list) != 2 || cxuiWidgetPalette(list) != pal || !strEq(cxuiWidgetTip(check), _S"Wraps lines"))
        TEST_FAIL(1, _SL("set by name ${int}: title ${string}, checked ${int}, enabled ${int}, list at ${int}"),
                  stvar(int32, set), stvar(strref, cxuiWindowTitle(w)), stvar(int32, cxuiCheckValue(check)),
                  stvar(int32, cxuiWidgetEnabled(check)), stvar(int32, cxuiListValue(list)));

    cxuiCheckSetTitle(check, _S"&Fold");
    bool got = objPropGet(check, _SL("title"), &v) && strEq(v.data.st_string, _S"&Fold");
    stvarDestroy(&v);
    got = got && objPropGet(check, _SL("state"), &v) && v.data.st_uint32 == CXUI_CHECK_On;
    stvarDestroy(&v);
    got = got && objPropGet(list, _SL("count"), &v) && v.data.st_int32 == 3;
    stvarDestroy(&v);
    if (!got)
        TEST_FAIL(1, _SL("a property read didn't match the typed functions"), stvNone);
    if (objPropSet(list, _SL("count"), stvar(int32, 1)) || objPropSet(check, _SL("nonsense"), stvar(int32, 1)))
        TEST_FAIL(1, _SL("a read-only or missing property took a value"), stvNone);

    cxuiDestroy(w);
    objRelease(&check);
    objRelease(&list);
    objRelease(&w);
    return 0;
}

static CxUiResult answerWhenOpen(stvlist* cvars, CxUiTimerEvent* ev)
{
    if (cxuiTestAnswerMessageBox(stvlAt(cvars, 0, int32)))
        cxuiTimerStop(ev->timer);
    return CXUI_Default;
}

static bool uiStartAnswering(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiTimer** out = stvlAtPtr(cvars, 0);
    *out            = cxuiTimerCreate(timeMS(5), 0, answerWhenOpen, stvar(int32, stvlAt(cvars, 1, int32)));
    return cxuiTimerStart(*out);
}

// A message box from another thread waits there for the answer; with cxui stopped it returns 0.
static int test_cxui_msgboxthread(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    CxUiTimer* t = NULL;
    cxuiCallSync(closureCreate(uiStartAnswering, stvar(ptr, &t), stvar(int32, 2)));
    int32 r = cxuiMessageBox(&(CxUiMessageBoxDesc){ .text = _S"From another thread", .buttons = CXUI_MB_YesNo });
    cxuiShutdown();
    objRelease(&t);
    int32 stopped = cxuiMessageBox(&(CxUiMessageBoxDesc){ .text = _S"Nobody home" });

    if (r != 2 || stopped != 0)
        TEST_FAIL(1, _SL("box from another thread returned ${int} (want 2), with cxui stopped ${int}"), stvar(int32, r),
                  stvar(int32, stopped));
    return 0;
}

typedef struct ShutdownModal {
    atomic(bool) boxOpen;
    atomic(bool) returned;
    int32 popupResult;
    int32 boxResult;
} ShutdownModal;

static CxUiResult boxForShutdown(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    ShutdownModal* sm = stvlAtPtr(cvars, 0);
    atomicStore(bool, &sm->boxOpen, true, Release);
    sm->boxResult = cxuiMessageBox(&(CxUiMessageBoxDesc){ .text = _S"Shutting down", .buttons = CXUI_MB_OkCancel });
    return CXUI_Default;
}

static bool uiPopupForShutdown(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    ShutdownModal* sm = stvlAtPtr(cvars, 0);
    CxUiWindow* popup = cxuiWindowCreate(NULL);
    CxUiTimer* t      = UI_AFTER(5, boxForShutdown, stvar(ptr, sm));
    sm->popupResult   = cxuiWindowPopup(popup, CxUiWindowNone);
    objRelease(&t);
    objRelease(&popup);
    atomicStore(bool, &sm->returned, true, Release);
    return true;
}

// Shutting down while a popup and a message box inside it are open unwinds both, with 0.
static int test_cxui_modalshutdown(void)
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    ShutdownModal sm = { .popupResult = -1, .boxResult = -1 };
    cxuiPost(closureCreate(uiPopupForShutdown, stvar(ptr, &sm)));
    int64 end = clockTimer() + timeS(10);
    while (!atomicLoad(bool, &sm.boxOpen, Acquire) && clockTimer() < end)
        osSleep(timeMS(1));
    bool opened = atomicLoad(bool, &sm.boxOpen, Acquire);
    cxuiShutdown();

    if (!opened || !atomicLoad(bool, &sm.returned, Acquire) || sm.popupResult != 0 || sm.boxResult != 0)
        TEST_FAIL(1, _SL("box opened ${int}, popup returned ${int} with ${int}, box ${int}"), stvar(int32, opened),
                  stvar(int32, atomicLoad(bool, &sm.returned, Acquire)), stvar(int32, sm.popupResult),
                  stvar(int32, sm.boxResult));
    if (cxuiTestPeersAlive() != 0 || cxuiTestPeersOffThread() != 0)
        TEST_FAIL(1, _SL("${int} peers alive, ${int} destroyed off the UI thread"), stvar(int32, cxuiTestPeersAlive()),
                  stvar(int32, cxuiTestPeersOffThread()));
    return 0;
}

// ---- canvases ----
//
// The null backend paints from the UI queue, as a platform paints at its next chance, so these
// tests run cxuiFlush() wherever a real loop would get around to painting.

typedef struct CanvasLog {
    int32 seq;   // events seen, of every kind
    int32 paints;
    CxUiRect dirty;
    CxUiSize drawSize;
    float32 drawScale;
    int32 resizes;
    int32 resizeSeq;
    CxUiSize resized;
    int32 dpis;
    int32 dpiSeq;
    float32 scale, oldScale;
    void (*draw)(CxUiDraw* d);
    bool destroyInPaint;
    CxUiWindow* resizeInPaint;
} CanvasLog;

static CanvasLog cvlog;

static CxUiResult logPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    unused_noeval(cvars);
    cvlog.seq++;
    cvlog.paints++;
    cvlog.dirty     = ev->dirty;
    cvlog.drawSize  = cxuiDrawSize(ev->draw);
    cvlog.drawScale = cxuiDrawScale(ev->draw);
    if (cvlog.draw)
        cvlog.draw(ev->draw);
    if (cvlog.destroyInPaint)
        cxuiDestroy(ev->base.current);
    if (cvlog.resizeInPaint)
        cxuiWindowSetSize(cvlog.resizeInPaint, 50, 50);
    return CXUI_Handled;
}

static CxUiResult logResize(stvlist* cvars, CxUiResizeEvent* ev)
{
    unused_noeval(cvars);
    cvlog.resizeSeq = ++cvlog.seq;
    cvlog.resizes++;
    cvlog.resized = ev->size;
    return CXUI_Default;
}

static CxUiResult logDpi(stvlist* cvars, CxUiDpiChangedEvent* ev)
{
    unused_noeval(cvars);
    cvlog.dpiSeq = ++cvlog.seq;
    cvlog.dpis++;
    cvlog.scale    = ev->scale;
    cvlog.oldScale = ev->oldScale;
    return CXUI_Default;
}

// A canvas with the logging handlers, as the content of a new window of the given size.
static CxUiCanvas* canvasInWindow(CxUiWindow** win, int32 w, int32 h, const CxUiCanvasDesc* desc)
{
    memset(&cvlog, 0, sizeof(cvlog));
    *win           = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { w, h } });
    CxUiCanvas* cv = cxuiCanvasCreate(desc);
    cxuiOnPaint(cv, logPaint, stvNone);
    cxuiOnResize(cv, logResize, stvNone);
    cxuiOnDpiChanged(*win, logDpi, stvNone);
    cxuiContainerAppend(*win, cv);
    return cv;
}

static bool rectIs(CxUiRect r, int32 x, int32 y, int32 w, int32 h)
{
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

#define RECT_ARGS(r) stvar(int32, (r).x), stvar(int32, (r).y), stvar(int32, (r).w), stvar(int32, (r).h)

// Realizing paints all of the canvas once; invalidations coalesce into one paint of their
// bounding rectangle, however many there are; resizing repaints all of it.
UI_TEST(canvasbasic)
{
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 200, 100, NULL);
    CxUiSize sz;
    if (CxUiWidget(cv)->peer || cxuiCanvasSize(cv, &sz))
        TEST_FAIL(1, _SL("the canvas was realized before its window"), stvNone);

    cxuiWindowShow(w, CXUI_SHOW_Normal);
    if (!CxUiWidget(cv)->peer || cvlog.resizes != 1 || cvlog.resized.w != 200 || cvlog.resized.h != 100 ||
        cvlog.paints != 0)
        TEST_FAIL(1, _SL("showing gave ${int} resizes to ${int}x${int} and ${int} paints"), stvar(int32, cvlog.resizes),
                  stvar(int32, cvlog.resized.w), stvar(int32, cvlog.resized.h), stvar(int32, cvlog.paints));

    cxuiFlush();
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (cvlog.paints != 1 || !rectIs(cvlog.dirty, 0, 0, 200, 100) || !rectIs(info.bounds, 0, 0, 200, 100) ||
        cvlog.drawSize.w != 200 || cvlog.drawScale != 1.0f)
        TEST_FAIL(1, _SL("first paint: ${int} paints of ${int},${int} ${int}x${int}"), stvar(int32, cvlog.paints),
                  RECT_ARGS(cvlog.dirty));

    cxuiCanvasInvalidate(cv, 10, 10, 5, 5);
    cxuiCanvasInvalidate(cv, 50, 20, 10, 10);
    cxuiCanvasInvalidate(cv, 12, 12, 2, 2);
    cxuiCanvasInvalidate(cv, 500, 500, 2, 2);
    cxuiFlush();
    if (cvlog.paints != 2 || !rectIs(cvlog.dirty, 10, 10, 50, 20))
        TEST_FAIL(1, _SL("three invalidations: ${int} paints of ${int},${int} ${int}x${int}"), stvar(int32, cvlog.paints),
                  RECT_ARGS(cvlog.dirty));

    // More rectangles than the region keeps
    for (int32 i = 0; i < 12; i++)
        cxuiCanvasInvalidate(cv, i * 10, 0, 1, 1);
    cxuiFlush();
    if (cvlog.paints != 3 || !rectIs(cvlog.dirty, 0, 0, 111, 1))
        TEST_FAIL(1, _SL("twelve invalidations: ${int} paints of ${int},${int} ${int}x${int}"), stvar(int32, cvlog.paints),
                  RECT_ARGS(cvlog.dirty));

    cxuiCanvasInvalidate(cv, 500, 500, 2, 2);
    cxuiFlush();
    if (cvlog.paints != 3)
        TEST_FAIL(1, _SL("painted with nothing to paint"), stvNone);

    cxuiTestResize(w, 300, 150);
    cxuiFlush();
    cxuiTestCanvasInfo(cv, &info);
    if (cvlog.resizes != 2 || cvlog.resized.w != 300 || cvlog.paints != 4 || !rectIs(cvlog.dirty, 0, 0, 300, 150) ||
        !rectIs(info.bounds, 0, 0, 300, 150) || !cxuiCanvasSize(cv, &sz) || sz.w != 300 || sz.h != 150)
        TEST_FAIL(1, _SL("after a resize: ${int} resizes, ${int} paints of ${int},${int} ${int}x${int}"),
                  stvar(int32, cvlog.resizes), stvar(int32, cvlog.paints), RECT_ARGS(cvlog.dirty));

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

static void drawScript(CxUiDraw* d)
{
    CxUiColor red = cxuiHex(0xFF0000);
    cxuiDrawFill(d, -5, -5, 20, 20, red);
    cxuiDrawFill(d, 0, 0, 50, 50, cxuiRGBA(1, 2, 3, 0));
    cxuiDrawFrame(d, 10, 10, 30, 20, 2, red);
    cxuiDrawFrame(d, 50, 50, 4, 4, 2, red);
    cxuiDrawLine(d, 9, 60, 0, 60, 1, red);
    cxuiDrawLine(d, 70, 0, 70, 9, 2, red);
    cxuiDrawLine(d, 70, 9, 70, 0, 3, red);
    cxuiDrawLine(d, 0, 0, 50, 40, 1, red);
    cxuiDrawLine(d, 200, 200, 300, 250, 1, red);
    cxuiDrawClipPush(d, (CxUiRect){ 20, 20, 10, 10 });
    cxuiDrawFill(d, 0, 0, 100, 100, red);
    cxuiDrawClipPush(d, (CxUiRect){ 25, 25, 100, 100 });
    cxuiDrawClipPop(d);
    cxuiDrawClipPop(d);
    cxuiDrawClipPop(d);
    // Deeper than the clip stack's first allocation, and left unbalanced
    for (int32 i = 0; i < 20; i++)
        cxuiDrawClipPush(d, (CxUiRect){ i, i, 100, 100 });
    cxuiDrawFill(d, 0, 0, 100, 100, red);
}

typedef struct ExpectOp {
    uint32 kind;
    int32 x, y, w, h;
} ExpectOp;

static int checkOps(CxUiCanvas* cv, const ExpectOp* want, int32 nwant)
{
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (info.nops != nwant)
        TEST_FAIL(1, _SL("recorded ${int} operations, expected ${int}"), stvar(int32, info.nops), stvar(int32, nwant));
    for (int32 i = 0; i < nwant; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        CxUiRect r               = op->r;
        if (op->kind == CXUI_TESTOP_Line)
            r = (CxUiRect){ op->a.x, op->a.y, op->b.x, op->b.y };
        if (op->kind != want[i].kind || !rectIs(r, want[i].x, want[i].y, want[i].w, want[i].h))
            TEST_FAIL(1, _SL("operation ${int} is kind ${int} at ${int},${int} ${int}x${int}; expected kind ${int} at "
                             "${int},${int} ${int}x${int}"),
                      stvar(int32, i), stvar(uint32, op->kind), RECT_ARGS(r), stvar(uint32, want[i].kind),
                      stvar(int32, want[i].x), stvar(int32, want[i].y), stvar(int32, want[i].w), stvar(int32, want[i].h));
    }
    return 0;
}

// What the front end sends the backend: fills clipped, frames and straight lines as exact fills,
// angled lines passed on, and the clip stack.
UI_TEST(canvasdraw)
{
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, NULL);
    cvlog.draw     = drawScript;
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiFlush();

    enum { F = CXUI_TESTOP_Fill, L = CXUI_TESTOP_Line, C = CXUI_TESTOP_Clip };
    static const ExpectOp want[] = {
        { F, 0, 0, 15, 15 },                                                           // clipped to the dirty area
        { F, 10, 10, 30, 2 }, { F, 10, 28, 30, 2 }, { F, 10, 12, 2, 16 }, { F, 38, 12, 2, 16 },   // frame
        { F, 50, 50, 4, 4 },    // frame too thick for its size
        { F, 0, 60, 10, 1 },    // horizontal, ends included
        { F, 70, 0, 2, 10 },    // even width: extra pixel to the right
        { F, 69, 0, 3, 10 },    // odd width: centered
        { L, 0, 0, 50, 40 },
        { C, 20, 20, 10, 10 }, { F, 20, 20, 10, 10 }, { C, 25, 25, 5, 5 }, { C, 20, 20, 10, 10 }, { C, 0, 0, 100, 100 },
        { C, 0, 0, 100, 100 }, { C, 1, 1, 99, 99 }, { C, 2, 2, 98, 98 }, { C, 3, 3, 97, 97 }, { C, 4, 4, 96, 96 },
        { C, 5, 5, 95, 95 }, { C, 6, 6, 94, 94 }, { C, 7, 7, 93, 93 }, { C, 8, 8, 92, 92 }, { C, 9, 9, 91, 91 },
        { C, 10, 10, 90, 90 }, { C, 11, 11, 89, 89 }, { C, 12, 12, 88, 88 }, { C, 13, 13, 87, 87 },
        { C, 14, 14, 86, 86 }, { C, 15, 15, 85, 85 }, { C, 16, 16, 84, 84 }, { C, 17, 17, 83, 83 },
        { C, 18, 18, 82, 82 }, { C, 19, 19, 81, 81 }, { F, 19, 19, 81, 81 },
    };
    if (checkOps(cv, want, sizeof(want) / sizeof(want[0])))
        return 1;

    // The next paint starts clipped to its own dirty area, whatever the last one left pushed.
    cxuiCanvasInvalidate(cv, 40, 40, 10, 10);
    cvlog.draw = NULL;
    cxuiFlush();
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (!rectIs(info.lastDirty, 40, 40, 10, 10) || info.nops != 0)
        TEST_FAIL(1, _SL("second paint: ${int} operations in ${int},${int} ${int}x${int}"), stvar(int32, info.nops),
                  RECT_ARGS(info.lastDirty));

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

static void drawDip(CxUiDraw* d)
{
    CxUiColor c = cxuiHex(0x123456);
    cxuiDrawFill(d, 1, 1, 1, 1, c);
    cxuiDrawFill(d, 0, 0, 10, 10, c);
    cxuiDrawLine(d, 0, 10, 10, 10, 1, c);
}

// In DIP mode, cxui scales drawing, sizes and areas, rounding edges to whole pixels.
UI_TEST(canvasdip)
{
    cxuiTestSetSystemScale(1.5f);
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, &(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    cvlog.draw     = drawDip;
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiFlush();

    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    CxUiSize sz;
    if (!rectIs(info.bounds, 0, 0, 150, 150) || !rectIs(cvlog.dirty, 0, 0, 100, 100) || cvlog.drawSize.w != 100 ||
        cvlog.resized.w != 100 || !cxuiCanvasSize(cv, &sz) || sz.w != 100 || cvlog.drawScale != 1.5f)
        TEST_FAIL(1, _SL("bounds ${int}x${int}, dirty ${int}x${int}, draw size ${int}, resize ${int}"),
                  stvar(int32, info.bounds.w), stvar(int32, info.bounds.h), stvar(int32, cvlog.dirty.w),
                  stvar(int32, cvlog.dirty.h), stvar(int32, cvlog.drawSize.w), stvar(int32, cvlog.resized.w));

    enum { F = CXUI_TESTOP_Fill };
    static const ExpectOp want[] = { { F, 2, 2, 1, 1 }, { F, 0, 0, 15, 15 }, { F, 0, 15, 16, 2 } };
    if (checkOps(cv, want, 3))
        return 1;

    if (cxuiWidgetScale(cv) != 1.5f || cxuiPx(cv, 10) != 15 || cxuiDip(cv, 15) != 10)
        TEST_FAIL(1, _SL("scale ${float}: 10 DIPs is ${int} px, 15 px is ${int} DIPs"),
                  stvar(float64, cxuiWidgetScale(cv)), stvar(int32, cxuiPx(cv, 10)), stvar(int32, cxuiDip(cv, 15)));

    // Invalidated outward to whole pixels, and reported back as the DIPs that cover them
    cvlog.draw = NULL;
    cxuiCanvasInvalidate(cv, 1, 1, 1, 1);
    cxuiFlush();
    cxuiTestCanvasInfo(cv, &info);
    if (!rectIs(info.lastDirty, 1, 1, 2, 2) || !rectIs(cvlog.dirty, 0, 0, 2, 2))
        TEST_FAIL(1, _SL("a 1-DIP invalidation painted ${int},${int} ${int}x${int} px"), RECT_ARGS(info.lastDirty));

    cxuiCanvasSetUnits(cv, CXUI_Units_Pixels);
    cxuiFlush();
    if (cvlog.resized.w != 150 || !rectIs(cvlog.dirty, 0, 0, 150, 150))
        TEST_FAIL(1, _SL("switching to pixels: resized to ${int}, painted ${int},${int} ${int}x${int}"),
                  stvar(int32, cvlog.resized.w), RECT_ARGS(cvlog.dirty));

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// Moving to a monitor with another scale resizes the canvas, then tells the window, then repaints.
UI_TEST(canvasdpi)
{
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 200, 100, NULL);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiFlush();
    int32 paints = cvlog.paints;

    cxuiTestDpi(w, 2.0f);
    if (cvlog.dpis != 1 || cvlog.scale != 2.0f || cvlog.oldScale != 1.0f || cvlog.resized.w != 400 ||
        cvlog.resized.h != 200 || cvlog.resizeSeq > cvlog.dpiSeq || cxuiWidgetScale(cv) != 2.0f)
        TEST_FAIL(1, _SL("${int} scale events, ${float} from ${float}; canvas resized to ${int}x${int}"),
                  stvar(int32, cvlog.dpis), stvar(float64, cvlog.scale), stvar(float64, cvlog.oldScale),
                  stvar(int32, cvlog.resized.w), stvar(int32, cvlog.resized.h));
    CxUiSize sz;
    cxuiWindowSize(w, &sz);
    if (sz.w != 200 || sz.h != 100)
        TEST_FAIL(1, _SL("the window's size in DIPs changed to ${int}x${int}"), stvar(int32, sz.w), stvar(int32, sz.h));

    cxuiFlush();
    if (cvlog.paints != paints + 1 || !rectIs(cvlog.dirty, 0, 0, 400, 200))
        TEST_FAIL(1, _SL("${int} paints after the change, of ${int},${int} ${int}x${int}"),
                  stvar(int32, cvlog.paints - paints), RECT_ARGS(cvlog.dirty));

    cxuiTestDpi(w, 2.0f);
    if (cvlog.dpis != 1)
        TEST_FAIL(1, _SL("an unchanged scale sent a scale event"), stvNone);

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// cxuiCanvasPaintNow() paints before it returns, and leaves nothing for later.
UI_TEST(canvaspaintnow)
{
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, NULL);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiCanvasPaintNow(cv);
    if (cvlog.paints != 1)
        TEST_FAIL(1, _SL("${int} paints from paint-now"), stvar(int32, cvlog.paints));
    cxuiFlush();
    cxuiCanvasPaintNow(cv);
    if (cvlog.paints != 1)
        TEST_FAIL(1, _SL("${int} paints with nothing to paint"), stvar(int32, cvlog.paints));

    cxuiCanvasInvalidate(cv, 1, 2, 3, 4);
    cxuiCanvasPaintNow(cv);
    cxuiFlush();
    if (cvlog.paints != 2 || !rectIs(cvlog.dirty, 1, 2, 3, 4))
        TEST_FAIL(1, _SL("${int} paints, of ${int},${int} ${int}x${int}"), stvar(int32, cvlog.paints),
                  RECT_ARGS(cvlog.dirty));

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// A paint handler can resize its window or destroy its canvas; a canvas moved out of its window and
// back is painted afresh.
UI_TEST(canvaslifetime)
{
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, NULL);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cvlog.resizeInPaint = w;
    cxuiFlush();
    cvlog.resizeInPaint = NULL;
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    // The paint the resize asked for comes in the same flush.
    if (cvlog.paints != 2 || !rectIs(info.bounds, 0, 0, 50, 50) || cvlog.resized.w != 50 ||
        !rectIs(cvlog.dirty, 0, 0, 50, 50))
        TEST_FAIL(1, _SL("resized in paint: ${int} paints, resized to ${int}, bounds ${int},${int} ${int}x${int}"),
                  stvar(int32, cvlog.paints), stvar(int32, cvlog.resized.w), RECT_ARGS(info.bounds));

    cxuiContainerDetach(w, cv);   // the test still holds its own reference
    if (CxUiWidget(cv)->peer)
        TEST_FAIL(1, _SL("a detached canvas kept its peer"), stvNone);
    cxuiContainerAppend(w, cv);
    cxuiFlush();
    if (cvlog.paints != 3 || !rectIs(cvlog.dirty, 0, 0, 50, 50))
        TEST_FAIL(1, _SL("re-added: ${int} paints of ${int},${int} ${int}x${int}"), stvar(int32, cvlog.paints),
                  RECT_ARGS(cvlog.dirty));

    int32 alive          = cxuiTestPeersAlive();
    cvlog.destroyInPaint = true;
    cxuiCanvasInvalidateAll(cv);
    cxuiFlush();
    if (!(CxUiWidget(cv)->state & CXUI_WS_Dead) || cxuiContainerChildCount(w) != 0 ||
        cxuiTestPeersAlive() != alive - 1)
        TEST_FAIL(1, _SL("destroyed in paint: ${int} peers of ${int} left"), stvar(int32, cxuiTestPeersAlive()),
                  stvar(int32, alive));
    cxuiCanvasInvalidateAll(cv);
    cxuiCanvasPaintNow(cv);

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

static CxUiFont* textFont;
static CxUiFont* textFontU;
static int32 textRet[4];

static void drawTextScript(CxUiDraw* d)
{
    CxUiColor red = cxuiHex(0xFF0000);
    textRet[0]    = cxuiDrawText(d, textFont, 5, 6, _S"abc", red);
    textRet[1]    = cxuiDrawText(d, textFontU, 5, 30, _S"abc", red);
    textRet[2]    = cxuiDrawText(d, textFont, 5, 200, _S"hidden", red);   // below the canvas
    textRet[3]    = cxuiDrawTextCells(d, textFont, 0, 50, _S"a\xE2\x82\xAC" "b", 10, red);
    cxuiDrawTextBox(d, textFont, (CxUiRect){ 10, 70, 100, 40 }, _S"a\nbcd", CXUI_TEXT_Center | CXUI_TEXT_Middle,
                    red);
}

// Fonts on the null backend measure the same everywhere, and text reaches the backend as runs,
// cell runs and aligned lines, with underlines drawn by cxui.
UI_TEST(canvastext)
{
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 120, 120, NULL);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    textFont  = cxuiFontCreateFor(cv, &(CxUiFontDesc){ .faces = _S"Consolas;monospace", .size = 10 });
    textFontU = cxuiFontCreateFor(cv, &(CxUiFontDesc){ .size = 10, .flags = CXUI_FONT_Underline });

    CxUiFontMetrics m;
    cxuiFontMetrics(textFont, &m);
    if (m.cellW != 8 || m.cellH != 16 || m.height != 16 || m.ascent != 12 || m.descent != 4 || m.avgCharW != 7)
        TEST_FAIL(1, _SL("10-point metrics: cell ${int}x${int}, ascent ${int}, average ${int}"), stvar(int32, m.cellW),
                  stvar(int32, m.cellH), stvar(int32, m.ascent), stvar(int32, m.avgCharW));
    CxUiSize ms  = cxuiFontMeasure(textFont, _S"ab\ncdef");
    CxUiSize me  = cxuiFontMeasure(textFont, NULL);
    int32 accent = cxuiFontAdvance(textFont, _S"h\xC3\xA9llo");
    if (ms.w != 32 || ms.h != 32 || me.w != 0 || me.h != 16 || accent != 40)
        TEST_FAIL(1, _SL("measured two lines as ${int}x${int}, nothing as ${int}x${int}, 5 codepoints as ${int}"),
                  stvar(int32, ms.w), stvar(int32, ms.h), stvar(int32, me.w), stvar(int32, me.h), stvar(int32, accent));

    cvlog.draw = drawTextScript;
    cxuiFlush();
    if (textRet[0] != 24 || textRet[1] != 24 || textRet[2] != 48 || textRet[3] != 3)
        TEST_FAIL(1, _SL("returned ${int}, ${int}, ${int} and ${int} cells"), stvar(int32, textRet[0]),
                  stvar(int32, textRet[1]), stvar(int32, textRet[2]), stvar(int32, textRet[3]));

    enum { F = CXUI_TESTOP_Fill, T = CXUI_TESTOP_Text, C = CXUI_TESTOP_Clip, X = CXUI_TESTOP_Cells };
    static const ExpectOp want[] = {
        { T, 5, 6, 24, 16 },
        { T, 5, 30, 24, 16 }, { F, 5, 43, 24, 1 },   // underline at the baseline
        { X, 0, 50, 10, 0 },
        { C, 10, 70, 100, 40 }, { T, 56, 74, 8, 16 }, { T, 48, 90, 24, 16 }, { C, 0, 0, 120, 120 },
    };
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    for (int32 i = 0; i < info.nops && i < (int32)(sizeof(want) / sizeof(want[0])); i++) {
        CxUiTestDrawOp* op = (CxUiTestDrawOp*)&info.ops[i];
        if (op->kind == CXUI_TESTOP_Cells)
            op->r = (CxUiRect){ op->a.x, op->a.y, op->width, 0 };   // compared as a rect below
    }
    if (checkOps(cv, want, sizeof(want) / sizeof(want[0])))
        return 1;
    if (!strEq(info.ops[0].text, _S"abc") || !strEq(info.ops[3].text, _S"a\xE2\x82\xAC" "b") ||
        !strEq(info.ops[6].text, _S"bcd") || info.ops[0].font != 13 || info.ops[0].c.bits != cxuiHex(0xFF0000).bits)
        TEST_FAIL(1, _SL("recorded text '${string}', '${string}', '${string}' in font ${int}"),
                  stvar(strref, info.ops[0].text), stvar(strref, info.ops[3].text), stvar(strref, info.ops[6].text),
                  stvar(int32, info.ops[0].font));

    // A font for twice the scale
    CxUiFont* big = cxuiFontRescale(textFont, 2.0f);
    cxuiFontMetrics(big, &m);
    if (m.cellW != 16 || m.height != 32 || cxuiFontScale(big) != 2.0f)
        TEST_FAIL(1, _SL("rescaled to 2: cell ${int}, height ${int}"), stvar(int32, m.cellW), stvar(int32, m.height));

    objRelease(&big);
    objRelease(&textFont);
    objRelease(&textFontU);
    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

static CxUiFont* dipFont;

static void drawDipText(CxUiDraw* d)
{
    textRet[0] = cxuiDrawText(d, dipFont, 5, 6, _S"abc", cxuiHex(0));
    textRet[1] = cxuiDrawTextCells(d, dipFont, 0, 20, _S"ab", 10, cxuiHex(0));
}

// In DIP mode, text is placed in DIPs and its advance comes back in DIPs; fonts stay in pixels.
UI_TEST(canvastextdip)
{
    cxuiTestSetSystemScale(2.0f);
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, &(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    dipFont = cxuiFontCreateFor(cv, &(CxUiFontDesc){ .size = 10 });
    CxUiFontMetrics m;
    cxuiFontMetrics(dipFont, &m);
    cvlog.draw = drawDipText;
    cxuiFlush();

    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (m.cellW != 16 || textRet[0] != 24 || info.nops != 2 || info.ops[0].a.x != 10 || info.ops[0].a.y != 12 ||
        info.ops[1].width != 20 || info.ops[1].a.y != 40)
        TEST_FAIL(1, _SL("cell ${int} px; advance ${int} DIPs; run at ${int},${int}; cells ${int} px wide"),
                  stvar(int32, m.cellW), stvar(int32, textRet[0]), stvar(int32, info.nops ? info.ops[0].a.x : -1),
                  stvar(int32, info.nops ? info.ops[0].a.y : -1), stvar(int32, info.nops > 1 ? info.ops[1].width : -1));

    objRelease(&dipFont);
    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

static CxUiImage* imgAtlas;
static CxUiImage* imgSub;
static CxUiImage* imgSubSub;

static void drawImages(CxUiDraw* d)
{
    cxuiDrawImage(d, imgAtlas, NULL, (CxUiRect){ 10, 10, 4, 4 });
    cxuiDrawImage(d, imgSub, NULL, (CxUiRect){ 20, 20, 8, 8 });
    cxuiDrawImage(d, imgSub, &(CxUiRect){ 1, 0, 5, 5 }, (CxUiRect){ 30, 30, 1, 2 });   // src clipped to the part
    cxuiDrawImage(d, imgSubSub, NULL, (CxUiRect){ 40, 40, 1, 1 });
    cxuiDrawImage(d, imgAtlas, NULL, (CxUiRect){ 500, 500, 4, 4 });   // off the canvas
}

// Parts of an image draw as source rectangles within the image that holds the pixels, which
// they keep alive.
UI_TEST(canvasimage)
{
    uint8 px[4 * 4 * 4] = { 0 };
    if (cxuiImageCreateRGBA(0, 4, 16, px) || cxuiImageCreateRGB(4, -1, 12, px))
        TEST_FAIL(1, _SL("an image with no area was made"), stvNone);
    imgAtlas  = cxuiImageCreateRGBA(4, 4, 16, px);
    imgSub    = cxuiImageCreateSub(imgAtlas, (CxUiRect){ 1, 1, 2, 2 });
    imgSubSub = cxuiImageCreateSub(imgSub, (CxUiRect){ 1, 0, 9, 9 });
    CxUiImage* none = cxuiImageCreateSub(imgAtlas, (CxUiRect){ 4, 0, 2, 2 });
    CxUiSize ss = cxuiImageSize(imgSub), sss = cxuiImageSize(imgSubSub);
    if (none || ss.w != 2 || ss.h != 2 || sss.w != 1 || sss.h != 2)
        TEST_FAIL(1, _SL("parts sized ${int}x${int} and ${int}x${int}; outside part made ${int}"), stvar(int32, ss.w),
                  stvar(int32, ss.h), stvar(int32, sss.w), stvar(int32, sss.h), stvar(int32, none != NULL));
    objRelease(&imgAtlas);   // the parts keep it

    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, NULL);
    cvlog.draw     = drawImages;
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    imgAtlas = objAcquire(imgSub->_root);
    cxuiFlush();

    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    static const CxUiRect wantDst[] = { { 10, 10, 4, 4 }, { 20, 20, 8, 8 }, { 30, 30, 1, 2 }, { 40, 40, 1, 1 } };
    static const CxUiRect wantSrc[] = { { 0, 0, 4, 4 }, { 1, 1, 2, 2 }, { 2, 1, 1, 2 }, { 2, 1, 1, 2 } };
    if (info.nops != 4)
        TEST_FAIL(1, _SL("${int} image operations, expected 4"), stvar(int32, info.nops));
    for (int32 i = 0; i < 4; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        CxUiRect wd = wantDst[i], ws = wantSrc[i];
        if (op->kind != CXUI_TESTOP_Image || op->b.x != 4 || op->b.y != 4 || !rectIs(op->r, wd.x, wd.y, wd.w, wd.h) ||
            !rectIs(op->src, ws.x, ws.y, ws.w, ws.h))
            TEST_FAIL(1, _SL("image ${int}: src ${int},${int} ${int}x${int} to ${int},${int} ${int}x${int}"),
                      stvar(int32, i), RECT_ARGS(op->src), RECT_ARGS(op->r));
    }

    objRelease(&imgAtlas);
    objRelease(&imgSub);
    objRelease(&imgSubSub);
    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

static CxUiFont* cellsFont;

static void drawDipCells(CxUiDraw* d)
{
    cxuiDrawTextCells(d, cellsFont, 20, 0, _S"abcd", 7, cxuiHex(0));
}

// At a fractional scale, cell n starts where x + n * cellw lands as a DIP position, so cells line
// up with lines drawn at those positions.
UI_TEST(canvascellsdip)
{
    cxuiTestSetSystemScale(1.5f);
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, &(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cellsFont  = cxuiFontCreateFor(cv, &(CxUiFontDesc){ .size = 10 });
    cvlog.draw = drawDipCells;
    cxuiFlush();

    // Edges at 20, 27, 34, 41 and 48 DIPs: 30, 41, 51, 62 and 72 px
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (info.nops != 1 || info.ops[0].a.x != 30 || info.ops[0].width != 11 || info.ops[0].b.x != 42 ||
        info.ops[0].b.y != 4)
        TEST_FAIL(1, _SL("cells at ${int}, first ${int} px, all ${int} px for ${int}"),
                  stvar(int32, info.nops ? info.ops[0].a.x : -1), stvar(int32, info.nops ? info.ops[0].width : -1),
                  stvar(int32, info.nops ? info.ops[0].b.x : -1), stvar(int32, info.nops ? info.ops[0].b.y : -1));

    objRelease(&cellsFont);
    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}


// ---- canvas input ----

typedef struct MouseRec {
    uint32 action, button, buttons, flags;
    int32 x, y;
    float32 dx, dy;
} MouseRec;

typedef struct InputLog {
    MouseRec mouse[32];
    int32 nmouse;
    int32 focusGained, focusLost;
    int32 wheels;
    float64 wheelDy;
    CxUiWidget* wheelAt;   // whose wheel handler ran
    int32 keys;
    CxUiKeyEvent lastKey;
    CxUiWidget* keyAt;
    bool destroyOnDown;
    CxUiWindow* dragOnDown;   // a Down moves this window
    int32 drags;              // moves cxuiWindowBeginDrag() started
    bool unlockOnUp;          // an Up lets go of the pointer lock
    int32 texts;
    string text;              // what text events brought, run together
    CxUiWidget* textAt;       // whose text handler ran last
    int32 winFocus[2];        // the window's focus events: lost, gained
    uint32 winFocusFlags;     // the last one's flags
    int32 winFocusAt;         // inlog.nmouse when the last one came
} InputLog;

static InputLog inlog;

static CxUiResult inMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    unused_noeval(cvars);
    if (inlog.nmouse < 32)
        inlog.mouse[inlog.nmouse] = (MouseRec){ ev->action, ev->button, ev->buttons, ev->flags,
                                                ev->x,      ev->y,      ev->dx,      ev->dy };
    inlog.nmouse++;
    if (inlog.destroyOnDown && ev->action == CXUI_MOUSE_Down)
        cxuiDestroy(ev->base.current);
    if (inlog.dragOnDown && ev->action == CXUI_MOUSE_Down && cxuiWindowBeginDrag(inlog.dragOnDown))
        inlog.drags++;
    if (inlog.unlockOnUp && ev->action == CXUI_MOUSE_Up)
        cxuiCanvasSetPointerLock(objDynCast(CxUiCanvas, ev->base.current), CXUI_POINTERLOCK_Off);
    return CXUI_Handled;
}

static CxUiResult inText(stvlist* cvars, CxUiTextEvent* ev)
{
    inlog.texts++;
    strAppend(&inlog.text, ev->text);
    inlog.textAt = ev->base.current;
    return (CxUiResult)stvlAt(cvars, 0, int32);
}

static CxUiResult inWinFocus(stvlist* cvars, CxUiFocusEvent* ev)
{
    unused_noeval(cvars);
    inlog.winFocus[ev->gained ? 1 : 0]++;
    inlog.winFocusFlags = ev->flags;
    inlog.winFocusAt    = inlog.nmouse;
    return CXUI_Default;
}

static void inlogReset(void)
{
    strDestroy(&inlog.text);
    memset(&inlog, 0, sizeof(inlog));
}

static CxUiResult inFocus(stvlist* cvars, CxUiFocusEvent* ev)
{
    unused_noeval(cvars);
    if (ev->gained)
        inlog.focusGained++;
    else
        inlog.focusLost++;
    return CXUI_Default;
}

static CxUiResult inWheel(stvlist* cvars, CxUiWheelEvent* ev)
{
    inlog.wheels++;
    inlog.wheelDy = ev->dy;
    inlog.wheelAt = ev->base.current;
    return (CxUiResult)stvlAt(cvars, 0, int32);
}

static CxUiResult inKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    inlog.keys++;
    inlog.lastKey = *ev;
    inlog.keyAt   = ev->base.current;
    return (CxUiResult)stvlAt(cvars, 0, int32);
}

static CxUiCanvas* inputCanvas(CxUiWindow** w, const CxUiCanvasDesc* desc)
{
    inlogReset();
    CxUiCanvas* cv = canvasInWindow(w, 100, 100, desc);
    cxuiOnMouse(cv, inMouse, stvNone);
    cxuiOnFocus(cv, inFocus, stvNone);
    cxuiWindowShow(*w, CXUI_SHOW_Normal);
    return cv;
}

static int checkMouse(int32 from, const MouseRec* want, int32 n)
{
    if (inlog.nmouse != from + n)
        TEST_FAIL(1, _SL("${int} mouse events, expected ${int}"), stvar(int32, inlog.nmouse - from), stvar(int32, n));
    for (int32 i = 0; i < n; i++) {
        const MouseRec* g = &inlog.mouse[from + i];
        const MouseRec* e = &want[i];
        if (g->action != e->action || g->button != e->button || g->buttons != e->buttons || g->flags != e->flags ||
            g->x != e->x || g->y != e->y)
            TEST_FAIL(1, _SL("mouse event ${int}: action ${uint} button ${uint} buttons ${uint} flags ${uint} at "
                             "${int},${int}; expected ${uint} ${uint} ${uint} ${uint} at ${int},${int}"),
                      stvar(int32, from + i), stvar(uint32, g->action), stvar(uint32, g->button), stvar(uint32, g->buttons),
                      stvar(uint32, g->flags), stvar(int32, g->x), stvar(int32, g->y), stvar(uint32, e->action),
                      stvar(uint32, e->button), stvar(uint32, e->buttons), stvar(uint32, e->flags), stvar(int32, e->x),
                      stvar(int32, e->y));
    }
    return 0;
}

enum { M_Down = CXUI_MOUSE_Down, M_Up = CXUI_MOUSE_Up, M_Dbl = CXUI_MOUSE_DoubleClick, M_Move = CXUI_MOUSE_Move,
       M_Enter = CXUI_MOUSE_Enter, M_Leave = CXUI_MOUSE_Leave };
enum { L = CXUI_BTN_Left, R = CXUI_BTN_Right, Mid = CXUI_BTN_Middle };
enum { P_Press = CXUI_TESTPTR_Press, P_Release = CXUI_TESTPTR_Release, P_Move = CXUI_TESTPTR_Move,
       P_Left = CXUI_TESTPTR_Left };

// Pressing captures the mouse and focuses the canvas; dragging outside keeps events coming with
// positions outside it and holds Leave back until the last button is released.
UI_TEST(inputcapture)
{
    CxUiWindow* w;
    CxUiCanvas* cv = inputCanvas(&w, NULL);

    cxuiTestPointer(cv, P_Move, 0, 0, 0, 10, 10);
    cxuiTestPointer(cv, P_Press, L, 0, 0, 10, 10);
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (!info.captured || !info.focused || inlog.focusGained != 1 || cxuiFocused(w) != CxUiWidget(cv))
        TEST_FAIL(1, _SL("after a press: captured ${int}, focused ${int}, ${int} focus events"),
                  stvar(int32, info.captured), stvar(int32, info.focused), stvar(int32, inlog.focusGained));
    cxuiTestPointer(cv, P_Move, 0, L, 0, 150, -20);
    cxuiTestPointer(cv, P_Left, 0, L, 0, 0, 0);   // the platform says it left; held back
    cxuiTestPointer(cv, P_Press, R, L, 0, 150, -20);
    cxuiTestPointer(cv, P_Release, L, L | R, 0, 150, -20);
    cxuiTestPointer(cv, P_Release, R, R, 0, 150, -20);
    cxuiTestCanvasInfo(cv, &info);
    if (info.captured)
        TEST_FAIL(1, _SL("still captured after the last release"), stvNone);
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 5, 5);
    cxuiTestPointer(cv, P_Left, 0, 0, 0, 0, 0);

    static const MouseRec want[] = {
        { M_Enter, 0, 0, 0, 10, 10 },  { M_Move, 0, 0, 0, 10, 10 },     { M_Down, L, L, 0, 10, 10 },
        { M_Move, 0, L, 0, 150, -20 }, { M_Down, R, L | R, 0, 150, -20 }, { M_Up, L, R, 0, 150, -20 },
        { M_Up, R, 0, 0, 150, -20 },   { M_Leave, 0, 0, 0, 150, -20 },  { M_Enter, 0, 0, 0, 5, 5 },
        { M_Move, 0, 0, 0, 5, 5 },     { M_Leave, 0, 0, 0, 0, 0 },
    };
    if (checkMouse(0, want, sizeof(want) / sizeof(want[0])))
        return 1;

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// A second press of the same button, soon and close by, is a double click; a third starts again.
UI_TEST(inputdouble)
{
    CxUiWindow* w;
    CxUiCanvas* cv = inputCanvas(&w, NULL);
    cxuiTestSetDoubleClick(timeS(10), 4);

    int32 from = 0;
    for (int32 i = 0; i < 3; i++) {
        cxuiTestPointer(cv, P_Press, L, 0, 0, 20 + i, 20);
        cxuiTestPointer(cv, P_Release, L, L, 0, 20 + i, 20);
    }
    cxuiTestPointer(cv, P_Press, L, 0, 0, 30, 20);   // too far from the last
    cxuiTestPointer(cv, P_Release, L, L, 0, 30, 20);
    cxuiTestPointer(cv, P_Press, R, 0, 0, 30, 20);   // another button
    cxuiTestPointer(cv, P_Release, R, R, 0, 30, 20);
    cxuiTestSetDoubleClick(0, 4);
    cxuiTestPointer(cv, P_Press, R, 0, 0, 30, 20);   // too late
    cxuiTestPointer(cv, P_Release, R, R, 0, 30, 20);

    static const MouseRec want[] = {
        { M_Enter, 0, 0, 0, 20, 20 }, { M_Down, L, L, 0, 20, 20 }, { M_Up, L, 0, 0, 20, 20 },
        { M_Dbl, L, L, 0, 21, 20 },   { M_Up, L, 0, 0, 21, 20 },   { M_Down, L, L, 0, 22, 20 },
        { M_Up, L, 0, 0, 22, 20 },    { M_Down, L, L, 0, 30, 20 }, { M_Up, L, 0, 0, 30, 20 },
        { M_Down, R, R, 0, 30, 20 },  { M_Up, R, 0, 0, 30, 20 },   { M_Down, R, R, 0, 30, 20 },
        { M_Up, R, 0, 0, 30, 20 },
    };
    if (checkMouse(from, want, sizeof(want) / sizeof(want[0])))
        return 1;

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// Losing capture sends an Up for each held button, then Leave; destroying the canvas from its
// own Down handler releases the capture.
UI_TEST(inputlost)
{
    CxUiWindow* w;
    CxUiCanvas* cv = inputCanvas(&w, NULL);
    cxuiTestPointer(cv, P_Press, L, 0, 0, 10, 10);
    cxuiTestPointer(cv, P_Press, Mid, L, 0, 10, 10);
    cxuiTestCaptureLost(cv);
    cxuiTestPointer(cv, P_Release, L, L, 0, 10, 10);   // stale: the capture is gone
    static const MouseRec want[] = {
        { M_Enter, 0, 0, 0, 10, 10 },   { M_Down, L, L, 0, 10, 10 },
        { M_Down, Mid, L | Mid, 0, 10, 10 },
        { M_Up, L, Mid, CXUI_MOUSE_CaptureLost, -1, -1 }, { M_Up, Mid, 0, CXUI_MOUSE_CaptureLost, -1, -1 },
        { M_Leave, 0, 0, 0, -1, -1 },
    };
    if (checkMouse(0, want, sizeof(want) / sizeof(want[0])))
        return 1;
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (info.captured)
        TEST_FAIL(1, _SL("still captured after losing it"), stvNone);

    inlog.destroyOnDown = true;
    cxuiTestPointer(cv, P_Press, L, 0, 0, 10, 10);
    cxuiTestPointer(cv, P_Release, L, L, 0, 10, 10);
    if (!(CxUiWidget(cv)->state & CXUI_WS_Dead) || cxuiFocused(w))
        TEST_FAIL(1, _SL("destroyed in its Down handler: dead ${int}, still focused ${int}"),
                  stvar(int32, (CxUiWidget(cv)->state & CXUI_WS_Dead) != 0), stvar(int32, cxuiFocused(w) != NULL));

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// A borderless window has no frame and can't be resized by the user, whatever its sizing; a move
// started from a Down ends the press there with a lost capture, and needs a button held over a
// widget in that window.
UI_TEST(inputdrag)
{
    inlogReset();
    CxUiWindow* w    = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 }, .flags = CXUI_WINDOW_Borderless });
    CxUiWindow* other = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 } });
    CxUiCanvas* cv   = cxuiCanvasCreate(NULL);
    cxuiOnMouse(cv, inMouse, stvNone);
    cxuiContainerAppend(w, cv);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiWindowShow(other, CXUI_SHOW_Normal);

    CxUiTestWindowInfo wi, oi;
    bool borderless = false;
    cxuiTestWindowInfo(w, &wi);
    cxuiTestWindowInfo(other, &oi);
    if (!wi.borderless || wi.resizable || oi.borderless || !oi.resizable || !cxuiTestBorderless(w, &borderless) ||
        !borderless)
        TEST_FAIL(1, _SL("borderless ${int} (read back ${int}), resizable ${int}; framed window borderless ${int}, "
                         "resizable ${int}"),
                  stvar(int32, wi.borderless), stvar(int32, borderless), stvar(int32, wi.resizable),
                  stvar(int32, oi.borderless), stvar(int32, oi.resizable));
    cxuiWindowSetSizing(w, CXUI_SIZE_Fixed);
    cxuiWindowSetSizing(w, CXUI_SIZE_Start);
    cxuiTestWindowInfo(w, &wi);
    if (wi.resizable)
        TEST_FAIL(1, _SL("a change of sizing made the borderless window resizable"), stvNone);

    if (cxuiWindowBeginDrag(w))
        TEST_FAIL(1, _SL("a move started with no button held"), stvNone);
    inlog.dragOnDown = other;
    cxuiTestPointer(cv, P_Press, L, 0, 0, 10, 10);
    cxuiTestPointer(cv, P_Release, L, L, 0, 10, 10);
    cxuiTestWindowInfo(other, &oi);
    if (inlog.drags != 0 || oi.drags != 0)
        TEST_FAIL(1, _SL("a press in one window moved another"), stvNone);

    inlog.dragOnDown = w;
    int32 from       = inlog.nmouse;
    cxuiTestPointer(cv, P_Press, R, 0, 0, 20, 20);
    CxUiTestCanvasInfo ci;
    cxuiTestCanvasInfo(cv, &ci);
    cxuiTestPointer(cv, P_Release, R, R, 0, 30, 30);   // the platform's move took it
    cxuiTestWindowInfo(w, &wi);
    if (inlog.drags != 1 || wi.drags != 1 || ci.captured)
        TEST_FAIL(1, _SL("moves started ${int}, backend told ${int}, still captured ${int}"), stvar(int32, inlog.drags),
                  stvar(int32, wi.drags), stvar(int32, ci.captured));
    static const MouseRec want[] = {
        { M_Down, R, R, 0, 20, 20 },
        { M_Up, R, 0, CXUI_MOUSE_CaptureLost, -1, -1 },
        { M_Leave, 0, 0, 0, -1, -1 },
    };
    if (checkMouse(from, want, sizeof(want) / sizeof(want[0])))
        return 1;

    cxuiDestroy(w);
    cxuiDestroy(other);
    objRelease(&cv);
    objRelease(&w);
    objRelease(&other);
    return 0;
}

// In DIP mode positions are DIPs, rounded down, so they stay negative left of and above the canvas.
UI_TEST(inputdip)
{
    cxuiTestSetSystemScale(1.5f);
    CxUiWindow* w;
    CxUiCanvas* cv = inputCanvas(&w, &(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    cxuiTestPointer(cv, P_Press, L, 0, 0, 15, 16);
    cxuiTestPointer(cv, P_Release, L, L, 0, -1, -2);
    static const MouseRec want[] = {
        { M_Enter, 0, 0, 0, 10, 10 },
        { M_Down, L, L, 0, 10, 10 },
        { M_Up, L, 0, 0, -1, -2 },
        { M_Leave, 0, 0, 0, -1, -2 },
    };
    if (checkMouse(0, want, sizeof(want) / sizeof(want[0])))
        return 1;
    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// Wheel and key events bubble from the canvas to its window; a held key repeats; focus can be set
// and is forgotten when the canvas goes; cursors are applied whenever they are set.
UI_TEST(inputkeys)
{
    inlogReset();
    CxUiWindow* w  = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 } });
    CxUiCanvas* cv = cxuiCanvasCreate(NULL);
    cxuiCanvasSetCursor(cv, CXUI_CURSOR_Hand);
    cxuiOnFocus(cv, inFocus, stvNone);
    cxuiContainerAppend(w, cv);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (info.cursor != CXUI_CURSOR_Hand)
        TEST_FAIL(1, _SL("cursor set before realizing: ${uint}"), stvar(uint32, info.cursor));
    cxuiCanvasSetCursor(cv, CXUI_CURSOR_SizeWE);
    cxuiTestCanvasInfo(cv, &info);
    if (info.cursor != CXUI_CURSOR_SizeWE)
        TEST_FAIL(1, _SL("cursor set after realizing: ${uint}"), stvar(uint32, info.cursor));

    cxuiOnWheel(w, inWheel, stvar(int32, CXUI_Handled));
    if (cxuiTestWheel(cv, 0, 2.5, 0, 5, 5) != CXUI_Handled || inlog.wheels != 1 || inlog.wheelDy != 2.5 ||
        inlog.wheelAt != CxUiWidget(w))
        TEST_FAIL(1, _SL("wheel: ${int} events, dy ${float}"), stvar(int32, inlog.wheels), stvar(float64, inlog.wheelDy));

    cxuiFocus(cv);
    if (cxuiFocused(w) != CxUiWidget(cv) || inlog.focusGained != 1)
        TEST_FAIL(1, _SL("cxuiFocus gave ${int} focus events"), stvar(int32, inlog.focusGained));
    cxuiOnKey(cv, inKey, stvar(int32, CXUI_Default));
    cxuiOnKey(w, inKey, stvar(int32, CXUI_Handled));
    CxUiResult r1 = cxuiTestKey(CxUiWidget(cv), CXUI_KEY_A, CXUI_MOD_Ctrl, true);
    bool first    = !inlog.lastKey.repeat;
    CxUiResult r2 = cxuiTestKey(CxUiWidget(cv), CXUI_KEY_A, CXUI_MOD_Ctrl, true);
    bool second   = inlog.lastKey.repeat;
    cxuiTestKey(CxUiWidget(cv), CXUI_KEY_A, CXUI_MOD_Ctrl, false);
    bool up = !inlog.lastKey.down && !inlog.lastKey.repeat;
    cxuiTestKey(CxUiWidget(cv), CXUI_KEY_A, 0, true);
    bool again = !inlog.lastKey.repeat;
    if (r1 != CXUI_Handled || r2 != CXUI_Handled || !first || !second || !up || !again || inlog.keys != 8 ||
        inlog.keyAt != CxUiWidget(w) || inlog.lastKey.key != CXUI_KEY_A)
        TEST_FAIL(1, _SL("keys: ${int} handler calls, first ${int} repeat ${int} up ${int} again ${int}"),
                  stvar(int32, inlog.keys), stvar(int32, first), stvar(int32, second), stvar(int32, up),
                  stvar(int32, again));

    cxuiDestroy(cv);
    if (cxuiFocused(w))
        TEST_FAIL(1, _SL("a destroyed canvas kept focus"), stvNone);

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// A key with no CxUiKey is reported by its scancode, and each such key repeats on its own.
UI_TEST(inputunmapped)
{
    inlogReset();
    CxUiWindow* w     = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 } });
    CxUiBox* box      = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V });
    CxUiCanvas* cv    = cxuiCanvasCreate(NULL);
    CxUiCanvas* other = cxuiCanvasCreate(NULL);
    cxuiContainerAppend(box, cv);
    cxuiContainerAppend(box, other);
    cxuiContainerAppend(w, box);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiFocus(cv);
    cxuiOnKey(w, inKey, stvar(int32, CXUI_Handled));

    // 1 down, 1 down again, 2 down, 1 up, 1 down, 2 down again
    static const struct {
        uint32 scancode;
        bool down, repeat;
    } steps[] = {
        { 94, true, false }, { 94, true, true }, { 34, true, false },
        { 94, false, false }, { 94, true, false }, { 34, true, true },
    };
    for (int32 i = 0; i < (int32)(sizeof(steps) / sizeof(steps[0])); i++) {
        CxUiResult r    = cxuiTestKeyScancode(CxUiWidget(cv), CXUI_KEY_None, 0, steps[i].scancode, steps[i].down);
        CxUiKeyEvent* k = &inlog.lastKey;
        if (r != CXUI_Handled || inlog.keys != i + 1 || k->key != CXUI_KEY_None || k->scancode != steps[i].scancode ||
            k->down != steps[i].down || k->repeat != steps[i].repeat)
            TEST_FAIL(1, _SL("step ${int}: ${int} events, key ${int} scancode ${uint} down ${int} repeat ${int}"),
                      stvar(int32, i), stvar(int32, inlog.keys), stvar(int32, k->key), stvar(uint32, k->scancode),
                      stvar(int32, k->down), stvar(int32, k->repeat));
    }

    // Keys released while focus was elsewhere are forgotten.
    cxuiFocus(other);
    cxuiFocus(cv);
    cxuiTestKeyScancode(CxUiWidget(cv), CXUI_KEY_None, 0, 34, true);
    if (inlog.lastKey.repeat)
        TEST_FAIL(1, _SL("a key held across a focus change repeated"), stvNone);

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&other);
    objRelease(&box);
    objRelease(&w);
    return 0;
}

// Text comes only while text input is on, and bubbles; settings made before the canvas is
// realized reach its peer, and the caret reaches it in physical pixels.
UI_TEST(inputtext)
{
    cxuiTestSetSystemScale(2.0f);
    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, &(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    inlogReset();
    if (!cxuiCanvasSetTextInput(cv, CXUI_TEXTINPUT_On) || cxuiCanvasTextInput(cv) != CXUI_TEXTINPUT_On)
        TEST_FAIL(1, _SL("text input before realizing: ${uint}"), stvar(uint32, cxuiCanvasTextInput(cv)));
    cxuiCanvasSetTextCaret(cv, 10, 20, 1, 12);
    cxuiOnText(w, inText, stvar(int32, CXUI_Handled));
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (info.textInput != CXUI_TEXTINPUT_On || !rectIs(info.caret, 20, 40, 2, 24))
        TEST_FAIL(1, _SL("the peer got text input ${uint}, caret ${int},${int} ${int}x${int}"),
                  stvar(uint32, info.textInput), RECT_ARGS(info.caret));

    cxuiTestText(cv, _S"h\xC3\xA9");
    if (inlog.texts != 1 || !strEq(inlog.text, _S"h\xC3\xA9") || inlog.textAt != CxUiWidget(w))
        TEST_FAIL(1, _SL("text bubbling to the window: ${int} events, \"${string}\""), stvar(int32, inlog.texts),
                  stvar(strref, inlog.text));
    cxuiOnText(cv, inText, stvar(int32, CXUI_Handled));
    cxuiTestText(cv, _S"\xF0\x9F\x98\x80");
    if (inlog.texts != 2 || !strEq(inlog.text, _S"h\xC3\xA9\xF0\x9F\x98\x80") || inlog.textAt != CxUiWidget(cv))
        TEST_FAIL(1, _SL("text handled by the canvas: ${int} events, \"${string}\""), stvar(int32, inlog.texts),
                  stvar(strref, inlog.text));

    cxuiCanvasSetTextInput(cv, 0);
    cxuiTestText(cv, _S"x");
    cxuiTestCanvasInfo(cv, &info);
    if (inlog.texts != 2 || info.textInput != 0)
        TEST_FAIL(1, _SL("with text input off: ${int} events, peer has ${uint}"), stvar(int32, inlog.texts),
                  stvar(uint32, info.textInput));

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    inlogReset();
    return 0;
}

static int checkDeltas(int32 from, const float32 (*want)[2], int32 n)
{
    if (inlog.nmouse != from + n)
        TEST_FAIL(1, _SL("${int} mouse events, expected ${int}"), stvar(int32, inlog.nmouse - from), stvar(int32, n));
    for (int32 i = 0; i < n; i++) {
        const MouseRec* g = &inlog.mouse[from + i];
        if (g->dx != want[i][0] || g->dy != want[i][1])
            TEST_FAIL(1, _SL("mouse event ${int} (action ${uint}) moved ${float},${float}; expected ${float},${float}"),
                      stvar(int32, from + i), stvar(uint32, g->action), stvar(float64, g->dx), stvar(float64, g->dy),
                      stvar(float64, want[i][0]), stvar(float64, want[i][1]));
    }
    return 0;
}

// Each move says how far the pointer went since the canvas's last mouse event, in the canvas's
// units, also outside it while a button is held; the first move after Enter says none.
UI_TEST(pointerdelta)
{
    cxuiTestSetSystemScale(2.0f);
    CxUiWindow* w;
    CxUiCanvas* cv = inputCanvas(&w, &(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 10, 10);
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 16, 4);
    cxuiTestPointer(cv, P_Press, L, 0, 0, 20, 4);
    cxuiTestPointer(cv, P_Move, 0, L, 0, 30, 4);
    cxuiTestPointer(cv, P_Move, 0, L, 0, 250, 4);
    cxuiTestPointer(cv, P_Release, L, L, 0, 250, 4);
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 50, 50);
    // Enter, Move; Move; Down; Move; Move outside; Up, Leave; Enter, Move
    static const float32 want[][2] = { { 0, 0 }, { 0, 0 }, { 3, -3 }, { 0, 0 }, { 5, 0 },
                                       { 110, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } };
    if (checkDeltas(0, want, sizeof(want) / sizeof(want[0])))
        return 1;

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// A lock begins where the pointer is over the canvas, or in its middle; moves then come as
// distances at that point, and so do presses, while the platform's own positions are ignored.
// Raw falls back to Warp where the platform can't do it. The lock is let go while the window is
// inactive and taken again after, as the window's focus events say; a lock asked for while
// inactive is taken on activation. Only one canvas has it, and hiding ends it.
UI_TEST(pointerlock)
{
    CxUiWindow* w;
    CxUiCanvas* cv = inputCanvas(&w, NULL);
    cxuiOnFocus(w, inWinFocus, stvNone);
    CxUiTestCanvasInfo info;

    cxuiTestPointer(cv, P_Move, 0, 0, 0, 30, 40);
    if (!cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Warp) || cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Warp)
        TEST_FAIL(1, _SL("Warp lock: mode ${uint}"), stvar(uint32, cxuiCanvasPointerLockMode(cv)));
    cxuiTestCanvasInfo(cv, &info);
    if (info.lockMode != CXUI_POINTERLOCK_Warp || !pointIs(info.lockAt, 30, 40))
        TEST_FAIL(1, _SL("the peer was locked with ${uint} at ${int},${int}"), stvar(uint32, info.lockMode),
                  stvar(int32, info.lockAt.x), stvar(int32, info.lockAt.y));
    int32 from = inlog.nmouse;
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 60, 60);
    cxuiTestPointerMotion(cv, 5, -2, 0);
    cxuiTestPointer(cv, P_Press, R, 0, 0, 70, 70);
    cxuiTestPointer(cv, P_Release, R, R, 0, 70, 70);
    static const MouseRec locked[] = {
        { M_Move, 0, 0, 0, 30, 40 },
        { M_Down, R, R, 0, 30, 40 },
        { M_Up, R, 0, 0, 30, 40 },
    };
    if (checkMouse(from, locked, sizeof(locked) / sizeof(locked[0])))
        return 1;
    if (inlog.mouse[from].dx != 5 || inlog.mouse[from].dy != -2)
        TEST_FAIL(1, _SL("a locked move went ${float},${float}"), stvar(float64, inlog.mouse[from].dx),
                  stvar(float64, inlog.mouse[from].dy));

    cxuiTestSetPointerLockMax(CXUI_POINTERLOCK_Warp);
    if (!cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Raw) || cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Warp)
        TEST_FAIL(1, _SL("Raw where the platform gives only Warp: mode ${uint}"),
                  stvar(uint32, cxuiCanvasPointerLockMode(cv)));
    cxuiTestSetPointerLockMax(CXUI_POINTERLOCK_Raw);

    cxuiTestActivate(w, false);
    cxuiTestCanvasInfo(cv, &info);
    if (cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Off || info.lockMode != CXUI_POINTERLOCK_Off ||
        inlog.winFocus[0] != 1 || inlog.winFocusFlags != CXUI_FOCUS_PointerLock)
        TEST_FAIL(1, _SL("inactive: mode ${uint}, peer ${uint}, ${int} focus events with flags ${uint}"),
                  stvar(uint32, cxuiCanvasPointerLockMode(cv)), stvar(uint32, info.lockMode),
                  stvar(int32, inlog.winFocus[0]), stvar(uint32, inlog.winFocusFlags));
    from = inlog.nmouse;
    cxuiTestPointerMotion(cv, 1, 1, 0);
    if (inlog.nmouse != from)
        TEST_FAIL(1, _SL("motion came while the lock was let go"), stvNone);
    cxuiTestActivate(w, true);
    cxuiTestCanvasInfo(cv, &info);
    if (cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Raw || !pointIs(info.lockAt, 30, 40) ||
        inlog.winFocus[1] != 1 || inlog.winFocusFlags != CXUI_FOCUS_PointerLock)
        TEST_FAIL(1, _SL("active again: mode ${uint} at ${int},${int}, ${int} focus events with flags ${uint}"),
                  stvar(uint32, cxuiCanvasPointerLockMode(cv)), stvar(int32, info.lockAt.x), stvar(int32, info.lockAt.y),
                  stvar(int32, inlog.winFocus[1]), stvar(uint32, inlog.winFocusFlags));

    // Let go, the pointer shows where the lock began, which the next move counts from.
    cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Off);
    cxuiTestCanvasInfo(cv, &info);
    from = inlog.nmouse;
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 35, 41);
    if (info.lockMode != CXUI_POINTERLOCK_Off || !pointIs(info.lockAt, 30, 40) || inlog.nmouse != from + 1 ||
        inlog.mouse[from].dx != 5 || inlog.mouse[from].dy != 1)
        TEST_FAIL(1, _SL("unlocked: peer ${uint} at ${int},${int}; the next move went ${float},${float}"),
                  stvar(uint32, info.lockMode), stvar(int32, info.lockAt.x), stvar(int32, info.lockAt.y),
                  stvar(float64, inlog.mouse[from].dx), stvar(float64, inlog.mouse[from].dy));
    cxuiTestActivate(w, false);
    cxuiTestActivate(w, true);
    if (inlog.winFocusFlags != 0 || cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Off)
        TEST_FAIL(1, _SL("with no lock, activation flagged ${uint}, mode ${uint}"), stvar(uint32, inlog.winFocusFlags),
                  stvar(uint32, cxuiCanvasPointerLockMode(cv)));

    // Asked for while inactive, away from the canvas: taken in its middle on activation.
    cxuiTestPointer(cv, P_Left, 0, 0, 0, 0, 0);
    cxuiTestActivate(w, false);
    if (!cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Warp) || cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Off)
        TEST_FAIL(1, _SL("asked for while inactive: mode ${uint}"), stvar(uint32, cxuiCanvasPointerLockMode(cv)));
    cxuiTestActivate(w, true);
    cxuiTestCanvasInfo(cv, &info);
    if (cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Warp || !pointIs(info.lockAt, 50, 50) ||
        inlog.winFocusFlags != CXUI_FOCUS_PointerLock)
        TEST_FAIL(1, _SL("taken on activation: mode ${uint} at ${int},${int}, flags ${uint}"),
                  stvar(uint32, cxuiCanvasPointerLockMode(cv)), stvar(int32, info.lockAt.x), stvar(int32, info.lockAt.y),
                  stvar(uint32, inlog.winFocusFlags));

    // Another canvas takes it; hiding that one's window ends it for good.
    CxUiWindow* w2;
    CxUiCanvas* cv2 = canvasInWindow(&w2, 100, 100, NULL);
    cxuiWindowShow(w2, CXUI_SHOW_Normal);
    if (!cxuiCanvasSetPointerLock(cv2, CXUI_POINTERLOCK_Warp) || cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Off)
        TEST_FAIL(1, _SL("a second canvas's lock left the first with ${uint}"), stvar(uint32, cxuiCanvasPointerLockMode(cv)));
    cxuiWindowHide(w2);
    cxuiTestActivate(w2, false);
    cxuiTestActivate(w2, true);
    cxuiTestCanvasInfo(cv2, &info);
    if (cxuiCanvasPointerLockMode(cv2) != CXUI_POINTERLOCK_Off || info.lockMode != CXUI_POINTERLOCK_Off)
        TEST_FAIL(1, _SL("a hidden window's canvas kept its lock: ${uint}"), stvar(uint32, cxuiCanvasPointerLockMode(cv2)));
    if (cxuiCanvasSetPointerLock(cv2, CXUI_POINTERLOCK_Warp))
        TEST_FAIL(1, _SL("a canvas in a hidden window took the lock"), stvNone);

    // Destroying the locked canvas lets go of it.
    cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Warp);
    cxuiDestroy(cv);
    if (!cxuiCanvasSetPointerLock(cv2, CXUI_POINTERLOCK_Off))
        TEST_FAIL(1, _SL("unlocking failed"), stvNone);

    cxuiDestroy(w);
    cxuiDestroy(w2);
    objRelease(&cv);
    objRelease(&cv2);
    objRelease(&w);
    objRelease(&w2);
    return 0;
}

// A lock taken during a drag keeps the button's capture. The Up comes where the lock began, and a
// handler that lets go of the lock there shows the pointer at that point, where the next move
// counts from. Deactivated mid-drag, the Ups come first, marked as a lost capture, then the
// window's focus event, flagged only if the lock is still on.
UI_TEST(pointerlockdrag)
{
    CxUiWindow* w;
    CxUiCanvas* cv = inputCanvas(&w, NULL);
    cxuiOnFocus(w, inWinFocus, stvNone);
    inlog.unlockOnUp = true;
    CxUiTestCanvasInfo info;

    cxuiTestPointer(cv, P_Move, 0, 0, 0, 20, 20);
    cxuiTestPointer(cv, P_Press, L, 0, 0, 20, 20);
    cxuiTestPointer(cv, P_Move, 0, L, 0, 26, 20);
    cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Warp);
    cxuiTestCanvasInfo(cv, &info);
    if (!info.captured || info.lockMode != CXUI_POINTERLOCK_Warp || !pointIs(info.lockAt, 26, 20))
        TEST_FAIL(1, _SL("locked mid-drag: captured ${int}, mode ${uint} at ${int},${int}"), stvar(int32, info.captured),
                  stvar(uint32, info.lockMode), stvar(int32, info.lockAt.x), stvar(int32, info.lockAt.y));
    cxuiTestPointerMotion(cv, 40, 0, 0);
    cxuiTestPointerMotion(cv, 300, 10, 0);
    cxuiTestPointer(cv, P_Release, L, L, 0, 90, 90);
    cxuiTestCanvasInfo(cv, &info);
    if (info.captured || info.lockMode != CXUI_POINTERLOCK_Off || !pointIs(info.lockAt, 26, 20) ||
        cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Off)
        TEST_FAIL(1, _SL("after the Up: captured ${int}, peer ${uint} at ${int},${int}"), stvar(int32, info.captured),
                  stvar(uint32, info.lockMode), stvar(int32, info.lockAt.x), stvar(int32, info.lockAt.y));
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 28, 21);
    static const MouseRec drag[] = {
        { M_Enter, 0, 0, 0, 20, 20 }, { M_Move, 0, 0, 0, 20, 20 }, { M_Down, L, L, 0, 20, 20 },
        { M_Move, 0, L, 0, 26, 20 },  { M_Move, 0, L, 0, 26, 20 }, { M_Move, 0, L, 0, 26, 20 },
        { M_Up, L, 0, 0, 26, 20 },    { M_Move, 0, 0, 0, 28, 21 },
    };
    static const float32 dragMoved[][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 6, 0 },
                                            { 40, 0 }, { 300, 10 }, { 0, 0 }, { 2, 1 } };
    if (checkMouse(0, drag, sizeof(drag) / sizeof(drag[0])) ||
        checkDeltas(0, dragMoved, sizeof(dragMoved) / sizeof(dragMoved[0])))
        return 1;

    // Deactivated mid-drag, with the Up letting go of the lock; later presses aren't double clicks.
    cxuiTestSetDoubleClick(0, 4);
    int32 from = inlog.nmouse;
    cxuiTestPointer(cv, P_Press, L, 0, 0, 28, 21);
    cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Warp);
    cxuiTestActivate(w, false);
    static const MouseRec lost[] = {
        { M_Down, L, L, 0, 28, 21 },
        { M_Up, L, 0, CXUI_MOUSE_CaptureLost, 28, 21 },
    };
    if (checkMouse(from, lost, sizeof(lost) / sizeof(lost[0])))
        return 1;
    cxuiTestCanvasInfo(cv, &info);
    if (info.captured || info.lockMode != CXUI_POINTERLOCK_Off || inlog.winFocus[0] != 1 || inlog.winFocusFlags != 0)
        TEST_FAIL(1, _SL("unlocked on the lost Up: captured ${int}, peer ${uint}, focus flags ${uint}"),
                  stvar(int32, info.captured), stvar(uint32, info.lockMode), stvar(uint32, inlog.winFocusFlags));
    cxuiTestActivate(w, true);
    if (cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Off || inlog.winFocusFlags != 0)
        TEST_FAIL(1, _SL("a lock let go on the Up came back: mode ${uint}"), stvar(uint32, cxuiCanvasPointerLockMode(cv)));

    // The same, keeping the lock
    inlog.unlockOnUp = false;
    from             = inlog.nmouse;
    cxuiTestPointer(cv, P_Press, L, 0, 0, 28, 21);
    cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Warp);
    cxuiTestActivate(w, false);
    if (checkMouse(from, lost, sizeof(lost) / sizeof(lost[0])))
        return 1;
    if (inlog.winFocusFlags != CXUI_FOCUS_PointerLock || inlog.winFocusAt != inlog.nmouse)
        TEST_FAIL(1, _SL("kept lock: focus flags ${uint}, after ${int} of ${int} mouse events"),
                  stvar(uint32, inlog.winFocusFlags), stvar(int32, inlog.winFocusAt), stvar(int32, inlog.nmouse));
    cxuiTestActivate(w, true);
    if (cxuiCanvasPointerLockMode(cv) != CXUI_POINTERLOCK_Warp || inlog.winFocusFlags != CXUI_FOCUS_PointerLock)
        TEST_FAIL(1, _SL("the kept lock wasn't taken again: mode ${uint}"), stvar(uint32, cxuiCanvasPointerLockMode(cv)));

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// A cursor from an image, set before or after realizing, held by the canvas; a stock cursor
// replaces it. Warping the pointer converts from the canvas's units, and the next move counts its
// distance from there; not while the pointer is locked.
UI_TEST(cursorimage)
{
    cxuiTestSetSystemScale(2.0f);
    uint8 px[4 * 4 * 4] = { 0 };
    CxUiImage* img  = cxuiImageCreateRGBA(4, 4, 16, px);
    CxUiCursor* cur = cxuiCursorCreate(img, 9, 1);
    objRelease(&img);
    if (cur->_hot.x != 3 || cur->_hot.y != 1)
        TEST_FAIL(1, _SL("the hot spot wasn't kept on the image: ${int},${int}"), stvar(int32, cur->_hot.x),
                  stvar(int32, cur->_hot.y));

    CxUiWindow* w;
    CxUiCanvas* cv = canvasInWindow(&w, 100, 100, &(CxUiCanvasDesc){ .units = CXUI_Units_Dip });
    inlogReset();
    cxuiOnMouse(cv, inMouse, stvNone);
    cxuiCanvasSetCursorImage(cv, cur);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    objRelease(&cur);   // the canvas holds it
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (!info.cursorImage || info.cursorImage->_hot.x != 3)
        TEST_FAIL(1, _SL("the image cursor didn't reach the peer"), stvNone);
    cxuiCanvasSetCursor(cv, CXUI_CURSOR_Hand);
    cxuiTestCanvasInfo(cv, &info);
    if (info.cursorImage || info.cursor != CXUI_CURSOR_Hand)
        TEST_FAIL(1, _SL("a stock cursor didn't replace the image's: cursor ${uint}"), stvar(uint32, info.cursor));

    cxuiTestPointer(cv, P_Move, 0, 0, 0, 10, 10);
    if (!cxuiCanvasWarpPointer(cv, 50, 40))
        TEST_FAIL(1, _SL("the pointer couldn't be moved"), stvNone);
    cxuiTestCanvasInfo(cv, &info);
    int32 from = inlog.nmouse;
    cxuiTestPointer(cv, P_Move, 0, 0, 0, 104, 80);
    if (!pointIs(info.warpedTo, 100, 80) || inlog.nmouse != from + 1 || inlog.mouse[from].dx != 2 ||
        inlog.mouse[from].dy != 0)
        TEST_FAIL(1, _SL("warped to ${int},${int}; the next move went ${float},${float}"), stvar(int32, info.warpedTo.x),
                  stvar(int32, info.warpedTo.y), stvar(float64, inlog.nmouse > from ? inlog.mouse[from].dx : -1),
                  stvar(float64, inlog.nmouse > from ? inlog.mouse[from].dy : -1));
    cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Warp);
    if (cxuiCanvasWarpPointer(cv, 1, 1))
        TEST_FAIL(1, _SL("the pointer was moved while locked"), stvNone);
    cxuiCanvasSetPointerLock(cv, CXUI_POINTERLOCK_Off);

    cxuiDestroy(w);
    objRelease(&cv);
    objRelease(&w);
    return 0;
}

// ---- layout ----

static const float32 layoutScales[] = { 1.0f, 1.25f, 2.0f };

static int32 snapPx(int32 dip, float32 s)
{
    return (int32)floor(dip * (float64)s + 0.5);
}

// Whether layout put w at a rectangle in DIPs, each edge rounded to a pixel at scale s.
static bool placedAt(CxUiWidget* w, float32 s, int32 x, int32 y, int32 wd, int32 ht, CxUiRect* got)
{
    if (!cxuiTestBounds(w, got))
        return false;
    int32 x0 = snapPx(x, s), y0 = snapPx(y, s);
    return got->x == x0 && got->y == y0 && got->w == snapPx(x + wd, s) - x0 && got->h == snapPx(y + ht, s) - y0;
}

#define CHECK_AT(what, w, s, x, y, wd, ht)                                                                       \
    do {                                                                                                         \
        CxUiRect got_;                                                                                           \
        if (!placedAt(CxUiWidget(w), s, x, y, wd, ht, &got_))                                                    \
            TEST_FAIL(1, _SL(what " at scale ${float}: placed at ${int},${int} ${int}x${int}, wanted DIPs ${int},${int} ${int}x${int}"), \
                      stvar(float64, s), RECT_ARGS(got_), stvar(int32, x), stvar(int32, y), stvar(int32, wd),       \
                      stvar(int32, ht));                                                                         \
    } while (0)

#define CHECK_SIZE(what, sz, ww, hh)                                                                      \
    do {                                                                                                  \
        CxUiSize sz_ = (sz);                                                                              \
        if (sz_.w != (ww) || sz_.h != (hh))                                                               \
            TEST_FAIL(1, _SL(what ": ${int}x${int}, wanted ${int}x${int}"), stvar(int32, sz_.w), stvar(int32, sz_.h), \
                      stvar(int32, ww), stvar(int32, hh));                                                \
    } while (0)

static CxUiSpace* space(int32 w, int32 h)
{
    return cxuiSpaceCreate(cxuiLen(w), cxuiLen(h));
}

// Appends child to c and drops the caller's reference; returns child, borrowed.
static void* put(void* c, void* child)
{
    cxuiContainerAppend((CxUiWidget*)c, (CxUiWidget*)child);
    CxUiWidget* w = child;
    objRelease(&w);
    return child;
}

static CxUiWindow* startWindow(int32 w, int32 h, CxUiWidget* content)
{
    CxUiWindow* win = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { w, h }, .sizing = CXUI_SIZE_Start });
    cxuiContainerAppend(win, content);
    objRelease(&content);
    cxuiWindowShow(win, CXUI_SHOW_Normal);
    return win;
}

// Natural sizes, margins that stay with their box, expanding, and alignment in both axes, at
// three scales.
UI_TEST(layoutbox)
{
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        cxuiTestSetSystemScale(s);
        CxUiBox* col  = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .margin = { 10, 10, 10, 10 }, .gap = 5 });
        CxUiBox* row1 = put(col, cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 4 }));
        CxUiSpace* a  = put(row1, space(30, 20));
        CxUiSpace* b  = put(row1, space(40, 10));
        CxUiSpace* c  = put(row1, space(20, 10));
        CxUiSpace* d  = put(col, space(50, 30));
        CxUiSpace* e  = put(col, space(10, 10));
        // A fill pushes the row's ends apart, and makes the row expand across.
        CxUiBox* row2 = put(col, cxuiBoxCreateH());
        CxUiSpace* f1 = put(row2, space(20, 10));
        put(row2, cxuiFillCreate());
        CxUiSpace* f2 = put(row2, space(20, 10));
        cxuiWidgetSetExpand(b, CXUI_EXPAND_H);
        cxuiWidgetSetAlign(c, CXUI_ALIGN_Default, CXUI_ALIGN_End);
        cxuiWidgetSetAlign(d, CXUI_ALIGN_Center, CXUI_ALIGN_Default);
        cxuiWidgetSetExpand(e, CXUI_EXPAND_V);

        // The row's own natural size has no margin: margins stay with the column.
        CHECK_SIZE("row natural size", cxuiNaturalSize(row1), 98, 20);
        CHECK_SIZE("column natural size", cxuiNaturalSize(col), 118, 105);

        CxUiWindow* w = startWindow(200, 120, CxUiWidget(col));
        CHECK_AT("column", col, s, 0, 0, 200, 120);
        // The row expands across because b does.
        CHECK_AT("row", row1, s, 10, 10, 180, 20);
        CHECK_AT("a", a, s, 10, 10, 30, 20);
        CHECK_AT("b", b, s, 44, 10, 122, 10);
        CHECK_AT("c", c, s, 170, 20, 20, 10);
        CHECK_AT("d", d, s, 75, 35, 50, 30);
        CHECK_AT("e", e, s, 10, 70, 10, 25);
        CHECK_AT("before the fill", f1, s, 10, 100, 20, 10);
        CHECK_AT("after the fill", f2, s, 170, 100, 20, 10);

        cxuiDestroy(w);
        objRelease(&w);
    }
    return 0;
}

// Spare space shared among expanding children leaves no gaps or overlaps at fractional scales,
// and the last child ends at the box's edge.
UI_TEST(layoutsnap)
{
    static const float32 scales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f };
    for (int i = 0; i < 6; i++) {
        float32 s    = scales[i];
        cxuiTestSetSystemScale(s);
        CxUiBox* row = cxuiBoxCreateH();
        CxUiSpace* sp[7];
        for (int j = 0; j < 7; j++) {
            sp[j] = put(row, space(j, 7));
            cxuiWidgetSetExpand(sp[j], CXUI_EXPAND_Both);
        }
        CxUiWindow* w = startWindow(101, 33, CxUiWidget(row));

        int32 edge = 0;
        for (int j = 0; j < 7; j++) {
            CxUiRect b;
            cxuiTestBounds(CxUiWidget(sp[j]), &b);
            if (b.x != edge || b.y != 0 || b.h != snapPx(33, s) || b.w < snapPx(j, s))
                TEST_FAIL(1, _SL("at scale ${float}, child ${int} is at ${int},${int} ${int}x${int}; the last ended at ${int}"),
                          stvar(float64, s), stvar(int32, j), RECT_ARGS(b), stvar(int32, edge));
            edge = b.x + b.w;
        }
        CxUiLayoutRect last = CxUiWidget(sp[6])->_lrect, all = CxUiWidget(row)->_lrect;
        if (edge != snapPx(101, s) || last.x + last.w != all.x + all.w)
            TEST_FAIL(1, _SL("at scale ${float} the row ends at ${int} (${int}/256 DIP short)"), stvar(float64, s),
                      stvar(int32, edge), stvar(int32, all.x + all.w - last.x - last.w));
        cxuiDestroy(w);
        objRelease(&w);
    }
    return 0;
}

// Expanding passes up to containers unless they stop it: a settings window keeps a column of
// "100% [fill] 200%" over a slider as wide as the slider.
UI_TEST(layoutexpand)
{
    for (int stop = 0; stop < 2; stop++) {
        for (int i = 0; i < 3; i++) {
            float32 s = layoutScales[i];
            cxuiTestSetSystemScale(s);
            CxUiBox* outer  = cxuiBoxCreateH();
            put(outer, space(60, 10));
            CxUiBox* col    = put(outer, cxuiBoxCreateV());
            CxUiBox* labels = put(col, cxuiBoxCreateH());
            put(labels, space(20, 10));
            put(labels, cxuiFillCreate());
            CxUiSpace* hi     = put(labels, space(20, 10));
            CxUiSpace* slider = put(col, space(160, 20));
            if (stop)
                cxuiWidgetSetExpand(col, CXUI_EXPAND_StopH);

            CxUiWindow* w = startWindow(400, 50, CxUiWidget(outer));
            int32 colw    = stop ? 160 : 340;
            CHECK_AT("column", col, s, 60, 0, colw, 30);
            CHECK_AT("label row", labels, s, 60, 0, colw, 10);
            CHECK_AT("right label", hi, s, 60 + colw - 20, 0, 20, 10);
            CHECK_AT("slider", slider, s, 60, 10, 160, 20);
            cxuiDestroy(w);
            objRelease(&w);
        }
    }
    return 0;
}

// Normalizing within a box, and across boxes with a group number.
UI_TEST(layoutnormalize)
{
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        cxuiTestSetSystemScale(s);
        CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V });
        CxUiBox* row = put(col, cxuiBoxCreate(&(CxUiBoxDesc){ .normalize = CXUI_EXPAND_H, .gap = 2 }));
        CxUiSpace* n1 = put(row, space(10, 5));
        CxUiSpace* n2 = put(row, space(30, 8));
        put(row, space(20, 5));
        CHECK_SIZE("normalized row", cxuiNaturalSize(row), 94, 8);

        CxUiBox* g1   = put(col, cxuiBoxCreateH());
        CxUiSpace* ga = put(g1, space(40, 10));
        CxUiSpace* gb = put(g1, space(25, 10));
        CxUiBox* g2   = put(col, cxuiBoxCreateH());
        CxUiSpace* gc = put(g2, space(60, 12));
        cxuiWidgetSetLayout(ga, &(CxUiLayoutParams){ .size = { cxuiLen(40), cxuiLen(10) }, .normalizeGroup = 3 });
        cxuiWidgetSetLayout(gb, &(CxUiLayoutParams){ .size = { cxuiLen(25), cxuiLen(10) }, .normalizeGroup = 3 });
        cxuiWidgetSetLayout(gc, &(CxUiLayoutParams){ .size = { cxuiLen(60), cxuiLen(12) }, .normalizeGroup = 3 });

        CxUiWindow* w = startWindow(300, 100, CxUiWidget(col));
        CHECK_AT("first normalized", n1, s, 0, 0, 30, 5);
        CHECK_AT("second normalized", n2, s, 32, 0, 30, 8);
        // Groups share width only, as their boxes don't normalize, and the boxes' natural sizes
        // follow.
        CHECK_AT("group member a", ga, s, 0, 8, 60, 10);
        CHECK_AT("group member b", gb, s, 60, 8, 60, 10);
        CHECK_AT("group member c", gc, s, 0, 18, 60, 12);
        CHECK_SIZE("first group's box", cxuiNaturalSize(g1), 120, 10);

        // A larger member grows every other.
        cxuiWidgetSetNaturalSize(gb, cxuiLen(70), cxuiLen(10));
        cxuiRefresh(w);
        CHECK_AT("group member a, grown", ga, s, 0, 8, 70, 10);
        CHECK_AT("group member c, grown", gc, s, 0, 18, 70, 12);
        cxuiDestroy(w);
        objRelease(&w);
    }
    return 0;
}

// Hidden children take no space and lose their gap; floating ones keep the bounds they're
// given; sizes can be set in one axis; minimum sizes hold when the window shrinks.
UI_TEST(layouthidden)
{
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        cxuiTestSetSystemScale(s);
        CxUiBox* row  = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 5 });
        CxUiSpace* a  = put(row, space(10, 10));
        CxUiSpace* b  = put(row, space(20, 10));
        CxUiSpace* c  = put(row, space(30, 10));
        CxUiSpace* f  = put(row, space(0, 0));
        CxUiBox* one  = put(row, cxuiBoxCreateV());
        put(one, space(10, 10));
        cxuiWidgetSetNaturalSize(one, cxuiLen(50), cxuiLen(0));
        cxuiWidgetSetLayout(f, &(CxUiLayoutParams){ .flags = CXUI_LP_Floating });
        cxuiWidgetSetBounds(f, 7, 3, 12, 4);
        cxuiWidgetSetVisible(b, false);
        CHECK_SIZE("row with a hidden child", cxuiNaturalSize(row), 100, 10);

        CxUiWindow* w = startWindow(200, 50, CxUiWidget(row));
        CHECK_AT("a", a, s, 0, 0, 10, 10);
        CHECK_AT("c", c, s, 15, 0, 30, 10);
        CHECK_AT("box sized across only", one, s, 50, 0, 50, 10);
        CHECK_AT("floating", f, s, 7, 3, 12, 4);

        cxuiWidgetSetVisible(b, true);
        cxuiRefresh(w);
        CHECK_AT("shown again", b, s, 15, 0, 20, 10);
        CHECK_AT("c after b", c, s, 40, 0, 30, 10);
        cxuiDestroy(w);
        objRelease(&w);

        // Shrinking: expanding children give up space first, down to their minimum, then the
        // rest from the end.
        CxUiBox* tight = cxuiBoxCreateH();
        CxUiSpace* x   = put(tight, space(40, 10));
        CxUiSpace* y   = put(tight, space(40, 10));
        cxuiWidgetSetExpand(x, CXUI_EXPAND_H);
        cxuiWidgetSetMinSize(x, cxuiLen(30), cxuiLen(0));
        w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 50, 10 }, .sizing = CXUI_SIZE_Start, .flags = CXUI_WINDOW_Shrink });
        put(w, tight);
        cxuiWindowShow(w, CXUI_SHOW_Normal);
        CHECK_AT("shrunk expanding child", x, s, 0, 0, 30, 10);
        CHECK_AT("shrunk other child", y, s, 30, 0, 20, 10);
        cxuiDestroy(w);
        objRelease(&w);
    }
    return 0;
}

// Character units follow the interface font at the window's scale, and convert again when it
// changes.
UI_TEST(layoutchars)
{
    CxUiBox* row  = cxuiBoxCreateH();
    CxUiSpace* sp = put(row, cxuiSpaceCreate(cxuiChars(10), cxuiChars(2)));
    CxUiWindow* w = startWindow(400, 200, CxUiWidget(row));
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        if (s != 1.0f)
            cxuiTestDpi(w, s);
        CxUiFont* f = cxuiFontCreate(NULL, s);
        CxUiFontMetrics m;
        cxuiFontMetrics(f, &m);
        objRelease(&f);
        int32 cw = (int32)floor(10.0 * m.avgCharW / s + 0.5), ch = (int32)floor(2.0 * m.height / s + 0.5);
        CHECK_SIZE("ten by two characters", cxuiNaturalSize(sp), cw, ch);
        CHECK_AT("ten by two characters", sp, s, 0, 0, cw, ch);
    }
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static bool appendMany(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiBox* col = stvlNextObj(cvars, CxUiBox);
    for (int i = 0; i < 100; i++)
        put(col, space(10, 1));
    return true;
}

// A hundred appends in one task are laid out in one pass, after the task.
UI_TEST(layoutcoalesce)
{
    CxUiBox* col  = cxuiBoxCreateV();
    CxUiWindow* w = startWindow(100, 100, CxUiWidget(col));
    cxuiFlush();
    int32 before = cxuiTestLayoutPasses(w);
    cxuiPost(closureCreate(appendMany, stvar(object, col)));
    cxuiFlush();
    int32 passes = cxuiTestLayoutPasses(w) - before;
    CxUiRect b;
    if (passes != 1 || cxuiContainerChildCount(col) != 100 ||
        !cxuiTestBounds(cxuiContainerChild(col, 99), &b) || b.y != 99)
        TEST_FAIL(1, _SL("100 appends took ${int} passes; the last child is at y ${int}"), stvar(int32, passes),
                  stvar(int32, b.y));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static bool busyTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    int64 end = clockTimer() + timeMS(30);
    while (clockTimer() < end) {
    }
    return true;
}

// A flush lays out a change even when work queued ahead of it uses up the tick.
UI_TEST(layoutflush)
{
    CxUiBox* col  = cxuiBoxCreateV();
    CxUiWindow* w = startWindow(100, 100, CxUiWidget(col));
    cxuiFlush();
    cxuiPost(closureCreate(busyTask, stvNone));
    CxUiSpace* added = put(col, space(10, 7));
    cxuiFlush();
    CxUiRect b = { 0 };
    if (!cxuiTestBounds(CxUiWidget(added), &b) || b.h != 7)
        TEST_FAIL(1, _SL("a child added behind a busy task was laid out ${int}x${int}"), stvar(int32, b.w), stvar(int32, b.h));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static CxUiResult countKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(ev);
    int32* n = stvlNextPtr(cvars);
    (*n)++;
    return CXUI_Handled;
}

// A disabled container disables everything inside it without changing their own settings, and a
// disabled widget's keys go to its containers.
UI_TEST(layoutenabled)
{
    CxUiBox* outer  = cxuiBoxCreateV();
    CxUiBox* inner  = put(outer, cxuiBoxCreateV());
    CxUiCanvas* on  = put(inner, cxuiCanvasCreate(NULL));
    CxUiCanvas* off = put(inner, cxuiCanvasCreate(NULL));
    cxuiWidgetSetEnabled(off, false);
    CxUiWindow* w = startWindow(100, 100, CxUiWidget(outer));
    int32 canvasKeys = 0, boxKeys = 0;
    cxuiOnKey(on, countKey, stvar(ptr, &canvasKeys));
    cxuiOnKey(outer, countKey, stvar(ptr, &boxKeys));

    cxuiTestKey(CxUiWidget(on), CXUI_KEY_A, 0, true);
    if (canvasKeys != 1 || boxKeys != 0)
        TEST_FAIL(1, _SL("enabled: the canvas saw ${int} keys and its container ${int}"), stvar(int32, canvasKeys),
                  stvar(int32, boxKeys));

    cxuiWidgetSetEnabled(inner, false);
    if (cxuiWidgetEffectiveEnabled(on) || !cxuiWidgetEnabled(on) || cxuiWidgetEffectiveEnabled(inner) ||
        !cxuiWidgetEffectiveEnabled(outer))
        TEST_FAIL(1, _SL("disabling the inner box didn't disable just what's inside it"), stvNone);
    cxuiTestKey(CxUiWidget(on), CXUI_KEY_A, 0, false);
    if (canvasKeys != 1 || boxKeys != 1)
        TEST_FAIL(1, _SL("disabled: the canvas saw ${int} keys and its container ${int}"), stvar(int32, canvasKeys),
                  stvar(int32, boxKeys));

    cxuiWidgetSetEnabled(inner, true);
    if (!cxuiWidgetEffectiveEnabled(on) || cxuiWidgetEffectiveEnabled(off) || cxuiWidgetEnabled(off))
        TEST_FAIL(1, _SL("enabling the box again didn't restore its children's own settings"), stvNone);

    // A widget added to a disabled container is disabled too.
    cxuiWidgetSetEnabled(outer, false);
    CxUiCanvas* later = put(inner, cxuiCanvasCreate(NULL));
    if (cxuiWidgetEffectiveEnabled(later) || !cxuiWidgetEnabled(later))
        TEST_FAIL(1, _SL("a widget added to a disabled box isn't disabled"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Canvases anywhere in the tree are created by layout once they are on screen, hidden with their
// containers, and destroyed when they are removed.
UI_TEST(layoutrealize)
{
    CxUiBox* col     = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 10 });
    CxUiCanvas* a    = put(col, cxuiCanvasCreate(NULL));
    CxUiBox* hidden  = put(col, cxuiBoxCreateH());
    CxUiCanvas* b    = put(hidden, cxuiCanvasCreate(NULL));
    cxuiWidgetSetVisible(hidden, false);
    int32 peers0  = cxuiTestPeersAlive();
    CxUiWindow* w = startWindow(100, 110, CxUiWidget(col));

    CxUiTestCanvasInfo info;
    if (!cxuiTestCanvasInfo(a, &info) || !rectIs(info.bounds, 0, 0, 100, 110) || CxUiWidget(b)->peer)
        TEST_FAIL(1, _SL("after showing: a at ${int},${int} ${int}x${int}; b has a peer: ${bool}"), RECT_ARGS(info.bounds),
                  stvar(bool, CxUiWidget(b)->peer != NULL));

    cxuiWidgetSetVisible(hidden, true);
    if (CxUiWidget(b)->peer)
        TEST_FAIL(1, _SL("showing a box realized its canvas before layout ran"), stvNone);
    cxuiFlush();
    CxUiTestCanvasInfo binfo;
    cxuiTestCanvasInfo(a, &info);
    if (!cxuiTestCanvasInfo(b, &binfo) || !rectIs(info.bounds, 0, 0, 100, 50) || !rectIs(binfo.bounds, 0, 60, 100, 50) ||
        !binfo.visible)
        TEST_FAIL(1, _SL("after showing the box: a at ${int},${int} ${int}x${int}, b at ${int},${int} ${int}x${int}"),
                  RECT_ARGS(info.bounds), RECT_ARGS(binfo.bounds));

    cxuiWidgetSetVisible(hidden, false);
    cxuiFlush();
    cxuiTestCanvasInfo(a, &info);
    if (!cxuiTestCanvasInfo(b, &binfo) || binfo.visible || !rectIs(info.bounds, 0, 0, 100, 110))
        TEST_FAIL(1, _SL("hiding the box again: b visible ${bool}, a at ${int},${int} ${int}x${int}"),
                  stvar(bool, binfo.visible), RECT_ARGS(info.bounds));

    CxUiCanvas* c = put(col, cxuiCanvasCreate(NULL));
    if (CxUiWidget(c)->peer)
        TEST_FAIL(1, _SL("a canvas appended after showing was realized before layout ran"), stvNone);
    cxuiFlush();
    if (!CxUiWidget(c)->peer)
        TEST_FAIL(1, _SL("a canvas appended after showing wasn't realized"), stvNone);

    int32 peers = cxuiTestPeersAlive();
    objAcquire(hidden);
    cxuiContainerDetach(col, hidden);
    if (CxUiWidget(b)->peer || cxuiTestPeersAlive() != peers - 1)
        TEST_FAIL(1, _SL("removing a box left its canvas's peer"), stvNone);
    // Added back, it is realized again from the same widgets.
    cxuiContainerAppend(col, hidden);
    cxuiWidgetSetVisible(hidden, true);
    objRelease(&hidden);
    cxuiFlush();
    if (!CxUiWidget(b)->peer)
        TEST_FAIL(1, _SL("a box added back didn't realize its canvas"), stvNone);

    cxuiDestroy(w);
    objRelease(&w);
    if (cxuiTestPeersAlive() != peers0)
        TEST_FAIL(1, _SL("${int} peers left after destroying the window"), stvar(int32, cxuiTestPeersAlive() - peers0));
    return 0;
}

// How each sizing mode picks and limits the window's size.
UI_TEST(layoutwindow)
{
    CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V });
    put(col, space(120, 80));
    CxUiWindow* w = cxuiWindowCreate(NULL);
    put(w, col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    CxUiTestWindowInfo wi;
    cxuiTestWindowInfo(w, &wi);
    if (wi.size.w != 120 || wi.size.h != 80 || wi.minSize.w != 120 || wi.minSize.h != 80 || !wi.resizable)
        TEST_FAIL(1, _SL("natural: ${int}x${int}, minimum ${int}x${int}"), stvar(int32, wi.size.w), stvar(int32, wi.size.h),
                  stvar(int32, wi.minSize.w), stvar(int32, wi.minSize.h));

    CxUiSize n = cxuiNaturalSize(w);
    if (n.w != 120 || n.h != 80)
        TEST_FAIL(1, _SL("the window's natural size: ${int}x${int}"), stvar(int32, n.w), stvar(int32, n.h));

    // Grows with its content until the user sizes it, and never gets smaller than the content.
    CxUiSpace* more = put(col, space(150, 20));
    cxuiFlush();
    cxuiTestWindowInfo(w, &wi);
    n = cxuiNaturalSize(w);
    if (wi.size.w != 150 || wi.size.h != 100 || n.w != 150 || n.h != 100)
        TEST_FAIL(1, _SL("natural after growing: ${int}x${int}, measured ${int}x${int}"), stvar(int32, wi.size.w),
                  stvar(int32, wi.size.h), stvar(int32, n.w), stvar(int32, n.h));
    cxuiTestResize(w, 300, 200);
    cxuiWidgetSetVisible(more, false);
    cxuiFlush();
    cxuiTestWindowInfo(w, &wi);
    if (wi.size.w != 300 || wi.size.h != 200 || wi.minSize.w != 120 || wi.minSize.h != 80)
        TEST_FAIL(1, _SL("natural after a user resize: ${int}x${int}, minimum ${int}x${int}"), stvar(int32, wi.size.w),
                  stvar(int32, wi.size.h), stvar(int32, wi.minSize.w), stvar(int32, wi.minSize.h));
    cxuiWidgetSetVisible(more, true);
    cxuiWidgetSetNaturalSize(more, cxuiLen(400), cxuiLen(20));
    cxuiFlush();
    cxuiTestWindowInfo(w, &wi);
    if (wi.size.w != 400 || wi.size.h != 200)
        TEST_FAIL(1, _SL("content grown past a user's size: ${int}x${int}"), stvar(int32, wi.size.w), stvar(int32, wi.size.h));
    cxuiDestroy(w);
    objRelease(&w);

    // Fixed: the size given, not resizable, no minimum
    col = cxuiBoxCreateV();
    put(col, space(120, 80));
    w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 50 }, .sizing = CXUI_SIZE_Fixed });
    put(w, col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestWindowInfo(w, &wi);
    if (wi.size.w != 100 || wi.size.h != 50 || wi.resizable || wi.minSize.w != 0)
        TEST_FAIL(1, _SL("fixed: ${int}x${int}"), stvar(int32, wi.size.w), stvar(int32, wi.size.h));
    cxuiDestroy(w);
    objRelease(&w);

    // Start: the size given, and a minimum larger than the content
    col = cxuiBoxCreateV();
    put(col, space(120, 80));
    w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 500, 400 }, .sizing = CXUI_SIZE_Start, .minSize = { 200, 10 } });
    put(w, col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestWindowInfo(w, &wi);
    if (wi.size.w != 500 || wi.size.h != 400 || wi.minSize.w != 200 || wi.minSize.h != 80)
        TEST_FAIL(1, _SL("start: ${int}x${int}, minimum ${int}x${int}"), stvar(int32, wi.size.w), stvar(int32, wi.size.h),
                  stvar(int32, wi.minSize.w), stvar(int32, wi.minSize.h));

    // Fit grows it after it has been sized smaller.
    cxuiWindowSetSizing(w, CXUI_SIZE_Natural);
    cxuiWindowSetSize(w, 100, 50);
    cxuiFlush();
    cxuiTestWindowInfo(w, &wi);
    if (wi.size.w != 200 || wi.size.h != 80)
        TEST_FAIL(1, _SL("sized below the minimum: ${int}x${int}"), stvar(int32, wi.size.w), stvar(int32, wi.size.h));
    put(col, space(300, 10));
    cxuiRefresh(w);
    cxuiWindowFit(w, 0);
    cxuiTestWindowInfo(w, &wi);
    if (wi.size.w != 300 || wi.size.h != 90)
        TEST_FAIL(1, _SL("after fit: ${int}x${int}"), stvar(int32, wi.size.w), stvar(int32, wi.size.h));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static int test_cxui_grp_layout(void)
{
    TEST_CHAIN(test_cxui_layoutbox, test_cxui_layoutexpand, test_cxui_layoutsnap, test_cxui_layoutnormalize, test_cxui_layouthidden,
               test_cxui_layoutchars, test_cxui_layoutcoalesce, test_cxui_layoutflush, test_cxui_layoutenabled, test_cxui_layoutrealize,
               test_cxui_layoutwindow);
}

// ---- palettes, themes and fonts ----

static CxUiResult countPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    unused_noeval(ev);
    int32* n = stvlNextPtr(cvars);
    (*n)++;
    return CXUI_Handled;
}

typedef struct ThemeLog {
    int32 events;
    uint32 theme;
} ThemeLog;

static CxUiResult logTheme(stvlist* cvars, CxUiThemeChangedEvent* ev)
{
    ThemeLog* log = stvlNextPtr(cvars);
    log->events++;
    log->theme = ev->theme;
    return CXUI_Default;
}

// New palettes copy the light colors or another palette; the built-ins differ and are shared.
UI_TEST(palettebasic)
{
    CxUiPalette* light = cxuiPaletteLight();
    CxUiPalette* dark  = cxuiPaletteDark();
    CxUiPalette* again = cxuiPaletteDark();
    CxUiPalette* fresh = cxuiPaletteCreate();
    if (dark != again || light == dark ||
        cxuiPaletteColor(light, CXUI_PAL_Content).bits == cxuiPaletteColor(dark, CXUI_PAL_Content).bits ||
        cxuiPaletteColor(fresh, CXUI_PAL_Text).bits != cxuiPaletteColor(light, CXUI_PAL_Text).bits)
        TEST_FAIL(1, _SL("built-in palettes aren't shared, distinct, or what new palettes start from"), stvNone);

    cxuiPaletteSetColor(fresh, CXUI_PAL_Accent, cxuiHex(0x123456));
    CxUiPalette* copy = cxuiPaletteClone(fresh);
    if (cxuiPaletteColor(copy, CXUI_PAL_Accent).bits != cxuiHex(0x123456).bits ||
        cxuiPaletteColor(copy, CXUI_PAL_Count).bits != 0)
        TEST_FAIL(1, _SL("a clone didn't copy the colors, or an unknown role isn't transparent"), stvNone);

    objRelease(&copy);
    objRelease(&fresh);
    objRelease(&again);
    objRelease(&dark);
    objRelease(&light);
    return 0;
}

// A widget uses its own palette, else its nearest container's, else its window's built-in one;
// setting one repaints the canvases inside.
UI_TEST(paletteinherit)
{
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 }, .theme = CXUI_Theme_Dark });
    CxUiBox* box  = put(w, cxuiBoxCreateV());
    CxUiCanvas* cv = put(box, cxuiCanvasCreate(NULL));
    int32 paints   = 0;
    cxuiOnPaint(cv, countPaint, stvar(ptr, &paints));
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiFlush();

    CxUiPalette* dark = cxuiPaletteDark();
    if (cxuiWidgetPalette(cv) != dark || cxuiWidgetPalette(w) != dark)
        TEST_FAIL(1, _SL("a dark window's widgets don't use the built-in dark palette"), stvNone);

    CxUiPalette* mine = cxuiPaletteCreate();
    int32 before      = paints;
    cxuiWidgetSetPalette(box, mine);
    cxuiFlush();
    if (cxuiWidgetPalette(cv) != mine || cxuiWidgetPalette(box) != mine || cxuiWidgetPalette(w) != dark ||
        paints != before + 1)
        TEST_FAIL(1, _SL("a box's palette: used by the canvas ${int}, repainted ${int} times"),
                  stvar(int32, cxuiWidgetPalette(cv) == mine), stvar(int32, paints - before));

    // The box keeps its palette through a theme change; without one, the canvas follows the window.
    cxuiWindowSetThemeMode(w, CXUI_Theme_Light);
    CxUiPalette* light = cxuiPaletteLight();
    if (cxuiWidgetPalette(cv) != mine || cxuiWidgetPalette(w) != light)
        TEST_FAIL(1, _SL("an explicit palette didn't survive the window's theme change"), stvNone);
    cxuiWidgetSetPalette(box, CxUiPaletteNone);
    if (cxuiWidgetPalette(cv) != light)
        TEST_FAIL(1, _SL("removing the box's palette didn't fall back to the window's"), stvNone);

    // A widget outside any window uses the system's.
    CxUiBox* loose = cxuiBoxCreateH();
    if (cxuiWidgetPalette(loose) != light)
        TEST_FAIL(1, _SL("a widget outside a window doesn't use the system's palette"), stvNone);

    objRelease(&loose);
    objRelease(&light);
    objRelease(&mine);
    objRelease(&dark);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Windows that follow the system resolve again when it changes, and are told, with their frame,
// palette and canvases following; windows with a mode of their own don't.
UI_TEST(themesystem)
{
    CxUiWindow* sys   = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 } });
    CxUiWindow* fixed = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 }, .theme = CXUI_Theme_Light });
    CxUiCanvas* cv    = put(sys, cxuiCanvasCreate(NULL));
    int32 paints      = 0;
    ThemeLog sysLog = { 0 }, fixedLog = { 0 };
    cxuiOnPaint(cv, countPaint, stvar(ptr, &paints));
    cxuiOnThemeChanged(sys, logTheme, stvar(ptr, &sysLog));
    cxuiOnThemeChanged(fixed, logTheme, stvar(ptr, &fixedLog));
    cxuiWindowShow(sys, CXUI_SHOW_Normal);
    cxuiWindowShow(fixed, CXUI_SHOW_Normal);
    cxuiFlush();

    CxUiPalette* dark = cxuiPaletteDark();
    int32 before      = paints;
    cxuiTestSetSystemTheme(CXUI_Theme_Dark);
    cxuiFlush();
    if (sysLog.events != 1 || sysLog.theme != CXUI_Theme_Dark || fixedLog.events != 0 || !frameDark(sys) ||
        frameDark(fixed) || cxuiWidgetPalette(cv) != dark || paints != before + 1)
        TEST_FAIL(1, _SL("system went dark: ${int} events (theme ${uint}), the fixed window ${int}; frame dark ${int}; "
                         "repainted ${int}"),
                  stvar(int32, sysLog.events), stvar(uint32, sysLog.theme), stvar(int32, fixedLog.events),
                  stvar(int32, frameDark(sys)), stvar(int32, paints - before));

    // The same setting again changes nothing; a mode set on the window is reported.
    cxuiTestSetSystemTheme(CXUI_Theme_Dark);
    cxuiWindowSetThemeMode(sys, CXUI_Theme_Dark);
    if (sysLog.events != 1)
        TEST_FAIL(1, _SL("${int} theme events without a change"), stvar(int32, sysLog.events));
    cxuiWindowSetThemeMode(sys, CXUI_Theme_Light);
    cxuiTestSetSystemTheme(CXUI_Theme_Light);
    if (sysLog.events != 2 || sysLog.theme != CXUI_Theme_Light || frameDark(sys))
        TEST_FAIL(1, _SL("setting light: ${int} events, last theme ${uint}"), stvar(int32, sysLog.events),
                  stvar(uint32, sysLog.theme));

    objRelease(&dark);
    cxuiDestroy(sys);
    cxuiDestroy(fixed);
    objRelease(&sys);
    objRelease(&fixed);
    return 0;
}

// A widget's font is inherited, made at its window's scale, and measures character units; a
// change lays out again.
UI_TEST(widgetfont)
{
    CxUiBox* col   = cxuiBoxCreateV();
    CxUiBox* inner = put(col, cxuiBoxCreateH());
    CxUiSpace* sp  = put(inner, cxuiSpaceCreate(cxuiChars(10), cxuiChars(1)));
    CxUiWindow* w  = startWindow(400, 200, CxUiWidget(col));
    CxUiFont* ui   = cxuiWidgetFont(sp);
    if (ui != cxuiWidgetFont(col) || ui != cxuiWidgetFont(w))
        TEST_FAIL(1, _SL("widgets without a font don't share the interface font"), stvNone);
    CxUiSize small = cxuiNaturalSize(sp);

    cxuiWidgetSetFont(inner, &(CxUiFontDesc){ .size = 24 });
    CxUiFont* big = cxuiWidgetFont(sp);
    CxUiFontMetrics bm, um;
    cxuiFontMetrics(big, &bm);
    cxuiFontMetrics(ui, &um);
    cxuiRefresh(w);
    CxUiSize large = cxuiNaturalSize(sp);
    int32 want     = (int32)floor(10.0 * bm.avgCharW + 0.5);
    if (big == ui || big != cxuiWidgetFont(inner) || cxuiWidgetFont(col) != ui || bm.height <= um.height ||
        large.w != want || large.w <= small.w)
        TEST_FAIL(1, _SL("a 24 pt font on the row: ten characters went from ${int} to ${int} DIPs, wanted ${int}"),
                  stvar(int32, small.w), stvar(int32, large.w), stvar(int32, want));
    CxUiRect b;
    cxuiTestBounds(CxUiWidget(sp), &b);
    if (b.w != large.w)
        TEST_FAIL(1, _SL("the font change wasn't laid out: ${int} wide, wanted ${int}"), stvar(int32, b.w),
                  stvar(int32, large.w));

    // At another scale the font is made again, and the size in DIPs stays close.
    cxuiTestDpi(w, 2.0f);
    CxUiFont* scaled = cxuiWidgetFont(sp);
    if (cxuiFontScale(scaled) != 2.0f || abs(cxuiNaturalSize(sp).w - large.w) > 10)
        TEST_FAIL(1, _SL("at scale 2 the font's scale is ${float} and ten characters ${int} DIPs"),
                  stvar(float64, cxuiFontScale(scaled)), stvar(int32, cxuiNaturalSize(sp).w));

    // Removing the font goes back to the interface font.
    cxuiWidgetSetFont(inner, NULL);
    cxuiRefresh(w);
    if (cxuiWidgetFont(sp) != cxuiWidgetFont(w))
        TEST_FAIL(1, _SL("removing the row's font didn't go back to the window's"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static int test_cxui_grp_theme(void)
{
    TEST_CHAIN(test_cxui_palettebasic, test_cxui_paletteinherit, test_cxui_themesystem, test_cxui_widgetfont);
}

// ---- containers ----

// A panel is a native surface its children live in, placed relative to it, in the palette's
// panel color; a frame has a border and shows the background around it; the window's background
// is the palette's.
UI_TEST(panelhost)
{
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        cxuiTestSetSystemScale(s);
        CxUiBox* col     = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .margin = { 10, 10, 10, 10 } });
        CxUiPanel* panel = put(col, cxuiPanelCreate(&(CxUiPanelDesc){ .margin = { 4, 4, 4, 4 } }));
        CxUiFrame* frame = put(panel, cxuiFrameCreate(&(CxUiPanelDesc){ .margin = { 2, 2, 2, 2 } }));
        CxUiCanvas* cv   = put(frame, cxuiCanvasCreate(NULL));
        cxuiWidgetSetExpand(cv, CXUI_EXPAND_None);
        cxuiWidgetSetNaturalSize(cv, cxuiLen(50), cxuiLen(20));
        // The frame's natural size has its border inside its margin.
        CHECK_SIZE("frame natural size", cxuiNaturalSize(frame), 56, 26);
        CxUiWindow* w = startWindow(200, 100, CxUiWidget(col));

        CxUiTestPanelInfo pi, fi;
        CxUiTestCanvasInfo ci;
        CxUiTestWindowInfo wi;
        CxUiPalette* pal = cxuiWidgetPalette(w);
        if (!cxuiTestPanelInfo(CxUiWidget(panel), &pi) || !cxuiTestPanelInfo(CxUiWidget(frame), &fi) ||
            !cxuiTestCanvasInfo(cv, &ci) || !cxuiTestWindowInfo(w, &wi))
            TEST_FAIL(1, _SL("at scale ${float}, the panel, frame or canvas has no peer"), stvar(float64, s));
        CHECK_AT("panel", panel, s, 10, 10, 64, 34);
        CHECK_AT("frame", frame, s, 14, 14, 56, 26);
        // Peers are placed within their host: the frame in the panel, the canvas in the frame.
        CxUiRect pb, fb, cb;
        cxuiTestBounds(CxUiWidget(panel), &pb);
        cxuiTestBounds(CxUiWidget(frame), &fb);
        cxuiTestBounds(CxUiWidget(cv), &cb);
        if (!rectIs(pi.bounds, pb.x, pb.y, pb.w, pb.h) || !rectIs(fi.bounds, fb.x - pb.x, fb.y - pb.y, fb.w, fb.h) ||
            !rectIs(ci.bounds, cb.x - fb.x, cb.y - fb.y, cb.w, cb.h))
            TEST_FAIL(1, _SL("at scale ${float}: the frame's peer at ${int},${int}, the canvas's at ${int},${int}"),
                      stvar(float64, s), stvar(int32, fi.bounds.x), stvar(int32, fi.bounds.y), stvar(int32, ci.bounds.x),
                      stvar(int32, ci.bounds.y));
        CxUiColor panelColor = cxuiPaletteColor(pal, CXUI_PAL_Panel);
        if (pi.bg.bits != panelColor.bits || pi.borderPx != 0 || fi.bg.bits != panelColor.bits ||
            fi.border.bits != cxuiPaletteColor(pal, CXUI_PAL_Border).bits || fi.borderPx != snapPx(1, s) ||
            wi.bg.bits != panelColor.bits)
            TEST_FAIL(1, _SL("at scale ${float}: colors wrong, or a frame border ${int} px"), stvar(float64, s),
                      stvar(int32, fi.borderPx));

        // A theme change and a color of its own both reach the panel's surface.
        cxuiWindowSetThemeMode(w, CXUI_Theme_Dark);
        cxuiTestPanelInfo(CxUiWidget(frame), &fi);
        cxuiTestWindowInfo(w, &wi);
        CxUiPalette* dark = cxuiPaletteDark();
        if (fi.bg.bits != cxuiPaletteColor(dark, CXUI_PAL_Panel).bits || wi.bg.bits != cxuiPaletteColor(dark, CXUI_PAL_Panel).bits)
            TEST_FAIL(1, _SL("the panel's colors didn't follow the theme"), stvNone);
        objRelease(&dark);
        cxuiPanelSetBg(panel, cxuiHex(0x102030));
        cxuiTestPanelInfo(CxUiWidget(frame), &fi);
        if (fi.bg.bits != cxuiHex(0x102030).bits)
            TEST_FAIL(1, _SL("a frame doesn't show its panel's own color around it"), stvNone);

        // Hiding the panel hides the surface; removing it takes every peer inside with it.
        bool shown;
        cxuiWidgetSetVisible(panel, false);
        cxuiFlush();
        if (!cxuiTestShown(CxUiWidget(panel), &shown) || shown)
            TEST_FAIL(1, _SL("a hidden panel's surface is still shown"), stvNone);
        int32 peers = cxuiTestPeersAlive();
        cxuiContainerDetach(col, panel);
        if (cxuiTestPeersAlive() != peers - 3)
            TEST_FAIL(1, _SL("removing a panel left ${int} of its 3 peers"), stvar(int32, cxuiTestPeersAlive() - peers + 3));
        cxuiDestroy(w);
        objRelease(&w);
    }
    return 0;
}

// Columns as wide as their widest child, rows as tall as their tallest, spare space spread over
// the columns and rows that expand.
UI_TEST(gridbox)
{
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        cxuiTestSetSystemScale(s);
        CxUiGridBox* g = cxuiGridBoxCreate(&(CxUiGridBoxDesc){ .ncols = 3, .colGap = 4, .rowGap = 2, .margin = { 1, 1, 1, 1 } });
        CxUiSpace* c[7];
        static const int32 sizes[7][2] = { { 10, 5 }, { 30, 8 }, { 20, 5 }, { 15, 12 }, { 5, 5 }, { 25, 6 }, { 8, 3 } };
        for (int k = 0; k < 7; k++)
            c[k] = put(g, space(sizes[k][0], sizes[k][1]));
        // Columns 15, 30, 25 wide; rows 8, 12, 3 tall
        CHECK_SIZE("grid natural size", cxuiNaturalSize(g), 1 + 15 + 4 + 30 + 4 + 25 + 1, 1 + 8 + 2 + 12 + 2 + 3 + 1);
        cxuiWidgetSetExpand(c[4], CXUI_EXPAND_Both);
        cxuiWidgetSetAlign(c[5], CXUI_ALIGN_End, CXUI_ALIGN_End);
        CxUiWindow* w = startWindow(200, 100, CxUiWidget(g));
        // Column 1 and row 1 take the spare 120 and 71.
        CHECK_AT("first cell", c[0], s, 1, 1, 10, 5);
        CHECK_AT("expanding cell", c[4], s, 20, 11, 150, 83);
        CHECK_AT("aligned cell", c[5], s, 174, 88, 25, 6);
        CHECK_AT("last row", c[6], s, 1, 96, 8, 3);
        cxuiDestroy(w);
        objRelease(&w);
    }
    return 0;
}

// A zbox is as large as its largest child, or its shown one; pages are realized when first
// shown, unless marked eager, and keep their peers hidden afterwards.
UI_TEST(zboxlazy)
{
    CxUiZBox* z = cxuiZBoxCreate(NULL);
    CxUiBox* pages[3];
    for (int p = 0; p < 3; p++) {
        pages[p] = put(z, cxuiBoxCreateV());
        for (int k = 0; k < 250; k++) {
            CxUiCanvas* cv = put(pages[p], cxuiCanvasCreate(NULL));
            cxuiWidgetSetNaturalSize(cv, cxuiLen(10 + p * 10), cxuiLen(1));
        }
    }
    CHECK_SIZE("zbox natural size", cxuiNaturalSize(z), 30, 250);
    CxUiZBox* sized = cxuiZBoxCreate(&(CxUiZBoxDesc){ .flags = CXUI_ZBOX_SizeToActive });
    put(sized, space(10, 10));
    put(sized, space(40, 20));
    CHECK_SIZE("size-to-active natural size", cxuiNaturalSize(sized), 10, 10);
    cxuiZBoxSetActive(sized, 1);
    CHECK_SIZE("size-to-active after switching", cxuiNaturalSize(sized), 40, 20);
    objRelease(&sized);

    int32 base    = cxuiTestPeersAlive();
    CxUiWindow* w = startWindow(100, 300, CxUiWidget(z));
    int32 shown   = cxuiTestPeersAlive() - base - 1;   // less the window's
    if (shown != 250 || CxUiWidget(cxuiContainerChild(pages[1], 0))->peer)
        TEST_FAIL(1, _SL("showing a 3-page zbox realized ${int} canvases, wanted 250"), stvar(int32, shown));

    cxuiZBoxSetActive(z, 1);
    cxuiFlush();
    bool vis0, vis1;
    cxuiTestShown(cxuiContainerChild(pages[0], 0), &vis0);
    cxuiTestShown(cxuiContainerChild(pages[1], 0), &vis1);
    if (cxuiTestPeersAlive() - base - 1 != 500 || vis0 || !vis1)
        TEST_FAIL(1, _SL("after switching: ${int} canvases realized; page 0 shown ${int}, page 1 shown ${int}"),
                  stvar(int32, cxuiTestPeersAlive() - base - 1), stvar(int32, vis0), stvar(int32, vis1));
    cxuiZBoxSetActive(z, 0);
    cxuiFlush();
    if (cxuiTestPeersAlive() - base - 1 != 500)
        TEST_FAIL(1, _SL("switching back realized again: ${int}"), stvar(int32, cxuiTestPeersAlive() - base - 1));

    // Eager: realized now, but hidden
    CxUiLayoutParams lp;
    cxuiWidgetLayout(pages[2], &lp);
    lp.flags |= CXUI_LP_RealizeEager;
    cxuiWidgetSetLayout(pages[2], &lp);
    cxuiRefresh(w);
    bool vis2;
    if (!cxuiTestShown(cxuiContainerChild(pages[2], 0), &vis2) || vis2 || cxuiTestPeersAlive() - base - 1 != 750)
        TEST_FAIL(1, _SL("an eager page: ${int} canvases realized, shown ${int}"),
                  stvar(int32, cxuiTestPeersAlive() - base - 1), stvar(int32, vis2));

    // Removing the shown page shows the next.
    cxuiContainerDetach(z, pages[0]);
    cxuiFlush();
    if (cxuiZBoxActive(z) != 0 || cxuiContainerChild(z, 0) != CxUiWidget(pages[1]) ||
        !cxuiTestShown(cxuiContainerChild(pages[1], 0), &vis1) || !vis1)
        TEST_FAIL(1, _SL("removing the shown page: active ${int}, next page shown ${int}"), stvar(int32, cxuiZBoxActive(z)),
                  stvar(int32, vis1));
    // Removing a page before the shown one keeps the same page shown.
    put(z, cxuiBoxCreateV());
    cxuiZBoxSetActive(z, 1);
    cxuiContainerDetach(z, pages[1]);
    cxuiFlush();
    if (cxuiZBoxActive(z) != 0 || !cxuiTestShown(cxuiContainerChild(pages[2], 0), &vis2) || !vis2)
        TEST_FAIL(1, _SL("removing an earlier page: active ${int}, the shown page still shown ${int}"),
                  stvar(int32, cxuiZBoxActive(z)), stvar(int32, vis2));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static CxUiResult countChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    int32* n = stvlNextPtr(cvars);
    (*n)++;
    return CXUI_Default;
}

// Panes share the split by its value within their limits; dragging the sash moves it and reports
// a change when let go.
// Fitting a window keeps a split's fraction and makes it big enough for that to give each pane
// its natural size, without changing the window's natural size, which is its minimum.
UI_TEST(splitfit)
{
    cxuiTestSetSystemScale(1.0f);
    CxUiSplit* split = cxuiSplitCreate(&(CxUiSplitDesc){ .value = 0.25f });
    CxUiCanvas* side = put(split, cxuiCanvasCreate(NULL));
    cxuiWidgetSetNaturalSize(side, cxuiLen(50), cxuiLen(20));
    CxUiBox* tiles = put(split, cxuiBoxCreateH());
    for (int i = 0; i < 2; i++) {
        CxUiCanvas* t = put(tiles, cxuiCanvasCreate(NULL));
        cxuiWidgetSetExpand(t, CXUI_EXPAND_None);
        cxuiWidgetSetNaturalSize(t, cxuiLen(60), cxuiLen(20));
    }
    CxUiWindow* w = startWindow(400, 60, CxUiWidget(split));
    CxUiRect sb, tb;
    CxUiSize sz;

    cxuiWindowFit(w, 0);
    cxuiWindowSize(w, &sz);
    if (sz.w != 400)
        TEST_FAIL(1, _SL("fitting without shrinking shrank the window to ${int}"), stvar(int32, sz.w));

    // A quarter must be the side's 50: the panes get 200 between them, and the sash takes 5.
    cxuiWindowFit(w, CXUI_FIT_Shrink);
    cxuiFlush();
    cxuiWindowSize(w, &sz);
    cxuiTestBounds(CxUiWidget(side), &sb);
    cxuiTestBounds(CxUiWidget(tiles), &tb);
    if (sz.w != 205 || sb.w != 50 || tb.w != 150 || cxuiSplitValue(split) != 0.25f)
        TEST_FAIL(1, _SL("fitted to ${int} wide (want 205): side ${int} (want 50), tiles ${int} (want 150), fraction ${float}"),
                  stvar(int32, sz.w), stvar(int32, sb.w), stvar(int32, tb.w), stvar(float64, cxuiSplitValue(split)));
    CxUiSize n = cxuiNaturalSize(w);
    if (n.w != 50 + 5 + 120)
        TEST_FAIL(1, _SL("fitting changed the natural width to ${int}"), stvar(int32, n.w));

    // With the sash moved right, the tiles' three quarters decide instead.
    cxuiSplitSetValue(split, 0.5f);
    cxuiWindowFit(w, CXUI_FIT_Shrink);
    cxuiFlush();
    cxuiWindowSize(w, &sz);
    cxuiTestBounds(CxUiWidget(tiles), &tb);
    if (sz.w != 245 || tb.w != 120)
        TEST_FAIL(1, _SL("at half: fitted to ${int} wide (want 245), tiles ${int} (want 120)"), stvar(int32, sz.w),
                  stvar(int32, tb.w));
    cxuiDestroy(w);
    objRelease(&w);

    // A window that fits its content starts big enough, and grows the same way for a new tile.
    split = cxuiSplitCreate(&(CxUiSplitDesc){ .value = 0.25f });
    side  = put(split, cxuiCanvasCreate(NULL));
    cxuiWidgetSetNaturalSize(side, cxuiLen(50), cxuiLen(20));
    tiles = put(split, cxuiBoxCreateH());
    for (int i = 0; i < 2; i++) {
        CxUiCanvas* t = put(tiles, cxuiCanvasCreate(NULL));
        cxuiWidgetSetExpand(t, CXUI_EXPAND_None);
        cxuiWidgetSetNaturalSize(t, cxuiLen(60), cxuiLen(20));
    }
    w = cxuiWindowCreate(NULL);
    put(w, split);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestBounds(CxUiWidget(side), &sb);
    cxuiTestBounds(CxUiWidget(tiles), &tb);
    CxUiTestWindowInfo wi;
    cxuiTestWindowInfo(w, &wi);
    if (sb.w != 50 || tb.w != 150 || wi.created.w != 205)
        TEST_FAIL(1, _SL("a natural window was created ${int} wide (want 205) with side ${int} (want 50), tiles ${int} (want 150)"),
                  stvar(int32, wi.created.w), stvar(int32, sb.w), stvar(int32, tb.w));
    CxUiCanvas* more = put(tiles, cxuiCanvasCreate(NULL));
    cxuiWidgetSetExpand(more, CXUI_EXPAND_None);
    cxuiWidgetSetNaturalSize(more, cxuiLen(60), cxuiLen(20));
    cxuiFlush();
    cxuiTestBounds(CxUiWidget(side), &sb);
    cxuiTestBounds(CxUiWidget(tiles), &tb);
    if (sb.w != 60 || tb.w != 180)
        TEST_FAIL(1, _SL("a natural window with a new tile: side ${int} (want 60), tiles ${int} (want 180)"),
                  stvar(int32, sb.w), stvar(int32, tb.w));
    // A pass that doesn't measure the split again, such as after the platform reports the size,
    // keeps it.
    cxuiTestWindowInfo(w, &wi);
    cxuiTestResize(w, wi.size.w, wi.size.h);
    cxuiTestBounds(CxUiWidget(tiles), &tb);
    if (tb.w != 180)
        TEST_FAIL(1, _SL("after the size was reported: tiles ${int} (want 180)"), stvar(int32, tb.w));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

UI_TEST(splitsash)
{
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        cxuiTestSetSystemScale(s);
        CxUiSplit* sp = cxuiSplitCreate(&(CxUiSplitDesc){ .value = 0.25f, .minPane = { 60, 50 }, .maxPane = { 150, 0 } });
        CxUiCanvas* a = put(sp, cxuiCanvasCreate(NULL));
        CxUiCanvas* b = put(sp, cxuiCanvasCreate(NULL));
        CxUiSpace* third = space(1, 1);
        bool took        = cxuiContainerAppend(sp, third);
        objRelease(&third);
        if (took || cxuiContainerChildCount(sp) != 3 || cxuiContainerChild(sp, 2) != CxUiWidget(cxuiSplitSash(sp)))
            TEST_FAIL(1, _SL("a split took a third pane, or its sash isn't its last child"), stvNone);
        CHECK_SIZE("split natural size", cxuiNaturalSize(sp), 60 + 5 + 50, 0);
        int32 changes = 0;
        cxuiOnChange(sp, countChange, stvar(ptr, &changes));
        CxUiWindow* w = startWindow(305, 100, CxUiWidget(sp));
        // 300 to share: a quarter is 75
        CHECK_AT("first pane", a, s, 0, 0, 75, 100);
        CHECK_AT("sash", cxuiSplitSash(sp), s, 75, 0, 5, 100);
        CHECK_AT("second pane", b, s, 80, 0, 225, 100);

        // Limits: the first pane's maximum, then the second's minimum
        cxuiSplitSetValue(sp, 0.9f);
        cxuiRefresh(w);
        CHECK_AT("first pane at its maximum", a, s, 0, 0, 150, 100);
        cxuiSplitSetValue(sp, 0.1f);
        cxuiRefresh(w);
        CHECK_AT("first pane at its minimum", a, s, 0, 0, 60, 100);

        // Drag the sash 40 DIPs right, in two moves, then let go.
        CxUiSash* sash = cxuiSplitSash(sp);
        int32 dx       = snapPx(20, s);
        cxuiTestPointer(CxUiCanvas(sash), CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 1, 10);
        cxuiTestPointer(CxUiCanvas(sash), CXUI_TESTPTR_Move, 0, CXUI_BTN_Left, 0, 1 + dx, 10);
        // The sash has moved under the pointer; positions are relative to it.
        CxUiRect sb;
        cxuiTestBounds(CxUiWidget(sash), &sb);
        int32 x = snapPx(60, s) + 1 + 2 * dx - sb.x;
        cxuiTestPointer(CxUiCanvas(sash), CXUI_TESTPTR_Move, 0, CXUI_BTN_Left, 0, x, 10);
        if (changes != 0)
            TEST_FAIL(1, _SL("a change was reported before the sash was let go"), stvNone);
        cxuiTestPointer(CxUiCanvas(sash), CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, x, 10);
        cxuiFlush();
        CxUiRect ab;
        cxuiTestBounds(CxUiWidget(a), &ab);
        if (changes != 1 || abs(ab.w - snapPx(100, s)) > 2)
            TEST_FAIL(1, _SL("at scale ${float}: dragged 40 DIPs, the first pane is ${int} px (wanted about ${int}); "
                             "${int} changes"),
                      stvar(float64, s), stvar(int32, ab.w), stvar(int32, snapPx(100, s)), stvar(int32, changes));
        cxuiDestroy(w);
        objRelease(&w);
    }
    return 0;
}

// A panel that moves carries the peers inside it: one whose place in the window stays the same
// while its panel moves is placed again within the panel, and so is a panel inside it.
UI_TEST(panelmoved)
{
    cxuiTestSetSystemScale(1.0f);
    CxUiBox* row    = cxuiBoxCreateH();
    CxUiCanvas* side = put(row, cxuiCanvasCreate(NULL));
    cxuiWidgetSetExpand(side, CXUI_EXPAND_None);
    cxuiWidgetSetNaturalSize(side, cxuiLen(100), cxuiLen(20));
    CxUiFrame* frame = put(row, cxuiFrameCreate(&(CxUiPanelDesc){ .border = 0 }));
    cxuiWidgetSetExpand(frame, CXUI_EXPAND_Both);
    CxUiCanvas* grow = put(frame, cxuiCanvasCreate(NULL));
    cxuiWidgetSetExpand(grow, CXUI_EXPAND_Both);
    CxUiPanel* inner  = put(frame, cxuiPanelCreate(NULL));
    CxUiCanvas* right = put(inner, cxuiCanvasCreate(NULL));
    cxuiWidgetSetExpand(right, CXUI_EXPAND_None);
    cxuiWidgetSetNaturalSize(right, cxuiLen(30), cxuiLen(20));
    CxUiWindow* w = startWindow(300, 40, CxUiWidget(row));

    CxUiRect before, after, fb, ib, rb;
    cxuiTestBounds(CxUiWidget(right), &before);
    cxuiWidgetSetVisible(side, false);
    cxuiFlush();
    cxuiTestBounds(CxUiWidget(right), &after);
    cxuiTestBounds(CxUiWidget(frame), &fb);
    cxuiTestBounds(CxUiWidget(inner), &ib);
    cxuiTestBounds(CxUiWidget(right), &rb);
    if (before.x != after.x || fb.x != 0)
        TEST_FAIL(1, _SL("the setup moved the canvas from ${int} to ${int}, or the frame to ${int}"), stvar(int32, before.x),
                  stvar(int32, after.x), stvar(int32, fb.x));

    CxUiTestPanelInfo ii;
    CxUiTestCanvasInfo ri;
    cxuiTestPanelInfo(CxUiWidget(inner), &ii);
    cxuiTestCanvasInfo(right, &ri);
    if (!rectIs(ii.bounds, ib.x - fb.x, ib.y - fb.y, ib.w, ib.h) || !rectIs(ri.bounds, rb.x - ib.x, rb.y - ib.y, rb.w, rb.h))
        TEST_FAIL(1, _SL("after the frame moved: the inner panel's peer at ${int},${int} (want ${int}), the canvas's at ${int},${int} (want ${int})"),
                  stvar(int32, ii.bounds.x), stvar(int32, ii.bounds.y), stvar(int32, ib.x - fb.x), stvar(int32, ri.bounds.x),
                  stvar(int32, ri.bounds.y), stvar(int32, rb.x - ib.x));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static int test_cxui_grp_containers(void)
{
    TEST_CHAIN(test_cxui_panelhost, test_cxui_panelmoved, test_cxui_gridbox, test_cxui_zboxlazy, test_cxui_splitsash, test_cxui_splitfit);
}

// ---- drawn widgets ----

// The last paint's operation of a kind, from the n-th match (0 for the first)
static const CxUiTestDrawOp* findOp(CxUiCanvas* cv, uint32 kind, int n)
{
    CxUiTestCanvasInfo info;
    if (!cxuiTestCanvasInfo(cv, &info))
        return NULL;
    for (int32 i = 0; i < info.nops; i++) {
        if (info.ops[i].kind == kind && n-- == 0)
            return &info.ops[i];
    }
    return NULL;
}

static CxUiColor palColor(void* w, uint32 role)
{
    return cxuiPaletteColor(cxuiWidgetPalette((CxUiWidget*)w), role);
}

// Lays out and paints everything pending.
static void settle(CxUiWindow* w)
{
    cxuiRefresh(w);
    cxuiFlush();
}

// A drawn label sizes itself to its text, draws it in the palette's colors on the background
// around it, shows text set after it started empty, keeps its colors when disabled, and shortens
// text that doesn't fit.
UI_TEST(dlabel)
{
    CxUiPanel* bar = cxuiPanelCreate(&(CxUiPanelDesc){ .gap = 4 });
    CxUiDLabel* l  = put(bar, cxuiDLabelCreate(&(CxUiDLabelDesc){ .padding = { 3, 2 } }));
    CxUiDLabel* e  = put(bar, cxuiDLabelCreate(&(CxUiDLabelDesc){ .text = _S"a rather long piece of text", .ellipsis = true }));
    cxuiWidgetSetNaturalSize(e, cxuiLen(60), cxuiLen(0));
    CxUiWindow* w = startWindow(300, 100, CxUiWidget(bar));
    settle(w);

    CxUiFont* f   = cxuiWidgetFont(l);
    CxUiSize line = cxuiFontMeasure(f, _S"");
    CHECK_SIZE("empty label", cxuiNaturalSize(l), 6, line.h + 4);
    const CxUiTestDrawOp* bg = findOp(CxUiCanvas(l), CXUI_TESTOP_Fill, 0);
    if (!bg || bg->c.bits != palColor(bar, CXUI_PAL_Panel).bits)
        TEST_FAIL(1, _SL("an empty label didn't paint the panel's background"), stvNone);

    cxuiDLabelSetText(l, _S"later");
    settle(w);
    const CxUiTestDrawOp* t = findOp(CxUiCanvas(l), CXUI_TESTOP_Text, 0);
    CxUiSize ts             = cxuiFontMeasure(f, _S"later");
    if (!t || !strEq(t->text, _S"later") || t->c.bits != palColor(l, CXUI_PAL_Text).bits || t->a.x != 3 || t->a.y != 2)
        TEST_FAIL(1, _SL("text set later: drawn ${int} at ${int},${int}"), stvar(int32, t != NULL), stvar(int32, t ? t->a.x : 0),
                  stvar(int32, t ? t->a.y : 0));
    CHECK_SIZE("label with text", cxuiNaturalSize(l), ts.w + 6, ts.h + 4);

    cxuiWidgetSetEnabled(bar, false);
    settle(w);
    t = findOp(CxUiCanvas(l), CXUI_TESTOP_Text, 0);
    if (!t || t->c.bits != palColor(l, CXUI_PAL_TextDisabled).bits)
        TEST_FAIL(1, _SL("a disabled label isn't in the palette's disabled color"), stvNone);
    cxuiWidgetSetEnabled(bar, true);
    settle(w);

    t = findOp(CxUiCanvas(e), CXUI_TESTOP_Text, 0);
    if (!t || !strEndsWith(t->text, _S"\xE2\x80\xA6") || t->r.w > 60 || strLen(t->text) < 4)
        TEST_FAIL(1, _SL("a shortened label drew \"${string}\", ${int} px wide"), stvar(strref, t ? t->text : NULL),
                  stvar(int32, t ? t->r.w : 0));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

typedef struct ClickLog {
    int32 activates, changes;
} ClickLog;

static CxUiResult logActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    ((ClickLog*)stvlNextPtr(cvars))->activates++;
    return CXUI_Default;
}

static CxUiResult logChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((ClickLog*)stvlNextPtr(cvars))->changes++;
    return CXUI_Default;
}

static void click(void* cv, int32 x, int32 y)
{
    CxUiCanvas* c = cv;
    cxuiTestPointer(c, CXUI_TESTPTR_Move, 0, 0, 0, x, y);
    cxuiTestPointer(c, CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, x, y);
    cxuiTestPointer(c, CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, x, y);
}

static uint32 bgOf(void* cv)
{
    const CxUiTestDrawOp* op = findOp(cv, CXUI_TESTOP_Fill, 0);
    return op ? op->c.bits : 0;
}

// A drawn button's states in the palette's colors, clicks that count and ones that don't,
// toggles and groups, and no events from code.
UI_TEST(dbutton)
{
    CxUiPanel* bar  = cxuiPanelCreate(&(CxUiPanelDesc){ .gap = 4 });
    CxUiDButton* b  = put(bar, cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"&Open && go" }));
    CxUiDGroup* grp = cxuiDGroupCreate();
    CxUiDButton* t1 = put(bar, cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"One", .toggle = true, .group = grp, .value = true }));
    CxUiDButton* t2 = put(bar, cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"Two", .toggle = true, .group = grp }));
    ClickLog bl = { 0 }, tl = { 0 };
    cxuiOnActivate(b, logActivate, stvar(ptr, &bl));
    cxuiOnActivate(t2, logActivate, stvar(ptr, &tl));
    cxuiOnChange(t2, logChange, stvar(ptr, &tl));
    CxUiWindow* w = startWindow(300, 100, CxUiWidget(bar));
    settle(w);

    CxUiSize ts = cxuiFontMeasure(cxuiWidgetFont(b), _S"Open & go");
    CHECK_SIZE("button", cxuiNaturalSize(b), ts.w + 16, ts.h + 8);
    const CxUiTestDrawOp* t = findOp(CxUiCanvas(b), CXUI_TESTOP_Text, 0);
    if (!t || !strEq(t->text, _S"Open & go") || bgOf(b) != palColor(b, CXUI_PAL_Panel).bits)
        TEST_FAIL(1, _SL("the button drew \"${string}\" on the wrong background"), stvar(strref, t ? t->text : NULL));

    // Hover, then held down
    cxuiTestPointer(CxUiCanvas(b), CXUI_TESTPTR_Move, 0, 0, 0, 5, 5);
    settle(w);
    uint32 hover = bgOf(b);
    cxuiTestPointer(CxUiCanvas(b), CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 5);
    settle(w);
    uint32 pressed = bgOf(b);
    cxuiTestPointer(CxUiCanvas(b), CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 5, 5);
    if (hover != palColor(b, CXUI_PAL_Hover).bits || pressed != palColor(b, CXUI_PAL_Pressed).bits || bl.activates != 1)
        TEST_FAIL(1, _SL("hover and press colors wrong, or a click activated ${int} times"), stvar(int32, bl.activates));

    // Let go outside: no click
    cxuiTestPointer(CxUiCanvas(b), CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 5);
    cxuiTestPointer(CxUiCanvas(b), CXUI_TESTPTR_Move, 0, CXUI_BTN_Left, 0, 500, 5);
    cxuiTestPointer(CxUiCanvas(b), CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 500, 5);
    cxuiTestPointer(CxUiCanvas(b), CXUI_TESTPTR_Left, 0, 0, 0, 0, 0);
    if (bl.activates != 1)
        TEST_FAIL(1, _SL("letting go outside the button clicked it"), stvNone);

    // Toggles in a group: one down at a time; clicking the one that is down changes nothing
    settle(w);
    if (bgOf(t1) != palColor(t1, CXUI_PAL_Selection).bits || cxuiDGroupValue(grp) != t1)
        TEST_FAIL(1, _SL("a toggle that starts down isn't drawn down, or isn't its group's value"), stvNone);
    click(t2, 5, 5);
    if (!cxuiDButtonValue(t2) || cxuiDButtonValue(t1) || tl.changes != 1 || tl.activates != 1)
        TEST_FAIL(1, _SL("clicking the second toggle: ${int} changes, ${int} activates, first still down ${int}"),
                  stvar(int32, tl.changes), stvar(int32, tl.activates), stvar(int32, cxuiDButtonValue(t1)));
    click(t2, 5, 5);
    if (!cxuiDButtonValue(t2) || tl.changes != 1 || tl.activates != 2)
        TEST_FAIL(1, _SL("clicking a toggle that is down in a group changed it"), stvNone);

    // From code: no events
    cxuiDButtonSetValue(t1, true);
    if (cxuiDButtonValue(t2) || tl.changes != 1)
        TEST_FAIL(1, _SL("setting a toggle from code didn't raise its group's other, or raised an event"), stvNone);

    // Disabled: no clicks, its own disabled text color
    cxuiWidgetSetEnabled(b, false);
    click(b, 5, 5);
    settle(w);
    t = findOp(CxUiCanvas(b), CXUI_TESTOP_Text, 0);
    if (bl.activates != 1 || !t || t->c.bits != palColor(b, CXUI_PAL_TextDisabled).bits)
        TEST_FAIL(1, _SL("a disabled button was clicked, or not drawn disabled"), stvNone);
    objRelease(&grp);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

typedef struct TabLog {
    int32 changes;
    bool pageRealized;   // whether the new page had a peer when the change arrived
    int32 value;         // the tabs' value then
    CxUiCanvas* page;
    CxUiDTabs* tabs;
} TabLog;

static CxUiResult tabChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    TabLog* log = stvlNextPtr(cvars);
    log->changes++;
    log->pageRealized = CxUiWidget(log->page)->peer != NULL;
    log->value        = cxuiDTabsValue(log->tabs);
    return CXUI_Default;
}

// Tabs show a zbox's pages; a click switches it and raises a change before the page is created on
// screen; setting the value from code switches without one.
UI_TEST(dtabs)
{
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiZBox* z      = cxuiZBoxCreate(NULL);
    CxUiCanvas* p0   = put(z, cxuiCanvasCreate(NULL));
    CxUiCanvas* p1   = put(z, cxuiCanvasCreate(NULL));
    CxUiDTabs* tabs  = put(col, cxuiDTabsCreate(&(CxUiDTabsDesc){ .zbox = z }));
    put(col, z);
    cxuiDTabsAdd(tabs, _S"First");
    cxuiDTabsAdd(tabs, _S"&Second");
    TabLog log = { .page = p1, .tabs = tabs };
    cxuiOnChange(tabs, tabChanged, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    settle(w);

    CxUiFont* f  = cxuiWidgetFont(tabs);
    int32 first  = cxuiFontMeasure(f, _S"First").w + 20;
    int32 second = cxuiFontMeasure(f, _S"Second").w + 20;
    CHECK_SIZE("tabs", cxuiNaturalSize(tabs), first + 3 + second, cxuiFontMeasure(f, _S"").h + 6);
    if (cxuiDTabsValue(tabs) != 0 || !CxUiWidget(p0)->peer || CxUiWidget(p1)->peer)
        TEST_FAIL(1, _SL("tabs didn't start on the first page"), stvNone);

    click(tabs, first + 3 + 4, 5);
    settle(w);
    if (log.changes != 1 || log.pageRealized || log.value != 1 || cxuiDTabsValue(tabs) != 1 || cxuiZBoxActive(z) != 1 ||
        !CxUiWidget(p1)->peer)
        TEST_FAIL(1, _SL("clicking the second tab: ${int} changes, page created before the change ${int}, zbox on ${int}"),
                  stvar(int32, log.changes), stvar(int32, log.pageRealized), stvar(int32, cxuiZBoxActive(z)));
    const CxUiTestDrawOp* acc = findOp(CxUiCanvas(tabs), CXUI_TESTOP_Text, 1);
    if (!acc || acc->c.bits != palColor(tabs, CXUI_PAL_SelectionText).bits || !strEq(acc->text, _S"Second"))
        TEST_FAIL(1, _SL("the picked tab isn't drawn picked"), stvNone);

    cxuiDTabsSetValue(tabs, 0);
    if (log.changes != 1 || cxuiZBoxActive(z) != 0)
        TEST_FAIL(1, _SL("setting the tab from code raised an event or didn't switch pages"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

typedef struct ScrollLog {
    int32 n;
    int64 pos;
    uint32 reason;
    uint32 reasons[16];
} ScrollLog;

static CxUiResult logScroll(stvlist* cvars, CxUiScrollEvent* ev)
{
    ScrollLog* log = stvlNextPtr(cvars);
    if (log->n < 16)
        log->reasons[log->n] = ev->reason;
    log->n++;
    log->pos    = ev->pos;
    log->reason = ev->reason;
    return CXUI_Default;
}

// A drawn scroll bar's thumb follows the range; the track pages, the thumb drags, and the wheel
// steps, each reported with its reason; code moves it without an event; it can hide itself.
UI_TEST(dscrollbar)
{
    CxUiBox* row        = cxuiBoxCreateH();
    CxUiDScrollBar* sb  = put(row, cxuiDScrollBarCreate(&(CxUiDScrollBarDesc){ .orient = CXUI_ORIENT_V, .range = 100,
                                                                                .page = 20, .step = 3 }));
    ScrollLog log = { 0 };
    cxuiOnScroll(sb, logScroll, stvar(ptr, &log));
    CxUiWindow* w = startWindow(100, 200, CxUiWidget(row));
    settle(w);
    CHECK_AT("scroll bar", sb, 1.0f, 0, 0, 12, 200);
    // The thumb is the second fill: 40 of 200, at the top
    const CxUiTestDrawOp* th = findOp(CxUiCanvas(sb), CXUI_TESTOP_Fill, 1);
    if (!th || th->r.y != 0 || th->r.h != 40 || th->c.bits != palColor(sb, CXUI_PAL_ScrollThumb).bits)
        TEST_FAIL(1, _SL("thumb at ${int} long ${int}"), stvar(int32, th ? th->r.y : -1), stvar(int32, th ? th->r.h : -1));

    cxuiDScrollBarSetPos(sb, 40);
    settle(w);
    th = findOp(CxUiCanvas(sb), CXUI_TESTOP_Fill, 1);
    if (log.n != 0 || !th || th->r.y != 80)
        TEST_FAIL(1, _SL("moved from code: ${int} events, thumb at ${int}"), stvar(int32, log.n), stvar(int32, th ? th->r.y : -1));

    // A click below the thumb pages down; letting go stops the paging.
    cxuiTestPointer(CxUiCanvas(sb), CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 190);
    cxuiTestPointer(CxUiCanvas(sb), CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 5, 190);
    if (log.n != 1 || log.pos != 60 || log.reason != CXUI_SCROLL_Page || cxuiDScrollBarPos(sb) != 60)
        TEST_FAIL(1, _SL("paging: ${int} events, pos ${int}"), stvar(int32, log.n), stvar(int64, log.pos));

    // Drag the thumb (now at 120) 32 DIPs up: 160 of track per 80 units
    log.n = 0;
    cxuiTestPointer(CxUiCanvas(sb), CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 130);
    cxuiTestPointer(CxUiCanvas(sb), CXUI_TESTPTR_Move, 0, CXUI_BTN_Left, 0, 5, 98);
    cxuiTestPointer(CxUiCanvas(sb), CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 5, 98);
    if (log.n != 3 || log.reasons[0] != CXUI_SCROLL_ThumbGrabbed || log.reasons[1] != CXUI_SCROLL_Thumb ||
        log.reasons[2] != CXUI_SCROLL_ThumbReleased || cxuiDScrollBarPos(sb) != 44)
        TEST_FAIL(1, _SL("dragging: ${int} events, pos ${int}"), stvar(int32, log.n), stvar(int64, cxuiDScrollBarPos(sb)));

    // Wheel: 2 notches of 3; then far past the end, kept within range
    log.n = 0;
    cxuiTestWheel(CxUiCanvas(sb), 0, 2, 0, 5, 5);
    cxuiTestWheel(CxUiCanvas(sb), 0, 100, 0, 5, 5);
    if (log.n != 2 || log.reasons[0] != CXUI_SCROLL_Wheel || cxuiDScrollBarPos(sb) != 80)
        TEST_FAIL(1, _SL("wheel: ${int} events, pos ${int}"), stvar(int32, log.n), stvar(int64, cxuiDScrollBarPos(sb)));

    // With a long range the thumb is at its minimum length, 16 of 200, and the rest of the track
    // covers the range less a page.
    CxUiDScrollBar* lng = put(row, cxuiDScrollBarCreate(&(CxUiDScrollBarDesc){ .orient = CXUI_ORIENT_V, .range = 1000,
                                                                               .page = 20 }));
    cxuiDScrollBarSetPos(lng, 490);
    settle(w);
    th = findOp(CxUiCanvas(lng), CXUI_TESTOP_Fill, 1);
    if (!th || th->r.y != 92 || th->r.h != 16)
        TEST_FAIL(1, _SL("a minimum-length thumb at ${int} long ${int}, wanted 92 and 16"), stvar(int32, th ? th->r.y : -1),
                  stvar(int32, th ? th->r.h : -1));
    cxuiTestPointer(CxUiCanvas(lng), CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 100);
    cxuiTestPointer(CxUiCanvas(lng), CXUI_TESTPTR_Move, 0, CXUI_BTN_Left, 0, 5, 146);
    cxuiTestPointer(CxUiCanvas(lng), CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 5, 146);
    if (cxuiDScrollBarPos(lng) != 735)
        TEST_FAIL(1, _SL("dragging a minimum-length thumb 46 DIPs moved to ${int}, wanted 735"),
                  stvar(int64, cxuiDScrollBarPos(lng)));

    // Hiding itself while everything is visible
    CxUiDScrollBar* ah = put(row, cxuiDScrollBarCreate(&(CxUiDScrollBarDesc){ .orient = CXUI_ORIENT_V, .range = 10,
                                                                              .page = 20, .autoHide = true }));
    if (cxuiWidgetVisible(ah))
        TEST_FAIL(1, _SL("a bar with everything visible didn't hide itself"), stvNone);
    cxuiDScrollBarSetRange(ah, 100, 20);
    if (!cxuiWidgetVisible(ah))
        TEST_FAIL(1, _SL("a bar with more than a page didn't show itself"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// ---- text fields ----

typedef struct TextLog {
    int32 changes, commits, keys;
    string atChange;            // the field's text when the change event came
    CxUiResult keyResult;       // what the key handler returns
    CxUiKey takeKey;            // a key the handler takes; others it leaves
} TextLog;

static CxUiResult textChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    TextLog* log = stvlNextPtr(cvars);
    log->changes++;
    strDup(&log->atChange, cxuiTextFieldText((CxUiTextField*)ev->base.current));
    return CXUI_Default;
}

static CxUiResult textCommitted(stvlist* cvars, CxUiCommitEvent* ev)
{
    unused_noeval(ev);
    ((TextLog*)stvlNextPtr(cvars))->commits++;
    return CXUI_Default;
}

static CxUiResult textKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    TextLog* log = stvlNextPtr(cvars);
    log->keys++;
    return ev->down && ev->key == log->takeKey ? CXUI_Handled : CXUI_Default;
}

static void pressKey(void* w, CxUiKey key, uint32 mods)
{
    cxuiTestKey((CxUiWidget*)w, key, mods, true);
    cxuiTestKey((CxUiWidget*)w, key, mods, false);
}

#define CHECK_TEXT(what, t, want)                                                                              \
    do {                                                                                                       \
        CxUiTestTextInfo ti_;                                                                                  \
        cxuiTestTextInfo((t), &ti_);                                                                           \
        bool ok_ = strEq(ti_.text, (want)) && strEq(cxuiTextFieldText(t), (want));                              \
        if (!ok_)                                                                                              \
            TEST_FAIL(1, _SL(what ": shows \"${string}\", model \"${string}\", wanted \"${string}\""),         \
                      stvar(strref, ti_.text), stvar(strref, cxuiTextFieldText(t)), stvar(strref, (want)));     \
        cxuiTestTextInfoDestroy(&ti_);                                                                         \
    } while (0)

// A field's text, cue, limit and selection reach its peer whether set before or after it is
// created; the user's edits raise change events and are seen at once, code's don't; the limit
// and read-only hold; and its natural size comes from its columns and font.
UI_TEST(textbasic)
{
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiTextField* t = put(col, cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = _S"hello", .cue = _S"Find",
                                                                           .maxLength = 8 }));
    CxUiTextField* n = put(col, cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .columns = 4 }));
    cxuiTextFieldSelect(t, 1, 3);
    TextLog log = { 0 };
    cxuiOnChange(t, textChanged, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    settle(w);

    // 20 columns of the null font's 7-pixel average width, the caret's room and a border of 4 each
    // side; a line of 16 and the same border
    CHECK_SIZE("text field", cxuiNaturalSize(t), 150, 24);
    CHECK_SIZE("four columns", cxuiNaturalSize(n), 38, 24);

    CxUiTestTextInfo ti;
    cxuiTestTextInfo(t, &ti);
    if (!strEq(ti.text, _S"hello") || !strEq(ti.cue, _S"Find") || ti.maxLength != 8 || ti.selStart != 1 ||
        ti.selEnd != 3 || ti.caret != 3 || ti.framed || ti.hasColors || !ti.enabled || ti.readOnly)
        TEST_FAIL(1, _SL("set before realizing: \"${string}\" cue \"${string}\" max ${int} sel ${int}-${int}"),
                  stvar(strref, ti.text), stvar(strref, ti.cue), stvar(int32, ti.maxLength), stvar(int32, ti.selStart),
                  stvar(int32, ti.selEnd));
    cxuiTestTextInfoDestroy(&ti);

    cxuiTextFieldSetCaret(t, 0);
    cxuiTextFieldSetText(t, _S"abc");
    CHECK_TEXT("set from code", t, _S"abc");
    // From a C string, which the field copies, over the text it has
    char typed[] = "xyz";
    cxuiTextFieldSetText(t, (strref)typed);
    CHECK_TEXT("set from a C string", t, _S"xyz");
    cxuiTextFieldSetText(t, _S"abc");
    if (log.changes != 0 || cxuiTextFieldCaret(t) != 3)
        TEST_FAIL(1, _SL("setting text: ${int} changes, caret ${int}"), stvar(int32, log.changes),
                  stvar(int32, cxuiTextFieldCaret(t)));

    cxuiTestType(t, _S"de");
    CHECK_TEXT("typed", t, _S"abcde");
    if (log.changes != 1 || !strEq(log.atChange, _S"abcde"))
        TEST_FAIL(1, _SL("typing: ${int} changes, text then \"${string}\""), stvar(int32, log.changes),
                  stvar(strref, log.atChange));

    // Over a selection, up to the limit of 8
    cxuiTextFieldSelect(t, 1, 2);
    cxuiTestType(t, _S"\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9xx");
    CHECK_TEXT("typed to the limit", t, _S"a\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9" "cde");
    int32 s0, s1;
    cxuiTextFieldSelection(t, &s0, &s1);
    if (s0 != 5 || s1 != 5 || cxuiTextFieldCaret(t) != 5)
        TEST_FAIL(1, _SL("caret after typing at ${int}-${int}"), stvar(int32, s0), stvar(int32, s1));

    cxuiTextFieldSelectAll(t);
    cxuiTextFieldSelection(t, &s0, &s1);
    if (s0 != 0 || s1 != 8)
        TEST_FAIL(1, _SL("select all: ${int}-${int}"), stvar(int32, s0), stvar(int32, s1));
    cxuiTextFieldSetCaret(t, 99);
    if (cxuiTextFieldCaret(t) != 8)
        TEST_FAIL(1, _SL("caret past the end at ${int}"), stvar(int32, cxuiTextFieldCaret(t)));

    cxuiTextFieldSetReadOnly(t, true);
    cxuiTestType(t, _S"zz");
    if (log.changes != 2 || !cxuiTextFieldReadOnly(t))
        TEST_FAIL(1, _SL("a read-only field took typing: ${int} changes"), stvar(int32, log.changes));
    cxuiTextFieldSetReadOnly(t, false);

    cxuiTextFieldSetCue(t, _S"Later");
    cxuiTextFieldSetMaxLength(t, 0);
    cxuiTestTextInfo(t, &ti);
    if (!strEq(ti.cue, _S"Later") || ti.maxLength != 0)
        TEST_FAIL(1, _SL("cue and limit set after realizing: \"${string}\" ${int}"), stvar(strref, ti.cue),
                  stvar(int32, ti.maxLength));
    cxuiTestTextInfoDestroy(&ti);

    // A font of its own: 12 points is 16 pixels, with a 19-pixel line and 8-pixel characters
    cxuiWidgetSetFont(n, &(CxUiFontDesc){ .size = 12 });
    settle(w);
    cxuiTestTextInfo(n, &ti);
    if (ti.fontPx != 16)
        TEST_FAIL(1, _SL("the field's font is ${int} px"), stvar(int32, ti.fontPx));
    cxuiTestTextInfoDestroy(&ti);
    CHECK_SIZE("four columns in a larger font", cxuiNaturalSize(n), 42, 27);

    strDestroy(&log.atChange);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// A field the platform measures differently from the estimate gets its new size in the same layout
// pass that creates it, so the window is shown at the right size, with no pass left over.
UI_TEST(textnative)
{
    cxuiTestSetTextNative(180, 30);
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiTextField* t = put(col, cxuiTextFieldCreate(NULL));
    CHECK_SIZE("estimate before showing", cxuiNaturalSize(t), 150, 24);
    CxUiWindow* w = cxuiWindowCreate(NULL);
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    CxUiTestWindowInfo wi;
    cxuiTestWindowInfo(w, &wi);
    int32 passes = cxuiTestLayoutPasses(w);
    CHECK_SIZE("window as shown", wi.size, 180, 30);
    cxuiFlush();
    if (cxuiTestLayoutPasses(w) != passes)
        TEST_FAIL(1, _SL("${int} layout passes ran after showing"), stvar(int32, cxuiTestLayoutPasses(w) - passes));
    cxuiTestSetTextNative(0, 0);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Enter commits and goes on to the containers; a key handler that takes Enter stops both; leaving
// the field after an edit commits, leaving it untouched doesn't.
UI_TEST(textcommit)
{
    CxUiBox* col      = cxuiBoxCreateV();
    CxUiTextField* t  = put(col, cxuiTextFieldCreate(NULL));
    CxUiTextField* t2 = put(col, cxuiTextFieldCreate(NULL));
    TextLog log = { 0 }, winLog = { 0 };
    cxuiOnCommit(t, textCommitted, stvar(ptr, &log));
    cxuiOnKey(t, textKey, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    cxuiOnKey(w, textKey, stvar(ptr, &winLog));
    settle(w);
    cxuiFocus(t);

    pressKey(t, CXUI_KEY_Enter, 0);
    if (log.commits != 1 || winLog.keys != 2)
        TEST_FAIL(1, _SL("Enter: ${int} commits, ${int} keys reached the window"), stvar(int32, log.commits),
                  stvar(int32, winLog.keys));
    pressKey(t, CXUI_KEY_PadEnter, 0);
    if (log.commits != 2)
        TEST_FAIL(1, _SL("Enter on the keypad: ${int} commits"), stvar(int32, log.commits));

    log.takeKey = CXUI_KEY_Enter;
    pressKey(t, CXUI_KEY_Enter, 0);
    if (log.commits != 2 || winLog.keys != 5)
        TEST_FAIL(1, _SL("Enter taken by the handler: ${int} commits, ${int} keys at the window"),
                  stvar(int32, log.commits), stvar(int32, winLog.keys));
    log.takeKey = CXUI_KEY_None;

    cxuiFocus(t2);
    if (log.commits != 2)
        TEST_FAIL(1, _SL("leaving an untouched field committed it"), stvar(int32, log.commits));
    cxuiFocus(t);
    cxuiTestType(t, _S"x");
    cxuiFocus(t2);
    if (log.commits != 3)
        TEST_FAIL(1, _SL("leaving an edited field: ${int} commits"), stvar(int32, log.commits));
    cxuiFocus(t);
    cxuiFocus(t2);
    if (log.commits != 3)
        TEST_FAIL(1, _SL("a commit repeated on leaving again"), stvar(int32, log.commits));

    cxuiTextFieldSetReadOnly(t, true);
    cxuiFocus(t);
    pressKey(t, CXUI_KEY_Enter, 0);
    if (log.commits != 3)
        TEST_FAIL(1, _SL("Enter committed a read-only field"), stvar(int32, log.commits));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

typedef struct FindLog {
    CxUiTextField* find;
    int32 hits;
} FindLog;

// The window's Ctrl+F, wherever the focus is: focus the find box and select its text.
static CxUiResult findShortcut(stvlist* cvars, CxUiKeyEvent* ev)
{
    FindLog* log = stvlNextPtr(cvars);
    if (!ev->down || ev->key != CXUI_KEY_F || ev->mods != CXUI_MOD_Ctrl)
        return CXUI_Default;
    log->hits++;
    cxuiFocus(log->find);
    cxuiTextFieldSelectAll(log->find);
    return CXUI_Handled;
}

// Tab visits what can take focus in the order of the tree, skipping drawn widgets without
// canFocus, disabled and hidden widgets and pages not shown, and wraps around, showing the
// window's focus cues; Shift+Tab goes back. A key handler that takes Tab keeps the focus where it
// is. Focusing a box focuses the first widget in it. Ctrl+F from any pane reaches the
// window. Clicking a drawn button leaves the focus in the field.
UI_TEST(textfocus)
{
    CxUiBox* col       = cxuiBoxCreateV();
    CxUiCanvas* cv     = put(col, cxuiCanvasCreate(NULL));
    CxUiTextField* f1  = put(col, cxuiTextFieldCreate(NULL));
    CxUiDButton* btn   = put(col, cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"Next" }));
    CxUiTextField* off = put(col, cxuiTextFieldCreate(NULL));
    CxUiTextField* f3  = put(col, cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .text = _S"needle" }));
    CxUiTextField* hid = put(col, cxuiTextFieldCreate(NULL));
    CxUiZBox* z        = put(col, cxuiZBoxCreate(NULL));
    put(z, cxuiBoxCreateH());
    CxUiTextField* pg  = put(z, cxuiTextFieldCreate(NULL));
    cxuiWidgetSetLayout(pg, &(CxUiLayoutParams){ .flags = CXUI_LP_RealizeEager });
    cxuiWidgetSetEnabled(off, false);
    cxuiWidgetSetVisible(hid, false);
    cxuiWidgetSetNaturalSize(cv, cxuiLen(50), cxuiLen(50));
    CxUiWindow* w = startWindow(300, 400, CxUiWidget(col));
    settle(w);
    if (!CxUiWidget(pg)->peer)
        TEST_FAIL(1, _SL("the eager page's field wasn't created"), stvNone);

    // Focus from code leaves the window's focus cues hidden; Tab shows them.
    bool cues = true;
    cxuiTestFocusCues(w, true, &cues);
    cxuiFocus(f1);
    cxuiTestFocusCues(w, false, &cues);
    if (cues)
        TEST_FAIL(1, _SL("focus from code showed the focus cues"), stvNone);
    pressKey(f1, CXUI_KEY_Tab, 0);
    if (cxuiFocused(w) != CxUiWidget(f3))
        TEST_FAIL(1, _SL("Tab from the first field went to ${string}"), stvar(strref, cxuiFocused(w) ? cxuiFocused(w)->name : NULL));
    cxuiTestFocusCues(w, false, &cues);
    if (!cues)
        TEST_FAIL(1, _SL("Tab didn't show the focus cues"), stvNone);
    pressKey(f3, CXUI_KEY_Tab, 0);
    if (cxuiFocused(w) != CxUiWidget(cv))
        TEST_FAIL(1, _SL("Tab from the last field didn't wrap to the canvas"), stvNone);
    pressKey(cv, CXUI_KEY_Tab, CXUI_MOD_Shift);
    if (cxuiFocused(w) != CxUiWidget(f3))
        TEST_FAIL(1, _SL("Shift+Tab from the canvas didn't go back to the last field"), stvNone);
    // Ctrl+Tab isn't navigation.
    pressKey(f3, CXUI_KEY_Tab, CXUI_MOD_Ctrl);
    if (cxuiFocused(w) != CxUiWidget(f3))
        TEST_FAIL(1, _SL("Ctrl+Tab moved the focus"), stvNone);

    // Taken by the field's handler, Tab doesn't move the focus.
    TextLog log = { .takeKey = CXUI_KEY_Tab };
    cxuiOnKey(f3, textKey, stvar(ptr, &log));
    if (cxuiTestKey(CxUiWidget(f3), CXUI_KEY_Tab, 0, true) != CXUI_Handled || cxuiFocused(w) != CxUiWidget(f3) ||
        log.keys != 1)
        TEST_FAIL(1, _SL("a handler that took Tab: focus moved, or it ran ${int} times"), stvar(int32, log.keys));
    cxuiTestKey(CxUiWidget(f3), CXUI_KEY_Tab, 0, false);

    // A field on a page not shown can't be given focus.
    cxuiFocus(pg);
    if (cxuiFocused(w) != CxUiWidget(f3))
        TEST_FAIL(1, _SL("a field on a page not shown took the focus"), stvNone);

    // Showing the page brings its field into the order.
    cxuiZBoxSetActive(z, 1);
    settle(w);
    log.takeKey = CXUI_KEY_None;
    pressKey(f3, CXUI_KEY_Tab, 0);
    if (cxuiFocused(w) != CxUiWidget(pg))
        TEST_FAIL(1, _SL("Tab didn't reach the field on the page shown"), stvNone);

    // A box gives focus to the first widget in it that takes it.
    cxuiFocus(col);
    if (cxuiFocused(w) != CxUiWidget(cv))
        TEST_FAIL(1, _SL("focusing a box didn't focus the canvas in it"), stvNone);

    FindLog find = { .find = f3 };
    cxuiOnKey(w, findShortcut, stvar(ptr, &find));
    cxuiFocus(cv);
    pressKey(cv, CXUI_KEY_F, CXUI_MOD_Ctrl);
    int32 s0, s1;
    cxuiTextFieldSelection(f3, &s0, &s1);
    if (find.hits != 1 || cxuiFocused(w) != CxUiWidget(f3) || s0 != 0 || s1 != 6)
        TEST_FAIL(1, _SL("Ctrl+F from the canvas: ${int} hits, selection ${int}-${int}"), stvar(int32, find.hits),
                  stvar(int32, s0), stvar(int32, s1));
    cxuiFocus(f1);
    pressKey(f1, CXUI_KEY_F, CXUI_MOD_Ctrl);
    if (find.hits != 2 || cxuiFocused(w) != CxUiWidget(f3))
        TEST_FAIL(1, _SL("Ctrl+F from another field didn't reach the window"), stvNone);

    // A drawn button made without canFocus leaves the focus alone; a canvas takes it.
    click(btn, 3, 3);
    if (cxuiFocused(w) != CxUiWidget(f3))
        TEST_FAIL(1, _SL("clicking a drawn button took the focus"), stvNone);
    click(cv, 3, 3);
    if (cxuiFocused(w) != CxUiWidget(cv))
        TEST_FAIL(1, _SL("clicking a canvas didn't take the focus"), stvNone);
    // A disabled field can't be given focus.
    cxuiFocus(off);
    if (cxuiFocused(w) != CxUiWidget(cv))
        TEST_FAIL(1, _SL("a disabled field took the focus"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// A text frame draws its border and background from the palette and shows focus and a bad value;
// its field has no border of its own and takes the palette's input colors; disabling the frame
// keeps those colors, with the disabled text color, and makes the field read-only rather than
// disabled; a theme change reaches the field.
UI_TEST(textframe)
{
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiTextField* t = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .cue = _S"Find" });
    CxUiTextFrame* fr = put(col, cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = t }));
    CxUiTextField* t2 = cxuiTextFieldCreate(&(CxUiTextFieldDesc){ .columns = 4 });
    CxUiTextFrame* nb = put(col, cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = t2, .noBorder = true,
                                                                           .padding = { 5, 1 } }));
    CxUiTextField* other = put(col, cxuiTextFieldCreate(NULL));
    objRelease(&t2);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    cxuiWindowSetThemeMode(w, CXUI_Theme_Light);
    settle(w);

    // The field: 20 columns of 7 and the caret's 2 by a 16-pixel line; padding 3 by 2 and a border of 1
    CHECK_SIZE("framed field", cxuiNaturalSize(t), 142, 16);
    CHECK_SIZE("text frame", cxuiNaturalSize(fr), 150, 22);
    CHECK_SIZE("borderless frame", cxuiNaturalSize(nb), 40, 18);
    if (cxuiTextFrameField(fr) != t)
        TEST_FAIL(1, _SL("the frame's field isn't the one it was given"), stvNone);

    CxUiTestPanelInfo pi;
    CxUiTestTextInfo ti;
    cxuiTestPanelInfo(CxUiWidget(fr), &pi);
    cxuiTestTextInfo(t, &ti);
    if (pi.bg.bits != palColor(fr, CXUI_PAL_Input).bits || pi.border.bits != palColor(fr, CXUI_PAL_InputBorder).bits ||
        pi.borderPx != 1)
        TEST_FAIL(1, _SL("the frame's colors aren't the palette's input colors"), stvNone);
    if (!ti.framed || !ti.hasColors || ti.textColor.bits != palColor(t, CXUI_PAL_InputText).bits ||
        ti.bg.bits != palColor(t, CXUI_PAL_Input).bits || ti.cueColor.bits != palColor(t, CXUI_PAL_InputCue).bits)
        TEST_FAIL(1, _SL("the framed field's colors aren't the palette's"), stvNone);
    cxuiTestTextInfoDestroy(&ti);
    cxuiTestPanelInfo(CxUiWidget(nb), &pi);
    if (pi.borderPx != 0)
        TEST_FAIL(1, _SL("a frame without a border has one ${int} px wide"), stvar(int32, pi.borderPx));

    cxuiFocus(t);
    cxuiTestPanelInfo(CxUiWidget(fr), &pi);
    if (pi.border.bits != palColor(fr, CXUI_PAL_Focus).bits)
        TEST_FAIL(1, _SL("the frame's border doesn't show focus"), stvNone);
    cxuiFocus(other);
    cxuiTestPanelInfo(CxUiWidget(fr), &pi);
    if (pi.border.bits != palColor(fr, CXUI_PAL_InputBorder).bits)
        TEST_FAIL(1, _SL("the frame's border still shows focus after it left"), stvNone);

    // Placed again at another scale, the frame keeps its own colors and scales its border.
    cxuiTestDpi(w, 2.0f);
    settle(w);
    cxuiTestPanelInfo(CxUiWidget(fr), &pi);
    if (pi.border.bits != palColor(fr, CXUI_PAL_InputBorder).bits || pi.borderPx != 2)
        TEST_FAIL(1, _SL("placed again at scale 2: border ${int} px, in the input border color ${int}"),
                  stvar(int32, pi.borderPx), stvar(int32, pi.border.bits == palColor(fr, CXUI_PAL_InputBorder).bits));
    cxuiTestDpi(w, 1.0f);
    settle(w);
    cxuiWidgetSetExpand(fr, CXUI_EXPAND_H);
    cxuiTestResize(w, 400, 200);
    settle(w);
    cxuiTestPanelInfo(CxUiWidget(fr), &pi);
    if (pi.border.bits != palColor(fr, CXUI_PAL_InputBorder).bits || pi.bounds.w != 400)
        TEST_FAIL(1, _SL("resized to ${int} wide, the frame lost its border color"), stvar(int32, pi.bounds.w));

    cxuiTextFrameSetBad(fr, true);
    cxuiTestPanelInfo(CxUiWidget(fr), &pi);
    cxuiTestTextInfo(t, &ti);
    if (pi.bg.bits != palColor(fr, CXUI_PAL_InputBad).bits || ti.bg.bits != palColor(fr, CXUI_PAL_InputBad).bits)
        TEST_FAIL(1, _SL("a bad value doesn't show"), stvNone);
    cxuiTestTextInfoDestroy(&ti);
    cxuiTextFrameSetBad(fr, false);

    // A background of the program's choosing reaches the field, and comes back after a bad value.
    cxuiPanelSetBgRole(fr, CXUI_PAL_Panel);
    cxuiTextFrameSetBad(fr, true);
    cxuiTextFrameSetBad(fr, false);
    cxuiTestPanelInfo(CxUiWidget(fr), &pi);
    cxuiTestTextInfo(t, &ti);
    if (pi.bg.bits != palColor(fr, CXUI_PAL_Panel).bits || ti.bg.bits != palColor(fr, CXUI_PAL_Panel).bits)
        TEST_FAIL(1, _SL("the frame's own background: frame ${uint(hex)}, field ${uint(hex)}"), stvar(uint32, pi.bg.bits),
                  stvar(uint32, ti.bg.bits));
    cxuiTestTextInfoDestroy(&ti);
    cxuiPanelSetBgRole(fr, CXUI_PAL_Input);

    cxuiWidgetSetEnabled(fr, false);
    settle(w);
    cxuiTestTextInfo(t, &ti);
    if (!ti.enabled || !ti.readOnly || ti.focusable || ti.textColor.bits != palColor(t, CXUI_PAL_TextDisabled).bits ||
        ti.bg.bits != palColor(t, CXUI_PAL_Input).bits)
        TEST_FAIL(1, _SL("disabled frame: field enabled ${int}, read-only ${int}, focusable ${int}"),
                  stvar(int32, ti.enabled), stvar(int32, ti.readOnly), stvar(int32, ti.focusable));
    cxuiTestTextInfoDestroy(&ti);
    cxuiTestType(t, _S"x");
    if (!strEmpty(cxuiTextFieldText(t)))
        TEST_FAIL(1, _SL("a disabled framed field took typing"), stvNone);
    cxuiWidgetSetEnabled(fr, true);
    settle(w);
    cxuiTestTextInfo(t, &ti);
    if (!ti.enabled || ti.readOnly || !ti.focusable || ti.textColor.bits != palColor(t, CXUI_PAL_InputText).bits)
        TEST_FAIL(1, _SL("enabled again, the field isn't back to normal"), stvNone);
    cxuiTestTextInfoDestroy(&ti);

    // A field on its own is disabled the platform's way.
    cxuiWidgetSetEnabled(other, false);
    cxuiTestTextInfo(other, &ti);
    if (ti.enabled || ti.hasColors)
        TEST_FAIL(1, _SL("a field on its own, disabled: enabled ${int}"), stvar(int32, ti.enabled));
    cxuiTestTextInfoDestroy(&ti);

    cxuiWindowSetThemeMode(w, CXUI_Theme_Dark);
    settle(w);
    cxuiTestTextInfo(t, &ti);
    CxUiPalette* dark = cxuiPaletteDark();
    bool darkOk       = ti.bg.bits == cxuiPaletteColor(dark, CXUI_PAL_Input).bits &&
                  ti.textColor.bits == cxuiPaletteColor(dark, CXUI_PAL_InputText).bits;
    objRelease(&dark);
    cxuiTestTextInfoDestroy(&ti);
    if (!darkOk)
        TEST_FAIL(1, _SL("the dark theme's colors didn't reach the field"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&t);
    return 0;
}

static int test_cxui_grp_drawn(void)
{
    TEST_CHAIN(test_cxui_dlabel, test_cxui_dbutton, test_cxui_dtabs, test_cxui_dscrollbar);
}

// ---- labels and buttons ----

typedef struct PressLog {
    int32 presses[4];   // by button
    int32 order[8];     // which button (1-4), or 9 for a commit, as they came
    int32 n;
    int32 winKeys;
    CxUiKey winTakes;   // a key the window's handler takes
} PressLog;

static void pressSeen(PressLog* log, int32 which)
{
    if (which >= 1 && which <= 4)
        log->presses[which - 1]++;
    if (log->n < 8)
        log->order[log->n++] = which;
}

static CxUiResult pressActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    pressSeen(stvlAtPtr(cvars, 0), stvlAt(cvars, 1, int32));
    return CXUI_Default;
}

static CxUiResult pressCommit(stvlist* cvars, CxUiCommitEvent* ev)
{
    unused_noeval(ev);
    pressSeen(stvlNextPtr(cvars), 9);
    return CXUI_Default;
}

static CxUiResult pressWinKey(stvlist* cvars, CxUiKeyEvent* ev)
{
    PressLog* log = stvlNextPtr(cvars);
    log->winKeys++;
    return ev->down && ev->key == log->winTakes ? CXUI_Handled : CXUI_Default;
}

static CxUiButton* logButton(void* box, strref title, PressLog* log, int32 which)
{
    CxUiButton* b = put(box, cxuiButtonCreate(&(CxUiButtonDesc){ .title = title }));
    cxuiOnActivate(b, pressActivate, stvar(ptr, log), stvar(int32, which));
    return b;
}

static CxUiTestControlInfo controlInfo(void* w)
{
    CxUiTestControlInfo info;
    cxuiTestControlInfo((CxUiWidget*)w, &info);
    return info;
}

// A label's text, style and color, and a button's title, reach their peers whether set before
// or after; a label is never disabled natively, but shows the palette's disabled text color; the
// mnemonic's `&` takes no room; a button's padding adds to both sides.
UI_TEST(controlbasic)
{
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiLabel* label = put(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Name", .align = CXUI_TEXT_Center,
                                                                  .ellipsis = CXUI_ELLIPSIS_End }));
    CxUiLabel* plain = put(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Name" }));
    CxUiButton* b    = put(col, cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Go" }));
    CxUiButton* pad  = put(col, cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Go", .padding = { 5, 3 } }));
    CxUiWindow* w    = startWindow(300, 200, CxUiWidget(col));
    settle(w);

    CxUiPalette* pal = cxuiWidgetPalette(label);
    CxUiTestControlInfo li = controlInfo(label);
    bool ok = strEq(li.text, _S"&Name") && li.align == CXUI_TEXT_Center && li.ellipsis == CXUI_ELLIPSIS_End &&
              li.color.bits == cxuiPaletteColor(pal, CXUI_PAL_Text).bits && li.visible && li.fontPx > 0 &&
              li.bounds.w > 0;
    cxuiTestControlInfoDestroy(&li);
    if (!ok)
        TEST_FAIL(1, _SL("the label's peer didn't get what it was given"), stvNone);
    CxUiSize ln = cxuiNaturalSize(label), pn = cxuiNaturalSize(plain);
    if (ln.w != pn.w || ln.h != pn.h || ln.w <= 0)
        TEST_FAIL(1, _SL("\"&Name\" measures ${int}x${int}, \"Name\" ${int}x${int}"), stvar(int32, ln.w),
                  stvar(int32, ln.h), stvar(int32, pn.w), stvar(int32, pn.h));
    CxUiSize bn = cxuiNaturalSize(b), padn = cxuiNaturalSize(pad);
    if (padn.w != bn.w + 10 || padn.h != bn.h + 6)
        TEST_FAIL(1, _SL("padding 5x3: ${int}x${int} against ${int}x${int}"), stvar(int32, padn.w), stvar(int32, padn.h),
                  stvar(int32, bn.w), stvar(int32, bn.h));

    cxuiLabelSetText(label, _S"Other");
    cxuiLabelSetColor(label, cxuiRGB(200, 10, 20));
    cxuiButtonSetTitle(b, _S"&Stop");
    settle(w);
    li = controlInfo(label);
    CxUiTestControlInfo bi = controlInfo(b);
    ok = strEq(li.text, _S"Other") && li.color.bits == cxuiRGB(200, 10, 20).bits && strEq(bi.text, _S"&Stop") &&
         !bi.isDefault && bi.enabled;
    cxuiTestControlInfoDestroy(&li);
    cxuiTestControlInfoDestroy(&bi);
    if (!ok)
        TEST_FAIL(1, _SL("text, color or title set after realizing didn't reach the peer"), stvNone);

    cxuiWidgetSetEnabled(col, false);
    settle(w);
    li = controlInfo(label);
    bi = controlInfo(b);
    ok = li.enabled && li.color.bits == cxuiPaletteColor(pal, CXUI_PAL_TextDisabled).bits && !bi.enabled;
    cxuiTestControlInfoDestroy(&li);
    cxuiTestControlInfoDestroy(&bi);
    if (!ok)
        TEST_FAIL(1, _SL("disabled: the label should keep its peer enabled in the disabled color, the button not"),
                  stvNone);
    cxuiWidgetSetEnabled(col, true);
    settle(w);
    li = controlInfo(label);
    ok = li.color.bits == cxuiRGB(200, 10, 20).bits;
    cxuiTestControlInfoDestroy(&li);
    if (!ok)
        TEST_FAIL(1, _SL("enabled again, the label lost its own color"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Clicks, Enter on a button with focus, and cxuiButtonPress() raise activate; a disabled or
// hidden button takes none of them.
UI_TEST(controlpress)
{
    PressLog log  = { 0 };
    CxUiBox* col  = cxuiBoxCreateV();
    CxUiButton* a = logButton(col, _S"A", &log, 1);
    CxUiButton* b = logButton(col, _S"B", &log, 2);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    cxuiOnKey(w, pressWinKey, stvar(ptr, &log));
    settle(w);

    cxuiTestClick(CxUiWidget(a));
    cxuiButtonPress(a);
    if (log.presses[0] != 2)
        TEST_FAIL(1, _SL("a click and a press: ${int} activates"), stvar(int32, log.presses[0]));

    // Enter presses the button with focus, not the default, and goes no further.
    cxuiWindowSetDefaultEnter(w, b);
    cxuiFocus(a);
    if (cxuiFocused(w) != CxUiWidget(a))
        TEST_FAIL(1, _SL("a button didn't take focus"), stvNone);
    pressKey(a, CXUI_KEY_Enter, 0);
    if (log.presses[0] != 3 || log.presses[1] != 0 || log.winKeys != 1)
        TEST_FAIL(1, _SL("Enter on a focused button: it ${int}, the default ${int}, keys at the window ${int}"),
                  stvar(int32, log.presses[0] - 2), stvar(int32, log.presses[1]), stvar(int32, log.winKeys));
    // Held down, it presses once.
    cxuiTestKey(CxUiWidget(a), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(a), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(a), CXUI_KEY_Enter, 0, false);
    if (log.presses[0] != 4)
        TEST_FAIL(1, _SL("a held Enter pressed ${int} times"), stvar(int32, log.presses[0] - 3));

    cxuiWidgetSetEnabled(a, false);
    settle(w);
    cxuiTestClick(CxUiWidget(a));
    cxuiButtonPress(a);
    cxuiWidgetSetEnabled(a, true);
    cxuiWidgetSetVisible(a, false);
    settle(w);
    cxuiTestClick(CxUiWidget(a));
    cxuiButtonPress(a);
    if (log.presses[0] != 4)
        TEST_FAIL(1, _SL("a disabled or hidden button was pressed ${int} times"), stvar(int32, log.presses[0] - 4));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Ends a popup that should have ended already, so a failure can't hang the test
static CxUiResult popupGiveUp(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    cxuiWindowEndPopup(stvlAtObj(cvars, 0, CxUiWindow), 99);
    return CXUI_Default;
}

static CxUiResult pressEscAt(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    pressKey(stvlAtObj(cvars, 0, CxUiWidget), CXUI_KEY_Escape, 0);
    return CXUI_Default;
}

// Enter and Esc nothing takes press the window's defaults, after a field's commit; the default
// is shown, moves, and isn't pressed while disabled or when the window's handler takes the key.
// Esc closes a popup without a default, and nothing else.
UI_TEST(controldefault)
{
    PressLog log       = { 0 };
    CxUiBox* col       = cxuiBoxCreateV();
    CxUiTextField* f   = put(col, cxuiTextFieldCreate(NULL));
    CxUiButton* ok     = logButton(col, _S"OK", &log, 1);
    CxUiButton* cancel = logButton(col, _S"Cancel", &log, 2);
    cxuiOnCommit(f, pressCommit, stvar(ptr, &log));
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiContainerAppend(w, col);
    objRelease(&col);
    // Set before the peers exist
    cxuiWindowSetDefaultEnter(w, ok);
    cxuiWindowSetDefaultEsc(w, cancel);
    cxuiOnKey(w, pressWinKey, stvar(ptr, &log));
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);
    CxUiTestControlInfo oi = controlInfo(ok), ci = controlInfo(cancel);
    bool shown = oi.isDefault && !ci.isDefault;
    cxuiTestControlInfoDestroy(&oi);
    cxuiTestControlInfoDestroy(&ci);
    if (!shown)
        TEST_FAIL(1, _SL("the default set before realizing isn't shown"), stvNone);

    cxuiFocus(f);
    pressKey(f, CXUI_KEY_Enter, 0);
    pressKey(f, CXUI_KEY_Escape, 0);
    if (log.n != 3 || log.order[0] != 9 || log.order[1] != 1 || log.order[2] != 2)
        TEST_FAIL(1, _SL("Enter then Esc in a field: ${int} events, ${int} ${int} ${int} (want commit, OK, Cancel)"),
                  stvar(int32, log.n), stvar(int32, log.order[0]), stvar(int32, log.order[1]), stvar(int32, log.order[2]));
    pressKey(f, CXUI_KEY_Enter, CXUI_MOD_Shift);
    if (log.presses[0] != 1)
        TEST_FAIL(1, _SL("Shift+Enter pressed the default"), stvNone);
    // Held down, it presses once.
    cxuiTestKey(CxUiWidget(f), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(f), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(f), CXUI_KEY_Enter, 0, false);
    if (log.presses[0] != 2)
        TEST_FAIL(1, _SL("a held Enter pressed the default ${int} times"), stvar(int32, log.presses[0] - 1));

    log.winTakes = CXUI_KEY_Enter;
    pressKey(f, CXUI_KEY_Enter, 0);
    log.winTakes = CXUI_KEY_None;
    cxuiWidgetSetEnabled(ok, false);
    settle(w);
    pressKey(f, CXUI_KEY_Enter, 0);
    cxuiWidgetSetEnabled(ok, true);
    if (log.presses[0] != 2)
        TEST_FAIL(1, _SL("Enter pressed the default ${int} more times, taken by the window or disabled"),
                  stvar(int32, log.presses[0] - 2));

    cxuiWindowSetDefaultEnter(w, cancel);
    oi = controlInfo(ok);
    ci = controlInfo(cancel);
    shown = !oi.isDefault && ci.isDefault;
    cxuiTestControlInfoDestroy(&oi);
    cxuiTestControlInfoDestroy(&ci);
    pressKey(f, CXUI_KEY_Enter, 0);
    if (!shown || log.presses[1] != 2)
        TEST_FAIL(1, _SL("a new default: shown ${int}, pressed ${int}"), stvar(int32, shown), stvar(int32, log.presses[1] - 1));

    // Without one, Esc leaves an ordinary window open.
    cxuiWindowSetDefaultEsc(w, CxUiWidgetNone);
    pressKey(f, CXUI_KEY_Escape, 0);
    if (!cxuiWindowIsVisible(w) || log.presses[1] != 2)
        TEST_FAIL(1, _SL("Esc with no default closed the window or pressed something"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);

    // In a popup, it closes it.
    CxUiWindow* owner    = cxuiWindowCreate(NULL);
    CxUiWindow* popup    = cxuiWindowCreate(NULL);
    CxUiTextField* field = cxuiTextFieldCreate(NULL);
    cxuiContainerAppend(popup, field);
    cxuiWindowShow(owner, CXUI_SHOW_Normal);
    CxUiTimer* tm     = UI_AFTER(5, pressEscAt, stvar(object, field));
    CxUiTimer* giveUp = UI_AFTER(3000, popupGiveUp, stvar(object, popup));
    int32 r           = cxuiWindowPopup(popup, owner);
    objRelease(&tm);
    objRelease(&giveUp);
    if (r != 0 || cxuiWindowIsVisible(popup))
        TEST_FAIL(1, _SL("Esc in a popup: returned ${int}, still visible ${int}"), stvar(int32, r),
                  stvar(int32, cxuiWindowIsVisible(popup)));
    objRelease(&field);
    cxuiDestroy(popup);
    cxuiDestroy(owner);
    objRelease(&popup);
    objRelease(&owner);
    return 0;
}

// A canvas that captures keys keeps Tab, Enter and Esc from the window's own handling whatever its
// handler returns, while the window's key handler still sees them; the system key alone captures
// nothing.
UI_TEST(keycapture)
{
    PressLog log       = { 0 };
    CxUiBox* col       = cxuiBoxCreateV();
    CxUiCanvas* cv     = put(col, cxuiCanvasCreate(NULL));
    CxUiCanvas* other  = put(col, cxuiCanvasCreate(NULL));
    CxUiButton* ok     = logButton(col, _S"OK", &log, 1);
    CxUiButton* cancel = logButton(col, _S"Cancel", &log, 2);
    CxUiWindow* w      = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowSetDefaultEnter(w, ok);
    cxuiWindowSetDefaultEsc(w, cancel);
    cxuiOnKey(w, pressWinKey, stvar(ptr, &log));
    if (!cxuiCanvasSetKeyCapture(cv, CXUI_KEYCAPTURE_On | CXUI_KEYCAPTURE_SystemKey) ||
        cxuiCanvasKeyCapture(cv) != (CXUI_KEYCAPTURE_On | CXUI_KEYCAPTURE_SystemKey))
        TEST_FAIL(1, _SL("key capture set before realizing: ${uint}"), stvar(uint32, cxuiCanvasKeyCapture(cv)));
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(cv, &info);
    if (info.keyCapture != (CXUI_KEYCAPTURE_On | CXUI_KEYCAPTURE_SystemKey))
        TEST_FAIL(1, _SL("the peer got key capture ${uint}"), stvar(uint32, info.keyCapture));

    cxuiFocus(cv);
    pressKey(cv, CXUI_KEY_Tab, 0);
    pressKey(cv, CXUI_KEY_Enter, 0);
    pressKey(cv, CXUI_KEY_Escape, 0);
    if (cxuiFocused(w) != CxUiWidget(cv) || log.presses[0] != 0 || log.presses[1] != 0 || log.winKeys != 6)
        TEST_FAIL(1, _SL("captured: focus moved ${int}, OK ${int}, Cancel ${int}, window saw ${int} keys"),
                  stvar(int32, cxuiFocused(w) != CxUiWidget(cv)), stvar(int32, log.presses[0]),
                  stvar(int32, log.presses[1]), stvar(int32, log.winKeys));

    cxuiCanvasSetKeyCapture(cv, CXUI_KEYCAPTURE_SystemKey);
    pressKey(cv, CXUI_KEY_Enter, 0);
    pressKey(cv, CXUI_KEY_Tab, 0);
    if (cxuiCanvasKeyCapture(cv) != 0 || log.presses[0] != 1 || cxuiFocused(w) != CxUiWidget(other))
        TEST_FAIL(1, _SL("the system key alone: capture ${uint}, OK ${int}, focus moved ${int}"),
                  stvar(uint32, cxuiCanvasKeyCapture(cv)), stvar(int32, log.presses[0]),
                  stvar(int32, cxuiFocused(w) == CxUiWidget(other)));

    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Alt with a mnemonic presses its button without moving focus, or focuses the first widget after
// its label that can take focus; `&&` is no mnemonic, and a disabled button is passed over.
UI_TEST(controlmnemonic)
{
    PressLog log        = { 0 };
    CxUiBox* col        = cxuiBoxCreateV();
    put(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Name:" }));
    CxUiTextField* name = put(col, cxuiTextFieldCreate(NULL));
    put(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Other:" }));
    CxUiTextField* gone = put(col, cxuiTextFieldCreate(NULL));
    CxUiTextField* off  = put(col, cxuiTextFieldCreate(NULL));
    CxUiTextField* other = put(col, cxuiTextFieldCreate(NULL));
    CxUiButton* go      = logButton(col, _S"&Go", &log, 1);
    CxUiButton* fish    = logButton(col, _S"Fish && &Chips", &log, 2);
    put(col, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Go to:" }));
    CxUiTextField* goTo = put(col, cxuiTextFieldCreate(NULL));
    cxuiWidgetSetVisible(gone, false);
    cxuiWidgetSetEnabled(off, false);
    CxUiWindow* w = startWindow(300, 300, CxUiWidget(col));
    cxuiOnKey(w, pressWinKey, stvar(ptr, &log));
    settle(w);

    cxuiFocus(other);
    pressKey(other, CXUI_KEY_N, CXUI_MOD_Alt);
    if (cxuiFocused(w) != CxUiWidget(name))
        TEST_FAIL(1, _SL("Alt+N didn't focus the field after \"&Name:\""), stvNone);
    pressKey(name, CXUI_KEY_O, CXUI_MOD_Alt);
    if (cxuiFocused(w) != CxUiWidget(other))
        TEST_FAIL(1, _SL("Alt+O didn't skip a hidden and a disabled field"), stvNone);
    pressKey(other, CXUI_KEY_G, CXUI_MOD_Alt);
    pressKey(other, CXUI_KEY_C, CXUI_MOD_Alt);
    if (log.presses[0] != 1 || log.presses[1] != 1 || cxuiFocused(w) != CxUiWidget(other))
        TEST_FAIL(1, _SL("Alt+G and Alt+C: ${int} and ${int} presses, focus moved ${int}"), stvar(int32, log.presses[0]),
                  stvar(int32, log.presses[1]), stvar(int32, cxuiFocused(w) != CxUiWidget(other)));
    int32 keys = log.winKeys;
    CxUiResult rf = cxuiTestKey(CxUiWidget(other), CXUI_KEY_F, CXUI_MOD_Alt, true);
    cxuiTestKey(CxUiWidget(other), CXUI_KEY_F, CXUI_MOD_Alt, false);
    CxUiResult rg = cxuiTestKey(CxUiWidget(other), CXUI_KEY_G, 0, true);
    cxuiTestKey(CxUiWidget(other), CXUI_KEY_G, 0, false);
    if (rf != CXUI_Default || rg != CXUI_Default || log.presses[0] != 1 || log.presses[1] != 1 || log.winKeys != keys + 4)
        TEST_FAIL(1, _SL("Alt+F (\"&&\") or G without Alt was taken"), stvNone);
    cxuiWidgetSetEnabled(go, false);
    settle(w);
    // Its mnemonic goes to the next widget that has it.
    pressKey(other, CXUI_KEY_G, CXUI_MOD_Alt);
    if (log.presses[0] != 1 || cxuiFocused(w) != CxUiWidget(goTo))
        TEST_FAIL(1, _SL("a disabled button's mnemonic was run, or not passed on to the next"), stvNone);
    unused_noeval(fish);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// The start widget gets focus when the window is first shown normally, not shown without
// activating it, and not again on a later show.
UI_TEST(controlstart)
{
    CxUiBox* col       = cxuiBoxCreateV();
    CxUiTextField* f1  = put(col, cxuiTextFieldCreate(NULL));
    CxUiTextField* f2  = put(col, cxuiTextFieldCreate(NULL));
    CxUiWindow* w      = cxuiWindowCreate(NULL);
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowSetStartFocus(w, f2);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    if (cxuiFocused(w) != CxUiWidget(f2))
        TEST_FAIL(1, _SL("the start widget didn't get focus"), stvNone);
    cxuiFocus(f1);
    cxuiWindowHide(w);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    if (cxuiFocused(w) != CxUiWidget(f1))
        TEST_FAIL(1, _SL("a second show moved focus to the start widget"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);

    CxUiTextField* f3 = cxuiTextFieldCreate(NULL);
    w                 = cxuiWindowCreate(NULL);
    cxuiContainerAppend(w, f3);
    cxuiWindowSetStartFocus(w, f3);
    cxuiWindowShow(w, CXUI_SHOW_NoActivate);
    if (cxuiFocused(w))
        TEST_FAIL(1, _SL("a window shown without activating took focus"), stvNone);
    objRelease(&f3);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// ---- wrapped labels and height for width ----

// The null font at scale 1: each character and space 8 pixels across, a line 16 down, the average
// character 7. This text is 192 across on one line; at 70 each word takes a line, at 100 three
// lines ("aaaa bbbb", "cccc dddd", "eeee"), and at 192 or more one.
#define WRAP_TEXT _S"aaaa bbbb cccc dddd eeee"

static CxUiLabel* wrapLabel(strref text, int32 columns)
{
    return cxuiLabelCreate(&(CxUiLabelDesc){ .text = text, .wrap = true, .columns = columns });
}

static CxUiCanvas* fixedCanvas(int32 w, int32 h)
{
    CxUiCanvas* cv = cxuiCanvasCreate(NULL);
    cxuiWidgetSetExpand(cv, CXUI_EXPAND_None);
    cxuiWidgetSetNaturalSize(cv, cxuiLen(w), cxuiLen(h));
    return cv;
}

static bool rectIs4(CxUiRect r, int32 x, int32 y, int32 w, int32 h)
{
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

// A window that fits its content is as wide as the label's columns and as tall as its lines at
// that width; widened by the user, the label takes the width and needs fewer lines, the widget
// below it moves up, and the window's minimum height follows its width.
UI_TEST(wrapcolumn)
{
    cxuiTestSetSystemScale(1.0f);
    CxUiBox* col    = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
    CxUiLabel* l    = put(col, wrapLabel(WRAP_TEXT, 10));
    CxUiCanvas* cv  = put(col, fixedCanvas(50, 20));
    CxUiWindow* w   = cxuiWindowCreate(NULL);
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);
    CxUiSize sz;
    CxUiRect lb, cb;
    cxuiWindowSize(w, &sz);
    cxuiTestBounds(CxUiWidget(l), &lb);
    cxuiTestBounds(CxUiWidget(cv), &cb);
    if (sz.w != 70 || sz.h != 104 || !rectIs4(lb, 0, 0, 70, 80) || cb.y != 84)
        TEST_FAIL(1, _SL("fitted: window ${int}x${int} (want 70x104), label ${int}x${int} (want 70x80), canvas at ${int} (want 84)"),
                  stvar(int32, sz.w), stvar(int32, sz.h), stvar(int32, lb.w), stvar(int32, lb.h), stvar(int32, cb.y));

    struct {
        int32 width, lines;
    } widths[] = { { 200, 1 }, { 100, 3 } };
    for (int i = 0; i < 2; i++) {
        cxuiTestResize(w, widths[i].width, 104);
        settle(w);
        cxuiTestBounds(CxUiWidget(l), &lb);
        cxuiTestBounds(CxUiWidget(cv), &cb);
        CxUiTestWindowInfo wi;
        cxuiTestWindowInfo(w, &wi);
        int32 h = widths[i].lines * 16;
        if (lb.w != widths[i].width || lb.h != h || cb.y != h + 4 || wi.minSize.w != 70 || wi.minSize.h != h + 24)
            TEST_FAIL(1, _SL("${int} wide: label ${int}x${int} (want ${int}x${int}), canvas at ${int}, minimum ${int}x${int} (want 70x${int})"),
                      stvar(int32, widths[i].width), stvar(int32, lb.w), stvar(int32, lb.h), stvar(int32, widths[i].width),
                      stvar(int32, h), stvar(int32, cb.y), stvar(int32, wi.minSize.w), stvar(int32, wi.minSize.h),
                      stvar(int32, h + 24));
    }

    CxUiTestControlInfo li = controlInfo(l);
    bool wraps             = li.wrap && li.ellipsis == CXUI_ELLIPSIS_None;
    cxuiTestControlInfoDestroy(&li);
    if (!wraps)
        TEST_FAIL(1, _SL("the label's peer wasn't told to wrap"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);

    // A wider widget makes the window wider than the label's columns: the label takes that width,
    // and the window is only as tall as one line needs, not the five its columns would.
    col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
    l   = put(col, wrapLabel(WRAP_TEXT, 10));
    put(col, fixedCanvas(200, 20));
    w = cxuiWindowCreate(NULL);
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);
    cxuiWindowSize(w, &sz);
    cxuiTestBounds(CxUiWidget(l), &lb);
    CxUiTestWindowInfo wi;
    cxuiTestWindowInfo(w, &wi);
    if (sz.w != 200 || sz.h != 40 || lb.w != 200 || lb.h != 16 || wi.created.h != 40)
        TEST_FAIL(1, _SL("beside a wider widget: window ${int}x${int} (want 200x40, created ${int} tall), label ${int}x${int}"),
                  stvar(int32, sz.w), stvar(int32, sz.h), stvar(int32, wi.created.h), stvar(int32, lb.w), stvar(int32, lb.h));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Dragged narrower than its content, a window that may shrink gives its label less width, and a
// word wider than that is split across lines.
UI_TEST(wrapshrink)
{
    cxuiTestSetSystemScale(1.0f);
    CxUiBox* col   = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V });
    CxUiLabel* l   = put(col, wrapLabel(_S"aaaaaaaaaaaa bb", 0));
    CxUiCanvas* cv = put(col, fixedCanvas(10, 10));
    CxUiWindow* w  = cxuiWindowCreate(&(CxUiWindowDesc){ .flags = CXUI_WINDOW_Shrink });
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);
    CxUiRect lb, cb;
    cxuiTestBounds(CxUiWidget(l), &lb);
    bool fitted = lb.w == 120 && lb.h == 16;
    // 96 across for the long word: three lines of 40, the last 16 wide, with room for " bb"
    cxuiTestResize(w, 40, 200);
    settle(w);
    cxuiTestBounds(CxUiWidget(l), &lb);
    cxuiTestBounds(CxUiWidget(cv), &cb);
    if (!fitted || lb.w != 40 || lb.h != 48 || cb.y != 48)
        TEST_FAIL(1, _SL("fitted ${int}; 40 wide: label ${int}x${int} (want 40x48), canvas at ${int}"), stvar(int32, fitted),
                  stvar(int32, lb.w), stvar(int32, lb.h), stvar(int32, cb.y));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static CxUiWindow* wrapWindow(int32 width, CxUiWidget* content)
{
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { width, 0 }, .sizing = CXUI_SIZE_Start });
    cxuiContainerAppend(w, content);
    objRelease(&content);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);
    return w;
}

// In a row, a grid and a split, a wrapped label is measured at the width it is given there, and
// the window starts as tall as that makes it. The labels' 10 columns keep the natural widths
// inside the windows, which can't be narrower.
UI_TEST(wrapcontainers)
{
    cxuiTestSetSystemScale(1.0f);
    CxUiSize sz;
    CxUiRect lb;

    CxUiBox* row  = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 4 });
    put(row, fixedCanvas(50, 20));
    CxUiLabel* l  = put(row, wrapLabel(WRAP_TEXT, 10));
    CxUiWindow* w = wrapWindow(154, CxUiWidget(row));
    cxuiWindowSize(w, &sz);
    cxuiTestBounds(CxUiWidget(l), &lb);
    CxUiTestWindowInfo wi;
    cxuiTestWindowInfo(w, &wi);
    if (!rectIs4(lb, 54, 0, 100, 48) || sz.h != 48 || wi.created.h != 48)
        TEST_FAIL(1, _SL("row: label at ${int} ${int}x${int} (want 54, 100x48), window ${int} tall, created ${int} tall"),
                  stvar(int32, lb.x), stvar(int32, lb.w), stvar(int32, lb.h), stvar(int32, sz.h), stvar(int32, wi.created.h));
    // Taller, the row has room to spare, and the label still takes only what its lines need.
    cxuiTestResize(w, 154, 100);
    settle(w);
    cxuiTestBounds(CxUiWidget(l), &lb);
    if (lb.h != 48)
        TEST_FAIL(1, _SL("row 100 tall: label ${int} tall (want 48)"), stvar(int32, lb.h));
    cxuiDestroy(w);
    objRelease(&w);

    CxUiGridBox* grid = cxuiGridBoxCreate(&(CxUiGridBoxDesc){ .ncols = 2, .colGap = 4, .rowGap = 2 });
    put(grid, fixedCanvas(30, 20));
    l                  = put(grid, wrapLabel(WRAP_TEXT, 10));
    CxUiCanvas* second = put(grid, fixedCanvas(30, 10));
    w                  = wrapWindow(134, CxUiWidget(grid));
    cxuiWindowSize(w, &sz);
    cxuiTestBounds(CxUiWidget(l), &lb);
    CxUiRect sb;
    cxuiTestBounds(CxUiWidget(second), &sb);
    if (!rectIs4(lb, 34, 0, 100, 48) || sz.h != 48 + 2 + 10 || sb.y != 50)
        TEST_FAIL(1, _SL("grid: label at ${int} ${int}x${int} (want 34, 100x48), second row at ${int} (want 50), window ${int} tall (want 60)"),
                  stvar(int32, lb.x), stvar(int32, lb.w), stvar(int32, lb.h), stvar(int32, sb.y), stvar(int32, sz.h));
    cxuiDestroy(w);
    objRelease(&w);

    CxUiSplit* split = cxuiSplitCreate(&(CxUiSplitDesc){ .value = 0.5f });
    put(split, wrapLabel(WRAP_TEXT, 10));
    l = put(split, wrapLabel(_S"aaaa", 0));
    w = wrapWindow(205, CxUiWidget(split));
    cxuiWindowSize(w, &sz);
    cxuiTestBounds(CxUiWidget(l), &lb);
    if (lb.w != 100 || sz.h != 48)
        TEST_FAIL(1, _SL("split: second pane ${int} wide (want 100), window ${int} tall (want 48)"), stvar(int32, lb.w),
                  stvar(int32, sz.h));
    cxuiDestroy(w);
    objRelease(&w);

    CxUiZBox* pages = cxuiZBoxCreate(NULL);
    put(pages, wrapLabel(WRAP_TEXT, 10));
    w = wrapWindow(100, CxUiWidget(pages));
    cxuiWindowSize(w, &sz);
    if (sz.h != 48)
        TEST_FAIL(1, _SL("zbox: window ${int} tall (want 48)"), stvar(int32, sz.h));
    cxuiDestroy(w);
    objRelease(&w);

    // A container of a fixed height keeps it, whatever its label needs.
    CxUiBox* outer = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V });
    CxUiBox* inner = put(outer, cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V }));
    cxuiWidgetSetNaturalSize(inner, cxuiLen(0), cxuiLen(30));
    put(inner, wrapLabel(WRAP_TEXT, 10));
    CxUiCanvas* below = put(outer, fixedCanvas(10, 10));
    w                 = wrapWindow(100, CxUiWidget(outer));
    CxUiRect bb;
    cxuiTestBounds(CxUiWidget(below), &bb);
    if (bb.y != 30)
        TEST_FAIL(1, _SL("under a fixed height of 30, the next widget is at ${int}"), stvar(int32, bb.y));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// ---- check boxes and radio buttons ----

typedef struct ChangeLog {
    int32 n;
    CxUiWidget* last;   // the widget whose handler ran last
    uint32 state;       // a check box's state as its handler saw it
} ChangeLog;

static CxUiResult choiceChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    ChangeLog* log = stvlAtPtr(cvars, 0);
    log->n++;
    log->last = ev->base.current;
    CxUiCheck* c = objDynCast(CxUiCheck, ev->base.current);
    if (c)
        log->state = cxuiCheckState(c);
    return CXUI_Default;
}

static uint32 peerCheck(void* w)
{
    CxUiTestControlInfo info = controlInfo(w);
    uint32 st                = info.checkState;
    cxuiTestControlInfoDestroy(&info);
    return st;
}

// A check box's title and state reach its peer before and after realizing, and setting them
// raises no change; clicks, Space-free presses and its mnemonic go off, on and back (through mixed
// when three-state, and from mixed to on otherwise), each with a change; disabled or hidden it
// takes none; its title is in the palette's text color, disabled natively in the disabled one.
UI_TEST(checkbasic)
{
    ChangeLog log    = { 0 };
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiCheck* a     = put(col, cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"&Wrap", .state = CXUI_CHECK_On }));
    CxUiCheck* three = put(col, cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"Three", .threeState = true }));
    CxUiCheck* plain = put(col, cxuiCheckCreate(&(CxUiCheckDesc){ .title = _S"Wrap" }));
    CxUiTextField* f = put(col, cxuiTextFieldCreate(NULL));
    cxuiOnChange(a, choiceChange, stvar(ptr, &log));
    cxuiOnChange(three, choiceChange, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    settle(w);

    CxUiTestControlInfo ai = controlInfo(a);
    CxUiPalette* pal       = cxuiWidgetPalette(a);
    bool ok = strEq(ai.text, _S"&Wrap") && ai.checkState == CXUI_CHECK_On && ai.enabled && ai.bounds.w > 0 &&
              ai.color.bits == cxuiPaletteColor(pal, CXUI_PAL_Text).bits && cxuiCheckValue(a);
    cxuiTestControlInfoDestroy(&ai);
    if (!ok)
        TEST_FAIL(1, _SL("the check box's peer didn't get its title, state or color"), stvNone);
    CxUiSize an = cxuiNaturalSize(a), pn = cxuiNaturalSize(plain);
    if (an.w != pn.w || an.h != pn.h || an.w <= cxuiNaturalSize(f).h)
        TEST_FAIL(1, _SL("\"&Wrap\" measures ${int}x${int}, \"Wrap\" ${int}x${int}"), stvar(int32, an.w),
                  stvar(int32, an.h), stvar(int32, pn.w), stvar(int32, pn.h));

    cxuiCheckSetValue(a, false);
    cxuiCheckSetState(three, CXUI_CHECK_Mixed);
    cxuiCheckSetTitle(a, _S"&Lines");
    cxuiCheckSetState(a, 7);
    if (log.n != 0 || peerCheck(a) != CXUI_CHECK_Off || peerCheck(three) != CXUI_CHECK_Mixed ||
        cxuiCheckState(a) != CXUI_CHECK_Off || cxuiCheckValue(three))
        TEST_FAIL(1, _SL("set from code: ${int} changes, peers show ${uint} and ${uint}"), stvar(int32, log.n),
                  stvar(uint32, peerCheck(a)), stvar(uint32, peerCheck(three)));

    // Two-state: off, on, off; from mixed, on.
    static const uint32 twoWant[]   = { CXUI_CHECK_On, CXUI_CHECK_Off };
    static const uint32 threeWant[] = { CXUI_CHECK_Off, CXUI_CHECK_On, CXUI_CHECK_Mixed, CXUI_CHECK_Off };
    for (int32 i = 0; i < 2; i++) {
        cxuiTestClick(CxUiWidget(a));
        if (log.n != i + 1 || log.last != CxUiWidget(a) || log.state != twoWant[i] || peerCheck(a) != twoWant[i])
            TEST_FAIL(1, _SL("click ${int}: ${int} changes, state ${uint} seen, ${uint} shown"), stvar(int32, i + 1),
                      stvar(int32, log.n), stvar(uint32, log.state), stvar(uint32, peerCheck(a)));
    }
    cxuiCheckSetState(a, CXUI_CHECK_Mixed);
    cxuiCheckPress(a);
    if (log.n != 3 || cxuiCheckState(a) != CXUI_CHECK_On)
        TEST_FAIL(1, _SL("pressed from mixed, a two-state box went to ${uint}"), stvar(uint32, cxuiCheckState(a)));
    for (int32 i = 0; i < 4; i++) {
        cxuiTestClick(CxUiWidget(three));
        if (log.state != threeWant[i] || peerCheck(three) != threeWant[i])
            TEST_FAIL(1, _SL("three-state click ${int}: ${uint}, want ${uint}"), stvar(int32, i + 1),
                      stvar(uint32, log.state), stvar(uint32, threeWant[i]));
    }

    // Alt with its mnemonic changes it and gives it focus.
    log.n = 0;
    cxuiFocus(f);
    pressKey(f, CXUI_KEY_L, CXUI_MOD_Alt);
    if (log.n != 1 || cxuiCheckValue(a) || cxuiFocused(w) != CxUiWidget(a))
        TEST_FAIL(1, _SL("Alt+L: ${int} changes, checked ${int}, focused ${int}"), stvar(int32, log.n),
                  stvar(int32, cxuiCheckValue(a)), stvar(int32, cxuiFocused(w) == CxUiWidget(a)));

    cxuiWidgetSetEnabled(col, false);
    settle(w);
    ai = controlInfo(a);
    ok = !ai.enabled && ai.color.bits == cxuiPaletteColor(pal, CXUI_PAL_TextDisabled).bits;
    cxuiTestControlInfoDestroy(&ai);
    cxuiTestClick(CxUiWidget(a));
    cxuiCheckPress(a);
    cxuiWidgetSetEnabled(col, true);
    cxuiWidgetSetVisible(a, false);
    settle(w);
    cxuiTestClick(CxUiWidget(a));
    cxuiCheckPress(a);
    if (!ok || log.n != 1 || cxuiCheckValue(a))
        TEST_FAIL(1, _SL("disabled or hidden: native look ${int}, ${int} more changes"), stvar(int32, ok),
                  stvar(int32, log.n - 1));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static CxUiRadio* radioIn(void* box, strref title)
{
    return put(box, cxuiRadioCreate(&(CxUiRadioDesc){ .title = title }));
}

// A group keeps one radio selected, however deep, apart from those of a group inside it; setting
// it from code raises nothing; a click raises one change on the group, none when already
// selected; the arrow keys choose the next or previous radio that takes input, with focus; Tab
// stops only at the selected one; a mnemonic selects and focuses its radio.
UI_TEST(radiogroup)
{
    ChangeLog log         = { 0 }, innerLog = { 0 };
    CxUiBox* col          = cxuiBoxCreateV();
    CxUiTextField* before = put(col, cxuiTextFieldCreate(NULL));
    CxUiRadioGroup* g     = put(col, cxuiRadioGroupCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V }));
    CxUiRadio* r0         = radioIn(g, _S"&Small");
    CxUiBox* nest         = put(g, cxuiBoxCreateH());
    CxUiRadio* r1         = radioIn(nest, _S"&Medium");
    CxUiRadioGroup* inner = put(g, cxuiRadioGroupCreate(NULL));
    CxUiRadio* i0         = radioIn(inner, _S"Inner 0");
    CxUiRadio* i1         = radioIn(inner, _S"Inner 1");
    CxUiRadio* r2         = radioIn(g, _S"&Large");
    CxUiTextField* after  = put(col, cxuiTextFieldCreate(NULL));
    cxuiOnChange(g, choiceChange, stvar(ptr, &log));
    cxuiOnChange(inner, choiceChange, stvar(ptr, &innerLog));
    cxuiRadioGroupSetIndex(g, 2);   // before realizing
    cxuiRadioGroupSetValue(inner, i1);
    CxUiWindow* w = startWindow(300, 300, CxUiWidget(col));
    settle(w);

    if (cxuiRadioGroupValue(g) != r2 || cxuiRadioGroupIndex(g) != 2 || peerCheck(r2) != CXUI_CHECK_On ||
        peerCheck(r0) != CXUI_CHECK_Off || peerCheck(i1) != CXUI_CHECK_On || cxuiRadioGroup(r1) != g ||
        cxuiRadioGroup(i0) != inner)
        TEST_FAIL(1, _SL("set before realizing: index ${int}, peers ${uint} ${uint}"), stvar(int32, cxuiRadioGroupIndex(g)),
                  stvar(uint32, peerCheck(r2)), stvar(uint32, peerCheck(i1)));
    cxuiRadioGroupSetValue(g, r1);
    cxuiRadioGroupSetValue(g, i0);   // not one of its own
    if (log.n != 0 || !cxuiRadioSelected(r1) || cxuiRadioSelected(r2) || peerCheck(r2) != CXUI_CHECK_Off ||
        !cxuiRadioSelected(i1))
        TEST_FAIL(1, _SL("set from code: ${int} changes, or the wrong radios selected"), stvar(int32, log.n));

    cxuiTestClick(CxUiWidget(r0));
    cxuiTestClick(CxUiWidget(r0));
    if (log.n != 1 || log.last != CxUiWidget(g) || cxuiRadioGroupValue(g) != r0 || peerCheck(r1) != CXUI_CHECK_Off ||
        peerCheck(r0) != CXUI_CHECK_On || !cxuiRadioSelected(i1) || innerLog.n != 0)
        TEST_FAIL(1, _SL("clicks: ${int} changes on the group, ${int} inside it, index ${int}"), stvar(int32, log.n),
                  stvar(int32, innerLog.n), stvar(int32, cxuiRadioGroupIndex(g)));
    cxuiTestClick(CxUiWidget(i0));
    if (log.n != 1 || innerLog.n != 1 || !cxuiRadioSelected(r0) || !cxuiRadioSelected(i0))
        TEST_FAIL(1, _SL("a click in the inner group changed the outer one"), stvNone);

    // Tab goes from the field before to the selected radio, then on past the group's others.
    cxuiFocus(before);
    pressKey(before, CXUI_KEY_Tab, 0);
    CxUiWidget* f1 = cxuiFocused(w);
    pressKey(f1, CXUI_KEY_Tab, 0);
    CxUiWidget* f2 = cxuiFocused(w);
    if (f1 != CxUiWidget(r0) || f2 != CxUiWidget(i0))
        TEST_FAIL(1, _SL("Tab from before the group stopped at ${string} then ${string}"),
                  stvar(strref, f1 ? f1->name : NULL), stvar(strref, f2 ? f2->name : NULL));

    // Down from the first skips a disabled one; Up from the first wraps to the last.
    cxuiWidgetSetEnabled(r1, false);
    settle(w);
    cxuiFocus(r0);
    pressKey(r0, CXUI_KEY_Down, 0);
    if (cxuiRadioGroupValue(g) != r2 || cxuiFocused(w) != CxUiWidget(r2) || log.n != 2)
        TEST_FAIL(1, _SL("Down: index ${int}, ${int} changes"), stvar(int32, cxuiRadioGroupIndex(g)), stvar(int32, log.n));
    // With a modifier, an arrow isn't the radio's.
    CxUiResult ctrl = cxuiTestKey(CxUiWidget(r2), CXUI_KEY_Down, CXUI_MOD_Ctrl, true);
    cxuiTestKey(CxUiWidget(r2), CXUI_KEY_Down, CXUI_MOD_Ctrl, false);
    if (ctrl != CXUI_Default || cxuiRadioGroupValue(g) != r2 || log.n != 2)
        TEST_FAIL(1, _SL("Ctrl+Down was taken by a radio, or chose another"), stvNone);
    pressKey(r2, CXUI_KEY_Right, 0);
    pressKey(r0, CXUI_KEY_Up, 0);
    if (cxuiRadioGroupValue(g) != r2 || cxuiFocused(w) != CxUiWidget(r2) || log.n != 4 || !cxuiRadioSelected(i0))
        TEST_FAIL(1, _SL("Right then Up: index ${int}, ${int} changes"), stvar(int32, cxuiRadioGroupIndex(g)),
                  stvar(int32, log.n));
    cxuiWidgetSetEnabled(r1, true);
    settle(w);

    // With none selected, each is a stop.
    cxuiRadioGroupSetIndex(g, -1);
    if (!cxuiWidgetCanFocus(r0) || !cxuiWidgetCanFocus(r1) || cxuiRadioGroupIndex(g) != -1)
        TEST_FAIL(1, _SL("with none selected, a radio isn't a Tab stop"), stvNone);
    cxuiFocus(after);
    pressKey(after, CXUI_KEY_M, CXUI_MOD_Alt);
    if (cxuiRadioGroupValue(g) != r1 || cxuiFocused(w) != CxUiWidget(r1) || log.n != 5)
        TEST_FAIL(1, _SL("Alt+M: index ${int}, focused ${int}"), stvar(int32, cxuiRadioGroupIndex(g)),
                  stvar(int32, cxuiFocused(w) == CxUiWidget(r1)));
    if (cxuiWidgetCanFocus(r0))
        TEST_FAIL(1, _SL("an unselected radio is a Tab stop"), stvNone);

    // On its own, a radio selects only itself.
    CxUiRadio* lone = radioIn(col, _S"Lone");
    settle(w);
    cxuiTestClick(CxUiWidget(lone));
    if (!cxuiRadioSelected(lone) || cxuiRadioGroup(lone) || peerCheck(lone) != CXUI_CHECK_On)
        TEST_FAIL(1, _SL("a radio outside any group wasn't selected by a click"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// ---- lists and dropdowns ----

typedef struct ListLog {
    int32 changes;
    int32 activates;
    uint64 item;   // the last activate's
} ListLog;

static CxUiResult listChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((ListLog*)stvlAtPtr(cvars, 0))->changes++;
    return CXUI_Default;
}

static CxUiResult listActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    ListLog* log = stvlAtPtr(cvars, 0);
    log->activates++;
    log->item = ev->item;
    return CXUI_Default;
}

// The peer's items and selection match what was asked for
static bool peerItems(void* w, strref items, int32 value)
{
    CxUiTestControlInfo info = controlInfo(w);
    bool ok                  = strEq(info.items, items) && info.value == value;
    if (!ok)
        logFmt(Info, _SL("peer has \"${string}\", selected ${int}"), stvar(strref, info.items), stvar(int32, info.value));
    cxuiTestControlInfoDestroy(&info);
    return ok;
}

// Items and a selection given before realizing reach the peer, and every later change does, the
// selection staying on its item; code raises no change, the user's choice does (once), and a
// double click or Enter activates the selected item; disabled, the user can't choose; the colors
// are a field's; more visible items make it taller, and wider items wider.
UI_TEST(listbasic)
{
    static const strref abc[] = { _S"alpha", _S"bravo", _S"charlie", NULL };
    ListLog log               = { 0 };
    CxUiBox* col              = cxuiBoxCreateV();
    CxUiList* l     = put(col, cxuiListCreate(&(CxUiListDesc){ .items = abc, .value = 1, .name = _S"list" }));
    CxUiList* tall  = put(col, cxuiListCreate(&(CxUiListDesc){ .items = abc, .visibleItems = 10 }));
    CxUiList* empty = put(col, cxuiListCreate(&(CxUiListDesc){ .value = 3 }));
    cxuiOnChange(l, listChange, stvar(ptr, &log));
    cxuiOnActivate(l, listActivate, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 400, CxUiWidget(col));
    settle(w);

    if (!peerItems(l, _S"alpha\nbravo\ncharlie\n", 1) || cxuiListValue(l) != 1 || cxuiListCount(l) != 3 ||
        !strEq(cxuiListItem(l, 2), _S"charlie") || cxuiListItem(l, 3) || cxuiListValue(empty) != -1)
        TEST_FAIL(1, _SL("items or selection set before realizing didn't reach the peer"), stvNone);
    CxUiSize ln = cxuiNaturalSize(l), tn = cxuiNaturalSize(tall), en = cxuiNaturalSize(empty);
    if (tn.h <= ln.h || tn.w != ln.w || en.w < ln.w / 2)
        TEST_FAIL(1, _SL("5 items high ${int}x${int}, 10 high ${int}x${int}, empty ${int} wide"), stvar(int32, ln.w),
                  stvar(int32, ln.h), stvar(int32, tn.w), stvar(int32, tn.h), stvar(int32, en.w));

    // The selection follows its item.
    cxuiListInsert(l, 0, _S"zero");
    if (!peerItems(l, _S"zero\nalpha\nbravo\ncharlie\n", 2) || cxuiListValue(l) != 2)
        TEST_FAIL(1, _SL("inserted before the selection, it is at ${int}"), stvar(int32, cxuiListValue(l)));
    cxuiListAppend(l, _S"a much wider item than the others");
    cxuiListSetItem(l, 2, _S"BRAVO");
    cxuiListRemove(l, 1);
    if (!peerItems(l, _S"zero\nBRAVO\ncharlie\na much wider item than the others\n", 1))
        TEST_FAIL(1, _SL("append, change and remove"), stvNone);
    settle(w);
    if (cxuiNaturalSize(l).w <= ln.w)
        TEST_FAIL(1, _SL("a wider item didn't make the list wider"), stvNone);
    cxuiListRemove(l, 1);
    if (!peerItems(l, _S"zero\ncharlie\na much wider item than the others\n", -1) || cxuiListValue(l) != -1)
        TEST_FAIL(1, _SL("removing the selected item left ${int} selected"), stvar(int32, cxuiListValue(l)));
    cxuiListSetValue(l, 2);
    cxuiListSetValue(l, 7);
    if (log.changes != 0 || !peerItems(l, _S"zero\ncharlie\na much wider item than the others\n", -1))
        TEST_FAIL(1, _SL("set from code: ${int} changes"), stvar(int32, log.changes));

    // The user's choice
    cxuiTestChoose(CxUiWidget(l), 1, false);
    cxuiTestChoose(CxUiWidget(l), 1, false);
    if (log.changes != 1 || cxuiListValue(l) != 1 || log.activates != 0)
        TEST_FAIL(1, _SL("chosen twice: ${int} changes, selected ${int}"), stvar(int32, log.changes),
                  stvar(int32, cxuiListValue(l)));
    cxuiTestChoose(CxUiWidget(l), 0, true);
    if (log.changes != 2 || log.activates != 1 || log.item != 0)
        TEST_FAIL(1, _SL("a double click: ${int} changes, ${int} activates, item ${uint}"), stvar(int32, log.changes),
                  stvar(int32, log.activates), stvar(uint64, log.item));
    cxuiFocus(l);
    cxuiTestKey(CxUiWidget(l), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(l), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(l), CXUI_KEY_Enter, 0, false);
    if (log.activates != 2 || log.item != 0)
        TEST_FAIL(1, _SL("Enter, held: ${int} activates"), stvar(int32, log.activates - 1));
    cxuiListSetValue(l, -1);
    CxUiResult r = cxuiTestKey(CxUiWidget(l), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(l), CXUI_KEY_Enter, 0, false);
    if (r != CXUI_Default || log.activates != 2)
        TEST_FAIL(1, _SL("Enter with nothing selected was taken"), stvNone);

    sa_string fresh;
    saInit(&fresh, string, 2);
    saPush(&fresh, strref, _S"one");
    saPush(&fresh, strref, _S"two");
    cxuiListSetValue(l, 1);
    cxuiListSetItems(l, fresh);
    saDestroy(&fresh);
    if (!peerItems(l, _S"one\ntwo\n", -1))
        TEST_FAIL(1, _SL("set as a whole"), stvNone);

    CxUiPalette* pal       = cxuiWidgetPalette(l);
    CxUiTestControlInfo li = controlInfo(l);
    bool colored           = li.color.bits == cxuiPaletteColor(pal, CXUI_PAL_InputText).bits;
    cxuiTestControlInfoDestroy(&li);
    cxuiWidgetSetEnabled(l, false);
    settle(w);
    li          = controlInfo(l);
    colored     = colored && li.color.bits == cxuiPaletteColor(pal, CXUI_PAL_TextDisabled).bits && !li.enabled;
    cxuiTestControlInfoDestroy(&li);
    cxuiTestChoose(CxUiWidget(l), 0, true);
    if (!colored || log.changes != 2 || log.activates != 2)
        TEST_FAIL(1, _SL("disabled: colors ${int}, ${int} changes"), stvar(int32, colored), stvar(int32, log.changes));
    cxuiListClear(l);
    if (!peerItems(l, NULL, -1) || cxuiListCount(l) != 0)
        TEST_FAIL(1, _SL("cleared, the peer still has items"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// A dropdown's items and selection reach its peer before and after realizing; the user's choice
// raises a change, code doesn't; Enter goes to the window's default; it is one line high.
UI_TEST(dropdownbasic)
{
    static const strref sizes[] = { _S"Small", _S"Medium", _S"Large", NULL };
    ListLog log                 = { 0 };
    PressLog presses            = { 0 };
    CxUiBox* col                = cxuiBoxCreateV();
    CxUiDropdown* d  = put(col, cxuiDropdownCreate(&(CxUiDropdownDesc){ .items = sizes }));
    CxUiTextField* f = put(col, cxuiTextFieldCreate(NULL));
    CxUiButton* ok   = logButton(col, _S"OK", &presses, 1);
    cxuiOnChange(d, listChange, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    cxuiWindowSetDefaultEnter(w, ok);
    settle(w);
    if (!peerItems(d, _S"Small\nMedium\nLarge\n", 0) || cxuiDropdownValue(d) != 0)
        TEST_FAIL(1, _SL("a dropdown's items or first selection didn't reach the peer"), stvNone);
    if (cxuiNaturalSize(d).h > cxuiNaturalSize(f).h * 2)
        TEST_FAIL(1, _SL("a dropdown is ${int} high, a field ${int}"), stvar(int32, cxuiNaturalSize(d).h),
                  stvar(int32, cxuiNaturalSize(f).h));
    cxuiDropdownSetValue(d, 2);
    cxuiDropdownRemove(d, 0);
    cxuiDropdownAppend(d, _S"Huge");
    if (log.changes != 0 || !peerItems(d, _S"Medium\nLarge\nHuge\n", 1) || !strEq(cxuiDropdownItem(d, 2), _S"Huge"))
        TEST_FAIL(1, _SL("changed from code: ${int} changes"), stvar(int32, log.changes));
    cxuiTestChoose(CxUiWidget(d), 2, false);
    if (log.changes != 1 || cxuiDropdownValue(d) != 2)
        TEST_FAIL(1, _SL("the user's choice: ${int} changes, value ${int}"), stvar(int32, log.changes),
                  stvar(int32, cxuiDropdownValue(d)));
    cxuiFocus(d);
    pressKey(d, CXUI_KEY_Enter, 0);
    if (presses.presses[0] != 1)
        TEST_FAIL(1, _SL("Enter on a dropdown didn't press the default"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// The peer's selection where any number can be selected: the positions, each with a space after
static bool peerMarks(void* w, strref marks)
{
    CxUiTestControlInfo info = controlInfo(w);
    bool ok                  = strEq(info.selection, marks);
    if (!ok)
        logFmt(Info, _SL("peer has \"${string}\" selected"), stvar(strref, info.selection));
    cxuiTestControlInfoDestroy(&info);
    return ok;
}

// The model's selection, in the same form
static bool listMarks(CxUiList* l, strref marks)
{
    string have = 0, at = 0;
    for (int32 i = cxuiListNextSelected(l, -1); i >= 0; i = cxuiListNextSelected(l, i)) {
        strFormat(&at, _S"${int} ", stvar(int32, i));
        strAppend(&have, at);
    }
    bool ok = strEq(have, marks);
    if (!ok)
        logFmt(Info, _SL("the list has \"${string}\" selected"), stvar(strref, have));
    strDestroy(&have);
    strDestroy(&at);
    return ok && peerMarks(l, marks);
}

// Where any number can be selected: the selection set from code reaches the platform without an
// event, follows its items as items come and go, and its first item is the value; the user's
// click selects one item alone and a Ctrl-click adds or takes one away, each with one change;
// disabled, neither does anything. Where one can be, the same calls keep to one.
UI_TEST(listmulti)
{
    static const strref five[] = { _S"a", _S"b", _S"c", _S"d", _S"e", NULL };
    ListLog log                = { 0 };
    CxUiBox* col               = cxuiBoxCreateV();
    CxUiList* l = put(col, cxuiListCreate(&(CxUiListDesc){ .items = five, .value = 1, .select = CXUI_SELECT_Multi }));
    CxUiList* one = put(col, cxuiListCreate(&(CxUiListDesc){ .items = five }));
    cxuiOnChange(l, listChange, stvar(ptr, &log));
    cxuiOnActivate(l, listActivate, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 400, CxUiWidget(col));
    settle(w);
    if (!listMarks(l, _S"1 ") || cxuiListValue(l) != 1 || cxuiListSelectedCount(l) != 1)
        TEST_FAIL(1, _SL("the first selection didn't reach the platform"), stvNone);

    cxuiListSetSelected(l, 2, 2, true);
    cxuiListSetSelected(l, 4, 9, true);
    cxuiListSetSelected(l, 4, 1, false);
    if (!listMarks(l, _S"1 2 3 ") || cxuiListValue(l) != 1 || cxuiListSelectedCount(l) != 3 ||
        !cxuiListIsSelected(l, 3) || cxuiListIsSelected(l, 0) || cxuiListIsSelected(l, 9))
        TEST_FAIL(1, _SL("selected from code: ${int} of them"), stvar(int32, cxuiListSelectedCount(l)));
    cxuiListInsert(l, 0, _S"z");
    cxuiListRemove(l, 3);
    if (!listMarks(l, _S"2 3 ") || cxuiListValue(l) != 2 || !peerItems(l, _S"z\na\nb\nd\ne\n", -1))
        TEST_FAIL(1, _SL("the selection didn't follow its items"), stvNone);
    cxuiListRemove(l, 0);
    if (!listMarks(l, _S"1 2 ") || cxuiListValue(l) != 1)
        TEST_FAIL(1, _SL("removing an item before the selection: value ${int}"), stvar(int32, cxuiListValue(l)));
    cxuiListInsert(l, 0, _S"z");
    cxuiListSelectAll(l);
    if (!listMarks(l, _S"0 1 2 3 4 ") || cxuiListValue(l) != 0)
        TEST_FAIL(1, _SL("select all"), stvNone);
    cxuiListSetValue(l, 3);
    if (!listMarks(l, _S"3 ") || log.changes != 0)
        TEST_FAIL(1, _SL("a value set selects that item alone, without an event"), stvNone);
    cxuiListClearSelection(l);
    if (!listMarks(l, _S"") || cxuiListValue(l) != -1)
        TEST_FAIL(1, _SL("clearing the selection"), stvNone);

    cxuiTestChoose(CxUiWidget(l), 1, false);
    cxuiTestToggle(l, 4);
    if (!listMarks(l, _S"1 4 ") || log.changes != 2 || cxuiListValue(l) != 1)
        TEST_FAIL(1, _SL("a click and a Ctrl-click: ${int} changes"), stvar(int32, log.changes));
    cxuiTestToggle(l, 1);
    cxuiTestChoose(CxUiWidget(l), 2, true);
    cxuiTestChoose(CxUiWidget(l), 2, false);
    if (!listMarks(l, _S"2 ") || log.changes != 4 || log.activates != 1 || log.item != 2)
        TEST_FAIL(1, _SL("a Ctrl-click off, a double click and the same click again: ${int} changes, ${int} activates"),
                  stvar(int32, log.changes), stvar(int32, log.activates));
    cxuiWidgetSetEnabled(l, false);
    cxuiTestToggle(l, 0);
    cxuiTestChoose(CxUiWidget(l), 0, false);
    cxuiWidgetSetEnabled(l, true);
    if (!listMarks(l, _S"2 ") || log.changes != 4)
        TEST_FAIL(1, _SL("a disabled list took the user's selection"), stvNone);
    cxuiFocus(l);
    cxuiTestKey(CxUiWidget(l), CXUI_KEY_Enter, 0, true);
    cxuiTestKey(CxUiWidget(l), CXUI_KEY_Enter, 0, false);
    if (log.activates != 2 || log.item != 2)
        TEST_FAIL(1, _SL("Enter didn't open the selected item"), stvNone);

    // One at a time
    cxuiListSetSelected(one, 2, 2, true);
    if (cxuiListValue(one) != 2 || cxuiListSelectedCount(one) != 1 || !cxuiListIsSelected(one, 2) ||
        cxuiListNextSelected(one, 2) != -1 || cxuiListNextSelected(one, -1) != 2 || !peerItems(one, _S"a\nb\nc\nd\ne\n", 2))
        TEST_FAIL(1, _SL("where one can be selected, several were"), stvNone);
    cxuiListSelectAll(one);
    cxuiListSetSelected(one, 0, 2, false);
    if (cxuiListValue(one) != 2 || cxuiTestToggle(one, 1))
        TEST_FAIL(1, _SL("select all, or unselecting others, changed a single selection"), stvNone);
    cxuiListClearSelection(one);
    if (cxuiListValue(one) != -1 || cxuiListSelectedCount(one) != 0)
        TEST_FAIL(1, _SL("clearing a single selection"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Between beginUpdate() and the last endUpdate(), changes stay off the platform and out of layout
// while the list reads back as changed; at the end they reach the platform at once, the top item
// too. The top item set before the list is on screen is where it starts.
UI_TEST(listupdate)
{
    static const strref three[] = { _S"x", _S"y", _S"z", NULL };
    CxUiList* l = cxuiListCreate(&(CxUiListDesc){ .items = three, .select = CXUI_SELECT_Multi });
    cxuiListSetTopItem(l, 2);
    CxUiWindow* w = startWindow(300, 400, CxUiWidget(l));
    settle(w);
    CxUiTestControlInfo info = controlInfo(l);
    int32 top                = info.top;
    cxuiTestControlInfoDestroy(&info);
    if (top != 2 || cxuiListTopItem(l) != 2)
        TEST_FAIL(1, _SL("the top item set before showing: ${int}"), stvar(int32, top));

    int32 passes = cxuiTestLayoutPasses(w);
    cxuiListBeginUpdate(l);
    cxuiListBeginUpdate(l);
    cxuiListClear(l);
    string text = 0;
    for (int32 i = 0; i < 1000; i++) {
        strFormat(&text, _S"item ${int}", stvar(int32, i));
        cxuiListAppend(l, text);
    }
    strDestroy(&text);
    cxuiListSetSelected(l, 10, 3, true);
    cxuiListSetTopItem(l, 500);
    cxuiListEndUpdate(l);
    cxuiFlush();
    if (cxuiListTopItem(l) != 500)
        TEST_FAIL(1, _SL("the top item set while updating reads back as ${int}"), stvar(int32, cxuiListTopItem(l)));
    info = controlInfo(l);
    bool held = strEq(info.items, _S"x\ny\nz\n") && strEq(info.selection, _S"0 ") && info.top == 2 &&
                cxuiTestLayoutPasses(w) == passes;
    cxuiTestControlInfoDestroy(&info);
    if (!held || cxuiListCount(l) != 1000 || !strEq(cxuiListItem(l, 999), _S"item 999") || cxuiListValue(l) != 10)
        TEST_FAIL(1, _SL("changes reached the platform, or layout, before the last end"), stvNone);

    cxuiListEndUpdate(l);
    cxuiListEndUpdate(l);
    cxuiFlush();
    info = controlInfo(l);
    bool shown = info.top == 500 && strBeginsWith(info.items, _S"item 0\nitem 1\n") &&
                 strEndsWith(info.items, _S"item 999\n") && strEq(info.selection, _S"10 11 12 ") &&
                 cxuiTestLayoutPasses(w) == passes + 1;
    cxuiTestControlInfoDestroy(&info);
    if (!shown || cxuiListTopItem(l) != 500)
        TEST_FAIL(1, _SL("after the last end the platform isn't up to date, or laid out ${int} times"),
                  stvar(int32, cxuiTestLayoutPasses(w) - passes));

    // An update that gives the platform every item again keeps the first one it shows.
    cxuiListSetTopItem(l, 400);
    cxuiListBeginUpdate(l);
    cxuiListRemove(l, 450);
    cxuiListEndUpdate(l);
    cxuiFlush();
    info = controlInfo(l);
    top  = info.top;
    cxuiTestControlInfoDestroy(&info);
    if (top != 400 || cxuiListCount(l) != 999)
        TEST_FAIL(1, _SL("an update scrolled the list to ${int}"), stvar(int32, top));
    cxuiListAppend(l, _S"item 999");

    // Without an update, the top item reaches the platform at once, kept within the items.
    cxuiListSetTopItem(l, 5000);
    if (cxuiListTopItem(l) != 999)
        TEST_FAIL(1, _SL("the top item past the end: ${int}"), stvar(int32, cxuiListTopItem(l)));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// A text area's lines reach its peer before and after realizing, and so does text appended, which
// raises no change and leaves the selection alone; scrolling to the end asked before realizing
// happens once it is; it is taller for more lines and grows with its window; Enter neither commits
// nor presses the default, unless it is read-only; leaving it after an edit commits; it works in a
// frame.
UI_TEST(textarea)
{
    TextLog log        = { 0 };
    PressLog presses   = { 0 };
    CxUiBox* col       = cxuiBoxCreateV();
    CxUiTextArea* a    = put(col, cxuiTextAreaCreate(&(CxUiTextAreaDesc){ .text = _S"one\ntwo", .lines = 3 }));
    CxUiTextArea* tall = put(col, cxuiTextAreaCreate(&(CxUiTextAreaDesc){ .lines = 6, .wrap = true, .readOnly = true }));
    CxUiTextField* f   = put(col, cxuiTextFieldCreate(NULL));
    CxUiTextArea* inner = cxuiTextAreaCreate(NULL);
    put(col, cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field = CxUiTextField(inner) }));
    CxUiButton* ok = logButton(col, _S"OK", &presses, 1);
    cxuiOnChange(a, textChanged, stvar(ptr, &log));
    cxuiOnCommit(a, textCommitted, stvar(ptr, &log));
    cxuiTextAreaAppend(tall, _S"first\n");
    cxuiTextAreaScrollToEnd(tall);
    CxUiWindow* w = startWindow(400, 400, CxUiWidget(col));
    cxuiWindowSetDefaultEnter(w, ok);
    settle(w);

    CxUiTestTextInfo ai, ti, ii;
    cxuiTestTextInfo(CxUiTextField(a), &ai);
    cxuiTestTextInfo(CxUiTextField(tall), &ti);
    cxuiTestTextInfo(CxUiTextField(inner), &ii);
    bool ok1 = strEq(ai.text, _S"one\ntwo") && ai.multiline && !ai.wrap && ti.multiline && ti.wrap && ti.atEnd &&
               strEq(ti.text, _S"first\n") && ti.readOnly && ii.multiline && ii.framed && ii.hasColors;
    cxuiTestTextInfoDestroy(&ai);
    cxuiTestTextInfoDestroy(&ti);
    cxuiTestTextInfoDestroy(&ii);
    if (!ok1)
        TEST_FAIL(1, _SL("text, wrap, read-only, a frame or the scroll to the end didn't reach the peers"), stvNone);
    CxUiSize an = cxuiNaturalSize(a), tn = cxuiNaturalSize(tall), fn = cxuiNaturalSize(f);
    if (an.h <= fn.h * 2 || tn.h <= an.h || an.w <= fn.w)
        TEST_FAIL(1, _SL("3 lines ${int}x${int}, 6 lines ${int} high, a field ${int}x${int}"), stvar(int32, an.w),
                  stvar(int32, an.h), stvar(int32, tn.h), stvar(int32, fn.w), stvar(int32, fn.h));
    CxUiRect before, after;
    cxuiTestBounds(CxUiWidget(a), &before);
    cxuiWindowSetSize(w, 500, 600);
    settle(w);
    cxuiTestBounds(CxUiWidget(a), &after);
    if (after.h <= before.h || after.w <= before.w)
        TEST_FAIL(1, _SL("a larger window: the area went from ${int}x${int} to ${int}x${int}"), stvar(int32, before.w),
                  stvar(int32, before.h), stvar(int32, after.w), stvar(int32, after.h));

    cxuiTextFieldSelect(CxUiTextField(a), 1, 2);
    cxuiTextAreaAppend(a, _S"\nthree");
    int32 ss, se;
    cxuiTextFieldSelection(CxUiTextField(a), &ss, &se);
    CHECK_TEXT("appended", CxUiTextField(a), _S"one\ntwo\nthree");
    if (log.changes != 0 || ss != 1 || se != 2)
        TEST_FAIL(1, _SL("appending: ${int} changes, selection ${int}-${int}"), stvar(int32, log.changes),
                  stvar(int32, ss), stvar(int32, se));
    cxuiTextAreaScrollToEnd(a);
    cxuiTestTextInfo(CxUiTextField(a), &ai);
    bool atEnd = ai.atEnd;
    cxuiTestTextInfoDestroy(&ai);
    if (!atEnd)
        TEST_FAIL(1, _SL("scrolling to the end didn't reach the peer"), stvNone);

    cxuiFocus(a);
    cxuiTextFieldSetCaret(CxUiTextField(a), 3);
    cxuiTestType(CxUiTextField(a), _S"\n");
    pressKey(a, CXUI_KEY_Enter, 0);
    if (log.changes != 1 || log.commits != 0 || presses.presses[0] != 0)
        TEST_FAIL(1, _SL("Enter in an area: ${int} changes, ${int} commits, the default pressed ${int} times"),
                  stvar(int32, log.changes), stvar(int32, log.commits), stvar(int32, presses.presses[0]));
    cxuiFocus(f);
    if (log.commits != 1 || !strEq(cxuiTextFieldText(CxUiTextField(a)), _S"one\n\ntwo\nthree"))
        TEST_FAIL(1, _SL("leaving an edited area: ${int} commits"), stvar(int32, log.commits));
    cxuiFocus(tall);
    pressKey(tall, CXUI_KEY_Enter, 0);
    strDestroy(&log.atChange);
    if (presses.presses[0] != 1)
        TEST_FAIL(1, _SL("Enter in a read-only area didn't press the default"), stvNone);
    cxuiDestroy(w);
    objRelease(&inner);
    objRelease(&w);
    return 0;
}

// ---- sliders and progress bars ----

typedef struct SlideLog {
    int32 changes, commits;
    float64 atChange, atCommit;
} SlideLog;

static CxUiResult slideChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    SlideLog* log = stvlAtPtr(cvars, 0);
    log->changes++;
    log->atChange = cxuiSliderValue((CxUiSlider*)ev->base.current);
    return CXUI_Default;
}

static CxUiResult slideCommit(stvlist* cvars, CxUiCommitEvent* ev)
{
    SlideLog* log = stvlAtPtr(cvars, 0);
    log->commits++;
    log->atCommit = cxuiSliderValue((CxUiSlider*)ev->base.current);
    return CXUI_Default;
}

static CxUiTestControlInfo sliderInfo(void* w)
{
    return controlInfo(w);
}

// A slider's range, ticks and value reach its peer as whole positions, before and after
// realizing; values snap to a step and stay in range; dragging raises a change per new value and
// letting go one commit, and none when the value is back where it was; code raises neither; a
// disabled one doesn't move; a vertical one is taller than wide.
UI_TEST(sliderbasic)
{
    SlideLog log  = { 0 };
    CxUiBox* col  = cxuiBoxCreateV();
    CxUiSlider* s = put(col, cxuiSliderCreate(&(CxUiSliderDesc){ .min = 10, .max = 20, .step = 0.5, .value = 13.3,
                                                                  .ticks = 5 }));
    CxUiSlider* d = put(col, cxuiSliderCreate(NULL));
    CxUiSlider* v = put(col, cxuiSliderCreate(&(CxUiSliderDesc){ .orient = CXUI_ORIENT_V, .length = 90 }));
    cxuiOnChange(s, slideChange, stvar(ptr, &log));
    cxuiOnCommit(s, slideCommit, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 400, CxUiWidget(col));
    settle(w);

    CxUiTestControlInfo si = sliderInfo(s), di = sliderInfo(d), vi = sliderInfo(v);
    bool ok = si.range == 20 && si.pos == 7 && si.tickEvery == 5 && !si.vertical && cxuiSliderValue(s) == 13.5 &&
              di.range == 100 && di.pos == 0 && di.tickEvery == 0 && vi.vertical;
    cxuiTestControlInfoDestroy(&si);
    cxuiTestControlInfoDestroy(&di);
    cxuiTestControlInfoDestroy(&vi);
    if (!ok)
        TEST_FAIL(1, _SL("range, ticks or value didn't reach the peers (value ${float})"),
                  stvar(float64, cxuiSliderValue(s)));
    CxUiSize vn = cxuiNaturalSize(v), dn = cxuiNaturalSize(d);
    if (vn.h != 90 || vn.w >= vn.h || dn.w <= dn.h)
        TEST_FAIL(1, _SL("vertical ${int}x${int}, horizontal ${int}x${int}"), stvar(int32, vn.w), stvar(int32, vn.h),
                  stvar(int32, dn.w), stvar(int32, dn.h));

    cxuiSliderSetValue(s, 99);
    si = sliderInfo(s);
    ok = si.pos == 20 && cxuiSliderValue(s) == 20 && log.changes == 0;
    cxuiTestControlInfoDestroy(&si);
    if (!ok)
        TEST_FAIL(1, _SL("set past the end: ${float}"), stvar(float64, cxuiSliderValue(s)));

    // A drag through two values and back to one, then letting go
    cxuiTestSlide(s, 15, false);
    cxuiTestSlide(s, 15.2, false);
    cxuiTestSlide(s, 16, false);
    cxuiTestSlide(s, 16, true);
    if (log.changes != 2 || log.atChange != 16 || log.commits != 1 || log.atCommit != 16)
        TEST_FAIL(1, _SL("a drag: ${int} changes (last ${float}), ${int} commits (${float})"), stvar(int32, log.changes),
                  stvar(float64, log.atChange), stvar(int32, log.commits), stvar(float64, log.atCommit));
    cxuiTestSlide(s, 17, false);
    cxuiTestSlide(s, 16, true);
    if (log.changes != 4 || log.commits != 1)
        TEST_FAIL(1, _SL("let go where it started: ${int} commits"), stvar(int32, log.commits));
    cxuiSliderSetValue(s, 12);
    cxuiTestSlide(s, 12, true);
    if (log.commits != 1)
        TEST_FAIL(1, _SL("let go at a value set from code: ${int} commits"), stvar(int32, log.commits));

    // Defaults, values outside the range, and steps that don't divide it evenly
    cxuiSliderSetValue(d, 50);
    cxuiSliderSetValue(s, -5);
    if (cxuiSliderValue(d) != 50 || cxuiSliderValue(s) != 10)
        TEST_FAIL(1, _SL("the default slider took 50 as ${float}, -5 became ${float}"), stvar(float64, cxuiSliderValue(d)),
                  stvar(float64, cxuiSliderValue(s)));
    cxuiSliderSetRange(s, 0, 1, 0);
    si = sliderInfo(s);
    ok = si.range == 100;
    cxuiTestControlInfoDestroy(&si);
    cxuiSliderSetRange(s, 0, 1, 0.1);
    si = sliderInfo(s);
    ok = ok && si.range == 10;
    cxuiTestControlInfoDestroy(&si);
    cxuiSliderSetRange(s, 0, 10, 4);
    cxuiSliderSetValue(s, 10);
    if (!ok || cxuiSliderValue(s) != 10)
        TEST_FAIL(1, _SL("0 to 1 by default steps or 0.1, or 10 by 4 snapped to ${float}"), stvar(float64, cxuiSliderValue(s)));
    cxuiSliderSetRange(s, 0, 4, 1);
    si = sliderInfo(s);
    ok = si.range == 4 && si.pos == 4 && cxuiSliderValue(s) == 4 && si.tickEvery == 1;
    cxuiTestControlInfoDestroy(&si);
    if (!ok)
        TEST_FAIL(1, _SL("a new range: value ${float}"), stvar(float64, cxuiSliderValue(s)));

    cxuiWidgetSetEnabled(s, false);
    settle(w);
    cxuiTestSlide(s, 1, true);
    if (cxuiSliderValue(s) != 4 || log.changes != 4)
        TEST_FAIL(1, _SL("a disabled slider moved"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// A progress bar's value reaches its peer as a share of its maximum, kept within it, before and
// after realizing; showing activity and a value again switch between them.
UI_TEST(progressbasic)
{
    CxUiBox* col    = cxuiBoxCreateV();
    CxUiProgress* p = put(col, cxuiProgressCreate(&(CxUiProgressDesc){ .value = 30, .max = 60 }));
    CxUiProgress* q = put(col, cxuiProgressCreate(&(CxUiProgressDesc){ .indeterminate = true, .length = 80 }));
    CxUiWindow* w   = startWindow(300, 200, CxUiWidget(col));
    settle(w);
    CxUiTestControlInfo pi = controlInfo(p), qi = controlInfo(q);
    bool ok = pi.pos == 5000 && !pi.indeterminate && qi.indeterminate && cxuiNaturalSize(q).w == 80;
    cxuiTestControlInfoDestroy(&pi);
    cxuiTestControlInfoDestroy(&qi);
    if (!ok)
        TEST_FAIL(1, _SL("value or activity didn't reach the peers"), stvNone);
    cxuiProgressSetValue(p, 90);
    cxuiProgressSetValue(q, 25);
    pi = controlInfo(p);
    qi = controlInfo(q);
    ok = pi.pos == 10000 && cxuiProgressValue(p) == 60 && qi.pos == 2500 && !qi.indeterminate;
    cxuiTestControlInfoDestroy(&pi);
    cxuiTestControlInfoDestroy(&qi);
    if (!ok)
        TEST_FAIL(1, _SL("past the maximum, or a value after activity"), stvNone);
    cxuiProgressSetMax(p, 30);
    ok = cxuiProgressValue(p) == 30;
    cxuiProgressSetMax(p, 60);
    cxuiProgressSetIndeterminate(p, true);
    pi = controlInfo(p);
    ok = ok && pi.indeterminate && pi.pos == 5000;
    cxuiTestControlInfoDestroy(&pi);
    if (!ok)
        TEST_FAIL(1, _SL("a new maximum, then activity"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// ---- menus ----

static bool menuAlive(void* w)
{
    return !(((CxUiWidget*)w)->state & CXUI_WS_Dead);
}

typedef struct MenuLog {
    int32 activations;
    CxUiMenuItem* last;
    bool checkedThen;   // the item's check mark when its handler ran
} MenuLog;

static CxUiResult menuChosen(stvlist* cvars, CxUiActivateEvent* ev)
{
    MenuLog* log     = stvlAtPtr(cvars, 0);
    CxUiMenuItem* it = (CxUiMenuItem*)ev->base.current;
    log->activations++;
    log->last        = it;
    log->checkedThen = cxuiMenuItemChecked(it);
    return CXUI_Default;
}

// What the platform shows at a position is the title, hint, kind, check mark and enabled state
static bool menuShows(CxUiMenu* m, int32 at, strref title, strref hint, uint32 kind, bool checked, bool enabled)
{
    CxUiTestMenuItemInfo mi;
    bool ok = cxuiTestMenuItem(m, at, &mi) && strEq(mi.title, title) && strEq(mi.hint, hint) && mi.kind == kind &&
              mi.checked == checked && mi.enabled == enabled;
    cxuiTestMenuItemInfoDestroy(&mi);
    return ok;
}

// A window's menu bar and the menus in it reach the platform as built: titles with their
// mnemonics, hints, kinds and check marks, submenus; nothing is made before the window is.
UI_TEST(menubuild)
{
    CxUiMenu* file = cxuiMenuCreate();
    cxuiMenuAppendItem(file, .title = _S"&Open...", .hint = _S"Ctrl+O");
    cxuiMenuAppendSeparator(file);
    cxuiMenuAppendItem(file, .title = _S"E&xit");
    CxUiMenu* view     = cxuiMenuCreate();
    CxUiMenuItem* grid = cxuiMenuAppendItem(view, .title = _S"&Grid", .kind = CXUI_MENU_Check, .checked = true);
    cxuiMenuAppendItem(view, .title = _S"&Off", .kind = CXUI_MENU_Radio);
    cxuiMenuAppendItem(view, .title = _S"O&n", .kind = CXUI_MENU_Radio, .checked = true);
    CxUiMenu* bar = cxuiMenuCreate();
    cxuiMenuAppendItem(bar, .title = _S"&File", .submenu = file);
    CxUiMenuItem* viewItem = cxuiMenuAppendItem(bar, .title = _S"&View", .submenu = view);
    objRelease(&file);
    objRelease(&view);
    file = cxuiMenuItemSubmenu((CxUiMenuItem*)cxuiContainerChild(bar, 0));
    view = cxuiMenuItemSubmenu(viewItem);

    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiWindowSetMenu(w, bar);
    if (cxuiTestMenuCount(bar) != -1 || cxuiWindowMenu(w) != bar || CxUiWidget(bar)->parent != CxUiWidget(w))
        TEST_FAIL(1, _SL("before the window is realized: ${int} native items"), stvar(int32, cxuiTestMenuCount(bar)));
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);
    if (!cxuiTestWindowMenu(w, bar) || cxuiTestMenuCount(bar) != 2 || cxuiTestMenuCount(file) != 3 ||
        cxuiTestMenuCount(view) != 3)
        TEST_FAIL(1, _SL("shown: the bar ${int} items, File ${int}, View ${int}"), stvar(int32, cxuiTestMenuCount(bar)),
                  stvar(int32, cxuiTestMenuCount(file)), stvar(int32, cxuiTestMenuCount(view)));
    CxUiTestMenuItemInfo mi;
    bool sub = cxuiTestMenuItem(bar, 1, &mi) && mi.submenu && strEq(mi.title, _S"&View");
    cxuiTestMenuItemInfoDestroy(&mi);
    if (!sub || !menuShows(file, 0, _S"&Open...", _S"Ctrl+O", CXUI_MENU_Command, false, true) ||
        !menuShows(file, 1, NULL, NULL, CXUI_MENU_Separator, false, true) ||
        !menuShows(view, 0, _S"&Grid", NULL, CXUI_MENU_Check, true, true) ||
        !menuShows(view, 1, _S"&Off", NULL, CXUI_MENU_Radio, false, true) ||
        !menuShows(view, 2, _S"O&n", NULL, CXUI_MENU_Radio, true, true))
        TEST_FAIL(1, _SL("the items don't show as built (View opens a menu: ${int})"), stvar(int32, sub));

    // Changes reach the platform.
    cxuiMenuItemSetTitle(grid, _S"&Grid Lines");
    cxuiMenuItemSetHint(grid, _S"G");
    if (!menuShows(view, 0, _S"&Grid Lines", _S"G", CXUI_MENU_Check, true, true))
        TEST_FAIL(1, _SL("a new title and hint don't show"), stvNone);
    cxuiMenuItemSetHint(grid, NULL);
    if (!menuShows(view, 0, _S"&Grid Lines", NULL, CXUI_MENU_Check, true, true) || cxuiMenuItemHint(grid))
        TEST_FAIL(1, _SL("the hint wasn't taken away"), stvNone);

    // A command can't start checked, a radio item can't open a menu, and a menu holds only items.
    CxUiMenu* loose = cxuiMenuCreate();
    CxUiMenuItem* plain = cxuiMenuItemCreate(&(CxUiMenuItemDesc){ .title = _S"x", .checked = true });
    CxUiMenuItem* radio = cxuiMenuItemCreate(&(CxUiMenuItemDesc){ .title = _S"y", .kind = CXUI_MENU_Radio, .submenu = loose });
    CxUiLabel* label    = cxuiLabelCreate(NULL);
    bool odd            = cxuiMenuItemChecked(plain) || cxuiMenuItemSubmenu(radio) || CxUiWidget(loose)->parent ||
               cxuiContainerAppend(view, label) || cxuiContainerAppend(view, loose);
    objRelease(&plain);
    objRelease(&radio);
    objRelease(&label);
    objRelease(&loose);
    if (odd)
        TEST_FAIL(1, _SL("an item took a setting its kind can't have, or a menu took a widget that isn't an item"), stvNone);

    // A radio item can't be given a submenu, nor can a menu that already has a holder.
    CxUiMenu* other = cxuiMenuCreate();
    cxuiMenuItemSetSubmenu((CxUiMenuItem*)cxuiContainerChild(view, 1), other);
    cxuiMenuItemSetSubmenu(viewItem, file);
    if (cxuiMenuItemSubmenu((CxUiMenuItem*)cxuiContainerChild(view, 1)) || cxuiMenuItemSubmenu(viewItem) != view ||
        CxUiWidget(other)->parent)
        TEST_FAIL(1, _SL("a submenu went where it can't"), stvNone);
    objRelease(&other);

    // The bar goes with the window, and its native menus with it.
    CxUiMenu* held     = objAcquire(bar);
    CxUiMenu* fileHeld = objAcquire(file);
    cxuiDestroy(w);
    objRelease(&w);
    bool lived = menuAlive(CxUiWidget(held)) || menuAlive(CxUiWidget(fileHeld)) || cxuiTestMenuCount(held) != -1;
    objRelease(&held);
    objRelease(&fileHeld);
    if (lived)
        TEST_FAIL(1, _SL("the menu bar outlived its window"), stvNone);
    objRelease(&bar);
    return 0;
}

// Choosing an item raises its activate event, after a check item has turned over or a radio item
// has been checked and its group cleared; groups end at an item of another kind. Disabled items,
// items in a disabled menu, separators and items that open menus aren't chosen. Checking from code
// raises nothing.
UI_TEST(menuchoose)
{
    MenuLog log      = { 0 };
    CxUiMenu* menu   = cxuiMenuCreate();
    CxUiMenuItem* go = cxuiMenuAppendItem(menu, .title = _S"&Go");
    CxUiMenuItem* ck = cxuiMenuAppendItem(menu, .title = _S"&Check", .kind = CXUI_MENU_Check);
    CxUiMenuItem* r[5];
    r[0] = cxuiMenuAppendItem(menu, .title = _S"A", .kind = CXUI_MENU_Radio, .checked = true);
    r[1] = cxuiMenuAppendItem(menu, .title = _S"B", .kind = CXUI_MENU_Radio);
    r[2] = cxuiMenuAppendItem(menu, .title = _S"C", .kind = CXUI_MENU_Radio);
    cxuiMenuAppendSeparator(menu);
    r[3] = cxuiMenuAppendItem(menu, .title = _S"D", .kind = CXUI_MENU_Radio, .checked = true);
    r[4] = cxuiMenuAppendItem(menu, .title = _S"E", .kind = CXUI_MENU_Radio);
    CxUiMenu* subMenu   = cxuiMenuCreate();
    CxUiMenuItem* inner = cxuiMenuAppendItem(subMenu, .title = _S"Inner");
    CxUiMenuItem* opens = cxuiMenuAppendItem(menu, .title = _S"More", .submenu = subMenu);
    objRelease(&subMenu);
    subMenu       = cxuiMenuItemSubmenu(opens);
    CxUiMenu* bar = cxuiMenuCreate();
    CxUiMenuItem* top = cxuiMenuAppendItem(bar, .title = _S"&Menu", .submenu = menu);
    objRelease(&menu);
    menu = cxuiMenuItemSubmenu(top);
    for (int32 i = 0; i < cxuiContainerChildCount(menu); i++)
        cxuiOnActivate(cxuiContainerChild(menu, i), menuChosen, stvar(ptr, &log));
    cxuiOnActivate(inner, menuChosen, stvar(ptr, &log));
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiWindowSetMenu(w, bar);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);

    cxuiTestMenuChoose(go);
    if (log.activations != 1 || log.last != go)
        TEST_FAIL(1, _SL("choosing a command: ${int} activations"), stvar(int32, log.activations));
    cxuiTestMenuChoose(ck);
    bool on = log.checkedThen && cxuiMenuItemChecked(ck) && menuShows(menu, 1, _S"&Check", NULL, CXUI_MENU_Check, true, true);
    cxuiTestMenuChoose(ck);
    if (!on || log.activations != 3 || log.checkedThen || cxuiMenuItemChecked(ck) ||
        !menuShows(menu, 1, _S"&Check", NULL, CXUI_MENU_Check, false, true))
        TEST_FAIL(1, _SL("a check item didn't turn over before its handler: on ${int}"), stvar(int32, on));

    cxuiTestMenuChoose(r[2]);
    if (!log.checkedThen || cxuiMenuItemChecked(r[0]) || !cxuiMenuItemChecked(r[2]) || !cxuiMenuItemChecked(r[3]) ||
        !menuShows(menu, 2, _S"A", NULL, CXUI_MENU_Radio, false, true) ||
        !menuShows(menu, 4, _S"C", NULL, CXUI_MENU_Radio, true, true))
        TEST_FAIL(1, _SL("choosing C: A ${int}, C ${int}, D across the separator ${int}"), stvar(int32, cxuiMenuItemChecked(r[0])),
                  stvar(int32, cxuiMenuItemChecked(r[2])), stvar(int32, cxuiMenuItemChecked(r[3])));
    // Choosing the checked one again keeps it checked and still raises the event.
    cxuiTestMenuChoose(r[2]);
    int32 before = log.activations;
    cxuiMenuItemSetChecked(r[4], true);
    cxuiMenuItemSetChecked(r[1], true);
    if (log.activations != before || before != 5 || cxuiMenuItemChecked(r[2]) || cxuiMenuItemChecked(r[3]) ||
        !cxuiMenuItemChecked(r[4]) || !cxuiMenuItemChecked(r[1]) || !menuShows(menu, 6, _S"D", NULL, CXUI_MENU_Radio, false, true))
        TEST_FAIL(1, _SL("checking from code: ${int} activations, C ${int}, D ${int}, E ${int}"), stvar(int32, log.activations),
                  stvar(int32, cxuiMenuItemChecked(r[2])), stvar(int32, cxuiMenuItemChecked(r[3])),
                  stvar(int32, cxuiMenuItemChecked(r[4])));
    // Clearing a radio item clears only it, and a check item can't be cleared through its kind.
    cxuiMenuItemSetChecked(r[1], false);
    cxuiMenuItemSetChecked(go, true);
    if (cxuiMenuItemChecked(r[1]) || cxuiMenuItemChecked(r[0]) || cxuiMenuItemChecked(go))
        TEST_FAIL(1, _SL("clearing a radio item, or checking a command"), stvNone);

    // Separators and items that open menus aren't chosen; an item inside one is.
    before = log.activations;
    cxuiTestMenuChoose((CxUiMenuItem*)cxuiContainerChild(menu, 5));
    cxuiMenuItemChoose((CxUiMenuItem*)cxuiContainerChild(menu, 5));
    cxuiTestMenuChoose(opens);
    cxuiMenuItemChoose(opens);
    cxuiTestMenuChoose(inner);
    if (log.activations != before + 1 || log.last != inner)
        TEST_FAIL(1, _SL("separators and menu openers: ${int} activations"), stvar(int32, log.activations - before));

    // Disabled: the item, or the item holding its menu
    before = log.activations;
    cxuiWidgetSetEnabled(go, false);
    cxuiTestMenuChoose(go);
    cxuiMenuItemChoose(go);
    bool greyed = menuShows(menu, 0, _S"&Go", NULL, CXUI_MENU_Command, false, false);
    cxuiWidgetSetEnabled(go, true);
    cxuiWidgetSetEnabled(top, false);
    cxuiTestMenuChoose(ck);
    CxUiTestMenuItemInfo mi;
    bool topGrey = cxuiTestMenuItem(bar, 0, &mi) && !mi.enabled;
    cxuiTestMenuItemInfoDestroy(&mi);
    bool inGrey = menuShows(menu, 0, _S"&Go", NULL, CXUI_MENU_Command, false, false);
    // An item added to a menu inside a disabled one is grayed as it arrives, and not after.
    CxUiMenuItem* late = cxuiMenuAppendItem(menu, .title = _S"Late");
    bool lateGrey = menuShows(menu, cxuiContainerChildCount(menu) - 1, _S"Late", NULL, CXUI_MENU_Command, false, false);
    cxuiWidgetSetEnabled(top, true);
    lateGrey = lateGrey && menuShows(menu, cxuiContainerChildCount(menu) - 1, _S"Late", NULL, CXUI_MENU_Command, false, true);
    cxuiDestroy(late);
    if (log.activations != before || !greyed || !topGrey || !inGrey || !lateGrey ||
        !menuShows(menu, 0, _S"&Go", NULL, CXUI_MENU_Command, false, true))
        TEST_FAIL(1, _SL("disabled: ${int} activations, item grey ${int}, menu title grey ${int}, items in it ${int}"),
                  stvar(int32, log.activations - before), stvar(int32, greyed), stvar(int32, topGrey), stvar(int32, inGrey));

    // From code, as the user would
    cxuiMenuItemChoose(ck);
    if (log.activations != before + 1 || !cxuiMenuItemChecked(ck))
        TEST_FAIL(1, _SL("cxuiMenuItemChoose didn't choose"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&bar);
    return 0;
}

// The log first: menuChosen takes the same handler arguments
typedef struct MenuRecent {
    MenuLog log;
    CxUiMenu* recent;
    int32 rebuilds;
} MenuRecent;

static CxUiResult menuRecentChosen(stvlist* cvars, CxUiActivateEvent* ev);

// Rebuilds the menu from scratch, the chosen item with it
static void menuRecentFill(MenuRecent* mr, int32 n)
{
    cxuiMenuClear(mr->recent);
    for (int32 i = 0; i < n; i++) {
        string t = 0;
        strFormat(&t, _SL("&${int} file"), stvar(int32, i + 1));
        cxuiOnActivate(cxuiMenuAppendItem(mr->recent, .title = t), menuRecentChosen, stvar(ptr, mr));
        strDestroy(&t);
    }
}

static CxUiResult menuRecentChosen(stvlist* cvars, CxUiActivateEvent* ev)
{
    MenuRecent* mr = stvlAtPtr(cvars, 0);
    menuChosen(cvars, ev);
    mr->rebuilds++;
    menuRecentFill(mr, 2);
    return CXUI_Default;
}

// Menus change while shown: items added and destroyed, from the chosen item's own handler too;
// a submenu replaced or taken away; an item taken out and put back; the bar taken away and given
// again.
UI_TEST(menuchange)
{
    MenuRecent mr = { .recent = cxuiMenuCreate() };
    CxUiMenu* file = cxuiMenuCreate();
    CxUiMenuItem* recentItem = cxuiMenuAppendItem(file, .title = _S"Open &Recent", .submenu = mr.recent);
    CxUiMenuItem* exitItem   = cxuiMenuAppendItem(file, .title = _S"E&xit");
    objRelease(&mr.recent);
    mr.recent     = cxuiMenuItemSubmenu(recentItem);
    CxUiMenu* bar = cxuiMenuCreate();
    cxuiMenuAppendItem(bar, .title = _S"&File", .submenu = file);
    objRelease(&file);
    file          = cxuiMenuItemSubmenu((CxUiMenuItem*)cxuiContainerChild(bar, 0));
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiWindowSetMenu(w, bar);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);

    menuRecentFill(&mr, 3);
    CxUiMenuItem* first = objAcquire((CxUiMenuItem*)cxuiContainerChild(mr.recent, 0));
    if (cxuiTestMenuCount(mr.recent) != 3 || !menuShows(mr.recent, 2, _S"&3 file", NULL, CXUI_MENU_Command, false, true))
        TEST_FAIL(1, _SL("items added while shown: ${int}"), stvar(int32, cxuiTestMenuCount(mr.recent)));
    cxuiTestMenuChoose(first);
    bool firstAlive = menuAlive(CxUiWidget(first));
    objRelease(&first);
    if (mr.rebuilds != 1 || mr.log.activations != 1 || cxuiTestMenuCount(mr.recent) != 2 || firstAlive ||
        cxuiContainerChildCount(mr.recent) != 2)
        TEST_FAIL(1, _SL("rebuilt from the chosen item's handler: ${int} native items, ${int} children"),
                  stvar(int32, cxuiTestMenuCount(mr.recent)), stvar(int32, cxuiContainerChildCount(mr.recent)));

    // An item taken out keeps its settings and goes back in where it is put.
    CxUiMenuItem* held = objAcquire(exitItem);
    cxuiContainerDetach(file, exitItem);
    bool out = cxuiTestMenuCount(file) == 1 && menuAlive(CxUiWidget(held));
    cxuiContainerInsert(file, held, 0);
    if (!out || cxuiTestMenuCount(file) != 2 || !menuShows(file, 0, _S"E&xit", NULL, CXUI_MENU_Command, false, true))
        TEST_FAIL(1, _SL("an item taken out and put back: out ${int}, then ${int} items"), stvar(int32, out),
                  stvar(int32, cxuiTestMenuCount(file)));
    objRelease(&held);

    // A submenu replaced: the new one is made, the old one's native menu goes; then taken away.
    CxUiMenu* older = objAcquire(mr.recent);
    CxUiMenu* fresh = cxuiMenuCreate();
    cxuiMenuAppendItem(fresh, .title = _S"Nothing yet");
    cxuiMenuItemSetSubmenu(recentItem, fresh);
    bool swapped = cxuiTestMenuCount(fresh) == 1 && cxuiTestMenuCount(older) == -1 && !CxUiWidget(older)->parent &&
                   menuAlive(CxUiWidget(older));
    cxuiMenuItemSetSubmenu(recentItem, CxUiMenuNone);
    CxUiTestMenuItemInfo mi;
    bool plain = cxuiTestMenuItem(file, 1, &mi) && !mi.submenu && cxuiTestMenuCount(fresh) == -1;
    cxuiTestMenuItemInfoDestroy(&mi);
    if (!swapped || !plain)
        TEST_FAIL(1, _SL("a submenu replaced ${int}, then taken away ${int}"), stvar(int32, swapped), stvar(int32, plain));
    objRelease(&fresh);
    objRelease(&older);

    // An item taken out takes the native menu it opens with it, and brings it back.
    CxUiMenuItem* fileItem = objAcquire((CxUiMenuItem*)cxuiContainerChild(bar, 0));
    cxuiContainerDetach(bar, fileItem);
    bool taken = cxuiTestMenuCount(file) == -1 && cxuiTestMenuCount(bar) == 0;
    cxuiContainerInsert(bar, fileItem, 0);
    objRelease(&fileItem);
    if (!taken || cxuiTestMenuCount(file) != 2)
        TEST_FAIL(1, _SL("an item that opens a menu taken out ${int}, and put back: ${int} items"), stvar(int32, taken),
                  stvar(int32, cxuiTestMenuCount(file)));

    // A bar another window has can't be taken.
    CxUiWindow* w2 = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 } });
    cxuiWindowSetMenu(w2, bar);
    bool kept = !cxuiWindowMenu(w2) && CxUiWidget(bar)->parent == CxUiWidget(w);
    cxuiDestroy(w2);
    objRelease(&w2);
    if (!kept)
        TEST_FAIL(1, _SL("a second window took the first one's menu bar"), stvNone);

    // The bar taken away and given again
    cxuiWindowSetMenu(w, CxUiMenuNone);
    bool none = cxuiTestWindowMenu(w, CxUiMenuNone) && cxuiTestMenuCount(bar) == -1 && !CxUiWidget(bar)->parent;
    cxuiWindowSetMenu(w, bar);
    if (!none || !cxuiTestWindowMenu(w, bar) || cxuiTestMenuCount(file) != 2)
        TEST_FAIL(1, _SL("the bar taken away ${int} and given again"), stvar(int32, none));

    // Destroying an item that opens a menu destroys the menu too.
    CxUiMenu* fileHeld = objAcquire(file);
    cxuiDestroy(cxuiContainerChild(bar, 0));
    if (menuAlive(CxUiWidget(fileHeld)) || cxuiTestMenuCount(bar) != 0 || cxuiTestMenuCount(fileHeld) != -1)
        TEST_FAIL(1, _SL("a destroyed item's menu lived on"), stvNone);
    objRelease(&fileHeld);
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&bar);
    return 0;
}

static int test_cxui_grp_menus(void)
{
    TEST_CHAIN(test_cxui_menubuild, test_cxui_menuchoose, test_cxui_menuchange);
}

// ---- tabs ----

// What the null backend's tab control shows: its titles, a line each, and the tab picked
static bool tabsShow(CxUiTabs* t, strref titles, int32 value)
{
    CxUiTestControlInfo ci;
    bool ok = cxuiTestControlInfo(CxUiWidget(t), &ci) && strEq(ci.items, titles) && ci.value == value;
    cxuiTestControlInfoDestroy(&ci);
    return ok;
}

// The room the null backend's tabs leave around the pages: the core's estimate from the font
static CxUiEdges tabsInsetsFor(CxUiTabs* t)
{
    CxUiFont* f = cxuiWidgetFont(t);
    CxUiFontMetrics fm;
    cxuiFontMetrics(f, &fm);
    return (CxUiEdges){ 1, (int32)ceilf(fm.height / cxuiFontScale(f)) + 8, 1, 1 };
}

// Tabs are as large as the largest page and the strip around it, and at least as wide as every
// tab; the platform shows their titles and the page picked, which follow insertions, removals and
// title changes. The pages sit on a surface inside the control, in the pages' own background.
UI_TEST(tabsbuild)
{
    for (int i = 0; i < 3; i++) {
        float32 s = layoutScales[i];
        cxuiTestSetSystemScale(s);
        CxUiTabs* tabs = cxuiTabsCreate(NULL);
        CxUiCanvas* cv = cxuiCanvasCreate(NULL);
        cxuiWidgetSetNaturalSize(cv, cxuiLen(120), cxuiLen(40));
        cxuiTabsAdd(tabs, cv, _S"&General");
        objRelease(&cv);
        CxUiSpace* big = space(200, 30);
        cxuiTabsAdd(tabs, big, _S"Advanced");
        objRelease(&big);
        CxUiEdges in = tabsInsetsFor(tabs);
        CHECK_SIZE("tabs natural size", cxuiNaturalSize(tabs), 200 + in.left + in.right, 40 + in.top + in.bottom);
        if (!strEq(cxuiTabsTitle(tabs, 0), _S"&General") || cxuiTabsTitle(tabs, 2) || cxuiZBoxActive(tabs) != 0)
            TEST_FAIL(1, _SL("titles or the first page wrong before showing"), stvNone);

        CxUiBox* col  = cxuiBoxCreateV();
        CxUiFrame* fr = put(col, cxuiFrameCreate(NULL));
        put(col, tabs);
        CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
        cxuiWidgetSetExpand(fr, CXUI_EXPAND_None);
        cxuiFlush();
        if (!tabsShow(tabs, _S"&General\nAdvanced\n", 0))
            TEST_FAIL(1, _SL("at scale ${float}: the platform's tabs are wrong once shown"), stvar(float64, s));

        // The page's peers are placed on the page surface, which is inside the strip and border.
        CxUiTestControlInfo ci;
        CxUiRect tb, cb;
        cxuiTestControlInfo(CxUiWidget(tabs), &ci);
        cxuiTestBounds(CxUiWidget(tabs), &tb);
        cxuiTestBounds(cxuiContainerChild(tabs, 0), &cb);
        CxUiTestCanvasInfo cvi;
        cxuiTestCanvasInfo((CxUiCanvas*)cxuiContainerChild(tabs, 0), &cvi);
        int32 top = (int32)floor((tb.y / s + in.top) * s + 0.5) - tb.y;
        if (!rectIs(ci.bounds, tb.x, tb.y, tb.w, tb.h) || ci.page.y != top || cb.y != tb.y + top ||
            cvi.bounds.x != cb.x - tb.x - ci.page.x || cvi.bounds.y != cb.y - tb.y - ci.page.y)
            TEST_FAIL(1, _SL("at scale ${float}: page surface at ${int},${int}, the canvas on it at ${int},${int}"),
                      stvar(float64, s), stvar(int32, ci.page.x), stvar(int32, ci.page.y), stvar(int32, cvi.bounds.x),
                      stvar(int32, cvi.bounds.y));
        if (ci.bg.bits != palColor(tabs, CXUI_PAL_Content).bits || ci.color.bits != palColor(tabs, CXUI_PAL_Panel).bits ||
            ci.fontPx <= 0)
            TEST_FAIL(1, _SL("the pages aren't in CXUI_PAL_Content, the strip not on the window's color, or no font"),
                      stvNone);
        cxuiTestControlInfoDestroy(&ci);

        // Titles and pages added and taken out reach the platform, the same page staying picked.
        cxuiTabsSetTitle(tabs, 1, _S"Advanced \xE2\x80\xA2");
        CxUiFrame* third = cxuiFrameCreate(NULL);
        cxuiContainerInsert(tabs, third, 0);
        cxuiTabsSetTitle(tabs, 0, _S"First");
        cxuiFlush();
        if (!tabsShow(tabs, _S"First\n&General\nAdvanced \xE2\x80\xA2\n", 1) || cxuiZBoxActive(tabs) != 1)
            TEST_FAIL(1, _SL("inserting a page: the platform doesn't keep the same tab picked"), stvNone);
        // A frame on a page shows the page's background around it.
        CxUiTestPanelInfo pi;
        cxuiZBoxSetActive(tabs, 0);
        cxuiFlush();
        if (!cxuiTestPanelInfo(CxUiWidget(third), &pi) || pi.bg.bits != palColor(tabs, CXUI_PAL_Content).bits ||
            !cxuiTestPanelInfo(CxUiWidget(fr), &pi) || pi.bg.bits != palColor(tabs, CXUI_PAL_Panel).bits)
            TEST_FAIL(1, _SL("a frame on a page doesn't take the page's background"), stvNone);
        cxuiContainerDetach(tabs, third);
        objRelease(&third);
        cxuiFlush();
        if (!tabsShow(tabs, _S"&General\nAdvanced \xE2\x80\xA2\n", 0) || cxuiZBoxActive(tabs) != 0 ||
            !strEq(cxuiTabsTitle(tabs, 0), _S"&General"))
            TEST_FAIL(1, _SL("removing the shown page: the platform doesn't show the next, or its title is wrong"), stvNone);
        cxuiDestroy(w);
        objRelease(&w);
    }

    // Tabs are as wide as their strip, and as tall as a page that wraps to the width it gets.
    cxuiTestSetSystemScale(1.0f);
    CxUiTabs* wide = cxuiTabsCreate(NULL);
    int32 strip    = 0;
    CxUiFont* f    = cxuiWidgetFont(wide);
    strref long3[3] = { _S"A long first title", _S"&A second long title", _S"And a third" };
    for (int k = 0; k < 3; k++) {
        CxUiSpace* sp = space(10, 10);
        cxuiTabsAdd(wide, sp, long3[k]);
        objRelease(&sp);
        string shown = 0;
        strCopy(&shown, k == 1 ? _S"A second long title" : long3[k]);
        strip += (int32)ceilf(cxuiFontAdvance(f, shown) / cxuiFontScale(f)) + 16;
        strDestroy(&shown);
    }
    CxUiEdges in = tabsInsetsFor(wide);
    CHECK_SIZE("tabs as wide as their strip", cxuiNaturalSize(wide), strip, 10 + in.top + in.bottom);
    int32 line = cxuiFontMeasure(f, _S"A").h;
    objRelease(&wide);

    CxUiTabs* tall = cxuiTabsCreate(NULL);
    CxUiLabel* para = cxuiLabelCreate(&(CxUiLabelDesc){
        .text = _S"A paragraph long enough to wrap onto several lines once it is given less room than it would take on one",
        .wrap = true });
    cxuiTabsAdd(tall, para, _S"Page");
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200, 0 }, .sizing = CXUI_SIZE_Start });
    CxUiBox* col  = cxuiBoxCreateV();
    cxuiContainerAppend(col, tall);
    put(col, space(10, 10));
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiFlush();
    // The same paragraph on its own, as wide as the page
    CxUiLabel* alone = cxuiLabelCreate(&(CxUiLabelDesc){ .text = cxuiLabelText(para), .wrap = true });
    CxUiWindow* w2   = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200 - in.left - in.right, 0 }, .sizing = CXUI_SIZE_Start });
    col              = cxuiBoxCreateV();
    cxuiContainerAppend(col, alone);
    put(col, space(10, 10));
    cxuiContainerAppend(w2, col);
    objRelease(&col);
    cxuiWindowShow(w2, CXUI_SHOW_Normal);
    cxuiFlush();
    CxUiRect tb, pb;
    cxuiTestBounds(CxUiWidget(tall), &tb);
    cxuiTestBounds(CxUiWidget(alone), &pb);
    if (pb.h <= line || tb.h != pb.h + in.top + in.bottom)
        TEST_FAIL(1, _SL("a paragraph ${int} tall as wide as the page, in tabs ${int} tall"), stvar(int32, pb.h),
                  stvar(int32, tb.h));
    objRelease(&para);
    objRelease(&alone);
    objRelease(&tall);
    cxuiDestroy(w);
    objRelease(&w);
    cxuiDestroy(w2);
    objRelease(&w2);
    return 0;
}

typedef struct TabsLog {
    CxUiTabs* tabs;
    int32 changes;
    int32 active;
    bool realized;   // the page being shown had a peer when the handler ran
    bool fill;       // add a canvas to the page the first time it is picked
} TabsLog;

static CxUiResult tabsChanged(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    TabsLog* log    = stvlNextPtr(cvars);
    log->changes++;
    log->active     = cxuiZBoxActive(log->tabs);
    CxUiWidget* pg  = cxuiContainerChild(log->tabs, log->active);
    log->realized   = pg->peer != NULL;
    if (log->fill && cxuiContainerChildCount(pg) == 0)
        put(pg, cxuiCanvasCreate(NULL));
    return CXUI_Default;
}

// A pick by the user raises a change before the page is created on screen, so the handler can fill
// it, and the filled page is on screen when the pick returns; a pick from code raises nothing.
// Focus left on the page that was shown goes to the strip.
UI_TEST(tabspick)
{
    CxUiTabs* tabs = cxuiTabsCreate(NULL);
    CxUiBox* p0    = cxuiBoxCreateV();
    CxUiCanvas* cv = put(p0, cxuiCanvasCreate(NULL));
    cxuiTabsAdd(tabs, p0, _S"One");
    objRelease(&p0);
    for (int k = 0; k < 2; k++) {
        CxUiBox* pg = cxuiBoxCreateV();
        cxuiTabsAdd(tabs, pg, k ? _S"Three" : _S"Two");
        objRelease(&pg);
    }
    TabsLog log = { .tabs = tabs, .fill = true };
    cxuiOnChange(tabs, tabsChanged, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(tabs));
    cxuiFlush();
    cxuiFocus(cv);
    if (!cxuiTestCanvasInfo(cv, &(CxUiTestCanvasInfo){ 0 }) || cxuiFocused(w) != CxUiWidget(cv))
        TEST_FAIL(1, _SL("the first page's canvas isn't on screen with focus"), stvNone);

    cxuiTestChoose(CxUiWidget(tabs), 2, false);
    CxUiWidget* added = cxuiContainerChild(cxuiContainerChild(tabs, 2), 0);
    bool shown        = false;
    if (log.changes != 1 || log.active != 2 || log.realized || !added || !cxuiTestShown(added, &shown) || !shown)
        TEST_FAIL(1, _SL("picking a tab: ${int} changes on page ${int}, created before ${int}, filled and shown ${int}"),
                  stvar(int32, log.changes), stvar(int32, log.active), stvar(int32, log.realized), stvar(int32, shown));
    if (cxuiFocused(w) != CxUiWidget(tabs))
        TEST_FAIL(1, _SL("focus stayed on the page that was hidden"), stvNone);
    // Picking the tab already shown does nothing.
    cxuiTestChoose(CxUiWidget(tabs), 2, false);
    if (log.changes != 1)
        TEST_FAIL(1, _SL("picking the shown tab raised a change"), stvNone);

    cxuiZBoxSetActive(tabs, 1);
    cxuiFlush();
    if (log.changes != 1 || !tabsShow(tabs, _S"One\nTwo\nThree\n", 1))
        TEST_FAIL(1, _SL("a pick from code: ${int} changes, or the platform not told"), stvar(int32, log.changes));
    if (cxuiContainerChildCount(cxuiContainerChild(tabs, 1)) != 0)
        TEST_FAIL(1, _SL("a pick from code ran the handler"), stvNone);

    // Disabled tabs don't take the user's pick.
    cxuiWidgetSetEnabled(tabs, false);
    cxuiTestChoose(CxUiWidget(tabs), 0, false);
    if (log.changes != 1 || cxuiZBoxActive(tabs) != 1)
        TEST_FAIL(1, _SL("disabled tabs took a pick"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// Ctrl+Tab, Ctrl+Shift+Tab and Ctrl+Page Down and Up go round the tabs from anywhere inside them,
// as picks by the user; Alt with a tab's mnemonic picks it and focuses the strip.
UI_TEST(tabskeys)
{
    CxUiBox* col   = cxuiBoxCreateV();
    CxUiTabs* tabs = put(col, cxuiTabsCreate(NULL));
    CxUiCanvas* cv = NULL;
    strref titles[3] = { _S"&One", _S"T&wo", _S"Th&ree" };
    for (int k = 0; k < 3; k++) {
        CxUiBox* pg = cxuiBoxCreateV();
        CxUiCanvas* c = put(pg, cxuiCanvasCreate(NULL));
        if (k == 0)
            cv = c;
        cxuiTabsAdd(tabs, pg, titles[k]);
        objRelease(&pg);
    }
    CxUiButton* after = put(col, cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Other" }));
    TabsLog log       = { .tabs = tabs };
    cxuiOnChange(tabs, tabsChanged, stvar(ptr, &log));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    cxuiFlush();

    static const struct {
        CxUiKey key;
        uint32 mods;
        int32 want;
    } steps[] = {
        { CXUI_KEY_Tab, CXUI_MOD_Ctrl, 1 },
        { CXUI_KEY_PageDown, CXUI_MOD_Ctrl, 2 },
        { CXUI_KEY_Tab, CXUI_MOD_Ctrl, 0 },
        { CXUI_KEY_Tab, CXUI_MOD_Ctrl | CXUI_MOD_Shift, 2 },
        { CXUI_KEY_PageUp, CXUI_MOD_Ctrl, 1 },
    };
    cxuiFocus(cv);
    for (int i = 0; i < (int)(sizeof(steps) / sizeof(steps[0])); i++) {
        CxUiWidget* f = cxuiFocused(w);
        if (cxuiTestKey(f ? f : CxUiWidget(tabs), steps[i].key, steps[i].mods, true) != CXUI_Handled ||
            cxuiZBoxActive(tabs) != steps[i].want || log.changes != i + 1 ||
            !tabsShow(tabs, _S"&One\nT&wo\nTh&ree\n", steps[i].want))
            TEST_FAIL(1, _SL("key ${int}: on page ${int}, wanted ${int}, after ${int} changes"), stvar(int32, i),
                      stvar(int32, cxuiZBoxActive(tabs)), stvar(int32, steps[i].want), stvar(int32, log.changes));
        cxuiTestKey(f ? f : CxUiWidget(tabs), steps[i].key, steps[i].mods, false);
    }
    // Ctrl+Tab from outside the tabs goes nowhere; a plain Page Down isn't theirs.
    cxuiFocus(after);
    cxuiTestKey(CxUiWidget(after), CXUI_KEY_Tab, CXUI_MOD_Ctrl, true);
    cxuiTestKey(CxUiWidget(after), CXUI_KEY_Tab, CXUI_MOD_Ctrl, false);
    cxuiFocus(tabs);
    cxuiTestKey(CxUiWidget(tabs), CXUI_KEY_PageDown, 0, true);
    cxuiTestKey(CxUiWidget(tabs), CXUI_KEY_PageDown, 0, false);
    if (cxuiZBoxActive(tabs) != 1 || log.changes != 5)
        TEST_FAIL(1, _SL("keys outside the tabs or without Ctrl picked a tab"), stvNone);

    cxuiFocus(after);
    cxuiTestKey(CxUiWidget(after), CXUI_KEY_R, CXUI_MOD_Alt, true);
    cxuiTestKey(CxUiWidget(after), CXUI_KEY_R, CXUI_MOD_Alt, false);
    if (cxuiZBoxActive(tabs) != 2 || log.changes != 6 || cxuiFocused(w) != CxUiWidget(tabs) ||
        !tabsShow(tabs, _S"&One\nT&wo\nTh&ree\n", 2))
        TEST_FAIL(1, _SL("Alt+R: on page ${int}, ${int} changes, focus on the strip ${int}"),
                  stvar(int32, cxuiZBoxActive(tabs)), stvar(int32, log.changes),
                  stvar(int32, cxuiFocused(w) == CxUiWidget(tabs)));
    // The tab already shown isn't picked again, and disabled tabs take no keys.
    cxuiTestKey(CxUiWidget(tabs), CXUI_KEY_R, CXUI_MOD_Alt, true);
    cxuiTestKey(CxUiWidget(tabs), CXUI_KEY_R, CXUI_MOD_Alt, false);
    cxuiWidgetSetEnabled(tabs, false);
    cxuiTestKey(CxUiWidget(tabs), CXUI_KEY_Tab, CXUI_MOD_Ctrl, true);
    cxuiTestKey(CxUiWidget(tabs), CXUI_KEY_Tab, CXUI_MOD_Ctrl, false);
    if (cxuiZBoxActive(tabs) != 2 || log.changes != 6)
        TEST_FAIL(1, _SL("the shown tab picked again, or disabled tabs took Ctrl+Tab: ${int} changes"),
                  stvar(int32, log.changes));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// The lines the core's estimate wraps titles onto within a width, in DIPs
static int32 tabLines(CxUiTabs* t, int32 n, int32 width)
{
    CxUiFont* f  = cxuiWidgetFont(t);
    int32 lines  = 1, line = 0;
    for (int32 i = 0; i < n; i++) {
        int32 tab = (int32)ceilf(cxuiFontAdvance(f, cxuiTabsTitle(t, i)) / cxuiFontScale(f)) + 16;
        if (line > 0 && line + tab > width) {
            lines++;
            line = 0;
        }
        line += tab;
    }
    return lines;
}

// Tabs on several lines are as wide as their pages, and take as many lines as their titles need
// at the width they get: in a narrow window the strip grows and the pages start lower. Without
// the flag, tabs are as wide as every title.
UI_TEST(tabswrap)
{
    CxUiTabs* tabs = cxuiTabsCreate(&(CxUiTabsDesc){ .flags = CXUI_TABS_Multiline });
    CxUiTabs* flat = cxuiTabsCreate(NULL);
    string title   = 0;
    for (int32 i = 0; i < 8; i++) {
        strFormat(&title, _SL("Page number ${int}"), stvar(int32, i + 1));
        CxUiSpace* page = space(100, 40);
        cxuiTabsAdd(tabs, page, title);
        objRelease(&page);
        page = space(100, 40);
        cxuiTabsAdd(flat, page, title);
        objRelease(&page);
    }
    strDestroy(&title);
    CxUiEdges one = tabsInsetsFor(tabs);
    int32 lines   = tabLines(tabs, 8, 102);
    CHECK_SIZE("wrapped tabs' natural size", cxuiNaturalSize(tabs), 102, 40 + lines * one.top + 1);
    if (cxuiNaturalSize(flat).w <= 102 * 4)
        TEST_FAIL(1, _SL("tabs on one line are only ${int} wide"), stvar(int32, cxuiNaturalSize(flat).w));
    objRelease(&flat);

    CxUiBox* col = cxuiBoxCreateV();
    cxuiWidgetSetExpand(tabs, CXUI_EXPAND_Both);
    put(col, tabs);
    CxUiWindow* w = startWindow(2000, 300, CxUiWidget(col));
    settle(w);
    CxUiRect tb, pb;
    cxuiTestBounds(CxUiWidget(tabs), &tb);
    cxuiTestBounds(cxuiContainerChild(tabs, 0), &pb);
    if (pb.y - tb.y != one.top)
        TEST_FAIL(1, _SL("wide: the page starts ${int} below the tabs (want ${int}, one line)"), stvar(int32, pb.y - tb.y),
                  stvar(int32, one.top));
    cxuiWindowSetSize(w, 300, 300);
    settle(w);
    cxuiTestBounds(CxUiWidget(tabs), &tb);
    cxuiTestBounds(cxuiContainerChild(tabs, 0), &pb);
    lines = tabLines(tabs, 8, tb.w);
    if (lines < 2 || pb.y - tb.y != lines * one.top || pb.h != tb.h - lines * one.top - 1)
        TEST_FAIL(1, _SL("300 wide: the page at ${int} below the tabs, ${int} high (want ${int} lines of ${int})"),
                  stvar(int32, pb.y - tb.y), stvar(int32, pb.h), stvar(int32, lines), stvar(int32, one.top));

    // Given only the height they need, they get the lines they take at that width.
    cxuiWidgetSetExpand(tabs, CXUI_EXPAND_H);
    settle(w);
    cxuiTestBounds(CxUiWidget(tabs), &tb);
    if (tb.h != 40 + tabLines(tabs, 8, tb.w) * one.top + 1)
        TEST_FAIL(1, _SL("${int} wide, the tabs are ${int} high (want ${int})"), stvar(int32, tb.w), stvar(int32, tb.h),
                  stvar(int32, 40 + tabLines(tabs, 8, tb.w) * one.top + 1));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static int test_cxui_grp_tabs(void)
{
    TEST_CHAIN(test_cxui_tabsbuild, test_cxui_tabspick, test_cxui_tabskeys, test_cxui_tabswrap);
}

// ---- scroll boxes ----

// A window and what sits on it take CXUI_PAL_Panel; a scroll box shows the background around it
// unless given a role; a panel or a palette of the program's own picks another.
UI_TEST(backgrounds)
{
    CxUiBox* col          = cxuiBoxCreateV();
    CxUiScrollBox* plain  = put(col, cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V }));
    CxUiScrollBox* own    = put(col, cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V,
                                                                                .bgRole = CXUI_PAL_Input }));
    CxUiPanel* content    = put(col, cxuiPanelCreate(&(CxUiPanelDesc){ .orient = CXUI_ORIENT_V,
                                                                       .bgRole = CXUI_PAL_Content }));
    CxUiScrollBox* inside = put(content, cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V }));
    CxUiWindow* w         = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200, 200 } });
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);

    CxUiPalette* pal = cxuiWidgetPalette(w);
    CxUiColor panel  = cxuiPaletteColor(pal, CXUI_PAL_Panel);
    CxUiTestWindowInfo wi;
    CxUiTestPanelInfo pp, po, pi;
    cxuiTestWindowInfo(w, &wi);
    cxuiTestPanelInfo(CxUiWidget(plain), &pp);
    cxuiTestPanelInfo(CxUiWidget(own), &po);
    cxuiTestPanelInfo(CxUiWidget(inside), &pi);
    if (wi.bg.bits != panel.bits || pp.bg.bits != panel.bits ||
        po.bg.bits != cxuiPaletteColor(pal, CXUI_PAL_Input).bits ||
        pi.bg.bits != cxuiPaletteColor(pal, CXUI_PAL_Content).bits || panel.bits == cxuiPaletteColor(pal, CXUI_PAL_Content).bits)
        TEST_FAIL(1, _SL("the window ${uint(hex)}, a scroll box on it ${uint(hex)}, one given Input ${uint(hex)}, one on "
                         "Content ${uint(hex)}"),
                  stvar(uint32, wi.bg.bits), stvar(uint32, pp.bg.bits), stvar(uint32, po.bg.bits), stvar(uint32, pi.bg.bits));

    // A palette whose Panel is white
    CxUiPalette* light = cxuiPaletteClone(pal);
    cxuiPaletteSetColor(light, CXUI_PAL_Panel, cxuiHex(0xffffff));
    cxuiWidgetSetPalette(w, light);
    objRelease(&light);
    cxuiTestWindowInfo(w, &wi);
    cxuiTestPanelInfo(CxUiWidget(plain), &pp);
    if (wi.bg.bits != cxuiHex(0xffffff).bits || pp.bg.bits != cxuiHex(0xffffff).bits)
        TEST_FAIL(1, _SL("with the window's own palette: the window ${uint(hex)}, a scroll box ${uint(hex)}"),
                  stvar(uint32, wi.bg.bits), stvar(uint32, pp.bg.bits));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// A scroll box lays its children out over content as tall as they need and as wide as the box,
// their peers placed on the content; it asks only for the view it was given; it shows a widget
// on request and one that gets focus, except from a click, and the wheel scrolls it, without
// laying anything out again; one that scrolls across makes its content as wide as the children
// need.
UI_TEST(scrollbox)
{
    CxUiBox* col      = cxuiBoxCreateV();
    CxUiScrollBox* sb =
        put(col, cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .view = { 0, 100 }, .gap = 4 }));
    cxuiWidgetSetExpand(sb, CXUI_EXPAND_Both);
    CxUiLabel* labels[20];
    for (int32 i = 0; i < 20; i++) {
        string t = 0;
        strFormat(&t, _SL("Line ${int}"), stvar(int32, i));
        labels[i] = put(sb, cxuiLabelCreate(&(CxUiLabelDesc){ .text = t }));
        strDestroy(&t);
    }
    CxUiCanvas* cv = put(sb, cxuiCanvasCreate(NULL));
    cxuiWidgetSetNaturalSize(cv, cxuiLen(0), cxuiLen(30));
    CxUiTextField* last = put(sb, cxuiTextFieldCreate(NULL));
    CxUiTextField* top  = put(col, cxuiTextFieldCreate(NULL));
    CxUiScrollBox* across =
        put(col, cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_H, .scroll = CXUI_EXPAND_H,
                                                           .view = { 120, 0 } }));
    for (int32 i = 0; i < 10; i++)
        put(across, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"Wide label" }));
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 260 }, .sizing = CXUI_SIZE_Natural });
    cxuiContainerAppend(w, col);
    objRelease(&col);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    settle(w);

    CxUiSize sn = cxuiNaturalSize(sb);
    CxUiRect sbb, l19, lastB;
    cxuiTestBounds(CxUiWidget(sb), &sbb);
    cxuiTestBounds(CxUiWidget(labels[19]), &l19);
    cxuiTestBounds(CxUiWidget(last), &lastB);
    CxUiTestPanelInfo pi;
    cxuiTestPanelInfo(CxUiWidget(sb), &pi);
    CxUiTestControlInfo li = controlInfo(labels[19]);
    bool placed = li.bounds.y == l19.y - sbb.y && li.bounds.x == l19.x - sbb.x;
    cxuiTestControlInfoDestroy(&li);
    if (sn.h != 100 || pi.content.w != sbb.w || pi.content.h < lastB.y + lastB.h - sbb.y || l19.y < sbb.y + sbb.h ||
        !placed)
        TEST_FAIL(1, _SL("natural ${int} high, box ${int}x${int}, content ${int}x${int}, line 19 at ${int}, peer placed ${int}"),
                  stvar(int32, sn.h), stvar(int32, sbb.w), stvar(int32, sbb.h), stvar(int32, pi.content.w),
                  stvar(int32, pi.content.h), stvar(int32, l19.y), stvar(int32, placed));

    CxUiPoint pos;
    cxuiScrollBoxShow(sb, CxUiWidget(labels[19]));
    cxuiTestScrollPos(sb, &pos);
    if (pos.y != l19.y + l19.h - (sbb.y + sbb.h))
        TEST_FAIL(1, _SL("showing line 19 scrolled to ${int}, want ${int}"), stvar(int32, pos.y),
                  stvar(int32, l19.y + l19.h - (sbb.y + sbb.h)));
    cxuiScrollBoxShow(sb, CxUiWidget(labels[0]));
    cxuiTestScrollPos(sb, &pos);
    if (pos.y != 0)
        TEST_FAIL(1, _SL("showing line 0 left it at ${int}"), stvar(int32, pos.y));
    cxuiTestPointer(cv, CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 5);
    cxuiTestPointer(cv, CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 5, 5);
    cxuiTestScrollPos(sb, &pos);
    if (cxuiFocused(w) != CxUiWidget(cv) || pos.y != 0)
        TEST_FAIL(1, _SL("a click giving a canvas focus scrolled to ${int}"), stvar(int32, pos.y));
    cxuiFocus(top);
    cxuiFocus(cv);
    cxuiTestScrollPos(sb, &pos);
    if (pos.y == 0)
        TEST_FAIL(1, _SL("focusing a canvas from code didn't scroll to it"), stvNone);
    cxuiFocus(last);
    cxuiTestScrollPos(sb, &pos);
    if (pos.y != pi.content.h - sbb.h)
        TEST_FAIL(1, _SL("focusing the last field scrolled to ${int} of ${int}"), stvar(int32, pos.y),
                  stvar(int32, pi.content.h - sbb.h));
    cxuiFocus(top);
    int32 passes = cxuiTestLayoutPasses(w);
    cxuiTestScrollWheel(sb, -1);
    CxUiPoint after;
    cxuiTestScrollPos(sb, &after);
    li           = controlInfo(labels[19]);
    bool unmoved = li.bounds.y == l19.y - sbb.y;
    cxuiTestControlInfoDestroy(&li);
    if (after.y >= pos.y || !unmoved || cxuiTestLayoutPasses(w) != passes)
        TEST_FAIL(1, _SL("the wheel went from ${int} to ${int}, peers moved ${int}, ${int} more layout passes"),
                  stvar(int32, pos.y), stvar(int32, after.y), stvar(int32, !unmoved),
                  stvar(int32, cxuiTestLayoutPasses(w) - passes));

    CxUiRect ab;
    cxuiTestBounds(CxUiWidget(across), &ab);
    cxuiTestPanelInfo(CxUiWidget(across), &pi);
    if (cxuiNaturalSize(across).w != 120 || pi.content.w <= ab.w || pi.content.h != ab.h)
        TEST_FAIL(1, _SL("across: box ${int}x${int}, content ${int}x${int}"), stvar(int32, ab.w), stvar(int32, ab.h),
                  stvar(int32, pi.content.w), stvar(int32, pi.content.h));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

// A frame's title reaches its peer in the palette's text color, takes a line's room at the top,
// and keeps the frame at least as wide as it with its gap; without one the content moves back up.
UI_TEST(frametitle)
{
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiFrame* plain = put(col, cxuiFrameCreate(NULL));
    CxUiLabel* in0   = put(plain, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"x" }));
    CxUiFrame* f     = put(col, cxuiFrameCreate(&(CxUiPanelDesc){ .title = _S"A rather long title" }));
    CxUiLabel* in1   = put(f, cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"x" }));
    CxUiWindow* w    = startWindow(300, 200, CxUiWidget(col));
    settle(w);

    CxUiRect fb0, fb1, lb0, lb1;
    cxuiTestBounds(CxUiWidget(plain), &fb0);
    cxuiTestBounds(CxUiWidget(f), &fb1);
    cxuiTestBounds(CxUiWidget(in0), &lb0);
    cxuiTestBounds(CxUiWidget(in1), &lb1);
    CxUiTestPanelInfo pi;
    cxuiTestPanelInfo(CxUiWidget(f), &pi);
    CxUiPalette* pal = cxuiWidgetPalette(f);
    CxUiFont* font   = cxuiWidgetFont(f);
    CxUiFontMetrics fm;
    cxuiFontMetrics(font, &fm);
    int32 down0 = lb0.y - fb0.y, down1 = lb1.y - fb1.y;
    int32 title = cxuiFontAdvance(font, _S"A rather long title");
    if (!strEq(pi.title, _S"A rather long title") || pi.titleColor.bits != cxuiPaletteColor(pal, CXUI_PAL_Text).bits ||
        down1 != fm.height || down0 != 1 || cxuiNaturalSize(f).w < title + 22 ||
        !strEq(cxuiFrameTitle(f), _S"A rather long title") || cxuiFrameTitle(plain))
        TEST_FAIL(1, _SL("title \"${string}\", content ${int} down (${int} without, a line ${int}), ${int} wide for ${int}"),
                  stvar(strref, pi.title), stvar(int32, down1), stvar(int32, down0), stvar(int32, fm.height),
                  stvar(int32, cxuiNaturalSize(f).w), stvar(int32, title));

    cxuiWidgetSetEnabled(f, false);
    settle(w);
    cxuiTestPanelInfo(CxUiWidget(f), &pi);
    bool dim = pi.titleColor.bits == cxuiPaletteColor(pal, CXUI_PAL_TextDisabled).bits;
    cxuiFrameSetTitle(f, NULL);
    settle(w);
    cxuiTestBounds(CxUiWidget(in1), &lb1);
    cxuiTestBounds(CxUiWidget(f), &fb1);
    cxuiTestPanelInfo(CxUiWidget(f), &pi);
    if (!dim || !strEmpty(pi.title) || lb1.y - fb1.y != 1)
        TEST_FAIL(1, _SL("disabled title dimmed ${int}; without a title the content is ${int} down"), stvar(int32, dim),
                  stvar(int32, lb1.y - fb1.y));
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static CxUiResult linkOpened(stvlist* cvars, CxUiActivateEvent* ev)
{
    unused_noeval(ev);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return CXUI_Default;
}

// A link keeps its URL; a press and release over it opens it, one let go outside doesn't; Space
// and Enter open it while it has focus, and Tab stops at it unless made without focus; disabled,
// nothing opens it.
UI_TEST(linkbasic)
{
    int32 opened    = 0;
    CxUiBox* col    = cxuiBoxCreateV();
    CxUiLink* link  = put(col, cxuiLinkCreate(&(CxUiLinkDesc){ .text = _S"Release notes", .url = _S"https://x/notes" }));
    CxUiLink* plain = put(col, cxuiLinkCreate(&(CxUiLinkDesc){ .text = _S"No focus", .noFocus = true }));
    cxuiOnActivate(link, linkOpened, stvar(ptr, &opened));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    settle(w);
    CxUiCanvas* cv = CxUiCanvas(link);
    if (!strEq(cxuiLinkUrl(link), _S"https://x/notes") || cxuiNaturalSize(link).w <= cxuiNaturalSize(plain).w ||
        !cxuiWidgetCanFocus(link) || cxuiWidgetCanFocus(plain))
        TEST_FAIL(1, _SL("URL, size or focus"), stvNone);

    cxuiTestPointer(cv, CXUI_TESTPTR_Move, 0, 0, 0, 5, 5);
    cxuiTestPointer(cv, CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 5);
    cxuiTestPointer(cv, CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 5, 5);
    cxuiTestPointer(cv, CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 5);
    cxuiTestPointer(cv, CXUI_TESTPTR_Move, 0, CXUI_BTN_Left, 0, 500, 5);
    cxuiTestPointer(cv, CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 500, 5);
    if (opened != 1)
        TEST_FAIL(1, _SL("a click and one let go outside: opened ${int} times"), stvar(int32, opened));
    cxuiFocus(link);
    pressKey(link, CXUI_KEY_Enter, 0);
    pressKey(link, CXUI_KEY_Space, 0);
    pressKey(link, CXUI_KEY_Enter, CXUI_MOD_Ctrl);
    if (opened != 3 || cxuiFocused(w) != CxUiWidget(link))
        TEST_FAIL(1, _SL("Enter, Space and Ctrl+Enter: opened ${int} more times"), stvar(int32, opened - 1));
    cxuiWidgetSetEnabled(link, false);
    settle(w);
    cxuiTestPointer(cv, CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 5, 5);
    cxuiTestPointer(cv, CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 5, 5);
    if (opened != 3)
        TEST_FAIL(1, _SL("a disabled link opened"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static bool tipIs(void* w, strref want)
{
    string got = 0;
    bool ok    = cxuiTestTip((CxUiWidget*)w, &got) && strEq(got, want);
    if (!ok)
        logFmt(Info, _SL("tip \"${string}\", want \"${string}\""), stvar(strref, got), stvar(strref, want));
    strDestroy(&got);
    return ok;
}

// A tip set before realizing reaches the peer, and so do changes after; NULL takes it away.
UI_TEST(tips)
{
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiButton* b    = put(col, cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"Go" }));
    CxUiTextField* t = put(col, cxuiTextFieldCreate(NULL));
    CxUiLink* l      = put(col, cxuiLinkCreate(&(CxUiLinkDesc){ .text = _S"Link" }));
    cxuiWidgetSetTip(b, _S"Goes there\nnow");
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    settle(w);
    cxuiWidgetSetTip(t, _S"Type here");
    cxuiWidgetSetTip(l, _S"Opens it");
    if (!tipIs(b, _S"Goes there\nnow") || !tipIs(t, _S"Type here") || !tipIs(l, _S"Opens it") ||
        !strEq(cxuiWidgetTip(t), _S"Type here"))
        TEST_FAIL(1, _SL("a tip didn't reach its peer"), stvNone);
    cxuiWidgetSetTip(b, NULL);
    if (!tipIs(b, NULL) || cxuiWidgetTip(b))
        TEST_FAIL(1, _SL("a tip taken away is still there"), stvNone);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

static int test_cxui_grp_text(void)
{
    TEST_CHAIN(test_cxui_textbasic, test_cxui_textnative, test_cxui_textcommit, test_cxui_textfocus,
               test_cxui_textframe, test_cxui_textarea);
}

static int test_cxui_grp_controls(void)
{
    TEST_CHAIN(test_cxui_controlbasic, test_cxui_controlpress, test_cxui_controldefault, test_cxui_controlmnemonic,
               test_cxui_controlstart, test_cxui_wrapcolumn, test_cxui_wrapshrink, test_cxui_wrapcontainers);
}

static int test_cxui_grp_checks(void)
{
    TEST_CHAIN(test_cxui_checkbasic, test_cxui_radiogroup);
}

static int test_cxui_grp_lists(void)
{
    TEST_CHAIN(test_cxui_listbasic, test_cxui_dropdownbasic, test_cxui_listmulti, test_cxui_listupdate);
}

static int test_cxui_grp_sliders(void)
{
    TEST_CHAIN(test_cxui_sliderbasic, test_cxui_progressbasic);
}

static int test_cxui_grp_frames(void)
{
    TEST_CHAIN(test_cxui_scrollbox, test_cxui_backgrounds, test_cxui_frametitle, test_cxui_linkbasic, test_cxui_tips);
}

static int test_cxui_grp_input(void)
{
    TEST_CHAIN(test_cxui_inputcapture, test_cxui_inputdouble, test_cxui_inputlost, test_cxui_inputdrag,
               test_cxui_inputdip, test_cxui_inputkeys, test_cxui_inputunmapped, test_cxui_inputtext,
               test_cxui_pointerdelta, test_cxui_pointerlock, test_cxui_pointerlockdrag, test_cxui_keycapture,
               test_cxui_cursorimage);
}

// ---- render views ----

typedef struct SurfLog {
    CxUiRenderView* view;
    int32 available, going, paints, resizes, presses;
    bool drawNull;       // every paint came without a draw
    bool readInGoing;    // the surface read back while it was going
    bool destroyInGoing; // the going handler destroys the view
    CxUiSize resized;
    CxUiRect dirty;      // the last paint's
    CxUiNativeSurface last;
} SurfLog;

static CxUiResult surfChanged(stvlist* cvars, CxUiSurfaceEvent* ev)
{
    SurfLog* log = stvlNextPtr(cvars);
    if (ev->available) {
        log->available++;
        cxuiRenderViewSurface(log->view, &log->last);
    } else {
        log->going++;
        CxUiNativeSurface s;
        log->readInGoing = cxuiRenderViewSurface(log->view, &s) && s.native == log->last.native;
        if (log->destroyInGoing)
            cxuiDestroy(log->view);
    }
    return CXUI_Default;
}

static CxUiResult surfPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    SurfLog* log = stvlNextPtr(cvars);
    log->paints++;
    log->drawNull = log->drawNull && !ev->draw;
    log->dirty    = ev->dirty;
    return CXUI_Handled;
}

static CxUiResult surfResize(stvlist* cvars, CxUiResizeEvent* ev)
{
    SurfLog* log = stvlNextPtr(cvars);
    log->resizes++;
    log->resized = ev->size;
    return CXUI_Default;
}

static CxUiResult surfMouse(stvlist* cvars, CxUiMouseEvent* ev)
{
    SurfLog* log = stvlNextPtr(cvars);
    if (ev->action == CXUI_MOUSE_Down)
        log->presses++;
    return CXUI_Default;
}

// A render view's surface comes after it is shown, from the queue, with one event; it is the
// view's size in pixels, and a paint asks for a frame without a draw. Taken off the screen, the
// event that says the surface is going comes while it still reads back, and putting the view
// back brings it again; destroying the window says it is going first.
UI_TEST(renderview)
{
    cxuiTestSetSystemScale(2.0f);
    SurfLog log      = { .drawNull = true };
    CxUiBox* col     = cxuiBoxCreateV();
    CxUiRenderView* v = cxuiRenderViewCreate(&(CxUiRenderViewDesc){ .name = _S"view" });
    log.view          = v;
    cxuiOnSurface(v, surfChanged, stvar(ptr, &log));
    cxuiOnPaint(v, surfPaint, stvar(ptr, &log));
    cxuiOnResize(v, surfResize, stvar(ptr, &log));
    cxuiOnMouse(v, surfMouse, stvar(ptr, &log));
    put(col, v);
    CxUiNativeSurface s;
    if (cxuiRenderViewSurface(v, &s))
        TEST_FAIL(1, _SL("a surface before showing"), stvNone);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(col));
    if (log.available != 0)
        TEST_FAIL(1, _SL("the surface event came in the middle of showing the window"), stvNone);
    // Taken out and put back before the event: it comes once.
    CxUiRenderView* early = objAcquire(v);
    cxuiContainerDetach(col, v);
    cxuiContainerAppend(col, v);
    objRelease(&early);
    cxuiRefresh(w);
    settle(w);
    if (log.available != 1 || log.going != 0 || !cxuiRenderViewSurface(v, &s) || s.native != log.last.native || s.w != 600 || s.h != 400 ||
        log.resizes < 1 || log.resized.w != 600 || log.resized.h != 400)
        TEST_FAIL(1, _SL("surface ${int} times, ${int}x${int}; resized to ${int}x${int}"), stvar(int32, log.available),
                  stvar(int32, s.w), stvar(int32, s.h), stvar(int32, log.resized.w), stvar(int32, log.resized.h));

    // Paints ask for a frame and draw nothing; marked or not, the renderer's frame stays.
    int32 paints = log.paints;
    cxuiTestSurfaceMark(v);
    cxuiCanvasInvalidate(v, 10, 10, 5, 5);
    cxuiTestResize(w, 320, 220);
    settle(w);
    CxUiTestSurfaceInfo si;
    if (log.paints <= paints || !log.drawNull || !cxuiTestSurfaceInfo(v, &si) || !si.marked || !si.shown ||
        si.bounds.w != 640 || !rectIs(log.dirty, 0, 0, 640, si.bounds.h))
        TEST_FAIL(1, _SL("${int} paints, with a draw ${int}, marked ${int}, ${int} wide"), stvar(int32, log.paints - paints),
                  stvar(int32, !log.drawNull), stvar(int32, si.marked), stvar(int32, si.bounds.w));
    cxuiTestPointer(CxUiCanvas(v), P_Move, 0, 0, 0, 10, 10);
    cxuiTestPointer(CxUiCanvas(v), P_Press, L, 0, 0, 10, 10);
    cxuiTestPointer(CxUiCanvas(v), P_Release, L, 0, 0, 10, 10);
    if (log.presses != 1 || cxuiFocused(w) != CxUiWidget(v))
        TEST_FAIL(1, _SL("a click: ${int} presses, focused ${int}"), stvar(int32, log.presses),
                  stvar(int32, cxuiFocused(w) == CxUiWidget(v)));

    // Taken out and put back
    CxUiRenderView* held = objAcquire(v);
    cxuiContainerDetach(col, v);
    if (log.going != 1 || !log.readInGoing || cxuiRenderViewSurface(v, &s))
        TEST_FAIL(1, _SL("taken out: ${int} going events, read back during it ${int}"), stvar(int32, log.going),
                  stvar(int32, log.readInGoing));
    cxuiContainerAppend(col, v);
    objRelease(&held);
    settle(w);
    if (log.available != 2 || !cxuiRenderViewSurface(v, &s))
        TEST_FAIL(1, _SL("put back: ${int} surface events"), stvar(int32, log.available));

    log.readInGoing = false;
    cxuiDestroy(w);
    objRelease(&w);
    if (log.going != 2 || !log.readInGoing)
        TEST_FAIL(1, _SL("destroying the window: ${int} going events, read back ${int}"), stvar(int32, log.going),
                  stvar(int32, log.readInGoing));

    // A handler that destroys the view as its surface goes hears of it once, and another view
    // still hears its own go.
    SurfLog log2 = { .drawNull = true, .destroyInGoing = true };
    SurfLog log3 = { .drawNull = true };
    col          = cxuiBoxCreateV();
    log2.view    = put(col, cxuiRenderViewCreate(NULL));
    log3.view    = put(col, cxuiRenderViewCreate(NULL));
    cxuiOnSurface(log2.view, surfChanged, stvar(ptr, &log2));
    cxuiOnSurface(log3.view, surfChanged, stvar(ptr, &log3));
    w = startWindow(300, 200, CxUiWidget(col));
    settle(w);
    cxuiContainerDetach(col, log2.view);
    if (log2.available != 1 || log2.going != 1)
        TEST_FAIL(1, _SL("destroyed by its going handler: ${int} going events"), stvar(int32, log2.going));
    cxuiDestroy(w);
    objRelease(&w);
    if (log3.available != 1 || log3.going != 1)
        TEST_FAIL(1, _SL("the other view: ${int} surface events, ${int} going"), stvar(int32, log3.available),
                  stvar(int32, log3.going));
    return 0;
}

static int test_cxui_grp_canvas(void)
{
    TEST_CHAIN(test_cxui_canvasbasic, test_cxui_canvasdraw, test_cxui_canvasdip, test_cxui_canvasdpi,
               test_cxui_canvaspaintnow, test_cxui_canvaslifetime, test_cxui_canvastext, test_cxui_canvastextdip,
               test_cxui_canvasimage, test_cxui_canvascellsdip, test_cxui_renderview);
}

static int test_cxui_grp_modal(void)
{
    TEST_CHAIN(test_cxui_popup, test_cxui_popupnested, test_cxui_popuptimer, test_cxui_popupappmodal, test_cxui_msgbox,
               test_cxui_msgboxcustom, test_cxui_msgboxthread, test_cxui_showmodal, test_cxui_progressdialog, test_cxui_filedialog,
               test_cxui_colordialog, test_cxui_colorthread, test_cxui_modalshutdown);
}

static int test_cxui_grp_windows(void)
{
    TEST_CHAIN(test_cxui_windowmodel, test_cxui_windowtheme, test_cxui_windowclose, test_cxui_windowcontent,
               test_cxui_windowlifetime, test_cxui_windowoffthread, test_cxui_windowowner, test_cxui_windownear);
}

static int test_cxui_grp_keystimers(void)
{
    TEST_CHAIN(test_cxui_keyroundtrip, test_cxui_keynames, test_cxui_keymatch, test_cxui_keylabel, test_cxui_keylocks,
               test_cxui_timerrepeat, test_cxui_timerstartstop, test_cxui_timershutdown);
}

static int test_cxui_grp_widgets(void)
{
    TEST_CHAIN(test_cxui_slots, test_cxui_bubble, test_cxui_selfreplace, test_cxui_destroy,
               test_cxui_destroyindispatch, test_cxui_offthread, test_cxui_wrappers, test_cxui_props);
}

static int test_cxui_grp_runtime(void)
{
    TEST_CHAIN(test_cxui_startstop, test_cxui_startcount, test_cxui_startonui, test_cxui_post,
               test_cxui_callsync, test_cxui_budget, test_cxui_schedule, test_cxui_shutdowndrop);
}

// ---- grids ----

static void addNumbered(CxUiListStore* store, int32 i)
{
    string name = 0, size = 0;
    strFormat(&name, _S"row ${int}", stvar(int32, i));
    strFormat(&size, _S"${int} KB", stvar(int32, i));
    cxuiListStoreAppend(store, name, size);
    strDestroy(&name);
    strDestroy(&size);
}

// A row of a three-column store: a check cell with "xN", then "row N" and "N KB"
static void addNumberedChecked(CxUiListStore* store, int32 i)
{
    string x = 0, name = 0, size = 0;
    strFormat(&x, _S"x${int}", stvar(int32, i));
    strFormat(&name, _S"row ${int}", stvar(int32, i));
    strFormat(&size, _S"${int} KB", stvar(int32, i));
    cxuiListStoreAppend(store, x, name, size);
    strDestroy(&x);
    strDestroy(&name);
    strDestroy(&size);
}

static CxUiListStore* numberedStore(int32 n)
{
    CxUiListStore* store = cxuiListStoreCreate(2);
    for (int32 i = 0; i < n; i++)
        addNumbered(store, i);
    return store;
}

// A grid of "row n" and "n KB", 300x200 on its own in a window
static CxUiGrid* numberedGrid(CxUiListStore* store, CxUiWindow** win)
{
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .flags = CXUI_COL_Stretch },
                                        { .title = _S"Size", .width = cxuiLen(80), .align = CXUI_ALIGN_End } },
        .ncolumns = 2 });
    cxuiGridSetSource(g, store);
    *win = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(*win);
    return g;
}

// The text drawn as `text` in the grid's last paint, or NULL
static const CxUiTestDrawOp* gridText(CxUiGrid* g, strref text)
{
    CxUiTestCanvasInfo info;
    if (!cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info))
        return NULL;
    for (int32 i = 0; i < info.nops; i++) {
        if (info.ops[i].kind == CXUI_TESTOP_Text && strEq(info.ops[i].text, text))
            return &info.ops[i];
    }
    return NULL;
}

static void gridRepaint(CxUiGrid* g, CxUiWindow* w)
{
    cxuiCanvasInvalidateAll(g->_body);
    settle(w);
}

// Row and header heights in physical pixels
static void gridHeights(CxUiGrid* g, int32* rowH, int32* headH, int32* textH)
{
    CxUiFontMetrics fm;
    cxuiFontMetrics(cxuiWidgetFont(g->_body), &fm);
    *textH = fm.height;
    *rowH  = fm.height + 4;
    *headH = fm.height + 8;
}

// Rows take the height the program gives, at every scale, with their text in the middle.
UI_TEST(gridrowheight)
{
    CxUiListStore* store = numberedStore(100);
    CxUiGrid* g          = cxuiGridCreate(&(CxUiGridDesc){ .rowHeight = cxuiLen(23), .flags = CXUI_GRID_NoHeader });
    cxuiGridSetSource(g, store);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
    const CxUiTestDrawOp* r1 = gridText(g, _S"row 1");
    if (g->_rowH != 23 || g->_page != 200 / 23 || !r1 || r1->a.y != 23 + (23 - textH) / 2)
        TEST_FAIL(1, _SL("rows ${int} high, ${int} a page, row 1's text at ${int}"), stvar(int32, g->_rowH),
                  stvar(int64, g->_page), stvar(int32, r1 ? r1->a.y : -1));
    cxuiTestDpi(w, 2.0f);
    settle(w);
    if (g->_rowH != 46)
        TEST_FAIL(1, _SL("at scale 2, rows ${int} high"), stvar(int32, g->_rowH));
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// A grid lays out its columns and a vertical scroll bar, and draws the headers and the rows on
// screen in the palette's colors, with cells shortened to fit and a row's own colors and flags.
UI_TEST(gridpaint)
{
    CxUiListStore* store = numberedStore(100);
    CxUiWindow* w;
    CxUiGrid* g = numberedGrid(store, &w);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
    int64 page = (200 - headH) / rowH;

    CHECK_AT("grid body", g->_body, 1.0f, 0, 0, 288, 200);
    CHECK_AT("vertical bar", g->_vbar, 1.0f, 288, 0, 12, 200);
    if (CxUiWidget(g->_hbar)->_bounds.w != 0 || CxUiWidget(g->_hbar)->_bounds.h != 0)
        TEST_FAIL(1, _SL("the across bar is shown with nothing to scroll"), stvNone);
    if (cxuiGridColumnWidth(g, 0) != 208 || cxuiGridColumnWidth(g, 1) != 80)
        TEST_FAIL(1, _SL("columns ${int} and ${int} wide, wanted 208 and 80"), stvar(int32, cxuiGridColumnWidth(g, 0)),
                  stvar(int32, cxuiGridColumnWidth(g, 1)));
    if (cxuiGridRowCount(g) != 100)
        TEST_FAIL(1, _SL("${int} rows, wanted 100"), stvar(int64, cxuiGridRowCount(g)));

    const CxUiTestDrawOp* name = gridText(g, _S"Name");
    const CxUiTestDrawOp* size = gridText(g, _S"Size");
    if (!name || name->a.x != 6 || name->a.y != (headH - textH) / 2 ||
        name->c.bits != palColor(g, CXUI_PAL_GridHeaderText).bits)
        TEST_FAIL(1, _SL("the Name header at ${int},${int}"), stvar(int32, name ? name->a.x : -1),
                  stvar(int32, name ? name->a.y : -1));
    if (!size || size->r.x + size->r.w != 288 - 6)
        TEST_FAIL(1, _SL("the Size header, aligned at the end, ends at ${int}"),
                  stvar(int32, size ? size->r.x + size->r.w : -1));
    const CxUiTestDrawOp* head = findOp(CxUiCanvas(g->_body), CXUI_TESTOP_Fill, 1);
    if (!head || head->r.h != headH || head->c.bits != palColor(g, CXUI_PAL_GridHeader).bits)
        TEST_FAIL(1, _SL("the headers' background is ${int} high"), stvar(int32, head ? head->r.h : -1));

    const CxUiTestDrawOp* r0 = gridText(g, _S"row 0");
    const CxUiTestDrawOp* s0 = gridText(g, _S"0 KB");
    if (!r0 || r0->a.y != headH + (rowH - textH) / 2 || r0->c.bits != palColor(g, CXUI_PAL_Text).bits)
        TEST_FAIL(1, _SL("row 0 drawn at ${int}"), stvar(int32, r0 ? r0->a.y : -1));
    if (!s0 || s0->r.x + s0->r.w != 288 - 6)
        TEST_FAIL(1, _SL("row 0's size ends at ${int}"), stvar(int32, s0 ? s0->r.x + s0->r.w : -1));
    // The rows that fit, the one cut off at the bottom, and none after
    string cut = 0, past = 0;
    strFormat(&cut, _S"row ${int}", stvar(int64, page));
    strFormat(&past, _S"row ${int}", stvar(int64, page + 1));
    bool partial = (200 - headH) % rowH != 0;
    if ((gridText(g, cut) != NULL) != partial || gridText(g, past))
        TEST_FAIL(1, _SL("rows past the bottom: ${string} drawn ${int}, ${string} ${int}"), stvar(string, cut),
                  stvar(int32, gridText(g, cut) != NULL), stvar(string, past), stvar(int32, gridText(g, past) != NULL));
    strDestroy(&cut);
    strDestroy(&past);

    // A change paints only its row, and text too long ends with an ellipsis.
    cxuiListStoreSetText(store, 1, 1, _S"1234567890 KB");
    settle(w);
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    if (info.lastDirty.y != headH + rowH || info.lastDirty.h != rowH)
        TEST_FAIL(1, _SL("a changed row painted ${int} high at ${int}"), stvar(int32, info.lastDirty.h),
                  stvar(int32, info.lastDirty.y));
    if (!gridText(g, _S"1234567\xE2\x80\xA6"))
        TEST_FAIL(1, _SL("a long cell wasn't shortened to fit"), stvNone);

    // A row's own colors, and a dim row
    CxUiColor red = cxuiHex(0xC00000), blue = cxuiHex(0x0000C0);
    cxuiListStoreSetColors(store, 2, red, blue);
    cxuiListStoreSetFlags(store, 3, CXUI_CELL_Dim);
    gridRepaint(g, w);
    const CxUiTestDrawOp* r2 = gridText(g, _S"row 2");
    const CxUiTestDrawOp* r3 = gridText(g, _S"row 3");
    if (!r2 || r2->c.bits != red.bits || !r3 || r3->c.bits != palColor(g, CXUI_PAL_TextDim).bits)
        TEST_FAIL(1, _SL("a row's own text color, or a dim row's, wasn't used"), stvNone);
    bool bg = false;
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    for (int32 i = 0; i < info.nops; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        if (op->kind == CXUI_TESTOP_Fill && op->c.bits == blue.bits && op->r.y == headH + 2 * rowH && op->r.h == rowH)
            bg = true;
    }
    if (!bg)
        TEST_FAIL(1, _SL("a row's own background wasn't drawn"), stvNone);

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// The text a cell's room shows, as the null backend measures: as much as fits with an ellipsis
static void fitWant(string* out, CxUiGrid* g, strref text, int32 room)
{
    CxUiFontMetrics fm;
    cxuiFontMetrics(cxuiWidgetFont(g->_body), &fm);
    int32 len = (int32)strLen(text), n = len;
    if (n * fm.cellW > room)
        n = max(room / fm.cellW - 1, 0);
    strSubStr(out, text, 0, n);
    if (n < len)
        strAppend(out, _S"\xE2\x80\xA6");
}

// How many clips the grid's last paint set inside its rows, a row high or less
static int32 gridRowClips(CxUiGrid* g, int32 headH, int32 rowH)
{
    CxUiTestCanvasInfo info;
    int32 n = 0;
    if (!cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info))
        return -1;
    for (int32 i = 0; i < info.nops; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        if (op->kind == CXUI_TESTOP_Clip && op->r.y >= headH && op->r.h <= rowH)
            n++;
    }
    return n;
}

// Text that doesn't fit is shortened by its own content, the room it has and the font, and aligned
// from the width remembered; a repaint measures nothing it measured before.
UI_TEST(gridfit)
{
    CxUiListStore* store = numberedStore(20);
    CxUiWindow* w;
    CxUiGrid* g = numberedGrid(store, &w);
    // Size is 80 across, 6 padding either side, right aligned to 288 - 6.
    cxuiListStoreSetText(store, 1, 1, _S"1234567890 KB");
    gridRepaint(g, w);
    int32 before = cxuiTestFontMeasures();
    gridRepaint(g, w);
    if (cxuiTestFontMeasures() != before)
        TEST_FAIL(1, _SL("a repaint measured text ${int} times"), stvar(int32, cxuiTestFontMeasures() - before));

    string want = 0;
    static const strref texts[] = { _S"1234567890 KB", _S"abcdefghij KB" };
    static const int32 widths[] = { 80, 160, 80 };
    for (int32 t = 0; t < 2; t++) {
        cxuiListStoreSetText(store, 1, 1, texts[t]);
        for (int32 i = 0; i < 3; i++) {
            cxuiGridSetColumnWidth(g, 1, cxuiLen(widths[i]));
            settle(w);
            fitWant(&want, g, texts[t], widths[i] - 12);
            const CxUiTestDrawOp* op = gridText(g, want);
            if (!op || op->r.x + op->r.w != 288 - 6)
                TEST_FAIL(1, _SL("${string} in ${int}: wanted ${string} ending at 282, got ${int}"), stvar(strref, texts[t]),
                          stvar(int32, widths[i]), stvar(string, want), stvar(int32, op ? op->r.x + op->r.w : -1));
        }
    }
    // Every width from 40 to 200, so that some share a remembered place
    for (int32 wd = 40; wd <= 200; wd++) {
        cxuiGridSetColumnWidth(g, 1, cxuiLen(wd));
        settle(w);
        fitWant(&want, g, texts[1], wd - 12);
        if (!gridText(g, want))
            TEST_FAIL(1, _SL("${int} across: wanted ${string}"), stvar(int32, wd), stvar(string, want));
    }
    // Centered: the room left over split either side
    cxuiGridSetColumns(g, { .title = _S"Name", .flags = CXUI_COL_Stretch },
                       { .title = _S"Size", .width = cxuiLen(160), .align = CXUI_ALIGN_Center });
    settle(w);
    const CxUiTestDrawOp* mid = gridText(g, texts[1]);
    CxUiFontMetrics fm;
    cxuiFontMetrics(cxuiWidgetFont(g->_body), &fm);
    int32 adv = strLen(texts[1]) * fm.cellW;
    if (!mid || mid->r.x != 288 - 160 + 6 + (160 - 12 - adv) / 2)
        TEST_FAIL(1, _SL("centered text at ${int}"), stvar(int32, mid ? mid->r.x : -1));

    // Fitted cells are drawn without clipping; one too narrow for its box, or for even the
    // ellipsis, is clipped.
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
    gridRepaint(g, w);
    if (gridRowClips(g, headH, rowH) != 0)
        TEST_FAIL(1, _SL("fitted cells were clipped ${int} times"), stvar(int32, gridRowClips(g, headH, rowH)));
    cxuiGridSetColumns(g, { .title = _S"Name", .flags = CXUI_COL_Stretch },
                       { .title = _S"Size", .width = cxuiLen(10), .minWidth = cxuiLen(4), .kind = CXUI_COL_Check });
    gridRepaint(g, w);
    int32 boxClips = gridRowClips(g, headH, rowH);
    cxuiGridSetColumns(g, { .title = _S"Name", .flags = CXUI_COL_Stretch },
                       { .title = _S"Size", .width = cxuiLen(15), .minWidth = cxuiLen(4) });
    gridRepaint(g, w);
    int32 textClips = gridRowClips(g, headH, rowH);
    if (boxClips < 5 || textClips < 5)
        TEST_FAIL(1, _SL("narrow columns: ${int} boxes and ${int} texts clipped"), stvar(int32, boxClips),
                  stvar(int32, textClips));
    cxuiGridSetColumns(g, { .title = _S"Name", .flags = CXUI_COL_Stretch },
                       { .title = _S"Size", .width = cxuiLen(80), .align = CXUI_ALIGN_End });

    // A larger font fits less in the same room.
    cxuiWidgetSetFont(g, &(CxUiFontDesc){ .size = 24 });
    settle(w);
    fitWant(&want, g, texts[1], 80 - 12);
    if (!gridText(g, want))
        TEST_FAIL(1, _SL("a larger font: wanted ${string}"), stvar(string, want));
    strDestroy(&want);

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// With FollowTail, rows added while the last row is shown keep it shown, in a list and a tree;
// scrolled away, or without the flag, the rows shown stay put.
UI_TEST(gridtail)
{
    CxUiListStore* store = numberedStore(100);
    CxUiGrid* g          = cxuiGridCreate(&(CxUiGridDesc){ .flags = CXUI_GRID_FollowTail });
    cxuiGridSetSource(g, store);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    int64 page = g->_page;

    addNumbered(store, 100);
    if (cxuiGridTop(g) != 0)
        TEST_FAIL(1, _SL("added at the top: top ${int}"), stvar(int64, cxuiGridTop(g)));
    cxuiGridShowRow(g, 100);
    for (int32 i = 101; i < 105; i++)
        addNumbered(store, i);
    settle(w);
    if (cxuiGridTop(g) != 105 - page || !gridText(g, _S"row 104"))
        TEST_FAIL(1, _SL("added at the tail: top ${int}, wanted ${int}"), stvar(int64, cxuiGridTop(g)),
                  stvar(int64, 105 - page));
    cxuiGridSetTop(g, 100 - page);
    addNumbered(store, 105);
    if (cxuiGridTop(g) != 100 - page)
        TEST_FAIL(1, _SL("scrolled away: top ${int}"), stvar(int64, cxuiGridTop(g)));
    cxuiGridSetTop(g, 1000);
    addNumbered(store, 106);
    if (cxuiGridTop(g) != 107 - page)
        TEST_FAIL(1, _SL("back at the tail: top ${int}"), stvar(int64, cxuiGridTop(g)));
    // Editing a cell holds it still.
    cxuiGridEdit(g, 106, 0);
    addNumbered(store, 107);
    if (cxuiGridTop(g) != 107 - page || !g->_editFrame)
        TEST_FAIL(1, _SL("editing at the tail: top ${int}"), stvar(int64, cxuiGridTop(g)));
    cxuiGridEndEdit(g, false);
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);

    // Without the flag
    g = cxuiGridCreate(NULL);
    cxuiGridSetSource(g, store);
    w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    cxuiGridSetTop(g, 1000);
    int64 top = cxuiGridTop(g);
    addNumbered(store, 107);
    if (cxuiGridTop(g) != top)
        TEST_FAIL(1, _SL("without the flag: top ${int}, was ${int}"), stvar(int64, cxuiGridTop(g)), stvar(int64, top));
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);

    // A tree: nodes added at the end, and under the last node
    CxUiTreeStore* t = cxuiTreeStoreCreate(1);
    for (int32 i = 0; i < 30; i++)
        cxuiTreeStoreAppend(t, 0, _S"n");
    g = cxuiGridCreate(&(CxUiGridDesc){ .flags = CXUI_GRID_FollowTail });
    cxuiGridSetSource(g, t);
    w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    cxuiGridSetTop(g, 1000);
    CxUiNodeId last = cxuiTreeStoreAppend(t, 0, _S"last");
    cxuiGridSetExpanded(g, last, true);
    cxuiTreeStoreAppend(t, last, _S"child");
    if (cxuiGridTop(g) != cxuiGridRowCount(g) - g->_page || cxuiGridRowCount(g) != 32)
        TEST_FAIL(1, _SL("a tree: top ${int} of ${int} rows"), stvar(int64, cxuiGridTop(g)), stvar(int64, cxuiGridRowCount(g)));
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&t);
    return 0;
}

// The wheel is passed on where a grid can't scroll that way, and a gesture stays with what took
// its first turn until a pause.
UI_TEST(gridwheel)
{
    CxUiListStore* store = numberedStore(100);
    CxUiWindow* w;
    CxUiGrid* g      = numberedGrid(store, &w);
    CxUiCanvas* body = CxUiCanvas(g->_body);
    int64 end        = cxuiGridRowCount(g) - g->_page;

    // Nothing above the first row, or to either side
    cxuiTestWheelPause();
    CxUiResult up = cxuiTestWheel(body, 0, -1, 0, 50, 50);
    cxuiTestWheelPause();
    CxUiResult side = cxuiTestWheel(body, 1, 0, 0, 50, 50);
    cxuiTestWheelPause();
    CxUiResult barUp = cxuiTestWheel(CxUiCanvas(g->_vbar), 0, -1, 0, 5, 50);
    if (up != CXUI_Default || side != CXUI_Default || barUp != CXUI_Default || cxuiGridTop(g) != 0)
        TEST_FAIL(1, _SL("at the top: up ${int}, across ${int}, up over the bar ${int}, top ${int}"), stvar(int32, up),
                  stvar(int32, side), stvar(int32, barUp), stvar(int64, cxuiGridTop(g)));

    // One that scrolls the rows keeps them once they reach the end.
    cxuiTestWheelPause();
    CxUiResult first = cxuiTestWheel(body, 0, 1, 0, 50, 50);
    cxuiGridSetTop(g, end);
    CxUiResult more = cxuiTestWheel(body, 0, 1, 0, 50, 50);
    // Another canvas isn't part of it.
    CxUiResult bar = cxuiTestWheel(CxUiCanvas(g->_vbar), 0, 1, 0, 5, 50);
    if (first != CXUI_Handled || more != CXUI_Handled || bar != CXUI_Default || cxuiGridTop(g) != end)
        TEST_FAIL(1, _SL("scrolling to the end: first ${int}, then ${int}, over the bar ${int}, top ${int}"),
                  stvar(int32, first), stvar(int32, more), stvar(int32, bar), stvar(int64, cxuiGridTop(g)));

    // One that starts at the end is passed on, even turned back or over the scroll bar.
    cxuiTestWheelPause();
    CxUiResult past  = cxuiTestWheel(body, 0, 1, 0, 50, 50);
    CxUiResult back  = cxuiTestWheel(body, 0, -1, 0, 50, 50);
    CxUiResult other = cxuiTestWheel(CxUiCanvas(g->_vbar), 0, -1, 0, 5, 50);
    if (past != CXUI_Default || back != CXUI_Default || other != CXUI_Default || cxuiGridTop(g) != end)
        TEST_FAIL(1, _SL("passed on: past the end ${int}, back ${int}, over the bar ${int}, top ${int}"),
                  stvar(int32, past), stvar(int32, back), stvar(int32, other), stvar(int64, cxuiGridTop(g)));
    // Another window isn't part of it.
    CxUiListStore* store2 = numberedStore(100);
    CxUiWindow* w2;
    CxUiGrid* g2           = numberedGrid(store2, &w2);
    CxUiResult elsewhere = cxuiTestWheel(CxUiCanvas(g2->_body), 0, 1, 0, 50, 50);
    cxuiDestroy(w2);
    objRelease(&w2);
    objRelease(&g2);
    objRelease(&store2);
    if (elsewhere != CXUI_Handled)
        TEST_FAIL(1, _SL("another window's grid didn't take the wheel"), stvNone);

    // After a pause the rows take it again.
    cxuiTestWheelPause();
    back = cxuiTestWheel(body, 0, -1, 0, 50, 50);
    if (back != CXUI_Handled || cxuiGridTop(g) != end - 3)
        TEST_FAIL(1, _SL("after a pause: ${int}, top ${int}, end ${int}"), stvar(int32, back), stvar(int64, cxuiGridTop(g)), stvar(int64, end));
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);

    // Rows that all fit pass it on.
    store = numberedStore(3);
    g     = numberedGrid(store, &w);
    cxuiTestWheelPause();
    CxUiResult fits = cxuiTestWheel(CxUiCanvas(g->_body), 0, 1, 0, 50, 50);
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    if (fits != CXUI_Default)
        TEST_FAIL(1, _SL("rows that fit took the wheel"), stvNone);
    return 0;
}

// Scrolling a grid by wheel, scroll bar and code, and keeping the rows shown in place as rows
// come and go.
UI_TEST(gridscroll)
{
    CxUiListStore* store = numberedStore(100);
    CxUiWindow* w;
    CxUiGrid* g = numberedGrid(store, &w);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
    int64 page = (200 - headH) / rowH;

    cxuiTestWheel(CxUiCanvas(g->_body), 0, 1, 0, 50, 50);
    settle(w);
    const CxUiTestDrawOp* r3 = gridText(g, _S"row 3");
    if (cxuiGridTop(g) != 3 || cxuiDScrollBarPos(g->_vbar) != 3 || !r3 || r3->a.y != headH + (rowH - textH) / 2)
        TEST_FAIL(1, _SL("a wheel notch: top ${int}, bar at ${int}"), stvar(int64, cxuiGridTop(g)),
                  stvar(int64, cxuiDScrollBarPos(g->_vbar)));
    cxuiTestWheel(CxUiCanvas(g->_vbar), 0, 2, 0, 5, 50);
    if (cxuiGridTop(g) != 9)
        TEST_FAIL(1, _SL("the wheel over the bar: top ${int}, wanted 9"), stvar(int64, cxuiGridTop(g)));

    cxuiGridSetTop(g, 1000);
    if (cxuiGridTop(g) != 100 - page || cxuiDScrollBarPos(g->_vbar) != 100 - page)
        TEST_FAIL(1, _SL("scrolled past the end: top ${int}, wanted ${int}"), stvar(int64, cxuiGridTop(g)),
                  stvar(int64, 100 - page));
    cxuiGridShowRow(g, 10);
    int64 t1 = cxuiGridTop(g);
    cxuiGridShowRow(g, 30);
    int64 t2 = cxuiGridTop(g);
    cxuiGridShowRow(g, 25);
    if (t1 != 10 || t2 != 30 - page + 1 || cxuiGridTop(g) != t2)
        TEST_FAIL(1, _SL("showing rows: tops ${int}, ${int}, ${int}"), stvar(int64, t1), stvar(int64, t2),
                  stvar(int64, cxuiGridTop(g)));
    // A change above the rows shown paints nothing.
    CxUiTestCanvasInfo info;
    settle(w);
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    int32 paints = info.paints;
    cxuiListStoreSetText(store, 0, 0, _S"changed");
    settle(w);
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    if (info.paints != paints)
        TEST_FAIL(1, _SL("a change above the rows shown painted"), stvNone);
    // One that starts above them paints from the first shown.
    cxuiGridRowsChanged(g, cxuiGridTop(g) - 1, 3);
    settle(w);
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    if (info.lastDirty.y != headH || info.lastDirty.h != 2 * rowH)
        TEST_FAIL(1, _SL("rows from above the top painted ${int} high at ${int}"), stvar(int32, info.lastDirty.h),
                  stvar(int32, info.lastDirty.y));

    // The rows shown stay put as rows are added and taken out before them.
    int64 top = cxuiGridTop(g);
    cxuiListStoreInsert(store, 0, _S"new", _S"");
    cxuiListStoreAppend(store, _S"last", _S"");
    if (cxuiGridRowCount(g) != 102 || cxuiGridTop(g) != top + 1)
        TEST_FAIL(1, _SL("after adding: ${int} rows, top ${int}"), stvar(int64, cxuiGridRowCount(g)),
                  stvar(int64, cxuiGridTop(g)));
    cxuiListStoreRemove(store, 0, 5);
    int64 t3 = cxuiGridTop(g);
    cxuiListStoreRemove(store, t3 - 2, 5);
    if (t3 != top - 4 || cxuiGridTop(g) != t3 - 2 || cxuiGridRowCount(g) != 92)
        TEST_FAIL(1, _SL("after taking out: tops ${int}, ${int}; ${int} rows"), stvar(int64, t3),
                  stvar(int64, cxuiGridTop(g)), stvar(int64, cxuiGridRowCount(g)));

    // With every row gone the bar goes, and comes back when they don't fit again.
    cxuiListStoreClear(store);
    settle(w);
    if (cxuiGridRowCount(g) != 0 || cxuiGridTop(g) != 0 || CxUiWidget(g->_vbar)->_bounds.w != 0 ||
        CxUiWidget(g->_body)->_bounds.w != 300)
        TEST_FAIL(1, _SL("cleared: ${int} rows, bar ${int} wide"), stvar(int64, cxuiGridRowCount(g)),
                  stvar(int32, CxUiWidget(g->_vbar)->_bounds.w));
    for (int32 i = 0; i < (int32)page + 1; i++)
        addNumbered(store, i);
    settle(w);
    if (CxUiWidget(g->_vbar)->_bounds.w != 12)
        TEST_FAIL(1, _SL("a page and one: bar ${int} wide"), stvar(int32, CxUiWidget(g->_vbar)->_bounds.w));

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// Fixed, fitted and hidden columns; scrolling across when they don't fit; a grid with no columns
// or no source.
UI_TEST(gridcolumns)
{
    CxUiListStore* store = cxuiListStoreCreate(3);
    for (int32 i = 0; i < 5; i++)
        cxuiListStoreAppend(store, _S"a", _S"b", _S"c");
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"A", .width = cxuiLen(150) },
                                        { .title = _S"B", .width = cxuiLen(150) },
                                        { .title = _S"C", .width = cxuiLen(150) } },
        .ncolumns = 3 });
    cxuiGridSetSource(g, store);
    // Narrower than its columns, which would widen the window
    cxuiWidgetSetNaturalSize(g, cxuiLen(100), cxuiLen(100));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);

    CHECK_AT("across bar", g->_hbar, 1.0f, 0, 188, 300, 12);
    if (CxUiWidget(g->_vbar)->_bounds.w != 0)
        TEST_FAIL(1, _SL("an up-and-down bar for five rows"), stvNone);
    const CxUiTestDrawOp* b = gridText(g, _S"B");
    int32 bx                = b ? b->a.x : -1;
    cxuiTestWheel(CxUiCanvas(g->_body), 1, 0, 0, 50, 50);
    settle(w);
    b = gridText(g, _S"B");
    if (cxuiDScrollBarPos(g->_hbar) != 30 || !b || b->a.x != bx - 30)
        TEST_FAIL(1, _SL("scrolled across to ${int}: B at ${int}, was ${int}"), stvar(int64, cxuiDScrollBarPos(g->_hbar)),
                  stvar(int32, b ? b->a.x : -1), stvar(int32, bx));

    // The wheel over the bar scrolls across too.
    cxuiTestWheel(CxUiCanvas(g->_hbar), 0, 1, 0, 50, 5);
    settle(w);
    b = gridText(g, _S"B");
    if (cxuiGridTop(g) != 0 || !b || b->a.x != bx - 60)
        TEST_FAIL(1, _SL("the wheel over the across bar: B at ${int}, top ${int}"), stvar(int32, b ? b->a.x : -1),
                  stvar(int64, cxuiGridTop(g)));

    // Hiding one makes the rest fit, so the bar goes and the scroll goes back.
    cxuiGridSetColumnHidden(g, 2, true);
    settle(w);
    if (CxUiWidget(g->_hbar)->_bounds.h != 0 || cxuiDScrollBarPos(g->_hbar) != 0 || cxuiGridColumnWidth(g, 2) != 0 ||
        gridText(g, _S"C"))
        TEST_FAIL(1, _SL("a hidden column: bar ${int} high, at ${int}"), stvar(int32, CxUiWidget(g->_hbar)->_bounds.h),
                  stvar(int64, cxuiDScrollBarPos(g->_hbar)));
    // A fitted width is the title's, but no narrower than the least
    cxuiGridSetColumnWidth(g, 0, cxuiLen(0));
    cxuiGridSetColumnWidth(g, 1, cxuiLen(0));
    cxuiGridSetColumns(g, { .title = _S"A" }, { .title = _S"A longer title" });
    settle(w);
    int32 cw = cxuiFontAdvance(cxuiWidgetFont(g->_body), _S"A longer title") + 12;
    if (cxuiGridColumnWidth(g, 0) != 24 || cxuiGridColumnWidth(g, 1) != cw || cxuiGridColumnCount(g) != 2)
        TEST_FAIL(1, _SL("fitted columns ${int} and ${int}, wanted 24 and ${int}"), stvar(int32, cxuiGridColumnWidth(g, 0)),
                  stvar(int32, cxuiGridColumnWidth(g, 1)), stvar(int32, cw));

    // No columns: one that fills the width. No source: no rows.
    cxuiGridSetColumnArray(g, NULL, 0);
    settle(w);
    if (cxuiGridColumnCount(g) != 1 || cxuiGridColumnWidth(g, 0) != 300 || !gridText(g, _S"a"))
        TEST_FAIL(1, _SL("no columns: ${int} of ${int} wide"), stvar(int32, cxuiGridColumnCount(g)),
                  stvar(int32, cxuiGridColumnWidth(g, 0)));
    cxuiGridSetSource(g, CxUiListStoreNone);
    gridRepaint(g, w);
    if (cxuiGridRowCount(g) != 0 || gridText(g, _S"a") || cxuiGridSource(g))
        TEST_FAIL(1, _SL("no source: ${int} rows"), stvar(int64, cxuiGridRowCount(g)));

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// Only the first stretching column stretches; without headers the rows start at the top; the
// across bar comes when the up-and-down bar takes room the columns needed.
UI_TEST(gridlayout)
{
    CxUiListStore* store = numberedStore(100);
    CxUiGrid* g          = cxuiGridCreate(&(CxUiGridDesc){
                 .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .flags = CXUI_COL_Stretch },
                                                 { .title = _S"Size", .width = cxuiLen(80), .flags = CXUI_COL_Stretch } },
                 .ncolumns = 2,
                 .flags    = CXUI_GRID_NoHeader });
    cxuiGridSetSource(g, store);
    cxuiWidgetSetNaturalSize(g, cxuiLen(100), cxuiLen(100));
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);

    if (cxuiGridColumnWidth(g, 0) != 208 || cxuiGridColumnWidth(g, 1) != 80)
        TEST_FAIL(1, _SL("two stretching columns ${int} and ${int} wide, wanted 208 and 80"),
                  stvar(int32, cxuiGridColumnWidth(g, 0)), stvar(int32, cxuiGridColumnWidth(g, 1)));
    const CxUiTestDrawOp* r0 = gridText(g, _S"row 0");
    if (!r0 || r0->a.y != (rowH - textH) / 2 || gridText(g, _S"Name"))
        TEST_FAIL(1, _SL("without headers: row 0 at ${int}"), stvar(int32, r0 ? r0->a.y : -1));

    // 295 fits in 300, but not in the 288 beside the up-and-down bar.
    cxuiGridSetColumns(g, { .width = cxuiLen(150) }, { .width = cxuiLen(145) });
    settle(w);
    CHECK_AT("across bar", g->_hbar, 1.0f, 0, 188, 288, 12);
    CHECK_AT("up-and-down bar", g->_vbar, 1.0f, 288, 0, 12, 188);

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// What a grid's events said
typedef struct GridLog {
    int32 changes, activates, sorts, contexts;
    int64 item;
    int32 column;
    bool descending, onItem, header;
    int32 x, y;
} GridLog;

static CxUiResult gridLogChange(stvlist* cvars, CxUiChangeEvent* ev)
{
    unused_noeval(ev);
    ((GridLog*)stvlNextPtr(cvars))->changes++;
    return CXUI_Default;
}

static CxUiResult gridLogActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    GridLog* log = stvlNextPtr(cvars);
    log->activates++;
    log->item = (int64)ev->item;
    return CXUI_Default;
}

static CxUiResult gridLogSort(stvlist* cvars, CxUiSortEvent* ev)
{
    GridLog* log = stvlNextPtr(cvars);
    log->sorts++;
    log->column     = ev->column;
    log->descending = ev->descending;
    return CXUI_Default;
}

static CxUiResult gridLogContext(stvlist* cvars, CxUiContextEvent* ev)
{
    GridLog* log = stvlNextPtr(cvars);
    log->contexts++;
    log->item   = (int64)ev->item;
    log->column = ev->column;
    log->onItem = ev->onItem;
    log->header = ev->header;
    log->x      = ev->x;
    log->y      = ev->y;
    return CXUI_Default;
}

static void gridLogAll(CxUiGrid* g, GridLog* log)
{
    cxuiOnChange(g, gridLogChange, stvar(ptr, log));
    cxuiOnActivate(g, gridLogActivate, stvar(ptr, log));
    cxuiOnSort(g, gridLogSort, stvar(ptr, log));
    cxuiOnContext(g, gridLogContext, stvar(ptr, log));
}

static void gridPtr(CxUiGrid* g, uint32 kind, uint32 button, uint32 buttons, uint32 mods, int32 x, int32 y)
{
    cxuiTestPointer(CxUiCanvas(g->_body), kind, button, buttons, mods, x, y);
}

static void gridClickAt(CxUiGrid* g, uint32 button, uint32 mods, int32 x, int32 y)
{
    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, mods, x, y);
    gridPtr(g, CXUI_TESTPTR_Press, button, button, mods, x, y);
    gridPtr(g, CXUI_TESTPTR_Release, button, 0, mods, x, y);
}

// Whether the grid's last paint filled a row-wide band at y in a color
static bool gridBand(CxUiGrid* g, int32 y, CxUiColor c)
{
    CxUiTestCanvasInfo info;
    if (!cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info))
        return false;
    for (int32 i = 0; i < info.nops; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        if (op->kind == CXUI_TESTOP_Fill && op->r.y == y && op->r.x == 0 && op->r.w > 100 && op->c.bits == c.bits)
            return true;
    }
    return false;
}

static bool gridFocused(CxUiGrid* g)
{
    CxUiTestCanvasInfo info;
    return cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info) && info.focused;
}

static uint32 gridCursor(CxUiGrid* g)
{
    CxUiTestCanvasInfo info;
    return cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info) ? info.cursor : (uint32)-1;
}

// Runs the loop for a while, in a popup owned by a window of its own
static void gridWait(int32 ms)
{
    CxUiWindow* owner = cxuiWindowCreate(NULL);
    CxUiWindow* popup = cxuiWindowCreate(NULL);
    CxUiTimer* t      = UI_AFTER(ms, timerEndPopup, stvar(object, popup), stvar(int32, 0));
    cxuiWindowPopup(popup, owner);
    objRelease(&t);
    cxuiDestroy(popup);
    cxuiDestroy(owner);
    objRelease(&popup);
    objRelease(&owner);
}

// The modifier that toggles rows in a selection: Command on the Mac
#if defined(_PLATFORM_MACOS)
#define GRID_TOGGLE CXUI_MOD_Meta
#else
#define GRID_TOGGLE CXUI_MOD_Ctrl
#endif

#define CHECK_SEL(what, g, first, count)                                                                           \
    do {                                                                                                           \
        if (cxuiGridSelected(g) != (first) || cxuiGridSelectedCount(g) != (count))                                  \
            TEST_FAIL(1, _SL(what ": ${int} selected from row ${int}, wanted ${int} from ${int}"),                  \
                      stvar(int64, cxuiGridSelectedCount(g)), stvar(int64, cxuiGridSelected(g)),                   \
                      stvar(int64, (int64)(count)), stvar(int64, (int64)(first)));                                 \
    } while (0)

// Clicks select a row, with Ctrl and Shift many; a double click opens it; a right click asks for
// a menu, selecting the row first unless it is selected; the selection and the row under the
// mouse are drawn in the palette's colors; changes from code raise no events.
UI_TEST(gridselect)
{
    CxUiListStore* store = numberedStore(100);
    CxUiWindow* w;
    CxUiGrid* g  = numberedGrid(store, &w);
    GridLog log = { 0 };
    gridLogAll(g, &log);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
#define ROW_Y(r) (headH + (r) * rowH + rowH / 2)

    CxUiPalette* own = cxuiPaletteCreate();
    cxuiPaletteSetColor(own, CXUI_PAL_SelectionText, cxuiHex(0x123456));
    cxuiWidgetSetPalette(g, own);
    objRelease(&own);

    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(2));
    CHECK_SEL("a click", g, 2, 1);
    if (cxuiGridFocusRow(g) != 2 || log.changes != 1 || !gridFocused(g))
        TEST_FAIL(1, _SL("a click: focus row ${int}, ${int} changes, focused ${bool}"), stvar(int64, cxuiGridFocusRow(g)),
                  stvar(int32, log.changes), stvar(bool, gridFocused(g)));
    gridRepaint(g, w);
    const CxUiTestDrawOp* t2 = gridText(g, _S"row 2");
    if (!gridBand(g, headH + 2 * rowH, palColor(g, CXUI_PAL_Selection)) || !t2 ||
        t2->c.bits != cxuiHex(0x123456).bits)
        TEST_FAIL(1, _SL("the selected row isn't drawn in the selection's colors"), stvNone);

    // Where one row is selected, Ctrl doesn't add to it.
    gridClickAt(g, CXUI_BTN_Left, GRID_TOGGLE, 50, ROW_Y(4));
    CHECK_SEL("a Ctrl+click selecting one", g, 4, 1);

    cxuiGridSetSelectMode(g, CXUI_SELECT_Multi);
    gridClickAt(g, CXUI_BTN_Left, CXUI_MOD_Shift, 50, ROW_Y(6));
    CHECK_SEL("a Shift+click", g, 4, 3);
    gridClickAt(g, CXUI_BTN_Left, GRID_TOGGLE, 50, ROW_Y(5));
    CHECK_SEL("a Ctrl+click", g, 4, 2);
    if (cxuiGridIsSelected(g, 5) || cxuiGridNextSelected(g, -1) != 4 || cxuiGridNextSelected(g, 4) != 6 ||
        cxuiGridNextSelected(g, 6) != -1)
        TEST_FAIL(1, _SL("a Ctrl+click didn't take row 5 out"), stvNone);
    // From the row Ctrl last clicked: Shift alone replaces the rest, with Ctrl it adds to them.
    gridClickAt(g, CXUI_BTN_Left, CXUI_MOD_Shift, 50, ROW_Y(7));
    CHECK_SEL("a Shift+click after a Ctrl+click", g, 5, 3);
    gridClickAt(g, CXUI_BTN_Left, GRID_TOGGLE | CXUI_MOD_Shift, 50, ROW_Y(1));
    CHECK_SEL("a Ctrl+Shift+click", g, 1, 7);
    if (saSize(g->_sel) != 2 || log.changes != 6)
        TEST_FAIL(1, _SL("${int} ranges kept, ${int} changes"), stvar(int32, saSize(g->_sel) / 2),
                  stvar(int32, log.changes));

    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(3));
    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(3));
    CHECK_SEL("a double click", g, 3, 1);
    if (log.activates != 1 || log.item != 3)
        TEST_FAIL(1, _SL("a double click: ${int} activations, of row ${int}"), stvar(int32, log.activates),
                  stvar(int64, log.item));

    gridClickAt(g, CXUI_BTN_Right, 0, 220, ROW_Y(7));
    CHECK_SEL("a right click", g, 7, 1);
    if (log.contexts != 1 || log.item != 7 || !log.onItem || log.header || log.column != 1 || log.x != 220 ||
        log.y != ROW_Y(7))
        TEST_FAIL(1, _SL("a right click: ${int} menus, row ${int} (on it ${bool}), column ${int}, at ${int},${int}"),
                  stvar(int32, log.contexts), stvar(int64, log.item), stvar(bool, log.onItem), stvar(int32, log.column),
                  stvar(int32, log.x), stvar(int32, log.y));
    cxuiGridSetSelected(g, 7, 3, true);
    int32 changes = log.changes;
    gridClickAt(g, CXUI_BTN_Right, 0, 50, ROW_Y(8));
    CHECK_SEL("a right click on a selected row", g, 7, 3);
    if (log.contexts != 2 || log.item != 8 || cxuiGridFocusRow(g) != 8 || log.changes != changes)
        TEST_FAIL(1, _SL("a right click on a selected row: ${int} menus, row ${int}, ${int} changes"),
                  stvar(int32, log.contexts), stvar(int64, log.item), stvar(int32, log.changes - changes));
    gridClickAt(g, CXUI_BTN_Right, 0, 50, headH / 2);
    if (log.contexts != 3 || log.onItem || !log.header || log.column != 0)
        TEST_FAIL(1, _SL("a right click on a header: on a row ${bool}, header ${bool}, column ${int}"),
                  stvar(bool, log.onItem), stvar(bool, log.header), stvar(int32, log.column));
#if defined(_PLATFORM_MACOS)
    // A Control-click is a right click on the Mac.
    gridClickAt(g, CXUI_BTN_Left, CXUI_MOD_Ctrl, 50, ROW_Y(2));
    CHECK_SEL("a Control-click", g, 2, 1);
    if (log.contexts != 4 || log.item != 2 || !log.onItem)
        TEST_FAIL(1, _SL("a Control-click: ${int} menus, row ${int}"), stvar(int32, log.contexts), stvar(int64, log.item));
    changes = log.changes;
#endif

    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, 0, 50, ROW_Y(5));
    gridRepaint(g, w);
    if (!gridBand(g, headH + 5 * rowH, palColor(g, CXUI_PAL_Hover)))
        TEST_FAIL(1, _SL("the row under the mouse isn't drawn hot"), stvNone);
    gridPtr(g, CXUI_TESTPTR_Left, 0, 0, 0, 50, ROW_Y(5));
    gridRepaint(g, w);
    if (gridBand(g, headH + 5 * rowH, palColor(g, CXUI_PAL_Hover)))
        TEST_FAIL(1, _SL("the row stays hot after the mouse leaves"), stvNone);

    cxuiGridClearSelection(g);
    cxuiGridSelectAll(g);
    CHECK_SEL("all selected from code", g, 0, 100);
    cxuiGridSetSelected(g, 10, 80, false);
    CHECK_SEL("rows unselected from code", g, 0, 20);
    cxuiGridSetSelected(g, 10, 1, true);
    if (saSize(g->_sel) != 4)
        TEST_FAIL(1, _SL("a row selected next to others kept ${int} ranges"), stvar(int32, saSize(g->_sel) / 2));
    if (log.changes != changes)
        TEST_FAIL(1, _SL("code raised ${int} changes"), stvar(int32, log.changes - changes));
#undef ROW_Y

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// Dragging over the rows selects them, scrolling while the pointer is outside; the selection and
// the focus row stay on the same rows as others come and go; a click below the rows unselects
// them all.
UI_TEST(griddrag)
{
    CxUiListStore* store = numberedStore(100);
    CxUiWindow* w;
    CxUiGrid* g  = numberedGrid(store, &w);
    GridLog log = { 0 };
    gridLogAll(g, &log);
    cxuiGridSetSelectMode(g, CXUI_SELECT_Multi);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
#define ROW_Y(r) (headH + (r) * rowH + rowH / 2)
    const uint32 L = CXUI_BTN_Left;

    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, 0, 50, ROW_Y(1));
    gridPtr(g, CXUI_TESTPTR_Press, L, L, 0, 50, ROW_Y(1));
    gridPtr(g, CXUI_TESTPTR_Move, 0, L, 0, 50, ROW_Y(4));
    CHECK_SEL("dragging down", g, 1, 4);
    gridPtr(g, CXUI_TESTPTR_Move, 0, L, 0, 50, 250);
    gridWait(200);
    int64 top = cxuiGridTop(g), focus = cxuiGridFocusRow(g);
    // The pointer is below the partly shown last row, which the selection reaches.
    if (top < 3 || focus != top + (200 - headH) / rowH)
        TEST_FAIL(1, _SL("held below: top ${int}, focus row ${int}"), stvar(int64, top), stvar(int64, focus));
    CHECK_SEL("held below", g, 1, focus);
    gridPtr(g, CXUI_TESTPTR_Release, L, 0, 0, 50, 250);
    if (!g->_autoscroll || cxuiTimerIsRunning(g->_autoscroll))
        TEST_FAIL(1, _SL("the scrolling timer runs on after the release"), stvNone);
    top = cxuiGridTop(g);
    gridWait(80);
    if (cxuiGridTop(g) != top)
        TEST_FAIL(1, _SL("still scrolling after the release: top ${int}, was ${int}"), stvar(int64, cxuiGridTop(g)),
                  stvar(int64, top));

    cxuiGridSetTop(g, 0);
    cxuiGridClearSelection(g);
    cxuiGridSetSelected(g, 2, 3, true);
    cxuiGridSetFocusRow(g, 10);
    int32 changes = log.changes;
    cxuiListStoreInsert(store, 3, _S"new", _S"0 KB");
    CHECK_SEL("a row inserted into the selection", g, 2, 3);
    if (cxuiGridIsSelected(g, 3) || !cxuiGridIsSelected(g, 5) || cxuiGridFocusRow(g) != 11)
        TEST_FAIL(1, _SL("a row inserted: the new row selected ${bool}, focus row ${int}"), stvar(bool, cxuiGridIsSelected(g, 3)),
                  stvar(int64, cxuiGridFocusRow(g)));
    cxuiListStoreRemove(store, 3, 1);
    CHECK_SEL("the row removed again", g, 2, 3);
    if (saSize(g->_sel) != 2 || cxuiGridFocusRow(g) != 10)
        TEST_FAIL(1, _SL("the row removed again: ${int} ranges, focus row ${int}"), stvar(int32, saSize(g->_sel) / 2),
                  stvar(int64, cxuiGridFocusRow(g)));
    cxuiListStoreRemove(store, 0, 3);
    CHECK_SEL("rows removed from the selection", g, 0, 2);
    cxuiListStoreRemove(store, 7, 1);
    if (cxuiGridFocusRow(g) != 7)
        TEST_FAIL(1, _SL("the focus row removed: focus row ${int}, wanted 7"), stvar(int64, cxuiGridFocusRow(g)));
    if (log.changes != changes)
        TEST_FAIL(1, _SL("the source's changes raised ${int} change events"), stvar(int32, log.changes - changes));
    cxuiGridReset(g);
    CHECK_SEL("a reset", g, -1, 0);

    cxuiListStoreRemove(store, 3, cxuiListStoreRowCount(store));
    cxuiGridSetSelected(g, 0, 2, true);
    gridClickAt(g, L, 0, 50, ROW_Y(6));
    CHECK_SEL("a click below the rows", g, -1, 0);
    if (log.changes != changes + 1)
        TEST_FAIL(1, _SL("a click below the rows raised ${int} changes"), stvar(int32, log.changes - changes));
#undef ROW_Y

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

static CxUiResult eatDown(stvlist* cvars, CxUiKeyEvent* ev)
{
    unused_noeval(cvars);
    return ev->key == CXUI_KEY_Down ? CXUI_Handled : CXUI_Default;
}

// The arrow, page, Home and End keys move the focus row, selecting as the modifiers say; Enter
// opens it, the Menu key and Shift+F10 ask about it; typing finds a row; keys the grid doesn't
// use go on to its parent, and the program's handler sees keys first.
UI_TEST(gridkeys)
{
    CxUiListStore* store = numberedStore(100);
    static const strref fruit[] = { _S"apple", _S"banana", _S"blueberry", _S"cherry", _S"avocado" };
    for (int32 i = 0; i < 5; i++)
        cxuiListStoreSetText(store, i, 0, fruit[i]);
    CxUiWindow* w;
    CxUiGrid* g  = numberedGrid(store, &w);
    GridLog log = { 0 };
    gridLogAll(g, &log);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
    int64 page    = (200 - headH) / rowH;
    CxUiGridBody* b = g->_body;

    cxuiFocus(g);
    if (!gridFocused(g))
        TEST_FAIL(1, _SL("focusing the grid didn't focus its rows"), stvNone);

    struct {
        CxUiKey key;
        uint32 mods;
        int64 focus, top;
    } moves[] = {
        { CXUI_KEY_Down, 0, 0, 0 },
        { CXUI_KEY_Down, 0, 1, 0 },
        { CXUI_KEY_Down, 0, 2, 0 },
        { CXUI_KEY_PageDown, 0, page - 1, 0 },
        { CXUI_KEY_PageDown, 0, 2 * page - 2, page - 1 },
        { CXUI_KEY_Up, 0, 2 * page - 3, page - 1 },
        { CXUI_KEY_PageUp, 0, page - 1, page - 1 },
        { CXUI_KEY_PageUp, 0, 0, 0 },
        { CXUI_KEY_End, 0, 99, 100 - page },
        { CXUI_KEY_Home, 0, 0, 0 },
        { CXUI_KEY_PageDown, 0, page - 1, 0 },
        { CXUI_KEY_Up, CXUI_MOD_Shift, page - 2, 0 },
    };
    int32 nmoves = (int32)(sizeof(moves) / sizeof(moves[0]));
    for (int32 i = 0; i < nmoves; i++) {
        pressKey(b, moves[i].key, moves[i].mods);
        if (cxuiGridFocusRow(g) != moves[i].focus || cxuiGridTop(g) != moves[i].top ||
            cxuiGridSelected(g) != moves[i].focus || cxuiGridSelectedCount(g) != 1)
            TEST_FAIL(1, _SL("key ${int}: focus row ${int}, top ${int}, row ${int} selected; wanted ${int} and ${int}"),
                      stvar(int32, i), stvar(int64, cxuiGridFocusRow(g)), stvar(int64, cxuiGridTop(g)),
                      stvar(int64, cxuiGridSelected(g)), stvar(int64, moves[i].focus), stvar(int64, moves[i].top));
    }
    if (log.changes != nmoves)
        TEST_FAIL(1, _SL("${int} changes from moving, wanted ${int}"), stvar(int32, log.changes), stvar(int32, nmoves));

    int64 at = page - 2;
    cxuiGridSetSelectMode(g, CXUI_SELECT_Multi);
    pressKey(b, CXUI_KEY_Up, CXUI_MOD_Shift);
    CHECK_SEL("Shift+Up", g, at - 1, 2);
#if defined(_PLATFORM_MACOS)
    // Command-Down opens the row on the Mac, which has no keys moving only the focus.
    cxuiGridSetFocusRow(g, at + 1);
#else
    pressKey(b, CXUI_KEY_Down, GRID_TOGGLE);
    pressKey(b, CXUI_KEY_Down, GRID_TOGGLE);
#endif
    CHECK_SEL("Ctrl+Down", g, at - 1, 2);
    if (cxuiGridFocusRow(g) != at + 1)
        TEST_FAIL(1, _SL("Ctrl+Down: focus row ${int}"), stvar(int64, cxuiGridFocusRow(g)));
    pressKey(b, CXUI_KEY_Space, GRID_TOGGLE);
    CHECK_SEL("Ctrl+Space", g, at - 1, 3);
    pressKey(b, CXUI_KEY_Space, GRID_TOGGLE);
    pressKey(b, CXUI_KEY_Space, 0);
    CHECK_SEL("Ctrl+Space again, then Space", g, at - 1, 3);
    pressKey(b, CXUI_KEY_Space, GRID_TOGGLE);
    CHECK_SEL("Ctrl+Space a third time", g, at - 1, 2);
    pressKey(b, CXUI_KEY_A, GRID_TOGGLE);
    CHECK_SEL("Ctrl+A", g, 0, 100);

    pressKey(b, CXUI_KEY_Enter, 0);
    if (log.activates != 1 || log.item != at + 1)
        TEST_FAIL(1, _SL("Enter: ${int} activations, of row ${int}"), stvar(int32, log.activates), stvar(int64, log.item));
#if defined(_PLATFORM_MACOS)
    pressKey(b, CXUI_KEY_Down, CXUI_MOD_Meta);
    if (log.activates != 2 || log.item != at + 1 || cxuiGridFocusRow(g) != at + 1)
        TEST_FAIL(1, _SL("Command-Down: ${int} activations, focus row ${int}"), stvar(int32, log.activates),
                  stvar(int64, cxuiGridFocusRow(g)));
#endif
    pressKey(b, CXUI_KEY_ContextMenu, 0);
    pressKey(b, CXUI_KEY_F10, CXUI_MOD_Shift);
    int32 below = headH + (int32)(at + 2 - cxuiGridTop(g)) * rowH;
    if (log.contexts != 2 || log.item != at + 1 || !log.onItem || log.y != below)
        TEST_FAIL(1, _SL("menu keys: ${int} menus, for row ${int} at y ${int}"), stvar(int32, log.contexts),
                  stvar(int64, log.item), stvar(int32, log.y));

    // Typing: the same letter again goes on to the next row starting with it, wrapping around;
    // after a pause, typing starts again; a longer prefix may match the focus row.
    pressKey(b, CXUI_KEY_Home, 0);
    struct {
        CxUiKey key;
        bool fresh;
        int64 row;
    } typed[] = {
        { CXUI_KEY_B, false, 1 }, { CXUI_KEY_B, false, 2 }, { CXUI_KEY_B, false, 1 }, { CXUI_KEY_C, true, 3 },
        { CXUI_KEY_B, true, 1 },  { CXUI_KEY_L, false, 2 }, { CXUI_KEY_A, true, 4 },
    };
    for (int32 i = 0; i < (int32)(sizeof(typed) / sizeof(typed[0])); i++) {
        if (typed[i].fresh)
            g->_typedAt = 0;
        pressKey(b, typed[i].key, 0);
        if (cxuiGridFocusRow(g) != typed[i].row || cxuiGridSelected(g) != typed[i].row)
            TEST_FAIL(1, _SL("typed key ${int}: focus row ${int}, wanted ${int}"), stvar(int32, i),
                      stvar(int64, cxuiGridFocusRow(g)), stvar(int64, typed[i].row));
    }

    // Nothing to scroll across, so Right goes on.
    CxUiResult r1 = cxuiTestKey(CxUiWidget(b), CXUI_KEY_Right, 0, true);
    CxUiResult r2 = cxuiTestKey(CxUiWidget(b), CXUI_KEY_F5, 0, true);
    if (r1 != CXUI_Default || r2 != CXUI_Default)
        TEST_FAIL(1, _SL("keys the grid doesn't use were handled: Right ${int}, F5 ${int}"), stvar(int32, r1),
                  stvar(int32, r2));
    cxuiTestKey(CxUiWidget(b), CXUI_KEY_Right, 0, false);
    cxuiTestKey(CxUiWidget(b), CXUI_KEY_F5, 0, false);

    cxuiOnKey(g, eatDown, stvNone);
    pressKey(b, CXUI_KEY_Down, 0);
    if (cxuiGridFocusRow(g) != 4)
        TEST_FAIL(1, _SL("the program's key handler didn't come first: focus row ${int}"), stvar(int64, cxuiGridFocusRow(g)));

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// A click on a sortable header shows its arrow and asks for a sort, the second time largest
// first; dragging a header's edge resizes its column, double-clicking it fits the column; a list
// store sorts digits as numbers.
UI_TEST(gridheader)
{
    CxUiListStore* store = numberedStore(20);
    CxUiGrid* g          = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"A", .width = cxuiLen(100), .flags = CXUI_COL_Sortable },
                                        { .title = _S"B", .width = cxuiLen(60),
                                          .flags = CXUI_COL_Sortable | CXUI_COL_FixedWidth },
                                        { .title = _S"C", .width = cxuiLen(60) } },
        .ncolumns = 3 });
    cxuiGridSetSource(g, store);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    GridLog log = { 0 };
    gridLogAll(g, &log);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
    const uint32 L = CXUI_BTN_Left;
    int32 hy       = headH / 2;

    gridClickAt(g, L, 0, 50, hy);
    if (log.sorts != 1 || log.column != 0 || log.descending || cxuiGridSortColumn(g) != 0 || cxuiGridSortDescending(g))
        TEST_FAIL(1, _SL("a header click: ${int} sorts, column ${int}, descending ${bool}"), stvar(int32, log.sorts),
                  stvar(int32, log.column), stvar(bool, log.descending));
    gridRepaint(g, w);
    // The arrow's widest line, at its bottom: 8 wide, 6 in from the column's right edge
    const CxUiTestDrawOp* arrow = NULL;
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    for (int32 i = 0; i < info.nops; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        if (op->kind == CXUI_TESTOP_Fill && op->r.h == 1 && op->r.w == 8 && op->r.x == 86 &&
            op->c.bits == palColor(g, CXUI_PAL_GridHeaderText).bits)
            arrow = op;
    }
    if (!arrow || arrow->r.y != (headH - 4) / 2 + 3)
        TEST_FAIL(1, _SL("no upward sort arrow: its base at ${int}"), stvar(int32, arrow ? arrow->r.y : -1));
    gridClickAt(g, L, 0, 50, hy);
    if (log.sorts != 2 || !log.descending || !cxuiGridSortDescending(g))
        TEST_FAIL(1, _SL("a second click: ${int} sorts, descending ${bool}"), stvar(int32, log.sorts), stvar(bool, log.descending));

    // Let go over another column, or a click on one that doesn't sort: no sort
    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, 0, 40, hy);
    gridPtr(g, CXUI_TESTPTR_Press, L, L, 0, 40, hy);
    gridPtr(g, CXUI_TESTPTR_Move, 0, L, 0, 130, hy);
    gridPtr(g, CXUI_TESTPTR_Release, L, 0, 0, 130, hy);
    gridClickAt(g, L, 0, 190, hy);
    if (log.sorts != 2 || cxuiGridSortColumn(g) != 0)
        TEST_FAIL(1, _SL("${int} sorts where none was wanted, column ${int}"), stvar(int32, log.sorts - 2),
                  stvar(int32, cxuiGridSortColumn(g)));

    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, 0, 98, hy);
    uint32 onEdge = gridCursor(g);
    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, 0, 160, hy);
    uint32 onFixed = gridCursor(g);
    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, 0, 101, headH + rowH + rowH / 2);
    if (onEdge != CXUI_CURSOR_SizeWE || onFixed != CXUI_CURSOR_Arrow || gridCursor(g) != CXUI_CURSOR_Arrow)
        TEST_FAIL(1, _SL("cursors: ${int} on an edge, ${int} on a fixed column's, ${int} over a row"), stvar(uint32, onEdge),
                  stvar(uint32, onFixed), stvar(uint32, gridCursor(g)));

    gridPtr(g, CXUI_TESTPTR_Move, 0, 0, 0, 101, hy);
    gridPtr(g, CXUI_TESTPTR_Press, L, L, 0, 101, hy);
    gridPtr(g, CXUI_TESTPTR_Move, 0, L, 0, 141, hy);
    gridPtr(g, CXUI_TESTPTR_Release, L, 0, 0, 141, hy);
    settle(w);
    if (cxuiGridColumnWidth(g, 0) != 140 || log.sorts != 2)
        TEST_FAIL(1, _SL("dragged 40 wider: ${int}, wanted 140"), stvar(int32, cxuiGridColumnWidth(g, 0)));
    gridPtr(g, CXUI_TESTPTR_Press, L, L, 0, 140, hy);
    gridPtr(g, CXUI_TESTPTR_Move, 0, L, 0, -100, hy);
    gridPtr(g, CXUI_TESTPTR_Release, L, 0, 0, -100, hy);
    settle(w);
    if (cxuiGridColumnWidth(g, 0) != 24)
        TEST_FAIL(1, _SL("dragged far to the left: ${int}, wanted the least, 24"), stvar(int32, cxuiGridColumnWidth(g, 0)));

    // Fits "row 8" (5 cells of 8) and the padding either side, wider than the title and arrow
    gridClickAt(g, L, 0, 23, hy);
    gridClickAt(g, L, 0, 23, hy);
    settle(w);
    if (cxuiGridColumnWidth(g, 0) != 52)
        TEST_FAIL(1, _SL("a double click on the edge fit the column to ${int}, wanted 52"), stvar(int32, cxuiGridColumnWidth(g, 0)));

    cxuiGridSetSelected(g, 1, 1, true);
    cxuiListStoreSort(store, 0, false);
    if (!strEq(cxuiListStoreText(store, 2, 0), _S"row 2") || !strEq(cxuiListStoreText(store, 19, 1), _S"19 KB") ||
        cxuiGridSelectedCount(g) != 0)
        TEST_FAIL(1, _SL("sorted: row 2 is \"${string}\", the last size \"${string}\", ${int} selected"),
                  stvar(strref, cxuiListStoreText(store, 2, 0)), stvar(strref, cxuiListStoreText(store, 19, 1)),
                  stvar(int64, cxuiGridSelectedCount(g)));
    cxuiListStoreSort(store, 1, true);
    if (!strEq(cxuiListStoreText(store, 0, 0), _S"row 19") || !strEq(cxuiListStoreText(store, 19, 0), _S"row 0"))
        TEST_FAIL(1, _SL("sorted largest first: \"${string}\" to \"${string}\""), stvar(strref, cxuiListStoreText(store, 0, 0)),
                  stvar(strref, cxuiListStoreText(store, 19, 0)));

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

static int test_cxui_grp_grid(void)
{
    TEST_CHAIN(test_cxui_gridpaint, test_cxui_gridrowheight, test_cxui_gridfit, test_cxui_gridtail, test_cxui_gridscroll, test_cxui_gridwheel, test_cxui_gridcolumns, test_cxui_gridlayout,
               test_cxui_gridselect, test_cxui_griddrag, test_cxui_gridkeys, test_cxui_gridheader);
}

// ---- trees ----

// A tree store's ids name the same node while it lives and nothing afterwards, even once its
// place is reused; nodes go where they are put among their siblings; removing one takes its
// subtree; text and data stay with their node.
UI_TEST(treestore)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(2);
    CxUiNodeId a     = cxuiTreeStoreAppend(t, 0, _S"a");
    CxUiNodeId c     = cxuiTreeStoreAppend(t, 0, _S"c");
    CxUiNodeId b     = cxuiTreeStoreInsert(t, 0, a, _S"b", _S"2");
    CxUiNodeId z     = cxuiTreeStoreInsert(t, 0, 0, _S"z");
    if (!a || !b || !c || !z || a == b || cxuiTreeStoreFirstChild(t, 0) != z || cxuiTreeStoreNextSibling(t, z) != a ||
        cxuiTreeStoreNextSibling(t, a) != b || cxuiTreeStoreNextSibling(t, b) != c || cxuiTreeStoreNextSibling(t, c) != 0)
        TEST_FAIL(1, _SL("the top level isn't z, a, b, c"), stvNone);
    if (cxuiTreeStoreInsert(t, a, b, _S"x") != 0 || cxuiTreeStoreAppend(t, 12345, _S"x") != 0)
        TEST_FAIL(1, _SL("a node was added after a node not its parent's, or under no node"), stvNone);

    CxUiNodeId a1  = cxuiTreeStoreAppend(t, a, _S"a1");
    CxUiNodeId a2  = cxuiTreeStoreAppend(t, a, _S"a2");
    CxUiNodeId a21 = cxuiTreeStoreAppend(t, a2, _S"a21");
    if (cxuiTreeStoreParentOf(t, a21) != a2 || cxuiTreeStoreParentOf(t, a1) != a || cxuiTreeStoreParentOf(t, a) != 0 ||
        cxuiTreeStoreChildCount(t, a) != 2 || cxuiTreeStoreChildCount(t, 0) != 4 || !cxuiTreeStoreHasChildren(t, a2) ||
        cxuiTreeStoreHasChildren(t, a1) || cxuiTreeStoreNodeCount(t) != 7)
        TEST_FAIL(1, _SL("parents and counts are wrong: ${int} nodes"), stvar(int64, cxuiTreeStoreNodeCount(t)));
    if (!strEq(cxuiTreeStoreText(t, b, 1), _S"2") || cxuiTreeStoreText(t, b, 2) != NULL)
        TEST_FAIL(1, _SL("cell text"), stvNone);

    string kept = 0;
    strDup(&kept, _S"kept");
    cxuiTreeStoreSetData(t, a21, stvar(string, kept));
    strDestroy(&kept);
    stvar got = cxuiTreeStoreData(t, a21);
    if (!stvarIs(&got, string) || !strEq(stvarString(&got), _S"kept"))
        TEST_FAIL(1, _SL("a node's data didn't keep its string"), stvNone);

    if (!cxuiTreeStoreRemove(t, a) || cxuiTreeStoreNodeCount(t) != 3 || cxuiTreeStoreIsValid(t, a) ||
        cxuiTreeStoreIsValid(t, a21) || cxuiTreeStoreText(t, a1, 0) != NULL || cxuiTreeStoreNextSibling(t, z) != b)
        TEST_FAIL(1, _SL("removing a node and its children: ${int} left"), stvar(int64, cxuiTreeStoreNodeCount(t)));
    CxUiNodeId again = cxuiTreeStoreAppend(t, 0, _S"again");
    if (again == a || again == a1 || again == a2 || again == a21 || cxuiTreeStoreIsValid(t, a))
        TEST_FAIL(1, _SL("a reused place took an old id"), stvNone);
    if (cxuiTreeStoreRemove(t, a))
        TEST_FAIL(1, _SL("an old id was removed twice"), stvNone);

    cxuiTreeStoreClear(t);
    if (cxuiTreeStoreNodeCount(t) != 0 || cxuiTreeStoreFirstChild(t, 0) != 0 || cxuiTreeStoreIsValid(t, b))
        TEST_FAIL(1, _SL("clearing left nodes"), stvNone);
    objRelease(&t);
    return 0;
}

// A tree grid of ncols with a Name column, 300x200 in a window
static CxUiGrid* treeGrid(CxUiTreeStore* store, flags_t flags, CxUiWindow** win)
{
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .flags = CXUI_COL_Stretch, .kind = CXUI_COL_Tree },
                                        { .title = _S"Size", .width = cxuiLen(60) } },
        .ncolumns = 2,
        .flags    = flags,
        .select   = CXUI_SELECT_Multi });
    cxuiGridSetSource(g, store);
    *win = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(*win);
    return g;
}

// The nodes shown, top to bottom, as the grid says which are open, with their depths
static void treeShown(CxUiTreeStore* t, CxUiGrid* g, CxUiNodeId parent, int32 depth, sa_uint64* out, sa_int32* depths)
{
    for (CxUiNodeId n = cxuiTreeStoreFirstChild(t, parent); n; n = cxuiTreeStoreNextSibling(t, n)) {
        saPush(out, uint64, n);
        saPush(depths, int32, depth);
        if (cxuiGridIsExpanded(g, n))
            treeShown(t, g, n, depth + 1, out, depths);
    }
}

static uint32 treeRand(uint32* seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 16) & 0x7FFF;
}

static CxUiNodeId treeAdd(CxUiTreeStore* t, CxUiNodeId parent, CxUiNodeId after, bool insert, int32* counter)
{
    string name = 0;
    strFormat(&name, _S"n${int}", stvar(int32, (*counter)++));
    CxUiNodeId n = insert ? cxuiTreeStoreInsert(t, parent, after, name) : cxuiTreeStoreAppend(t, parent, name);
    strDestroy(&name);
    return n;
}

// A random node of the store, or 0 if it is empty
static CxUiNodeId treePick(CxUiTreeStore* t, uint32* seed)
{
    if (cxuiTreeStoreNodeCount(t) == 0)
        return 0;
    for (;;) {
        int32 i = (int32)(treeRand(seed) % (uint32)t->_nslots);
        if (t->_slots[i].used)
            return ((uint64)t->_slots[i].gen << 32) | (uint32)(i + 1);
    }
}

// Against nodes walked from the store, after random opening, closing, adding and removing: every
// row shows the right node at the right depth, every node shown knows its row, the rows painted
// are those nodes indented by depth, and the selected nodes are those selected before that are
// still shown.
UI_TEST(treemap)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(1);
    uint32 seed      = 12345;
    int32 counter    = 0;
    sa_uint64 all;
    saInit(&all, uint64, 64);
    for (int32 i = 0; i < 120; i++) {
        CxUiNodeId parent = saSize(all) && treeRand(&seed) % 4 ? all.a[treeRand(&seed) % saSize(all)] : 0;
        saPush(&all, uint64, treeAdd(t, parent, 0, false, &counter));
    }
    saDestroy(&all);
    CxUiWindow* w;
    CxUiGrid* g = treeGrid(t, 0, &w);

    sa_uint64 shown, sel;
    sa_int32 depths;
    saInit(&shown, uint64, 64);
    saInit(&sel, uint64, 16);
    saInit(&depths, int32, 64);
    for (int32 op = 0; op < 400; op++) {
        uint32 what     = treeRand(&seed) % 100;
        CxUiNodeId node = treePick(t, &seed);
        if (what < 40) {
            cxuiGridSetExpanded(g, node, true);
        } else if (what < 65) {
            cxuiGridSetExpanded(g, node, false);
        } else if (what < 70) {
            cxuiGridExpandAll(g, what & 1 ? node : 0);
        } else if (what < 73) {
            cxuiGridCollapseAll(g, node);
        } else if (what < 85) {
            CxUiNodeId added = treeAdd(t, treeRand(&seed) % 3 ? node : 0, 0, false, &counter);
            if (treeRand(&seed) % 2)
                treeAdd(t, cxuiTreeStoreParentOf(t, added), 0, true, &counter);
        } else if (what < 93) {
            if (node && cxuiTreeStoreNodeCount(t) > 20)
                cxuiTreeStoreRemove(t, node);
        } else if (what < 96) {
            cxuiGridReset(g);
        } else {
            if (node && cxuiGridRowOf(g, node) >= 0)
                cxuiGridSetNodeSelected(g, node, true);
        }

        saClear(&shown);
        saClear(&depths);
        treeShown(t, g, 0, 0, &shown, &depths);
        if (cxuiGridRowCount(g) != saSize(shown))
            TEST_FAIL(1, _SL("op ${int} (${int}): ${int} rows, wanted ${int}"), stvar(int32, op), stvar(uint32, what),
                      stvar(int64, cxuiGridRowCount(g)), stvar(int32, saSize(shown)));
        for (int32 r = 0; r < saSize(shown); r++) {
            int32 depth = cxuiGridDepthOf(g, shown.a[r]);
            if (cxuiGridNodeAt(g, r) != shown.a[r] || cxuiGridRowOf(g, shown.a[r]) != r || depth != depths.a[r])
                TEST_FAIL(1, _SL("op ${int} (${int}), row ${int}: node ${uint}, its row ${int} and depth ${int}, wanted ${int}"),
                          stvar(int32, op), stvar(uint32, what), stvar(int32, r), stvar(uint64, cxuiGridNodeAt(g, r)),
                          stvar(int64, cxuiGridRowOf(g, shown.a[r])), stvar(int32, depth), stvar(int32, depths.a[r]));
        }

        // The rows painted, from somewhere along the tree
        if (op % 10 == 0 && saSize(shown) > 0) {
            int32 rowH, headH, textH;
            gridHeights(g, &rowH, &headH, &textH);
            cxuiGridSetTop(g, treeRand(&seed) % saSize(shown));
            gridRepaint(g, w);
            int64 top = cxuiGridTop(g);
            for (int64 r = top; r < min(top + (200 - headH) / rowH, saSize(shown)); r++) {
                const CxUiTestDrawOp* op_ = gridText(g, cxuiTreeStoreText(t, shown.a[r], 0));
                int32 x = 19 + 16 * depths.a[r], y = headH + (int32)(r - top) * rowH + (rowH - textH) / 2;
                if (!op_ || op_->a.x != x || op_->a.y != y)
                    TEST_FAIL(1, _SL("op ${int}: row ${int} painted at ${int},${int}, wanted ${int},${int}"), stvar(int32, op),
                              stvar(int64, r), stvar(int32, op_ ? op_->a.x : -1), stvar(int32, op_ ? op_->a.y : -1),
                              stvar(int32, x), stvar(int32, y));
            }
        }

        // What was selected and is still shown stays selected; nothing else is. A reset unselects.
        if (what >= 93 && what < 96)
            saClear(&sel);
        for (int32 i = saSize(sel) - 1; i >= 0; i--) {
            if (cxuiGridRowOf(g, sel.a[i]) < 0)
                saRemove(&sel, i);
        }
        if (what >= 96 && node && cxuiGridIsNodeSelected(g, node) && saFind(sel, uint64, node) < 0)
            saPush(&sel, uint64, node);
        int64 nsel = 0;
        for (int64 r = cxuiGridNextSelected(g, -1); r >= 0; r = cxuiGridNextSelected(g, r), nsel++) {
            if (saFind(sel, uint64, cxuiGridNodeAt(g, r)) < 0)
                TEST_FAIL(1, _SL("op ${int} (${int}): row ${int} is selected, but its node wasn't"), stvar(int32, op),
                          stvar(uint32, what), stvar(int64, r));
        }
        if (nsel != saSize(sel))
            TEST_FAIL(1, _SL("op ${int} (${int}): ${int} selected, wanted ${int}"), stvar(int32, op), stvar(uint32, what),
                      stvar(int64, nsel), stvar(int32, saSize(sel)));
    }
    saDestroy(&shown);
    saDestroy(&sel);
    saDestroy(&depths);

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&t);
    return 0;
}

// The fills the grid's last paint made in a color, from x
static const CxUiTestDrawOp* treeFill(CxUiGrid* g, int32 x, int32 w, int32 h, CxUiColor c, int32 n)
{
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    for (int32 i = 0; i < info.nops; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        if (op->kind == CXUI_TESTOP_Fill && op->r.x == x && op->r.w == w && op->r.h == h && op->c.bits == c.bits && n-- == 0)
            return op;
    }
    return NULL;
}

// Whether the grid's last paint filled a rectangle in a color
static bool findFillAt(CxUiGrid* g, int32 x, int32 y, int32 w, int32 h, CxUiColor c)
{
    CxUiTestCanvasInfo info;
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    for (int32 i = 0; i < info.nops; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        if (op->kind == CXUI_TESTOP_Fill && op->r.x == x && op->r.y == y && op->r.w == w && op->r.h == h &&
            op->c.bits == c.bits)
            return true;
    }
    return false;
}

// A tree's nodes are indented by depth after their expanders, which point right while closed and
// down while open; guide lines join nodes to their parents and siblings.
UI_TEST(treepaint)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(2);
    CxUiNodeId a     = cxuiTreeStoreAppend(t, 0, _S"alpha", _S"1");
    CxUiNodeId a1    = cxuiTreeStoreAppend(t, a, _S"one");
    cxuiTreeStoreAppend(t, a1, _S"deep");
    cxuiTreeStoreAppend(t, a, _S"two");
    cxuiTreeStoreAppend(t, 0, _S"beta");
    CxUiWindow* w;
    CxUiGrid* g = treeGrid(t, CXUI_GRID_TreeLines, &w);
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
    CxUiColor text = palColor(g, CXUI_PAL_Text), line = palColor(g, CXUI_PAL_GridLine);

    // Indents of 16, starting 3 in, the expander centered in the first: the closed arrow's
    // first column, 8 tall, at 3 + 4 + 2
    gridRepaint(g, w);
    const CxUiTestDrawOp* alpha = gridText(g, _S"alpha");
    if (!alpha || alpha->a.x != 19 || !treeFill(g, 9, 1, 8, text, 0) || gridText(g, _S"one"))
        TEST_FAIL(1, _SL("closed: alpha at ${int}, wanted 19, or no arrow pointing right"), stvar(int32, alpha ? alpha->a.x : -1));

    cxuiGridSetExpanded(g, a, true);
    cxuiGridSetExpanded(g, a1, true);
    gridRepaint(g, w);
    const CxUiTestDrawOp* one  = gridText(g, _S"one");
    const CxUiTestDrawOp* deep = gridText(g, _S"deep");
    // Open: the arrow's widest line, 8 wide at 3 + 4
    if (!one || one->a.x != 35 || !deep || deep->a.x != 51 || deep->a.y != headH + 2 * rowH + (rowH - textH) / 2 ||
        !treeFill(g, 7, 8, 1, text, 0))
        TEST_FAIL(1, _SL("open: one at ${int}, deep at ${int}, or no arrow pointing down"), stvar(int32, one ? one->a.x : -1),
                  stvar(int32, deep ? deep->a.x : -1));
    // Guide lines: alpha's through its children's rows, at 3 + 8, the full height for one, which
    // has a sibling after it; the top half for two, the last child; stopping 6 short of one's
    // expander, above and below.
    if (!treeFill(g, 11, 1, rowH, line, 0) || !treeFill(g, 27, 1, rowH / 2, line, 0) ||
        !treeFill(g, 27, 1, rowH / 2 - 6, line, 0) || !treeFill(g, 33, 1 + 8 - 6 - 1, 1, line, 0))
        TEST_FAIL(1, _SL("no guide lines"), stvNone);

    // With the tree column second, the first shows its cells plainly and the second is indented.
    cxuiGridSetColumns(g, { .title = _S"Name", .width = cxuiLen(100) }, { .title = _S"Size", .kind = CXUI_COL_Tree,
                                                                         .flags = CXUI_COL_Stretch });
    settle(w);
    gridRepaint(g, w);
    alpha                         = gridText(g, _S"alpha");
    const CxUiTestDrawOp* oneSize = gridText(g, _S"1");
    if (!alpha || alpha->a.x != 6 || !oneSize || oneSize->a.x != 100 + 19)
        TEST_FAIL(1, _SL("tree column second: alpha at ${int}, its size at ${int}"), stvar(int32, alpha ? alpha->a.x : -1),
                  stvar(int32, oneSize ? oneSize->a.x : -1));

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&t);
    return 0;
}

typedef struct TreeLog {
    int32 expands, collapses, activates;
    uint64 item;
    CxUiTreeStore* store;   // when set, opening a node adds a child to it
    bool takeActivate;
} TreeLog;

static CxUiResult treeLogExpand(stvlist* cvars, CxUiExpandEvent* ev)
{
    TreeLog* log = stvlNextPtr(cvars);
    if (ev->expanded)
        log->expands++;
    else
        log->collapses++;
    log->item = ev->item;
    if (ev->expanded && log->store)
        cxuiTreeStoreAppend(log->store, (CxUiNodeId)ev->item, _S"lazy");
    return CXUI_Default;
}

static CxUiResult treeLogActivate(stvlist* cvars, CxUiActivateEvent* ev)
{
    TreeLog* log = stvlNextPtr(cvars);
    log->activates++;
    log->item = ev->item;
    return log->takeActivate ? CXUI_Handled : CXUI_Default;
}

// Clicking an expander opens the node, and the handler can add its children first; Right and
// Left open, close and move between parent and child; double-clicking and Enter open and close a
// node unless the activate handler takes them; closing a node moves the focus row in it to the
// node and unselects its children; the selection stays on its node as rows above open.
UI_TEST(treeinput)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(1);
    CxUiNodeId a     = cxuiTreeStoreAppend(t, 0, _S"a");
    CxUiNodeId a1    = cxuiTreeStoreAppend(t, a, _S"a1");
    CxUiNodeId b     = cxuiTreeStoreAppend(t, 0, _S"b");
    CxUiNodeId b1    = cxuiTreeStoreAppend(t, b, _S"b1");
    cxuiTreeStoreAppend(t, 0, _S"c");
    CxUiWindow* w;
    CxUiGrid* g = treeGrid(t, 0, &w);
    TreeLog log = { .store = t };
    cxuiOnExpand(g, treeLogExpand, stvar(ptr, &log));
    cxuiOnActivate(g, treeLogActivate, stvar(ptr, &log));
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
#define ROW_Y(r) (headH + (r) * rowH + rowH / 2)
    CxUiGridBody* body = g->_body;

    // The expander is the first indent; a click there opens without selecting.
    gridClickAt(g, CXUI_BTN_Left, 0, 10, ROW_Y(0));
    if (log.expands != 1 || log.item != a || !cxuiGridIsExpanded(g, a) || cxuiGridRowCount(g) != 5 ||
        cxuiGridSelectedCount(g) != 0 || !strEq(cxuiTreeStoreText(t, cxuiGridNodeAt(g, 2), 0), _S"lazy"))
        TEST_FAIL(1, _SL("an expander click: ${int} expands, ${int} rows, ${int} selected"), stvar(int32, log.expands),
                  stvar(int64, cxuiGridRowCount(g)), stvar(int64, cxuiGridSelectedCount(g)));

    // b is row 3. Right opens it, Right again goes to b1, Left back to b, Left closes it.
    gridClickAt(g, CXUI_BTN_Left, 0, 100, ROW_Y(3));
    pressKey(body, CXUI_KEY_Right, 0);
    if (!cxuiGridIsExpanded(g, b) || cxuiGridFocusNode(g) != b || log.expands != 2)
        TEST_FAIL(1, _SL("Right didn't open b"), stvNone);
    pressKey(body, CXUI_KEY_Right, 0);
    if (cxuiGridFocusNode(g) != b1 || cxuiGridSelectedNode(g) != b1)
        TEST_FAIL(1, _SL("Right on an open node didn't go to its child"), stvNone);
    // A node without children has nothing to open.
    pressKey(body, CXUI_KEY_Right, 0);
    if (cxuiGridFocusNode(g) != b1 || log.expands != 2)
        TEST_FAIL(1, _SL("Right on a leaf: ${int} expand events"), stvar(int32, log.expands));
    pressKey(body, CXUI_KEY_Left, 0);
    if (cxuiGridFocusNode(g) != b || !cxuiGridIsExpanded(g, b))
        TEST_FAIL(1, _SL("Left on a child didn't go to its parent"), stvNone);
    pressKey(body, CXUI_KEY_Left, 0);
    if (cxuiGridIsExpanded(g, b) || log.collapses != 1)
        TEST_FAIL(1, _SL("Left on an open node didn't close it"), stvNone);
    pressKey(body, CXUI_KEY_PadAdd, 0);
    bool padOpened = cxuiGridIsExpanded(g, b);
    pressKey(body, CXUI_KEY_PadSubtract, 0);
    if (!padOpened || cxuiGridIsExpanded(g, b) || log.collapses != 2)
        TEST_FAIL(1, _SL("the keypad's + and - didn't open and close b"), stvNone);

    // Double click and Enter: opening by default, the node as the item
    gridClickAt(g, CXUI_BTN_Left, 0, 100, ROW_Y(3));
    gridClickAt(g, CXUI_BTN_Left, 0, 100, ROW_Y(3));
    if (log.activates != 1 || log.item != b || !cxuiGridIsExpanded(g, b))
        TEST_FAIL(1, _SL("a double click: ${int} activations, b open ${bool}"), stvar(int32, log.activates),
                  stvar(bool, cxuiGridIsExpanded(g, b)));
    log.takeActivate = true;
    pressKey(body, CXUI_KEY_Enter, 0);
    if (log.activates != 2 || !cxuiGridIsExpanded(g, b))
        TEST_FAIL(1, _SL("Enter taken by the handler still closed b"), stvNone);
    log.takeActivate = false;

    // Closing b with b1 focused and selected
    cxuiGridClearSelection(g);
    cxuiGridSetFocusNode(g, b1);
    cxuiGridSetNodeSelected(g, b1, true);
    cxuiGridSetExpanded(g, b, false);
    if (cxuiGridFocusNode(g) != b || cxuiGridSelectedCount(g) != 0 || cxuiGridIsNodeSelected(g, b1))
        TEST_FAIL(1, _SL("closing b: focus on ${uint}, ${int} selected"), stvar(uint64, cxuiGridFocusNode(g)),
                  stvar(int64, cxuiGridSelectedCount(g)));

    // b selected at row 3 stays selected as a, above it, closes and opens.
    cxuiGridSetNodeSelected(g, b, true);
    cxuiGridSetExpanded(g, a, false);
    if (cxuiGridSelected(g) != 1 || cxuiGridSelectedNode(g) != b)
        TEST_FAIL(1, _SL("closing a moved the selection to row ${int}"), stvar(int64, cxuiGridSelected(g)));
    cxuiGridSetExpanded(g, a, true);
    if (cxuiGridSelected(g) != 3 || cxuiGridSelectedNode(g) != b)
        TEST_FAIL(1, _SL("opening a moved the selection to row ${int}"), stvar(int64, cxuiGridSelected(g)));

    // Showing a node opens the nodes above it, from code with no events.
    int32 expands = log.expands;
    CxUiNodeId deep = cxuiTreeStoreAppend(t, b1, _S"deep");
    cxuiGridSetExpanded(g, b, false);
    cxuiGridShowNode(g, deep);
    if (cxuiGridRowOf(g, deep) < 0 || !cxuiGridIsExpanded(g, b) || !cxuiGridIsExpanded(g, b1) || log.expands != expands)
        TEST_FAIL(1, _SL("showing a node: row ${int}, ${int} events"), stvar(int64, cxuiGridRowOf(g, deep)),
                  stvar(int32, log.expands - expands));
    if (cxuiGridDepthOf(g, deep) != 2 || cxuiGridDepthOf(g, a1) != 1 || cxuiGridDepthOf(g, a) != 0)
        TEST_FAIL(1, _SL("depths"), stvNone);

    // A child's expander is one indent further in.
    cxuiGridSetExpanded(g, b1, false);
    expands = log.expands;
    gridClickAt(g, CXUI_BTN_Left, 0, 3 + 16 + 8, ROW_Y((int32)cxuiGridRowOf(g, b1)));
    if (!cxuiGridIsExpanded(g, b1) || log.expands != expands + 1)
        TEST_FAIL(1, _SL("a click on b1's expander didn't open it"), stvNone);

    // Closing everything moves a focus row now hidden to its node at the top.
    cxuiGridSetFocusNode(g, deep);
    cxuiGridCollapseAll(g, 0);
    if (cxuiGridFocusNode(g) != b || cxuiGridRowCount(g) != 3)
        TEST_FAIL(1, _SL("closing everything: focus on ${uint}, ${int} rows"), stvar(uint64, cxuiGridFocusNode(g)),
                  stvar(int64, cxuiGridRowCount(g)));

    // Typing searches the tree column.
    g->_typedAt = 0;
    pressKey(body, CXUI_KEY_C, 0);
    if (!strEq(cxuiTreeStoreText(t, cxuiGridFocusNode(g), 0), _S"c"))
        TEST_FAIL(1, _SL("typing c didn't go to c"), stvNone);
#undef ROW_Y

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&t);
    return 0;
}

// Nodes added and removed under open nodes move the rows below, and the selection with them;
// under closed nodes they don't; a reset keeps open the nodes still there.
UI_TEST(treenotify)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(1);
    CxUiNodeId a     = cxuiTreeStoreAppend(t, 0, _S"a");
    CxUiNodeId b     = cxuiTreeStoreAppend(t, 0, _S"b");
    CxUiNodeId b1    = cxuiTreeStoreAppend(t, b, _S"b1");
    CxUiNodeId c     = cxuiTreeStoreAppend(t, 0, _S"c");
    CxUiWindow* w;
    CxUiGrid* g = treeGrid(t, 0, &w);
    cxuiGridSetExpanded(g, b, true);
    cxuiGridSetNodeSelected(g, c, true);
    if (cxuiGridRowCount(g) != 4 || cxuiGridSelected(g) != 3)
        TEST_FAIL(1, _SL("start: ${int} rows, c at ${int}"), stvar(int64, cxuiGridRowCount(g)), stvar(int64, cxuiGridSelected(g)));

    CxUiNodeId a1 = cxuiTreeStoreAppend(t, a, _S"a1");
    if (cxuiGridRowCount(g) != 4 || cxuiGridSelected(g) != 3)
        TEST_FAIL(1, _SL("added under a closed node: ${int} rows"), stvar(int64, cxuiGridRowCount(g)));
    CxUiNodeId b0 = cxuiTreeStoreInsert(t, b, 0, _S"b0");
    if (cxuiGridRowCount(g) != 5 || cxuiGridSelected(g) != 4 || cxuiGridNodeAt(g, 2) != b0)
        TEST_FAIL(1, _SL("added under an open node: ${int} rows, c at ${int}"), stvar(int64, cxuiGridRowCount(g)),
                  stvar(int64, cxuiGridSelected(g)));
    cxuiGridSetExpanded(g, a, true);
    cxuiTreeStoreRemove(t, b);
    if (cxuiGridRowCount(g) != 3 || cxuiGridSelected(g) != 2 || cxuiGridSelectedNode(g) != c || cxuiGridIsExpanded(g, b1))
        TEST_FAIL(1, _SL("an open node removed: ${int} rows, c at ${int}"), stvar(int64, cxuiGridRowCount(g)),
                  stvar(int64, cxuiGridSelected(g)));

    cxuiGridReset(g);
    if (!cxuiGridIsExpanded(g, a) || cxuiGridRowCount(g) != 3 || cxuiGridNodeAt(g, 1) != a1 || cxuiGridSelectedCount(g) != 0)
        TEST_FAIL(1, _SL("a reset: a open ${bool}, ${int} rows"), stvar(bool, cxuiGridIsExpanded(g, a)),
                  stvar(int64, cxuiGridRowCount(g)));
    cxuiTreeStoreClear(t);
    if (cxuiGridRowCount(g) != 0 || cxuiGridIsExpanded(g, a))
        TEST_FAIL(1, _SL("cleared: ${int} rows"), stvar(int64, cxuiGridRowCount(g)));

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&t);
    return 0;
}

// The rows shown, as "t1 t2 ..."
static void treeRows(CxUiGrid* g, string* out)
{
    string one = 0;
    strClear(out);
    for (int64 r = 0; r < cxuiGridRowCount(g); r++) {
        strFormat(&one, _S"t${uint} ", stvar(uint64, cxuiGridNodeAt(g, r)));
        strAppend(out, one);
    }
    strDestroy(&one);
}

// The program's own source: the grid reads it through the interface, hears of changes only when
// told, and a reset after reordering, or a node opening again, reads its children again.
UI_TEST(treesource)
{
    UiTestTree* src = uitesttreeCreate();
    for (int32 i = 0; i < 3; i++)
        uitesttreeAdd(src, 0);
    uitesttreeAdd(src, 2);   // 4
    uitesttreeAdd(src, 2);   // 5
    uitesttreeAdd(src, 5);   // 6
    uitesttreeAdd(src, 5);   // 7
    CxUiWindow* w;
    CxUiGrid* g = treeGrid(CxUiTreeStoreNone, 0, &w);
    cxuiGridSetSource(g, src);
    cxuiGridSetExpanded(g, 5, true);
    string rows = 0;
    treeRows(g, &rows);
    if (!strEq(rows, _S"t1 t2 t4 t5 t6 t7 t3 ") || !cxuiGridIsExpanded(g, 2))
        TEST_FAIL(1, _SL("opening 5: ${string}"), stvar(strref, rows));
    gridRepaint(g, w);
    if (!gridText(g, _S"t6"))
        TEST_FAIL(1, _SL("the source's cells weren't painted"), stvNone);

    // Every node in the opposite order, unknown to the grid until the reset
    for (int32 i = 0, n = saSize(src->order); i < n / 2; i++) {
        uint64 x               = src->order.a[i];
        src->order.a[i]         = src->order.a[n - 1 - i];
        src->order.a[n - 1 - i] = x;
    }
    cxuiGridReset(g);
    treeRows(g, &rows);
    if (!strEq(rows, _S"t3 t2 t5 t7 t6 t4 t1 "))
        TEST_FAIL(1, _SL("reordered and reset: ${string}"), stvar(strref, rows));

    // Told of a node added under a closed node and of one removed under an open one
    CxUiNodeId n8 = uitesttreeAdd(src, 4);
    cxuiGridNodeInserted(g, n8);
    saFindRemove(&src->order, uint64, 6);
    cxuiGridNodeRemoved(g, 5, 6);
    treeRows(g, &rows);
    if (!strEq(rows, _S"t3 t2 t5 t7 t4 t1 "))
        TEST_FAIL(1, _SL("a node added under a closed one and one removed: ${string}"), stvar(strref, rows));
    cxuiGridSetExpanded(g, 4, true);
    treeRows(g, &rows);
    if (!strEq(rows, _S"t3 t2 t5 t7 t4 t8 t1 "))
        TEST_FAIL(1, _SL("opening 4: ${string}"), stvar(strref, rows));
    // A node opening again reads its children again, finding one the grid wasn't told of.
    cxuiGridSetExpanded(g, 4, false);
    uitesttreeAdd(src, 4);
    cxuiGridSetExpanded(g, 4, true);
    treeRows(g, &rows);
    if (!strEq(rows, _S"t3 t2 t5 t7 t4 t8 t9 t1 "))
        TEST_FAIL(1, _SL("opening 4 again: ${string}"), stvar(strref, rows));
    strDestroy(&rows);

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&src);
    return 0;
}

typedef struct CheckLog {
    int32 checks;
    uint64 item;
    int32 column;
    uint32 state;
    bool take;                // handles the event, so the grid leaves the box alone
    CxUiTreeStore* spread;    // sets the box itself and spreads it through the tree
} CheckLog;

static CxUiResult logCheck(stvlist* cvars, CxUiCheckEvent* ev)
{
    CheckLog* log = stvlNextPtr(cvars);
    log->checks++;
    log->item   = ev->item;
    log->column = ev->column;
    log->state  = ev->state;
    if (log->spread) {
        cxuiTreeStoreSetCheck(log->spread, (CxUiNodeId)ev->item, ev->column, ev->state);
        cxuiTreeStorePropagateCheck(log->spread, (CxUiNodeId)ev->item, ev->column);
        return CXUI_Handled;
    }
    return log->take ? CXUI_Handled : CXUI_Default;
}

// A check column draws each cell's box in its state, or none where hidden; a click on a box
// raises the event and sets the store without selecting, unless the handler takes it; Space
// changes the focus row, or every selected row with it; the boxes move with their rows.
UI_TEST(gridcheck)
{
    CxUiListStore* store = cxuiListStoreCreate(3);
    for (int32 i = 0; i < 20; i++)
        addNumberedChecked(store, i);
    cxuiListStoreSetCheck(store, 1, 0, CXUI_CHECK_On);
    cxuiListStoreSetCheck(store, 2, 0, CXUI_CHECK_Mixed);
    cxuiListStoreSetFlags(store, 3, CXUI_CELL_CheckHidden);
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"Done", .kind = CXUI_COL_Check },
                                        { .title = _S"Name", .flags = CXUI_COL_Stretch } },
        .ncolumns = 2,
        .select   = CXUI_SELECT_Multi });
    cxuiGridSetSource(g, store);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    CheckLog log = { 0 };
    cxuiOnCheck(g, logCheck, stvar(ptr, &log));
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
#define ROW_Y(r) (headH + (r) * rowH + rowH / 2)
    CxUiColor input = palColor(g, CXUI_PAL_Input), accent = palColor(g, CXUI_PAL_Accent);

    // "Done" is 32 wide with 6 either side; the 13 box is centered in its 44.
    gridRepaint(g, w);
    int32 bx = (44 - 13) / 2, by = headH + (rowH - 13) / 2;
    if (!treeFill(g, bx, 13, 13, input, 0) || !findFillAt(g, bx, by + rowH, 13, 13, accent) ||
        !findFillAt(g, bx + 3, by + 2 * rowH + 3, 7, 7, accent) || findFillAt(g, bx, by + 3 * rowH, 13, 13, input))
        TEST_FAIL(1, _SL("the boxes aren't drawn off, on, mixed and hidden"), stvNone);
    if (gridText(g, _S"x0"))
        TEST_FAIL(1, _SL("a check column drew its cell's text"), stvNone);
    if (cxuiGridEdit(g, 0, 0) || g->_editFrame)
        TEST_FAIL(1, _SL("a check column was edited"), stvNone);

    gridClickAt(g, CXUI_BTN_Left, 0, bx + 6, ROW_Y(0));
    if (log.checks != 1 || log.item != 0 || log.column != 0 || log.state != CXUI_CHECK_On ||
        cxuiListStoreCheck(store, 0, 0) != CXUI_CHECK_On || cxuiGridSelectedCount(g) != 0)
        TEST_FAIL(1, _SL("a click on a box: ${int} events, now ${int}, ${int} selected"), stvar(int32, log.checks),
                  stvar(uint32, cxuiListStoreCheck(store, 0, 0)), stvar(int64, cxuiGridSelectedCount(g)));
    gridClickAt(g, CXUI_BTN_Left, 0, bx + 4, ROW_Y(2));
    if (log.state != CXUI_CHECK_On || cxuiListStoreCheck(store, 2, 0) != CXUI_CHECK_On)
        TEST_FAIL(1, _SL("a click on a mixed box made it ${int}"), stvar(uint32, log.state));
    log.take = true;
    gridClickAt(g, CXUI_BTN_Left, 0, bx + 6, ROW_Y(1));
    if (log.checks != 3 || log.state != CXUI_CHECK_Off || cxuiListStoreCheck(store, 1, 0) != CXUI_CHECK_On)
        TEST_FAIL(1, _SL("a handler that took the event: the box became ${int}"), stvar(uint32, cxuiListStoreCheck(store, 1, 0)));
    log.take = false;
    gridClickAt(g, CXUI_BTN_Left, 0, bx + 6, ROW_Y(3));
    if (log.checks != 3 || cxuiGridSelected(g) != 3)
        TEST_FAIL(1, _SL("a click where a box is hidden: ${int} events, row ${int} selected"), stvar(int32, log.checks),
                  stvar(int64, cxuiGridSelected(g)));

    // Space: the focus row alone, then the rows selected with it, all one way from the focus row
    CxUiGridBody* body = g->_body;
    cxuiGridClearSelection(g);
    cxuiGridSetFocusRow(g, 4);
    pressKey(body, CXUI_KEY_Space, 0);
    if (cxuiListStoreCheck(store, 4, 0) != CXUI_CHECK_On || cxuiGridSelectedCount(g) != 0)
        TEST_FAIL(1, _SL("Space on the focus row: ${int}"), stvar(uint32, cxuiListStoreCheck(store, 4, 0)));
    cxuiListStoreSetCheck(store, 6, 0, CXUI_CHECK_On);
    cxuiGridSetSelected(g, 5, 3, true);
    cxuiGridSetFocusRow(g, 5);
    pressKey(body, CXUI_KEY_Space, 0);
    if (cxuiListStoreCheck(store, 5, 0) != CXUI_CHECK_On || cxuiListStoreCheck(store, 6, 0) != CXUI_CHECK_On ||
        cxuiListStoreCheck(store, 7, 0) != CXUI_CHECK_On || cxuiGridSelectedCount(g) != 3)
        TEST_FAIL(1, _SL("Space on selected rows: ${int} ${int} ${int}"), stvar(uint32, cxuiListStoreCheck(store, 5, 0)),
                  stvar(uint32, cxuiListStoreCheck(store, 6, 0)), stvar(uint32, cxuiListStoreCheck(store, 7, 0)));
    int32 checks = log.checks;
    pressKey(body, CXUI_KEY_Space, GRID_TOGGLE);
    if (log.checks != checks || cxuiGridIsSelected(g, 5))
        TEST_FAIL(1, _SL("Ctrl+Space changed a box, or didn't unselect"), stvNone);

    // Sorted by name, largest first: row 0's box goes with it to the end.
    cxuiListStoreSort(store, 1, true);
    if (cxuiListStoreCheck(store, 19, 0) != CXUI_CHECK_On || cxuiListStoreCheck(store, 0, 0) != CXUI_CHECK_Off)
        TEST_FAIL(1, _SL("sorting left the boxes behind"), stvNone);
#undef ROW_Y

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&store);
    return 0;
}

// A tree's box sits between the expander and the text; checking a node from the handler and
// spreading it checks the nodes under it and makes those above it checked, cleared or mixed,
// ignoring hidden boxes.
UI_TEST(treecheck)
{
    CxUiTreeStore* t = cxuiTreeStoreCreate(1);
    CxUiNodeId a     = cxuiTreeStoreAppend(t, 0, _S"a");
    CxUiNodeId a1    = cxuiTreeStoreAppend(t, a, _S"a1");
    CxUiNodeId a2    = cxuiTreeStoreAppend(t, a, _S"a2");
    CxUiNodeId a21   = cxuiTreeStoreAppend(t, a2, _S"a21");
    CxUiNodeId b     = cxuiTreeStoreAppend(t, 0, _S"b");
    CxUiWindow* w;
    CxUiGrid* g = treeGrid(t, 0, &w);
    cxuiGridSetColumns(g, { .title = _S"Name", .kind = CXUI_COL_Tree, .flags = CXUI_COL_Stretch | CXUI_COL_CheckBox });
    cxuiGridExpandAll(g, 0);
    settle(w);
    CheckLog log = { .spread = t };
    cxuiOnCheck(g, logCheck, stvar(ptr, &log));
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
#define ROW_Y(r) (headH + (r) * rowH + rowH / 2)

    gridRepaint(g, w);
    const CxUiTestDrawOp* ta = gridText(g, _S"a");
    if (!treeFill(g, 19, 13, 13, palColor(g, CXUI_PAL_Input), 0) || !ta || ta->a.x != 19 + 13 + 3)
        TEST_FAIL(1, _SL("the box isn't after the expander: a's text at ${int}"), stvar(int32, ta ? ta->a.x : -1));

    gridClickAt(g, CXUI_BTN_Left, 0, 25, ROW_Y(0));
    if (log.item != a || cxuiTreeStoreCheck(t, a, 0) != CXUI_CHECK_On || cxuiTreeStoreCheck(t, a21, 0) != CXUI_CHECK_On ||
        cxuiTreeStoreCheck(t, a1, 0) != CXUI_CHECK_On || cxuiTreeStoreCheck(t, b, 0) != CXUI_CHECK_Off)
        TEST_FAIL(1, _SL("checking a didn't check the nodes under it alone"), stvNone);
    // a21 is row 3: clearing it clears a2, its only child's parent, and mixes a.
    gridClickAt(g, CXUI_BTN_Left, 0, 25 + 32, ROW_Y(3));
    if (cxuiTreeStoreCheck(t, a21, 0) != CXUI_CHECK_Off || cxuiTreeStoreCheck(t, a2, 0) != CXUI_CHECK_Off ||
        cxuiTreeStoreCheck(t, a, 0) != CXUI_CHECK_Mixed)
        TEST_FAIL(1, _SL("clearing a21: a2 ${int}, a ${int}"), stvar(uint32, cxuiTreeStoreCheck(t, a2, 0)),
                  stvar(uint32, cxuiTreeStoreCheck(t, a, 0)));
    // With a1's box hidden, a follows a2 alone.
    cxuiTreeStoreSetFlags(t, a1, CXUI_CELL_CheckHidden);
    cxuiTreeStorePropagateCheck(t, a21, 0);
    if (cxuiTreeStoreCheck(t, a, 0) != CXUI_CHECK_Off)
        TEST_FAIL(1, _SL("a hidden box still counted: a is ${int}"), stvar(uint32, cxuiTreeStoreCheck(t, a, 0)));
    // A parent whose children's boxes are all hidden keeps its own.
    CxUiNodeId b1 = cxuiTreeStoreAppend(t, b, _S"b1");
    cxuiTreeStoreSetFlags(t, b1, CXUI_CELL_CheckHidden);
    cxuiTreeStoreSetCheck(t, b, 0, CXUI_CHECK_Mixed);
    cxuiTreeStorePropagateCheck(t, b1, 0);
    if (cxuiTreeStoreCheck(t, b, 0) != CXUI_CHECK_Mixed)
        TEST_FAIL(1, _SL("hidden children changed their parent to ${int}"), stvar(uint32, cxuiTreeStoreCheck(t, b, 0)));
    cxuiTreeStoreRemove(t, b1);
    cxuiTreeStoreSetCheck(t, b, 0, CXUI_CHECK_Off);

    // Space on b
    cxuiGridSetFocusNode(g, b);
    pressKey(g->_body, CXUI_KEY_Space, 0);
    if (cxuiTreeStoreCheck(t, b, 0) != CXUI_CHECK_On || log.item != b)
        TEST_FAIL(1, _SL("Space didn't check b"), stvNone);

    // A column narrower than a node's indent clips its lines and expander.
    cxuiGridSetColumns(g, { .title = _S"Name", .kind = CXUI_COL_Tree, .width = cxuiLen(20), .minWidth = cxuiLen(4) });
    gridRepaint(g, w);
    CxUiTestCanvasInfo info;
    int32 spilled = 0;
    cxuiTestCanvasInfo(CxUiCanvas(g->_body), &info);
    for (int32 i = 0; i < info.nops; i++) {
        const CxUiTestDrawOp* op = &info.ops[i];
        // The expanders' strokes, a pixel or two wide, past the column's edge
        if (op->kind == CXUI_TESTOP_Fill && op->r.y >= headH && op->r.w <= 2 && op->r.x + op->r.w > 20)
            spilled++;
    }
    if (spilled)
        TEST_FAIL(1, _SL("a narrow outline column's expanders spilled out ${int} times"), stvar(int32, spilled));
#undef ROW_Y

    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&t);
    return 0;
}

typedef struct EditLog {
    int32 starts, commits, cancels;
    uint64 item;
    int32 column;
    string text;
    bool refuse;   // handles the start, which closes the editor
    bool take;     // handles a commit, so the grid sets nothing
} EditLog;

static CxUiResult logEdit(stvlist* cvars, CxUiEditEvent* ev)
{
    EditLog* log = stvlNextPtr(cvars);
    log->item    = ev->item;
    log->column  = ev->column;
    strDup(&log->text, ev->text);
    switch (ev->stage) {
    case CXUI_EDIT_Start:
        log->starts++;
        return log->refuse ? CXUI_Handled : CXUI_Default;
    case CXUI_EDIT_Commit:
        log->commits++;
        return log->take ? CXUI_Handled : CXUI_Default;
    default:
        log->cancels++;
        return CXUI_Default;
    }
}

static int32 editEvents(EditLog* log)
{
    return log->starts + log->commits + log->cancels;
}

// Sends a key to a grid's editor, which may close on it
static void editKey(CxUiGrid* g, CxUiKey key, uint32 mods)
{
    CxUiTextField* ed = cxuiGridEditor(g);
    if (!ed)
        return;
    objAcquire(ed);
    pressKey(ed, key, mods);
    objRelease(&ed);
}

// Whether the editor is open with its frame at x, y, w, h in the window
static bool editorAt(CxUiGrid* g, int32 x, int32 y, int32 w, int32 h)
{
    CxUiTestPanelInfo pi;
    return g->_editFrame && cxuiTestPanelInfo(CxUiWidget(g->_editFrame), &pi) && rectIs(pi.bounds, x, y, w, h);
}

static CxUiRect editorRect(CxUiGrid* g)
{
    CxUiTestPanelInfo pi;
    return g->_editFrame && cxuiTestPanelInfo(CxUiWidget(g->_editFrame), &pi) ? pi.bounds : (CxUiRect){ -1, -1, -1, -1 };
}

// A grid of 50 rows, with editable "row n" and "note n" and a "n KB" that isn't, over a text
// field, 300x230 in a window
static CxUiGrid* editGrid(CxUiListStore** store, CxUiTextField** other, CxUiWindow** win)
{
    *store = cxuiListStoreCreate(3);
    for (int32 i = 0; i < 50; i++) {
        string name = 0, note = 0, size = 0;
        strFormat(&name, _S"row ${int}", stvar(int32, i));
        strFormat(&note, _S"note ${int}", stvar(int32, i));
        strFormat(&size, _S"${int} KB", stvar(int32, i));
        cxuiListStoreAppend(*store, name, note, size);
        strDestroy(&name);
        strDestroy(&note);
        strDestroy(&size);
    }
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .flags = CXUI_COL_Stretch | CXUI_COL_Editable },
                                        { .title = _S"Note", .width = cxuiLen(80), .flags = CXUI_COL_Editable },
                                        { .title = _S"Size", .width = cxuiLen(60) } },
        .ncolumns = 3,
        .select   = CXUI_SELECT_Multi });
    cxuiGridSetSource(g, *store);
    CxUiBox* box = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V });
    cxuiContainerAppend(box, g);
    *other = cxuiTextFieldCreate(NULL);
    cxuiContainerAppend(box, *other);
    *win = startWindow(300, 230, CxUiWidget(box));
    settle(*win);
    return g;
}

// F2 opens a field over the focus row's first editable cell, with the text selected; keys typed
// there are the field's; Enter sets the store, Esc doesn't, nor does Enter when the handler takes
// it; a handler handling the start closes it; read-only cells and other columns are left to the
// API; Tab goes along and down, Shift+Tab back; focus going elsewhere keeps the edit, or drops it
// with CancelOnBlur, focus leaving the window leaves it open; a click in the rows and scrolling
// keep it; it follows its row, closing quietly when the row goes; a click on the selected row
// edits it after the double-click time; resizing keeps its row shown.
UI_TEST(gridedit)
{
    CxUiListStore* store;
    CxUiTextField* other;
    CxUiWindow* w;
    CxUiGrid* g = editGrid(&store, &other, &w);
    EditLog log = { 0 };
    cxuiOnEdit(g, logEdit, stvar(ptr, &log));
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
#define ROW_TOP(r) (headH + (r) * rowH)
#define ROW_Y(r)   (ROW_TOP(r) + rowH / 2)
    CxUiGridBody* body = g->_body;
    cxuiFocus(g);
    // The body is 288 across, by the scroll bar: Name is 148, Note 80. The frame starts 3 before
    // the text, which is 6 in, and ends a pixel short of the column's edge.
    int32 nameW = 288 - 140;

    cxuiGridSetFocusRow(g, 3);
    pressKey(body, CXUI_KEY_F2, 0);
    CxUiTextField* ed  = cxuiGridEditor(g);
    CxUiTestTextInfo ti = { 0 };
    if (!ed || !cxuiTestTextInfo(ed, &ti) || !strEq(ti.text, _S"row 3") || ti.selStart != 0 || ti.selEnd != 5 ||
        !ti.focused || log.starts != 1 || log.item != 3 || log.column != 0 || !strEq(log.text, _S"row 3"))
        TEST_FAIL(1, _SL("F2: editing \"${string}\", selected ${int} to ${int}, ${int} starts for row ${int}"),
                  stvar(strref, ti.text), stvar(int32, ti.selStart), stvar(int32, ti.selEnd), stvar(int32, log.starts),
                  stvar(uint64, log.item));
    cxuiTestTextInfoDestroy(&ti);
    CxUiTestPanelInfo pi;
    if (!cxuiTestPanelInfo(CxUiWidget(g->_editFrame), &pi) || !pi.raised || !rectIs(pi.bounds, 3, ROW_TOP(3), nameW - 4, rowH))
        TEST_FAIL(1, _SL("the editor is at ${int},${int} ${int}x${int}, raised ${bool}"), RECT_ARGS(pi.bounds),
                  stvar(bool, pi.raised));
    if (!cxuiIsTextInput(ed) || !cxuiIsTextInput(other) || cxuiIsTextInput(g) || cxuiIsTextInput(body) ||
        cxuiIsTextInput((CxUiWidget*)NULL))
        TEST_FAIL(1, _SL("cxuiIsTextInput is wrong"), stvNone);

    editKey(g, CXUI_KEY_Down, 0);
    editKey(g, CXUI_KEY_R, 0);
    if (!g->_editFrame || cxuiGridFocusRow(g) != 3)
        TEST_FAIL(1, _SL("keys in the editor moved the focus row to ${int}"), stvar(int64, cxuiGridFocusRow(g)));

    cxuiTestType(ed, _S"renamed");
    editKey(g, CXUI_KEY_Enter, 0);
    if (g->_editFrame || cxuiGridEditor(g) || log.commits != 1 || !strEq(log.text, _S"renamed") ||
        !strEq(cxuiListStoreText(store, 3, 0), _S"renamed") || !gridFocused(g))
        TEST_FAIL(1, _SL("Enter: ${int} commits of \"${string}\", the store has \"${string}\""), stvar(int32, log.commits),
                  stvar(strref, log.text), stvar(strref, cxuiListStoreText(store, 3, 0)));

    pressKey(body, CXUI_KEY_F2, 0);
    cxuiTestType(cxuiGridEditor(g), _S"dropped");
    editKey(g, CXUI_KEY_Escape, 0);
    if (g->_editFrame || log.cancels != 1 || !strEq(log.text, _S"dropped") || !strEq(cxuiListStoreText(store, 3, 0), _S"renamed"))
        TEST_FAIL(1, _SL("Esc: ${int} cancels, the store has \"${string}\""), stvar(int32, log.cancels),
                  stvar(strref, cxuiListStoreText(store, 3, 0)));

    log.take = true;
    pressKey(body, CXUI_KEY_F2, 0);
    cxuiTestType(cxuiGridEditor(g), _S"taken");
    editKey(g, CXUI_KEY_Enter, 0);
    log.take = false;
    if (log.commits != 2 || !strEq(cxuiListStoreText(store, 3, 0), _S"renamed"))
        TEST_FAIL(1, _SL("a handler that took the commit: the store has \"${string}\""), stvar(strref, cxuiListStoreText(store, 3, 0)));

    log.refuse = true;
    if (cxuiGridEdit(g, 4, 0) || g->_editFrame || log.starts != 4)
        TEST_FAIL(1, _SL("a handler that handled the start left the editor open"), stvNone);
    log.refuse = false;

    // Row 5 is read-only, to the user; the API edits it, and a column that isn't editable.
    cxuiListStoreSetFlags(store, 5, CXUI_CELL_ReadOnly);
    cxuiGridSetFocusRow(g, 5);
    pressKey(body, CXUI_KEY_F2, 0);
    if (g->_editFrame)
        TEST_FAIL(1, _SL("F2 edited a read-only row"), stvNone);
    if (!cxuiGridEdit(g, 5, 0) || !editorAt(g, 3, ROW_TOP(5), nameW - 4, rowH))
        TEST_FAIL(1, _SL("cxuiGridEdit didn't edit a read-only cell"), stvNone);
    // At least 60 across, as far as the body's edge
    if (!cxuiGridEdit(g, 5, 2) || !editorAt(g, nameW + 80 + 3, ROW_TOP(5), 288 - (nameW + 83), rowH) || log.commits != 3)
        TEST_FAIL(1, _SL("cxuiGridEdit of the size column: at ${int},${int} ${int}x${int}, ${int} commits"),
                  RECT_ARGS(editorRect(g)), stvar(int32, log.commits));
    cxuiGridEndEdit(g, false);
    if (g->_editFrame || log.cancels != 2)
        TEST_FAIL(1, _SL("cxuiGridEndEdit didn't cancel"), stvNone);
#if defined(_PLATFORM_MACOS)
    // Return renames on the Mac; a row with nothing to rename opens.
    cxuiGridSetFocusRow(g, 6);
    pressKey(body, CXUI_KEY_Enter, 0);
    if (!editorAt(g, 3, ROW_TOP(6), nameW - 4, rowH))
        TEST_FAIL(1, _SL("Return didn't edit row 6"), stvNone);
    editKey(g, CXUI_KEY_Escape, 0);
#endif

    // Tab: along the row, then the next row's first; Shift+Tab back
    cxuiGridSetFocusRow(g, 6);
    pressKey(body, CXUI_KEY_F2, 0);
    cxuiTestType(cxuiGridEditor(g), _S"n6");
    editKey(g, CXUI_KEY_Tab, 0);
    if (!strEq(cxuiListStoreText(store, 6, 0), _S"n6") || !editorAt(g, nameW + 3, ROW_TOP(6), 76, rowH) ||
        log.column != 1 || log.item != 6)
        TEST_FAIL(1, _SL("Tab: at ${int},${int} ${int}x${int} for column ${int}"), RECT_ARGS(editorRect(g)),
                  stvar(int32, log.column));
    editKey(g, CXUI_KEY_Tab, 0);
    if (!editorAt(g, 3, ROW_TOP(7), nameW - 4, rowH) || cxuiGridFocusRow(g) != 7 || cxuiGridSelected(g) != 7 ||
        cxuiGridSelectedCount(g) != 1)
        TEST_FAIL(1, _SL("Tab at the row's end: at ${int},${int} ${int}x${int}, focus row ${int}"),
                  RECT_ARGS(editorRect(g)), stvar(int64, cxuiGridFocusRow(g)));
    editKey(g, CXUI_KEY_Tab, CXUI_MOD_Shift);
    if (!editorAt(g, nameW + 3, ROW_TOP(6), 76, rowH) || cxuiGridFocusRow(g) != 6)
        TEST_FAIL(1, _SL("Shift+Tab: at ${int},${int} ${int}x${int}"), RECT_ARGS(editorRect(g)));
    editKey(g, CXUI_KEY_Escape, 0);

    // Focus leaving the window leaves the edit open; focus going to another widget keeps the text,
    // or drops it with CancelOnBlur.
    cxuiGridEdit(g, 2, 0);
    cxuiTestBlur();
    cxuiFlush();
    if (!g->_editFrame)
        TEST_FAIL(1, _SL("leaving the window closed the editor"), stvNone);
    cxuiFocus(cxuiGridEditor(g));
    cxuiTestType(cxuiGridEditor(g), _S"blurred");
    int32 commits = log.commits, cancels = log.cancels;
    cxuiFocus(other);
    cxuiFlush();
    if (g->_editFrame || log.commits != commits + 1 || !strEq(cxuiListStoreText(store, 2, 0), _S"blurred"))
        TEST_FAIL(1, _SL("focus going elsewhere: the store has \"${string}\""), stvar(strref, cxuiListStoreText(store, 2, 0)));
    g->_flags |= CXUI_GRID_CancelOnBlur;
    cxuiGridEdit(g, 2, 0);
    cxuiTestType(cxuiGridEditor(g), _S"dropped");
    cxuiFocus(other);
    cxuiFlush();
    g->_flags &= ~CXUI_GRID_CancelOnBlur;
    if (g->_editFrame || log.cancels != cancels + 1 || !strEq(cxuiListStoreText(store, 2, 0), _S"blurred"))
        TEST_FAIL(1, _SL("CancelOnBlur: the store has \"${string}\""), stvar(strref, cxuiListStoreText(store, 2, 0)));

    cxuiFocus(g);
    cxuiGridEdit(g, 2, 0);
    cxuiTestType(cxuiGridEditor(g), _S"clicked");
    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(4));
    if (g->_editFrame || log.commits != commits + 2 || cxuiGridSelected(g) != 4 || !strEq(cxuiListStoreText(store, 2, 0), _S"clicked"))
        TEST_FAIL(1, _SL("a click in the rows: the store has \"${string}\""), stvar(strref, cxuiListStoreText(store, 2, 0)));
    cxuiGridEdit(g, 2, 0);
    cxuiTestType(cxuiGridEditor(g), _S"scrolled");
    cxuiTestWheel(CxUiCanvas(body), 0, 1, 0, 50, 50);
    if (g->_editFrame || cxuiGridTop(g) != 3 || !strEq(cxuiListStoreText(store, 2, 0), _S"scrolled"))
        TEST_FAIL(1, _SL("scrolling: top ${int}, the store has \"${string}\""), stvar(int64, cxuiGridTop(g)),
                  stvar(strref, cxuiListStoreText(store, 2, 0)));
    cxuiGridSetTop(g, 0);
    // A row below those shown is scrolled to first.
    cxuiGridEdit(g, 30, 0);
    settle(w);
    int32 shownTop = (int32)cxuiGridTop(g);
    if (shownTop <= 0 || !editorAt(g, 3, ROW_TOP(30 - shownTop), nameW - 4, rowH))
        TEST_FAIL(1, _SL("editing a row not shown: top ${int}, at ${int},${int} ${int}x${int}"), stvar(int32, shownTop),
                  RECT_ARGS(editorRect(g)));
    editKey(g, CXUI_KEY_Escape, 0);
    cxuiGridSetTop(g, 0);

    // Rows coming above it move it down; its own row going closes it, saying nothing.
    cxuiGridEdit(g, 4, 0);
    cxuiListStoreInsert(store, 1, _S"new", _S"", _S"");
    settle(w);
    if (!editorAt(g, 3, ROW_TOP(5), nameW - 4, rowH))
        TEST_FAIL(1, _SL("a row inserted above: the editor is at ${int},${int} ${int}x${int}"), RECT_ARGS(editorRect(g)));
    cxuiListStoreRemove(store, 0, 1);
    settle(w);
    if (!editorAt(g, 3, ROW_TOP(4), nameW - 4, rowH))
        TEST_FAIL(1, _SL("a row removed above: the editor is at ${int},${int} ${int}x${int}"), RECT_ARGS(editorRect(g)));
    cxuiTestType(cxuiGridEditor(g), _S"moved");
    editKey(g, CXUI_KEY_Enter, 0);
    if (!strEq(cxuiListStoreText(store, 4, 0), _S"moved"))
        TEST_FAIL(1, _SL("the moved edit went to \"${string}\""), stvar(strref, cxuiListStoreText(store, 4, 0)));
    cxuiGridEdit(g, 5, 0);
    int32 events = editEvents(&log);
    cxuiListStoreRemove(store, 5, 1);
    if (g->_editFrame || editEvents(&log) != events || !gridFocused(g))
        TEST_FAIL(1, _SL("removing the edited row: still open ${bool}, ${int} events"), stvar(bool, g->_editFrame != NULL),
                  stvar(int32, editEvents(&log) - events));
    cxuiGridEdit(g, 3, 0);
    cxuiGridReset(g);
    if (g->_editFrame || editEvents(&log) != events + 1)
        TEST_FAIL(1, _SL("a reset left the editor open"), stvNone);

    // A click on the selected focus row's cell edits it after the double-click time, unless a
    // double click or a key comes first.
    cxuiTestSetDoubleClick(timeMS(40), 4);
    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(8));
    gridWait(100);
    if (g->_editFrame)
        TEST_FAIL(1, _SL("a click on a row not selected edited it"), stvNone);
    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(8));
    if (g->_editFrame)
        TEST_FAIL(1, _SL("a click edited before the double-click time"), stvNone);
    gridWait(100);
    if (!editorAt(g, 3, ROW_TOP(8), nameW - 4, rowH))
        TEST_FAIL(1, _SL("a click on the selected row didn't edit it"), stvNone);
    editKey(g, CXUI_KEY_Escape, 0);
    gridWait(100);
    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(8));
    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(8));
    gridWait(100);
    if (g->_editFrame)
        TEST_FAIL(1, _SL("a double click edited the row"), stvNone);
    gridWait(100);
    gridClickAt(g, CXUI_BTN_Left, 0, 50, ROW_Y(8));
    pressKey(body, CXUI_KEY_Up, 0);
    gridClickAt(g, CXUI_BTN_Left, 0, nameW + 20, ROW_Y(7));
    pressKey(body, CXUI_KEY_Space, 0);
    gridWait(100);
    if (g->_editFrame)
        TEST_FAIL(1, _SL("a key after the click didn't stop the edit"), stvNone);
    cxuiTestSetDoubleClick(timeMS(500), 4);

    // Made shorter, the grid scrolls to keep the edited row shown, and the editor stays over it.
    cxuiGridEdit(g, 6, 1);
    cxuiWidgetSetNaturalSize(g, cxuiLen(0), cxuiLen(60));
    cxuiTestResize(w, 300, 120);
    settle(w);
    int32 top = (int32)cxuiGridTop(g);
    if (top <= 0 || !editorAt(g, nameW + 3, ROW_TOP(6 - top), 76, rowH) || ROW_TOP(7 - top) > CxUiWidget(body)->_bounds.h)
        TEST_FAIL(1, _SL("made shorter: top ${int}, the editor at ${int},${int} ${int}x${int}"), stvar(int32, top),
                  RECT_ARGS(editorRect(g)));
    editKey(g, CXUI_KEY_Escape, 0);

    // Hiding the column, or replacing the columns, closes it quietly.
    events = editEvents(&log);
    cxuiGridEdit(g, 6, 1);
    cxuiGridSetColumnHidden(g, 1, true);
    if (g->_editFrame || editEvents(&log) != events + 1)
        TEST_FAIL(1, _SL("hiding the edited column left the editor open"), stvNone);
    cxuiGridSetColumnHidden(g, 1, false);
    cxuiGridEdit(g, 6, 0);
    cxuiGridSetColumns(g, { .title = _S"Name", .flags = CXUI_COL_Stretch | CXUI_COL_Editable });
    if (g->_editFrame || editEvents(&log) != events + 2)
        TEST_FAIL(1, _SL("new columns left the editor open"), stvNone);
#undef ROW_TOP
#undef ROW_Y

    // In a scroll box, the rows getting focus back as the editor closes don't scroll it.
    CxUiScrollBox* sb = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .view = { 0, 100 } });
    CxUiGrid* inner   = cxuiGridCreate(&(CxUiGridDesc){
        .columns = (CxUiGridColumn[]){ { .title = _S"Name", .flags = CXUI_COL_Stretch | CXUI_COL_Editable } }, .ncolumns = 1 });
    cxuiWidgetSetNaturalSize(inner, cxuiLen(0), cxuiLen(300));
    cxuiGridSetSource(inner, store);
    cxuiContainerAppend(sb, inner);
    CxUiWindow* w2 = startWindow(300, 100, CxUiWidget(sb));
    settle(w2);
    CxUiPoint before, after;
    cxuiGridEdit(inner, 10, 0);
    cxuiTestScrollPos(sb, &before);
    editKey(inner, CXUI_KEY_Enter, 0);
    cxuiTestScrollPos(sb, &after);
    if (before.y == 0 || !gridFocused(inner) || after.y != before.y)
        TEST_FAIL(1, _SL("closing the editor scrolled from ${int} to ${int}"), stvar(int32, before.y), stvar(int32, after.y));
    cxuiDestroy(w2);
    objRelease(&w2);
    objRelease(&inner);

    strDestroy(&log.text);
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&other);
    objRelease(&store);
    return 0;
}

// In a tree, the editor starts after the indent and the box; the source is told only of a changed
// text; closing a node above closes it quietly; cxuiGridEdit opens the nodes above; nodes added
// above move it down.
UI_TEST(treeedit)
{
    UiTestTree* src = uitesttreeCreate();
    CxUiNodeId a    = uitesttreeAdd(src, 0);
    CxUiNodeId a1   = uitesttreeAdd(src, a);
    uitesttreeAdd(src, 0);
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){
        .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .kind = CXUI_COL_Tree,
                                          .flags = CXUI_COL_Stretch | CXUI_COL_CheckBox | CXUI_COL_Editable } },
        .ncolumns = 1 });
    cxuiGridSetSource(g, src);
    CxUiWindow* w = startWindow(300, 200, CxUiWidget(objAcquire(g)));
    settle(w);
    cxuiGridExpandAll(g, 0);
    settle(w);
    EditLog log = { 0 };
    cxuiOnEdit(g, logEdit, stvar(ptr, &log));
    int32 rowH, headH, textH;
    gridHeights(g, &rowH, &headH, &textH);
#define ROW_TOP(r) (headH + (r) * rowH)
    cxuiFocus(g);

    // a1 is at depth 1: its text starts at 3 + 2 * 16, then the box and a gap of 3.
    cxuiGridSetFocusNode(g, a1);
    pressKey(g->_body, CXUI_KEY_F2, 0);
    int32 x = 3 + 2 * 16 + 13 + 3 - 3;
    if (!editorAt(g, x, ROW_TOP(1), 299 - x, rowH) || log.item != a1 || !strEq(log.text, _S"t2"))
        TEST_FAIL(1, _SL("F2 on a1: at ${int},${int} ${int}x${int}, for ${uint}"), RECT_ARGS(editorRect(g)),
                  stvar(uint64, log.item));
    editKey(g, CXUI_KEY_Enter, 0);
    if (src->edits != 0 || log.commits != 1)
        TEST_FAIL(1, _SL("an unchanged text was set"), stvNone);
    pressKey(g->_body, CXUI_KEY_F2, 0);
    cxuiTestType(cxuiGridEditor(g), _S"x");
    editKey(g, CXUI_KEY_Enter, 0);
    if (src->edits != 1 || src->editedNode != a1 || !strEq(src->editedText, _S"x"))
        TEST_FAIL(1, _SL("a changed text: ${int} edits of ${uint}"), stvar(int32, src->edits), stvar(uint64, src->editedNode));

    pressKey(g->_body, CXUI_KEY_F2, 0);
    int32 events = editEvents(&log);
    cxuiGridSetExpanded(g, a, false);
    if (g->_editFrame || editEvents(&log) != events)
        TEST_FAIL(1, _SL("closing the node above didn't close the editor quietly"), stvNone);
    if (!cxuiGridEdit(g, a1, 0) || !cxuiGridIsExpanded(g, a) || !editorAt(g, x, ROW_TOP(1), 299 - x, rowH))
        TEST_FAIL(1, _SL("cxuiGridEdit of a node in a closed one"), stvNone);
    events = editEvents(&log);
    cxuiGridCollapseAll(g, 0);
    if (g->_editFrame || editEvents(&log) != events)
        TEST_FAIL(1, _SL("collapsing all didn't close the editor quietly"), stvNone);
    cxuiGridEdit(g, a1, 0);

#if defined(_PLATFORM_MACOS)
    // Command-Up goes to the parent on the Mac.
    cxuiGridEndEdit(g, false);
    cxuiGridSetFocusNode(g, a1);
    pressKey(g->_body, CXUI_KEY_Up, CXUI_MOD_Meta);
    if (cxuiGridFocusNode(g) != a)
        TEST_FAIL(1, _SL("Command-Up went to ${uint}"), stvar(uint64, cxuiGridFocusNode(g)));
    cxuiGridEdit(g, a1, 0);
#endif
    // A new first node at the top moves a1 down a row.
    CxUiNodeId n = uitesttreeAdd(src, 0);
    saRemove(&src->order, saSize(src->order) - 1);
    saInsert(&src->order, 0, uint64, n);
    cxuiGridNodeInserted(g, n);
    settle(w);
    if (!editorAt(g, x, ROW_TOP(2), 299 - x, rowH))
        TEST_FAIL(1, _SL("a node added above: at ${int},${int} ${int}x${int}"), RECT_ARGS(editorRect(g)));
    editKey(g, CXUI_KEY_Escape, 0);
#undef ROW_TOP

    strDestroy(&log.text);
    cxuiDestroy(w);
    objRelease(&w);
    objRelease(&g);
    objRelease(&src);
    return 0;
}

static int test_cxui_grp_tree(void)
{
    TEST_CHAIN(test_cxui_treestore, test_cxui_treemap, test_cxui_treepaint, test_cxui_treeinput, test_cxui_treenotify,
               test_cxui_treesource, test_cxui_gridcheck, test_cxui_treecheck, test_cxui_gridedit, test_cxui_treeedit);
}

// ---- desktop ----

#define PLACE_ALL (CXUI_PLACE_Position | CXUI_PLACE_Size | CXUI_PLACE_State)

static void placeSet(CxUiWindow* w, CxUiRect r, uint32 state, flags_t valid)
{
    CxUiPlacement pl = { .normal = r, .state = state, .valid = valid };
    cxuiWindowSetPlacement(w, &pl);
}

// A placement set before realization is applied when the window is created, its state on the
// first show; later ones at once, the state of a hidden window again on its next show.
UI_TEST(placement)
{
    if (cxuiPlacementCaps() != PLACE_ALL)
        TEST_FAIL(1, _SL("null caps ${uint}"), stvar(uint32, cxuiPlacementCaps()));

    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 }, .sizing = CXUI_SIZE_Start });
    CxUiPlacement pl;
    CxUiTestWindowInfo info;
    if (cxuiWindowPlacement(w, &pl) || pl.valid != 0)
        TEST_FAIL(1, _SL("a new window had a placement (valid ${uint})"), stvar(uint32, pl.valid));

    placeSet(w, (CxUiRect){ 100, 120, 0, 0 }, CXUI_WINDOW_Normal, CXUI_PLACE_Position);
    placeSet(w, (CxUiRect){ 7, 7, 400, 300 }, CXUI_WINDOW_Maximized, CXUI_PLACE_Size | CXUI_PLACE_State);
    if (!cxuiWindowPlacement(w, &pl) || pl.valid != PLACE_ALL || !rectIs(pl.normal, 100, 120, 400, 300) ||
        pl.state != CXUI_WINDOW_Maximized || CxUiWidget(w)->peer)
        TEST_FAIL(1, _SL("stored placement ${int},${int} ${int}x${int} state ${uint} valid ${uint}"),
                  stvar(int32, pl.normal.x), stvar(int32, pl.normal.y), stvar(int32, pl.normal.w),
                  stvar(int32, pl.normal.h), stvar(uint32, pl.state), stvar(uint32, pl.valid));

    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestWindowInfo(w, &info);
    if (!pointIs(info.pos, 100, 120) || !sizeIs(info.size, 400, 300) || info.mode != CXUI_SHOW_Maximized)
        TEST_FAIL(1, _SL("first show: ${int},${int} ${int}x${int} mode ${uint}"), stvar(int32, info.pos.x),
                  stvar(int32, info.pos.y), stvar(int32, info.size.w), stvar(int32, info.size.h),
                  stvar(uint32, info.mode));
    CxUiSize sz;
    if (!cxuiWindowSize(w, &sz) || !sizeIs(sz, 400, 300))
        TEST_FAIL(1, _SL("the window's size is ${int}x${int}"), stvar(int32, sz.w), stvar(int32, sz.h));

    // visible: the state applies now; parts not given stay
    placeSet(w, (CxUiRect){ 0 }, CXUI_WINDOW_Normal, CXUI_PLACE_State);
    if (!cxuiWindowPlacement(w, &pl) || pl.state != CXUI_WINDOW_Normal || !rectIs(pl.normal, 100, 120, 400, 300))
        TEST_FAIL(1, _SL("state only: ${int},${int} ${int}x${int} state ${uint}"), stvar(int32, pl.normal.x),
                  stvar(int32, pl.normal.y), stvar(int32, pl.normal.w), stvar(int32, pl.normal.h), stvar(uint32, pl.state));
    placeSet(w, (CxUiRect){ 50, 60, 1, 1 }, CXUI_WINDOW_Normal, CXUI_PLACE_Position);
    cxuiTestWindowInfo(w, &info);
    if (!pointIs(info.pos, 50, 60) || !sizeIs(info.size, 400, 300))
        TEST_FAIL(1, _SL("position only: ${int},${int} ${int}x${int}"), stvar(int32, info.pos.x),
                  stvar(int32, info.pos.y), stvar(int32, info.size.w), stvar(int32, info.size.h));

    placeSet(w, (CxUiRect){ 9, 9, 350, 250 }, CXUI_WINDOW_Normal, CXUI_PLACE_Size);
    cxuiTestWindowInfo(w, &info);
    if (!pointIs(info.pos, 50, 60) || !sizeIs(info.size, 350, 250))
        TEST_FAIL(1, _SL("size only: ${int},${int} ${int}x${int}"), stvar(int32, info.pos.x), stvar(int32, info.pos.y),
                  stvar(int32, info.size.w), stvar(int32, info.size.h));
    placeSet(w, (CxUiRect){ 50, 60, 400, 300 }, CXUI_WINDOW_Normal, CXUI_PLACE_Size);

    // hidden: the state waits for the next show, which it overrides only from Normal or NoActivate
    cxuiWindowHide(w);
    placeSet(w, (CxUiRect){ 0 }, CXUI_WINDOW_Minimized, CXUI_PLACE_State);
    if (!cxuiWindowPlacement(w, &pl) || pl.state != CXUI_WINDOW_Minimized)
        TEST_FAIL(1, _SL("a hidden window's pending state read as ${uint}"), stvar(uint32, pl.state));
    cxuiWindowShow(w, CXUI_SHOW_NoActivate);
    cxuiTestWindowInfo(w, &info);
    uint32 pendingMode = info.mode;
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestWindowInfo(w, &info);
    if (pendingMode != CXUI_SHOW_Minimized || info.mode != CXUI_SHOW_Normal)
        TEST_FAIL(1, _SL("shows after a pending state: ${uint} then ${uint}"), stvar(uint32, pendingMode),
                  stvar(uint32, info.mode));
    cxuiWindowHide(w);
    placeSet(w, (CxUiRect){ 0 }, CXUI_WINDOW_Maximized, CXUI_PLACE_State);
    cxuiWindowShow(w, CXUI_SHOW_Minimized);
    cxuiTestWindowInfo(w, &info);
    if (info.mode != CXUI_SHOW_Minimized)
        TEST_FAIL(1, _SL("a pending state overrode an explicit show mode (${uint})"), stvar(uint32, info.mode));

    // a size that isn't one is ignored
    placeSet(w, (CxUiRect){ 10, 10, 0, 300 }, CXUI_WINDOW_Normal, CXUI_PLACE_Position | CXUI_PLACE_Size);
    cxuiTestWindowInfo(w, &info);
    if (!pointIs(info.pos, 10, 10) || !sizeIs(info.size, 400, 300))
        TEST_FAIL(1, _SL("a zero width gave ${int},${int} ${int}x${int}"), stvar(int32, info.pos.x),
                  stvar(int32, info.pos.y), stvar(int32, info.size.w), stvar(int32, info.size.h));

    cxuiDestroy(w);
    placeSet(w, (CxUiRect){ 1, 1, 50, 50 }, CXUI_WINDOW_Normal, PLACE_ALL);
    if (cxuiWindowPlacement(w, &pl))
        TEST_FAIL(1, _SL("a dead window had a placement"), stvNone);
    objRelease(&w);
    return 0;
}

// Applies a placement to a fresh window and reads back where it went.
static CxUiRect placeAt(int32 x, int32 y, int32 w, int32 h)
{
    CxUiWindow* win = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 100, 100 }, .sizing = CXUI_SIZE_Start,
                                                          .flags = CXUI_WINDOW_Shrink });
    cxuiWindowRealize(win);
    placeSet(win, (CxUiRect){ x, y, w, h }, CXUI_WINDOW_Normal, CXUI_PLACE_Position | CXUI_PLACE_Size);
    CxUiPlacement pl;
    cxuiWindowPlacement(win, &pl);
    cxuiDestroy(win);
    objRelease(&win);
    return pl.normal;
}

#define PLACE_CHECK(got, ex, ey, ew, eh, what)                                                                     \
    if (!rectIs(got, ex, ey, ew, eh))                                                                             \
        TEST_FAIL(1, _SL(what ": ${int},${int} ${int}x${int}"), stvar(int32, got.x), stvar(int32, got.y),           \
                  stvar(int32, got.w), stvar(int32, got.h));

// A rectangle out of reach moves onto the nearest monitor's work area, shrinking to fit.
UI_TEST(placementclamp)
{
    CxUiRect r = placeAt(200, 100, 400, 300);
    PLACE_CHECK(r, 200, 100, 400, 300, "on screen");
    r = placeAt(1800, 500, 400, 300);
    PLACE_CHECK(r, 1800, 500, 400, 300, "partly off the right, enough to grab");
    r = placeAt(1830, 500, 400, 300);
    PLACE_CHECK(r, 1520, 500, 400, 300, "too little on screen");
    r = placeAt(100, -16, 400, 300);
    PLACE_CHECK(r, 100, -16, 400, 300, "frame just above the top");
    r = placeAt(100, -17, 400, 300);
    PLACE_CHECK(r, 100, 0, 400, 300, "title bar above the top");
    r = placeAt(100, 1016, 400, 300);
    PLACE_CHECK(r, 100, 1016, 400, 300, "title bar just above the taskbar");
    r = placeAt(100, 1017, 400, 300);
    PLACE_CHECK(r, 100, 740, 400, 300, "title bar behind the taskbar");
    r = placeAt(5000, 5000, 400, 300);
    PLACE_CHECK(r, 1520, 740, 400, 300, "far off");
    r = placeAt(-3000, 10, 2500, 1200);
    PLACE_CHECK(r, 0, 0, 1920, 1040, "far off and too big");
    r = placeAt(-60, 10, 300, 100);
    PLACE_CHECK(r, -60, 10, 300, 100, "partly off the left");
    r = placeAt(-10, 10, 80, 100);
    PLACE_CHECK(r, 0, 10, 80, 100, "a narrow window must be all on screen");

    // a second monitor to the right, at twice the scale
    CxUiMonitor two[2] = {
        { .bounds = { 0, 0, 1920, 1080 }, .workArea = { 0, 0, 1920, 1040 }, .scale = 1.0f, .flags = CXUI_MON_Primary },
        { .bounds = { 1920, -200, 2560, 1440 }, .workArea = { 1920, -200, 2560, 1400 }, .scale = 2.0f },
    };
    cxuiTestSetMonitors(two, 2);
    r = placeAt(3000, -100, 800, 600);
    PLACE_CHECK(r, 3000, -100, 800, 600, "on the second monitor");
    r = placeAt(5000, 200, 800, 600);
    PLACE_CHECK(r, 3680, 200, 800, 600, "off the second monitor's right");
    r = placeAt(1920 + 2560 - 150, 200, 800, 600);
    PLACE_CHECK(r, 3680, 200, 800, 600, "150 pixels on a monitor at scale 2");
    r = placeAt(1920 + 2560 - 200, 200, 800, 600);
    PLACE_CHECK(r, 4280, 200, 800, 600, "200 pixels on a monitor at scale 2");
    r = placeAt(500, -180, 400, 300);
    PLACE_CHECK(r, 500, 0, 400, 300, "above the primary, beside the taller second");

    cxuiTestSetMonitors(NULL, 0);
    r = placeAt(-9000, -9000, 400, 300);
    PLACE_CHECK(r, -9000, -9000, 400, 300, "no monitors known");
    return 0;
}

// Show state events, with the fullscreen mode the window reports while each runs
typedef struct ShowStateLog {
    uint32 state[8];
    uint32 fs[8];
    int32 n;
} ShowStateLog;

static CxUiResult logShowState(stvlist* cvars, CxUiShowStateEvent* ev)
{
    ShowStateLog* log = stvlNextPtr(cvars);
    if (log->n < 8) {
        log->state[log->n] = ev->state;
        log->fs[log->n]    = cxuiWindowFullscreen(ev->base.window);
    }
    log->n++;
    return CXUI_Default;
}

#define FS_CHECK(w, log, wantFs, wantState, wantEvents, what)                                                      \
    do {                                                                                                           \
        CxUiTestWindowInfo fi_;                                                                                    \
        cxuiTestWindowInfo((w), &fi_);                                                                             \
        uint32 last_ = (log).n > 0 && (log).n <= 8 ? (log).state[(log).n - 1] : 99;                                \
        if (cxuiWindowFullscreen(w) != (wantFs) || fi_.fullscreen != (wantFs) || fi_.state != (wantState) ||        \
            (log).n != (wantEvents) || ((wantEvents) > 0 && last_ != (wantState)))                                 \
            TEST_FAIL(1, _SL(what ": mode ${uint}, peer ${uint}, state ${uint}, ${int} events, last ${uint}"),    \
                      stvar(uint32, cxuiWindowFullscreen(w)), stvar(uint32, fi_.fullscreen),                       \
                      stvar(uint32, fi_.state), stvar(int32, (log).n), stvar(uint32, last_));                      \
    } while (0)

// Fullscreen raises the show state event, and leaving it gives back the state before. A monitor
// can be chosen, but not one that isn't connected. Set on a hidden window, or by a placement, it
// applies when the window is shown; showing it normally, or a placement that isn't fullscreen,
// leaves it.
UI_TEST(fullscreen)
{
    CxUiMonitor mons[2] = {
        { .bounds   = { 0, 0, 1920, 1080 },
          .workArea = { 0, 0, 1920, 1040 },
          .scale    = 1.0f,
          .flags    = CXUI_MON_Primary | CXUI_MON_WorkAreaKnown | CXUI_MON_PositionKnown },
        { .bounds   = { 1920, 0, 2560, 1440 },
          .workArea = { 1920, 0, 2560, 1440 },
          .scale    = 1.0f,
          .flags    = CXUI_MON_WorkAreaKnown | CXUI_MON_PositionKnown },
    };
    cxuiTestSetMonitors(mons, 2);
    ShowStateLog log = { 0 };
    CxUiWindow* w    = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiOnShowState(w, logShowState, stvar(ptr, &log));
    cxuiWindowShow(w, CXUI_SHOW_Maximized);

    if (!cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_System, NULL))
        TEST_FAIL(1, _SL("System fullscreen failed"), stvNone);
    FS_CHECK(w, log, CXUI_FULLSCREEN_System, CXUI_WINDOW_Fullscreen, 1, "System");
    CxUiPlacement pl;
    if (!cxuiWindowPlacement(w, &pl) || pl.state != CXUI_WINDOW_Fullscreen || log.fs[0] != CXUI_FULLSCREEN_System)
        TEST_FAIL(1, _SL("placement state ${uint}, mode during the event ${uint}"), stvar(uint32, pl.state),
                  stvar(uint32, log.fs[0]));
    cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_None, NULL);
    FS_CHECK(w, log, CXUI_FULLSCREEN_None, CXUI_WINDOW_Maximized, 2, "left");

    cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_Monitor, &mons[1]);
    CxUiTestWindowInfo info;
    cxuiTestWindowInfo(w, &info);
    FS_CHECK(w, log, CXUI_FULLSCREEN_Monitor, CXUI_WINDOW_Fullscreen, 3, "Monitor on the second");
    if (!rectIs(info.fsMonitor, 1920, 0, 2560, 1440) || log.fs[2] != CXUI_FULLSCREEN_Monitor)
        TEST_FAIL(1, _SL("covers ${int},${int} ${int}x${int}; mode during the event ${uint}"), RECT_ARGS(info.fsMonitor),
                  stvar(uint32, log.fs[2]));
    CxUiMonitor gone = { .bounds = { -800, 0, 800, 600 } };
    if (cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_System, &gone))
        TEST_FAIL(1, _SL("fullscreen on a monitor that isn't connected"), stvNone);
    FS_CHECK(w, log, CXUI_FULLSCREEN_Monitor, CXUI_WINDOW_Fullscreen, 3, "a monitor not connected");

    // Leaving gives back Maximized, which showing normally then restores.
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestWindowInfo(w, &info);
    if (cxuiWindowFullscreen(w) != CXUI_FULLSCREEN_None || info.fullscreen != CXUI_FULLSCREEN_None ||
        info.state != CXUI_WINDOW_Normal || log.n != 4 || log.state[3] != CXUI_WINDOW_Maximized)
        TEST_FAIL(1, _SL("shown normally: mode ${uint}, peer ${uint}, state ${uint}, ${int} events"),
                  stvar(uint32, cxuiWindowFullscreen(w)), stvar(uint32, info.fullscreen), stvar(uint32, info.state),
                  stvar(int32, log.n));

    // Set while hidden, it waits for the next show.
    cxuiWindowHide(w);
    if (!cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_System, &mons[1]) ||
        cxuiWindowFullscreen(w) != CXUI_FULLSCREEN_System)
        TEST_FAIL(1, _SL("set while hidden: mode ${uint}"), stvar(uint32, cxuiWindowFullscreen(w)));
    cxuiTestWindowInfo(w, &info);
    if (info.fullscreen != CXUI_FULLSCREEN_None)
        TEST_FAIL(1, _SL("a hidden window went fullscreen"), stvNone);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    cxuiTestWindowInfo(w, &info);
    FS_CHECK(w, log, CXUI_FULLSCREEN_System, CXUI_WINDOW_Fullscreen, 5, "shown after setting it hidden");
    if (!rectIs(info.fsMonitor, 1920, 0, 2560, 1440))
        TEST_FAIL(1, _SL("set while hidden, covers ${int},${int} ${int}x${int}"), RECT_ARGS(info.fsMonitor));

    // A placement brings it to another window when shown, and a placement leaves it.
    cxuiWindowPlacement(w, &pl);
    ShowStateLog log2 = { 0 };
    CxUiWindow* w2    = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiOnShowState(w2, logShowState, stvar(ptr, &log2));
    cxuiWindowSetPlacement(w2, &pl);
    if (cxuiWindowFullscreen(w2) != CXUI_FULLSCREEN_None)
        TEST_FAIL(1, _SL("a placement made an unrealized window fullscreen"), stvNone);
    cxuiWindowShow(w2, CXUI_SHOW_Normal);
    FS_CHECK(w2, log2, CXUI_FULLSCREEN_System, CXUI_WINDOW_Fullscreen, 1, "placed fullscreen");
    placeSet(w2, (CxUiRect){ 0 }, CXUI_WINDOW_Normal, CXUI_PLACE_State);
    FS_CHECK(w2, log2, CXUI_FULLSCREEN_None, CXUI_WINDOW_Normal, 2, "placed normal");
    placeSet(w2, (CxUiRect){ 0 }, CXUI_WINDOW_Fullscreen, CXUI_PLACE_State);
    FS_CHECK(w2, log2, CXUI_FULLSCREEN_System, CXUI_WINDOW_Fullscreen, 3, "placed fullscreen while shown");

    cxuiDestroy(w);
    cxuiDestroy(w2);
    objRelease(&w);
    objRelease(&w2);
    return 0;
}

// Topmost applies to the peer whether set before or after realizing. Occlusion comes as a show
// state event that keeps the state, and is forgotten when the window is realized again.
UI_TEST(windowlevel)
{
    ShowStateLog log = { 0 };
    CxUiWindow* w    = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 300, 200 } });
    cxuiOnShowState(w, logShowState, stvar(ptr, &log));
    if (!cxuiWindowSetTopmost(w, true) || !cxuiWindowTopmost(w))
        TEST_FAIL(1, _SL("topmost before realizing"), stvNone);
    cxuiWindowShow(w, CXUI_SHOW_Maximized);
    bool top = false;
    if (!cxuiTestTopmost(w, &top) || !top)
        TEST_FAIL(1, _SL("the peer wasn't made topmost"), stvNone);
    cxuiWindowSetTopmost(w, false);
    cxuiTestTopmost(w, &top);
    if (top || cxuiWindowTopmost(w))
        TEST_FAIL(1, _SL("topmost wasn't turned off"), stvNone);

    int32 before = log.n;
    cxuiTestOccluded(w, true);
    cxuiTestOccluded(w, true);
    if (!cxuiWindowOccluded(w) || log.n != before + 1 || log.state[before] != CXUI_WINDOW_Maximized)
        TEST_FAIL(1, _SL("occluded: ${int} events, state ${uint}"), stvar(int32, log.n - before),
                  stvar(uint32, log.n > before ? log.state[before] : 99));
    cxuiTestOccluded(w, false);
    if (cxuiWindowOccluded(w) || log.n != before + 2)
        TEST_FAIL(1, _SL("shown again: ${int} events"), stvar(int32, log.n - before));

    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

UI_TEST(monitors)
{
    sa_CxUiMonitor mons = { 0 };
    int32 n = cxuiMonitors(&mons);
    if (n != 1 || saSize(mons) != 1 || !(mons.a[0].flags & CXUI_MON_Primary) ||
        !rectIs(mons.a[0].workArea, 0, 0, 1920, 1040))
        TEST_FAIL(1, _SL("default monitors: ${int}"), stvar(int32, n));

    CxUiMonitor two[2] = {
        { .bounds = { 0, 0, 800, 600 }, .workArea = { 0, 0, 800, 560 }, .scale = 1.0f, .flags = CXUI_MON_Primary },
        { .bounds = { 800, 0, 1024, 768 }, .workArea = { 800, 0, 1024, 768 }, .scale = 1.5f },
    };
    cxuiTestSetMonitors(two, 2);
    n = cxuiMonitors(&mons);
    if (n != 2 || saSize(mons) != 2 || mons.a[1].scale != 1.5f || !rectIs(mons.a[1].bounds, 800, 0, 1024, 768))
        TEST_FAIL(1, _SL("two monitors read back as ${int} (array ${int})"), stvar(int32, n), stvar(int32, saSize(mons)));
    saDestroy(&mons);
    return 0;
}

static bool monitorsSeen(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    (*(int32*)stvlAtPtr(cvars, 0))++;
    return true;
}

// Monitors carry an id, a name and a refresh rate, copied with the list; a monitor kept apart is
// copied and destroyed on its own. A change is announced, a window says which monitor it is on,
// and fullscreen finds its monitor by id after it moves, and not once it is gone.
UI_TEST(monitorinfo)
{
    CxUiMonitor two[2] = {
        { .bounds = { 0, 0, 800, 600 }, .workArea = { 0, 0, 800, 560 }, .scale = 1.0f, .flags = CXUI_MON_Primary,
          .refreshHz = 60.0f },
        { .bounds = { 800, 0, 1024, 768 }, .workArea = { 800, 0, 1024, 768 }, .scale = 1.5f, .refreshHz = 144.0f,
          .safeArea = { 0, 32, 0, 0 } },
    };
    strDup(&two[0].id, _S"conn-a");
    strDup(&two[0].name, _S"Left");
    strDup(&two[1].id, _S"conn-b");
    strDup(&two[1].name, _S"Right");
    cxuiTestSetMonitors(two, 2);

    sa_CxUiMonitor mons = { 0 };
    cxuiMonitors(&mons);
    CxUiMonitor kept = { 0 };
    if (saSize(mons) == 2)
        cxuiMonitorCopy(&kept, &mons.a[1]);
    bool listed = saSize(mons) == 2 && strEq(mons.a[0].id, _S"conn-a") && strEq(mons.a[1].name, _S"Right") &&
                  mons.a[1].refreshHz == 144.0f && mons.a[1].safeArea.top == 32;
    saDestroy(&mons);
    if (!listed || !strEq(kept.id, _S"conn-b") || !strEq(kept.name, _S"Right"))
        TEST_FAIL(1, _SL("monitors read back wrong; the kept copy is '${string}'"), stvar(strref, kept.id));

    int32 seen = 0;
    cchainAttach(&cxuiMonitorsChangedNotify, monitorsSeen, stvar(ptr, &seen));
    cxuiTestMonitorsChanged();
    cchainDetach(&cxuiMonitorsChangedNotify, monitorsSeen, 0);
    cxuiTestMonitorsChanged();
    if (seen != 1)
        TEST_FAIL(1, _SL("the change was seen ${int} times"), stvar(int32, seen));

    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .size = { 200, 100 } });
    if (cxuiWindowMonitor(w) != -1)
        TEST_FAIL(1, _SL("a window never shown is on monitor ${int}"), stvar(int32, cxuiWindowMonitor(w)));
    cxuiWindowSetPosition(w, 900, 100);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    if (cxuiWindowMonitor(w) != 1)
        TEST_FAIL(1, _SL("a window on the second monitor is on ${int}"), stvar(int32, cxuiWindowMonitor(w)));

    // The second monitor moves left of the first, keeping its id.
    two[1].bounds = (CxUiRect){ -1024, 0, 1024, 768 };
    cxuiTestSetMonitors(two, 2);
    CxUiTestWindowInfo info;
    bool moved = cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_Monitor, &kept) && cxuiTestWindowInfo(w, &info) &&
                 rectIs(info.fsMonitor, -1024, 0, 1024, 768);
    cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_None, NULL);
    if (!moved)
        TEST_FAIL(1, _SL("fullscreen didn't follow the moved monitor: ${int},${int} ${int}x${int}"),
                  RECT_ARGS(info.fsMonitor));
    cxuiTestSetMonitors(two, 1);
    if (cxuiWindowSetFullscreen(w, CXUI_FULLSCREEN_Monitor, &kept))
        TEST_FAIL(1, _SL("fullscreen on a monitor that was unplugged"), stvNone);

    cxuiMonitorDestroy(&kept);
    for (int32 i = 0; i < 2; i++)
        cxuiMonitorDestroy(&two[i]);
    cxuiDestroy(w);
    objRelease(&w);
    return 0;
}

UI_TEST(clipboard)
{
    string got = 0;
    strDup(&got, _SL("stale"));
    if (cxuiClipboardGetText(&got) || !strEmpty(got))
        TEST_FAIL(1, _SL("an empty clipboard gave '${string}'"), stvar(string, got));
    if (!cxuiClipboardSetText(_SL("one\ntwo")) || !cxuiClipboardGetText(&got) || !strEq(got, _SL("one\ntwo")))
        TEST_FAIL(1, _SL("the clipboard gave back '${string}'"), stvar(string, got));
    if (!cxuiClipboardSetText(NULL) || !cxuiClipboardGetText(&got) || !strEmpty(got))
        TEST_FAIL(1, _SL("empty text gave back '${string}'"), stvar(string, got));
    strDestroy(&got);
    return 0;
}

typedef struct NotifyRec {
    atomic(int32) calls;
    atomic(bool) onUI;
    atomic(int32) peersAtCall;
    CxUiWindow* win;   // destroyed by the notification, as a hosted library's would be
} NotifyRec;

static bool uiShutdownSeen(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    NotifyRec* rec = stvlAtPtr(cvars, 0);
    atomicFetchAdd(int32, &rec->calls, 1, AcqRel);
    atomicStore(bool, &rec->onUI, cxuiIsUIThread(), Release);
    atomicStore(int32, &rec->peersAtCall, cxuiTestPeersAlive(), Release);
    if (rec->win) {
        cxuiDestroy(rec->win);
        objRelease(&rec->win);
    }
    return true;
}

static bool uiHostedAttach(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    NotifyRec* rec = stvlAtPtr(cvars, 0);
    rec->win       = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Hosted" });
    cxuiWindowShow(rec->win, CXUI_SHOW_NoActivate);
    return true;
}

// The shutdown that stops the UI tells listeners first, on the UI thread with windows still
// there, then forgets them; one that only drops a count doesn't.
static int test_cxui_shutdownnotify(void)
{
    NotifyRec rec = { 0 };
    if (!cxuiStart(&nullStart) || !cxuiStart(NULL))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    // a library joining the running UI
    cchainAttach(&cxuiShutdownNotify, uiShutdownSeen, stvar(ptr, &rec));
    cxuiCallSync(closureCreate(uiHostedAttach, stvar(ptr, &rec)));

    cxuiShutdown();
    int32 early = atomicLoad(int32, &rec.calls, Acquire);
    cxuiShutdown();
    int32 calls = atomicLoad(int32, &rec.calls, Acquire);
    int32 peers = atomicLoad(int32, &rec.peersAtCall, Acquire);
    if (early != 0 || calls != 1)
        TEST_FAIL(1, _SL("notified ${int} times before the last shutdown, ${int} after"), stvar(int32, early),
                  stvar(int32, calls));
    if (!atomicLoad(bool, &rec.onUI, Acquire) || peers < 1 || rec.win)
        TEST_FAIL(1, _SL("notified off the UI thread, with ${int} peers, or the window survived"), stvar(int32, peers));
    if (cxuiTestPeersAlive() != 0 || cxuiTestPeersOffThread() != 0)
        TEST_FAIL(1, _SL("${int} peers left, ${int} destroyed off the UI thread"), stvar(int32, cxuiTestPeersAlive()),
                  stvar(int32, cxuiTestPeersOffThread()));

    // the next session's listeners are called, not the last one's
    NotifyRec rec2 = { 0 };
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("restart failed"), stvNone);
    bool attached = cchainAttach(&cxuiShutdownNotify, uiShutdownSeen, stvar(ptr, &rec2));
    cxuiShutdown();
    if (atomicLoad(int32, &rec.calls, Acquire) != 1 || !attached || atomicLoad(int32, &rec2.calls, Acquire) != 1)
        TEST_FAIL(1, _SL("second session: the first listener called ${int} times, the second attached ${int} and "
                         "called ${int} times"),
                  stvar(int32, atomicLoad(int32, &rec.calls, Acquire)), stvar(int32, attached),
                  stvar(int32, atomicLoad(int32, &rec2.calls, Acquire)));
    return 0;
}

typedef struct ShutdownRec {
    atomic(int32) winNotified;
    atomic(int32) childNotified;
    atomic(int32) keptNotified;
    CxUiWindow* kept;   // hides on close, and the program holds it
    CxUiBox* held;   // released in the window's destroyNotify, as a program's state would be
    Weak(CxUiWindow)* winWeak;
    Weak(CxUiBox)* childWeak;
} ShutdownRec;

static bool uiShutdownWinGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    ShutdownRec* rec = stvlAtPtr(cvars, 0);
    atomicFetchAdd(int32, &rec->winNotified, 1, AcqRel);
    objRelease(&rec->held);
    return true;
}

static bool uiShutdownChildGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    atomicFetchAdd(int32, &((ShutdownRec*)stvlAtPtr(cvars, 0))->childNotified, 1, AcqRel);
    return true;
}

static bool uiShutdownKeptGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    atomicFetchAdd(int32, &((ShutdownRec*)stvlAtPtr(cvars, 0))->keptNotified, 1, AcqRel);
    return true;
}

static bool uiShutdownBuild(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    ShutdownRec* rec = stvlAtPtr(cvars, 0);
    rec->kept        = cxuiWindowCreate(NULL);
    cchainAttach(&CxUiWidget(rec->kept)->destroyNotify, uiShutdownKeptGone, stvar(ptr, rec));
    cxuiWindowShow(rec->kept, CXUI_SHOW_Normal);
    CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .closeAction = CXUI_OnClose_Destroy });
    rec->held        = cxuiBoxCreateV();
    cxuiContainerAppend(w, rec->held);
    cchainAttach(&CxUiWidget(w)->destroyNotify, uiShutdownWinGone, stvar(ptr, rec));
    cchainAttach(&CxUiWidget(rec->held)->destroyNotify, uiShutdownChildGone, stvar(ptr, rec));
    rec->winWeak   = objGetWeak(CxUiWindow, w);
    rec->childWeak = objGetWeak(CxUiBox, rec->held);
    cxuiWindowShow(w, CXUI_SHOW_Normal);
    objRelease(&w);   // the shown window keeps itself alive
    return true;
}

// Ending the session destroys the windows that closing destroys: destroyNotify is called for each
// widget in them once, and state that holds a widget until then lets it go, so nothing is left. A
// window that hides on close outlives the session.
static int test_cxui_shutdowndestroy(void)
{
    ShutdownRec rec = { 0 };
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    cxuiCallSync(closureCreate(uiShutdownBuild, stvar(ptr, &rec)));
    cxuiShutdown();
    int32 wn = atomicLoad(int32, &rec.winNotified, Acquire), cn = atomicLoad(int32, &rec.childNotified, Acquire);
    CxUiWindow* w = objAcquireFromWeak(CxUiWindow, rec.winWeak);
    CxUiBox* b    = objAcquireFromWeak(CxUiBox, rec.childWeak);
    bool freed    = !w && !b;
    objRelease(&w);
    objRelease(&b);
    objDestroyWeak(&rec.winWeak);
    objDestroyWeak(&rec.childWeak);
    int32 kn  = atomicLoad(int32, &rec.keptNotified, Acquire);
    bool kept = rec.kept && !(CxUiWidget(rec.kept)->state & CXUI_WS_Dead);
    objRelease(&rec.kept);
    if (wn != 1 || cn != 1 || !freed || rec.held)
        TEST_FAIL(1, _SL("at shutdown: window notified ${int}, child ${int}, both freed ${int}"), stvar(int32, wn),
                  stvar(int32, cn), stvar(int32, freed));
    if (kn != 0 || !kept)
        TEST_FAIL(1, _SL("a window that hides on close was destroyed at shutdown (${int})"), stvar(int32, kn));
    if (cxuiTestPeersAlive() != 0)
        TEST_FAIL(1, _SL("${int} peers left"), stvar(int32, cxuiTestPeersAlive()));
    return 0;
}

static int test_cxui_grp_desktop(void)
{
    TEST_CHAIN(test_cxui_placement, test_cxui_placementclamp, test_cxui_fullscreen, test_cxui_windowlevel, test_cxui_monitors,
               test_cxui_monitorinfo, test_cxui_clipboard, test_cxui_shutdownnotify, test_cxui_shutdowndestroy);
}

testfunc cxuitest_funcs[] = {
    { "startstop", test_cxui_startstop },
    { "startcount", test_cxui_startcount },
    { "startonui", test_cxui_startonui },
    { "post", test_cxui_post },
    { "callsync", test_cxui_callsync },
    { "budget", test_cxui_budget },
    { "schedule", test_cxui_schedule },
    { "shutdowndrop", test_cxui_shutdowndrop },
    { "grp_runtime", test_cxui_grp_runtime },
    { "slots", test_cxui_slots },
    { "bubble", test_cxui_bubble },
    { "selfreplace", test_cxui_selfreplace },
    { "destroy", test_cxui_destroy },
    { "destroyindispatch", test_cxui_destroyindispatch },
    { "offthread", test_cxui_offthread },
    { "wrappers", test_cxui_wrappers },
    { "props", test_cxui_props },
    { "grp_widgets", test_cxui_grp_widgets },
    { "benchdispatch", test_cxui_benchdispatch },
    { "keyroundtrip", test_cxui_keyroundtrip },
    { "keynames", test_cxui_keynames },
    { "keymatch", test_cxui_keymatch },
    { "keylabel", test_cxui_keylabel },
    { "keylocks", test_cxui_keylocks },
    { "timerrepeat", test_cxui_timerrepeat },
    { "timerstartstop", test_cxui_timerstartstop },
    { "timershutdown", test_cxui_timershutdown },
    { "grp_keystimers", test_cxui_grp_keystimers },
    { "windowmodel", test_cxui_windowmodel },
    { "windowtheme", test_cxui_windowtheme },
    { "windowclose", test_cxui_windowclose },
    { "windowcontent", test_cxui_windowcontent },
    { "windowlifetime", test_cxui_windowlifetime },
    { "windowoffthread", test_cxui_windowoffthread },
    { "grp_windows", test_cxui_grp_windows },
    { "popup", test_cxui_popup },
    { "popupnested", test_cxui_popupnested },
    { "popuptimer", test_cxui_popuptimer },
    { "popupappmodal", test_cxui_popupappmodal },
    { "msgbox", test_cxui_msgbox },
    { "msgboxcustom", test_cxui_msgboxcustom },
    { "msgboxthread", test_cxui_msgboxthread },
    { "showmodal", test_cxui_showmodal },
    { "progressdialog", test_cxui_progressdialog },
    { "filedialog", test_cxui_filedialog },
    { "colordialog", test_cxui_colordialog },
    { "colorthread", test_cxui_colorthread },
    { "windowowner", test_cxui_windowowner },
    { "windownear", test_cxui_windownear },
    { "headless", test_cxui_headless },
    { "modalshutdown", test_cxui_modalshutdown },
    { "grp_modal", test_cxui_grp_modal },
    { "canvasbasic", test_cxui_canvasbasic },
    { "canvasdraw", test_cxui_canvasdraw },
    { "canvasdip", test_cxui_canvasdip },
    { "canvasdpi", test_cxui_canvasdpi },
    { "canvaspaintnow", test_cxui_canvaspaintnow },
    { "canvaslifetime", test_cxui_canvaslifetime },
    { "canvastext", test_cxui_canvastext },
    { "canvastextdip", test_cxui_canvastextdip },
    { "canvasimage", test_cxui_canvasimage },
    { "canvascellsdip", test_cxui_canvascellsdip },
    { "grp_canvas", test_cxui_grp_canvas },
    { "inputcapture", test_cxui_inputcapture },
    { "inputdouble", test_cxui_inputdouble },
    { "inputlost", test_cxui_inputlost },
    { "inputdrag", test_cxui_inputdrag },
    { "inputdip", test_cxui_inputdip },
    { "inputkeys", test_cxui_inputkeys },
    { "inputunmapped", test_cxui_inputunmapped },
    { "inputtext", test_cxui_inputtext },
    { "pointerdelta", test_cxui_pointerdelta },
    { "pointerlock", test_cxui_pointerlock },
    { "pointerlockdrag", test_cxui_pointerlockdrag },
    { "keycapture", test_cxui_keycapture },
    { "cursorimage", test_cxui_cursorimage },
    { "grp_input", test_cxui_grp_input },
    { "layoutbox", test_cxui_layoutbox },
    { "layoutexpand", test_cxui_layoutexpand },
    { "layoutsnap", test_cxui_layoutsnap },
    { "layoutnormalize", test_cxui_layoutnormalize },
    { "layouthidden", test_cxui_layouthidden },
    { "layoutchars", test_cxui_layoutchars },
    { "layoutcoalesce", test_cxui_layoutcoalesce },
    { "layoutflush", test_cxui_layoutflush },
    { "layoutenabled", test_cxui_layoutenabled },
    { "layoutrealize", test_cxui_layoutrealize },
    { "layoutwindow", test_cxui_layoutwindow },
    { "grp_layout", test_cxui_grp_layout },
    { "palettebasic", test_cxui_palettebasic },
    { "paletteinherit", test_cxui_paletteinherit },
    { "themesystem", test_cxui_themesystem },
    { "widgetfont", test_cxui_widgetfont },
    { "grp_theme", test_cxui_grp_theme },
    { "panelhost", test_cxui_panelhost },
    { "panelmoved", test_cxui_panelmoved },
    { "gridbox", test_cxui_gridbox },
    { "zboxlazy", test_cxui_zboxlazy },
    { "tabsbuild", test_cxui_tabsbuild },
    { "tabspick", test_cxui_tabspick },
    { "tabskeys", test_cxui_tabskeys },
    { "tabswrap", test_cxui_tabswrap },
    { "grp_tabs", test_cxui_grp_tabs },
    { "splitsash", test_cxui_splitsash },
    { "splitfit", test_cxui_splitfit },
    { "grp_containers", test_cxui_grp_containers },
    { "dlabel", test_cxui_dlabel },
    { "dbutton", test_cxui_dbutton },
    { "dtabs", test_cxui_dtabs },
    { "dscrollbar", test_cxui_dscrollbar },
    { "grp_drawn", test_cxui_grp_drawn },
    { "gridpaint", test_cxui_gridpaint },
    { "gridscroll", test_cxui_gridscroll },
    { "gridrowheight", test_cxui_gridrowheight },
    { "gridwheel", test_cxui_gridwheel },
    { "gridcolumns", test_cxui_gridcolumns },
    { "gridlayout", test_cxui_gridlayout },
    { "gridselect", test_cxui_gridselect },
    { "griddrag", test_cxui_griddrag },
    { "gridkeys", test_cxui_gridkeys },
    { "gridheader", test_cxui_gridheader },
    { "treestore", test_cxui_treestore },
    { "treemap", test_cxui_treemap },
    { "treepaint", test_cxui_treepaint },
    { "treeinput", test_cxui_treeinput },
    { "treenotify", test_cxui_treenotify },
    { "treesource", test_cxui_treesource },
    { "gridcheck", test_cxui_gridcheck },
    { "treecheck", test_cxui_treecheck },
    { "gridedit", test_cxui_gridedit },
    { "gridfit", test_cxui_gridfit },
    { "gridtail", test_cxui_gridtail },
    { "treeedit", test_cxui_treeedit },
    { "grp_grid", test_cxui_grp_grid },
    { "grp_tree", test_cxui_grp_tree },
    { "textbasic", test_cxui_textbasic },
    { "textnative", test_cxui_textnative },
    { "textcommit", test_cxui_textcommit },
    { "textfocus", test_cxui_textfocus },
    { "textframe", test_cxui_textframe },
    { "grp_text", test_cxui_grp_text },
    { "controlbasic", test_cxui_controlbasic },
    { "controlpress", test_cxui_controlpress },
    { "controldefault", test_cxui_controldefault },
    { "controlmnemonic", test_cxui_controlmnemonic },
    { "controlstart", test_cxui_controlstart },
    { "wrapcolumn", test_cxui_wrapcolumn },
    { "wrapshrink", test_cxui_wrapshrink },
    { "wrapcontainers", test_cxui_wrapcontainers },
    { "grp_controls", test_cxui_grp_controls },
    { "checkbasic", test_cxui_checkbasic },
    { "radiogroup", test_cxui_radiogroup },
    { "grp_checks", test_cxui_grp_checks },
    { "listbasic", test_cxui_listbasic },
    { "dropdownbasic", test_cxui_dropdownbasic },
    { "listmulti", test_cxui_listmulti },
    { "listupdate", test_cxui_listupdate },
    { "renderview", test_cxui_renderview },
    { "grp_lists", test_cxui_grp_lists },
    { "textarea", test_cxui_textarea },
    { "sliderbasic", test_cxui_sliderbasic },
    { "progressbasic", test_cxui_progressbasic },
    { "grp_sliders", test_cxui_grp_sliders },
    { "menubuild", test_cxui_menubuild },
    { "menuchoose", test_cxui_menuchoose },
    { "menuchange", test_cxui_menuchange },
    { "grp_menus", test_cxui_grp_menus },
    { "scrollbox", test_cxui_scrollbox },
    { "backgrounds", test_cxui_backgrounds },
    { "frametitle", test_cxui_frametitle },
    { "linkbasic", test_cxui_linkbasic },
    { "tips", test_cxui_tips },
    { "grp_frames", test_cxui_grp_frames },
    { "placement", test_cxui_placement },
    { "placementclamp", test_cxui_placementclamp },
    { "fullscreen", test_cxui_fullscreen },
    { "windowlevel", test_cxui_windowlevel },
    { "monitors", test_cxui_monitors },
    { "monitorinfo", test_cxui_monitorinfo },
    { "clipboard", test_cxui_clipboard },
    { "shutdownnotify", test_cxui_shutdownnotify },
    { "shutdowndestroy", test_cxui_shutdowndestroy },
    { "grp_desktop", test_cxui_grp_desktop },
    { 0, 0 }
};
