#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
// clang-format off
#include <cx/obj.h>
#include "tqmanager.h"
#include <cx/taskqueue/worker/tqthreadworker.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TaskQueue TaskQueue;
typedef struct TaskQueue_WeakRef TaskQueue_WeakRef;
typedef struct TQThreadPoolRunner TQThreadPoolRunner;
typedef struct TQThreadPoolRunner_WeakRef TQThreadPoolRunner_WeakRef;
typedef struct TQThreadPoolManager TQThreadPoolManager;
typedef struct TQThreadPoolManager_WeakRef TQThreadPoolManager_WeakRef;
saDeclarePtr(TQThreadPoolManager);
saDeclarePtr(TQThreadPoolManager_WeakRef);

typedef struct TQThreadPoolManager_ClassIf {
    ObjIface* _implements;
    ObjIface* _parent;
    size_t _size;

    bool (*start)(_Inout_ void* self, _In_ TaskQueue* tq);
    bool (*stop)(_Inout_ void* self);
    void (*notify)(_Inout_ void* self);
    int64 (*tick)(_Inout_ void* self);
    void (*updatePoolSize)(_Inout_ void* self);
} TQThreadPoolManager_ClassIf;
extern TQThreadPoolManager_ClassIf TQThreadPoolManager_ClassIf_tmpl;

typedef struct TQThreadPoolManager {
    union {
        TQThreadPoolManager_ClassIf* _;
        void* _is_TQThreadPoolManager;
        void* _is_TQManager;
        void* _is_ObjInst;
    };
    ObjClassInfo* _clsinfo;
    atomic(intptr) _ref;
    atomic(ptr) _weakref;

    TaskQueue* tq;
    bool needsWorkerTick;        // true if this manager needs to be ticked by the worker
    TQThreadPoolRunner* runner;
    uint32 lastcount;
    int64 lastop;
    int64 idlestart;
} TQThreadPoolManager;
extern ObjClassInfo TQThreadPoolManager_clsinfo;
#define TQThreadPoolManager(inst) ((TQThreadPoolManager*)(unused_noeval((inst) && &((inst)->_is_TQThreadPoolManager)), (inst)))
#define TQThreadPoolManagerNone ((TQThreadPoolManager*)NULL)

typedef struct TQThreadPoolManager_WeakRef {
    union {
        ObjInst* _inst;
        void* _is_TQThreadPoolManager_WeakRef;
        void* _is_TQManager_WeakRef;
        void* _is_ObjInst_WeakRef;
    };
    atomic(intptr) _ref;
    RWLock _lock;
} TQThreadPoolManager_WeakRef;
#define TQThreadPoolManager_WeakRef(inst) ((TQThreadPoolManager_WeakRef*)(unused_noeval((inst) && &((inst)->_is_TQThreadPoolManager_WeakRef)), (inst)))

// bool tqthreadpoolmanagerStart(TQThreadPoolManager* self, TaskQueue* tq);
#define tqthreadpoolmanagerStart(self, tq) (self)->_->start(TQThreadPoolManager(self), TaskQueue(tq))
// bool tqthreadpoolmanagerStop(TQThreadPoolManager* self);
#define tqthreadpoolmanagerStop(self) (self)->_->stop(TQThreadPoolManager(self))
// void tqthreadpoolmanagerNotify(TQThreadPoolManager* self);
#define tqthreadpoolmanagerNotify(self) (self)->_->notify(TQThreadPoolManager(self))
// int64 tqthreadpoolmanagerTick(TQThreadPoolManager* self);
#define tqthreadpoolmanagerTick(self) (self)->_->tick(TQThreadPoolManager(self))
// void tqthreadpoolmanagerUpdatePoolSize(TQThreadPoolManager* self);
#define tqthreadpoolmanagerUpdatePoolSize(self) (self)->_->updatePoolSize(TQThreadPoolManager(self))

