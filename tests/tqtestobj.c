// ==================== Auto-generated section begins ====================
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqtestobj.h"
// ==================== Auto-generated section ends ======================
#include <cx/taskqueue.h>

_objfactory_guaranteed TQTest1 *TQTest1_create(int num1, int num2, Event *notify)
{
    TQTest1 *self;
    self = objInstCreate(TQTest1);

    self->name = _S"TQTest1";
    self->num[0] = num1;
    self->num[1] = num2;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTest1_run(_Inout_ TQTest1 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    self->total = self->num[0] + self->num[1];
    tcon->notifyev = self->notify;

    return true;
}

_objfactory_guaranteed TQTestFail *TQTestFail_create(int n, Event *notify)
{
    TQTestFail *self;
    self = objInstCreate(TQTestFail);

    self->name = _S"TQTestFail";
    self->n = n;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTestFail_run(_Inout_ TQTestFail *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    tcon->notifyev = self->notify;
    return self->n & 1;     // odd will succeed, even will fail
}

_objfactory_guaranteed TQTestCC1 *TQTestCC1_create(int num1, int num2, TaskQueue *destq, int *accum, int *counter, Event *notify)
{
    TQTestCC1 *self;
    self = objInstCreate(TQTestCC1);

    self->name = _S"TQTestCC1";
    self->num[0] = num1;
    self->num[1] = num2;
    self->destq = destq;
    self->accum = accum;
    self->counter = counter;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTestCC1_run(_Inout_ TQTestCC1 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    TQTestCC2 *task = tqtestcc2Create(self->num[0] + self->num[1], self->accum, self->counter, self->notify);
    tqRun(self->destq, &task);
    return true;
}

_objfactory_guaranteed TQTestCC2 *TQTestCC2_create(int total, int *accum, int *counter, Event *notify)
{
    TQTestCC2 *self;
    self = objInstCreate(TQTestCC2);

    self->name = _S"TQTestCC2";
    self->total = total;
    self->accum = accum;
    self->counter = counter;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTestCC2_run(_Inout_ TQTestCC2 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    *self->accum += self->total;
    *self->counter += 1;
    tcon->notifyev = self->notify;
    return true;
}

extern atomic(int32) tqtestd_order;

_objfactory_guaranteed TQTestD1 *TQTestD1_create(int order, int64 dtime, Event *notify)
{
    TQTestD1 *self;
    self = objInstCreate(TQTestD1);

    self->name = _S"TQTestD1";
    self->order = order;
    self->dtime = dtime;
    self->notify = notify;

    objInstInit(self);

    return self;
}

bool TQTestD1_run(_Inout_ TQTestD1 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    if(self->rantime == 0) {
        tcon->defer = true;
        tcon->defertime = self->dtime;
        self->rantime = clockTimer();
        return true;
    }

    tcon->notifyev = self->notify;
    if(self->rantime + self->dtime > clockTimer())
        return false;           // was run early!!!

    if(atomicLoad(int32, &tqtestd_order, Acquire) > self->order)
        return false;

    atomicStore(int32, &tqtestd_order, self->order, Release);

    return true;
}

_objfactory_guaranteed TQTestD2 *TQTestD2_create(Task *waitfor, Event *notify)
{
    TQTestD2 *self;
    self = objInstCreate(TQTestD2);

    self->name = _S"TQTestD2";
    self->waitfor = objAcquire(waitfor);
    self->notify = notify;

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

bool TQTestD2_run(_Inout_ TQTestD2 *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    int state = btaskState(self->waitfor);
    if(state == TASK_Succeeded) {
        tcon->notifyev = self->notify;
        return true;
    }
    if(state == TASK_Failed) {
        tcon->notifyev = self->notify;
        return false;
    }

    tcon->defer = true;     // still waiting
    return true;
}

void TQTestD2_destroy(_Inout_ TQTestD2 *self)
{
    // Autogen begins -----
    objRelease(&self->waitfor);
    // Autogen ends -------
}

_objfactory_guaranteed TQDelayTest *TQDelayTest_create(int64 len)
{
    TQDelayTest *self;
    self = objInstCreate(TQDelayTest);

    self->name = _S"TQDelayTest";
    self->len = len;

    objInstInit(self);

    return self;
}

bool TQDelayTest_run(_Inout_ TQDelayTest *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    osSleep(self->len);
    return true;
}

_objfactory_guaranteed TQMTest *TQMTest_create(Event *notify, TaskQueue *tq, int limit)
{
    TQMTest *self;
    self = objInstCreate(TQMTest);

    self->notify = notify;
    self->tq = objAcquire(tq);
    self->limit = limit;

    objInstInit(self);

    return self;
}

extern bool MTask_run(_Inout_ MTask *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon); // parent
#define parent_run(tq, tcon) MTask_run((MTask*)(self), tq, tcon)
bool TQMTest_run(_Inout_ TQMTest *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon)
{
    bool ret = parent_run(tq, tcon);

    if(self->done) {
        eventSignal(self->notify);
    }

    return ret;
}

// Autogen begins -----
#include "tqtestobj.auto.inc"
// Autogen ends -------
