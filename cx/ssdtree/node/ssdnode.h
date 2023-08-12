#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/ssdtreeobj.h>
#include <cx/stype/stvar.h>

typedef struct SSDIterator SSDIterator;
typedef struct SSDNode SSDNode;
saDeclarePtr(SSDIterator);
saDeclarePtr(SSDNode);

enum SSD_INDEX_MARKER {
    SSD_ByName = -1             // Pass as index to address a child by name
};

#define ssditeratorObj(self, clsname) objDynCast(ssditeratorObjInst(self), clsname)

typedef struct SSDIteratorIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*valid)(void *self);
    bool (*next)(void *self);
    bool (*get)(void *self, stvar *out);
    stvar *(*ptr)(void *self);
    strref (*name)(void *self);
    int32 (*idx)(void *self);
    bool (*iterOut)(void *self, int32 *idx, strref *name, stvar **val);
} SSDIteratorIf;
extern SSDIteratorIf SSDIteratorIf_tmpl;

typedef struct SSDNodeIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

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
} SSDNodeIf;
extern SSDNodeIf SSDNodeIf_tmpl;

typedef struct SSDIterator_ClassIf {
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
} SSDIterator_ClassIf;
extern SSDIterator_ClassIf SSDIterator_ClassIf_tmpl;

typedef struct SSDNode_ClassIf {
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
} SSDNode_ClassIf;
extern SSDNode_ClassIf SSDNode_ClassIf_tmpl;

typedef struct SSDIterator {
    union {
        SSDIterator_ClassIf *_;
        void *_is_SSDIterator;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    SSDNode *node;
    SSDLock *lock;
    SSDLock transient_lock;
} SSDIterator;
extern ObjClassInfo SSDIterator_clsinfo;
#define SSDIterator(inst) ((SSDIterator*)(&((inst)->_is_SSDIterator)))
#define SSDIteratorNone ((SSDIterator*)NULL)

ObjInst *SSDIterator_objInst(SSDIterator *self);
// ObjInst *ssditeratorObjInst(SSDIterator *self);
#define ssditeratorObjInst(self) SSDIterator_objInst(SSDIterator(self))

// bool ssditeratorIsHashtable(SSDIterator *self);
#define ssditeratorIsHashtable(self) (self)->_->isHashtable(SSDIterator(self))
// bool ssditeratorIsArray(SSDIterator *self);
#define ssditeratorIsArray(self) (self)->_->isArray(SSDIterator(self))
// bool ssditeratorValid(SSDIterator *self);
#define ssditeratorValid(self) (self)->_->valid(SSDIterator(self))
// bool ssditeratorNext(SSDIterator *self);
#define ssditeratorNext(self) (self)->_->next(SSDIterator(self))
// bool ssditeratorGet(SSDIterator *self, stvar *out);
#define ssditeratorGet(self, out) (self)->_->get(SSDIterator(self), out)
// stvar *ssditeratorPtr(SSDIterator *self);
#define ssditeratorPtr(self) (self)->_->ptr(SSDIterator(self))
// strref ssditeratorName(SSDIterator *self);
#define ssditeratorName(self) (self)->_->name(SSDIterator(self))
// int32 ssditeratorIdx(SSDIterator *self);
#define ssditeratorIdx(self) (self)->_->idx(SSDIterator(self))
// bool ssditeratorIterOut(SSDIterator *self, int32 *idx, strref *name, stvar **val);
#define ssditeratorIterOut(self, idx, name, val) (self)->_->iterOut(SSDIterator(self), idx, name, val)

typedef struct SSDNode {
    union {
        SSDNode_ClassIf *_;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    SSDTree *tree;
    int64 modified;        // The timestamp this node was last modified
} SSDNode;
extern ObjClassInfo SSDNode_clsinfo;
#define SSDNode(inst) ((SSDNode*)(&((inst)->_is_SSDNode)))
#define SSDNodeNone ((SSDNode*)NULL)

void SSDNode_updateModified(SSDNode *self);
// void ssdnodeUpdateModified(SSDNode *self);
#define ssdnodeUpdateModified(self) SSDNode_updateModified(SSDNode(self))

// bool ssdnodeIsHashtable(SSDNode *self);
//
// This node is an object that contains values or objects by name
#define ssdnodeIsHashtable(self) (self)->_->isHashtable(SSDNode(self))
// bool ssdnodeIsArray(SSDNode *self);
//
// This node is an array that contains values or objects by array index
#define ssdnodeIsArray(self) (self)->_->isArray(SSDNode(self))
// bool ssdnodeGet(SSDNode *self, int32 idx, strref name, stvar *out, SSDLock *lock);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define ssdnodeGet(self, idx, name, out, lock) (self)->_->get(SSDNode(self), idx, name, out, lock)
// stvar *ssdnodePtr(SSDNode *self, int32 idx, strref name, SSDLock *lock);
//
// Gets a pointer to a value. This points to the internal storage within the node
// so it is only guaranteed to be valid while the read lock is held.
#define ssdnodePtr(self, idx, name, lock) (self)->_->ptr(SSDNode(self), idx, name, lock)
// bool ssdnodeSet(SSDNode *self, int32 idx, strref name, stvar val, SSDLock *lock);
//
// Sets the given value
#define ssdnodeSet(self, idx, name, val, lock) (self)->_->set(SSDNode(self), idx, name, val, lock)
// bool ssdnodeSetC(SSDNode *self, int32 idx, strref name, stvar *val, SSDLock *lock);
//
// Same as setValue but consumes the value
// (consumes even on failure)
#define ssdnodeSetC(self, idx, name, val, lock) (self)->_->setC(SSDNode(self), idx, name, val, lock)
// bool ssdnodeRemove(SSDNode *self, int32 idx, strref name, SSDLock *lock);
//
// Removes a value
#define ssdnodeRemove(self, idx, name, lock) (self)->_->remove(SSDNode(self), idx, name, lock)
// int32 ssdnodeCount(SSDNode *self, SSDLock *lock);
//
// How many values / objects does this node contain?
#define ssdnodeCount(self, lock) (self)->_->count(SSDNode(self), lock)
// SSDIterator *ssdnodeIter(SSDNode *self);
//
// IMPORTANT NOTE: The generic object iterator interface cannot take any parameters;
// thus it always acquires a transient read lock and holds it until the iterator is
// destroyed. The caller MUST NOT already have an SSDLock held.
// If you want to use iterators inside a larger locked transaction or modify the tree,
// use iterLocked() instead.
#define ssdnodeIter(self) (self)->_->iter(SSDNode(self))
// SSDIterator *ssdnodeIterLocked(SSDNode *self, SSDLock *lock);
#define ssdnodeIterLocked(self, lock) (self)->_->iterLocked(SSDNode(self), lock)

