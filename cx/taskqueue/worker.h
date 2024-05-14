#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/task.h>
#include <cx/thread.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
saDeclarePtr(TaskQueueWorker);
saDeclarePtr(TaskQueueWorker_WeakRef);

typedef struct TaskQueueWorker_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*_startThread)(_Inout_ void *self, TaskQueue *tq);
    // hooks for derived classes to override if desired
    // worker is starting up
    void (*startup)(_Inout_ void *self, TaskQueue *tq);
    // worker should process tasks (call parent!)
    void (*tick)(_Inout_ void *self, TaskQueue *tq, TQUICallback ui);
    // worker is shutting down
    void (*shutdown)(_Inout_ void *self, TaskQueue *tq);
} TaskQueueWorker_ClassIf;
extern TaskQueueWorker_ClassIf TaskQueueWorker_ClassIf_tmpl;

typedef struct TaskQueueWorker {
    union {
        TaskQueueWorker_ClassIf *_;
        void *_is_TaskQueueWorker;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    Thread *thr;        // worker thread
    atomic(ptr) curtask;        // this worker's currently running task
    int32 num;        // worker number
    bool shutdown;
} TaskQueueWorker;
extern ObjClassInfo TaskQueueWorker_clsinfo;
#define TaskQueueWorker(inst) ((TaskQueueWorker*)(unused_noeval((inst) && &((inst)->_is_TaskQueueWorker)), (inst)))
#define TaskQueueWorkerNone ((TaskQueueWorker*)NULL)

typedef struct TaskQueueWorker_WeakRef {
    union {
        ObjInst *_inst;
        void *_is_TaskQueueWorker_WeakRef;
        void *_is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TaskQueueWorker_WeakRef;
#define TaskQueueWorker_WeakRef(inst) ((TaskQueueWorker_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TaskQueueWorker_WeakRef)), (inst)))

_objfactory_guaranteed TaskQueueWorker *TaskQueueWorker_create(int32 num);
// TaskQueueWorker *taskqueueworkerCreate(int32 num);
#define taskqueueworkerCreate(num) TaskQueueWorker_create(num)

// bool taskqueueworker_startThread(TaskQueueWorker *self, TaskQueue *tq);
#define taskqueueworker_startThread(self, tq) (self)->_->_startThread(TaskQueueWorker(self), TaskQueue(tq))
// void taskqueueworkerStartup(TaskQueueWorker *self, TaskQueue *tq);
//
// hooks for derived classes to override if desired
// worker is starting up
#define taskqueueworkerStartup(self, tq) (self)->_->startup(TaskQueueWorker(self), TaskQueue(tq))
// void taskqueueworkerTick(TaskQueueWorker *self, TaskQueue *tq, TQUICallback ui);
//
// worker should process tasks (call parent!)
#define taskqueueworkerTick(self, tq, ui) (self)->_->tick(TaskQueueWorker(self), TaskQueue(tq), ui)
// void taskqueueworkerShutdown(TaskQueueWorker *self, TaskQueue *tq);
//
// worker is shutting down
#define taskqueueworkerShutdown(self, tq) (self)->_->shutdown(TaskQueueWorker(self), TaskQueue(tq))

