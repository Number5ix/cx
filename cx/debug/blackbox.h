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
///   bboxSet(_SL("version"), _SL("1.0.3"), 0);
///   bboxSet(_SL("session"), sessionId, 0);
///   bboxSet(_SL("username"), username, BBox_Private);
/// @endcode
///
/// **Buffers:**
///
/// bboxSet() takes a lock and searches for space, so it is too slow to call from a
/// performance sensitive loop. For a value that changes often, reserve a buffer once with
/// bboxReserve() and write a null-terminated string into it each time the value changes.
/// Writing to the buffer needs no black box call.
///
/// - Only one thread at a time should write to a buffer
/// - A crash in the middle of a write records a partly written string
/// - Buffers are meant for short strings, around 64 to 128 bytes
///
/// @code
///   char *state = bboxReserve(_SL("worker_state"), 64, 0);
///
///   for (int i = 0; i < count; i++) {
///       if (state)
///           snprintf(state, 64, "item %d of %d", i, count);
///       processItem(i);
///   }
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
/// The value of a `BBox_Buffer` entry ends at its first null byte.
typedef struct BlackBoxEnt {
    uint16 prev;     ///< Offset to previous entry (0 if first)
    uint16 next;     ///< Offset to next entry (0 if last)
    uint8 flags;     ///< Flags from BLACKBOX_FLAGS enum
    uint8 namelen;   ///< Length of name including null terminator
    uint16 vallen;   ///< Length of value including null terminator; for a `BBox_Buffer`
                     ///< entry, the space reserved for the value
    char name[1];    ///< Variable-length name string (null-terminated)
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
    BBox_Private = 0x01,   ///< Entry contains potentially private data; allow user opt-out in crash
                           ///< reports
    BBox_Buffer  = 0x02,   ///< Entry is a buffer from bboxReserve(); set automatically
};

/// Initialize the black box system
///
/// Calling this is optional; the black box initializes itself on first use.
void bboxInit(void);

/// Store or update a key-value pair in the black box
///
/// If an entry with the same name already exists:
/// - If new value fits in existing space, updates in-place
/// - Otherwise, deletes old entry and allocates new space
///
/// Uses best-fit allocation strategy. If the black box is full, nothing is stored
/// and any existing entry with that name is removed.
///
/// If the name belongs to a buffer from bboxReserve(), the value is copied into the
/// buffer and cut short if it does not fit.
///
/// @param name Key name, up to 254 bytes (copied into black box)
/// @param val Value to store (copied into black box)
/// @param flags Optional flags from BLACKBOX_FLAGS enum (0 for none)
/// @return true if the value was stored, false if the black box is full or the name
///         is too long
bool bboxSet(_In_ strref name, _In_ strref val, flags_t flags);

/// Reserve a buffer in the black box that can be written to directly
///
/// The buffer starts out filled with zeros and stays valid until the entry is removed
/// with bboxDelete(). Its contents are recorded as the value of the entry.
///
/// Reserving a name that already has a buffer of at least `size` bytes returns that
/// same buffer with its contents unchanged.
///
/// @param name Key name, up to 254 bytes (copied into black box)
/// @param size Size of the buffer in bytes, including room for the null terminator
/// @param flags Optional flags from BLACKBOX_FLAGS enum (0 for none)
/// @return Pointer to the buffer, or NULL if the black box is full or the name is
///         already used by an entry that is not a large enough buffer
///
/// Example:
/// @code
///   char *phase = bboxReserve(_SL("phase"), 32, 0);
///   if (phase)
///       strCopyOut(_SL("loading"), 0, (uint8*)phase, 32);
/// @endcode
_Ret_maybenull_ char* bboxReserve(_In_ strref name, uint32 size, flags_t flags);

/// Remove an entry from the black box
///
/// Frees the space used by the entry. If the entry doesn't exist,
/// this is a no-op. A buffer from bboxReserve() must not be used after
/// its entry is removed.
///
/// @param name Key name of entry to delete
void bboxDelete(_In_ strref name);

/// @}  // end of debug_blackbox group

CX_C_END
