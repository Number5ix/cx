// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqueue.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/taskqueue/taskqueue_private.h"

_objfactory_guaranteed TaskQueue*
TaskQueue_create(_In_opt_ strref name, uint32 flags, _In_ TQRunner* runner, _In_ TQManager* manager,
                 _In_opt_ TQMonitor* monitor)
{
    TaskQueue* self;
    self = objInstCreate(TaskQueue);

    strDup(&self->name, name);
    self->flags   = flags;
    self->runner  = objAcquire(runner);
    self->manager = objAcquire(manager);
    self->monitor = objAcquire(monitor);

    objInstInit(self);

    return self;
}

_objinit_guaranteed bool TaskQueue_init(_Inout_ TaskQueue* self)
{
    eventInit(&self->workev);

    // if this is a thread pool based queue, size the queues based on the desired number of worker
    // threads
    TQThreadPoolRunner* tprun = objDynCast(self->runner, TQThreadPoolRunner);
    int qsz                   = tprun ? tprun->conf.wBusy * 4 : 8;

    prqInitDynamic(&self->runq, qsz, qsz * 2, 0, PRQ_Grow_100, PRQ_Grow_25);
    prqInitDynamic(&self->doneq, qsz, qsz * 2, 0, PRQ_Grow_100, PRQ_Grow_25);
    self->runq.shrinkms  = 5;
    self->doneq.shrinkms = 5;
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

bool TaskQueue_add(_Inout_ TaskQueue* self, _In_ BasicTask* btask)
{
    if (atomicLoad(uint32, &self->state, Relaxed) != TQState_Running)
        return false;

    // try to move it to waiting state
    if (!btask_setState(btask, TASK_Waiting))
        return false;

    // add a reference count which becomes owned by the queue
    objAcquire(btask);

    Task* task = objDynCast(btask, Task);
    if (task)
        task->last = clockTimer();   // record when it was put in run queue

    if (!prqPush(&self->runq, btask)) {
        btask_setState(btask, TASK_Failed);
        objRelease(&btask);
        return false;
    }
    // Signal the runner to pick it up
    eventSignal(&self->workev);
    return true;
}

bool TaskQueue__processDone(_Inout_ TaskQueue* self)
{
    bool ret = false;

    BasicTask* btask;
    while ((btask = (BasicTask*)prqPop(&self->doneq))) {
        uint32 state = btaskState(btask);
        devAssert(state == TASK_Succeeded || state == TASK_Failed);

        // task is complete, remove it from the queue and possibly destroy it
        objRelease(&btask);
        ret = true;
    }

    // return true if some tasks were completed, false otherwise
    return ret;
}

bool TaskQueue__needProcessExtra(_Inout_ TaskQueue* self)
{
    return false;
}

int64 TaskQueue__processExtra(_Inout_ TaskQueue* self, bool taskscompleted)
{
    // nothing to do because this queue doesn't support complex tasks
    return timeForever;
}

bool TaskQueue_start(_Inout_ TaskQueue* self)
{
    uint32 state = atomicLoad(uint32, &self->state, Acquire);
    if (!(state == TQState_Init || state == TQState_Shutdown))
        return false;

    atomicStore(uint32, &self->state, TQState_Starting, Release);

    // reset event states in case this queue was previously shut down
    eventReset(&self->workev);

    if (!tqrunnerStart(self->runner, self))
        return false;

    if (!tqmanagerStart(self->manager, self)) {
        tqrunnerStop(self->runner);
        atomicStore(uint32, &self->state, TQState_Shutdown, Release);
        return false;
    }

    if (self->monitor) {
        if (!tqmonitorStart(self->monitor, self)) {
            tqmanagerStop(self->manager);
            tqrunnerStop(self->runner);
            atomicStore(uint32, &self->state, TQState_Shutdown, Release);
            return false;
        }
    }

    atomicStore(uint32, &self->state, TQState_Running, Release);

    return true;
}

bool TaskQueue_stop(_Inout_ TaskQueue* self, int64 timeout)
{
    uint32 state = atomicLoad(uint32, &self->state, Acquire);
    if (!(state == TQState_Running))
        return false;

    atomicStore(uint32, &self->state, TQState_Stopping, Release);
    eventSignalLock(&self->workev);

    if (self->monitor)
        tqmonitorStop(self->monitor);
    tqmanagerStop(self->manager);
    tqrunnerStop(self->runner);

    // delete anything left in the queue
    taskqueue_clear(self);

    return true;
}

int64 TaskQueue_tick(_Inout_ TaskQueue* self)
{
    if (self->flags & TQ_Manual)
        return tqrunnerTick(self->runner);

    return 0;
}

bool TaskQueue__runTask(_Inout_ TaskQueue* self, _Inout_ BasicTask** pbtask, _In_ TQWorker* worker)
{
    // context: This is called from worker threads. The task pointed to by pbtask has been removed
    // from the run queue and exists only in the parameter; so we MUST do something with it.

    if (!btask_setState(*pbtask, TASK_Running)) {
        btask_setState(*pbtask, TASK_Failed);
        objRelease(pbtask);
        return false;
    }

    TaskControl tcon = { 0 };
    uint32 tresult   = btaskRun(*pbtask, self, worker, &tcon);

    switch (tresult) {
    case TASK_Result_Success:
        btask_setState(*pbtask, TASK_Succeeded);
        break;
    default:
        // catch-all in case somebody tried to put a complex task with a different result code in
        // this queue
        btask_setState(*pbtask, TASK_Failed);
    }

    // See if this is a full-fledged Task we need to do completion callbacks on
    Task* task = objDynCast(*pbtask, Task);
    if (task && task->oncomplete) {
        cchainCall(&task->oncomplete, stvar(object, task));
        cchainDestroy(&task->oncomplete);   // oncomplete callbacks are one-shots
    }

    if (tcon.notifyev) {
        eventSignal(tcon.notifyev);
    }

    // In all cases the task needs to be moved to the done queue for the manager to clean up.
    prqPush(&self->doneq, *pbtask);
    *pbtask = NULL;   // task no longer belongs to worker
    tqmanagerNotify(self->manager);

    return true;
}

void TaskQueue__clear(_Inout_ TaskQueue* self)
{
    uint32 state = atomicLoad(uint32, &self->state, Acquire);
    devAssert(state == TQState_Init || state == TQState_Stopping || state == TQState_Shutdown);

    BasicTask* btask;
    while ((btask = prqPop(&self->runq))) {
        objRelease(&btask);
    }
    while ((btask = prqPop(&self->doneq))) {
        objRelease(&btask);
    }
}

void TaskQueue_destroy(_Inout_ TaskQueue* self)
{
    taskqueue_clear(self);
    eventDestroy(&self->workev);
    prqDestroy(&self->runq);
    prqDestroy(&self->doneq);
    // Autogen begins -----
    strDestroy(&self->name);
    objRelease(&self->runner);
    objRelease(&self->manager);
    objRelease(&self->monitor);
    // Autogen ends -------
}

// Autogen begins -----
#include "tqueue.auto.inc"
// Autogen ends -------