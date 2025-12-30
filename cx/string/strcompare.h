#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

// Tests if two strings are equal (case-sensitive)
//
// This is slightly faster than strCmp() when you only need to check equality
// rather than ordering. Performs an early-out optimization by comparing lengths
// first.
//
// Parameters:
//   s1 - First string to compare
//   s2 - Second string to compare
//
// Returns:
//   true if the strings are equal, false otherwise
//
// Example:
//   if (strEq(s1, _S"hello")) {
//       // Strings match exactly
//   }
_Pure bool strEq(_In_opt_ strref s1, _In_opt_ strref s2);

// Tests if two strings are equal (case-insensitive)
//
// Like strEq(), but performs case-insensitive comparison using tolower().
// This is an ASCII-only comparison - multi-byte UTF-8 characters are not
// properly case-folded.
//
// Parameters:
//   s1 - First string to compare
//   s2 - Second string to compare
//
// Returns:
//   true if the strings are equal ignoring case, false otherwise
//
// Example:
//   if (strEqi(_S"Hello", _S"HELLO")) {
//       // Returns true
//   }
_Pure bool strEqi(_In_opt_ strref s1, _In_opt_ strref s2);

// Compares two strings lexicographically (case-sensitive)
//
// Performs a binary comparison of string bytes. This is NOT intended for
// locale-aware lexical sorting - use a proper collation function for that.
//
// Parameters:
//   s1 - First string to compare
//   s2 - Second string to compare
//
// Returns:
//   < 0 if s1 comes before s2
//     0 if the strings are equal
//   > 0 if s1 comes after s2
//
// Example:
//   int32 result = strCmp(_S"apple", _S"banana");
//   if (result < 0) {
//       // "apple" comes before "banana"
//   }
_Pure int32 strCmp(_In_opt_ strref s1, _In_opt_ strref s2);

// Compares two strings lexicographically (case-insensitive)
//
// Like strCmp(), but performs case-insensitive comparison using tolower().
// This is an ASCII-only comparison - multi-byte UTF-8 characters are not
// properly case-folded.
//
// Parameters:
//   s1 - First string to compare
//   s2 - Second string to compare
//
// Returns:
//   < 0 if s1 comes before s2 (ignoring case)
//     0 if the strings are equal (ignoring case)
//   > 0 if s1 comes after s2 (ignoring case)
//
// Example:
//   int32 result = strCmpi(_S"Apple", _S"BANANA");
_Pure int32 strCmpi(_In_opt_ strref s1, _In_opt_ strref s2);

// Tests if a substring range equals another string (case-sensitive)
//
// Compares up to len bytes of str starting at offset off with sub.
// Negative offsets are relative to the end of str. If the offset is out of
// bounds or the ranges don't match in length (after clamping), returns false.
//
// Parameters:
//   str - String containing the range to compare
//   sub - String to compare against
//   off - Starting offset in str (negative = from end)
//   len - Maximum number of bytes to compare
//
// Returns:
//   true if the ranges are equal, false otherwise
//
// Example:
//   // Check if characters 2-6 of str equal "hello"
//   if (strRangeEq(str, _S"hello", 2, 5)) {
//       // Match found
//   }
_Pure bool strRangeEq(_In_opt_ strref str, _In_opt_ strref sub, int32 off, uint32 len);

// Tests if a substring range equals another string (case-insensitive)
//
// Like strRangeEq(), but performs case-insensitive comparison.
// This is an ASCII-only comparison.
//
// Parameters:
//   str - String containing the range to compare
//   sub - String to compare against
//   off - Starting offset in str (negative = from end)
//   len - Maximum number of bytes to compare
//
// Returns:
//   true if the ranges are equal ignoring case, false otherwise
_Pure bool strRangeEqi(_In_opt_ strref str, _In_opt_ strref sub, int32 off, uint32 len);

// Compares a substring range with another string (case-sensitive)
//
// Compares up to len bytes of str starting at offset off with sub.
// Negative offsets are relative to the end of str.
//
// Parameters:
//   str - String containing the range to compare
//   sub - String to compare against
//   off - Starting offset in str (negative = from end)
//   len - Maximum number of bytes to compare
//
// Returns:
//   < 0 if the range comes before sub
//     0 if the ranges are equal
//   > 0 if the range comes after sub
//
// Example:
//   // Compare 5 bytes starting at position 10
//   int32 result = strRangeCmp(str, _S"test", 10, 5);
_Pure int32 strRangeCmp(_In_opt_ strref str, _In_opt_ strref sub, int32 off, uint32 len);

// Compares a substring range with another string (case-insensitive)
//
// Like strRangeCmp(), but performs case-insensitive comparison.
// This is an ASCII-only comparison.
//
// Parameters:
//   str - String containing the range to compare
//   sub - String to compare against
//   off - Starting offset in str (negative = from end)
//   len - Maximum number of bytes to compare
//
// Returns:
//   < 0 if the range comes before sub (ignoring case)
//     0 if the ranges are equal (ignoring case)
//   > 0 if the range comes after sub (ignoring case)
_Pure int32 strRangeCmpi(_In_opt_ strref str, _In_opt_ strref sub, int32 off, uint32 len);

// Tests if a string begins with a specific prefix (case-sensitive)
//
// Checks if the first bytes of str match sub. This is equivalent to
// strRangeEq(str, sub, 0, strLen(sub)).
//
// Parameters:
//   str - String to check
//   sub - Prefix to test for
//
// Returns:
//   true if str starts with sub, false otherwise
//
// Example:
//   if (strBeginsWith(path, _S"/home/")) {
//       // Path starts with /home/
//   }
_Pure bool strBeginsWith(_In_opt_ strref str, _In_opt_ strref sub);

// Tests if a string begins with a specific prefix (case-insensitive)
//
// Like strBeginsWith(), but performs case-insensitive comparison.
// This is an ASCII-only comparison.
//
// Parameters:
//   str - String to check
//   sub - Prefix to test for
//
// Returns:
//   true if str starts with sub (ignoring case), false otherwise
//
// Example:
//   if (strBeginsWithi(str, _S"http")) {
//       // Matches "http", "HTTP", "Http", etc.
//   }
_Pure bool strBeginsWithi(_In_opt_ strref str, _In_opt_ strref sub);

// Tests if a string ends with a specific suffix (case-sensitive)
//
// Checks if the last bytes of str match sub. This is equivalent to
// strRangeEq(str, sub, -strLen(sub), strLen(sub)).
//
// Parameters:
//   str - String to check
//   sub - Suffix to test for
//
// Returns:
//   true if str ends with sub, false otherwise
//
// Example:
//   if (strEndsWith(filename, _S".txt")) {
//       // File has .txt extension
//   }
_Pure bool strEndsWith(_In_opt_ strref str, _In_opt_ strref sub);

// Tests if a string ends with a specific suffix (case-insensitive)
//
// Like strEndsWith(), but performs case-insensitive comparison.
// This is an ASCII-only comparison.
//
// Parameters:
//   str - String to check
//   sub - Suffix to test for
//
// Returns:
//   true if str ends with sub (ignoring case), false otherwise
//
// Example:
//   if (strEndsWithi(filename, _S".txt")) {
//       // Matches ".txt", ".TXT", ".Txt", etc.
//   }
_Pure bool strEndsWithi(_In_opt_ strref str, _In_opt_ strref sub);

CX_C_END
