#pragma once

#include <cx/string/strbase.h>

/// @file striter.h
/// @brief String iterator API for efficient traversal

/// @defgroup string_iteration Iteration
/// @ingroup string
/// @{
///
/// String iterators provide low-level access to string bytes for scanning and
/// parsing operations. They are designed to efficiently handle both simple strings
/// and ropes without exposing the internal representation.
///
/// @section iter_concept Iterator Concept
///
/// An iterator represents a "run" of contiguous bytes within the string. For
/// simple strings, the entire string is one run. For ropes (large strings
/// built from concatenation), the string consists of multiple runs that must be
/// traversed separately.
///
/// The iterator exposes the current run through public members:
///   - bytes: Pointer to the current run of bytes
///   - off: Byte offset of this run from the start of the string
///   - len: Number of bytes in this run
///   - cursor: Current position within this run (for byte-at-a-time access)
///
/// @section iter_usage Usage Patterns
///
/// Pattern 1 - Process runs directly (most efficient):
/// @code
///   striter it;
///   striInit(&it, s);
///   while (it.len > 0) {
///       for (uint32 i = 0; i < it.len; i++) {
///           // Process it.bytes[i]
///       }
///       striNext(&it);
///   }
///   striFinish(&it);
/// @endcode
///
/// Pattern 2 - Byte-at-a-time access (simpler but slower):
/// @code
///   striter it;
///   striInit(&it, s);
///   uint8 ch;
///   while (striChar(&it, &ch)) {
///       // Process ch
///   }
///   striFinish(&it);
/// @endcode
///
/// Pattern 3 - UTF-8 code point access:
/// @code
///   striter it;
///   striInit(&it, s);
///   int32 codepoint;
///   while (striU8Char(&it, &codepoint)) {
///       // Process codepoint
///   }
///   striFinish(&it);
/// @endcode
///
/// @section iter_borrowed Borrowed Iterators
///
/// For maximum performance in controlled situations, borrowed iterators don't
/// hold a reference to the string. They can be discarded without calling
/// striFinish(), but the source string must remain valid for the iterator's
/// lifetime. Use striBorrow() instead of striInit().

CX_C_BEGIN

/// String iterator structure
///
/// Represents a position within a string and the current run of contiguous bytes.
/// Users should access the public members (bytes, off, len, cursor) but not
/// modify them directly - use the iterator functions instead.
typedef struct striter {
    // Public members - safe to read
    uint8* _Nullable bytes;   /// Pointer to current run of bytes
    uint32 off;               /// Byte offset of this run from string start
    uint32 len;               /// Length of current run in bytes
    uint32 cursor;            /// Current position within run (for striChar/striAdvance)

    // Private members - do not access directly
    string _str;
    bool _borrowed;
} striter;

/// Iterator seek type - specifies what units to seek by
typedef enum {
    STRI_BYTE,    /// Seek by byte offset
    STRI_U8CHAR   /// Seek by UTF-8 code points (slower)
} STRI_SEEK_TYPE;

/// Iterator seek origin - specifies where to seek from
typedef enum {
    STRI_SET,   /// Seek from beginning of string
    STRI_CUR,   /// Seek from current position
    STRI_END    /// Seek from end of string
} STRI_SEEK_WHENCE;

/// Initializes an iterator at the beginning of a string (forward iteration)
///
/// Creates an iterator positioned at the start of the string, set up to iterate
/// forward. The iterator holds a reference to the string, so it must be cleaned
/// up with striFinish() when done.
///
/// @param i Iterator structure to initialize
/// @param s String to iterate over
///
/// Example:
/// @code
///   striter it;
///   striInit(&it, s);
///   // ... use iterator ...
///   striFinish(&it);
/// @endcode
void striInit(_Out_ striter* _Nonnull i, _In_opt_ strref s);

/// Initializes an iterator at the end of a string (reverse iteration)
///
/// Creates an iterator positioned at the end of the string, set up to iterate
/// backward using striPrev(). The iterator holds a reference to the string.
///
/// @param i Iterator structure to initialize
/// @param s String to iterate over
///
/// Example:
/// @code
///   striter it;
///   striInitRev(&it, s);
///   while (it.len > 0) {
///       // Process run backward
///       striPrev(&it);
///   }
///   striFinish(&it);
/// @endcode
void striInitRev(_Out_ striter* _Nonnull i, _In_opt_ strref s);

/// Advances to the next run of bytes
///
/// Moves the iterator forward to the next contiguous run of bytes in the string.
/// For simple strings, this will reach the end immediately. For rope strings,
/// this advances to the next segment.
///
/// If cursor was in the middle of the previous run, the cursor position carries
/// over (becomes negative) into the new run.
///
/// @param i Iterator to advance
/// @return true if advanced to next run, false if reached end of string
///
/// Example:
/// @code
///   striter it;
///   striInit(&it, s);
///   do {
///       // Process it.bytes[0..it.len-1]
///   } while (striNext(&it));
///   striFinish(&it);
/// @endcode
bool striNext(_Inout_ striter* _Nonnull i);

/// Moves to the previous run of bytes
///
/// Moves the iterator backward to the previous contiguous run of bytes. Used for
/// reverse iteration, typically after striInitRev().
///
/// @param i Iterator to move backward
/// @return true if moved to previous run, false if reached beginning of string
///
/// Example:
/// @code
///   striter it;
///   striInitRev(&it, s);
///   do {
///       // Process run
///   } while (striPrev(&it));
///   striFinish(&it);
/// @endcode
bool striPrev(_Inout_ striter* _Nonnull i);

/// Seeks to a specific position in the string
///
/// Repositions the iterator to a new location specified by offset, type, and origin.
/// Can seek by byte offset or UTF-8 code points (slower). After seeking, the
/// iterator is positioned at the start of a run containing the target position.
///
/// @param i Iterator to reposition
/// @param off Offset to seek to
/// @param type STRI_BYTE (byte offset) or STRI_U8CHAR (UTF-8 code points)
/// @param whence STRI_SET (from start), STRI_CUR (from current), STRI_END (from end)
/// @return true if seek succeeded, false if position is out of bounds
///
/// Example:
/// @code
///   striSeek(&it, 0, STRI_BYTE, STRI_SET);     // Seek to beginning
///   striSeek(&it, 10, STRI_BYTE, STRI_CUR);    // Advance 10 bytes
///   striSeek(&it, 5, STRI_U8CHAR, STRI_SET);   // Seek to 5th code point
/// @endcode
bool striSeek(_Inout_ striter* _Nonnull i, int32 off, STRI_SEEK_TYPE type, STRI_SEEK_WHENCE whence);

/// Finishes with an iterator and releases resources
///
/// Releases the string reference held by the iterator. Must be called for every
/// iterator created with striInit() or striInitRev() to prevent memory leaks.
/// Not required for borrowed iterators (striBorrow).
///
/// @param i Iterator to finish
///
/// Example:
/// @code
///   striter it;
///   striInit(&it, s);
///   // ... use iterator ...
///   striFinish(&it);
/// @endcode
void striFinish(_Inout_ striter* _Nonnull i);

/// bool striValid(striter *i)
///
/// Tests if an iterator has data available
///
/// Returns true if the iterator is positioned at a valid run with data,
/// false if it has reached the end of the string or is invalid.
///
/// @param i Iterator to test
/// @return true if i->len > 0, false otherwise
_meta_inline bool striValid(_In_ striter* _Nonnull i)
{
    return i->len > 0;
}

/// Initializes a borrowed iterator at the beginning (forward)
///
/// Creates an iterator without holding a reference to the string. This is more
/// efficient but requires the source string to remain valid for the iterator's
/// lifetime. The iterator can be discarded without calling striFinish().
///
/// Use this for performance-critical code with well-controlled lifetimes.
///
/// @param i Iterator structure to initialize
/// @param s String to iterate over (must remain valid)
///
/// Example:
/// @code
///   striter it;
///   striBorrow(&it, s);
///   // ... use iterator ...
///   // No striFinish() needed, but s must stay valid
/// @endcode
void striBorrow(_Out_ striter* _Nonnull i, _In_opt_ strref s);

/// Initializes a borrowed iterator at the end (reverse)
///
/// Like striBorrow(), but positioned at the end for reverse iteration with striPrev().
/// Does not hold a string reference and can be discarded without striFinish().
///
/// @param i Iterator structure to initialize
/// @param s String to iterate over (must remain valid)
///
/// Example:
/// @code
///   striter it;
///   striBorrowRev(&it, s);
///   while (it.len > 0) {
///       // Process run
///       striPrev(&it);
///   }
/// @endcode
void striBorrowRev(_Out_ striter* _Nonnull i, _In_opt_ strref s);

/// @defgroup string_iteration_byte Byte-at-a-time Access
/// @ingroup string_iteration
/// @{
///
/// These functions simplify iteration by handling run boundaries automatically.
/// They are less efficient than processing runs directly, but much simpler for
/// state machine-based parsing (like UTF-8 decoding) where you need to examine
/// one byte at a time without worrying about where runs begin and end.

/// bool striChar(striter *i, uint8 *out)
///
/// Retrieves the next byte and advances cursor
///
/// Gets one byte from the current position and moves the cursor forward.
/// Automatically advances to the next run if needed. Returns false when
/// the end of the string is reached.
///
/// @param i Iterator to read from
/// @param out Pointer to store the byte
/// @return true if a byte was read, false if end of string
///
/// Example:
/// @code
///   striter it;
///   striInit(&it, s);
///   uint8 ch;
///   while (striChar(&it, &ch)) {
///       // Process ch
///   }
///   striFinish(&it);
/// @endcode
_meta_inline _Success_(return) _Must_inspect_result_ bool
striChar(_Inout_ striter* _Nonnull i, _Out_ uint8* _Nonnull out)
{
    while (i->cursor >= i->len) {
        striNext(i);
        if (i->len == 0)
            return false;
    }

    *out = i->bytes[i->cursor++];
    return true;
}

/// bool striPeekChar(striter *i, uint8 *out)
///
/// Retrieves the next byte without advancing cursor
///
/// Gets one byte from the current position but doesn't move the cursor.
/// Useful for lookahead in parsers. Automatically advances to the next
/// run if at a boundary.
///
/// @param i Iterator to read from
/// @param out Pointer to store the byte
/// @return true if a byte was read, false if end of string
///
/// Example:
/// @code
///   uint8 ch;
///   if (striPeekChar(&it, &ch) && ch == '\\') {
///       // Next char is backslash, decide how to handle
///   }
/// @endcode
_meta_inline _Success_(return) _Must_inspect_result_ bool
striPeekChar(_Inout_ striter* _Nonnull i, _Out_ uint8* _Nonnull out)
{
    while (i->cursor >= i->len) {
        striNext(i);
        if (i->len == 0)
            return false;
    }

    *out = i->bytes[i->cursor];
    return true;
}

/// bool striAdvance(striter *i, uint32 by)
///
/// Advances the cursor by a specified number of bytes
///
/// Moves the cursor forward by 'by' bytes, automatically crossing run
/// boundaries as needed. Returns false if advancing would go past the
/// end of the string.
///
/// @param i Iterator to advance
/// @param by Number of bytes to advance
/// @return true if advanced successfully, false if hit end of string
///
/// Example:
/// @code
///   // Skip next 10 bytes
///   if (striAdvance(&it, 10)) {
///       // Successfully skipped
///   }
/// @endcode
_meta_inline bool striAdvance(_Inout_ striter* _Nonnull i, uint32 by)
{
    i->cursor += by;
    while (i->cursor >= i->len) {
        striNext(i);
        if (i->len == 0)
            return (i->cursor == 0);   // return true for hitting end of string exactly
    }
    return true;
}

/// @}  // end of string_iteration_byte group

#define _striU8Anno _Success_(return) _Must_inspect_result_

/// @defgroup string_iteration_utf8 UTF-8 Code Point Access
/// @ingroup string_iteration
/// @{
///
/// These functions operate on UTF-8 code points rather than raw bytes.
/// They handle multi-byte sequences automatically.

/// Decodes the next UTF-8 sequence into a Unicode code point and advances
/// the cursor past the entire sequence (1-4 bytes). Returns false if the
/// end of string is reached or invalid UTF-8 is encountered.
///
/// @param i Iterator to read from
/// @param out Pointer to store the Unicode code point
/// @return true if a code point was decoded, false on end of string or error
///
/// Example:
/// @code
///   striter it;
///   striInit(&it, utf8String);
///   int32 codepoint;
///   while (striU8Char(&it, &codepoint)) {
///       // Process Unicode code point
///   }
///   striFinish(&it);
/// @endcode
_striU8Anno bool striU8Char(_Inout_ striter* _Nonnull i, _Out_ int32* out);

#define _striPeekU8Anno _Success_(return) _Must_inspect_result_

/// Retrieves the next UTF-8 code point without advancing cursor
///
/// Decodes the next UTF-8 sequence but doesn't advance the cursor.
/// Useful for lookahead in parsers.
///
/// @param i Iterator to read from
/// @param out Pointer to store the Unicode code point
/// @return true if a code point was decoded, false on end of string or error
///
/// Example:
/// @code
///   int32 codepoint;
///   if (striPeekU8Char(&it, &codepoint)) {
///       // Look at next code point without consuming it
///   }
/// @endcode
_striPeekU8Anno bool striPeekU8Char(_Inout_ striter* _Nonnull i, _Out_ int32* out);

/// Advances by a specified number of UTF-8 code points
///
/// Moves forward by 'by' UTF-8 characters, automatically handling multi-byte
/// sequences. Returns false if the end of string is reached or invalid UTF-8
/// is encountered.
///
/// @param i Iterator to advance
/// @param by Number of UTF-8 code points to skip
/// @return true if advanced successfully, false on end of string or error
///
/// Example:
/// @code
///   // Skip next 5 UTF-8 characters
///   if (striAdvanceU8(&it, 5)) {
///       // Successfully skipped 5 code points
///   }
/// @endcode
bool striAdvanceU8(_Inout_ striter* _Nonnull i, uint32 by);

/// @}  // end of string_iteration_utf8 group

/// @}  // end of string_iteration group

CX_C_END
