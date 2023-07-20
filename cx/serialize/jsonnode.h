#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/ssdnode.h>

typedef struct JSONNode JSONNode;
saDeclarePtr(JSONNode);

typedef struct JSONNode_ClassIf {
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
    // This is called internally by getChild when it needs to create a new node.
    // It can be overridden by child classes to avoid having to re-implement all of getChild.
    SSDNode *(*createLike)(void *self, SSDInfo *info);
} JSONNode_ClassIf;
extern JSONNode_ClassIf JSONNode_ClassIf_tmpl;

typedef struct JSONNode {
    JSONNode_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_JSONNode;
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
    sa_string keyorder;        // order of keys in the children hashtable
} JSONNode;
extern ObjClassInfo JSONNode_clsinfo;
#define JSONNode(inst) ((JSONNode*)((void)((inst) && &((inst)->_is_JSONNode)), (inst)))
#define JSONNodeNone ((JSONNode*)NULL)

JSONNode *JSONNode_create(SSDInfo *info);
// JSONNode *jsonnodeCreate(SSDInfo *info);
#define jsonnodeCreate(info) JSONNode_create(SSDInfo(info))

// SSDNode *jsonnodeGetChild(JSONNode *self, strref name, bool create, SSDLock *lock);
//
// Retrieves a child node with the given name, creating it if create is set
#define jsonnodeGetChild(self, name, create, lock) (self)->_->getChild(JSONNode(self), name, create, lock)
// bool jsonnodeGetValue(JSONNode *self, strref name, stvar *out, SSDLock *lock);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define jsonnodeGetValue(self, name, out, lock) (self)->_->getValue(JSONNode(self), name, out, lock)
// stvar *jsonnodeGetPtr(JSONNode *self, strref name, SSDLock *lock);
//
// Gets a pointer to a value. This points to the hastable entry in children,
// so it is only guaranteed to be valid while the read lock is held.
#define jsonnodeGetPtr(self, name, lock) (self)->_->getPtr(JSONNode(self), name, lock)
// void jsonnodeSetValue(JSONNode *self, strref name, stvar val, SSDLock *lock);
//
// Sets the given value
#define jsonnodeSetValue(self, name, val, lock) (self)->_->setValue(JSONNode(self), name, val, lock)
// void jsonnodeSetValueC(JSONNode *self, strref name, stvar *val, SSDLock *lock);
//
// Same as setValue but consumes the value
#define jsonnodeSetValueC(self, name, val, lock) (self)->_->setValueC(JSONNode(self), name, val, lock)
// bool jsonnodeRemoveValue(JSONNode *self, strref name, SSDLock *lock);
//
// Remove a value or child node
#define jsonnodeRemoveValue(self, name, lock) (self)->_->removeValue(JSONNode(self), name, lock)
// SSDNode *jsonnodeCreateLike(JSONNode *self, SSDInfo *info);
//
// This is called internally by getChild when it needs to create a new node.
// It can be overridden by child classes to avoid having to re-implement all of getChild.
#define jsonnodeCreateLike(self, info) (self)->_->createLike(JSONNode(self), SSDInfo(info))

