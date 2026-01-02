#pragma once

#include <cx/thread/rwlock.h>
#include <cx/meta/block.h>
#include <cx/container/sarray.h>

#if DEBUG_LEVEL > 1
#define SSD_LOCK_DEBUG 1
#endif

typedef struct SSDTree SSDTree;
typedef struct SSDNode SSDNode;
typedef SSDNode *(*SSDNodeFactory)(SSDTree *info);

/// @addtogroup ssd_create
/// @{

/// Flags for configuring SSD tree behavior
enum SSD_FLAGS_ENUM {
    SSD_CaseInsensitive = 0x0001,       ///< Keys in hashtables are case-insensitive
};

/// Node creation types for specifying which kind of node to create
typedef enum SSD_CREATE_TYPE_ENUM {
    SSD_Create_None = 0,        ///< Do not create a node
    SSD_Create_Hashtable,       ///< Create a hashtable (key-value) node
    SSD_Create_Array,           ///< Create an array (indexed) node
    SSD_Create_Single,          ///< Create a single-value node

    SSD_Create_Count            ///< Total number of creation types (internal use)
} SSDCreateType;

/// @}

typedef struct SSDLockDebug {
    int64 time;
    const char *file;
    int32 line;
} SSDLockDebug;
saDeclare(SSDLockDebug);

typedef struct SSDLockState
{
    union {
        bool _is_SSDLockState;
        bool init;                      // lock state structure initialized
    };
    bool rdlock;                        // read lock held by current thread
    bool wrlock;                        // write lock held by current thread
#ifdef SSD_LOCK_DEBUG
    SSDLockDebug dbg;
#endif
} SSDLockState;

enum SSD_LOCK_STATE_ENUM {
    _ssdCurrentLockState = 0
};

// Initializes a lock structure
#ifdef SSD_LOCK_DEBUG
SSDLockState *__ssdLockStateInit(_Out_ SSDLockState *lstate, _In_z_ const char *fn, int lnum);
#define _ssdLockStateInit(lstate) __ssdLockStateInit(lstate, __FILE__, __LINE__)
#else
SSDLockState *_ssdLockStateInit(_Out_ SSDLockState *lstate);
#endif

/// @defgroup ssd_lock Locking
/// @ingroup ssd
/// @{
///
/// Manual locking functions for fine-grained control over thread synchronization.
///
/// **NOTE:** Most users should use ssdLockedTransaction() instead of manual locking.
/// Manual locking is only needed for advanced scenarios where you need explicit control
/// over lock acquisition and release.
///
/// **Lock upgrade warning:** When upgrading from read to write lock, the read lock is
/// briefly dropped. Other threads may acquire the write lock in between, so do not assume
/// the tree state remains unchanged during the upgrade.

/// bool ssdLockRead(SSDNode *root)
///
/// Starts a locked operation in read mode.
///
/// Acquires a read lock on the tree. Multiple threads can hold read locks simultaneously,
/// but write operations will block until all read locks are released.
///
/// @param root The root node of the tree to lock
/// @return true on success
///
/// Example:
/// @code
///   ssdLockRead(root);
///   // perform read operations
///   ssdLockEnd(root);
/// @endcode
#ifdef SSD_LOCK_DEBUG
#define ssdLockRead(root) _ssdLockRead(SSDNode(root), (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState, __FILE__, __LINE__)
#define _ssdManualLockRead(root, lstate) _ssdLockRead(SSDNode(root), lstate, __FILE__, __LINE__)
bool _ssdLockRead(_Inout_ SSDNode *root, _Inout_ SSDLockState *lstate, _In_z_ const char *fn, int lnum);
#else
#define ssdLockRead(root) _ssdLockRead(SSDNode(root), _ssdCurrentLockState)
#define _ssdManualLockRead(root, lstate) _ssdLockRead(SSDNode(root), lstate)
bool _ssdLockRead(_Inout_ SSDNode *root, _Inout_ SSDLockState *lstate);
#endif

/// bool ssdLockWrite(SSDNode *root)
///
/// Starts an operation in write mode or upgrades a read lock to write.
///
/// Acquires a write lock on the tree. Only one thread can hold a write lock at a time,
/// and no read locks can be held while a write lock is active.
///
/// **CRITICAL:** If upgrading from a read lock, the lock is briefly dropped during the
/// upgrade. Do not assume that no other thread obtained the write lock in between!
/// The tree state may have changed between dropping the read lock and acquiring the write lock.
///
/// @param root The root node of the tree to lock
/// @return true on success
///
/// Example:
/// @code
///   ssdLockWrite(root);
///   // perform write operations
///   ssdLockEnd(root);
/// @endcode
#ifdef SSD_LOCK_DEBUG
#define ssdLockWrite(root) _ssdLockWrite(SSDNode(root), (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState, __FILE__, __LINE__)
#define _ssdManualLockWrite(root, lstate) _ssdLockWrite(SSDNode(root), lstate, __FILE__, __LINE__)
bool _ssdLockWrite(_Inout_ SSDNode *root, _Inout_ SSDLockState *lstate, _In_z_ const char *fn, int lnum);
#else
#define ssdLockWrite(root) _ssdLockWrite(SSDNode(root), _ssdCurrentLockState)
#define _ssdManualLockWrite(root, lstate) _ssdLockWrite(SSDNode(root), lstate)
bool _ssdLockWrite(_Inout_ SSDNode *root, _Inout_ SSDLockState *lstate);
#endif

/// bool ssdUnlock(SSDNode *root)
///
/// Temporarily unlocks the lock while preserving lock state for later reuse.
///
/// This allows other threads to access the tree while maintaining the lock state
/// structure for potential reacquisition. Use this when you need to temporarily
/// release the lock within a longer operation.
///
/// @param root The root node of the tree to unlock
/// @return true on success
#define ssdUnlock(root) _ssdUnlock(SSDNode(root), (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState)
bool _ssdUnlock(_Inout_ SSDNode *root, _Inout_ SSDLockState *lstate);

/// bool ssdLockEnd(SSDNode *root)
///
/// Ends a locked operation and destroys the lock state.
///
/// This releases the lock and cleans up the associated lock state. Always call this
/// when done with a manually acquired lock.
///
/// @param root The root node of the tree to unlock
/// @return true on success
#define ssdLockEnd(root) _ssdLockEnd(SSDNode(root), (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState)
bool _ssdLockEnd(_Inout_ SSDNode *root, _Inout_ SSDLockState *lstate);

/// ssdLockedTransaction(root)
///
/// Wraps a group of SSD operations in an automatically managed lock transaction.
///
/// This is the recommended way to perform multiple SSD operations that should execute
/// together atomically. The lock is automatically acquired at the start of the block
/// and released at the end, even if the block exits early.
///
/// Functions that return pointers to internal storage (like ssdPtr(), ssdStrRef(),
/// ssdObjPtr(), etc.) MUST be used within a locked transaction.
///
/// @param root The root node of the tree to lock
///
/// Example:
/// @code
///   ssdLockedTransaction(root) {
///       // Read lock acquired automatically
///       strref name = ssdStrRef(root, _S"user/name");
///       int32 age = ssdVal(int32, root, _S"user/age", 0);
///       
///       // Can upgrade to write lock if needed
///       ssdSet(root, _S"user/lastAccess", true, stvar(int64, clockTimer()));
///   }  // Lock released automatically
/// @endcode
#define ssdLockedTransaction(root) _blkStart                                                    \
    _inhibitReturn                                                                              \
    _blkFull(SSDLockState _ssdTransientLockState = { 0 }, (root),                               \
        _ssdLockEnd(SSDNode(root), &_ssdTransientLockState))                                    \
    _blkBefore(SSDLockState *_ssdCurrentLockStateShadow = (SSDLockState*)_ssdCurrentLockState)  \
    _blkBefore(SSDLockState *_ssdCurrentLockState = _ssdCurrentLockStateShadow ?                \
        _ssdCurrentLockStateShadow : _ssdLockStateInit(&_ssdTransientLockState))                \
    _blkEnd

/// @}  // end of ssd_lock
