#include "cxui_private.h"

_Use_decl_annotations_
void _cxuiWakeHook(TaskQueue* tq, void* ctx)
{
    unused_noeval(tq);
    const CxUiBackend* backend = ctx;
    if (!atomicExchange(bool, &_cxui.wakePending, true, AcqRel))
        backend->wake();
}

void _cxuiTick(void)
{
    int64 start = clockTimer();
    int64 next  = 0;
    atomicFetchAdd(uint64, &_cxui.ticks, 1, Relaxed);

    // An exchange rather than a store: a producer's own exchange then either sees false and wakes
    // the loop again, or comes first in the flag's order, which makes the task it just queued
    // visible to the tqTick below. A plain store could let both miss.
    atomicExchange(bool, &_cxui.wakePending, false, AcqRel);

    while (next == 0 && clockTimer() - start < _cxui.tickBudget)
        next = tqTick(_cxui.queue);

    // 0 when over budget: the backend lets pending input and painting go first, then ticks again.
    atomicStore(int64, &_cxui.nextWake, next, Relaxed);
    _cxui.backend->armTimer(next);
}

void cxuiFlush(void)
{
    static bool inFlush;   // UI thread only

    if (!cxuiIsUIThread()) {
        devAssertMsg(false, "cxuiFlush called off the UI thread");
        return;
    }
    if (inFlush)
        return;

    inFlush = true;
    _cxui.backend->flush();
    // Layout first, so it never waits behind other work for a later tick
    _cxuiWindowsLayoutQueued();
    _cxuiTick();
    inFlush = false;
}
