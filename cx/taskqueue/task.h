#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include <cx/taskqueue/basictask.h>
#include <cx/closure.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct Task Task;
typedef struct Task_WeakRef Task_WeakRef;
saDeclarePtr(Task);
saDeclarePtr(Task_WeakRef);

typedef struct Task_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
} Task_ClassIf;
extern Task_ClassIf Task_ClassIf_tmpl;

typedef struct Task {
    union {
        Task_ClassIf *_;
        void *_is_Task;
        void *_is_BasicTask;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
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
} Task;
extern ObjClassInfo Task_clsinfo;
#define Task(inst) ((Task*)(unused_noeval((inst) && &((inst)->_is_Task)), (inst)))
#define TaskNone ((Task*)NULL)

typedef struct Task_WeakRef {
    union {
        ObjInst *_inst;
        void *_is_Task_WeakRef;
        void *_is_BasicTask_WeakRef;
        void *_is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} Task_WeakRef;
#define Task_WeakRef(inst) ((Task_WeakRef*)(unused_noeval((inst) && &((inst)->_is_Task_WeakRef)), (inst)))

bool Task_advance(_Inout_ Task* self);
// bool taskAdvance(Task* self);
//
// advance a deferred task to run as soon as possible
#define taskAdvance(self) Task_advance(Task(self))

// bool taskRun(Task* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define taskRun(self, tq, worker, tcon) (self)->_->run(Task(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool taskReset(Task* self);
#define taskReset(self) (self)->_->reset(Task(self))

