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
} SSDSingleNode_ClassIf;
extern SSDSingleNode_ClassIf SSDSingleNode_ClassIf_tmpl;

typedef struct SSDSingleIter_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*valid)(void *self);
    bool (*next)(void *self);
    bool (*get)(void *self, stvar *out);
    stvar *(*ptr)(void *self);
    int32 (*idx)(void *self);
    bool (*name)(void *self, string *out);
} SSDSingleIter_ClassIf;
extern SSDSingleIter_ClassIf SSDSingleIter_ClassIf_tmpl;

typedef struct SSDSingleNode {
    SSDSingleNode_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDSingleNode;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDInfo *info;
    stvar storage;
} SSDSingleNode;
extern ObjClassInfo SSDSingleNode_clsinfo;
#define SSDSingleNode(inst) ((SSDSingleNode*)((void)((inst) && &((inst)->_is_SSDSingleNode)), (inst)))
#define SSDSingleNodeNone ((SSDSingleNode*)NULL)

SSDSingleNode *SSDSingleNode_create(SSDInfo *info, stvar initval);
// SSDSingleNode *ssdsinglenodeCreate(SSDInfo *info, stvar initval);
#define ssdsinglenodeCreate(info, initval) SSDSingleNode_create(SSDInfo(info), initval)

// bool ssdsinglenodeIsHashtable(SSDSingleNode *self);
//
// This node is an object that contains values or objects by name
#define ssdsinglenodeIsHashtable(self) (self)->_->isHashtable(SSDSingleNode(self))
// bool ssdsinglenodeIsArray(SSDSingleNode *self);
//
// This node is an array that contains values or objects by array index
#define ssdsinglenodeIsArray(self) (self)->_->isArray(SSDSingleNode(self))
// bool ssdsinglenodeGet(SSDSingleNode *self, int32 idx, strref name, stvar *out, SSDLock *lock);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define ssdsinglenodeGet(self, idx, name, out, lock) (self)->_->get(SSDSingleNode(self), idx, name, out, lock)
// stvar *ssdsinglenodePtr(SSDSingleNode *self, int32 idx, strref name, SSDLock *lock);
//
// Gets a pointer to a value. This points to the internal storage within the node
// so it is only guaranteed to be valid while the read lock is held.
#define ssdsinglenodePtr(self, idx, name, lock) (self)->_->ptr(SSDSingleNode(self), idx, name, lock)
// bool ssdsinglenodeSet(SSDSingleNode *self, int32 idx, strref name, stvar val, SSDLock *lock);
//
// Sets the given value
#define ssdsinglenodeSet(self, idx, name, val, lock) (self)->_->set(SSDSingleNode(self), idx, name, val, lock)
// bool ssdsinglenodeSetC(SSDSingleNode *self, int32 idx, strref name, stvar *val, SSDLock *lock);
//
// Same as setValue but consumes the value
// (consumes even on failure)
#define ssdsinglenodeSetC(self, idx, name, val, lock) (self)->_->setC(SSDSingleNode(self), idx, name, val, lock)
// bool ssdsinglenodeRemove(SSDSingleNode *self, int32 idx, strref name, SSDLock *lock);
//
// Removes a value
#define ssdsinglenodeRemove(self, idx, name, lock) (self)->_->remove(SSDSingleNode(self), idx, name, lock)
// int32 ssdsinglenodeCount(SSDSingleNode *self, SSDLock *lock);
//
// How many values / objects does this node contain?
#define ssdsinglenodeCount(self, lock) (self)->_->count(SSDSingleNode(self), lock)
// SSDIterator *ssdsinglenodeIter(SSDSingleNode *self);
//
// IMPORTANT NOTE: Iterator interface cannot acquire the lock automatically;
// to use this you MUST first call ssdLockRead on the node!
#define ssdsinglenodeIter(self) (self)->_->iter(SSDSingleNode(self))

typedef struct SSDSingleIter {
    SSDSingleIter_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDSingleIter;
        void *_is_SSDIterator;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDSingleNode *node;
    bool done;
} SSDSingleIter;
extern ObjClassInfo SSDSingleIter_clsinfo;
#define SSDSingleIter(inst) ((SSDSingleIter*)((void)((inst) && &((inst)->_is_SSDSingleIter)), (inst)))
#define SSDSingleIterNone ((SSDSingleIter*)NULL)

SSDSingleIter *SSDSingleIter_create(SSDSingleNode *node);
// SSDSingleIter *ssdsingleiterCreate(SSDSingleNode *node);
#define ssdsingleiterCreate(node) SSDSingleIter_create(SSDSingleNode(node))

// bool ssdsingleiterValid(SSDSingleIter *self);
#define ssdsingleiterValid(self) (self)->_->valid(SSDSingleIter(self))
// bool ssdsingleiterNext(SSDSingleIter *self);
#define ssdsingleiterNext(self) (self)->_->next(SSDSingleIter(self))
// bool ssdsingleiterGet(SSDSingleIter *self, stvar *out);
#define ssdsingleiterGet(self, out) (self)->_->get(SSDSingleIter(self), out)
// stvar *ssdsingleiterPtr(SSDSingleIter *self);
#define ssdsingleiterPtr(self) (self)->_->ptr(SSDSingleIter(self))
// int32 ssdsingleiterIdx(SSDSingleIter *self);
#define ssdsingleiterIdx(self) (self)->_->idx(SSDSingleIter(self))
// bool ssdsingleiterName(SSDSingleIter *self, string *out);
#define ssdsingleiterName(self, out) (self)->_->name(SSDSingleIter(self), out)
