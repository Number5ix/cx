// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqmmanual.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cx/taskqueue/taskqueue_private.h"

_objfactory_guaranteed TQManualManager* TQManualManager_create()
{
    TQManualManager* self;
    self = objInstCreate(TQManualManager);
    objInstInit(self);
    return self;
}

_objinit_guaranteed bool TQManualManager_init(_Inout_ TQManualManager* self)
{
    self->needsWorkerTick = true;
    mutexInit(&self->mgrlock);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

extern void TQManager_notify(_Inout_ TQManager* self);   // parent
#define parent_notify() TQManager_notify((TQManager*)(self))
void TQManualManager_notify(_Inout_ TQManualManager* self)
{
    if (self->tq) {
        atomicStore(bool, &self->needrun, true, Relaxed);
        eventSignal(&self->tq->workev);
    }
}

extern int64 TQManager_tick(_Inout_ TQManager* self);   // parent
#define parent_tick() TQManager_tick((TQManager*)(self))
int64 TQManualManager_tick(_Inout_ TQManualManager* self)
{
    int64 ret = timeForever;

    // this will USUALLY be called from the same thread over and over, but manual workers CAN be
    // ticked from different threads, so we need to handle it just like the in-worker class
    if (atomicLoad(bool, &self->needrun, Relaxed) == true && mutexTryAcquire(&self->mgrlock)) {
        atomicStore(bool, &self->needrun, false, Relaxed);

        // Run monitor if we have one (usually don't for manual, but nothing stopping somebody from
        // writing one)
        if (self->tq->monitor)
            ret = tqmonitorTick(self->tq->monitor);

        // Process done queue and deferred/scheduled tasks
        bool taskscompleted = taskqueue_processDone(self->tq);

        int64 extrawait = taskqueue_processExtra(self->tq, taskscompleted);
        ret             = min(ret, extrawait);

        mutexRelease(&self->mgrlock);

        // if another thread needed the manager in the meantime, tell the caller to run it again
        if (atomicLoad(bool, &self->needrun, Relaxed))
            ret = 0;
    }

    return ret;
}

void TQManualManager_destroy(_Inout_ TQManualManager* self)
{
    mutexDestroy(&self->mgrlock);
}

// Autogen begins -----
#include "tqmmanual.auto.inc"
// Autogen ends -------