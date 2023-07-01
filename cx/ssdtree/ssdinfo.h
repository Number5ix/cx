#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/ssdshared.h>

typedef struct SSDInfo SSDInfo;
saDeclarePtr(SSDInfo);

typedef struct SSDInfo {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDInfo;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    RWLock lock;
    uint32 flags;
} SSDInfo;
extern ObjClassInfo SSDInfo_clsinfo;
#define SSDInfo(inst) ((SSDInfo*)((void)((inst) && &((inst)->_is_SSDInfo)), (inst)))
#define SSDInfoNone ((SSDInfo*)NULL)

SSDInfo *SSDInfo_create(uint32 flags);
#define ssdinfoCreate(flags) SSDInfo_create(flags)

