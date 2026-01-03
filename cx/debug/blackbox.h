#pragma once

/// @file blackbox.h
/// @brief Black box data recorder for crash dump analysis
///
/// @defgroup debug_blackbox Black Box
/// @ingroup debug
/// @{
///
/// Fixed-size key-value data store optimized for post-mortem crash analysis.
///
/// The black box is a fixed 64KB buffer that stores debugging metadata as key-value
/// pairs in a custom format designed to be parsed from raw memory dumps. This allows
/// crash analyzers to extract application state even when the process is completely
/// dead.
///
/// **Design characteristics:**
/// - Fixed 64KB size (`BLACKBOX_SIZE`) with inline allocation
/// - Globally accessible symbol (`dbgBlackBox`) visible to debuggers
/// - Doubly-linked list structure for sequential traversal
/// - Best-fit allocation strategy with coalescing
/// - Thread-safe with internal locking
/// - Supports in-place updates when values shrink or stay same size
///
/// **Typical use cases:**
/// - Storing application version, build info, session IDs
/// - Recording recent operation history or state transitions
/// - Capturing configuration values that might affect behavior
/// - Logging error codes or failure conditions
///
/// **Example:**
/// @code
///   bboxInit();
///   bboxSet(_S"version", _S"1.0.3", 0);
///   bboxSet(_S"session", sessionId, 0);
///   bboxSet(_S"username", username, BBox_Private);
/// @endcode

#include "cx/cx.h"
#include "cx/string.h"

CX_C_BEGIN

/// Size of the black box buffer in bytes (64KB)
#define BLACKBOX_SIZE 65535

/// Global black box buffer visible to debuggers and crash dump analyzers
extern char dbgBlackBox[];

/// Black box entry structure for offline parsing
///
/// Internal structure used to parse black box contents from crash dumps.
/// Entries are stored as a doubly-linked list with variable-length name and value.
typedef struct BlackBoxEnt {
    uint16 prev;        ///< Offset to previous entry (0 if first)
    uint16 next;        ///< Offset to next entry (0 if last)
    uint8 flags;        ///< Flags from BLACKBOX_FLAGS enum
    uint8 namelen;      ///< Length of name including null terminator
    uint16 vallen;      ///< Length of value including null terminator
    char name[1];       ///< Variable-length name string (null-terminated)
    // char val[];      ///< Variable-length value string follows name
} BlackBoxEnt;

/// Get pointer to value string within a black box entry
///
/// char* bboxGetVal(ent)
///
/// @param ent Pointer to BlackBoxEnt structure
/// @return Pointer to the null-terminated value string
#define bboxGetVal(ent) ((char*)ent + offsetof(BlackBoxEnt, name) + ent->namelen)

/// Calculate total size of a black box entry in bytes
///
/// size_t bboxEntSize(ent)
///
/// @param ent Pointer to BlackBoxEnt structure
/// @return Total size including structure, name, and value
#define bboxEntSize(ent) (offsetof(BlackBoxEnt, name) + ent->namelen + ent->vallen)

/// Flags for black box entries
enum BLACKBOX_FLAGS {
    BBox_Private        = 0x01, ///< Entry contains potentially private data; allow user opt-out in crash reports
};

/// Initialize the black box system
///
/// Must be called before using bboxSet or bboxDelete.
/// Initializes internal mutex, index, and free list.
void bboxInit();

/// Store or update a key-value pair in the black box
/// @param name Key name (stored by reference, must remain valid)
/// @param val Value to store (copied into black box)
/// @param flags Optional flags from BLACKBOX_FLAGS enum (0 for none)
///
/// If an entry with the same name already exists:
/// - If new value fits in existing space, updates in-place
/// - Otherwise, deletes old entry and allocates new space
///
/// Uses best-fit allocation strategy. If blackbox is full, the operation
/// silently fails (no error or assertion).
void bboxSet(_In_ strref name, _In_ strref val, uint8 flags);

/// Remove an entry from the black box
/// @param name Key name of entry to delete
///
/// Frees the space used by the entry. If the entry doesn't exist,
/// this is a no-op.
void bboxDelete(_In_ strref name);

/// @}  // end of debug_blackbox group

CX_C_END
