#pragma once

/// @file strbase.h
/// @brief Core string types and fundamental operations

#include <cx/cx.h>

CX_C_BEGIN

typedef struct str_ref {
    void* _is_string;
} str_ref;

/// @defgroup string_base Core String Functions
/// @ingroup string
/// @{

/// @brief Opaque handle to a string object
///
/// Strings use copy-on-write semantics with automatic reference counting.
/// Multiple string variables can efficiently share the same underlying buffer.
///
/// CRITICAL: Always initialize to NULL or 0 before first use:
/// @code
///   string s = 0;
/// @endcode
///
/// Strings must be destroyed with strDestroy() when no longer needed.
typedef struct str_ref* _Nullable string;

/// @brief Borrowed reference to a string
///
/// Used for passing strings as function arguments without transferring ownership.
/// The referenced string must remain valid for the duration of the function call.
///
/// IMPORTANT: Never return a strref from a function in multi-threaded programs.
/// This creates a race condition where the underlying string could be destroyed
/// by another thread. Always use an output parameter with strDup() instead.
typedef const struct str_ref* _Nullable strref;

/// @brief Pointer to a string variable
///
/// Used as an output parameter for functions that modify or create strings.
/// Must always point to a valid string variable (which may be NULL).
typedef string* _Nonnull strhandle;

/// void strInit(string *o);
///
/// Initializes a string variable to empty/NULL
///
/// This is a convenience macro equivalent to setting the string to NULL.
/// While not strictly required (direct assignment works), it provides clarity.
///
/// @param o Pointer to string variable to initialize
///
/// Example:
/// @code
///   string s;
///   strInit(&s);
/// @endcode
#define strInit(o) *(o) = NULL

/// Creates a new empty string with preallocated storage
///
/// If the output string already exists, it is destroyed first. The new string
/// is allocated with enough capacity for the specified size plus overhead.
/// This is useful when you know approximately how large the string will grow.
///
/// The string is initialized with UTF-8 and ASCII encoding flags set.
///
/// @param o Pointer to string variable (existing string will be destroyed)
/// @param sizehint Expected string size in bytes (0 for default minimum)
///
/// Example:
/// @code
///   string s = 0;
///   strReset(&s, 256);  // Preallocate for ~256 byte string
/// @endcode
void strReset(_Inout_ptr_opt_ strhandle o, uint32 sizehint);

/// Duplicates a string using copy-on-write optimization
///
/// Creates a reference to the source string when possible, avoiding copying.
/// The reference count is incremented atomically for thread safety. If the
/// source is not a CX-managed string, a plain C string, or a stack-allocated
/// string, a copy is made instead.
///
/// Special case: If the output is a stack-allocated string, the content is
/// copied into the stack buffer rather than replacing the handle.
///
/// The output string is destroyed first if it already exists.
///
/// @param o Pointer to output string variable
/// @param s String to duplicate (NULL creates empty string)
///
/// Example:
/// @code
///   string s1 = 0;
///   strDup(&s1, _S"hello");  // Efficient reference
///   string s2 = 0;
///   strDup(&s2, s1);         // Shares buffer with s1
/// @endcode
void strDup(_Inout_ strhandle o, _In_opt_ strref s);

/// Copies a string without using copy-on-write optimization
///
/// Always creates a new independent copy of the string, unlike strDup which
/// may create a reference. Use this when you need to ensure the output has
/// its own buffer that can be modified without needing to copy-on-write.
///
/// The output string is destroyed first if it already exists.
///
/// @param o Pointer to output string variable
/// @param s String to copy (NULL creates empty string)
///
/// Example:
/// @code
///   string original = _S"shared";
///   string copy = 0;
///   strCopy(&copy, original);  // Independent copy
/// @endcode
void strCopy(_Inout_ strhandle o, _In_opt_ strref s);

/// Clears a string to empty while potentially preserving capacity
///
/// Sets the string length to zero but may retain the allocated buffer for
/// future use. This is more efficient than destroying and recreating the
/// string if you plan to reuse it. The encoding flags are reset to UTF-8/ASCII.
///
/// For strings that cannot be cleared in place (ropes, shared references, etc.),
/// a new empty string is created instead.
///
/// @param ps Pointer to string to clear
///
/// Example:
/// @code
///   strClear(&s);  // Now empty but may keep buffer
/// @endcode
void strClear(_Inout_ strhandle ps);

/// Returns the length of a string in bytes
///
/// Returns the number of bytes in the string content, not including the null
/// terminator or any preallocated but unused capacity. NULL strings have length 0.
///
/// This is a constant-time operation (O(1)) as the length is cached in the
/// string header.
///
/// @param s String to query (NULL returns 0)
/// @return Length in bytes (not character count for multi-byte encodings)
///
/// Example:
/// @code
///   uint32 len = strLen(_S"hello");  // Returns 5
/// @endcode
_When_(s == NULL, _Post_equal_to_(0)) _Pure uint32 strLen(_In_opt_ strref s);

/// Sets the length of a string, growing or truncating as needed
///
/// If growing the string, the new space is zero-filled. If truncating, the
/// string is shortened and null-terminated at the new length.
///
/// The string is flattened (if it's a rope) and made unique (if it has multiple
/// references) before modification. If the buffer needs to grow, it is reallocated.
///
/// @param ps Pointer to string to modify
/// @param len New length in bytes
///
/// Example:
/// @code
///   strSetLen(&s, 10);  // Set to exactly 10 bytes
/// @endcode
void strSetLen(_Inout_ strhandle ps, uint32 len);

/// Tests if a string is empty or NULL
///
/// Returns true for NULL strings or strings with zero length. This is more
/// efficient than comparing strLen() to zero for strings without cached length.
///
/// @param s String to test (NULL returns true)
/// @return true if string is empty or NULL, false otherwise
///
/// Example:
/// @code
///   if (strEmpty(s)) {
///       // Handle empty string case
///   }
/// @endcode
_When_(s == NULL, _Post_equal_to_(true)) _Pure bool strEmpty(_In_opt_ strref s);

/// Destroys a string and frees its resources
///
/// Decrements the reference count atomically. If this was the last reference,
/// the string buffer is freed. The string variable is set to NULL.
///
/// Safe to call with NULL strings or strings not allocated by CX (which are
/// simply discarded without freeing). Stack-allocated strings are not freed.
///
/// Always call this when done with a string to prevent memory leaks.
///
/// @param ps Pointer to string to destroy (set to NULL after)
///
/// Example:
/// @code
///   string s = _S"hello";
///   strDestroy(&s);  // s is now NULL
/// @endcode
void strDestroy(_Inout_ strhandle ps);

/// Returns a read-only C-style string pointer
///
/// Provides access to the string content as a null-terminated C string.
/// For simple strings, returns a pointer to the internal buffer. For ropes,
/// returns a scratch buffer with the flattened content.
///
/// IMPORTANT: Scratch buffers are temporary and may be overwritten by other
/// operations (see cx/utils/scratch.h). Use or copy the result immediately.
/// For a persistent pointer, use strPC() instead.
///
/// NULL input returns an empty string (not NULL).
///
/// @param s String to access (NULL returns "")
/// @return Null-terminated C string (may be temporary for ropes)
///
/// Example:
/// @code
///   printf("String: %s\n", strC(s));
/// @endcode
_Ret_valid_ const char* _Nonnull strC(_In_opt_ strref s);

/// Returns a persistent read-only C-style string pointer
///
/// Similar to strC(), but guarantees the returned pointer remains valid as
/// long as the string is not modified. This is the middle ground between
/// strC() and strBuffer():
///
///   - Unlike strC(): Pointer is persistent (not a scratch buffer)
///   - Unlike strBuffer(): Buffer may be shared (read-only)
///                         Won't duplicate if already refcounted
///
/// For ropes, this flattens the string into a contiguous buffer, modifying
/// the handle to point to the flattened version.
///
/// @param ps Pointer to string variable (may be modified if rope)
/// @return Persistent null-terminated C string (valid until string is modified)
///
/// Example:
/// @code
///   const char *persistent = strPC(&s);
///   // Can safely use 'persistent' as long as s isn't modified
/// @endcode
_Ret_valid_ const char* _Nonnull strPC(_Inout_ strhandle ps);

/// Returns a writable pointer to the string's buffer
///
/// Provides direct access to the string's internal buffer for modification.
/// The string is automatically:
///   - Flattened if it's a rope
///   - Made unique if it has multiple references
///   - Created if NULL
///   - Grown and zero-padded if shorter than minsz
///
/// After calling this function, encoding flags are cleared since the buffer
/// may be modified in ways that invalidate them.
///
/// The returned pointer is valid until the next string operation.
///
/// @param ps Pointer to string variable
/// @param minsz Minimum buffer size in bytes (string is grown/padded if needed)
/// @return Writable buffer pointer (at least minsz bytes available)
///
/// Example:
/// @code
///   uint8 *buf = strBuffer(&s, 256);
///   memcpy(buf, data, 100);
///   strSetLen(&s, 100);  // Update length after direct write
/// @endcode
_Ret_valid_ uint8* _Nonnull strBuffer(_Inout_ strhandle ps, uint32 minsz);

/// Copies string content to an external buffer with null termination
///
/// Copies up to bufsz-1 bytes from the string starting at the specified offset,
/// then adds a null terminator. The result is always a valid C string even if
/// truncated.
///
/// If the offset is beyond the string length, an empty string is returned.
/// If NULL string or zero buffer size, no copy is performed.
///
/// @param s Source string (NULL returns 0)
/// @param off Starting offset in source string
/// @param buf Destination buffer
/// @param bufsz Size of destination buffer (must be at least 1)
/// @return Number of bytes copied (excluding null terminator)
///
/// Example:
/// @code
///   char buf[32];
///   uint32 copied = strCopyOut(s, 0, (uint8*)buf, sizeof(buf));
/// @endcode
uint32 strCopyOut(_In_opt_ strref s, uint32 off, _Out_writes_bytes_(bufsz) uint8* _Nonnull buf,
                  uint32 bufsz);

/// Copies raw string bytes without null termination
///
/// Copies up to maxlen bytes from the string starting at the specified offset.
/// Unlike strCopyOut(), this does NOT add a null terminator, making it suitable
/// for binary data or when concatenating into a larger buffer.
///
/// If the offset is beyond the string length, nothing is copied.
///
/// @param s Source string (NULL returns 0)
/// @param off Starting offset in source string
/// @param buf Destination buffer
/// @param maxlen Maximum number of bytes to copy
/// @return Number of bytes actually copied
///
/// Example:
/// @code
///   uint8 buf[256];
///   uint32 n = strCopyRaw(s, 10, buf, 100);  // Copy 100 bytes from offset 10
/// @endcode
uint32 strCopyRaw(_In_opt_ strref s, uint32 off, _Out_writes_bytes_(maxlen) uint8* _Nonnull buf,
                  uint32 maxlen);

_Pure uint32 _strStackAllocSize(uint32 maxlen);
void _strInitStack(_Inout_ _Deref_pre_valid_ _Deref_post_opt_valid_ strhandle ps, uint32 maxlen);

/// void strTemp(string *ps, uint32 maxlen);
///
/// Creates a stack-allocated temporary string
///
/// Allocates a string buffer on the stack for temporary use within the current
/// scope. This is more efficient than heap allocation for short-lived strings.
///
/// IMPORTANT RESTRICTIONS:
///   - Must NOT be returned from a function
///   - Must NOT be stored beyond the current scope
///   - MUST call strDestroy() before scope exit (even though stack-allocated)
///
/// String functions won't promote stack strings to ropes. If an operation needs
/// more space than maxlen, the handle may be replaced with a heap-allocated string,
/// which is why strDestroy() is required.
///
/// Maximum length is 65528 bytes. Larger sizes will fail.
///
/// @param ps Pointer to string variable (will point to stack buffer)
/// @param maxlen Maximum string length in bytes (excluding null terminator)
///
/// Example:
/// @code
///   string temp;
///   strTemp(&temp, 256);    // Stack buffer for up to 256 bytes
///   // ... use temp ...
///   strDestroy(&temp);      // Required even for stack strings
/// @endcode
#define strTemp(ps, maxlen)                                   \
    (*(ps)) = (string)stackAlloc(_strStackAllocSize(maxlen)); \
    _strInitStack(ps, maxlen);

/// @}  // end of core string functions group

#ifdef _WIN32
// definition of _S interferes with this header, so include it first
#include <mmintrin.h>
#include <wchar.h>
#endif

/// @defgroup string_literals String Literal Macros
/// @ingroup string
/// @brief Create static strings from compile-time constants
///
/// These macros convert C string literals into CX string objects by prepending
/// a minimal header. The strings are static and don't need to be destroyed.
/// They cannot be modified (attempts to modify will cause a copy).
///
/// Choose the appropriate macro based on content:
///   - _S  - Pure ASCII (most common, allows ASCII-specific optimizations)
///   - _SU - UTF-8 encoded (multi-byte characters)
///   - _SO - Other encoding or raw binary data
///
/// Example:
/// @code
///   string s1 = _S"Hello";           // ASCII literal
///   string s2 = _SU"Hello 世界";     // UTF-8 literal
///   strDup(&s1, _S"World");          // Can use in any string function
/// @endcode
/// @{

/// @def _S
/// @brief Creates a static ASCII string from a string literal
/// @hideinitializer
#define _S (string)"\xE0\xC1"

/// @def _SU
/// @brief Creates a static UTF-8 string from a string literal
/// @hideinitializer
#define _SU (string)"\xA0\xC1"

/// @def _SO
/// @brief Creates a static string with other/unknown encoding from a literal
/// @hideinitializer
#define _SO (string)"\x80\xC1"

/// @}  // end of string_literals group

CX_C_END
