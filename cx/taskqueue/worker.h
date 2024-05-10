#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/task.h>
#include <cx/thread.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskControl TaskControl;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
saDeclarePtr(TaskQueueWorker);
saDeclarePtr(TaskQueueWorker_WeakRef);

typedef struct TaskQueueWorker_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*start)(_Inout_ void *self, TaskQueue *tq);
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

// bool taskqueueworkerStart(TaskQueueWorker *self, TaskQueue *tq);
#define taskqueueworkerStart(self, tq) (self)->_->start(TaskQueueWorker(self), tq)

