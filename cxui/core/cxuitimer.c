// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuitimer.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

_objfactory_guaranteed CxUiTimer* CxUiTimer_createWith(int64 period, uint32 flags, closure fn)
{
    CxUiTimer* self;
    self = objInstCreate(CxUiTimer);

    self->period = period;
    self->flags  = flags;
    self->_fn    = fn;

    objInstInit(self);
    return self;
}

bool CxUiTimer_start(_In_ CxUiTimer* self)
{
    if (!self)
        return false;
    devAssertMsg(cxuiIsUIThread(), "cxuiTimerStart called off the UI thread");
    if (CxUiTimer_isRunning(self))
        return true;

    TaskQueue* queue = cxuiQueue();
    if (!queue)
        return false;

    self->_gen++;
    self->_running         = true;
    self->_session         = _cxui.session;
    CxUiTimerTask* task    = _cxuiTimerTaskCreate(self, self->_gen);
    bool ok                = tqSchedule(queue, task, self->period);
    self->_running         = ok;
    objRelease(&task);
    tqRelease(&queue);
    return ok;
}

void CxUiTimer_stop(_In_ CxUiTimer* self)
{
    if (!self)
        return;
    devAssertMsg(cxuiIsUIThread(), "cxuiTimerStop called off the UI thread");
    if (self->_running) {
        self->_gen++;
        self->_running = false;
    }
}

bool CxUiTimer_isRunning(_In_ CxUiTimer* self)
{
    return self && self->_running && self->_session == _cxui.session && cxuiIsRunning();
}

void CxUiTimer_destroy(_In_ CxUiTimer* self)
{
    // Autogen begins -----
    closureDestroy(&self->_fn);
    // Autogen ends -------
}

_objfactory_guaranteed CxUiTimerTask* CxUiTimerTask_create(_In_ CxUiTimer* timer, uint32 gen)
{
    CxUiTimerTask* self;
    self = objInstCreate(CxUiTimerTask);

    self->timer = objGetWeak(CxUiTimer, timer);
    self->gen   = gen;

    objInstInit(self);
    return self;
}

uint32 CxUiTimerTask_run(_In_ CxUiTimerTask* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    CxUiTimer* timer = objAcquireFromWeak(CxUiTimer, self->timer);
    if (!timer || timer->_gen != self->gen) {
        objRelease(&timer);
        return TASK_Result_Success;
    }

    if (timer->flags & CXUI_Timer_OneShot) {
        timer->_running = false;
        timer->_gen++;
    }

    CxUiTimerEvent ev = { .timer = timer };
    closureCallAs(CxUiTimerFn, timer->_fn, &ev);

    // The handler may have stopped the timer, or stopped and started it again with a new task.
    // The next run is timed from now, so a slow handler never has calls queue up behind it.
    bool again   = timer->_gen == self->gen;
    tcon->delay  = timer->period;
    objRelease(&timer);
    return again ? TASK_Result_Schedule : TASK_Result_Success;
}

void CxUiTimerTask_destroy(_In_ CxUiTimerTask* self)
{
    // Autogen begins -----
    objDestroyWeak(&self->timer);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuitimer.auto.inc"
// clang-format on
// Autogen ends -------
