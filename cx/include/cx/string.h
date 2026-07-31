#pragma once

#include <cx/string/strbase.h>

#include <cx/string/strcompare.h>
#include <cx/string/strencoding.h>
#include <cx/string/strfind.h>
#include <cx/string/striter.h>
#include <cx/string/strmanip.h>
#include <cx/string/strnum.h>

/// @file string.h
/// @brief Copy-on-write strings with automatic memory management and rope optimization

/// @defgroup string Strings
/// @{
/// High-level string abstraction with automatic memory management, copy-on-write
/// semantics, and transparent optimization for both small and large strings.

/// @defgroup string_overview Overview
/// @ingroup string
/// @{
///
/// @section string_types String Types
///
/// The library uses three related types:
///
/// - **string** - Owning handle to a string value (like char* for allocated memory).
///   Can be modified, must be destroyed with strDestroy()
///
/// - **strref** - Borrowed reference to a string (analogous to const char* in C).
///   Read-only view that doesn't own the string, cannot be modified.
///   Safe for function parameters, but lifetime is tied to the source.
///   Can be duplicated into an owning string variable with strDup()
///
/// - **strhandle** - Pointer to a string variable (string*).
///   Used as output parameters for functions that create/modify strings
///
/// Most functions take strref for input (read-only) and strhandle for output.
/// This pattern prevents accidental modification and makes ownership clear.
///
/// @section conceptual_model Conceptual Model - Immutable String Semantics
///
/// While the implementation uses copy-on-write optimization, the API is designed
/// to behave as if strings were immutable values. Think of string variables like
/// you would in Python, Java, or other languages with immutable strings:
///
/// @code
///   string s1 = _SL("hello");      // Create a string
///   string s2 = 0;
///   strDup(&s2, s1);            // "Copy" s1 to s2 (shares buffer internally)
///   strAppend(&s1, _SL(" world")); // Modify s1 (gets its own copy automatically)
/// @endcode
///
/// After the append, s1 and s2 appear to be independent strings, even though they
/// initially shared the same buffer. The copy-on-write mechanism is transparent -
/// you program as if each variable owns its own immutable string value, but the
/// library optimizes away unnecessary copies behind the scenes.
///
/// This mental model makes it easy to reason about string operations:
///   - "Copying" a string (strDup) is cheap - just a pointer copy and ref count bump
///   - Modifying a string automatically makes it unique if needed
///   - You never have to worry about whether other variables share your buffer
///   - Each variable behaves as an independent string value
///
/// @section null_strings NULL Strings
///
/// A string variable set to NULL (or 0) is treated as an empty string throughout
/// the entire API. This is an intentional design decision for convenience:
///
/// @code
///   string s = 0;                // NULL string
///   strLen(s);                   // Returns 0
///   strEmpty(s);                 // Returns true
///   strEq(s, _SL(""));              // Returns true
///   strDestroy(&s);              // Safe, does nothing
///   strAppend(&s, _SL("hello"));    // Works, creates new string
/// @endcode
///
/// This eliminates the need for null checks in most code and allows operations
/// to work naturally with uninitialized or empty strings. Functions that return
/// strings via output parameters will properly destroy any existing string first,
/// so you can always safely initialize to NULL and let the functions manage it.
///
/// @section cow_mechanics Copy-on-Write Mechanics
///
/// The library uses atomic reference counting to track how many variables reference
/// the same underlying string buffer. When you use strDup(), both variables point
/// to the same buffer with an incremented reference count. When a modification
/// operation is performed (strAppend, strBuffer, etc.), the library automatically
/// checks the reference count and creates a private copy if the buffer is shared.
///
/// String literals created with the _S macro have static storage and don't need
/// reference counting - they're truly immutable and exist for the program's lifetime.
///
/// @section creating_strings Creating Strings
///
/// String literals:
/// @code
///   string s = _SL("Hello");        // Static ASCII string literal
///   string s = _SU"Hello 世界";  // Static UTF-8 string literal
/// @endcode
///
/// Building strings dynamically:
/// @code
///   string s = 0;                 // Always initialize to NULL
///   strReset(&s, 256);            // Create empty with capacity hint
///   strDup(&s, _SL("content"));      // Copy from another string
///   strAppend(&s, _SL(" more"));     // Append operations
/// @endcode
///
/// @section cstring_interop C String Interop
///
/// A plain null-terminated C string can be cast directly to strref (or string) and
/// passed to any function in the API. No conversion call and no allocation are
/// involved:
///
/// @code
///   strDup(&program, (string)argv[0]);        // works anywhere a string is accepted
///   if (strEq((strref)getenv("HOME"), homedir)) { ... }
/// @endcode
///
/// Every cx string begins with a two-byte marker: a flags byte with the high bit set,
/// followed by 0xC1. That sequence is deliberately invalid UTF-8, so no valid UTF-8 C
/// string can be mistaken for a cx string. When the marker is absent, the library
/// treats the pointer as a plain C string - content starts at offset 0 and the length
/// is computed with strlen().
///
/// The important difference from string literals is lifetime. A literal created with
/// _S / _SL / STR_CONST lives in a read-only section for the life of the program, so
/// the library is free to reference it indefinitely. A C string cast this way is
/// assumed to have unknown ownership and lifetime and to be mutable, so anything that
/// retains it makes a copy instead of taking a reference:
///
///   - strDup() copies into a new cx string rather than incrementing a reference count
///   - Storing one in a container or an stvar copies it (the string stype copy operation
///     uses strDup)
///   - Rope references created by concatenation or substring operations copy it
///
/// The copy is a snapshot taken at the call, so mutating or freeing the C buffer
/// afterwards cannot corrupt the resulting cx string.
///
/// A C string can also be held in a string handle and used directly. Modification
/// triggers a copy-on-write, so the C buffer is never written through the handle, and
/// strDestroy() on an unmodified handle simply clears it without freeing anything:
///
/// @code
///   string s = (string)buf;        // no copy, no allocation
///   strAppend(&s, _SL(".txt"));    // s becomes a private heap string; buf is untouched
///   strDestroy(&s);                // still required - s may now own a heap buffer
/// @endcode
///
/// Points to be aware of:
///
///   - strLen() on a C string is O(n) and is recomputed on every call, since there is
///     no cached length. Duplicate it into a cx string if it is used repeatedly.
///   - The encoding is unknown and cannot be cached on a buffer the library does not
///     own, so strValidUTF8() / strValidASCII() rescan on every call, and results of
///     operations involving one have their cached encoding flags cleared.
///   - This path is not binary safe: embedded NULs terminate the string, and arbitrary
///     binary data may collide with the header marker. Use strBuffer() and strSetLen()
///     to build strings from raw bytes.
///   - An empty C string ("") is treated the same as NULL, which is itself a valid
///     empty string throughout the API.
///   - Iterators do not take a reference to a non-cx string, so the C buffer must
///     outlive any striter created from it.
///   - Wide (UTF-16) C strings have no cast path; convert them with strFromUTF16().
///
/// @section memory_management Memory Management
///
/// All strings created dynamically (not static literals) MUST be destroyed:
/// @code
///   string s = 0;
///   strDup(&s, _SL("hello"));
///   // ... use s ...
///   strDestroy(&s);               // Required! Decrements ref count, frees if last
/// @endcode
///
/// Strings initialized to NULL/0 are safe to destroy without allocation.
///
/// @section thread_safety Thread Safety
///
/// Reference counting uses atomic operations for thread safety. Multiple threads
/// can safely call strDup() on the same source string concurrently. However,
/// modifying a string is not thread-safe - if multiple threads need to modify
/// the same logical string, external synchronization is required.
///
/// @section optimizations Optimizations
///
/// The library automatically optimizes for different use cases:
///   - Small strings use compact headers with inline length
///   - Large concatenated strings may use rope data structures internally
///   - Stack allocation available for temporary strings (strTemp)
///   - Encoding awareness (ASCII, UTF-8) for optimizations

/// @}  // end of string_overview group
/// @}  // end of string group
