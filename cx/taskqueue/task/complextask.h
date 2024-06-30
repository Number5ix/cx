#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include "task.h"
#include <cx/thread/rwlock.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TQWorker TQWorker;
typedef struct TQWorker_WeakRef TQWorker_WeakRef;
typedef struct ComplexTaskQueue ComplexTaskQueue;
typedef struct ComplexTaskQueue_WeakRef ComplexTaskQueue_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct ComplexTask ComplexTask;
typedef struct ComplexTask_WeakRef ComplexTask_WeakRef;
saDeclarePtr(ComplexTask);
saDeclarePtr(ComplexTask_WeakRef);

enum ComplexTaskRunResultEnum {
    TASK_Result_Schedule = TASK_Result_Basic_Count,     // schedule the task to run after the time specified in the TaskControl structure
    TASK_Result_Schedule_Progress,                      // schedule the task and mark it has having made progress
    TASK_Result_Defer,                                  // defer the task until it is advanced
    TASK_Result_Defer_Progress                          // defer the task and mark it has having made progress
};

typedef struct ComplexTask_ClassIf {
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
} ComplexTask_ClassIf;
extern ComplexTask_ClassIf ComplexTask_ClassIf_tmpl;

typedef struct ComplexTask {
    union {
        ComplexTask_ClassIf* _;
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
} ComplexTask;
extern ObjClassInfo ComplexTask_clsinfo;
#define ComplexTask(inst) ((ComplexTask*)(unused_noeval((inst) && &((inst)->_is_ComplexTask)), (inst)))
#define ComplexTaskNone ((ComplexTask*)NULL)

typedef struct ComplexTask_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_ComplexTask_WeakRef;
        void* _is_Task_WeakRef;
        void* _is_BasicTask_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} ComplexTask_WeakRef;
#define ComplexTask_WeakRef(inst) ((ComplexTask_WeakRef*)(unused_noeval((inst) && &((inst)->_is_ComplexTask_WeakRef)), (inst)))

bool ComplexTask_advance(_Inout_ ComplexTask* self);
// bool ctaskAdvance(ComplexTask* self);
//
// advance a deferred task to run as soon as possible
#define ctaskAdvance(self) ComplexTask_advance(ComplexTask(self))

bool ComplexTask_checkDeps(_Inout_ ComplexTask* self, bool updateProgress);
// bool ctaskCheckDeps(ComplexTask* self, bool updateProgress);
//
// check if this task can run because all dependencies are satisfied
#define ctaskCheckDeps(self, updateProgress) ComplexTask_checkDeps(ComplexTask(self), updateProgress)

// bool ctask_setState(ComplexTask* self, uint32 newstate);
#define ctask_setState(self, newstate) BasicTask__setState(BasicTask(self), newstate)

// uint32 ctaskRun(ComplexTask* self, TaskQueue* tq, TQWorker* worker, TaskControl* tcon);
#define ctaskRun(self, tq, worker, tcon) (self)->_->run(ComplexTask(self), TaskQueue(tq), TQWorker(worker), tcon)
// bool ctaskCancel(ComplexTask* self);
#define ctaskCancel(self) (self)->_->cancel(ComplexTask(self))
// bool ctaskReset(ComplexTask* self);
#define ctaskReset(self) (self)->_->reset(ComplexTask(self))
// bool ctaskWait(ComplexTask* self, int64 timeout);
#define ctaskWait(self, timeout) (self)->_->wait(ComplexTask(self), timeout)
// void ctaskDependOn(ComplexTask* self, Task* dep);
#define ctaskDependOn(self, dep) (self)->_->dependOn(ComplexTask(self), Task(dep))
// intptr ctaskCmp(ComplexTask* self, ComplexTask* other, uint32 flags);
#define ctaskCmp(self, other, flags) (self)->_->cmp(ComplexTask(self), other, flags)
// uint32 ctaskHash(ComplexTask* self, uint32 flags);
#define ctaskHash(self, flags) (self)->_->hash(ComplexTask(self), flags)

