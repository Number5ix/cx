#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include <cx/taskqueue/task.h>
#include <cx/taskqueue/mtask.h>
#include <cx/thread/event.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct TQTest1 TQTest1;
typedef struct TQTest1_WeakRef TQTest1_WeakRef;
typedef struct TQTestFail TQTestFail;
typedef struct TQTestFail_WeakRef TQTestFail_WeakRef;
typedef struct TQTestCC1 TQTestCC1;
typedef struct TQTestCC1_WeakRef TQTestCC1_WeakRef;
typedef struct TQTestCC2 TQTestCC2;
typedef struct TQTestCC2_WeakRef TQTestCC2_WeakRef;
typedef struct TQTestDefer TQTestDefer;
typedef struct TQTestDefer_WeakRef TQTestDefer_WeakRef;
typedef struct TQTestD1 TQTestD1;
typedef struct TQTestD1_WeakRef TQTestD1_WeakRef;
typedef struct TQTestD2 TQTestD2;
typedef struct TQTestD2_WeakRef TQTestD2_WeakRef;
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
saDeclarePtr(TQTestDefer);
saDeclarePtr(TQTestDefer_WeakRef);
saDeclarePtr(TQTestD1);
saDeclarePtr(TQTestD1_WeakRef);
saDeclarePtr(TQTestD2);
saDeclarePtr(TQTestD2_WeakRef);
saDeclarePtr(TQDelayTest);
saDeclarePtr(TQDelayTest_WeakRef);
saDeclarePtr(TQMTest);
saDeclarePtr(TQMTest_WeakRef);

typedef struct TQTest1_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQTest1_ClassIf;
extern TQTest1_ClassIf TQTest1_ClassIf_tmpl;

typedef struct TQTestFail_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQTestFail_ClassIf;
extern TQTestFail_ClassIf TQTestFail_ClassIf_tmpl;

typedef struct TQTestCC1_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQTestCC1_ClassIf;
extern TQTestCC1_ClassIf TQTestCC1_ClassIf_tmpl;

typedef struct TQTestCC2_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQTestCC2_ClassIf;
extern TQTestCC2_ClassIf TQTestCC2_ClassIf_tmpl;

typedef struct TQTestDefer_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQTestDefer_ClassIf;
extern TQTestDefer_ClassIf TQTestDefer_ClassIf_tmpl;

typedef struct TQTestD1_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQTestD1_ClassIf;
extern TQTestD1_ClassIf TQTestD1_ClassIf_tmpl;

typedef struct TQTestD2_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQTestD2_ClassIf;
extern TQTestD2_ClassIf TQTestD2_ClassIf_tmpl;

typedef struct TQDelayTest_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} TQDelayTest_ClassIf;
extern TQDelayTest_ClassIf TQDelayTest_ClassIf_tmpl;

typedef struct TQMTest_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
    // Add a task
    void (*add)(_Inout_ void* self, _In_ Task* task);
    // Run cycle of checking / queueing tasks as needed (private)
    bool (*_cycle)(_Inout_ void* self);
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

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
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

// bool tqtest1Advance(TQTest1* self);
//
// advance a deferred task to run as soon as possible
#define tqtest1Advance(self) Task_advance(Task(self))

// bool tqtest1Run(TQTest1* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqtest1Run(self, tq, worker, tcon) (self)->_->run(TQTest1(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqtest1Reset(TQTest1* self);
#define tqtest1Reset(self) (self)->_->reset(TQTest1(self))

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

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
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

// bool tqtestfailAdvance(TQTestFail* self);
//
// advance a deferred task to run as soon as possible
#define tqtestfailAdvance(self) Task_advance(Task(self))

// bool tqtestfailRun(TQTestFail* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqtestfailRun(self, tq, worker, tcon) (self)->_->run(TQTestFail(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqtestfailReset(TQTestFail* self);
#define tqtestfailReset(self) (self)->_->reset(TQTestFail(self))

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

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
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

// bool tqtestcc1Advance(TQTestCC1* self);
//
// advance a deferred task to run as soon as possible
#define tqtestcc1Advance(self) Task_advance(Task(self))

// bool tqtestcc1Run(TQTestCC1* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqtestcc1Run(self, tq, worker, tcon) (self)->_->run(TQTestCC1(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqtestcc1Reset(TQTestCC1* self);
#define tqtestcc1Reset(self) (self)->_->reset(TQTestCC1(self))

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

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
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

// bool tqtestcc2Advance(TQTestCC2* self);
//
// advance a deferred task to run as soon as possible
#define tqtestcc2Advance(self) Task_advance(Task(self))

// bool tqtestcc2Run(TQTestCC2* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqtestcc2Run(self, tq, worker, tcon) (self)->_->run(TQTestCC2(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqtestcc2Reset(TQTestCC2* self);
#define tqtestcc2Reset(self) (self)->_->reset(TQTestCC2(self))

typedef struct TQTestDefer {
    union {
        TQTestDefer_ClassIf* _;
        void* _is_TQTestDefer;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    cchain oncomplete;        // functions that are called when this task has completed
    Event* notify;
} TQTestDefer;
extern ObjClassInfo TQTestDefer_clsinfo;
#define TQTestDefer(inst) ((TQTestDefer*)(unused_noeval((inst) && &((inst)->_is_TQTestDefer)), (inst)))
#define TQTestDeferNone ((TQTestDefer*)NULL)

typedef struct TQTestDefer_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestDefer_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestDefer_WeakRef;
#define TQTestDefer_WeakRef(inst) ((TQTestDefer_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestDefer_WeakRef)), (inst)))

// bool tqtestdeferAdvance(TQTestDefer* self);
//
// advance a deferred task to run as soon as possible
#define tqtestdeferAdvance(self) Task_advance(Task(self))

// bool tqtestdeferRun(TQTestDefer* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqtestdeferRun(self, tq, worker, tcon) (self)->_->run(TQTestDefer(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqtestdeferReset(TQTestDefer* self);
#define tqtestdeferReset(self) (self)->_->reset(TQTestDefer(self))

typedef struct TQTestD1 {
    union {
        TQTestD1_ClassIf* _;
        void* _is_TQTestD1;
        void* _is_TQTestDefer;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    cchain oncomplete;        // functions that are called when this task has completed
    Event* notify;
    int order;
    int64 dtime;
    int64 rantime;
} TQTestD1;
extern ObjClassInfo TQTestD1_clsinfo;
#define TQTestD1(inst) ((TQTestD1*)(unused_noeval((inst) && &((inst)->_is_TQTestD1)), (inst)))
#define TQTestD1None ((TQTestD1*)NULL)

typedef struct TQTestD1_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestD1_WeakRef;
        void* _is_TQTestDefer_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestD1_WeakRef;
#define TQTestD1_WeakRef(inst) ((TQTestD1_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestD1_WeakRef)), (inst)))

_objfactory_guaranteed TQTestD1* TQTestD1_create(int order, int64 dtime, Event* notify);
// TQTestD1* tqtestd1Create(int order, int64 dtime, Event* notify);
#define tqtestd1Create(order, dtime, notify) TQTestD1_create(order, dtime, notify)

// bool tqtestd1Advance(TQTestD1* self);
//
// advance a deferred task to run as soon as possible
#define tqtestd1Advance(self) Task_advance(Task(self))

// bool tqtestd1Run(TQTestD1* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqtestd1Run(self, tq, worker, tcon) (self)->_->run(TQTestD1(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqtestd1Reset(TQTestD1* self);
#define tqtestd1Reset(self) (self)->_->reset(TQTestD1(self))

typedef struct TQTestD2 {
    union {
        TQTestD2_ClassIf* _;
        void* _is_TQTestD2;
        void* _is_TQTestDefer;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    cchain oncomplete;        // functions that are called when this task has completed
    Event* notify;
    Task* waitfor;
} TQTestD2;
extern ObjClassInfo TQTestD2_clsinfo;
#define TQTestD2(inst) ((TQTestD2*)(unused_noeval((inst) && &((inst)->_is_TQTestD2)), (inst)))
#define TQTestD2None ((TQTestD2*)NULL)

typedef struct TQTestD2_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQTestD2_WeakRef;
        void* _is_TQTestDefer_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQTestD2_WeakRef;
#define TQTestD2_WeakRef(inst) ((TQTestD2_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQTestD2_WeakRef)), (inst)))

_objfactory_guaranteed TQTestD2* TQTestD2_create(Task* waitfor, Event* notify);
// TQTestD2* tqtestd2Create(Task* waitfor, Event* notify);
#define tqtestd2Create(waitfor, notify) TQTestD2_create(Task(waitfor), notify)

// bool tqtestd2Advance(TQTestD2* self);
//
// advance a deferred task to run as soon as possible
#define tqtestd2Advance(self) Task_advance(Task(self))

// bool tqtestd2Run(TQTestD2* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqtestd2Run(self, tq, worker, tcon) (self)->_->run(TQTestD2(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqtestd2Reset(TQTestD2* self);
#define tqtestd2Reset(self) (self)->_->reset(TQTestD2(self))

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

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
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

// bool tqdelaytestAdvance(TQDelayTest* self);
//
// advance a deferred task to run as soon as possible
#define tqdelaytestAdvance(self) Task_advance(Task(self))

// bool tqdelaytestRun(TQDelayTest* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqdelaytestRun(self, tq, worker, tcon) (self)->_->run(TQDelayTest(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqdelaytestReset(TQDelayTest* self);
#define tqdelaytestReset(self) (self)->_->reset(TQDelayTest(self))

typedef struct TQMTest {
    union {
        TQMTest_ClassIf* _;
        void* _is_TQMTest;
        void* _is_MTask;
        void* _is_Task;
        void* _is_BasicTask;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(int32) state;
    atomic(bool) cancelled;        // request that the task should be cancelled
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    Weak(TaskQueue)* lastq;        // The last queue this task ran on before it was deferred
    cchain oncomplete;        // functions that are called when this task has completed
    TaskQueue* tq;        // Queue to submit tasks to if they need to be run
    int32 limit;        // If queueing tasks, only queue this many at once
    atomic(int32) _nfinished;        // internal tracking, expected size of finished array
    Mutex _newlock;        // Mutex protecting _new
    int32 _newcursor;        // How far in the new task lists we've progressed
    sa_Task _new;        // Tasks that have been added to run (private)
    sa_Task _pending;        // List of tasks this MTask is waiting on (private)
    sa_Task finished;        // Tasks go here once they're finished
    int64 maxprogress;        // aggregate progress timestamp
    atomic(bool) alldone;        // cached state if all tasks are complete
    bool failed;        // true if any tasks failed
    Event* notify;
} TQMTest;
extern ObjClassInfo TQMTest_clsinfo;
#define TQMTest(inst) ((TQMTest*)(unused_noeval((inst) && &((inst)->_is_TQMTest)), (inst)))
#define TQMTestNone ((TQMTest*)NULL)

typedef struct TQMTest_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQMTest_WeakRef;
        void* _is_MTask_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQMTest_WeakRef;
#define TQMTest_WeakRef(inst) ((TQMTest_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQMTest_WeakRef)), (inst)))

_objfactory_guaranteed TQMTest* TQMTest_create(Event* notify, TaskQueue* tq, int limit);
// TQMTest* tqmtestCreate(Event* notify, TaskQueue* tq, int limit);
#define tqmtestCreate(notify, tq, limit) TQMTest_create(notify, TaskQueue(tq), limit)

// bool tqmtestAdvance(TQMTest* self);
//
// advance a deferred task to run as soon as possible
#define tqmtestAdvance(self) Task_advance(Task(self))

// bool tqmtestRun(TQMTest* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define tqmtestRun(self, tq, worker, tcon) (self)->_->run(TQMTest(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool tqmtestReset(TQMTest* self);
#define tqmtestReset(self) (self)->_->reset(TQMTest(self))
// void tqmtestAdd(TQMTest* self, Task* task);
//
// Add a task
#define tqmtestAdd(self, task) (self)->_->add(TQMTest(self), Task(task))
// bool tqmtest_cycle(TQMTest* self);
//
// Run cycle of checking / queueing tasks as needed (private)
#define tqmtest_cycle(self) (self)->_->_cycle(TQMTest(self))

