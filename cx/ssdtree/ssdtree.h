#pragma once

#include <cx/ssdtree/node/ssdnode.h>
#include <cx/meta/block.h>

// Semi-Structured data tree

// Initializes a lock structure
void ssdLockInit(SSDLock *lock);

// bool ssdLockRead(SSDNode *tree, SSDLock *lock);
//
// Starts a locked operation in read mode
#define ssdLockRead(tree, lock) _ssdLockRead(SSDNode(tree), lock)
bool _ssdLockRead(SSDNode *tree, SSDLock *lock);

// bool ssdLockWrite(SSDNode *tree, SSDLock *lock);
//
// Starts a operation in write mode or upgrades one from read to write
// NOTE, upgrading the lock drops it briefly, do not assume that no one else
// got the write lock in between! State may be changed between dropping the
// read lock and getting the write lock.
#define ssdLockWrite(tree, lock) _ssdLockWrite(SSDNode(tree), lock)
bool _ssdLockWrite(SSDNode *tree, SSDLock *lock);

// Ends a locked operation
bool ssdLockEnd(SSDNode *tree, SSDLock *lock);

#define withSSDLock(tree, name) blkWrap(SSDLock name = { .init = true }, ssdLockEnd(tree, &name))

SSDNode *_ssdCreateRoot(int crtype, SSDTree *tree, uint32 flags);
// Creates a new semi-structured tree with an hashtable as its root.
// Returns a reference-counted object that must be disposed of with objRelease
#define ssdCreateHashtable(...) _ssdCreateRoot(SSD_Create_Hashtable, NULL, opt_flags(__VA_ARGS__))
// Creates a new semi-structured tree with an array as its root.
// Returns a reference-counted object that must be disposed of with objRelease
#define ssdCreateArray(...) _ssdCreateRoot(SSD_Create_Array, NULL, opt_flags(__VA_ARGS__))
// Creates a new semi-structured tree with a single value as its root.
// The supplied value must be one of the supported types!
// (int, float, string, boolean, or stvNone)
// Returns a reference-counted object that must be disposed of with objRelease
#define ssdCreateSingle(...) _ssdCreateRoot(SSD_Create_Single, NULL, opt_flags(__VA_ARGS__))
#define ssdCreateCustom(crtype, tree) _ssdCreateRoot(crtype, tree, 0)

// Returns a node representing a subtree
// If this node does not exist and the create parameter is any value other than SSD_Create_None,
// a node of the specified type is created at the given path.
SSDNode *ssdSubtree(SSDNode *tree, strref path, int create, SSDLock *lock);

// For convenience, the following functions take a PATH to be traversed.
// The syntax for a path consists of a series of names separated by '/',
// and/or array indices enclosed in []
//
// For example:
//
//     bucket/paints[2]/yellow
//
// Represents a path to a value with the key 'yellow', which is the third hashtable in an array of hashtables
// that is named 'paints' inside of an hashtable called 'bucket' inside of the unnamed root hashtable. To make
// it easier to understand, see the corresponding JSON representation:
//
// {
//     "bucket": {
//         "paints": [
//             {},
//             {}
//             { "yellow": "THIS VALUE HERE" }
//         ]
//     }
// }
//
// This path is for a structure that uses an array as its root:
//
//     [1]/mango
//
// Or this JSON:
//
// [
//     {},
//     { "mango": "THIS VALUE HERE" }
// ]
// 
// If automatic path traversal is not desired, the SSDNode interface can instead be directly used.

// CAUTION: The output stvar MUST be destroyed with stDestroy or it could cause a memory leak if the
// value is a string or if a hashtable or array exists at that name. To avoid the hassle of memory
// management, ssdPtr can be used instead.
bool ssdGet(SSDNode *root, strref path, stvar *out, SSDLock *lock);

// A variant of ssdGet that fills in a default if the value does not exist. The function still
// returns false in that case.
_meta_inline bool ssdGetD(SSDNode *root, strref path, stvar *out, stvar def, SSDLock *lock)
{
    if (ssdGet(root, path, out, lock))
        return true;

    stvarCopy(out, def);
    return false;
}

// If createpath is true, the path to the value is automatically created if necessary
bool ssdSet(SSDNode *root, strref path, bool createpath, stvar val, SSDLock *lock);
bool ssdRemove(SSDNode *root, strref path, SSDLock *lock);

// Unlike the other ssdtree functions, the lock parameter for ssdPtr is REQUIRED.
// This function should be used as part of a larger locked transaction.
// That is because the pointer returned by this function is only guaranteed to be valid so long as
// the read or write lock is held.
stvar *ssdPtr(SSDNode *root, strref path, SSDLock *lock);
