#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include <cx/taskqueue/tqobject.h>
#include <cx/taskqueue/task.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct MTask MTask;
typedef struct MTask_WeakRef MTask_WeakRef;
saDeclarePtr(MTask);
saDeclarePtr(MTask_WeakRef);

#define mtaskAllDone(mtask) atomicLoad(bool, &(MTask(mtask)->alldone), Acquire)

typedef struct MTask_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TaskQueueWorker* worker, _Inout_ TaskControl* tcon);
    bool (*reset)(_Inout_ void* self);
    // Add a task
    void (*add)(_Inout_ void* self, _In_ Task* task);
    // Run cycle of checking / queueing tasks as needed (private)
    bool (*_cycle)(_Inout_ void* self);
} MTask_ClassIf;
extern MTask_ClassIf MTask_ClassIf_tmpl;

typedef struct MTask {
    union {
        MTask_ClassIf* _;
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
} MTask;
extern ObjClassInfo MTask_clsinfo;
#define MTask(inst) ((MTask*)(unused_noeval((inst) && &((inst)->_is_MTask)), (inst)))
#define MTaskNone ((MTask*)NULL)

typedef struct MTask_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_MTask_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} MTask_WeakRef;
#define MTask_WeakRef(inst) ((MTask_WeakRef*)(unused_noeval((inst) && &((inst)->_is_MTask_WeakRef)), (inst)))

_objfactory_guaranteed MTask* MTask_create();
// MTask* mtaskCreate();
#define mtaskCreate() MTask_create()

_objfactory_guaranteed MTask* MTask_createWithQueue(_In_ TaskQueue* tq, int limit);
// MTask* mtaskCreateWithQueue(TaskQueue* tq, int limit);
#define mtaskCreateWithQueue(tq, limit) MTask_createWithQueue(TaskQueue(tq), limit)

// bool mtaskAdvance(MTask* self);
//
// advance a deferred task to run as soon as possible
#define mtaskAdvance(self) Task_advance(Task(self))

// bool mtaskRun(MTask* self, TaskQueue* tq, TaskQueueWorker* worker, TaskControl* tcon);
#define mtaskRun(self, tq, worker, tcon) (self)->_->run(MTask(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool mtaskReset(MTask* self);
#define mtaskReset(self) (self)->_->reset(MTask(self))
// void mtaskAdd(MTask* self, Task* task);
//
// Add a task
#define mtaskAdd(self, task) (self)->_->add(MTask(self), Task(task))
// bool mtask_cycle(MTask* self);
//
// Run cycle of checking / queueing tasks as needed (private)
#define mtask_cycle(self) (self)->_->_cycle(MTask(self))

