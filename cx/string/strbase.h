#pragma once

#include <cx/cx.h>

CX_C_BEGIN

// IMPORTANT NOTE!
// Always initialize sstring to NULL or 0 first!
typedef struct str_impl* string;

// Create an empty string suitable for initializing a string variable.
// Do not assign to an existing string handle or you will leak resources!
string strCreate();

// Create a new empty string with preallocated storage.
//        o: output string, will be destroyed if it exists
// sizehint: Amount of memory to preallocate for string storage.
void strInit(string *o, uint32 sizehint);

// Duplicate an existing string, making an efficient reference if possible.
//       o: output string, will be destroyed if it exists
//       s: Handle of string to duplicate.
// Returns: String handle.
void strDup(string *o, string s);

// Like strDup but always makes a copy (does not use copy-on-write optimization).
//       o: output string, will be destroyed if it exists
//       s: Handle of string to copy.
void strCopy(string *o, string s);

// Clear the contents of a string. May leave existing storage as preallocated
// space if it is efficient to do so.
//      ps: pointer to string to clear
void strClear(string *ps);

// Retrieve the length of a string. This does not include preallocated memory.
//       s: String handle.
// Returns: String length.
uint32 strLen(string s);

// Sets the length of a string, truncating or filling with null-characters.
//      ps: Pointer to string handle.
//     len: New length
bool strSetLen(string *ps, uint32 len);

// Checks if a string is empty. Nonexistent strings are considered empty.
//       s: String handle
// Returns: true if string is empty
bool strEmpty(string s);

// Disposes of a string handle.
//      ps: Pointer to string handle. Handle will be reset to NULL.
void strDestroy(string *ps);

// Obtains a read-only pointer to a classic C-style string.
// Pointer is valid until the next string function is called.
//      ps: Pointer to string handle.
// Returns: C-style string.
const char *strC(string *ps);

// Obtains a read-write pointer to a string's backing memory buffer.
// This causes string memory to no longer be shared with duplicates.
// Pointer is valid until the next string function is called.
//      ps: Pointer to string handle.
//   minsz: If string is shorter than minsz, it will be zero-padded
//          up to this length. Useful for copying data into a string.
// Returns: Memory buffer.
char *strBuffer(string *ps, uint32 minsz);

// Copies up to bufsz bytes from the string to an external buffer.
// The resulting C-style string in buf will always be null terminated.
//       s: String handle.
//     off: Offset within string to begin copying.
//     buf: Pointer to memory buffer.
//   bufsz: Size of memory buffer.
// Returns: Number of bytes copied (may be smaller than requested if string length is exceeded).
uint32 strCopyOut(string s, uint32 off, char *buf, uint32 bufsz);

// Copies raw bytes out of a string without null terminating.
//       s: String handle.
//     off: Offset within string to begin copying.
//     buf: Pointer to memory buffer.
//  maxlen: Maximum number of bytes to copy.
// Returns: Number of bytes copied (may be smaller than requested if string length is exceeded).
uint32 strCopyRaw(string s, uint32 off, char *buf, uint32 maxlen);

#ifdef _WIN32
// definition of _S interferes with this header, so include it first
#include <wchar.h>
#endif

// Macros for creating string objects out of string literals by tacking on a basic header
// For pure ASCII strings
#define _S (string)"\xE0\xC1"
// For UTF-8 strings
#define _SU (string)"\xA0\xC1"
// For strings with other encodings or raw binary (no embedded NULLs)
#define _SO (string)"\x80\xC1"

CX_C_END
