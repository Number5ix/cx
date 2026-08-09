#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

/// @defgroup string_find Searching
/// @ingroup string
/// @{
///
/// Functions for finding substrings within strings.
///
/// Case-insensitive searches are ASCII-only and do not properly handle
/// multi-byte UTF-8 characters.

/// Finds the first occurrence of a substring (forward search)
///
/// Searches for the first occurrence of the substring 'find' in string 's',
/// starting at the specified position. The search proceeds forward toward the
/// end of the string.
///
/// Negative start positions are relative to the end of the string, allowing
/// searches from a position near the end. If the substring is not found, -1
/// is returned.
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param find Substring to search for
/// @return Byte offset of first occurrence, or -1 if not found
///
/// Example:
/// @code
///   int32 pos = strFind(s, 0, _SL("hello"));
///   if (pos >= 0) {
///       // Found at position pos
///   }
///
///   // Search starting from position 10
///   pos = strFind(s, 10, _SL("world"));
///
///   // Search from 5 characters before the end
///   pos = strFind(s, -5, _SL("end"));
/// @endcode
int32 strFind(_In_opt_ strref s, int32 start, _In_opt_ strref find);

/// Finds the first occurrence of a substring, ignoring case (forward search)
///
/// Case-insensitive version of strFind().
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param find Substring to search for
/// @return Byte offset of first occurrence, or -1 if not found
///
/// Example:
/// @code
///   // Matches "utf-8", "UTF-8", "UTF-8" anywhere in the value
///   if (strFindi((strref)getenv("LANG"), 0, _SL("utf-8")) >= 0) {
///       // Locale is UTF-8
///   }
/// @endcode
int32 strFindi(_In_opt_ strref s, int32 start, _In_opt_ strref find);

/// Finds the last occurrence of a substring (reverse search)
///
/// Searches for the last occurrence of the substring 'find' in string 's',
/// searching backward from the specified end position. This is useful for
/// finding the rightmost match or searching within a specific range.
///
/// The end position can be:
/// - strEnd: Search from the end of the string
/// - Positive: Search up to this byte offset
/// - Negative: Offset from the end of the string
///
/// If the substring is not found, -1 is returned.
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param find Substring to search for
/// @return Byte offset of last occurrence before end, or -1 if not found
///
/// Example:
/// @code
///   // Find last occurrence in entire string
///   int32 pos = strFindR(s, strEnd, _SL("."));
///   if (pos >= 0) {
///       // Found last period at position pos
///   }
///
///   // Find last occurrence before position 50
///   pos = strFindR(s, 50, _SL("item"));
///
///   // Find last occurrence in last 20 characters
///   pos = strFindR(s, -20, _SL("suffix"));
/// @endcode
int32 strFindR(_In_opt_ strref s, int32 end, _In_opt_ strref find);

/// Finds the last occurrence of a substring, ignoring case (reverse search)
///
/// Case-insensitive version of strFindR().
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param find Substring to search for
/// @return Byte offset of last occurrence before end, or -1 if not found
///
/// Example:
/// @code
///   // Find the last extension separator regardless of case
///   int32 pos = strFindRi(filename, strEnd, _SL(".TAR"));
/// @endcode
int32 strFindRi(_In_opt_ strref s, int32 end, _In_opt_ strref find);

/// Finds the first occurrence of a byte (forward search)
///
/// Searches for the first occurrence of the byte 'find' in string 's', starting at the
/// specified position.
///
/// @note This operates on bytes, not UTF-8 code points. Searching for a byte >= 0x80
/// can match a continuation byte in the middle of a multi-byte sequence.
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param find Byte to search for
/// @return Byte offset of first occurrence, or -1 if not found
///
/// Example:
/// @code
///   int32 pos = strFindChar(path, 0, '/');
/// @endcode
int32 strFindChar(_In_opt_ strref s, int32 start, char find);

/// Finds the first occurrence of a byte, ignoring case (forward search)
///
/// Case-insensitive version of strFindChar().
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param find Byte to search for
/// @return Byte offset of first occurrence, or -1 if not found
///
/// Example:
/// @code
///   int32 pos = strFindChari(s, 0, 'q');   // matches 'q' or 'Q'
/// @endcode
int32 strFindChari(_In_opt_ strref s, int32 start, char find);

/// Finds the last occurrence of a byte (reverse search)
///
/// Searches backward from the specified end position for the byte 'find'. The end
/// position can be strEnd (search the whole string), a positive byte offset, or a
/// negative offset from the end.
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param find Byte to search for
/// @return Byte offset of last occurrence before end, or -1 if not found
///
/// Example:
/// @code
///   int32 pos = strFindCharR(filename, strEnd, '.');
/// @endcode
int32 strFindCharR(_In_opt_ strref s, int32 end, char find);

/// Finds the last occurrence of a byte, ignoring case (reverse search)
///
/// Case-insensitive version of strFindCharR().
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param find Byte to search for
/// @return Byte offset of last occurrence before end, or -1 if not found
///
/// Example:
/// @code
///   int32 pos = strFindCharRi(s, strEnd, 'x');   // matches 'x' or 'X'
/// @endcode
int32 strFindCharRi(_In_opt_ strref s, int32 end, char find);

/// Finds the first byte that is a member of a set (forward search)
///
/// Searches for the first byte in 's' that appears anywhere in 'chars', starting at
/// the specified position. This is the string-scanning equivalent of C's strpbrk().
///
/// An empty or NULL character set never matches, so -1 is returned.
///
///@note This operates on bytes, not UTF-8 code points. A multi-byte character in the
/// set is treated as its individual bytes, any one of which may match.
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param chars Set of bytes to search for
/// @return Byte offset of first matching byte, or -1 if none found
///
/// Example:
/// @code
///   // find the first whitespace byte
///   int32 pos = strFindAny(line, 0, _SL(" \t\r\n"));
/// @endcode
int32 strFindAny(_In_opt_ strref s, int32 start, _In_opt_ strref chars);

/// Finds the first byte that is a member of a set, ignoring case (forward search)
///
/// Case-insensitive version of strFindAny().
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param chars Set of bytes to search for
/// @return Byte offset of first matching byte, or -1 if none found
///
/// Example:
/// @code
///   int32 pos = strFindAnyi(s, 0, _SL("aeiou"));   // also matches AEIOU
/// @endcode
int32 strFindAnyi(_In_opt_ strref s, int32 start, _In_opt_ strref chars);

/// Finds the last byte that is a member of a set (reverse search)
///
/// Searches backward from the specified end position for a byte that appears anywhere
/// in 'chars'. The end position follows the same rules as strFindR().
///
/// An empty or NULL character set never matches, so -1 is returned.
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param chars Set of bytes to search for
/// @return Byte offset of last matching byte before end, or -1 if none found
///
/// Example:
/// @code
///   // last path separator, either flavor
///   int32 pos = strFindAnyR(path, strEnd, _SL("/\\"));
/// @endcode
int32 strFindAnyR(_In_opt_ strref s, int32 end, _In_opt_ strref chars);

/// Finds the last byte that is a member of a set, ignoring case (reverse search)
///
/// Case-insensitive version of strFindAnyR().
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param chars Set of bytes to search for
/// @return Byte offset of last matching byte before end, or -1 if none found
///
/// Example:
/// @code
///   int32 pos = strFindAnyRi(s, strEnd, _SL("xyz"));
/// @endcode
int32 strFindAnyRi(_In_opt_ strref s, int32 end, _In_opt_ strref chars);

/// Finds the first byte that is NOT a member of a set (forward search)
///
/// Searches for the first byte in 's' that does not appear in 'chars', starting at the
/// specified position. This is the string-scanning equivalent of C's strspn(), and is
/// the primitive the trim functions are built on.
///
/// An empty or NULL character set excludes nothing, so the search position itself is
/// returned (or -1 if it is already at the end of the string).
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param chars Set of bytes to skip over
/// @return Byte offset of first non-matching byte, or -1 if all bytes are in the set
///
/// Example:
/// @code
///   // offset of the first byte that isn't leading whitespace
///   int32 pos = strFindNotAny(line, 0, _SL(" \t"));
/// @endcode
int32 strFindNotAny(_In_opt_ strref s, int32 start, _In_opt_ strref chars);

/// Finds the first byte that is NOT a member of a set, ignoring case (forward search)
///
/// Like strFindNotAny(), but a set entry excludes both cases of that letter.
///
/// @param s String to search within
/// @param start Starting position for search (negative = from end)
/// @param chars Set of bytes to skip over
/// @return Byte offset of first non-matching byte, or -1 if all bytes are in the set
///
/// Example:
/// @code
///   int32 pos = strFindNotAnyi(s, 0, _SL("abc"));   // also skips A, B, C
/// @endcode
int32 strFindNotAnyi(_In_opt_ strref s, int32 start, _In_opt_ strref chars);

/// Finds the last byte that is NOT a member of a set (reverse search)
///
/// Searches backward from the specified end position for a byte that does not appear
/// in 'chars'. The end position follows the same rules as strFindR().
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param chars Set of bytes to skip over
/// @return Byte offset of last non-matching byte before end, or -1 if all are in the set
///
/// Example:
/// @code
///   // offset of the last byte that isn't trailing whitespace
///   int32 pos = strFindNotAnyR(line, strEnd, _SL(" \t\r\n"));
/// @endcode
int32 strFindNotAnyR(_In_opt_ strref s, int32 end, _In_opt_ strref chars);

/// Finds the last byte that is NOT a member of a set, ignoring case (reverse search)
///
/// Like strFindNotAnyR(), but a set entry excludes both cases of that letter.
///
/// @param s String to search within
/// @param end Ending position for search (strEnd = string end, negative = from end)
/// @param chars Set of bytes to skip over
/// @return Byte offset of last non-matching byte before end, or -1 if all are in the set
///
/// Example:
/// @code
///   int32 pos = strFindNotAnyRi(s, strEnd, _SL("xyz"));
/// @endcode
int32 strFindNotAnyRi(_In_opt_ strref s, int32 end, _In_opt_ strref chars);

/// @}  // end of string_find group

CX_C_END
