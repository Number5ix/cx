#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/basictask.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskResult TaskResult;
typedef struct Task Task;
saDeclarePtr(Task);

typedef struct Task_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    void (*run)(_Inout_ void *self, _In_ TaskQueue *tq, _Inout_ TaskResult *result);
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

    atomic(int32) state;
    string name;        // task name to be shown in monitor output
    int64 lastrun;        // the last time this task was run on a worker
    int64 nextrun;        // next time for this task to run when deferred
    uint32 progress;        // how much forward progress this task has made
    uint32 _m_last_progress;        // internal use for the queue monitor to detect progress changes
    atomic(bool) cancelled;        // request that the task should be cancelled
} Task;
extern ObjClassInfo Task_clsinfo;
#define Task(inst) ((Task*)(unused_noeval((inst) && &((inst)->_is_Task)), (inst)))
#define TaskNone ((Task*)NULL)

// void taskRun(Task *self, TaskQueue *tq, TaskResult *result);
#define taskRun(self, tq, result) (self)->_->run(Task(self), tq, result)

