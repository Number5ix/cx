// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "tqtestobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include <cx/taskqueue.h>

_objfactory_guaranteed TQTest1* TQTest1_create(int num1, int num2, Event* notify)
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

uint32 TQTest1_run(_Inout_ TQTest1* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    self->total = self->num[0] + self->num[1];
    tcon->notifyev = self->notify;

    return TASK_Result_Success;
}

_objfactory_guaranteed TQTestFail* TQTestFail_create(int n, Event* notify)
{
    TQTestFail *self;
    self = objInstCreate(TQTestFail);

    self->name = _S"TQTestFail";
    self->n = n;
    self->notify = notify;

    objInstInit(self);

    return self;
}

uint32 TQTestFail_run(_Inout_ TQTestFail* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    tcon->notifyev = self->notify;
    return self->n & 1 ? TASK_Result_Success :
                         TASK_Result_Failure;   // odd will succeed, even will fail
}

_objfactory_guaranteed TQTestCC1* TQTestCC1_create(int num1, int num2, TaskQueue* destq, int* accum, int* counter, Event* notify)
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

uint32 TQTestCC1_run(_Inout_ TQTestCC1* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    TQTestCC2 *task = tqtestcc2Create(self->num[0] + self->num[1], self->accum, self->counter, self->notify);
    tqRun(self->destq, &task);
    return TASK_Result_Success;
}

_objfactory_guaranteed TQTestCC2* TQTestCC2_create(int total, int* accum, int* counter, Event* notify)
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

uint32 TQTestCC2_run(_Inout_ TQTestCC2* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    *self->accum += self->total;
    *self->counter += 1;
    tcon->notifyev = self->notify;
    return TASK_Result_Success;
}

extern atomic(int32) tqtests_order;

_objfactory_guaranteed TQTestS1* TQTestS1_create(int order, int64 dtime, Event* notify)
{
    TQTestS1 *self;
    self = objInstCreate(TQTestS1);

    self->name = _S"TQTestS1";
    self->order = order;
    self->dtime = dtime;
    self->notify = notify;

    objInstInit(self);

    return self;
}

uint32 TQTestS1_run(_Inout_ TQTestS1* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    if (self->rantime == 0) {
        self->rantime = clockTimer();
        tcon->delay   = self->dtime;
        return TASK_Result_Schedule;
    }

    tcon->notifyev = self->notify;
    if(self->rantime + self->dtime > clockTimer())
        return TASK_Result_Failure;   // was run early!!!

    if(atomicLoad(int32, &tqtests_order, Acquire) > self->order)
        return TASK_Result_Failure;

    atomicStore(int32, &tqtests_order, self->order, Release);

    return TASK_Result_Success;
}

_objfactory_guaranteed TQTestS2* TQTestS2_create(Task* waitfor, Event* notify)
{
    TQTestS2 *self;
    self = objInstCreate(TQTestS2);

    self->name = _S"TQTestS2";
    self->waitfor = objAcquire(waitfor);
    self->notify = notify;

    objInstInit(self);

    // Insert any post-initialization object construction here

    return self;
}

uint32 TQTestS2_run(_Inout_ TQTestS2* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    int state = btaskState(self->waitfor);
    if(state == TASK_Succeeded) {
        tcon->notifyev = self->notify;
        return TASK_Result_Success;
    }
    if(state == TASK_Failed) {
        tcon->notifyev = self->notify;
        return TASK_Result_Failure;
    }

    tcon->delay = -1;
    return TASK_Result_Schedule;
}

void TQTestS2_destroy(_Inout_ TQTestS2* self)
{
    // Autogen begins -----
    objRelease(&self->waitfor);
    // Autogen ends -------
}

_objfactory_guaranteed TQDelayTest* TQDelayTest_create(int64 len)
{
    TQDelayTest *self;
    self = objInstCreate(TQDelayTest);

    self->name = _S"TQDelayTest";
    self->len = len;

    objInstInit(self);

    return self;
}

uint32 TQDelayTest_run(_Inout_ TQDelayTest* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    osSleep(self->len);
    return TASK_Result_Success;
}

_objfactory_guaranteed TQMTest* TQMTest_create(Event* notify)
{
    TQMTest *self;
    self = objInstCreate(TQMTest);

    self->notify = notify;

    objInstInit(self);

    return self;
}

uint32 TQMTest_run(_Inout_ TQMTest* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon)
{
    tcon->notifyev = self->notify;

    return TASK_Result_Success;
}

// Autogen begins -----
#include "tqtestobj.auto.inc"
// Autogen ends -------
