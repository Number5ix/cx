#pragma once

#include <cx/thread/rwlock.h>
#include <cx/meta/block.h>

typedef struct SSDTree SSDTree;
typedef struct SSDNode SSDNode;
typedef SSDNode *(*SSDNodeFactory)(SSDTree *info);

enum SSD_FLAGS_ENUM {
    SSD_CaseInsensitive = 0x0001,       // keys in hashtables are case-insensitive
};

enum SSD_CREATE_TYPE_ENUM {
    SSD_Create_None = 0,
    SSD_Create_Hashtable,
    SSD_Create_Array,
    SSD_Create_Single,

    SSD_Create_Count
};

typedef struct SSDLock
{
    bool init;                          // lock structure initialized
    bool rdlock;                        // read lock held by current thread
    bool wrlock;                        // write lock held by current thread
} SSDLock;

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
