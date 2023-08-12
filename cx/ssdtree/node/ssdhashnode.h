#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/node/ssdnode.h>

typedef struct SSDHashNode SSDHashNode;
typedef struct SSDHashIter SSDHashIter;
saDeclarePtr(SSDHashNode);
saDeclarePtr(SSDHashIter);

typedef struct SSDHashNode_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    // This node is an object that contains values or objects by name
    bool (*isHashtable)(void *self);
    // This node is an array that contains values or objects by array index
    bool (*isArray)(void *self);
    // Gets a value. Caller owns the value and must destroy it with stDestroy!
    bool (*get)(void *self, int32 idx, strref name, stvar *out, SSDLock *lock);
    // Gets a pointer to a value. This points to the internal storage within the node
    // so it is only guaranteed to be valid while the read lock is held.
    stvar *(*ptr)(void *self, int32 idx, strref name, SSDLock *lock);
    // Sets the given value
    bool (*set)(void *self, int32 idx, strref name, stvar val, SSDLock *lock);
    // Same as setValue but consumes the value
    // (consumes even on failure)
    bool (*setC)(void *self, int32 idx, strref name, stvar *val, SSDLock *lock);
    // Removes a value
    bool (*remove)(void *self, int32 idx, strref name, SSDLock *lock);
    // How many values / objects does this node contain?
    int32 (*count)(void *self, SSDLock *lock);
    // IMPORTANT NOTE: The generic object iterator interface cannot take any parameters;
    // thus it always acquires a transient read lock and holds it until the iterator is
    // destroyed. The caller MUST NOT already have an SSDLock held.
    // If you want to use iterators inside a larger locked transaction or modify the tree,
    // use iterLocked() instead.
    SSDIterator *(*iter)(void *self);
    SSDIterator *(*iterLocked)(void *self, SSDLock *lock);
} SSDHashNode_ClassIf;
extern SSDHashNode_ClassIf SSDHashNode_ClassIf_tmpl;

typedef struct SSDHashIter_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*isHashtable)(void *self);
    bool (*isArray)(void *self);
    bool (*valid)(void *self);
    bool (*next)(void *self);
    bool (*get)(void *self, stvar *out);
    stvar *(*ptr)(void *self);
    strref (*name)(void *self);
    int32 (*idx)(void *self);
    bool (*iterOut)(void *self, int32 *idx, strref *name, stvar **val);
} SSDHashIter_ClassIf;
extern SSDHashIter_ClassIf SSDHashIter_ClassIf_tmpl;

typedef struct SSDHashNode {
    SSDHashNode_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDHashNode;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDTree *tree;
    int64 modified;        // The timestamp this node was last modified
    hashtable storage;
} SSDHashNode;
extern ObjClassInfo SSDHashNode_clsinfo;
#define SSDHashNode(inst) ((SSDHashNode*)((void)((inst) && &((inst)->_is_SSDHashNode)), (inst)))
#define SSDHashNodeNone ((SSDHashNode*)NULL)

SSDHashNode *SSDHashNode__create(SSDTree *tree);
// SSDHashNode *ssdhashnode_create(SSDTree *tree);
#define ssdhashnode_create(tree) SSDHashNode__create(SSDTree(tree))

// void ssdhashnodeUpdateModified(SSDHashNode *self);
#define ssdhashnodeUpdateModified(self) SSDNode_updateModified(SSDNode(self))

// bool ssdhashnodeIsHashtable(SSDHashNode *self);
//
// This node is an object that contains values or objects by name
#define ssdhashnodeIsHashtable(self) (self)->_->isHashtable(SSDHashNode(self))
// bool ssdhashnodeIsArray(SSDHashNode *self);
//
// This node is an array that contains values or objects by array index
#define ssdhashnodeIsArray(self) (self)->_->isArray(SSDHashNode(self))
// bool ssdhashnodeGet(SSDHashNode *self, int32 idx, strref name, stvar *out, SSDLock *lock);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define ssdhashnodeGet(self, idx, name, out, lock) (self)->_->get(SSDHashNode(self), idx, name, out, lock)
// stvar *ssdhashnodePtr(SSDHashNode *self, int32 idx, strref name, SSDLock *lock);
//
// Gets a pointer to a value. This points to the internal storage within the node
// so it is only guaranteed to be valid while the read lock is held.
#define ssdhashnodePtr(self, idx, name, lock) (self)->_->ptr(SSDHashNode(self), idx, name, lock)
// bool ssdhashnodeSet(SSDHashNode *self, int32 idx, strref name, stvar val, SSDLock *lock);
//
// Sets the given value
#define ssdhashnodeSet(self, idx, name, val, lock) (self)->_->set(SSDHashNode(self), idx, name, val, lock)
// bool ssdhashnodeSetC(SSDHashNode *self, int32 idx, strref name, stvar *val, SSDLock *lock);
//
// Same as setValue but consumes the value
// (consumes even on failure)
#define ssdhashnodeSetC(self, idx, name, val, lock) (self)->_->setC(SSDHashNode(self), idx, name, val, lock)
// bool ssdhashnodeRemove(SSDHashNode *self, int32 idx, strref name, SSDLock *lock);
//
// Removes a value
#define ssdhashnodeRemove(self, idx, name, lock) (self)->_->remove(SSDHashNode(self), idx, name, lock)
// int32 ssdhashnodeCount(SSDHashNode *self, SSDLock *lock);
//
// How many values / objects does this node contain?
#define ssdhashnodeCount(self, lock) (self)->_->count(SSDHashNode(self), lock)
// SSDIterator *ssdhashnodeIter(SSDHashNode *self);
//
// IMPORTANT NOTE: The generic object iterator interface cannot take any parameters;
// thus it always acquires a transient read lock and holds it until the iterator is
// destroyed. The caller MUST NOT already have an SSDLock held.
// If you want to use iterators inside a larger locked transaction or modify the tree,
// use iterLocked() instead.
#define ssdhashnodeIter(self) (self)->_->iter(SSDHashNode(self))
// SSDIterator *ssdhashnodeIterLocked(SSDHashNode *self, SSDLock *lock);
#define ssdhashnodeIterLocked(self, lock) (self)->_->iterLocked(SSDHashNode(self), lock)

typedef struct SSDHashIter {
    SSDHashIter_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDHashIter;
        void *_is_SSDIterator;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDNode *node;
    SSDLock *lock;
    SSDLock transient_lock;
    htiter iter;
} SSDHashIter;
extern ObjClassInfo SSDHashIter_clsinfo;
#define SSDHashIter(inst) ((SSDHashIter*)((void)((inst) && &((inst)->_is_SSDHashIter)), (inst)))
#define SSDHashIterNone ((SSDHashIter*)NULL)

SSDHashIter *SSDHashIter_create(SSDHashNode *node, SSDLock *lock);
// SSDHashIter *ssdhashiterCreate(SSDHashNode *node, SSDLock *lock);
#define ssdhashiterCreate(node, lock) SSDHashIter_create(SSDHashNode(node), lock)

// ObjInst *ssdhashiterObjInst(SSDHashIter *self);
#define ssdhashiterObjInst(self) SSDIterator_objInst(SSDIterator(self))

// bool ssdhashiterIsHashtable(SSDHashIter *self);
#define ssdhashiterIsHashtable(self) (self)->_->isHashtable(SSDHashIter(self))
// bool ssdhashiterIsArray(SSDHashIter *self);
#define ssdhashiterIsArray(self) (self)->_->isArray(SSDHashIter(self))
// bool ssdhashiterValid(SSDHashIter *self);
#define ssdhashiterValid(self) (self)->_->valid(SSDHashIter(self))
// bool ssdhashiterNext(SSDHashIter *self);
#define ssdhashiterNext(self) (self)->_->next(SSDHashIter(self))
// bool ssdhashiterGet(SSDHashIter *self, stvar *out);
#define ssdhashiterGet(self, out) (self)->_->get(SSDHashIter(self), out)
// stvar *ssdhashiterPtr(SSDHashIter *self);
#define ssdhashiterPtr(self) (self)->_->ptr(SSDHashIter(self))
// strref ssdhashiterName(SSDHashIter *self);
#define ssdhashiterName(self) (self)->_->name(SSDHashIter(self))
// int32 ssdhashiterIdx(SSDHashIter *self);
#define ssdhashiterIdx(self) (self)->_->idx(SSDHashIter(self))
// bool ssdhashiterIterOut(SSDHashIter *self, int32 *idx, strref *name, stvar **val);
#define ssdhashiterIterOut(self, idx, name, val) (self)->_->iterOut(SSDHashIter(self), idx, name, val)

