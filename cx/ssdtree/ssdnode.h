#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/ssdinfo.h>
#include <cx/core/stvar.h>

typedef struct SSDNode SSDNode;
saDeclarePtr(SSDNode);

typedef struct SSDNode_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    // Retrieves a child node with the given name, creating it if create is set
    SSDNode *(*getChild)(void *self, strref name, bool create, SSDLock *lock);
    // Gets a value. Caller owns the value and must destroy it with stDestroy!
    bool (*getValue)(void *self, strref name, stvar *out, SSDLock *lock);
    // Gets a pointer to a value. This points to the hastable entry in children,
    // so it is only guaranteed to be valid while the read lock is held.
    stvar *(*getPtr)(void *self, strref name, SSDLock *lock);
    // Sets the given value
    void (*setValue)(void *self, strref name, stvar val, SSDLock *lock);
    // Same as setValue but consumes the value
    void (*setValueC)(void *self, strref name, stvar *val, SSDLock *lock);
    // Remove a value or child node
    bool (*removeValue)(void *self, strref name, SSDLock *lock);
} SSDNode_ClassIf;
extern SSDNode_ClassIf SSDNode_ClassIf_tmpl;

typedef struct SSDNode {
    SSDNode_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDInfo *info;
    hashtable children;
    // Represents a tree that is not a tree but a single value at the root.
    // This exists mainly for JSON compatibility; if this is true the value
    // is stored in the children hashtable under the empty string ("")
    bool singleval;
} SSDNode;
extern ObjClassInfo SSDNode_clsinfo;
#define SSDNode(inst) ((SSDNode*)((void)((inst) && &((inst)->_is_SSDNode)), (inst)))
#define SSDNodeNone ((SSDNode*)NULL)

SSDNode *SSDNode_create(SSDInfo *info);
// SSDNode *ssdnodeCreate(SSDInfo *info);
#define ssdnodeCreate(info) SSDNode_create(SSDInfo(info))

// SSDNode *ssdnodeGetChild(SSDNode *self, strref name, bool create, SSDLock *lock);
//
// Retrieves a child node with the given name, creating it if create is set
#define ssdnodeGetChild(self, name, create, lock) (self)->_->getChild(SSDNode(self), name, create, lock)
// bool ssdnodeGetValue(SSDNode *self, strref name, stvar *out, SSDLock *lock);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define ssdnodeGetValue(self, name, out, lock) (self)->_->getValue(SSDNode(self), name, out, lock)
// stvar *ssdnodeGetPtr(SSDNode *self, strref name, SSDLock *lock);
//
// Gets a pointer to a value. This points to the hastable entry in children,
// so it is only guaranteed to be valid while the read lock is held.
#define ssdnodeGetPtr(self, name, lock) (self)->_->getPtr(SSDNode(self), name, lock)
// void ssdnodeSetValue(SSDNode *self, strref name, stvar val, SSDLock *lock);
//
// Sets the given value
#define ssdnodeSetValue(self, name, val, lock) (self)->_->setValue(SSDNode(self), name, val, lock)
// void ssdnodeSetValueC(SSDNode *self, strref name, stvar *val, SSDLock *lock);
//
// Same as setValue but consumes the value
#define ssdnodeSetValueC(self, name, val, lock) (self)->_->setValueC(SSDNode(self), name, val, lock)
// bool ssdnodeRemoveValue(SSDNode *self, strref name, SSDLock *lock);
//
// Remove a value or child node
#define ssdnodeRemoveValue(self, name, lock) (self)->_->removeValue(SSDNode(self), name, lock)

