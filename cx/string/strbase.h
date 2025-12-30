#pragma once

// ---------- CX String Library ----------
//
// High-level string abstraction with automatic memory management, copy-on-write
// semantics, and transparent optimization for both small and large strings.
//
// CONCEPTUAL MODEL - Immutable String Semantics:
//
// While the implementation uses copy-on-write optimization, the API is designed
// to behave as if strings were immutable values. Think of string variables like
// you would in Python, Java, or other languages with immutable strings:
//
//   string s1 = _S"hello";      // Create a string
//   string s2 = 0;
//   strDup(&s2, s1);            // "Copy" s1 to s2 (shares buffer internally)
//   strAppend(&s1, _S" world"); // Modify s1 (gets its own copy automatically)
//
// After the append, s1 and s2 appear to be independent strings, even though they
// initially shared the same buffer. The copy-on-write mechanism is transparent -
// you program as if each variable owns its own immutable string value, but the
// library optimizes away unnecessary copies behind the scenes.
//
// This mental model makes it easy to reason about string operations:
//   - "Copying" a string (strDup) is cheap - just a pointer copy and ref count bump
//   - Modifying a string automatically makes it unique if needed
//   - You never have to worry about whether other variables share your buffer
//   - Each variable behaves as an independent string value
//
// STRING TYPES:
//
// The library uses three related types:
//
//   string   - Owning handle to a string value (like char* for allocated memory)
//              Can be modified, must be destroyed with strDestroy()
//
//   strref   - Borrowed reference to a string (analogous to const char* in C)
//              Read-only view that doesn't own the string, cannot be modified
//              Safe for function parameters, but lifetime is tied to the source
//              Can be duplicated into an owning string variable with strDup()
//
//   strhandle - Pointer to a string variable (string*)
//               Used as output parameters for functions that create/modify strings
//
// Most functions take strref for input (read-only) and strhandle for output.
// This pattern prevents accidental modification and makes ownership clear.
//
// NULL STRINGS:
//
// A string variable set to NULL (or 0) is treated as an empty string throughout
// the entire API. This is an intentional design decision for convenience:
//
//   string s = 0;                // NULL string
//   strLen(s);                   // Returns 0
//   strEmpty(s);                 // Returns true
//   strEq(s, _S"");              // Returns true
//   strDestroy(&s);              // Safe, does nothing
//   strAppend(&s, _S"hello");    // Works, creates new string
//
// This eliminates the need for null checks in most code and allows operations
// to work naturally with uninitialized or empty strings. Functions that return
// strings via output parameters will properly destroy any existing string first,
// so you can always safely initialize to NULL and let the functions manage it.
//
// COPY-ON-WRITE MECHANICS:
//
// The library uses atomic reference counting to track how many variables reference
// the same underlying string buffer. When you use strDup(), both variables point
// to the same buffer with an incremented reference count. When a modification
// operation is performed (strAppend, strBuffer, etc.), the library automatically
// checks the reference count and creates a private copy if the buffer is shared.
//
// String literals created with the _S macro have static storage and don't need
// reference counting - they're truly immutable and exist for the program's lifetime.
//
// CREATING STRINGS:
//
// String literals:
//   string s = _S"Hello";        // Static ASCII string literal
//   string s = _SU"Hello 世界";  // Static UTF-8 string literal
//
// Building strings dynamically:
//   string s = 0;                 // Always initialize to NULL
//   strReset(&s, 256);            // Create empty with capacity hint
//   strDup(&s, _S"content");      // Copy from another string
//   strAppend(&s, _S" more");     // Append operations
//
// MEMORY MANAGEMENT:
//
// All strings created dynamically (not static literals) MUST be destroyed:
//   string s = 0;
//   strDup(&s, _S"hello");
//   // ... use s ...
//   strDestroy(&s);               // Required! Decrements ref count, frees if last
//
// Strings initialized to NULL/0 are safe to destroy without allocation.
//
// THREAD SAFETY:
//
// Reference counting uses atomic operations for thread safety. Multiple threads
// can safely call strDup() on the same source string concurrently. However,
// modifying a string is not thread-safe - if multiple threads need to modify
// the same logical string, external synchronization is required.
//
// OTHER NOTES:
//
// The library automatically optimizes for different use cases:
//   - Small strings use compact headers with inline length
//   - Large concatenated strings may use rope data structures internally
//   - Stack allocation available for temporary strings (strTemp)
//   - Encoding awareness (ASCII, UTF-8) for optimizations

#include <cx/cx.h>

CX_C_BEGIN

typedef struct str_ref {
    void *_is_string;
} str_ref;

// Opaque handle to a string object
//
// Strings use copy-on-write semantics with automatic reference counting.
// Multiple string variables can efficiently share the same underlying buffer.
//
// CRITICAL: Always initialize to NULL or 0 before first use:
//   string s = 0;
//
// Strings must be destroyed with strDestroy() when no longer needed.
typedef struct str_ref* _Nullable string;

// Borrowed reference to a string
//
// Used for passing strings as function arguments without transferring ownership.
// The referenced string must remain valid for the duration of the function call.
//
// IMPORTANT: Never return a strref from a function in multi-threaded programs.
// This creates a race condition where the underlying string could be destroyed
// by another thread. Always use an output parameter with strDup() instead.
typedef const struct str_ref* _Nullable strref;

// Pointer to a string variable
//
// Used as an output parameter for functions that modify or create strings.
// Must always point to a valid string variable (which may be NULL).
typedef string* _Nonnull strhandle;

// void strInit(string *o);
//
// Initializes a string variable to empty/NULL
//
// This is a convenience macro equivalent to setting the string to NULL.
// While not strictly required (direct assignment works), it provides clarity.
//
// Parameters:
//   o - Pointer to string variable to initialize
//
// Example:
//   string s;
//   strInit(&s);
#define strInit(o) *(o) = NULL

// Creates a new empty string with preallocated storage
//
// If the output string already exists, it is destroyed first. The new string
// is allocated with enough capacity for the specified size plus overhead.
// This is useful when you know approximately how large the string will grow.
//
// The string is initialized with UTF-8 and ASCII encoding flags set.
//
// Parameters:
//   o - Pointer to string variable (existing string will be destroyed)
//   sizehint - Expected string size in bytes (0 for default minimum)
//
// Example:
//   string s = 0;
//   strReset(&s, 256);  // Preallocate for ~256 byte string
void strReset(_Inout_ptr_opt_ strhandle o, uint32 sizehint);

// Duplicates a string using copy-on-write optimization
//
// Creates a reference to the source string when possible, avoiding copying.
// The reference count is incremented atomically for thread safety. If the
// source is not a CX-managed string, a plain C string, or a stack-allocated
// string, a copy is made instead.
//
// Special case: If the output is a stack-allocated string, the content is
// copied into the stack buffer rather than replacing the handle.
//
// The output string is destroyed first if it already exists.
//
// Parameters:
//   o - Pointer to output string variable
//   s - String to duplicate (NULL creates empty string)
//
// Example:
//   string s1 = 0;
//   strDup(&s1, _S"hello");  // Efficient reference
//   string s2 = 0;
//   strDup(&s2, s1);         // Shares buffer with s1
void strDup(_Inout_ strhandle o, _In_opt_ strref s);

// Copies a string without using copy-on-write optimization
//
// Always creates a new independent copy of the string, unlike strDup which
// may create a reference. Use this when you need to ensure the output has
// its own buffer that can be modified without needing to copy-on-write.
//
// The output string is destroyed first if it already exists.
//
// Parameters:
//   o - Pointer to output string variable
//   s - String to copy (NULL creates empty string)
//
// Example:
//   string original = _S"shared";
//   string copy = 0;
//   strCopy(&copy, original);  // Independent copy
void strCopy(_Inout_ strhandle o, _In_opt_ strref s);

// Clears a string to empty while potentially preserving capacity
//
// Sets the string length to zero but may retain the allocated buffer for
// future use. This is more efficient than destroying and recreating the
// string if you plan to reuse it. The encoding flags are reset to UTF-8/ASCII.
//
// For strings that cannot be cleared in place (ropes, shared references, etc.),
// a new empty string is created instead.
//
// Parameters:
//   ps - Pointer to string to clear
//
// Example:
//   strClear(&s);  // Now empty but may keep buffer
void strClear(_Inout_ strhandle ps);

// Returns the length of a string in bytes
//
// Returns the number of bytes in the string content, not including the null
// terminator or any preallocated but unused capacity. NULL strings have length 0.
//
// This is a constant-time operation (O(1)) as the length is cached in the
// string header.
//
// Parameters:
//   s - String to query (NULL returns 0)
//
// Returns:
//   Length in bytes (not character count for multi-byte encodings)
//
// Example:
//   uint32 len = strLen(_S"hello");  // Returns 5
_When_(s == NULL, _Post_equal_to_(0)) _Pure uint32 strLen(_In_opt_ strref s);

// Sets the length of a string, growing or truncating as needed
//
// If growing the string, the new space is zero-filled. If truncating, the
// string is shortened and null-terminated at the new length.
//
// The string is flattened (if it's a rope) and made unique (if it has multiple
// references) before modification. If the buffer needs to grow, it is reallocated.
//
// Parameters:
//   ps - Pointer to string to modify
//   len - New length in bytes
//
// Example:
//   strSetLen(&s, 10);  // Set to exactly 10 bytes
void strSetLen(_Inout_ strhandle ps, uint32 len);

// Tests if a string is empty or NULL
//
// Returns true for NULL strings or strings with zero length. This is more
// efficient than comparing strLen() to zero for strings without cached length.
//
// Parameters:
//   s - String to test (NULL returns true)
//
// Returns:
//   true if string is empty or NULL, false otherwise
//
// Example:
//   if (strEmpty(s)) {
//       // Handle empty string case
//   }
_When_(s == NULL, _Post_equal_to_(true)) _Pure bool strEmpty(_In_opt_ strref s);

// Destroys a string and frees its resources
//
// Decrements the reference count atomically. If this was the last reference,
// the string buffer is freed. The string variable is set to NULL.
//
// Safe to call with NULL strings or strings not allocated by CX (which are
// simply discarded without freeing). Stack-allocated strings are not freed.
//
// Always call this when done with a string to prevent memory leaks.
//
// Parameters:
//   ps - Pointer to string to destroy (set to NULL after)
//
// Example:
//   string s = _S"hello";
//   strDestroy(&s);  // s is now NULL
void strDestroy(_Inout_ strhandle ps);

// Returns a read-only C-style string pointer
//
// Provides access to the string content as a null-terminated C string.
// For simple strings, returns a pointer to the internal buffer. For ropes,
// returns a scratch buffer with the flattened content.
//
// IMPORTANT: Scratch buffers are temporary and may be overwritten by other
// operations (see cx/utils/scratch.h). Use or copy the result immediately.
// For a persistent pointer, use strPC() instead.
//
// NULL input returns an empty string (not NULL).
//
// Parameters:
//   s - String to access (NULL returns "")
//
// Returns:
//   Null-terminated C string (may be temporary for ropes)
//
// Example:
//   printf("String: %s\n", strC(s));
_Ret_valid_ const char *_Nonnull strC(_In_opt_ strref s);

// Returns a persistent read-only C-style string pointer
//
// Similar to strC(), but guarantees the returned pointer remains valid as
// long as the string is not modified. This is the middle ground between
// strC() and strBuffer():
//
//   - Unlike strC(): Pointer is persistent (not a scratch buffer)
//   - Unlike strBuffer(): Buffer may be shared (read-only)
//                         Won't duplicate if already refcounted
//
// For ropes, this flattens the string into a contiguous buffer, modifying
// the handle to point to the flattened version.
//
// Parameters:
//   ps - Pointer to string variable (may be modified if rope)
//
// Returns:
//   Persistent null-terminated C string (valid until string is modified)
//
// Example:
//   const char *persistent = strPC(&s);
//   // Can safely use 'persistent' as long as s isn't modified
_Ret_valid_ const char* _Nonnull strPC(_Inout_ strhandle ps);

// Returns a writable pointer to the string's buffer
//
// Provides direct access to the string's internal buffer for modification.
// The string is automatically:
//   - Flattened if it's a rope
//   - Made unique if it has multiple references
//   - Created if NULL
//   - Grown and zero-padded if shorter than minsz
//
// After calling this function, encoding flags are cleared since the buffer
// may be modified in ways that invalidate them.
//
// The returned pointer is valid until the next string operation.
//
// Parameters:
//   ps - Pointer to string variable
//   minsz - Minimum buffer size in bytes (string is grown/padded if needed)
//
// Returns:
//   Writable buffer pointer (at least minsz bytes available)
//
// Example:
//   uint8 *buf = strBuffer(&s, 256);
//   memcpy(buf, data, 100);
//   strSetLen(&s, 100);  // Update length after direct write
_Ret_valid_ uint8 *_Nonnull strBuffer(_Inout_ strhandle ps, uint32 minsz);

// Copies string content to an external buffer with null termination
//
// Copies up to bufsz-1 bytes from the string starting at the specified offset,
// then adds a null terminator. The result is always a valid C string even if
// truncated.
//
// If the offset is beyond the string length, an empty string is returned.
// If NULL string or zero buffer size, no copy is performed.
//
// Parameters:
//   s - Source string (NULL returns 0)
//   off - Starting offset in source string
//   buf - Destination buffer
//   bufsz - Size of destination buffer (must be at least 1)
//
// Returns:
//   Number of bytes copied (excluding null terminator)
//
// Example:
//   char buf[32];
//   uint32 copied = strCopyOut(s, 0, (uint8*)buf, sizeof(buf));
uint32 strCopyOut(_In_opt_ strref s, uint32 off, _Out_writes_bytes_(bufsz) uint8 *_Nonnull buf, uint32 bufsz);

// Copies raw string bytes without null termination
//
// Copies up to maxlen bytes from the string starting at the specified offset.
// Unlike strCopyOut(), this does NOT add a null terminator, making it suitable
// for binary data or when concatenating into a larger buffer.
//
// If the offset is beyond the string length, nothing is copied.
//
// Parameters:
//   s - Source string (NULL returns 0)
//   off - Starting offset in source string
//   buf - Destination buffer
//   maxlen - Maximum number of bytes to copy
//
// Returns:
//   Number of bytes actually copied
//
// Example:
//   uint8 buf[256];
//   uint32 n = strCopyRaw(s, 10, buf, 100);  // Copy 100 bytes from offset 10
uint32 strCopyRaw(_In_opt_ strref s, uint32 off, _Out_writes_bytes_(maxlen) uint8 *_Nonnull buf, uint32 maxlen);

_Pure uint32 _strStackAllocSize(uint32 maxlen);
void _strInitStack(_Inout_ _Deref_pre_valid_ _Deref_post_opt_valid_ strhandle ps, uint32 maxlen);

// void strTemp(string *ps, uint32 maxlen);
//
// Creates a stack-allocated temporary string
//
// Allocates a string buffer on the stack for temporary use within the current
// scope. This is more efficient than heap allocation for short-lived strings.
//
// IMPORTANT RESTRICTIONS:
//   - Must NOT be returned from a function
//   - Must NOT be stored beyond the current scope
//   - MUST call strDestroy() before scope exit (even though stack-allocated)
//
// String functions won't promote stack strings to ropes. If an operation needs
// more space than maxlen, the handle may be replaced with a heap-allocated string,
// which is why strDestroy() is required.
//
// Maximum length is 65528 bytes. Larger sizes will fail.
//
// Parameters:
//   ps - Pointer to string variable (will point to stack buffer)
//   maxlen - Maximum string length in bytes (excluding null terminator)
//
// Example:
//   string temp;
//   strTemp(&temp, 256);    // Stack buffer for up to 256 bytes
//   // ... use temp ...
//   strDestroy(&temp);      // Required even for stack strings
#define strTemp(ps, maxlen) (*(ps)) = (string)stackAlloc(_strStackAllocSize(maxlen)); \
    _strInitStack(ps, maxlen);

#ifdef _WIN32
// definition of _S interferes with this header, so include it first
#include <wchar.h>
#include <mmintrin.h>
#endif

// String literal macros - create static strings from compile-time constants
//
// These macros convert C string literals into CX string objects by prepending
// a minimal header. The strings are static and don't need to be destroyed.
// They cannot be modified (attempts to modify will cause a copy).
//
// Choose the appropriate macro based on content:
//   _S  - Pure ASCII (most common, allows ASCII-specific optimizations)
//   _SU - UTF-8 encoded (multi-byte characters)
//   _SO - Other encoding or raw binary data
//
// Example:
//   string s1 = _S"Hello";           // ASCII literal
//   string s2 = _SU"Hello 世界";     // UTF-8 literal
//   strDup(&s1, _S"World");          // Can use in any string function

// string _S"literal";
//
// Creates a static ASCII string from a string literal
#define _S (string)"\xE0\xC1"

// string _SU"literal";
//
// Creates a static UTF-8 string from a string literal
#define _SU (string)"\xA0\xC1"

// string _SO"literal";
//
// Creates a static string with other/unknown encoding from a literal
#define _SO (string)"\x80\xC1"

CX_C_END
