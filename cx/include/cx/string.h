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
///   string s1 = _S"hello";      // Create a string
///   string s2 = 0;
///   strDup(&s2, s1);            // "Copy" s1 to s2 (shares buffer internally)
///   strAppend(&s1, _S" world"); // Modify s1 (gets its own copy automatically)
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
///   strEq(s, _S"");              // Returns true
///   strDestroy(&s);              // Safe, does nothing
///   strAppend(&s, _S"hello");    // Works, creates new string
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
///   string s = _S"Hello";        // Static ASCII string literal
///   string s = _SU"Hello 世界";  // Static UTF-8 string literal
/// @endcode
///
/// Building strings dynamically:
/// @code
///   string s = 0;                 // Always initialize to NULL
///   strReset(&s, 256);            // Create empty with capacity hint
///   strDup(&s, _S"content");      // Copy from another string
///   strAppend(&s, _S" more");     // Append operations
/// @endcode
///
/// @section memory_management Memory Management
///
/// All strings created dynamically (not static literals) MUST be destroyed:
/// @code
///   string s = 0;
///   strDup(&s, _S"hello");
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
