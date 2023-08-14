#pragma once

#include <cx/thread/rwlock.h>
#include <cx/meta/block.h>

#if DEBUG_LEVEL > 0
#define SSD_LOCK_DEBUG 1
#endif

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

typedef struct SSDLockDebug {
    int64 time;
    const char *file;
    int32 line;
} SSDLockDebug;
saDeclare(SSDLockDebug);

typedef struct SSDLock
{
    bool init;                          // lock structure initialized
    bool rdlock;                        // read lock held by current thread
    bool wrlock;                        // write lock held by current thread
#ifdef SSD_LOCK_DEBUG
    SSDLockDebug dbg;
#endif
} SSDLock;

// Initializes a lock structure
#ifdef SSD_LOCK_DEBUG
void _ssdLockInit(SSDLock *lock, const char *fn, int lnum);
#define ssdLockInit(lock) _ssdLockInit(lock, __FILE__, __LINE__)
#else
void ssdLockInit(SSDLock *lock);
#endif

// bool ssdLockRead(SSDNode *root, SSDLock *lock);
//
// Starts a locked operation in read mode
#ifdef SSD_LOCK_DEBUG
#define ssdLockRead(root, lock) _ssdLockRead(SSDNode(root), lock, __FILE__, __LINE__)
bool _ssdLockRead(SSDNode *root, SSDLock *lock, const char *fn, int lnum);
#else
#define ssdLockRead(root, lock) _ssdLockRead(SSDNode(root), lock)
bool _ssdLockRead(SSDNode *root, SSDLock *lock);
#endif

// bool ssdLockWrite(SSDNode *root, SSDLock *lock);
//
// Starts a operation in write mode or upgrades one from read to write
// NOTE, upgrading the lock drops it briefly, do not assume that no one else
// got the write lock in between! State may be changed between dropping the
// read lock and getting the write lock.
#ifdef SSD_LOCK_DEBUG
#define ssdLockWrite(root, lock) _ssdLockWrite(SSDNode(root), lock, __FILE__, __LINE__)
bool _ssdLockWrite(SSDNode *root, SSDLock *lock, const char *fn, int lnum);
#else
#define ssdLockWrite(root, lock) _ssdLockWrite(SSDNode(root), lock)
bool _ssdLockWrite(SSDNode *root, SSDLock *lock);
#endif

// Unlocks the lock, potentially for re-use laster
#define ssdUnlock(root, lock) ssdUnlock(SSDNode(root), lock)
bool _ssdUnlock(SSDNode *root, SSDLock *lock);

// Ends a locked operation; destroys the lock
#define ssdEndLock(root, lock) _ssdEndLock(SSDNode(root), lock)
bool _ssdEndLock(SSDNode *root, SSDLock *lock);

#define withSSDLock(root, name) blkWrap(SSDLock name = { .init = true }, ssdEndLock(root, &name))
