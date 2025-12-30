#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

// Finds the first occurrence of a substring (forward search)
//
// Searches for the first occurrence of the substring 'find' in string 's',
// starting at the specified position. The search proceeds forward toward the
// end of the string.
//
// Negative start positions are relative to the end of the string, allowing
// searches from a position near the end. If the substring is not found, -1
// is returned.
//
// Parameters:
//   s - String to search within
//   start - Starting position for search (negative = from end)
//   find - Substring to search for
//
// Returns:
//   Byte offset of first occurrence, or -1 if not found
//
// Example:
//   int32 pos = strFind(s, 0, _S"hello");
//   if (pos >= 0) {
//       // Found at position pos
//   }
//
//   // Search starting from position 10
//   pos = strFind(s, 10, _S"world");
//
//   // Search from 5 characters before the end
//   pos = strFind(s, -5, _S"end");
int32 strFind(_In_opt_ strref s, int32 start, _In_opt_ strref find);

// Finds the last occurrence of a substring (reverse search)
//
// Searches for the last occurrence of the substring 'find' in string 's',
// searching backward from the specified end position. This is useful for
// finding the rightmost match or searching within a specific range.
//
// The end position can be:
//   - 0 or strEnd: Search from the end of the string
//   - Positive: Search up to this byte offset
//   - Negative: Offset from the end of the string
//
// If the substring is not found, -1 is returned.
//
// Parameters:
//   s - String to search within
//   end - Ending position for search (0/strEnd = string end, negative = from end)
//   find - Substring to search for
//
// Returns:
//   Byte offset of last occurrence before end, or -1 if not found
//
// Example:
//   // Find last occurrence in entire string
//   int32 pos = strFindR(s, 0, _S".");
//   if (pos >= 0) {
//       // Found last period at position pos
//   }
//
//   // Find last occurrence before position 50
//   pos = strFindR(s, 50, _S"item");
//
//   // Find last occurrence in last 20 characters
//   pos = strFindR(s, -20, _S"suffix");
int32 strFindR(_In_opt_ strref s, int32 end, _In_opt_ strref find);

CX_C_END
