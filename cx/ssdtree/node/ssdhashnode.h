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
    // IMPORTANT NOTE: Iterator interface cannot acquire the lock automatically;
    // to use this you MUST first call ssdLockRead on the node!
    SSDIterator *(*iter)(void *self);
} SSDHashNode_ClassIf;
extern SSDHashNode_ClassIf SSDHashNode_ClassIf_tmpl;

typedef struct SSDHashIter_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*valid)(void *self);
    bool (*next)(void *self);
    bool (*get)(void *self, stvar *out);
    stvar *(*ptr)(void *self);
    int32 (*idx)(void *self);
    bool (*name)(void *self, string *out);
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
// IMPORTANT NOTE: Iterator interface cannot acquire the lock automatically;
// to use this you MUST first call ssdLockRead on the node!
#define ssdhashnodeIter(self) (self)->_->iter(SSDHashNode(self))

typedef struct SSDHashIter {
    SSDHashIter_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDHashIter;
        void *_is_SSDIterator;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDHashNode *node;
    htiter iter;
} SSDHashIter;
extern ObjClassInfo SSDHashIter_clsinfo;
#define SSDHashIter(inst) ((SSDHashIter*)((void)((inst) && &((inst)->_is_SSDHashIter)), (inst)))
#define SSDHashIterNone ((SSDHashIter*)NULL)

SSDHashIter *SSDHashIter_create(SSDHashNode *node);
// SSDHashIter *ssdhashiterCreate(SSDHashNode *node);
#define ssdhashiterCreate(node) SSDHashIter_create(SSDHashNode(node))

// bool ssdhashiterValid(SSDHashIter *self);
#define ssdhashiterValid(self) (self)->_->valid(SSDHashIter(self))
// bool ssdhashiterNext(SSDHashIter *self);
#define ssdhashiterNext(self) (self)->_->next(SSDHashIter(self))
// bool ssdhashiterGet(SSDHashIter *self, stvar *out);
#define ssdhashiterGet(self, out) (self)->_->get(SSDHashIter(self), out)
// stvar *ssdhashiterPtr(SSDHashIter *self);
#define ssdhashiterPtr(self) (self)->_->ptr(SSDHashIter(self))
// int32 ssdhashiterIdx(SSDHashIter *self);
#define ssdhashiterIdx(self) (self)->_->idx(SSDHashIter(self))
// bool ssdhashiterName(SSDHashIter *self, string *out);
#define ssdhashiterName(self, out) (self)->_->name(SSDHashIter(self), out)

