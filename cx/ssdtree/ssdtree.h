#pragma once

#include <cx/ssdtree/ssdnode.h>
#include <cx/meta/block.h>

// Semi-Structured data tree

// Initializes a lock structure
void ssdLockInit(SSDLock *lock);

// Starts a locked operation in read mode
bool ssdLockRead(SSDNode *tree, SSDLock *lock);

// Starts a operation in write mode or upgrades one from read to write
// NOTE, upgrading the lock drops it briefly, do not assume that no one else
// got the write lock in between! State may be changed between dropping the
// read lock and getting the write lock.
bool ssdLockWrite(SSDNode *tree, SSDLock *lock);

// Ends a locked operation
bool ssdLockEnd(SSDNode *tree, SSDLock *lock);

#define withSSDLock(tree, name) blkWrap(SSDLock name = { .init = true }, ssdLockEnd(tree, &name))

SSDNode *_ssdCreateRoot(bool singleval, stvar initval, uint32 flags);
// Creates a new semi-structured tree with an object (i.e. hashtable or name/value mapping) as its root.
// Returns a reference-counted object that must be disposed of with objRelease
#define ssdCreateTree(...) _ssdCreateRoot(false, stvNone, opt_flags(__VA_ARGS__))
// Creates a new semi-structured tree with a single value as its root.
// The supplied value must be one of the supported types!
// (int, float, string, boolean, sarray, or stvNone)
// Returns a reference-counted object that must be disposed of with objRelease
#define ssdCreateSingleValue(val, ...) _ssdCreateRoot(true, val, opt_flags(__VA_ARGS__))

// CAUTION: The output stvar MUST be destroyed with stDestroy or it could cause a memory leak if the
// value is a string, array, or if a sub-object exists at that name. To avoid the hassle of memory
// management, ssdGetPtr can be used instead.
bool ssdGetValue(SSDNode *tree, strref path, stvar *out, SSDLock *lock);
bool ssdSetValue(SSDNode *tree, strref path, stvar val, SSDLock *lock);
bool ssdRemoveValue(SSDNode *tree, strref path, SSDLock *lock);

// Unlike the other ssdtree functions, the lock parameter for ssdGetPtr is REQUIRED.
// This function should be used as part of a larger locked transaction.
// That is because the pointer returned by this function is only guaranteed to be valid so long as
// the read lock is held.
stvar *ssdGetPtr(SSDNode *tree, strref path, SSDLock *lock);

SSDNode *ssdGetSubtree(SSDNode *tree, strref path, bool create, SSDLock *lock);

_meta_inline void ssdRelease(SSDNode **tree)
{
    objRelease(tree);
}
