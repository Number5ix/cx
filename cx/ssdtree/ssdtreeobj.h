#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/ssdshared.h>
#include <cx/thread/mutex.h>

typedef struct SSDTree SSDTree;
saDeclarePtr(SSDTree);

#ifdef SSD_LOCK_DEBUG
typedef struct SSDTreeDebug {
    Mutex mtx;
    sa_SSDLockDebug readlocks;
    sa_SSDLockDebug writelocks;
} SSDTreeDebug;
#else
typedef struct SSDTreeDebug {
    uint32 _dummy;
} SSDTreeDebug;
#endif

typedef struct SSDTree_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    SSDNode *(*createNode)(_Inout_ void *self, int crtype);
} SSDTree_ClassIf;
extern SSDTree_ClassIf SSDTree_ClassIf_tmpl;

typedef struct SSDTree {
    union {
        SSDTree_ClassIf *_;
        void *_is_SSDTree;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    RWLock lock;
    SSDTreeDebug dbg;
    uint32 flags;
    int64 modified;        // The most recent last-modified timestamp of any node in the tree
    SSDNodeFactory factories[SSD_Create_Count];        // Factory functions for if this tree wants to use derived node classes
} SSDTree;
extern ObjClassInfo SSDTree_clsinfo;
#define SSDTree(inst) ((SSDTree*)(unused_noeval((inst) && &((inst)->_is_SSDTree)), (inst)))
#define SSDTreeNone ((SSDTree*)NULL)

_objfactory_guaranteed SSDTree *SSDTree_create(uint32 flags);
// SSDTree *ssdtreeCreate(uint32 flags);
#define ssdtreeCreate(flags) SSDTree_create(flags)

// SSDNode *ssdtreeCreateNode(SSDTree *self, int crtype);
#define ssdtreeCreateNode(self, crtype) (self)->_->createNode(SSDTree(self), crtype)

