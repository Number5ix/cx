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
    bool (*isHashtable)(_Inout_ void *self);
    // This node is an array that contains values or objects by array index
    bool (*isArray)(_Inout_ void *self);
    // Gets a value. Caller owns the value and must destroy it with stDestroy!
    bool (*get)(_Inout_ void *self, int32 idx, _In_opt_ strref name, _When_(return == true, _Out_) stvar *out, _Inout_ SSDLockState *_ssdCurrentLockState);
    // Gets a pointer to a value. This points to the internal storage within the node
    // so it is only guaranteed to be valid while the read lock is held.
    _Ret_opt_valid_ stvar *(*ptr)(_Inout_ void *self, int32 idx, _In_opt_ strref name, _Inout_ SSDLockState *_ssdCurrentLockState);
    // Sets the given value
    bool (*set)(_Inout_ void *self, int32 idx, _In_opt_ strref name, stvar val, _Inout_ SSDLockState *_ssdCurrentLockState);
    // Same as setValue but consumes the value
    // (consumes even on failure)
    bool (*setC)(_Inout_ void *self, int32 idx, _In_opt_ strref name, _Inout_ stvar *val, _Inout_ SSDLockState *_ssdCurrentLockState);
    // Removes a value
    bool (*remove)(_Inout_ void *self, int32 idx, _In_opt_ strref name, _Inout_ SSDLockState *_ssdCurrentLockState);
    // How many values / objects does this node contain?
    int32 (*count)(_Inout_ void *self, _Inout_ SSDLockState *_ssdCurrentLockState);
    // IMPORTANT NOTE: The generic object iterator interface cannot take any parameters;
    // thus it always acquires a transient read lock and holds it until the iterator is
    // destroyed. The caller MUST NOT already have an SSDLock held.
    // If you want to use iterators inside a larger locked transaction or modify the tree,
    // use iterLocked() instead.
    _Ret_valid_ SSDIterator *(*iter)(_Inout_ void *self);
    SSDIterator *(*_iterLocked)(_Inout_ void *self, _Inout_ SSDLockState *_ssdCurrentLockState);
} SSDHashNode_ClassIf;
extern SSDHashNode_ClassIf SSDHashNode_ClassIf_tmpl;

typedef struct SSDHashIter_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*isHashtable)(_Inout_ void *self);
    bool (*isArray)(_Inout_ void *self);
    bool (*valid)(_Inout_ void *self);
    bool (*next)(_Inout_ void *self);
    bool (*get)(_Inout_ void *self, stvar *out);
    stvar *(*ptr)(_Inout_ void *self);
    strref (*name)(_Inout_ void *self);
    int32 (*idx)(_Inout_ void *self);
    bool (*iterOut)(_Inout_ void *self, _When_(return == true, _Out_) int32 *idx, _When_(return == true, _Out_) strref *name, _When_(return == true, _Out_) stvar **val);
} SSDHashIter_ClassIf;
extern SSDHashIter_ClassIf SSDHashIter_ClassIf_tmpl;

typedef struct SSDHashNode {
    union {
        SSDHashNode_ClassIf *_;
        void *_is_SSDHashNode;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    SSDTree *tree;
    int64 modified;        // The timestamp this node was last modified
    hashtable storage;
} SSDHashNode;
extern ObjClassInfo SSDHashNode_clsinfo;
#define SSDHashNode(inst) ((SSDHashNode*)(unused_noeval((inst) && &((inst)->_is_SSDHashNode)), (inst)))
#define SSDHashNodeNone ((SSDHashNode*)NULL)

_objfactory_guaranteed SSDHashNode *SSDHashNode__create(SSDTree *tree);
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
// bool ssdhashnodeGet(SSDHashNode *self, int32 idx, strref name, stvar *out, SSDLockState *_ssdCurrentLockState);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define ssdhashnodeGet(self, idx, name, out, _ssdCurrentLockState) (self)->_->get(SSDHashNode(self), idx, name, out, _ssdCurrentLockState)
// stvar *ssdhashnodePtr(SSDHashNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState);
//
// Gets a pointer to a value. This points to the internal storage within the node
// so it is only guaranteed to be valid while the read lock is held.
#define ssdhashnodePtr(self, idx, name, _ssdCurrentLockState) (self)->_->ptr(SSDHashNode(self), idx, name, _ssdCurrentLockState)
// bool ssdhashnodeSet(SSDHashNode *self, int32 idx, strref name, stvar val, SSDLockState *_ssdCurrentLockState);
//
// Sets the given value
#define ssdhashnodeSet(self, idx, name, val, _ssdCurrentLockState) (self)->_->set(SSDHashNode(self), idx, name, val, _ssdCurrentLockState)
// bool ssdhashnodeSetC(SSDHashNode *self, int32 idx, strref name, stvar *val, SSDLockState *_ssdCurrentLockState);
//
// Same as setValue but consumes the value
// (consumes even on failure)
#define ssdhashnodeSetC(self, idx, name, val, _ssdCurrentLockState) (self)->_->setC(SSDHashNode(self), idx, name, val, _ssdCurrentLockState)
// bool ssdhashnodeRemove(SSDHashNode *self, int32 idx, strref name, SSDLockState *_ssdCurrentLockState);
//
// Removes a value
#define ssdhashnodeRemove(self, idx, name, _ssdCurrentLockState) (self)->_->remove(SSDHashNode(self), idx, name, _ssdCurrentLockState)
// int32 ssdhashnodeCount(SSDHashNode *self, SSDLockState *_ssdCurrentLockState);
//
// How many values / objects does this node contain?
#define ssdhashnodeCount(self, _ssdCurrentLockState) (self)->_->count(SSDHashNode(self), _ssdCurrentLockState)
// SSDIterator *ssdhashnodeIter(SSDHashNode *self);
//
// IMPORTANT NOTE: The generic object iterator interface cannot take any parameters;
// thus it always acquires a transient read lock and holds it until the iterator is
// destroyed. The caller MUST NOT already have an SSDLock held.
// If you want to use iterators inside a larger locked transaction or modify the tree,
// use iterLocked() instead.
#define ssdhashnodeIter(self) (self)->_->iter(SSDHashNode(self))
// SSDIterator *ssdhashnode_iterLocked(SSDHashNode *self, SSDLockState *_ssdCurrentLockState);
#define ssdhashnode_iterLocked(self, _ssdCurrentLockState) (self)->_->_iterLocked(SSDHashNode(self), _ssdCurrentLockState)

typedef struct SSDHashIter {
    union {
        SSDHashIter_ClassIf *_;
        void *_is_SSDHashIter;
        void *_is_SSDIterator;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    SSDNode *node;
    SSDLockState *lstate;
    SSDLockState transient_lock_state;
    htiter iter;
    string lastName;
} SSDHashIter;
extern ObjClassInfo SSDHashIter_clsinfo;
#define SSDHashIter(inst) ((SSDHashIter*)(unused_noeval((inst) && &((inst)->_is_SSDHashIter)), (inst)))
#define SSDHashIterNone ((SSDHashIter*)NULL)

_objfactory_guaranteed SSDHashIter *SSDHashIter_create(SSDHashNode *node, SSDLockState *lstate);
// SSDHashIter *ssdhashiterCreate(SSDHashNode *node, SSDLockState *lstate);
#define ssdhashiterCreate(node, lstate) SSDHashIter_create(SSDHashNode(node), lstate)

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

