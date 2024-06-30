#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include <cx/taskqueue/task/complextask.h>
#include <cx/thread/event.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TQWorker TQWorker;
typedef struct TQWorker_WeakRef TQWorker_WeakRef;
typedef struct ComplexTaskQueue ComplexTaskQueue;
typedef struct ComplexTaskQueue_WeakRef ComplexTaskQueue_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct TQTest1 TQTest1;
typedef struct TQTest1_WeakRef TQTest1_WeakRef;
typedef struct TQTestFail TQTestFail;
typedef struct TQTestFail_WeakRef TQTestFail_WeakRef;
typedef struct TQTestCC1 TQTestCC1;
typedef struct TQTestCC1_WeakRef TQTestCC1_WeakRef;
typedef struct TQTestCC2 TQTestCC2;
typedef struct TQTestCC2_WeakRef TQTestCC2_WeakRef;
typedef struct TQTestSched TQTestSched;
typedef struct TQTestSched_WeakRef TQTestSched_WeakRef;
typedef struct TQTestS1 TQTestS1;
typedef struct TQTestS1_WeakRef TQTestS1_WeakRef;
typedef struct TQTestS2 TQTestS2;
typedef struct TQTestS2_WeakRef TQTestS2_WeakRef;
typedef struct TQDelayTest TQDelayTest;
typedef struct TQDelayTest_WeakRef TQDelayTest_WeakRef;
typedef struct TQMTest TQMTest;
typedef struct TQMTest_WeakRef TQMTest_WeakRef;
saDeclarePtr(TQTest1);
saDeclarePtr(TQTest1_WeakRef);
saDeclarePtr(TQTestFail);
saDeclarePtr(TQTestFail_WeakRef);
saDeclarePtr(TQTestCC1);
saDeclarePtr(TQTestCC1_WeakRef);
saDeclarePtr(TQTestCC2);
saDeclarePtr(TQTestCC2_WeakRef);
saDeclarePtr(TQTestSched);
saDeclarePtr(TQTestSched_WeakRef);
saDeclarePtr(TQTestS1);
saDeclarePtr(TQTestS1_WeakRef);
saDeclarePtr(TQTestS2);
saDeclarePtr(TQTestS2_WeakRef);
saDeclarePtr(TQDelayTest);
saDeclarePtr(TQDelayTest_WeakRef);
saDeclarePtr(TQMTest);
saDeclarePtr(TQMTest_WeakRef);

//#include <cx/taskqueue/mtask.sidl>

typedef struct TQTest1_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
} TQTest1_ClassIf;
extern TQTest1_ClassIf TQTest1_ClassIf_tmpl;

typedef struct TQTestFail_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
} TQTestFail_ClassIf;
extern TQTestFail_ClassIf TQTestFail_ClassIf_tmpl;

typedef struct TQTestCC1_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
} TQTestCC1_ClassIf;
extern TQTestCC1_ClassIf TQTestCC1_ClassIf_tmpl;

typedef struct TQTestCC2_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
} TQTestCC2_ClassIf;
extern TQTestCC2_ClassIf TQTestCC2_ClassIf_tmpl;

typedef struct TQTestSched_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
    void (*dependOn)(_Inout_ void* self, _In_ Task* dep);
    intptr (*cmp)(_Inout_ void* self, void* other, uint32 flags);
    uint32 (*hash)(_Inout_ void* self, uint32 flags);
} TQTestSched_ClassIf;
extern TQTestSched_ClassIf TQTestSched_ClassIf_tmpl;

typedef struct TQTestS1_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
    void (*dependOn)(_Inout_ void* self, _In_ Task* dep);
    intptr (*cmp)(_Inout_ void* self, void* other, uint32 flags);
    uint32 (*hash)(_Inout_ void* self, uint32 flags);
} TQTestS1_ClassIf;
extern TQTestS1_ClassIf TQTestS1_ClassIf_tmpl;

typedef struct TQTestS2_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
    void (*dependOn)(_Inout_ void* self, _In_ Task* dep);
    intptr (*cmp)(_Inout_ void* self, void* other, uint32 flags);
    uint32 (*hash)(_Inout_ void* self, uint32 flags);
} TQTestS2_ClassIf;
extern TQTestS2_ClassIf TQTestS2_ClassIf_tmpl;

typedef struct TQDelayTest_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
} TQDelayTest_ClassIf;
extern TQDelayTest_ClassIf TQDelayTest_ClassIf_tmpl;

typedef struct TQMTest_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
    void (*dependOn)(_Inout_ void* self, _In_ Task* dep);
    intptr (*cmp)(_Inout_ void* self, void* other, uint32 flags);
    uint32 (*hash)(_Inout_ void* self, uint32 flags);
} TQMTest_ClassIf;
extern TQMTest_ClassIf TQMTest_ClassIf_tmpl;

typedef struct TQTest1 {
    union {
        TQTest1_ClassIf* _;
        void* _is_TQTest1;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int num[2];
    int total;
    Event* notify;
} TQTest1;
extern ObjClassInfo TQTest1_clsinfo;
#define TQTest1(inst) ((TQTest1*)(unused_noeval((inst) && &((inst)->_is_TQTest1)), (inst)))
#define TQTest1None ((TQTest1*)NULL)

typedef struct TQTest1_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTest1_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTest1_WeakRef;
#define TQTest1_WeakRef(inst) ((TQTest1_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTest1_WeakRef)), (inst)))

_objfactory_guaranteed TQTest1* TQTest1_create(int num1, int num2, Event* notify);
// TQTest1* tqtest1Create(int num1, int num2, Event* notify);
#define tqtest1Create(num1, num2, notify) TQTest1_create(num1, num2, notify)

// bool tqtest1_setState(TQTest1* self, uint32 newstate);
#define tqtest1_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqtest1Run(TQTest1* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqtest1Run(self, tq, worker, tcon) (self)->_->run(TQTest1(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqtest1Cancel(TQTest1* self);
#define tqtest1Cancel(self) (self)->_->cancel(TQTest1(self))
// bool tqtest1Reset(TQTest1* self);
#define tqtest1Reset(self) (self)->_->reset(TQTest1(self))
// bool tqtest1Wait(TQTest1* self, int64 timeout);
#define tqtest1Wait(self, timeout) (self)->_->wait(TQTest1(self), timeout)

typedef struct TQTestFail {
    union {
        TQTestFail_ClassIf* _;
        void* _is_TQTestFail;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int n;
    Event* notify;
} TQTestFail;
extern ObjClassInfo TQTestFail_clsinfo;
#define TQTestFail(inst) ((TQTestFail*)(unused_noeval((inst) && &((inst)->_is_TQTestFail)), (inst)))
#define TQTestFailNone ((TQTestFail*)NULL)

typedef struct TQTestFail_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestFail_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestFail_WeakRef;
#define TQTestFail_WeakRef(inst) ((TQTestFail_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestFail_WeakRef)), (inst)))

_objfactory_guaranteed TQTestFail* TQTestFail_create(int n, Event* notify);
// TQTestFail* tqtestfailCreate(int n, Event* notify);
#define tqtestfailCreate(n, notify) TQTestFail_create(n, notify)

// bool tqtestfail_setState(TQTestFail* self, uint32 newstate);
#define tqtestfail_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqtestfailRun(TQTestFail* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqtestfailRun(self, tq, worker, tcon) (self)->_->run(TQTestFail(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqtestfailCancel(TQTestFail* self);
#define tqtestfailCancel(self) (self)->_->cancel(TQTestFail(self))
// bool tqtestfailReset(TQTestFail* self);
#define tqtestfailReset(self) (self)->_->reset(TQTestFail(self))
// bool tqtestfailWait(TQTestFail* self, int64 timeout);
#define tqtestfailWait(self, timeout) (self)->_->wait(TQTestFail(self), timeout)

typedef struct TQTestCC1 {
    union {
        TQTestCC1_ClassIf* _;
        void* _is_TQTestCC1;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int num[2];
    TaskQueue* destq;
    int* accum;
    int* counter;
    Event* notify;
} TQTestCC1;
extern ObjClassInfo TQTestCC1_clsinfo;
#define TQTestCC1(inst) ((TQTestCC1*)(unused_noeval((inst) && &((inst)->_is_TQTestCC1)), (inst)))
#define TQTestCC1None ((TQTestCC1*)NULL)

typedef struct TQTestCC1_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestCC1_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestCC1_WeakRef;
#define TQTestCC1_WeakRef(inst) ((TQTestCC1_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestCC1_WeakRef)), (inst)))

_objfactory_guaranteed TQTestCC1* TQTestCC1_create(int num1, int num2, TaskQueue* destq, int* accum, int* counter, Event* notify);
// TQTestCC1* tqtestcc1Create(int num1, int num2, TaskQueue* destq, int* accum, int* counter, Event* notify);
#define tqtestcc1Create(num1, num2, destq, accum, counter, notify) TQTestCC1_create(num1, num2, TaskQueue(destq), accum, counter, notify)

// bool tqtestcc1_setState(TQTestCC1* self, uint32 newstate);
#define tqtestcc1_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqtestcc1Run(TQTestCC1* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqtestcc1Run(self, tq, worker, tcon) (self)->_->run(TQTestCC1(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqtestcc1Cancel(TQTestCC1* self);
#define tqtestcc1Cancel(self) (self)->_->cancel(TQTestCC1(self))
// bool tqtestcc1Reset(TQTestCC1* self);
#define tqtestcc1Reset(self) (self)->_->reset(TQTestCC1(self))
// bool tqtestcc1Wait(TQTestCC1* self, int64 timeout);
#define tqtestcc1Wait(self, timeout) (self)->_->wait(TQTestCC1(self), timeout)

typedef struct TQTestCC2 {
    union {
        TQTestCC2_ClassIf* _;
        void* _is_TQTestCC2;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int total;
    int* accum;
    int* counter;
    Event* notify;
} TQTestCC2;
extern ObjClassInfo TQTestCC2_clsinfo;
#define TQTestCC2(inst) ((TQTestCC2*)(unused_noeval((inst) && &((inst)->_is_TQTestCC2)), (inst)))
#define TQTestCC2None ((TQTestCC2*)NULL)

typedef struct TQTestCC2_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestCC2_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestCC2_WeakRef;
#define TQTestCC2_WeakRef(inst) ((TQTestCC2_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestCC2_WeakRef)), (inst)))

_objfactory_guaranteed TQTestCC2* TQTestCC2_create(int total, int* accum, int* counter, Event* notify);
// TQTestCC2* tqtestcc2Create(int total, int* accum, int* counter, Event* notify);
#define tqtestcc2Create(total, accum, counter, notify) TQTestCC2_create(total, accum, counter, notify)

// bool tqtestcc2_setState(TQTestCC2* self, uint32 newstate);
#define tqtestcc2_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqtestcc2Run(TQTestCC2* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqtestcc2Run(self, tq, worker, tcon) (self)->_->run(TQTestCC2(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqtestcc2Cancel(TQTestCC2* self);
#define tqtestcc2Cancel(self) (self)->_->cancel(TQTestCC2(self))
// bool tqtestcc2Reset(TQTestCC2* self);
#define tqtestcc2Reset(self) (self)->_->reset(TQTestCC2(self))
// bool tqtestcc2Wait(TQTestCC2* self, int64 timeout);
#define tqtestcc2Wait(self, timeout) (self)->_->wait(TQTestCC2(self), timeout)

typedef struct TQTestSched {
    union {
        TQTestSched_ClassIf* _;
        void* _is_TQTestSched;
        void* _is_ComplexTask;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int64 nextrun;        // next time for this task to run when scheduled
    int64 lastprogress;        // timestamp of last progress change
    Weak(ComplexTaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    sa_Task _depends;        // other tasks that must complete before this task can run, do not modify directly!
    Event* notify;
} TQTestSched;
extern ObjClassInfo TQTestSched_clsinfo;
#define TQTestSched(inst) ((TQTestSched*)(unused_noeval((inst) && &((inst)->_is_TQTestSched)), (inst)))
#define TQTestSchedNone ((TQTestSched*)NULL)

typedef struct TQTestSched_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestSched_WeakRef;
        void* _is_ComplexTask_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestSched_WeakRef;
#define TQTestSched_WeakRef(inst) ((TQTestSched_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestSched_WeakRef)), (inst)))

// bool tqtestschedAdvance(TQTestSched* self);
//
// advance a deferred task to run as soon as possible
#define tqtestschedAdvance(self) ComplexTask_advance(ComplexTask(self))

// bool tqtestschedCheckDeps(TQTestSched* self, bool updateProgress);
//
// check if this task can run because all dependencies are satisfied
#define tqtestschedCheckDeps(self, updateProgress) ComplexTask_checkDeps(ComplexTask(self), updateProgress)

// bool tqtestsched_setState(TQTestSched* self, uint32 newstate);
#define tqtestsched_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqtestschedRun(TQTestSched* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqtestschedRun(self, tq, worker, tcon) (self)->_->run(TQTestSched(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqtestschedCancel(TQTestSched* self);
#define tqtestschedCancel(self) (self)->_->cancel(TQTestSched(self))
// bool tqtestschedReset(TQTestSched* self);
#define tqtestschedReset(self) (self)->_->reset(TQTestSched(self))
// bool tqtestschedWait(TQTestSched* self, int64 timeout);
#define tqtestschedWait(self, timeout) (self)->_->wait(TQTestSched(self), timeout)
// void tqtestschedDependOn(TQTestSched* self, Task* dep);
#define tqtestschedDependOn(self, dep) (self)->_->dependOn(TQTestSched(self), Task(dep))
// intptr tqtestschedCmp(TQTestSched* self, TQTestSched* other, uint32 flags);
#define tqtestschedCmp(self, other, flags) (self)->_->cmp(TQTestSched(self), other, flags)
// uint32 tqtestschedHash(TQTestSched* self, uint32 flags);
#define tqtestschedHash(self, flags) (self)->_->hash(TQTestSched(self), flags)

typedef struct TQTestS1 {
    union {
        TQTestS1_ClassIf* _;
        void* _is_TQTestS1;
        void* _is_TQTestSched;
        void* _is_ComplexTask;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int64 nextrun;        // next time for this task to run when scheduled
    int64 lastprogress;        // timestamp of last progress change
    Weak(ComplexTaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    sa_Task _depends;        // other tasks that must complete before this task can run, do not modify directly!
    Event* notify;
    int order;
    int64 dtime;
    int64 rantime;
} TQTestS1;
extern ObjClassInfo TQTestS1_clsinfo;
#define TQTestS1(inst) ((TQTestS1*)(unused_noeval((inst) && &((inst)->_is_TQTestS1)), (inst)))
#define TQTestS1None ((TQTestS1*)NULL)

typedef struct TQTestS1_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestS1_WeakRef;
        void* _is_TQTestSched_WeakRef;
        void* _is_ComplexTask_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestS1_WeakRef;
#define TQTestS1_WeakRef(inst) ((TQTestS1_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestS1_WeakRef)), (inst)))

_objfactory_guaranteed TQTestS1* TQTestS1_create(int order, int64 dtime, Event* notify);
// TQTestS1* tqtests1Create(int order, int64 dtime, Event* notify);
#define tqtests1Create(order, dtime, notify) TQTestS1_create(order, dtime, notify)

// bool tqtests1Advance(TQTestS1* self);
//
// advance a deferred task to run as soon as possible
#define tqtests1Advance(self) ComplexTask_advance(ComplexTask(self))

// bool tqtests1CheckDeps(TQTestS1* self, bool updateProgress);
//
// check if this task can run because all dependencies are satisfied
#define tqtests1CheckDeps(self, updateProgress) ComplexTask_checkDeps(ComplexTask(self), updateProgress)

// bool tqtests1_setState(TQTestS1* self, uint32 newstate);
#define tqtests1_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqtests1Run(TQTestS1* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqtests1Run(self, tq, worker, tcon) (self)->_->run(TQTestS1(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqtests1Cancel(TQTestS1* self);
#define tqtests1Cancel(self) (self)->_->cancel(TQTestS1(self))
// bool tqtests1Reset(TQTestS1* self);
#define tqtests1Reset(self) (self)->_->reset(TQTestS1(self))
// bool tqtests1Wait(TQTestS1* self, int64 timeout);
#define tqtests1Wait(self, timeout) (self)->_->wait(TQTestS1(self), timeout)
// void tqtests1DependOn(TQTestS1* self, Task* dep);
#define tqtests1DependOn(self, dep) (self)->_->dependOn(TQTestS1(self), Task(dep))
// intptr tqtests1Cmp(TQTestS1* self, TQTestS1* other, uint32 flags);
#define tqtests1Cmp(self, other, flags) (self)->_->cmp(TQTestS1(self), other, flags)
// uint32 tqtests1Hash(TQTestS1* self, uint32 flags);
#define tqtests1Hash(self, flags) (self)->_->hash(TQTestS1(self), flags)

typedef struct TQTestS2 {
    union {
        TQTestS2_ClassIf* _;
        void* _is_TQTestS2;
        void* _is_TQTestSched;
        void* _is_ComplexTask;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int64 nextrun;        // next time for this task to run when scheduled
    int64 lastprogress;        // timestamp of last progress change
    Weak(ComplexTaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    sa_Task _depends;        // other tasks that must complete before this task can run, do not modify directly!
    Event* notify;
    Task* waitfor;
} TQTestS2;
extern ObjClassInfo TQTestS2_clsinfo;
#define TQTestS2(inst) ((TQTestS2*)(unused_noeval((inst) && &((inst)->_is_TQTestS2)), (inst)))
#define TQTestS2None ((TQTestS2*)NULL)

typedef struct TQTestS2_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestS2_WeakRef;
        void* _is_TQTestSched_WeakRef;
        void* _is_ComplexTask_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestS2_WeakRef;
#define TQTestS2_WeakRef(inst) ((TQTestS2_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestS2_WeakRef)), (inst)))

_objfactory_guaranteed TQTestS2* TQTestS2_create(Task* waitfor, Event* notify);
// TQTestS2* tqtests2Create(Task* waitfor, Event* notify);
#define tqtests2Create(waitfor, notify) TQTestS2_create(Task(waitfor), notify)

// bool tqtests2Advance(TQTestS2* self);
//
// advance a deferred task to run as soon as possible
#define tqtests2Advance(self) ComplexTask_advance(ComplexTask(self))

// bool tqtests2CheckDeps(TQTestS2* self, bool updateProgress);
//
// check if this task can run because all dependencies are satisfied
#define tqtests2CheckDeps(self, updateProgress) ComplexTask_checkDeps(ComplexTask(self), updateProgress)

// bool tqtests2_setState(TQTestS2* self, uint32 newstate);
#define tqtests2_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqtests2Run(TQTestS2* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqtests2Run(self, tq, worker, tcon) (self)->_->run(TQTestS2(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqtests2Cancel(TQTestS2* self);
#define tqtests2Cancel(self) (self)->_->cancel(TQTestS2(self))
// bool tqtests2Reset(TQTestS2* self);
#define tqtests2Reset(self) (self)->_->reset(TQTestS2(self))
// bool tqtests2Wait(TQTestS2* self, int64 timeout);
#define tqtests2Wait(self, timeout) (self)->_->wait(TQTestS2(self), timeout)
// void tqtests2DependOn(TQTestS2* self, Task* dep);
#define tqtests2DependOn(self, dep) (self)->_->dependOn(TQTestS2(self), Task(dep))
// intptr tqtests2Cmp(TQTestS2* self, TQTestS2* other, uint32 flags);
#define tqtests2Cmp(self, other, flags) (self)->_->cmp(TQTestS2(self), other, flags)
// uint32 tqtests2Hash(TQTestS2* self, uint32 flags);
#define tqtests2Hash(self, flags) (self)->_->hash(TQTestS2(self), flags)

typedef struct TQDelayTest {
    union {
        TQDelayTest_ClassIf* _;
        void* _is_TQDelayTest;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int64 len;
} TQDelayTest;
extern ObjClassInfo TQDelayTest_clsinfo;
#define TQDelayTest(inst) ((TQDelayTest*)(unused_noeval((inst) && &((inst)->_is_TQDelayTest)), (inst)))
#define TQDelayTestNone ((TQDelayTest*)NULL)

typedef struct TQDelayTest_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQDelayTest_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQDelayTest_WeakRef;
#define TQDelayTest_WeakRef(inst) ((TQDelayTest_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQDelayTest_WeakRef)), (inst)))

_objfactory_guaranteed TQDelayTest* TQDelayTest_create(int64 len);
// TQDelayTest* tqdelaytestCreate(int64 len);
#define tqdelaytestCreate(len) TQDelayTest_create(len)

// bool tqdelaytest_setState(TQDelayTest* self, uint32 newstate);
#define tqdelaytest_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqdelaytestRun(TQDelayTest* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqdelaytestRun(self, tq, worker, tcon) (self)->_->run(TQDelayTest(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqdelaytestCancel(TQDelayTest* self);
#define tqdelaytestCancel(self) (self)->_->cancel(TQDelayTest(self))
// bool tqdelaytestReset(TQDelayTest* self);
#define tqdelaytestReset(self) (self)->_->reset(TQDelayTest(self))
// bool tqdelaytestWait(TQDelayTest* self, int64 timeout);
#define tqdelaytestWait(self, timeout) (self)->_->wait(TQDelayTest(self), timeout)

typedef struct TQMTest {
    union {
        TQMTest_ClassIf* _;
        void* _is_TQMTest;
        void* _is_ComplexTask;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(uint32) state;
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    cchain oncomplete;        // functions that are called when this task has completed
    int64 nextrun;        // next time for this task to run when scheduled
    int64 lastprogress;        // timestamp of last progress change
    Weak(ComplexTaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    sa_Task _depends;        // other tasks that must complete before this task can run, do not modify directly!
    Event* notify;
} TQMTest;
extern ObjClassInfo TQMTest_clsinfo;
#define TQMTest(inst) ((TQMTest*)(unused_noeval((inst) && &((inst)->_is_TQMTest)), (inst)))
#define TQMTestNone ((TQMTest*)NULL)

typedef struct TQMTest_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQMTest_WeakRef;
        void* _is_ComplexTask_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQMTest_WeakRef;
#define TQMTest_WeakRef(inst) ((TQMTest_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQMTest_WeakRef)), (inst)))

_objfactory_guaranteed TQMTest* TQMTest_create(Event* notify);
// TQMTest* tqmtestCreate(Event* notify);
#define tqmtestCreate(notify) TQMTest_create(notify)

// bool tqmtestAdvance(TQMTest* self);
//
// advance a deferred task to run as soon as possible
#define tqmtestAdvance(self) ComplexTask_advance(ComplexTask(self))

// bool tqmtestCheckDeps(TQMTest* self, bool updateProgress);
//
// check if this task can run because all dependencies are satisfied
#define tqmtestCheckDeps(self, updateProgress) ComplexTask_checkDeps(ComplexTask(self), updateProgress)

// bool tqmtest_setState(TQMTest* self, uint32 newstate);
#define tqmtest_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 tqmtestRun(TQMTest* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define tqmtestRun(self, tq, worker, tcon) (self)->_->run(TQMTest(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool tqmtestCancel(TQMTest* self);
#define tqmtestCancel(self) (self)->_->cancel(TQMTest(self))
// bool tqmtestReset(TQMTest* self);
#define tqmtestReset(self) (self)->_->reset(TQMTest(self))
// bool tqmtestWait(TQMTest* self, int64 timeout);
#define tqmtestWait(self, timeout) (self)->_->wait(TQMTest(self), timeout)
// void tqmtestDependOn(TQMTest* self, Task* dep);
#define tqmtestDependOn(self, dep) (self)->_->dependOn(TQMTest(self), Task(dep))
// intptr tqmtestCmp(TQMTest* self, TQMTest* other, uint32 flags);
#define tqmtestCmp(self, other, flags) (self)->_->cmp(TQMTest(self), other, flags)
// uint32 tqmtestHash(TQMTest* self, uint32 flags);
#define tqmtestHash(self, flags) (self)->_->hash(TQMTest(self), flags)

