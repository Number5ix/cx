#include "taskqueue_private.h"

// absolute maximum time between manager cycles
#define MAX_MANAGER_INTERVAL (timeS(5))

static bool addWorker(TaskQueue *tq)
{
    TaskQueueWorker *worker = taskqueueCreateWorker(tq, atomicLoad(int32, &tq->nworkers, Relaxed) + 1);
    bool ret = taskqueueworker_startThread(worker, tq);
    if(ret && worker->thr) {
        saPush(&tq->workers, object, worker);
        atomicFetchAdd(int32, &tq->nworkers, 1, AcqRel);
    }
    objRelease(&worker);
    return ret;
}

static bool removeWorker(Thread *self, TaskQueue *tq)
{
    int retries = 0;
    int nrm = saSize(tq->workers) - 1;
    TaskQueueWorker *worker = NULL;
    if(saExtract(&tq->workers, nrm, object, &worker) && worker) {
        atomicFetchAdd(int32, &tq->nworkers, -1, AcqRel);
        thrRequestExit(worker->thr);

        // Unfortunately we need to wake up ALL the workers to ensure that this one
        // sees the signal to shut down... WaitForMultipleObjects is great but not all
        // OSes support it, and even Windows can't wait on more than one futex at a time.
        eventSignalAll(&tq->workev);

        while(!worker->shutdown && retries < 10) {
            ++retries;          // eventually give up and hope it shuts down
            eventWaitTimeout(&self->notify, MAX_MANAGER_INTERVAL);
        }
        return true;
    }

    return false;
}

static bool managerStartup(TaskQueue *tq, Event *startev)
{
    // reset event states in case this queue was previously shut down
    eventReset(&tq->workev);
    eventReset(&tq->shutdownev);

    tq->state = TQState_Running;

    // start initial workers
    bool ret = true;
    for(int i = 0; i < tq->tqconfig.wInitial; i++) {
        ret &= addWorker(tq);
    }

    if (!ret) {
        // failed to create some workers!
        // lock the event so no workers get blocked on it
        tq->state = TQState_Stopping;
        eventSignalLock(&tq->workev);
        
        // shut down any workers we did manage to start
        for(int i = 0; i < atomicLoad(int32, &tq->nworkers, Relaxed); i++) {
            if (tq->workers.a[i]->thr)
            thrShutdown(tq->workers.a[i]->thr);
        }
        saClear(&tq->workers);      // will release threads

        tq->state = TQState_Shutdown;
    }

    // either way, notify the caller to re-check the state
    eventSignal(startev);

    return ret;
}

static void reapPtr(void *ptr)
{
    // all pointers in any of the queues or defer list are actually BasicTask
    // objects that we're avoiding refcount overhead on - and prqueue doesn't
    // directly support anyway

    BasicTask *btask = (BasicTask *)ptr;
    objRelease(&btask);
}

static void managerRun(Thread *thr, TaskQueue *tq)
{
    TQMonitorState mstate = { .lastrun = clockTimer() };
    TaskQueueConfig *conf = &tq->tqconfig;

    // timers and variables for queue sizing
    uint32 lastcount = 0;
    int64 lastOp = 0;
    int64 idleStart = clockTimer();         // queue starts out idle

    // main manager loop
    while(tq->state == TQState_Running) {
        int64 now = clockTimer();
        int64 waittime = MAX_MANAGER_INTERVAL;
        bool ctasks = false;                // did we complete any tasks this run?

        // go through doneq
        BasicTask *btask;
        while((btask = (BasicTask*)prqPop(&tq->doneq))) {
            int32 state = btaskState(btask);
            if(state == TASK_Succeeded || state == TASK_Failed) {
                // task is complete, remove it from the queue and possibly destroy it
                reapPtr(btask);
                ctasks = true;
            } else {
                // task is deferred, move it to the defer list
                // sanity check
                devAssert(state == TASK_Deferred);
                Task *task = objDynCast(btask, Task);
                if(devVerify(task)) {
                    if(task->lastprogress == 0)
                        task->lastprogress = now;
                    task->last = now;
                    saPush(&tq->deferred, ptr, btask);
                } else {
                    reapPtr(btask);     // shouldn't be possible, but don't leak it
                }
            }
        }

        int dcount = 0;
        // move advance queue entries out of defer list before we process it
        Task *task;
        while((task = (Task *)prqPop(&tq->advanceq))) {
            if(saFindRemove(&tq->deferred, ptr, task)) {
                atomicStore(int32, &task->state, TASK_Waiting, Release);
                task->last = now;
                prqPush(&tq->runq, task);
                ++dcount;
            } else {
                // might have just been put in doneq? Let it run ASAP if that's the case
                task->nextrun = now;
                waittime = 0;
            }
        }

        // process deferred list
        for(int i = 0, imax = saSize(tq->deferred); i < imax; ++i) {
            task = (Task *)tq->deferred.a[i];

            // if this task is deferred with a time of 0 and we completed some
            // tasks this run, or if it's a deferred task that's scheduled to
            // run now, put it back in the queue
            if((task->nextrun == 0 && ctasks) ||
               task->nextrun <= now) {
                atomicStore(int32, &task->state, TASK_Waiting, Release);
                task->last = now;
                prqPush(&tq->runq, task);
                saRemove(&tq->deferred, i);
                --i; --imax; ++dcount;
            }

            // the list is sorted, so once we hit one with a time in the future, just stop
            if(task->nextrun > now) {
                // but make sure we don't wait too long
                waittime = min(waittime, task->nextrun - now);
                break;
            }
        }

        // wake up enough workers for anything we put back in the run queue
        if(dcount > 0)
            eventSignalMany(&tq->workev, dcount);

        // see if we need to run the monitor
        if(conf->mInterval > 0 && now - mstate.lastrun > conf->mInterval) {
            _tqMonitorRun(tq, now, &mstate);
            mstate.lastrun = now;
        } else if (conf->mInterval > 0) {
            waittime = min(waittime, conf->mInterval - (now - mstate.lastrun));
        }

        // finally see if we need to grow or shrink the thread pool
        uint32 qcount = prqCount(&tq->runq);
        int32 nworkers = atomicLoad(int32, &tq->nworkers, Relaxed);         // relaxed is fine, only ever changed from this thread
        int32 targetw = nworkers;
        int32 qperw = qcount / nworkers;

        // LOAD FACTOR
        if(qcount > 0) {
            if(qperw <= conf->loadFactor) {
                // under load factor, use the busy target
                targetw = conf->wBusy;
            } else {
                // over load factor, scale out to max proprtional to how much over
                targetw = conf->wBusy + (conf->wMax - conf->wBusy) * (qperw - conf->loadFactor) / conf->loadFactor;;
            }
        }
        
        // IDLE QUEUE CHECK
        if(lastcount != 0 && qcount == 0) {
            // queue just became idle
            idleStart = now;
        } else if(lastcount == 0 && qcount == 0 && idleStart > 0) {
            // queue is still idle
            if(now - idleStart > conf->tIdle)
                targetw = conf->wIdle;          // start moving towards idle workers
        } else if(qcount > 0) {
            // queue is no longer idle
            idleStart = 0;
        }

        targetw = clamp(targetw, conf->wIdle, conf->wMax);
        lastcount = qcount;

        // add/remove workers as necessary
        if(targetw > nworkers && now - lastOp > conf->tRampUp) {
            addWorker(tq);
            lastOp = now;
        } else if(targetw < nworkers && now - lastOp > conf->tRampDown) {
            removeWorker(thr, tq);
            lastOp = now;
        }

        eventWaitTimeout(&thr->notify, clamphigh(waittime, MAX_MANAGER_INTERVAL));
    }
}

int tqManagerThread(Thread *self)
{
    TaskQueue *tq;

    do {
        // This block exists to scope startev and guarantee it can't be
        // used after startup, as it often lives in the stack of the
        // function that created this thread and will become invalid
        // once it's signaled.
        Event *startev;

        if(!stvlNext(&self->args, ptr, &tq) ||
           !stvlNext(&self->args, ptr, &startev))
            return 1;

        if(!managerStartup(tq, startev)) {
            objRelease(&tq);
            return 1;
        }
    } while(0);

    managerRun(self, tq);

    // The only legitimate way we can get here is if tq->state moved to TQState_Stopping.
    // Either way the queue needs to be shut down.
    tq->state = TQState_Stopping;
    eventSignalLock(&tq->workev);

    // wait for all workers to shut down
    for(;;) {
        bool running = false;
        int32 nworkers = atomicLoad(int32, &tq->nworkers, Relaxed);
        for(int i = 0; i < nworkers; i++) {
            running |= !tq->workers.a[i]->shutdown;
        }

        if(!running)
            break;

        eventWaitTimeout(&self->notify, MAX_MANAGER_INTERVAL);
    }
    saClear(&tq->workers);
    atomicStore(int32, &tq->nworkers, 0, Release);
    thrRelease(&tq->manager);       // we're about to exit

    // clear out anything left in the queues or defer list
    void *ptr;
    while((ptr = prqPop(&tq->runq)))
        reapPtr(ptr);
    while((ptr = prqPop(&tq->doneq)))
        reapPtr(ptr);
    for(int i = 0; i < saSize(tq->deferred); i++)
        reapPtr(tq->deferred.a[i]);
    saClear(&tq->deferred);

    tq->state = TQState_Shutdown;
    eventSignalLock(&tq->shutdownev);

    // finally release the thread-owned reference to the queue
    objRelease(&tq);

    return 0;
}
