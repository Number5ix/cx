#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/taskqueue_shared.h>
#include <cx/taskqueue/worker.h>
#include <cx/thread/event.h>
#include <cx/thread/thread.h>
#include <cx/thread/prqueue.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
saDeclarePtr(TaskQueue);
saDeclarePtr(TaskQueue_WeakRef);

typedef enum TaskQueueStateEnum {
    TQState_Init,
    TQState_Starting,
    TQState_Running,
    TQState_Stopping,
    TQState_Shutdown,
} TaskQueueStateEnum;

typedef struct TaskQueue_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*start)(_Inout_ void* self, Event* notify);
    // worker factory for custom queues to override
    _objfactory_guaranteed TaskQueueWorker* (*createWorker)(_Inout_ void* self, int32 num);
} TaskQueue_ClassIf;
extern TaskQueue_ClassIf TaskQueue_ClassIf_tmpl;

typedef struct TaskQueue {
    union {
        TaskQueue_ClassIf *_;
        void *_is_TaskQueue;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    string name;
    TaskQueueConfig tqconfig;
    // State should *probably* be an atomic that is only modified via CAS,
    // but for now we assume callers are reasonably well-behaved and don't try to do stupid
    // things like start/stop the same queue concurrently from many different threads.
    TaskQueueStateEnum state;
    atomic(int32) nworkers;
    Thread* manager;
    sa_TaskQueueWorker workers;        // [owned by manager]
    Event workev;        // signaled when there is work to be done
    Event shutdownev;        // signaled when queue is finished shutting down
    PrQueue runq;        // tasks that are ready to be picked up by workers
    PrQueue doneq;        // tasks that are either deferred or finished
    PrQueue advanceq;        // tasks that are being advanced out of the defer queue
    sa_ptr deferred;        // tasks that are deferred for later, sorted by defer time [owned by manager]
} TaskQueue;
extern ObjClassInfo TaskQueue_clsinfo;
#define TaskQueue(inst) ((TaskQueue*)(unused_noeval((inst) && &((inst)->_is_TaskQueue)), (inst)))
#define TaskQueueNone ((TaskQueue*)NULL)

typedef struct TaskQueue_WeakRef {
    union {
        ObjInst *_inst;
        void *_is_TaskQueue_WeakRef;
        void *_is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TaskQueue_WeakRef;
#define TaskQueue_WeakRef(inst) ((TaskQueue_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TaskQueue_WeakRef)), (inst)))

_objfactory_guaranteed TaskQueue* TaskQueue_create(_In_opt_ strref name, _In_ TaskQueueConfig* tqconfig);
// TaskQueue* taskqueueCreate(strref name, TaskQueueConfig* tqconfig);
#define taskqueueCreate(name, tqconfig) TaskQueue_create(name, tqconfig)

// bool taskqueueStart(TaskQueue* self, Event* notify);
#define taskqueueStart(self, notify) (self)->_->start(TaskQueue(self), notify)
// TaskQueueWorker* taskqueueCreateWorker(TaskQueue* self, int32 num);
//
// worker factory for custom queues to override
#define taskqueueCreateWorker(self, num) (self)->_->createWorker(TaskQueue(self), num)

