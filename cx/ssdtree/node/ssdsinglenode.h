#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/node/ssdnode.h>

typedef struct SSDSingleNode SSDSingleNode;
typedef struct SSDSingleIter SSDSingleIter;
saDeclarePtr(SSDSingleNode);
saDeclarePtr(SSDSingleIter);

typedef struct SSDSingleNode_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    // This node is an object that contains values or objects by name
    bool (*isHashtable)(void *self);
    // This node is an array that contains values or objects by array index
    bool (*isArray)(void *self);
    // Gets a value. Caller owns the value and must destroy it with stDestroy!
    bool (*get)(void *self, int32 idx, strref name, stvar *out, SSDLockState *_ssdCurrentLockState);
    // Gets a pointer to a value. This points to the internal storage within the node
    // so it is only guaranteed to be valid while the read lock is held.
    stvar *(*ptr)(void *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState);
    // Sets the given value
    bool (*set)(void *self, int32 idx, strref name, stvar val, SSDLockState *_ssdCurrentLockState);
    // Same as setValue but consumes the value
    // (consumes even on failure)
    bool (*setC)(void *self, int32 idx, strref name, stvar *val, SSDLockState *_ssdCurrentLockState);
    // Removes a value
    bool (*remove)(void *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState);
    // How many values / objects does this node contain?
    int32 (*count)(void *self, SSDLockState *_ssdCurrentLockState);
    // IMPORTANT NOTE: The generic object iterator interface cannot take any parameters;
    // thus it always acquires a transient read lock and holds it until the iterator is
    // destroyed. The caller MUST NOT already have an SSDLock held.
    // If you want to use iterators inside a larger locked transaction or modify the tree,
    // use iterLocked() instead.
    SSDIterator *(*iter)(void *self);
    SSDIterator *(*_iterLocked)(void *self, SSDLockState *_ssdCurrentLockState);
} SSDSingleNode_ClassIf;
extern SSDSingleNode_ClassIf SSDSingleNode_ClassIf_tmpl;

typedef struct SSDSingleIter_ClassIf {
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
} SSDSingleIter_ClassIf;
extern SSDSingleIter_ClassIf SSDSingleIter_ClassIf_tmpl;

typedef struct SSDSingleNode {
    union {
        SSDSingleNode_ClassIf *_;
        void *_is_SSDSingleNode;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    SSDTree *tree;
    int64 modified;        // The timestamp this node was last modified
    stvar storage;
} SSDSingleNode;
extern ObjClassInfo SSDSingleNode_clsinfo;
#define SSDSingleNode(inst) ((SSDSingleNode*)(unused_noeval((inst) && &((inst)->_is_SSDSingleNode)), (inst)))
#define SSDSingleNodeNone ((SSDSingleNode*)NULL)

SSDSingleNode *SSDSingleNode__create(SSDTree *tree);
// SSDSingleNode *ssdsinglenode_create(SSDTree *tree);
#define ssdsinglenode_create(tree) SSDSingleNode__create(SSDTree(tree))

// void ssdsinglenodeUpdateModified(SSDSingleNode *self);
#define ssdsinglenodeUpdateModified(self) SSDNode_updateModified(SSDNode(self))

// bool ssdsinglenodeIsHashtable(SSDSingleNode *self);
//
// This node is an object that contains values or objects by name
#define ssdsinglenodeIsHashtable(self) (self)->_->isHashtable(SSDSingleNode(self))
// bool ssdsinglenodeIsArray(SSDSingleNode *self);
//
// This node is an array that contains values or objects by array index
#define ssdsinglenodeIsArray(self) (self)->_->isArray(SSDSingleNode(self))
// bool ssdsinglenodeGet(SSDSingleNode *self, int32 idx, strref name, stvar *out, SSDLockState *_ssdCurrentLockState);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define ssdsinglenodeGet(self, idx, name, out, _ssdCurrentLockState) (self)->_->get(SSDSingleNode(self), idx, name, out, _ssdCurrentLockState)
// stvar *ssdsinglenodePtr(SSDSingleNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState);
//
// Gets a pointer to a value. This points to the internal storage within the node
// so it is only guaranteed to be valid while the read lock is held.
#define ssdsinglenodePtr(self, idx, name, _ssdCurrentLockState) (self)->_->ptr(SSDSingleNode(self), idx, name, _ssdCurrentLockState)
// bool ssdsinglenodeSet(SSDSingleNode *self, int32 idx, strref name, stvar val, SSDLockState *_ssdCurrentLockState);
//
// Sets the given value
#define ssdsinglenodeSet(self, idx, name, val, _ssdCurrentLockState) (self)->_->set(SSDSingleNode(self), idx, name, val, _ssdCurrentLockState)
// bool ssdsinglenodeSetC(SSDSingleNode *self, int32 idx, strref name, stvar *val, SSDLockState *_ssdCurrentLockState);
//
// Same as setValue but consumes the value
// (consumes even on failure)
#define ssdsinglenodeSetC(self, idx, name, val, _ssdCurrentLockState) (self)->_->setC(SSDSingleNode(self), idx, name, val, _ssdCurrentLockState)
// bool ssdsinglenodeRemove(SSDSingleNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState);
//
// Removes a value
#define ssdsinglenodeRemove(self, idx, name, _ssdCurrentLockState) (self)->_->remove(SSDSingleNode(self), idx, name, _ssdCurrentLockState)
// int32 ssdsinglenodeCount(SSDSingleNode *self, SSDLockState *_ssdCurrentLockState);
//
// How many values / objects does this node contain?
#define ssdsinglenodeCount(self, _ssdCurrentLockState) (self)->_->count(SSDSingleNode(self), _ssdCurrentLockState)
// SSDIterator *ssdsinglenodeIter(SSDSingleNode *self);
//
// IMPORTANT NOTE: The generic object iterator interface cannot take any parameters;
// thus it always acquires a transient read lock and holds it until the iterator is
// destroyed. The caller MUST NOT already have an SSDLock held.
// If you want to use iterators inside a larger locked transaction or modify the tree,
// use iterLocked() instead.
#define ssdsinglenodeIter(self) (self)->_->iter(SSDSingleNode(self))
// SSDIterator *ssdsinglenode_iterLocked(SSDSingleNode *self, SSDLockState *_ssdCurrentLockState);
#define ssdsinglenode_iterLocked(self, _ssdCurrentLockState) (self)->_->_iterLocked(SSDSingleNode(self), _ssdCurrentLockState)

typedef struct SSDSingleIter {
    union {
        SSDSingleIter_ClassIf *_;
        void *_is_SSDSingleIter;
        void *_is_SSDIterator;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    SSDNode *node;
    SSDLockState *lstate;
    SSDLockState transient_lock_state;
    bool done;
} SSDSingleIter;
extern ObjClassInfo SSDSingleIter_clsinfo;
#define SSDSingleIter(inst) ((SSDSingleIter*)(unused_noeval((inst) && &((inst)->_is_SSDSingleIter)), (inst)))
#define SSDSingleIterNone ((SSDSingleIter*)NULL)

SSDSingleIter *SSDSingleIter_create(SSDSingleNode *node, SSDLockState *lstate);
// SSDSingleIter *ssdsingleiterCreate(SSDSingleNode *node, SSDLockState *lstate);
#define ssdsingleiterCreate(node, lstate) SSDSingleIter_create(SSDSingleNode(node), lstate)

// ObjInst *ssdsingleiterObjInst(SSDSingleIter *self);
#define ssdsingleiterObjInst(self) SSDIterator_objInst(SSDIterator(self))

// bool ssdsingleiterIsHashtable(SSDSingleIter *self);
#define ssdsingleiterIsHashtable(self) (self)->_->isHashtable(SSDSingleIter(self))
// bool ssdsingleiterIsArray(SSDSingleIter *self);
#define ssdsingleiterIsArray(self) (self)->_->isArray(SSDSingleIter(self))
// bool ssdsingleiterValid(SSDSingleIter *self);
#define ssdsingleiterValid(self) (self)->_->valid(SSDSingleIter(self))
// bool ssdsingleiterNext(SSDSingleIter *self);
#define ssdsingleiterNext(self) (self)->_->next(SSDSingleIter(self))
// bool ssdsingleiterGet(SSDSingleIter *self, stvar *out);
#define ssdsingleiterGet(self, out) (self)->_->get(SSDSingleIter(self), out)
// stvar *ssdsingleiterPtr(SSDSingleIter *self);
#define ssdsingleiterPtr(self) (self)->_->ptr(SSDSingleIter(self))
// strref ssdsingleiterName(SSDSingleIter *self);
#define ssdsingleiterName(self) (self)->_->name(SSDSingleIter(self))
// int32 ssdsingleiterIdx(SSDSingleIter *self);
#define ssdsingleiterIdx(self) (self)->_->idx(SSDSingleIter(self))
// bool ssdsingleiterIterOut(SSDSingleIter *self, int32 *idx, strref *name, stvar **val);
#define ssdsingleiterIterOut(self, idx, name, val) (self)->_->iterOut(SSDSingleIter(self), idx, name, val)

