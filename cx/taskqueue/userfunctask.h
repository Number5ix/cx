#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/taskqueue/basictask.h>
#include <cx/taskqueue/taskqueue.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskResult TaskResult;
typedef struct UserFuncTask UserFuncTask;
saDeclarePtr(UserFuncTask);

typedef struct UserFuncTask_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    void (*run)(_Inout_ void *self, _In_ TaskQueue *tq, _Inout_ TaskResult *result);
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

    atomic(int32) state;
    UserTaskCB func;
    void *udata;
} UserFuncTask;
extern ObjClassInfo UserFuncTask_clsinfo;
#define UserFuncTask(inst) ((UserFuncTask*)(unused_noeval((inst) && &((inst)->_is_UserFuncTask)), (inst)))
#define UserFuncTaskNone ((UserFuncTask*)NULL)

_objfactory_guaranteed UserFuncTask *UserFuncTask_create(UserTaskCB func, void *udata);
// UserFuncTask *userfunctaskCreate(UserTaskCB func, void *udata);
#define userfunctaskCreate(func, udata) UserFuncTask_create(func, udata)

// void userfunctaskRun(UserFuncTask *self, TaskQueue *tq, TaskResult *result);
#define userfunctaskRun(self, tq, result) (self)->_->run(UserFuncTask(self), tq, result)

