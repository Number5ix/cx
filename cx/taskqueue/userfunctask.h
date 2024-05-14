#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/basictask.h>
#include <cx/taskqueue/taskqueue.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueueWorker TaskQueueWorker;
typedef struct TaskQueueWorker_WeakRef TaskQueueWorker_WeakRef;
typedef struct TaskControl TaskControl;
typedef struct UserFuncTask UserFuncTask;
typedef struct UserFuncTask_WeakRef UserFuncTask_WeakRef;
saDeclarePtr(UserFuncTask);
saDeclarePtr(UserFuncTask_WeakRef);

typedef struct UserFuncTask_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*run)(_Inout_ void *self, _In_ TaskQueue *tq, _In_ TaskQueueWorker *worker, _Inout_ TaskControl *tcon);
    bool (*reset)(_Inout_ void *self);
} UserFuncTask_ClassIf;
extern UserFuncTask_ClassIf UserFuncTask_ClassIf_tmpl;

typedef struct UserFuncTask {
    union {
        UserFuncTask_ClassIf *_;
        void *_is_UserFuncTask;
        void *_is_BasicTask;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    atomic(int32) state;
    UserTaskCB func;
    void *udata;
} UserFuncTask;
extern ObjClassInfo UserFuncTask_clsinfo;
#define UserFuncTask(inst) ((UserFuncTask*)(unused_noeval((inst) && &((inst)->_is_UserFuncTask)), (inst)))
#define UserFuncTaskNone ((UserFuncTask*)NULL)

typedef struct UserFuncTask_WeakRef {
    union {
        ObjInst *_inst;
        void *_is_UserFuncTask_WeakRef;
        void *_is_BasicTask_WeakRef;
        void *_is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} UserFuncTask_WeakRef;
#define UserFuncTask_WeakRef(inst) ((UserFuncTask_WeakRef*)(unused_noeval((inst) && &((inst)->_is_UserFuncTask_WeakRef)), (inst)))

_objfactory_guaranteed UserFuncTask *UserFuncTask_create(UserTaskCB func, void *udata);
// UserFuncTask *userfunctaskCreate(UserTaskCB func, void *udata);
#define userfunctaskCreate(func, udata) UserFuncTask_create(func, udata)

// bool userfunctaskRun(UserFuncTask *self, TaskQueue *tq, TaskQueueWorker *worker, TaskControl *tcon);
#define userfunctaskRun(self, tq, worker, tcon) (self)->_->run(UserFuncTask(self), TaskQueue(tq), TaskQueueWorker(worker), tcon)
// bool userfunctaskReset(UserFuncTask *self);
#define userfunctaskReset(self) (self)->_->reset(UserFuncTask(self))

