/// @file cstrutil.h
/// @brief C string utility functions

#pragma once

#include <cx/platform/cpp.h>
#include <stddef.h>

CX_C_BEGIN

/// @defgroup cstrutil C String Utilities
/// @ingroup utils
/// @{
///
/// Portable C string helpers for common operations like length, duplication, and comparison.
/// These functions use xalloc for memory allocation and provide optimized implementations.

/// Efficiently compute the length of a null-terminated C string.
///
/// This uses an optimized word-aligned algorithm for better performance than
/// standard strlen on most architectures.
///
/// @param s Null-terminated string
/// @return Length of the string in bytes (excluding null terminator)
size_t cstrLen(_In_z_ const char *s);

/// Duplicate a null-terminated C string.
///
/// Allocates memory using xaAlloc and copies the string. The returned string
/// must be freed with xaFree() or xaRelease().
///
/// @param s Null-terminated string to duplicate, or NULL
/// @return Newly allocated copy of the string, or NULL if input was NULL
_Ret_z_ char *cstrDup(_In_z_ const char *s);

/// Compute the length of a null-terminated wide character string.
///
/// @param s Null-terminated wide string (unsigned short)
/// @return Length of the string in characters (excluding null terminator)
size_t cstrLenw(_In_z_ const unsigned short *s);

/// Duplicate a null-terminated wide character string.
///
/// Allocates memory using xaAlloc and copies the wide string. The returned string
/// must be freed with xaFree() or xaRelease().
///
/// @param s Null-terminated wide string to duplicate, or NULL
/// @return Newly allocated copy of the wide string, or NULL if input was NULL
_Ret_z_ unsigned short *cstrDupw(_In_z_ const unsigned short *s);

/// Case-insensitive string comparison.
///
/// Compares two strings ignoring case differences.
///
/// @param s1 First null-terminated string
/// @param s2 Second null-terminated string
/// @return 0 if strings are equal (ignoring case), negative if s1 < s2, positive if s1 > s2
int cstrCmpi(_In_z_ const char *s1, _In_z_ const char *s2);

/// @}
// end of cstrutil group

CX_C_END
