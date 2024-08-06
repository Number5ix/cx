#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include "task.h"
#include <cx/thread/rwlock.h>
#include <cx/taskqueue/requires/taskrequires.h>
#include <cx/taskqueue/resource/taskresource.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TQWorker TQWorker;
typedef struct TQWorker_WeakRef TQWorker_WeakRef;
typedef struct ComplexTask ComplexTask;
typedef struct ComplexTask_WeakRef ComplexTask_WeakRef;
typedef struct ComplexTask ComplexTask;
typedef struct ComplexTask_WeakRef ComplexTask_WeakRef;
typedef struct TRGate TRGate;
typedef struct TRGate_WeakRef TRGate_WeakRef;
typedef struct ComplexTaskQueue ComplexTaskQueue;
typedef struct ComplexTaskQueue_WeakRef ComplexTaskQueue_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct ComplexTask ComplexTask;
typedef struct ComplexTask_WeakRef ComplexTask_WeakRef;
saDeclarePtr(ComplexTask);
saDeclarePtr(ComplexTask_WeakRef);

#define ctaskDependOn ctaskRequireTask
enum ComplexTaskRunResultEnum {
    TASK_Result_Schedule = TASK_Result_Basic_Count,     // schedule the task to run after the time specified in the TaskControl structure
    TASK_Result_Schedule_Progress,                      // schedule the task and mark it has having made progress
    TASK_Result_Defer,                                  // defer the task until it is advanced
    TASK_Result_Defer_Progress,                         // defer the task and mark it has having made progress
    TASK_Result_Complex_Count
};

enum ComplexTaskFlagsEnum {
    TASK_Cancel_Cascade = 0x01,
    TASK_Retain_Requires = 0x02,
};

enum ComplexTaskInternalFlagsEnum {
    TASK_INTERNAL_Needs_Resources = 0x01,   // The _requires list contains shared resources that must be acquired before running the task
    TASK_INTERNAL_Owns_Resources = 0x02,    // The task has acquired resources and needs to release them
};

typedef struct ComplexTask_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    uint32 (*run)(_Inout_ void* self, _In_ TaskQueue* tq, _In_ TQWorker* worker, _Inout_ TaskControl* tcon);
    bool (*cancel)(_Inout_ void* self);
    bool (*reset)(_Inout_ void* self);
    bool (*wait)(_Inout_ void* self, int64 timeout);
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
    sa_TaskRequires _requires;        // list of requirements that must be satisfied
    uint16 flags;        // flags to customize task behavior
    uint16 _intflags;        // internal flags reserved for use by the scheduler
    atomic(uint32) _advcount;        // number of times this task has been advanced
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

void ComplexTask_requireTask(_Inout_ ComplexTask* self, _In_ Task* dep);
// void ctaskRequireTask(ComplexTask* self, Task* dep);
//
// Wrapper around require() to depend on a task completing
#define ctaskRequireTask(self, dep) ComplexTask_requireTask(ComplexTask(self), Task(dep))

void ComplexTask_requireResource(_Inout_ ComplexTask* self, _In_ TaskResource* res);
// void ctaskRequireResource(ComplexTask* self, TaskResource* res);
//
// Wrapper around require() to depend on acquiring a resource
#define ctaskRequireResource(self, res) ComplexTask_requireResource(ComplexTask(self), TaskResource(res))

void ComplexTask_requireGate(_Inout_ ComplexTask* self, _In_ TRGate* gate);
// void ctaskRequireGate(ComplexTask* self, TRGate* gate);
//
// Wrapper around require() to depend on a gate being opened
#define ctaskRequireGate(self, gate) ComplexTask_requireGate(ComplexTask(self), TRGate(gate))

void ComplexTask_require(_Inout_ ComplexTask* self, _In_ TaskRequires* req);
// void ctaskRequire(ComplexTask* self, TaskRequires* req);
//
// Add a requirement for the task to run
#define ctaskRequire(self, req) ComplexTask_require(ComplexTask(self), TaskRequires(req))

bool ComplexTask_advance(_Inout_ ComplexTask* self);
// bool ctaskAdvance(ComplexTask* self);
//
// advance a deferred task to run as soon as possible
#define ctaskAdvance(self) ComplexTask_advance(ComplexTask(self))

bool ComplexTask_checkRequires(_Inout_ ComplexTask* self, bool updateProgress);
// bool ctaskCheckRequires(ComplexTask* self, bool updateProgress);
//
// check if this task can run because all requirements are satisfied
#define ctaskCheckRequires(self, updateProgress) ComplexTask_checkRequires(ComplexTask(self), updateProgress)

bool ComplexTask_acquireRequires(_Inout_ ComplexTask* self, sa_TaskRequires* acquired);
// bool ctaskAcquireRequires(ComplexTask* self, sa_TaskRequires* acquired);
//
// try to acquire required resources
#define ctaskAcquireRequires(self, acquired) ComplexTask_acquireRequires(ComplexTask(self), acquired)

bool ComplexTask_releaseRequires(_Inout_ ComplexTask* self, sa_TaskRequires resources);
// bool ctaskReleaseRequires(ComplexTask* self, sa_TaskRequires resources);
//
// release a list of acquired resources
#define ctaskReleaseRequires(self, resources) ComplexTask_releaseRequires(ComplexTask(self), resources)

bool ComplexTask_advanceCallback(stvlist* cvars, stvlist* args);
// bool ctaskAdvanceCallback(stvlist* cvars, stvlist* args);
//
// callback to be used in a closure; cvars = weak reference to the task to advance
#define ctaskAdvanceCallback(cvars, args) ComplexTask_advanceCallback(cvars, args)

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
// intptr ctaskCmp(ComplexTask* self, ComplexTask* other, uint32 flags);
#define ctaskCmp(self, other, flags) (self)->_->cmp(ComplexTask(self), other, flags)
// uint32 ctaskHash(ComplexTask* self, uint32 flags);
#define ctaskHash(self, flags) (self)->_->hash(ComplexTask(self), flags)

