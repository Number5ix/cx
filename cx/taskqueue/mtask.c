// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "mtask.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include <cx/closure.h>
#include <cx/taskqueue.h>

bool MTask__cycle(_Inout_ MTask* self)
{
    bool ret = false;
    bool needcycle = false;

    if(mtaskAllDone(self))
        return false;

    // process the pending list
    for(int i = saSize(self->_pending) - 1; i >= 0; --i) {
        Task *task = self->_pending.a[i];

        int32 state = taskState(task);
        switch(state) {
        case TASK_Running:
        case TASK_Waiting:
        case TASK_Deferred:
            self->maxprogress = max(self->maxprogress, task->lastprogress);
            break;
        case TASK_Failed:
            self->failed = true;
            // fallthrough
        case TASK_Succeeded:
            self->maxprogress = clockTimer();           // a task completing definitely counts as progress
            saPush(&self->finished, object, task);
            saRemove(&self->_pending, i);
            break;
        default:
            devFatalError("Unexpected state in mtask pending list");
        }
    }

    _Analysis_assume_(self->_new.a != NULL);

    withMutex(&self->_newlock)
    {
        // try to start some tasks if we can
        while((self->limit == 0 || saSize(self->_pending) < self->limit) && self->_newcursor < saSize(self->_new)) {
            Task *task = self->_new.a[self->_newcursor++];
            int32 state = taskState(task);
            if(state == TASK_Created) {
                // task that hasn't been started yet, run it
                if(self->tq && tqAdd(self->tq, task)) {
                    self->maxprogress = clockTimer();
                    saPush(&self->_pending, object, task);
                } else {
                    // failed to start task or we don't have a queue
                    saPush(&self->finished, object, task);
                    self->failed = true;
                }
            } else {
                // This task was already in progress when added; move
                // it to pending and immediately run another cycle
                saPush(&self->_pending, object, task);
                needcycle = true;
            }
        }

        if(self->_newcursor == saSize(self->_new)) {
            saClear(&self->_new);
            self->_newcursor = 0;
        }

        int32 nfinished = atomicLoad(int32, &self->_nfinished, Acquire);
        ret = (nfinished > saSize(self->finished));        // return true if there's more to process

        if(nfinished < saSize(self->finished)) {
            // We have more finished tasks than the counter says we should.
            // This can happen because of the race described in mtask_Add.
            // Just try to correct the count.
            atomicCompareExchange(int32, weak, &self->_nfinished, &nfinished, saSize(self->finished), AcqRel, Relaxed);
        }

        if(saSize(self->_pending) == 0 && saSize(self->_new) == 0)
            atomicStore(bool, &self->alldone, true, Release);
    }

    return ret || needcycle;
}

static bool mtaskCallback(stvlist *cvars, stvlist *args)
{
    Weak(MTask) *wref = NULL;
    if(!stvlNext(cvars, weakref, &wref))
        return false;

    MTask *mtask = objAcquireFromWeak(MTask, wref);
    if(mtask) {
        atomicFetchAdd(int32, &mtask->_nfinished, 1, AcqRel);
        // Bump the mtask the front of the queue so the cycle can run.
        // Originally this code tried to run the cycle directly in the callback,
        // but even using optimistic locking it turned out to too much of a performance
        // bottleneck. The task advance / undefer method is much less overhead.
        taskAdvance(mtask);
        objRelease(&mtask);
    }

    return true;
}

bool MTask_run(_Inout_ MTask* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon)
{
    bool runagain = MTask__cycle(self);
    if (mtaskAllDone(self))
        return !self->failed;

    // 15 second failsafe timer
    return taskRetDefer(tcon, runagain ? 0 : timeS(15), self->maxprogress > self->lastprogress);
}

void MTask_add(_Inout_ MTask* self, _In_ Task* task)
{
    if(taskIsComplete(task)) {
        // task is already done, bump counter for when it gets processed during the cycle
        atomicFetchAdd(int32, &self->_nfinished, 1, AcqRel);
    } else {
        // Otherwise the callback will increment the counter and trigger a cycle.
        // There's a race here; if a task completes after we check above but before
        // attaching the callback, it won't happen. However, we always run a cycle after
        // adding a task, making the race harmless.

        // Use a weak reference to the MTask to avoid a reference loop
        Weak(MTask) *wref = objGetWeak(MTask, self);
        cchainAttach(&task->oncomplete, mtaskCallback, stvar(weakref, wref));
        objDestroyWeak(&wref);
    }

    withMutex(&self->_newlock) {
        saPush(&self->_new, object, task);
    }
    atomicStore(bool, &self->alldone, false, Release);

    // make sure a cycle is run soon
    taskAdvance(self);
}

_objinit_guaranteed bool MTask_init(_Inout_ MTask* self)
{
    mutexInit(&self->_newlock);
    // Autogen begins -----
    saInit(&self->_new, object, 1);
    saInit(&self->_pending, object, 1);
    saInit(&self->finished, object, 1);
    return true;
    // Autogen ends -------
}

void MTask_destroy(_Inout_ MTask* self)
{
    mutexDestroy(&self->_newlock);
    // Autogen begins -----
    objRelease(&self->tq);
    saDestroy(&self->_new);
    saDestroy(&self->_pending);
    saDestroy(&self->finished);
    // Autogen ends -------
}

_objfactory_guaranteed MTask* MTask_create()
{
    MTask *self;
    self = objInstCreate(MTask);

    objInstInit(self);

    return self;
}

_objfactory_guaranteed MTask* MTask_createWithQueue(_In_ TaskQueue* tq, int limit)
{
    MTask *self;
    self = objInstCreate(MTask);

    self->tq = objAcquire(tq);
    self->limit = limit;

    objInstInit(self);

    return self;
}

extern bool Task_reset(_Inout_ Task* self);   // parent
#define parent_reset() Task_reset((Task*)(self))
bool MTask_reset(_Inout_ MTask* self)
{
    saClear(&self->_pending);
    saClear(&self->finished);
    withMutex(&self->_newlock)
    {
        saClear(&self->_new);
        self->_newcursor = 0;
    }

    atomicStore(int32, &self->_nfinished, 0, Relaxed);
    atomicStore(bool, &self->alldone, false, Relaxed);
    self->failed = false;

    return true;
}

// Autogen begins -----
#include "mtask.auto.inc"
// Autogen ends -------
