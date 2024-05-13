// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "mtask.h"
// ==================== Auto-generated section ends ======================
#include <cx/closure.h>
#include <cx/taskqueue.h>

bool MTask__cycle(_Inout_ MTask *self, _Out_opt_ int64 *progress)
{
    int64 maxprogress = 0;
    int nrunning = 0;
    bool complete = true;
    bool cantstart = false;
    bool ret = true;

    withMutex(&self->lock)
    {
        if(self->done)
            break;

        bool allstarted = true;
        for(int i = saSize(self->_pending) - 1; i >= 0; --i) {
            Task *task = self->_pending.a[i];

            int32 state = btaskState(task);
            maxprogress = max(maxprogress, task->lastprogress);
            switch(state) {
            case TASK_Created:
                complete = false;
                allstarted = false;
                break;
            case TASK_Running:
            case TASK_Waiting:
                nrunning++;
                // falltrhough
            case TASK_Deferred:
                complete = false;
                break;
            case TASK_Failed:
                self->failed = true;
                // fallthrough
            case TASK_Succeeded:
                saPush(&self->tasks, object, task);
                saRemove(&self->_pending, i);
                break;
            }
        }

        if(!allstarted && (self->limit == 0 || nrunning < self->limit)) {
            // need to start one or more tasks
            if(self->tq) {
                for(int i = 0, imax = saSize(self->_pending); i < imax && (self->limit == 0 || nrunning < self->limit); i++) {
                    Task *task = self->_pending.a[i];
                    int32 state = btaskState(task);
                    if(state == TASK_Created) {
                        if(tqAdd(self->tq, task)) {
                            nrunning++;
                        } else {
                            cantstart = true;
                        }
                    }
                }
            } else {
                cantstart = true;           // can't start anything because we don't have a queue!
            }
        }

        if(progress)
            *progress = maxprogress;

        if(cantstart)
            self->failed = true;

        self->done = complete || (nrunning == 0 && cantstart);

        ret = (atomicLoad(int32, &self->_ntasks, Acquire) == saSize(self->tasks));
    } // withMutex

    return ret;
}

static bool mtaskCallback(stvlist *cvars, stvlist *args)
{
    Weak(MTask) *wref = NULL;
    if(!stvlNext(cvars, weakref, &wref))
        return false;

    MTask *mtask = objAcquireFromWeak(MTask, wref);
    if(mtask) {
        atomicFetchAdd(int32, &mtask->_ntasks, 1, AcqRel);
        // Bump the mtask the front of the queue so the cycle can run.
        // Originally this code tried to run the cycle directly in the callback,
        // but even using optimistic locking it turned out to too much of a performance
        // bottleneck. The task advance / undefer method is much less overhead.
        taskAdvance(mtask);
        objRelease(&mtask);
    }

    return true;
}

bool MTask_run(_Inout_ MTask *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    int64 progress = 0;

    bool runagain = !MTask__cycle(self, &progress);
    if (self->done)
        return !self->failed;

    tcon->defer = true;
    tcon->defertime = runagain ? 0 : timeS(15);     // failsafe timer
    tcon->progress = (progress > self->lastprogress);
    return true;
}

void MTask_add(_Inout_ MTask *self, Task *task)
{
    // Use a weak reference to the MTask to avoid a reference loop
    Weak(MTask) *wref = objGetWeak(MTask, self);
    cchainAttach(&task->oncomplete, mtaskCallback, stvar(weakref, wref));
    objDestroyWeak(&wref);

    withMutex(&self->lock)
    {
        saPush(&self->_pending, object, task);
    }

    // make sure a cycle is run soon
    taskAdvance(self);
}

_objinit_guaranteed bool MTask_init(_Inout_ MTask *self)
{
    mutexInit(&self->lock);
    // Autogen begins -----
    saInit(&self->_pending, object, 1);
    saInit(&self->tasks, object, 1);
    return true;
    // Autogen ends -------
}

void MTask_destroy(_Inout_ MTask *self)
{
    // Autogen begins -----
    objRelease(&self->tq);
    saDestroy(&self->_pending);
    saDestroy(&self->tasks);
    // Autogen ends -------
}

_objfactory_guaranteed MTask *MTask_create()
{
    MTask *self;
    self = objInstCreate(MTask);
    objInstInit(self);
    return self;
}

_objfactory_guaranteed MTask *MTask_createWithQueue(TaskQueue *tq, int limit)
{
    MTask *self;
    self = objInstCreate(MTask);

    self->tq = objAcquire(tq);
    self->limit = limit;

    objInstInit(self);

    return self;
}

// Autogen begins -----
#include "mtask.auto.inc"
// Autogen ends -------
