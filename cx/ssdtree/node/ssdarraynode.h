#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/ssdtree/node/ssdnode.h>

typedef struct SSDArrayNode SSDArrayNode;
typedef struct SSDArrayIter SSDArrayIter;
saDeclarePtr(SSDArrayNode);
saDeclarePtr(SSDArrayIter);

typedef struct SSDArrayNode_ClassIf {
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
} SSDArrayNode_ClassIf;
extern SSDArrayNode_ClassIf SSDArrayNode_ClassIf_tmpl;

typedef struct SSDArrayIter_ClassIf {
    ObjIface *_implements;
    ObjIface *_parent;
    size_t _size;

    bool (*valid)(void *self);
    bool (*next)(void *self);
    bool (*get)(void *self, stvar *out);
    stvar *(*ptr)(void *self);
    int32 (*idx)(void *self);
    bool (*name)(void *self, string *out);
} SSDArrayIter_ClassIf;
extern SSDArrayIter_ClassIf SSDArrayIter_ClassIf_tmpl;

typedef struct SSDArrayNode {
    SSDArrayNode_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDArrayNode;
        void *_is_SSDNode;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDInfo *info;
    int64 modified;        // The timestamp this node was last modified
    sa_stvar storage;
} SSDArrayNode;
extern ObjClassInfo SSDArrayNode_clsinfo;
#define SSDArrayNode(inst) ((SSDArrayNode*)((void)((inst) && &((inst)->_is_SSDArrayNode)), (inst)))
#define SSDArrayNodeNone ((SSDArrayNode*)NULL)

SSDArrayNode *SSDArrayNode_create(SSDInfo *info);
// SSDArrayNode *ssdarraynodeCreate(SSDInfo *info);
#define ssdarraynodeCreate(info) SSDArrayNode_create(SSDInfo(info))

// bool ssdarraynodeIsHashtable(SSDArrayNode *self);
//
// This node is an object that contains values or objects by name
#define ssdarraynodeIsHashtable(self) (self)->_->isHashtable(SSDArrayNode(self))
// bool ssdarraynodeIsArray(SSDArrayNode *self);
//
// This node is an array that contains values or objects by array index
#define ssdarraynodeIsArray(self) (self)->_->isArray(SSDArrayNode(self))
// bool ssdarraynodeGet(SSDArrayNode *self, int32 idx, strref name, stvar *out, SSDLock *lock);
//
// Gets a value. Caller owns the value and must destroy it with stDestroy!
#define ssdarraynodeGet(self, idx, name, out, lock) (self)->_->get(SSDArrayNode(self), idx, name, out, lock)
// stvar *ssdarraynodePtr(SSDArrayNode *self, int32 idx, strref name, SSDLock *lock);
//
// Gets a pointer to a value. This points to the internal storage within the node
// so it is only guaranteed to be valid while the read lock is held.
#define ssdarraynodePtr(self, idx, name, lock) (self)->_->ptr(SSDArrayNode(self), idx, name, lock)
// bool ssdarraynodeSet(SSDArrayNode *self, int32 idx, strref name, stvar val, SSDLock *lock);
//
// Sets the given value
#define ssdarraynodeSet(self, idx, name, val, lock) (self)->_->set(SSDArrayNode(self), idx, name, val, lock)
// bool ssdarraynodeSetC(SSDArrayNode *self, int32 idx, strref name, stvar *val, SSDLock *lock);
//
// Same as setValue but consumes the value
// (consumes even on failure)
#define ssdarraynodeSetC(self, idx, name, val, lock) (self)->_->setC(SSDArrayNode(self), idx, name, val, lock)
// bool ssdarraynodeRemove(SSDArrayNode *self, int32 idx, strref name, SSDLock *lock);
//
// Removes a value
#define ssdarraynodeRemove(self, idx, name, lock) (self)->_->remove(SSDArrayNode(self), idx, name, lock)
// int32 ssdarraynodeCount(SSDArrayNode *self, SSDLock *lock);
//
// How many values / objects does this node contain?
#define ssdarraynodeCount(self, lock) (self)->_->count(SSDArrayNode(self), lock)
// SSDIterator *ssdarraynodeIter(SSDArrayNode *self);
//
// IMPORTANT NOTE: Iterator interface cannot acquire the lock automatically;
// to use this you MUST first call ssdLockRead on the node!
#define ssdarraynodeIter(self) (self)->_->iter(SSDArrayNode(self))

typedef struct SSDArrayIter {
    SSDArrayIter_ClassIf *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_SSDArrayIter;
        void *_is_SSDIterator;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    SSDArrayNode *node;
    int32 idx;
} SSDArrayIter;
extern ObjClassInfo SSDArrayIter_clsinfo;
#define SSDArrayIter(inst) ((SSDArrayIter*)((void)((inst) && &((inst)->_is_SSDArrayIter)), (inst)))
#define SSDArrayIterNone ((SSDArrayIter*)NULL)

SSDArrayIter *SSDArrayIter_create(SSDArrayNode *node);
// SSDArrayIter *ssdarrayiterCreate(SSDArrayNode *node);
#define ssdarrayiterCreate(node) SSDArrayIter_create(SSDArrayNode(node))

// bool ssdarrayiterValid(SSDArrayIter *self);
#define ssdarrayiterValid(self) (self)->_->valid(SSDArrayIter(self))
// bool ssdarrayiterNext(SSDArrayIter *self);
#define ssdarrayiterNext(self) (self)->_->next(SSDArrayIter(self))
// bool ssdarrayiterGet(SSDArrayIter *self, stvar *out);
#define ssdarrayiterGet(self, out) (self)->_->get(SSDArrayIter(self), out)
// stvar *ssdarrayiterPtr(SSDArrayIter *self);
#define ssdarrayiterPtr(self) (self)->_->ptr(SSDArrayIter(self))
// int32 ssdarrayiterIdx(SSDArrayIter *self);
#define ssdarrayiterIdx(self) (self)->_->idx(SSDArrayIter(self))
// bool ssdarrayiterName(SSDArrayIter *self, string *out);
#define ssdarrayiterName(self, out) (self)->_->name(SSDArrayIter(self), out)

