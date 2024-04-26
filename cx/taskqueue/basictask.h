#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>

typedef struct TaskQueue TaskQueue;
typedef struct TaskControl TaskControl;
typedef struct BasicTask BasicTask;
saDeclarePtr(BasicTask);

typedef struct BasicTask_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*run)(_Inout_ void *self, _In_ TaskQueue *tq, _Inout_ TaskControl *tcon);
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

    atomic(int32) state;
} BasicTask;
extern ObjClassInfo BasicTask_clsinfo;
#define BasicTask(inst) ((BasicTask*)(unused_noeval((inst) && &((inst)->_is_BasicTask)), (inst)))
#define BasicTaskNone ((BasicTask*)NULL)

// bool btaskRun(BasicTask *self, TaskQueue *tq, TaskControl *tcon);
#define btaskRun(self, tq, tcon) (self)->_->run(BasicTask(self), tq, tcon)

