#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/taskqueue_shared.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct BasicTask BasicTask;
typedef struct BasicTask_WeakRef BasicTask_WeakRef;
saDeclarePtr(BasicTask);
saDeclarePtr(BasicTask_WeakRef);

typedef struct BasicTask_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*run)(_Inout_ void *self, _In_ TaskQueue *tq, _In_ TaskQueueWorker *worker, _Inout_ TaskControl *tcon);
    bool (*reset)(_Inout_ void *self);
} BasicTask_ClassIf;
extern BasicTask_ClassIf BasicTask_ClassIf_tmpl;

typedef struct BasicTask {
    union {
        BasicTask_ClassIf *_;
        void *_is_BasicTask;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(int32) state;
} BasicTask;
extern ObjClassInfo BasicTask_clsinfo;
#define BasicTask(inst) ((BasicTask*)(unused_noeval((inst) && &((inst)->_is_BasicTask)), (inst)))
#define BasicTaskNone ((BasicTask*)NULL)

typedef struct BasicTask_WeakRef {
    union {
        ObjInst *_inst;
        void *_is_BasicTask_WeakRef;
        void *_is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} BasicTask_WeakRef;
#define BasicTask_WeakRef(inst) ((BasicTask_WeakRef*)(unused_noeval((inst) && &((inst)->_is_BasicTask_WeakRef)), (inst)))

// bool btaskRun(BasicTask *self, TaskQueue *tq, TaskQueueWorker *worker, TaskControl *tcon);
#define btaskRun(self, tq, worker, tcon) (self)->_->run(BasicTask(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool btaskReset(BasicTask *self);
#define btaskReset(self) (self)->_->reset(BasicTask(self))

