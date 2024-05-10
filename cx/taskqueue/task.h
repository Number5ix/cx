#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/basictask.h>
#include <cx/closure.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskControl TaskControl;
typedef struct Task Task;
typedef struct Task_WeakRef Task_WeakRef;
saDeclarePtr(Task);
saDeclarePtr(Task_WeakRef);

typedef struct Task_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*run)(_Inout_ void *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon);
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
    string name;        // task name to be shown in monitor output
    int64 last;        // the last time this task was moved between queues and/or run
    int64 nextrun;        // next time for this task to run when deferred
    int64 lastprogress;        // timestamp of last progress change
    atomic(bool) cancelled;        // request that the task should be cancelled
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

// bool taskRun(Task *self, TaskQueue *tq, TaskControl *tcon);
#define taskRun(self, tq, tcon) (self)->_->run(Task(self), tq, tcon)

