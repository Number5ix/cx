#pragma once

#include <cx/container/sarray.h>
#include <cx/string/strbase.h>

CX_C_BEGIN

/// Special constant for string manipulation functions
enum STRING_SPECIAL {
    strEnd = 0x7fffffff   ///< Represents the end of a string (use in place of length)
};

/// @defgroup string_manip Manipulation
/// @ingroup string
/// @{
///
/// String manipulation operations for modifying, combining, and extracting portions
/// of strings. Many operations have multiple variants optimized for different use cases.
///
/// @section string_manip_naming Naming convention for function parameters
///
/// - `o` - Output string; existing content is destroyed and replaced
/// - `io` - Input+output string; modified in-place when possible
/// - `s*` - Input string; read-only, not modified
/// - `sc*` - Input string that is consumed; destroyed/reused efficiently, handle set to NULL
///
/// @section string_manip_consuming Consuming variants (functions with 'C' suffix)
///
/// Functions ending in 'C' (like strConcatC, strSubStrC) take ownership of their
/// input strings and destroy them after use. This allows for more efficient memory
/// reuse when you no longer need the source strings:
/// @code
///   string s1 = _S"hello";
///   string s2 = _S" world";
///   string result = 0;
///   strConcatC(&result, &s1, &s2);  // s1 and s2 are now NULL
/// @endcode
///
/// @section string_manip_inplace In-place variants (functions with 'I' suffix)
///
/// Functions ending in 'I' modify the string in-place, efficiently reusing the
/// existing buffer when possible:
/// @code
///   string s = _S"hello world";
///   strSubStrI(&s, 0, 5);  // s is now "hello"
/// @endcode
///
/// @section string_manip_negative Negative indices
///
/// Most functions accept negative indices to count from the end of the string:
/// -1 refers to the last byte, -2 to second-to-last, etc.
///
/// @section string_manip_rope Rope optimization
///
/// For large string operations, the library may use rope data structures internally
/// to avoid copying. This is transparent to the caller but affects performance
/// characteristics - very large concatenations and substrings are much faster.

/// Appends a string to another string in-place
///
/// Adds the content of string s to the end of string io. The operation is performed
/// in-place when possible for efficiency. For large strings, may create a rope
/// structure instead of copying.
///
/// If io is NULL or empty, this is equivalent to strDup().
///
/// @param io String to append to (modified in-place)
/// @param s String to append (not modified)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _S"Hello");
///   strAppend(&s, _S" World");  // s is now "Hello World"
///   strDestroy(&s);
/// @endcode
bool strAppend(_Inout_ strhandle io, _In_opt_ strref s);

/// Prepends a string to another string in-place
///
/// Adds the content of string s to the beginning of string io. This is less efficient
/// than strAppend() because the entire string must be reconstructed.
///
/// @param s String to prepend (not modified)
/// @param io String to prepend to (modified in-place)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _S"World");
///   strPrepend(_S"Hello ", &s);  // s is now "Hello World"
///   strDestroy(&s);
/// @endcode
bool strPrepend(_In_opt_ strref s, _Inout_ strhandle io);

/// Concatenates two strings into an output string
///
/// Combines s1 and s2 into a new string stored in o. Any existing content in o is
/// destroyed. For large strings, may create a rope structure for efficiency.
///
/// If o points to the same string as s1, this is optimized to behave like strAppend().
///
/// @param o Output string (existing content destroyed)
/// @param s1 First string (not modified)
/// @param s2 Second string (not modified)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string result = 0;
///   strConcat(&result, _S"Hello", _S" World");
///   // result is "Hello World"
///   strDestroy(&result);
/// @endcode
bool strConcat(_Inout_ strhandle o, _In_opt_ strref s1, _In_opt_ strref s2);

/// Concatenates two strings, consuming the inputs
///
/// Like strConcat(), but takes ownership of sc1 and sc2, destroying them after use.
/// This allows for more efficient memory reuse when the source strings are no longer
/// needed. Both sc1 and sc2 will be NULL after this call.
///
/// @param o Output string (existing content destroyed)
/// @param sc1 First string (destroyed after use)
/// @param sc2 Second string (destroyed after use)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s1 = 0, s2 = 0, result = 0;
///   strDup(&s1, _S"Hello");
///   strDup(&s2, _S" World");
///   strConcatC(&result, &s1, &s2);
///   // result is "Hello World", s1 and s2 are now NULL
///   strDestroy(&result);
/// @endcode
bool strConcatC(_Inout_ strhandle o, _Inout_ strhandle sc1, _Inout_ strhandle sc2);

/// bool strNConcat(string *o, ...)
///
/// Concatenates multiple strings into an output string
///
/// Combines any number of strings into a single result. This is more efficient than
/// calling strConcat() repeatedly. For very large results, may create a rope structure.
///
/// The macro accepts a variable number of string arguments and automatically counts them.
///
/// @param o Output string (existing content destroyed)
/// @param ... Variable number of string arguments to concatenate
/// @return true on success, false on error
///
/// Example:
/// @code
///   string result = 0;
///   strNConcat(&result, _S"Hello", _S" ", _S"World", _S"!");
///   // result is "Hello World!"
///   strDestroy(&result);
/// @endcode
#define strNConcat(o, ...) _strNConcat(o, count_macro_args(__VA_ARGS__), (strref[]) { __VA_ARGS__ })
bool _strNConcat(_Inout_ strhandle o, int n, _In_ strref* _Nonnull stra);

/// bool strNConcatC(string *o, string *s1, string *s2, ...)
///
/// Concatenates multiple strings, consuming all inputs
///
/// Like strNConcat(), but takes ownership of all input strings and destroys them
/// after use. All input string handles will be NULL after this call. This is the
/// most efficient way to combine many temporary strings.
///
/// The macro accepts a variable number of string handle pointers.
///
/// @param o Output string (existing content destroyed)
/// @param ... Variable number of string handle pointers (destroyed after use)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s1 = 0, s2 = 0, s3 = 0, result = 0;
///   strDup(&s1, _S"Hello");
///   strDup(&s2, _S" ");
///   strDup(&s3, _S"World");
///   strNConcatC(&result, &s1, &s2, &s3);
///   // result is "Hello World", s1/s2/s3 are now NULL
///   strDestroy(&result);
/// @endcode
#define strNConcatC(o, ...) \
    _strNConcatC(o, count_macro_args(__VA_ARGS__), (string*[]) { __VA_ARGS__ })
bool _strNConcatC(_Inout_ strhandle o, int n, _Inout_ strhandle* _Nonnull stra);

/// Extracts a substring from a string
///
/// Creates a new string containing bytes from position b (inclusive) to position e
/// (exclusive). Negative indices count from the end. Use strEnd for e to extract to
/// the end of the string.
///
/// For large substrings, may create a rope reference instead of copying the data.
///
/// @param o Output string (existing content destroyed)
/// @param s Source string (not modified)
/// @param b Starting position (negative = from end)
/// @param e Ending position (negative = from end, strEnd = end of string)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string sub = 0;
///   strSubStr(&sub, _S"Hello World", 0, 5);    // "Hello"
///   strSubStr(&sub, _S"Hello World", 6, strEnd); // "World"
///   strSubStr(&sub, _S"Hello World", -5, strEnd); // "World" (last 5 chars)
///   strDestroy(&sub);
/// @endcode
bool strSubStr(_Inout_ strhandle o, _In_opt_ strref s, int32 b, int32 e);

/// Extracts a substring, consuming the source string
///
/// Like strSubStr(), but takes ownership of sc and destroys it after use. The sc
/// handle will be NULL after this call. More efficient when the source is no longer
/// needed.
///
/// @param o Output string (existing content destroyed)
/// @param sc Source string (destroyed after use)
/// @param b Starting position (negative = from end)
/// @param e Ending position (negative = from end, strEnd = end of string)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0, sub = 0;
///   strDup(&s, _S"Hello World");
///   strSubStrC(&sub, &s, 0, 5);  // sub is "Hello", s is now NULL
///   strDestroy(&sub);
/// @endcode
bool strSubStrC(_Inout_ strhandle o, _Inout_ strhandle sc, int32 b, int32 e);

/// Extracts a substring in-place
///
/// Modifies the string to contain only the specified range. This is the most efficient
/// way to truncate or extract from a string when you don't need the original.
///
/// @param io String to modify in-place
/// @param b Starting position (negative = from end)
/// @param e Ending position (negative = from end, strEnd = end of string)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _S"Hello World");
///   strSubStrI(&s, 0, 5);  // s is now "Hello"
///   strDestroy(&s);
/// @endcode
bool strSubStrI(_Inout_ strhandle io, int32 b, int32 e);

/// Converts a string to uppercase (ASCII only)
///
/// Modifies the string in-place, converting all lowercase ASCII letters (a-z) to
/// uppercase (A-Z). This is ASCII-only and does not properly handle multi-byte
/// UTF-8 characters or locale-specific case rules.
///
/// The string is flattened and made unique before modification.
///
/// @param io String to convert in-place
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _S"hello world");
///   strUpper(&s);  // s is now "HELLO WORLD"
///   strDestroy(&s);
/// @endcode
void strUpper(_Inout_ strhandle io);

/// Converts a string to lowercase (ASCII only)
///
/// Modifies the string in-place, converting all uppercase ASCII letters (A-Z) to
/// lowercase (a-z). This is ASCII-only and does not properly handle multi-byte
/// UTF-8 characters or locale-specific case rules.
///
/// The string is flattened and made unique before modification.
///
/// @param io String to convert in-place
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _S"HELLO WORLD");
///   strLower(&s);  // s is now "hello world"
///   strDestroy(&s);
/// @endcode
void strLower(_Inout_ strhandle io);

/// Splits a string into pieces separated by a delimiter
///
/// Divides the string s into segments at each occurrence of the separator string,
/// storing the results in a dynamic array. The output array is cleared first.
///
/// @param out Pointer to string array to store results (cleared first)
/// @param s String to split
/// @param sep Separator string to split on
/// @param empty If true, empty segments are preserved; if false, they are skipped
/// @return Number of segments created
///
/// Example:
/// @code
///   sa_string parts = {0};
///   strSplit(&parts, _S"a,b,c", _S",", false);
///   // parts contains ["a", "b", "c"]
///   for (int i = 0; i < saSize(parts); i++)
///       strDestroy(&parts.a[i]);
///   saDestroy(&parts);
///
///   strSplit(&parts, _S"a,,b", _S",", true);
///   // parts contains ["a", "", "b"] (empty segment preserved)
///   saDestroy(&parts);
/// @endcode
int32 strSplit(_Inout_ sa_string* _Nonnull out, _In_opt_ strref s, _In_opt_ strref sep, bool empty);

/// Joins an array of strings into a single string with a separator
///
/// Combines all strings in the array into one string, inserting the separator
/// between each element. The separator is not added before the first element or
/// after the last element.
///
/// @param out Output string (existing content destroyed)
/// @param arr Array of strings to join
/// @param sep Separator to insert between elements
/// @return true on success, false if array is empty
///
/// Example:
/// @code
///   sa_string parts = {0};
///   saPush(&parts, string, _S"Hello");
///   saPush(&parts, string, _S"World");
///   string result = 0;
///   strJoin(&result, parts, _S" ");
///   // result is "Hello World"
///   strDestroy(&result);
///   saDestroy(&parts);
/// @endcode
bool strJoin(_Inout_ strhandle out, _In_ sa_string arr, _In_opt_ strref sep);

/// Retrieves a single byte from a string
///
/// Gets the byte at position i in the string. Negative indices count from the end.
/// Returns 0 if the index is out of bounds.
///
/// Note: This operates on bytes, not UTF-8 characters. For multi-byte encodings,
/// use a string iterator instead.
///
/// @param str String to read from
/// @param i Index of byte to retrieve (negative = from end)
/// @return The byte at position i, or 0 if out of bounds
///
/// Example:
/// @code
///   uint8 ch = strGetChar(_S"Hello", 0);    // 'H'
///   ch = strGetChar(_S"Hello", -1);         // 'o' (last char)
/// @endcode
uint8 strGetChar(_In_opt_ strref str, int32 i);

/// Sets a single byte in a string
///
/// Modifies the byte at position i in the string. Negative indices count from the end.
/// Use strEnd for i to append a byte to the end of the string.
///
/// If the index is beyond the current length, the string is grown and zero-padded.
/// The string is flattened and made unique before modification.
///
/// Note: This operates on bytes, not UTF-8 characters. Be careful when modifying
/// multi-byte UTF-8 sequences as you can create invalid encodings.
///
/// @param str String to modify
/// @param i Index of byte to set (negative = from end, strEnd = append)
/// @param ch Byte value to set
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _S"Hello");
///   strSetChar(&s, 0, 'h');      // s is now "hello"
///   strSetChar(&s, strEnd, '!'); // s is now "hello!"
///   strDestroy(&s);
/// @endcode
void strSetChar(_Inout_ strhandle str, int32 i, uint8 ch);

/// @}  // end of string_manip group

CX_C_END
