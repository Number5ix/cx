#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/ssdinfo.h>
#include <cx/core/stvar.h>

typedef struct SSDNode SSDNode;
saDeclarePtr(SSDNode);

typedef struct SSDNode {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDInfo *info;
    hashtable children;
    bool singleval;
} SSDNode;
extern ObjClassInfo SSDNode_clsinfo;
#define SSDNode(inst) ((SSDNode*)((void)((inst) && &((inst)->_is_SSDNode)), (inst)))
#define SSDNodeNone ((SSDNode*)NULL)

SSDNode *SSDNode_getChild(SSDNode *self, strref name, bool create, SSDLock *lock);
#define ssdnodeGetChild(self, name, create, lock) SSDNode_getChild(SSDNode(self), name, create, lock)
bool SSDNode_getValue(SSDNode *self, strref name, stvar *out, SSDLock *lock);
#define ssdnodeGetValue(self, name, out, lock) SSDNode_getValue(SSDNode(self), name, out, lock)
stvar *SSDNode_getPtr(SSDNode *self, strref name, SSDLock *lock);
#define ssdnodeGetPtr(self, name, lock) SSDNode_getPtr(SSDNode(self), name, lock)
void SSDNode_setValue(SSDNode *self, strref name, stvar val, SSDLock *lock);
#define ssdnodeSetValue(self, name, val, lock) SSDNode_setValue(SSDNode(self), name, val, lock)
void SSDNode_setValueC(SSDNode *self, strref name, stvar *val, SSDLock *lock);
#define ssdnodeSetValueC(self, name, val, lock) SSDNode_setValueC(SSDNode(self), name, val, lock)
SSDNode *SSDNode_create(SSDInfo *info);
#define ssdnodeCreate(info) SSDNode_create(SSDInfo(info))

