// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqmthreadpool.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/taskqueue/taskqueue_private.h"

void TQThreadPoolManager_updatePoolSize(_Inout_ TQThreadPoolManager* self)
{
    if (!(self->tq && self->runner))
        return;

    TaskQueueThreadPoolConfig* conf = &self->runner->conf;

    // see if we need to grow or shrink the thread pool
    uint32 qcount = prqCount(&self->tq->runq);
    rwlockAcquireRead(&self->runner->workerlock);
    int32 nworkers = saSize(self->runner->workers);
    rwlockReleaseRead(&self->runner->workerlock);
    int32 targetw = nworkers;
    int32 qperw   = (nworkers > 0) ? qcount / nworkers : 100;
    int64 now     = clockTimer();

    // LOAD FACTOR
    if (qcount > 0) {
        if (qperw <= conf->loadFactor) {
            // under load factor, use the busy target
            targetw = conf->wBusy;
        } else {
            // over load factor, scale out to max proprtional to how much over
            targetw = conf->wBusy +
                (conf->wMax - conf->wBusy) * (qperw - conf->loadFactor) / conf->loadFactor;
        }
    }

    // IDLE QUEUE CHECK
    if (self->lastcount != 0 && qcount == 0) {
        // queue just became idle
        self->idlestart = now;
    } else if (self->lastcount == 0 && qcount == 0 && self->idlestart > 0) {
        // queue is still idle
        if (now - self->idlestart > conf->tIdle)
            targetw = conf->wIdle;   // start moving towards idle workers
    } else if (qcount > 0) {
        // queue is no longer idle
        self->idlestart = 0;
    }

    targetw         = clamp(targetw, conf->wIdle, conf->wMax);
    self->lastcount = qcount;

    // add/remove workers as necessary
    if (targetw > nworkers && now - max(self->lastop, self->idlestart) > conf->tRampUp) {
        tqthreadpoolrunnerAddWorker(self->runner);
        self->lastop = now;
    } else if (targetw < nworkers && now - self->lastop > conf->tRampDown) {
        tqthreadpoolrunnerRemoveWorker(self->runner);
        self->lastop = now;
    }
}

void TQThreadPoolManager_destroy(_Inout_ TQThreadPoolManager* self)
{
    // Autogen begins -----
    objRelease(&self->runner);
    // Autogen ends -------
}

extern bool TQManager_start(_Inout_ TQManager* self, _In_ TaskQueue* tq);   // parent
#define parent_start(tq) TQManager_start((TQManager*)(self), tq)
bool TQThreadPoolManager_start(_Inout_ TQThreadPoolManager* self, _In_ TaskQueue* tq)
{
    if (!parent_start(tq))
        return false;

    // thread pool manager must be paired with its correspoinding runner (or dervied class)
    self->runner = objAcquire(objDynCast(TQThreadPoolRunner, tq->runner));

    return self->runner != NULL;
}

extern int64 TQManager_tick(_Inout_ TQManager* self);   // parent
#define parent_tick() TQManager_tick((TQManager*)(self))
int64 TQThreadPoolManager_tick(_Inout_ TQThreadPoolManager* self)
{
    int64 waittime = timeForever;
    // Run monitor if we have one
    if (self->tq->monitor)
        waittime = tqmonitorTick(self->tq->monitor);

    // Process done queue and deferred/scheduled tasks
    bool taskscompleted = taskqueue_processDone(self->tq);

    int64 extrawait = taskqueue_processExtra(self->tq, taskscompleted);
    waittime        = min(waittime, extrawait);

    // Adjust thread pool size
    tqthreadpoolmanagerUpdatePoolSize(self);

    return waittime;
}

extern bool TQManager_stop(_Inout_ TQManager* self);   // parent
#define parent_stop() TQManager_stop((TQManager*)(self))
bool TQThreadPoolManager_stop(_Inout_ TQThreadPoolManager* self)
{
    objRelease(&self->runner);

    return parent_stop();
}

// Autogen begins -----
#include "tqmthreadpool.auto.inc"
// Autogen ends -------
