// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqcomplex.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/taskqueue/taskqueue_private.h"

_objfactory_guaranteed ComplexTaskQueue*
ComplexTaskQueue_create(_In_opt_ strref name, uint32 flags, _In_ TQRunner* runner,
                        _In_ TQManager* manager, _In_opt_ TQMonitor* monitor)
{
    ComplexTaskQueue* self;
    self = objInstCreate(ComplexTaskQueue);

    strDup(&self->name, name);
    self->flags   = flags;
    self->runner  = objAcquire(runner);
    self->manager = objAcquire(manager);
    self->monitor = objAcquire(monitor);
    objInstInit(self);

    return self;
}

_objinit_guaranteed bool ComplexTaskQueue_init(_Inout_ ComplexTaskQueue* self)
{
    prqInitDynamic(&self->advanceq, 4, 16, 0, PRQ_Grow_100, PRQ_Grow_100);
    self->advanceq.shrinkms = 5;

    // Autogen begins -----
    saInit(&self->scheduled, object, 1, SA_Ref | SA_Sorted);
    htInit(&self->deferred, object, none, 16, HT_RefKeys);
    return true;
    // Autogen ends -------
}

static bool CTQPushBackCommon(_Inout_ ComplexTaskQueue* self, _In_ ComplexTask* task, uint32 state,
                              int64 delay)
{
    if (atomicLoad(uint32, &self->state, Relaxed) != TQState_Running)
        return false;

    // try to move it to desired state
    if (!ctask_setState(task, state))
        return false;

    // add a reference count which becomes owned by the queue
    objAcquire(task);
    task->last    = clockTimer();   // record when it was put in queue
    task->nextrun = (state == TASK_Scheduled && delay != -1) ? task->last + delay : 0;

    // remember that it's associated with this queue for when it's advanced
    objDestroyWeak(&task->lastq);
    task->lastq = objGetWeak(ComplexTaskQueue, self);

    // By putting it in the done queue, the manager thread will pick it up and stick it in the
    // scheduled / deferred list, which we can't safely access from other threads.
    if (!prqPush(&self->doneq, task)) {
        ctask_setState(task, TASK_Failed);
        objRelease(&task);
        return false;
    }

    // Notify the manager to do something with it
    tqmanagerNotify(self->manager);
    return true;
}

bool ComplexTaskQueue_schedule(_Inout_ ComplexTaskQueue* self, _In_ ComplexTask* task, int64 delay)
{
    return CTQPushBackCommon(self, task, TASK_Scheduled, delay);
}

bool ComplexTaskQueue_defer(_Inout_ ComplexTaskQueue* self, _In_ ComplexTask* task)
{
    return CTQPushBackCommon(self, task, TASK_Deferred, 0);
}

extern bool TaskQueue__processDone(_Inout_ TaskQueue* self);   // parent
#define parent__processDone() TaskQueue__processDone((TaskQueue*)(self))
bool ComplexTaskQueue__processDone(_Inout_ ComplexTaskQueue* self)
{
    int64 now = clockTimer();
    bool ret  = false;

    BasicTask* btask;
    while ((btask = (BasicTask*)prqPop(&self->doneq))) {
        uint32 state = btaskState(btask);
        switch (state) {
        case TASK_Succeeded:
        case TASK_Failed:
            // task is complete, remove it from the queue and possibly destroy it
            objRelease(&btask);
            ret = true;
            break;
        case TASK_Scheduled:
        case TASK_Deferred: {
            ComplexTask* ctask = objDynCast(btask, ComplexTask);

            if (!devVerify(ctask)) {
                btask_setState(btask, TASK_Failed);
                objRelease(&btask);
                break;
            }

            if (ctask->lastprogress == 0)
                ctask->lastprogress = now;
            ctask->last = now;

            if (state == TASK_Scheduled) {
                saPush(&self->scheduled, object, btask);
            } else {
                htInsert(&self->deferred, object, btask, none, NULL);
            }

            break;
        }
        default:
            // task shouldn't be in the doneq in some other state
            btask_setState(btask, TASK_Failed);
            objRelease(&btask);
        }
    }

    // return true if some tasks were completed, false otherwise
    return ret;
}

extern bool TaskQueue__needProcessExtra(_Inout_ TaskQueue* self);   // parent
#define parent__needProcessExtra() TaskQueue__needProcessExtra((TaskQueue*)(self))
bool ComplexTaskQueue__needProcessExtra(_Inout_ ComplexTaskQueue* self)
{
    // While the manager is always appropriately signaled when entries are added to the advance
    // queue, we need to make sure that the manager is ALWAYS ticked if there is anything scheduled
    // to run on a delay. This is needed for the in-worker manager to know that it can't let the
    // thread sleep too long or it will miss the deadline.
    return saSize(self->scheduled) > 0;
}

#define parent__processExtra(taskscompleted) \
    TaskQueue__processExtra((TaskQueue*)(self), taskscompleted)
extern int64 TaskQueue__processExtra(_Inout_ TaskQueue* self, bool taskscompleted);   // parent
#define parent__processExtra(taskscompleted) \
    TaskQueue__processExtra((TaskQueue*)(self), taskscompleted)
int64 ComplexTaskQueue__processExtra(_Inout_ ComplexTaskQueue* self, bool taskscompleted)
{
    int64 waittime = timeForever;
    int64 now      = clockTimer();

    int dcount = 0;
    // process advance queue
    ComplexTask* ctask;
    while ((ctask = (ComplexTask*)prqPop(&self->advanceq))) {
        uint32 state = taskState(ctask);
        switch (state) {
        case TASK_Scheduled:
            ctask_setState(ctask, TASK_Waiting);
            if (saFindRemove(&self->scheduled, object, ctask)) {
                ctask->last = now;
                prqPush(&self->runq, ctask);
                ++dcount;
            } else {
                // might have just been put in doneq? Let it run ASAP if that's the case
                ctask->nextrun = 0;
                waittime       = 0;
            }
            break;
        case TASK_Deferred:
            // if the task was deferred because it's waiting on dependencies, make sure they're all
            // satisfied before releasing it
            if (ctaskCheckDeps(ctask)) {
                ctask_setState(ctask, TASK_Waiting);
                if (htRemove(&self->deferred, object, ctask)) {
                    ctask->last = now;
                    prqPush(&self->runq, ctask);
                    ++dcount;
                } else {
                    // might have just been put in doneq? Let it run ASAP if that's the case
                    waittime = 0;
                }
            }
            break;
        default:
            ctask_setState(ctask, TASK_Failed);
            objRelease(&ctask);
        }
    }

    // process scheduled list
    for (int i = 0, imax = saSize(self->scheduled); i < imax; ++i) {
        ctask         = self->scheduled.a[i];
        int64 nextrun = ctask->nextrun;

        // nextrun of -1 is special and means the task is scheduled to run whenever any other task
        // completes
        if ((nextrun != -1 || taskscompleted) && nextrun <= now) {
            // time for this task to run, so move it back to runq
            ctask_setState(ctask, TASK_Waiting);
            ctask->last = now;
            saRemove(&self->scheduled, i);
            prqPush(&self->runq, ctask);
            --i;
            --imax;
            ++dcount;
        }

        // the list is sorted, so once we hit one with a time in the future, just stop
        if (nextrun > now) {
            // but make sure we don't wait too long
            waittime = min(waittime, nextrun - now);
            break;
        }
    }

    // if we put anything back in, signal the event so that worker threads can pick it up
    if (dcount > 0)
        eventSignalMany(&self->workev, dcount);

    return waittime;
}

bool ComplexTaskQueue_advance(_Inout_ ComplexTaskQueue* self, _In_ ComplexTask* task)
{
    uint32 state = taskState(task);
    if (atomicLoad(uint32, &self->state, Relaxed) != TQState_Running ||
        !(state == TASK_Deferred || state == TASK_Scheduled))
        return false;

    // Put it in the advance queue, manager will safely move it out of the underlying structure,
    // either in a dedicated thread or with a lock held
    if (!prqPush(&self->advanceq, task))
        return false;

    // Signal the manager to move it
    tqmanagerNotify(self->manager);
    return true;
}

void ComplexTaskQueue_destroy(_Inout_ ComplexTaskQueue* self)
{
    ctaskqueue_clear(self);

    prqDestroy(&self->advanceq);

    // Autogen begins -----
    saDestroy(&self->scheduled);
    htDestroy(&self->deferred);
    // Autogen ends -------
}

extern void TaskQueue__clear(_Inout_ TaskQueue* self);   // parent
#define parent__clear() TaskQueue__clear((TaskQueue*)(self))
void ComplexTaskQueue__clear(_Inout_ ComplexTaskQueue* self)
{
    parent__clear();

    ComplexTask* ctask;
    while ((ctask = prqPop(&self->advanceq))) {
        objRelease(&ctask);
    }

    for (int i = saSize(self->scheduled) - 1; i >= 0; --i) {
        objRelease(&self->scheduled.a[i]);
    }
    saClear(&self->scheduled);

    foreach (hashtable, it, self->deferred) {
        ctask = (ComplexTask*)htiKey(it, object);
        objRelease(&ctask);
    }
    htClear(&self->deferred);
}

extern bool TaskQueue__runTask(_Inout_ TaskQueue* self, _Inout_ BasicTask** pbtask,
                               _In_ TQWorker* worker);   // parent
#define parent__runTask(pbtask, worker) TaskQueue__runTask((TaskQueue*)(self), pbtask, worker)
bool ComplexTaskQueue__runTask(_Inout_ ComplexTaskQueue* self, _Inout_ BasicTask** pbtask,
                               _In_ TQWorker* worker)
{
    // context: This is called from worker threads. The task pointed to by pbtask has been removed
    // from the run queue and exists only in the parameter; so we MUST do something with it.
    int64 now      = clockTimer();
    bool completed = false;

    if (!btask_setState(*pbtask, TASK_Running)) {
        btask_setState(*pbtask, TASK_Failed);
        objRelease(pbtask);
        return false;
    }

    TaskControl tcon = { 0 };
    uint32 tresult   = btaskRun(*pbtask, self, worker, &tcon);

    ComplexTask* ctask = objDynCast(*pbtask, ComplexTask);
    Task* task         = Task(ctask);

    if (ctask &&
        (tresult == TASK_Result_Schedule_Progress || tresult == TASK_Result_Defer_Progress)) {
        ctask->lastprogress = now;
    }

    // Failsafe check, if this isn't a complex task it's only allowed to return success or failure.
    // Vastly simplifies the logic below.
    if (!ctask && !(tresult == TASK_Result_Success || tresult == TASK_Result_Failure)) {
        tresult = TASK_Result_Failure;
    }

    switch (tresult) {
    case TASK_Result_Success:
        btask_setState(*pbtask, TASK_Succeeded);
        completed = true;
        break;
    case TASK_Result_Schedule:
        ctask->nextrun = (tcon.delay == -1) ? -1 : now + tcon.delay;
        btask_setState(*pbtask, TASK_Scheduled);
        break;
    case TASK_Result_Schedule_Progress:
        if (tcon.delay == 0) {
            // special case, if we made progress AND the desired wait time is 0, put it directly
            // back in the run queue
            ctask->last = now;
            btask_setState(*pbtask, TASK_Waiting);
            prqPush(&self->runq, ctask);
            *pbtask = NULL;   // task no longer belongs to worker
            return true;      // return early so it does NOT get put in doneq
        } else {
            ctask->nextrun = (tcon.delay == -1) ? -1 : now + tcon.delay;
            btask_setState(*pbtask, TASK_Scheduled);
        }
    case TASK_Result_Defer:
    case TASK_Result_Defer_Progress:
        btask_setState(*pbtask, TASK_Deferred);
        break;
    case TASK_Result_Failure:
    default:
        // catch-all in case of unexpected return status
        btask_setState(*pbtask, TASK_Failed);
        completed = true;
    }

    if (completed) {
        // If this isn't a ComplexTask it might still be a Task
        if (!task)
            task = objDynCast(*pbtask, Task);
        if (task && task->oncomplete) {
            cchainCall(&task->oncomplete, stvar(object, task));
            cchainDestroy(&task->oncomplete);   // oncomplete callbacks are one-shots
        }

        if (tcon.notifyev) {
            eventSignal(tcon.notifyev);
        }
    } else if (ctask) {
        ctask->last = now;
        // remember last queue so this task can be advanced if necessary
        objDestroyWeak(&ctask->lastq);
        ctask->lastq = objGetWeak(ComplexTaskQueue, self);
    }

    // In all other cases the task needs to be moved to the done queue for the manager to clean up.
    prqPush(&self->doneq, *pbtask);
    *pbtask = NULL;   // task no longer belongs to worker
    tqmanagerNotify(self->manager);

    return completed;
}

extern bool TaskQueue_add(_Inout_ TaskQueue* self, _In_ BasicTask* btask);   // parent
#define parent_add(btask) TaskQueue_add((TaskQueue*)(self), btask)
bool ComplexTaskQueue_add(_Inout_ ComplexTaskQueue* self, _In_ BasicTask* btask)
{
    if (atomicLoad(uint32, &self->state, Relaxed) != TQState_Running)
        return false;

    ComplexTask* ctask = objDynCast(btask, ComplexTask);
    if (ctask) {
        // if it's a complex task it might have dependencies
        if (!ctaskCheckDeps(ctask)) {
            // deps aren't satisifed, it needs to go in defer hash instead of run queue
            if (!btask_setState(btask, TASK_Deferred))
                return false;

            // add a reference and put it in the done queue, so it gets moved to the defer hash
            objAcquire(btask);
            ctask->last = clockTimer();
            if (!prqPush(&self->doneq, btask)) {
                btask_setState(btask, TASK_Failed);
                objRelease(&btask);
                return false;
            }
            // notify manager to go pick it up
            tqmanagerNotify(self->manager);
            return true;
        }
    }

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

// Autogen begins -----
#include "tqcomplex.auto.inc"
// Autogen ends -------