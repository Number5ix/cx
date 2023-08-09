#pragma once

#include <cx/ssdtree/node/ssdnode.h>
#include <cx/meta/block.h>
#include <cx/string/strbase.h>
#include <cx/container/sarray.h>

// Semi-Structured data tree

// Initializes a lock structure
void ssdLockInit(SSDLock *lock);

// bool ssdLockRead(SSDNode *root, SSDLock *lock);
//
// Starts a locked operation in read mode
#define ssdLockRead(root, lock) _ssdLockRead(SSDNode(root), lock)
bool _ssdLockRead(SSDNode *root, SSDLock *lock);

// bool ssdLockWrite(SSDNode *root, SSDLock *lock);
//
// Starts a operation in write mode or upgrades one from read to write
// NOTE, upgrading the lock drops it briefly, do not assume that no one else
// got the write lock in between! State may be changed between dropping the
// read lock and getting the write lock.
#define ssdLockWrite(root, lock) _ssdLockWrite(SSDNode(root), lock)
bool _ssdLockWrite(SSDNode *root, SSDLock *lock);

// Ends a locked operation
#define ssdLockEnd(root, lock) _ssdLockEnd(SSDNode(root), lock)
bool _ssdLockEnd(SSDNode *root, SSDLock *lock);

#define withSSDLock(root, name) blkWrap(SSDLock name = { .init = true }, ssdLockEnd(root, &name))

SSDNode *_ssdCreateRoot(int crtype, SSDTree *tree, uint32 flags);
// Creates a new semi-structured tree with a hashtable as its root.
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
// The caller inherits an object reference that must be freed with objRelease()
SSDNode *ssdSubtree(SSDNode *root, strref path, int create, SSDLock *lock_opt);

// Borrowed version of ssdSubtree that does not acquire an object reference.
// The lock parameter is REQUIRED for this version as the read lock must be held in order to
// avoid race conditions.
// This version is intended for reading and cannot create nodes.
SSDNode *ssdSubtreeB(SSDNode *root, strref path, SSDLock *lock_req);

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
bool ssdGet(SSDNode *root, strref path, stvar *out, SSDLock *lock_opt);

// A variant of ssdGet that fills in a default if the value does not exist. The function still
// returns false in that case.
_meta_inline bool ssdGetD(SSDNode *root, strref path, stvar *out, stvar def, SSDLock *lock_opt)
{
    if (ssdGet(root, path, out, lock_opt))
        return true;

    stvarCopy(out, def);
    return false;
}

bool _ssdCopyOut(SSDNode *root, strref path, stype valtype, stgeneric *val, SSDLock *lock_opt);
// bool ssdCopyOut(SSDNode *root, strref path, stype valtype, stgeneric *val, SSDLock *lock_opt);
// Variant of ssdGet that copies the value out of the tree to an arbitrary destination, converting
// it in the process
#define ssdCopyOut(root, path, vtype, val_copy_out, lock_opt) _ssdCopyOut(root, path, stCheckedPtrArg(vtype, val_copy_out), lock_opt)

bool _ssdCopyOutD(SSDNode *root, strref path, stype valtype, stgeneric *val, stgeneric def, SSDLock *lock_opt);
// bool ssdCopyOutD(SSDNode *root, strref path, stype valtype, stgeneric *val, stgeneric def, SSDLock *lock_opt);
// Variant of ssdGet that copies the value out of the tree to an arbitrary destination, converting
// it in the process. Copies the default if the value is not found or can't be converted.
#define ssdCopyOutD(root, path, vtype, val_copy_out, def, lock_opt) _ssdCopyOutD(root, path, stCheckedPtrArg(vtype, val_copy_out), stArg(vtype, def), lock_opt)

// If createpath is true, the path to the value is automatically created if necessary
bool ssdSet(SSDNode *root, strref path, bool createpath, stvar val, SSDLock *lock_opt);
bool ssdRemove(SSDNode *root, strref path, SSDLock *lock_opt);

// Unlike the other ssdtree functions, the lock parameter for ssdPtr is REQUIRED.
// This function should be used as part of a larger locked transaction.
// That is because the pointer returned by this function is only guaranteed to be valid so long as
// the read or write lock is held.
stvar *ssdPtr(SSDNode *root, strref path, SSDLock *lock_req);

// Gets a string -- lock is required as this will return a pointer directly to the string inside
// the internal storage!
_meta_inline strref ssdStrRef(SSDNode *root, strref path, SSDLock *lock_req)
{
    stvar *temp = ssdPtr(root, path, lock_req);
    return stvarIs(temp, string) ? temp->data.st_string : NULL;
}

// Gets an object instance -- lock is required as this will return a pointer directly to the object
// inside the internal storage!
_meta_inline ObjInst *ssdObjInstPtr(SSDNode *root, strref path, SSDLock *lock_req)
{
    stvar *temp = ssdPtr(root, path, lock_req);
    return stvarIs(temp, object) ? temp->data.st_object : NULL;
}

// ClassName *ssdObjPtr(SSDNode *root, strref path, [ClassName], SSDLock *lock_req)
#define ssdObjPtr(root, path, clsname, lock_req) objDynCast(ssdObjInstPtr(root, path, lock_req), clsname)

// convenience functions to get various types inline with a default
#define ssdval_spec(type) \
    _meta_inline stTypeDef(type) ssdVal_##type(SSDNode *root, strref path, stTypeDef(type) def, SSDLock *lock_opt)      \
    {                                                                                                                   \
        stTypeDef(type) out;                                                                                            \
        ssdCopyOutD(root, path, type, &out, def, lock_opt);                                                             \
        return out;                                                                                                     \
    }

ssdval_spec(bool)
ssdval_spec(int8)
ssdval_spec(int16)
ssdval_spec(int32)
ssdval_spec(int64)
ssdval_spec(uint8)
ssdval_spec(uint16)
ssdval_spec(uint32)
ssdval_spec(uint64)
ssdval_spec(float32)
ssdval_spec(float64)
#define ssdVal(type, root, path, def, lock_opt) \
    ssdVal_##type(root, path, def, lock_opt)
{
    strDestroy(out);
    return ssdCopyOutD(root, path, string, out, (string)def, lock_opt);
}

// out must be an initialized array or a pointer to NULL.
bool ssdExportArray(SSDNode *root, strref path, sa_stvar *out, SSDLock *lock_opt);
bool ssdImportArray(SSDNode *root, strref path, sa_stvar arr, SSDLock *lock_opt);

bool _ssdExportTypedArray(SSDNode *root, strref path, sahandle out, stype elemtype, bool strict, SSDLock *lock_opt);
bool _ssdImportTypedArray(SSDNode *root, strref path, sa_ref arr, stype elemtype, SSDLock *lock_opt);

// out must be an initialized array or a pointer to NULL.
// If strict is set, will cause the export to fail if any items of the wrong type are encountered,
// otherwise they are simply skipped over.
#define ssdExportTypedArray(root, path, out, type, strict, lock_opt) _ssdExportTypedArray(root, path, SAHANDLE(out), stType(type), strict, lock_opt)
#define ssdImportTypedArray(root, path, arr, type, lock_opt) _ssdImportTypedArray(root, path, SAREF(arr), stType(type), lock_opt)

// Grafts a subtree of the source tree onto the dest tree.
// This deep copies the tree and associates the resulting nodes with the destination tree (creating them
// using its customized node classes if needed).
bool ssdGraft(SSDNode *dest, strref destpath, SSDLock *dest_lock_opt,
              SSDNode *src, strref srcpath, SSDLock *src_lock_opt);
