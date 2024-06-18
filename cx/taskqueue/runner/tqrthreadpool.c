// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqrthreadpool.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/taskqueue/taskqueue_private.h"

_objfactory_guaranteed TQThreadPoolRunner*
TQThreadPoolRunner_create(_In_ TaskQueueThreadPoolConfig* config)
{
    TQThreadPoolRunner* self;
    self = objInstCreate(TQThreadPoolRunner);

    self->conf = *config;

    objInstInit(self);

    return self;
}

_objinit_guaranteed bool TQThreadPoolRunner_init(_Inout_ TQThreadPoolRunner* self)
{
    eventInit(&self->workershutdown);
    rwlockInit(&self->workerlock);
    // Autogen begins -----
    saInit(&self->workers, object, 1);
    return true;
    // Autogen ends -------
}

_objfactory_guaranteed TQThreadWorker*
TQThreadPoolRunner_createWorker(_Inout_ TQThreadPoolRunner* self, int32 num)
{
    return tqthreadworkerCreate(num);
}

bool TQThreadPoolRunner_addWorker(_Inout_ TQThreadPoolRunner* self)
{
    if (!self->tq)
        return false;

    rwlockAcquireWrite(&self->workerlock);
    TQThreadWorker* worker = tqthreadpoolrunnerCreateWorker(self, saSize(self->workers) + 1);
    bool ret               = tqthreadworkerStartThread(worker, self->tq);
    if (ret) {
        saPush(&self->workers, object, worker);
    }
    rwlockReleaseWrite(&self->workerlock);
    objRelease(&worker);
    return ret;
}

bool TQThreadPoolRunner_removeWorker(_Inout_ TQThreadPoolRunner* self)
{
    if (!self->tq)
        return false;

    rwlockAcquireWrite(&self->workerlock);
    int retries            = 0;
    int nrm                = saSize(self->workers) - 1;
    TQThreadWorker* worker = NULL;
    if (saExtract(&self->workers, nrm, object, &worker) && worker) {
        rwlockReleaseWrite(&self->workerlock);
        thrRequestExit(worker->thr);

        // Unfortunately we need to wake up ALL the workers to ensure that this one
        // sees the signal to shut down... WaitForMultipleObjects is great but not all
        // OSes support it, and even Windows can't wait on more than one futex at a time.
        eventSignalAll(&self->tq->workev);

        while (!worker->shutdown && retries < 10) {
            ++retries;   // eventually give up and hope it shuts down
            eventWaitTimeout(&self->workershutdown, timeS(1));
        }
        objRelease(&worker);
        return true;
    }
    rwlockReleaseWrite(&self->workerlock);
    return false;
}

extern bool TQRunner_start(_Inout_ TQRunner* self, _In_ TaskQueue* tq);   // parent
#define parent_start(tq) TQRunner_start((TQRunner*)(self), tq)
bool TQThreadPoolRunner_start(_Inout_ TQThreadPoolRunner* self, _In_ TaskQueue* tq)
{
    if (!parent_start(tq))
        return false;

    eventReset(&self->workershutdown);

    // start initial workers
    bool ret = true;
    for (int i = 0; i < self->conf.wInitial; i++) {
        ret &= tqthreadpoolrunnerAddWorker(self);
    }

    if (!ret) {
        // failed to create some workers!
        // lock the event so no workers get blocked on it
        eventSignalLock(&tq->workev);

        rwlockAcquireWrite(&self->workerlock);
        // shut down any workers we did manage to start
        for (int i = 0; i < saSize(self->workers); i++) {
            thrShutdown(self->workers.a[i]->thr);
        }
        saClear(&self->workers);   // will release threads
        rwlockReleaseWrite(&self->workerlock);

        return false;
    }

    return true;
}

int64 TQThreadPoolRunner_tick(_Inout_ TQThreadPoolRunner* self)
{
    // do nothing, thread pool runner doesn't need to be ticked as worker threads tick themselves
    // independently
    return timeForever;
}

extern bool TQRunner_stop(_Inout_ TQRunner* self);   // parent
#define parent_stop() TQRunner_stop((TQRunner*)(self))
bool TQThreadPoolRunner_stop(_Inout_ TQThreadPoolRunner* self)
{
    // workers shut themselves down when the queue is in a stopping state, just wait for them
    int64 timeStart = clockTimer();

    while (clockTimer() < timeStart + timeS(15)) {
        bool running = true;
        withReadLock (&self->workerlock) {
            for (int i = saSize(self->workers) - 1; i >= 0; --i) {
                running &= !self->workers.a[i]->shutdown;
            }
        }
        if (!running)
            break;
        eventWaitTimeout(&self->workershutdown, timeS(1));
    }
    withWriteLock (&self->workerlock) {
        saClear(&self->workers);
    }

    return parent_stop();
}

void TQThreadPoolRunner_destroy(_Inout_ TQThreadPoolRunner* self)
{
    rwlockDestroy(&self->workerlock);
    eventDestroy(&self->workershutdown);
    // Autogen begins -----
    saDestroy(&self->workers);
    // Autogen ends -------
}

// Autogen begins -----
#include "tqrthreadpool.auto.inc"
// Autogen ends -------
