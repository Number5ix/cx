// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "worker.h"
// ==================== Auto-generated section ends ======================
#include <cx/format.h>
#include "taskqueue_private.h"

#define WORKER_FAILSAFE_TIMEOUT timeS(5)

static int tqWorkerThread(Thread *thr)
{
    TaskQueue *tq;
    TaskQueueWorker *self;

    if(!stvlNext(&thr->args, ptr, &tq) ||
       !stvlNext(&thr->args, ptr, &self))
        return 1;

    // this is not allowed to change while the queue is running
    TQUICallback ui = tq->tqconfig.ui;

    while(thrLoop(thr) && tq->state == TQState_Running) {
        BasicTask *btask;
        bool hadtasks = false;

        while ((btask = (BasicTask*)prqPop(&tq->runq))) {
            bool doneq = true;
            hadtasks = true;

            // Do UI stuff first if this is a UI task queue, to keep things responsive
            if(ui)
                ui(tq);

            // IMPLEMENTATION NOTE: At this point we become the owner of the btask pointer
            // and it does not exist anywhere else in the queue -- MOSTLY. We are not allowed
            // to destroy the object because the manager/monitor thread may be accessing it
            // concurrently through our curtask pointer.

            // See if this is a full-fledged Task or just a BasicTask
            Task *task = objDynCast(btask, Task);

            if(task)
                task->last = clockTimer();

            // run it
            atomicStore(int32, &btask->state, TASK_Running, Release);
            atomicStore(ptr, &self->curtask, btask, Release);
            TaskControl tcon = { 0 };
            bool success = btaskRun(btask, tq, &tcon);      // <-- do the thing
            atomicStore(ptr, &self->curtask, NULL, Release);

            if(task && tcon.defer) {
                // Task is being deferred, so it goes back into the queue.
                atomicStore(int32, &btask->state, TASK_Waiting, Release);
                // Record if forward progress was made
                if(tcon.progress)
                    task->lastprogress = clockTimer();

                if(tcon.defertime > 0) {
                    // will go into defer list to be run at a certain time
                    task->nextrun = clockTimer() + tcon.defertime;
                } else if(tcon.progress) {
                    // defertime is 0 and we DID make progress, add it straight to the run queue so
                    // we'll get right back to it.
                    prqPush(&tq->runq, btask);
                    // Note: We intentionally don't signal the worker event because we're
                    // about to loop anyway, no need to wake up another thread. If something
                    // else is added to the queue in the interim, that action will wake up a
                    // different worker.
                    doneq = false;
                } else {
                    // defertime is 0 and we did NOT make progress. In this case, it does still go
                    // to the defer list, but nextrun is set to 0 which tells the manager to
                    // release it only once some other task finishes.
                    task->nextrun = 0;
                }
            } else {
                // tasks fail if they do not succeed
                atomicStore(int32, &btask->state, success ? TASK_Succeeded : TASK_Failed, Release);
            }

            if(tcon.notifyev) {
                eventSignal(tcon.notifyev);
            }

            // In all cases except one the task needs to be moved to the done queue for the manager
            // to either requeue later or destroy.
            if(doneq) {
                prqPush(&tq->doneq, btask);
                eventSignal(&tq->manager->notify);
            }
        }

        // if we did some work, signal one of the other threads to wake up; this helps kickstart the
        // queue faster after an idle period
        if (hadtasks)
            eventSignal(&tq->workev);

        // Queue is empty; wait for some work to do unless we are a UI thread
        // and there are STILL more unprocessed UI events.
        if((!ui || ui(tq)) && !hadtasks)
            eventWaitTimeout(&tq->workev, WORKER_FAILSAFE_TIMEOUT);
    }

    // the manager might be waiting on us to exit
    self->shutdown = true;
    eventSignal(&tq->manager->notify);

    return 0;
}
void TaskQueueWorker_destroy(_Inout_ TaskQueueWorker *self)
{
    // Autogen begins -----
    objRelease(&self->thr);
    // Autogen ends -------
}

_objfactory_guaranteed TaskQueueWorker *TaskQueueWorker_create(int32 num)
{
    TaskQueueWorker *self;
    self = objInstCreate(TaskQueueWorker);

    self->num = num;

    objInstInit(self);

    return self;
}

bool TaskQueueWorker_start(_Inout_ TaskQueueWorker *self, TaskQueue *tq)
{
    string thrname = 0;
    strFormat(&thrname, _S"${string} Worker #${int}", stvar(string, tq->name), stvar(int32, self->num));

    // may need to create a UI thread for the worker if there's a callback
    if (tq->tqconfig.ui)
        self->thr = thrCreateUI(tqWorkerThread, thrname, stvar(ptr, tq), stvar(ptr, self));
    else
        self->thr = thrCreate(tqWorkerThread, thrname, stvar(ptr, tq), stvar(ptr, self));

    strDestroy(&thrname);
    return self->thr;
}

// Autogen begins -----
#include "worker.auto.inc"
// Autogen ends -------
