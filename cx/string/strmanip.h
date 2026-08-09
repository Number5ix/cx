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
///   string s1 = _SL("hello");
///   string s2 = _SL(" world");
///   string result = 0;
///   strConcatC(&result, &s1, &s2);  // s1 and s2 are now NULL
/// @endcode
///
/// @section string_manip_inplace In-place variants (functions with 'I' suffix)
///
/// Functions ending in 'I' modify the string in-place, efficiently reusing the
/// existing buffer when possible:
/// @code
///   string s = _SL("hello world");
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
///   strDup(&s, _SL("Hello"));
///   strAppend(&s, _SL(" World"));  // s is now "Hello World"
///   strDestroy(&s);
/// @endcode
bool strAppend(_Inout_ strhandle io, _In_opt_ strref s);

/// Appends a raw byte buffer to a string in-place
///
/// Adds sz bytes from buf to the end of string io. This is binary safe: embedded NUL
/// bytes are preserved and the length comes from sz rather than from strlen(). The
/// result is still NUL terminated.
///
/// Because the appended bytes are arbitrary, the cached encoding flags are cleared.
///
/// If io is NULL or empty, this is equivalent to strFromBytes().
///
/// @param io String to append to (modified in-place)
/// @param buf Byte buffer to append (NULL or sz of 0 appends nothing)
/// @param sz Number of bytes to append
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _SL("len="));
///   strAppendBytes(&s, raw, rawsz);
///   strDestroy(&s);
/// @endcode
bool strAppendBytes(_Inout_ strhandle io, _In_reads_bytes_opt_(sz) const void* _Nullable buf,
                    uint32 sz);

/// Appends a single byte to a string in-place
///
/// Adds one byte to the end of the string. This replaces the strSetChar(&s, strEnd, ch)
/// idiom and is somewhat cheaper, since it does not have to resolve the append position.
///
/// Note: this operates on bytes, not UTF-8 code points. Appending a byte >= 0x80 clears
/// the cached encoding flags, since a single byte cannot complete a valid UTF-8 sequence
/// on its own.
///
/// @param io String to append to (modified in-place)
/// @param ch Byte value to append
///
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _SL("item"));
///   strAppendChar(&s, ':');   // s is now "item:"
///   strDestroy(&s);
/// @endcode
void strAppendChar(_Inout_ strhandle io, uint8 ch);

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
///   strDup(&s, _SL("World"));
///   strPrepend(_SL("Hello "), &s);  // s is now "Hello World"
///   strDestroy(&s);
/// @endcode
bool strPrepend(_In_opt_ strref s, _Inout_ strhandle io);

/// Creates a string by repeating another string a number of times
///
/// Writes n concatenated copies of s to o. A count of 0, or an empty source string,
/// produces an empty string.
///
/// The output handle may be the same as the source, in which case the string is
/// replaced by the repeated version:
/// @code
///   strRepeat(&s, s, 3);
/// @endcode
///
/// @param o Output string (existing content destroyed)
/// @param s String to repeat (not modified)
/// @param n Number of copies
/// @return true on success, false on error
///
/// Example:
/// @code
///   string bar = 0;
///   strRepeat(&bar, _SL("-="), 10);   // "-=-=-=-=-=-=-=-=-=-="
///   strDestroy(&bar);
/// @endcode
bool strRepeat(_Inout_ strhandle o, _In_opt_ strref s, uint32 n);

/// Creates a string consisting of a single byte repeated a number of times
///
/// Writes n copies of the byte ch to o. A count of 0 produces an empty string. This is
/// the efficient way to build padding or fill runs.
///
/// @param o Output string (existing content destroyed)
/// @param ch Byte value to fill with
/// @param n Number of bytes
/// @return true on success, false on error
///
/// Example:
/// @code
///   string pad = 0;
///   strFillChar(&pad, ' ', width - strLen(s));
///   strAppend(&out, pad);
///   strDestroy(&pad);
/// @endcode
bool strFillChar(_Inout_ strhandle o, uint8 ch, uint32 n);

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
///   strConcat(&result, _SL("Hello"), _SL(" World"));
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
///   strDup(&s1, _SL("Hello"));
///   strDup(&s2, _SL(" World"));
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
///   strNConcat(&result, _SL("Hello"), _SL(" "), _SL("World"), _SL("!"));
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
///   strDup(&s1, _SL("Hello"));
///   strDup(&s2, _SL(" "));
///   strDup(&s3, _SL("World"));
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
///   strSubStr(&sub, _SL("Hello World"), 0, 5);    // "Hello"
///   strSubStr(&sub, _SL("Hello World"), 6, strEnd); // "World"
///   strSubStr(&sub, _SL("Hello World"), -5, strEnd); // "World" (last 5 chars)
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
///   strDup(&s, _SL("Hello World"));
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
///   strDup(&s, _SL("Hello World"));
///   strSubStrI(&s, 0, 5);  // s is now "Hello"
///   strDestroy(&s);
/// @endcode
bool strSubStrI(_Inout_ strhandle io, int32 b, int32 e);

/// Removes leading and trailing bytes that are members of a set
///
/// Writes the portion of s between the first and last byte that is not in 'chars' to o.
/// A NULL character set means the default whitespace set: space, tab, carriage return,
/// linefeed, vertical tab, and formfeed. If every byte is in the set, the result is empty.
///
/// The output handle may be the same as the source, which is how a string is trimmed
/// in place:
/// @code
///   strTrim(&s, s, NULL);
/// @endcode
///
/// For large results this produces a rope reference instead of copying, exactly like
/// strSubStr().
///
/// @param o Output string (existing content destroyed)
/// @param s Source string (not modified)
/// @param chars Set of bytes to remove (NULL = whitespace)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strTrim(&s, _SL("  hello  "), NULL);        // "hello"
///   strTrim(&s, _SL("[hello]"), _SL("[]"));     // "hello"
///   strDestroy(&s);
/// @endcode
bool strTrim(_Inout_ strhandle o, _In_opt_ strref s, _In_opt_ strref chars);

/// Removes leading bytes that are members of a set
///
/// Like strTrim(), but only removes bytes from the beginning of the string.
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param chars Set of bytes to remove (NULL = whitespace)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strLTrim(&s, _SL("  hello  "), NULL);   // "hello  "
///   strDestroy(&s);
/// @endcode
bool strLTrim(_Inout_ strhandle o, _In_opt_ strref s, _In_opt_ strref chars);

/// Removes trailing bytes that are members of a set
///
/// Like strTrim(), but only removes bytes from the end of the string.
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param chars Set of bytes to remove (NULL = whitespace)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strRTrim(&s, _SL("  hello  "), NULL);   // "  hello"
///   strDestroy(&s);
/// @endcode
bool strRTrim(_Inout_ strhandle o, _In_opt_ strref s, _In_opt_ strref chars);

/// Replaces every occurrence of a byte with another byte
///
/// Writes s to o with every occurrence of 'from' replaced by 'to'. Since the length does
/// not change, this is a single pass over the buffer with no searching.
///
/// The output handle may be the same as the source, which causes the replacement to
/// be performed in-place.
/// @code
///   strReplaceChar(&s, s, '\\', '/');
/// @endcode
///
/// Note: this operates on bytes, not UTF-8 code points. Replacing a byte >= 0x80 can
/// corrupt a multi-byte sequence, so the cached encoding flags are cleared unless both
/// bytes are ASCII.
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param from Byte to search for
/// @param to Byte to replace it with
/// @return true on success, false on error
///
/// Example:
/// @code
///   string path = 0;
///   strDup(&path, _SL("a\\b\\c"));
///   strReplaceChar(&path, path, '\\', '/');   // "a/b/c"
///   strDestroy(&path);
/// @endcode
bool strReplaceChar(_Inout_ strhandle o, _In_opt_ strref s, char from, char to);

/// Replaces every occurrence of a byte with another byte, ignoring case
///
/// Like strReplaceChar(), but matches 'from' case-insensitively (ASCII only). The
/// replacement byte is written exactly as given, so the case of the result comes from
/// 'to' and not from what was matched.
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param from Byte to search for (matched in either case)
/// @param to Byte to replace it with
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strReplaceChari(&s, _SL("aAbB"), 'a', '-');   // "--bB"
///   strDestroy(&s);
/// @endcode
bool strReplaceChari(_Inout_ strhandle o, _In_opt_ strref s, char from, char to);

/// Replaces occurrences of a substring with another string
///
/// Writes s to o with occurrences of 'find' replaced by 'repl'. The search is
/// non-overlapping and proceeds left to right; the replacement text is never rescanned.
/// An empty or NULL 'find' matches nothing and the source is copied unchanged.
///
/// The output handle may be the same as the source, which replaces in place:
/// @code
///   strReplacei(&s, s, _SL("http://"), _SL("https://"), 0);
/// @endcode
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param find Substring to search for
/// @param repl Replacement string (NULL or empty deletes the match)
/// @param max Maximum number of replacements, or 0 (or negative) for all
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strReplace(&s, _SL("a,b,c"), _SL(","), _SL(" - "), 0);   // "a - b - c"
///   strReplace(&s, _SL("a,b,c"), _SL(","), _SL(" - "), 1);   // "a - b,c"
///   strDestroy(&s);
/// @endcode
bool strReplace(_Inout_ strhandle o, _In_opt_ strref s, _In_opt_ strref find, _In_opt_ strref repl,
                int32 max);

/// Replaces occurrences of a substring with another string, ignoring case
///
/// Like strReplace(), but matches 'find' case-insensitively (ASCII only).
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param find Substring to search for (matched without regard to case)
/// @param repl Replacement string (NULL or empty deletes the match)
/// @param max Maximum number of replacements, or 0 (or negative) for all
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strReplacei(&s, _SL("Foo foo FOO"), _SL("foo"), _SL("bar"), 0);   // "bar bar bar"
///   strDestroy(&s);
/// @endcode
bool strReplacei(_Inout_ strhandle o, _In_opt_ strref s, _In_opt_ strref find, _In_opt_ strref repl,
                 int32 max);

/// Inserts a string at a byte offset
///
/// Writes s to o with 'ins' spliced in at byte offset 'off'. Negative offsets count from
/// the end of the string and strEnd appends, matching strSubStr(). Offsets beyond the end
/// of the string are clamped.
///
/// The output handle may be the same as the source, which inserts in place.
///
/// Note: this operates on bytes, not UTF-8 code points. Inserting in the middle of a
/// multi-byte sequence produces invalid UTF-8; use strU8Offset() to find a safe offset.
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param off Byte offset to insert at (negative = from end, strEnd = append)
/// @param ins String to insert (NULL or empty leaves the source unchanged)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strInsert(&s, _SL("hello world"), 5, _SL(","));   // "hello, world"
///   strDestroy(&s);
/// @endcode
bool strInsert(_Inout_ strhandle o, _In_opt_ strref s, int32 off, _In_opt_ strref ins);

/// Removes a range of bytes from a string
///
/// Writes s to o with bytes from position b (inclusive) to position e (exclusive)
/// removed. Negative indices count from the end and strEnd means the end of the string,
/// matching strSubStr() — strErase() removes exactly the range that strSubStr() would
/// have kept.
///
/// The output handle may be the same as the source, which erases in place.
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param b Starting position of the range to remove (negative = from end)
/// @param e Ending position of the range to remove (negative = from end, strEnd = end)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strErase(&s, _SL("hello, world"), 5, 7);        // "helloworld"
///   strErase(&s, _SL("hello, world"), -6, strEnd);  // "hello, "
///   strDestroy(&s);
/// @endcode
bool strErase(_Inout_ strhandle o, _In_opt_ strref s, int32 b, int32 e);

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
///   strDup(&s, _SL("hello world"));
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
///   strDup(&s, _SL("HELLO WORLD"));
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
///   strSplit(&parts, _SL("a,b,c"), _SL(","), false);
///   // parts contains ["a", "b", "c"]
///   for (int i = 0; i < saSize(parts); i++)
///       strDestroy(&parts.a[i]);
///   saDestroy(&parts);
///
///   strSplit(&parts, _SL("a,,b"), _SL(","), true);
///   // parts contains ["a", "", "b"] (empty segment preserved)
///   saDestroy(&parts);
/// @endcode
int32 strSplit(_Inout_ sa_string* _Nonnull out, _In_opt_ strref s, _In_opt_ strref sep, bool empty);

/// Splits a string at any of a set of delimiter bytes
///
/// Like strSplit(), but the string is divided at every byte that appears anywhere in
/// 'chars' rather than at occurrences of a multi-byte separator. An empty or NULL
/// character set never matches, so the whole string comes back as one segment.
///
/// Note: this operates on bytes, not UTF-8 code points.
///
/// @param out Pointer to string array to store results (cleared first)
/// @param s String to split
/// @param chars Set of delimiter bytes to split on
/// @param empty If true, empty segments are preserved; if false, they are skipped
/// @return Number of segments created
///
/// Example:
/// @code
///   sa_string parts = { 0 };
///   strSplitAny(&parts, _SL("a,b;c"), _SL(",;"), false);
///   // parts contains ["a", "b", "c"]
///   saDestroy(&parts);
/// @endcode
int32 strSplitAny(_Inout_ sa_string* _Nonnull out, _In_opt_ strref s, _In_opt_ strref chars,
                  bool empty);

/// Splits a string into at most a given number of pieces
///
/// Like strSplit(), but stops splitting once maxparts segments have been produced. The
/// final element holds the entire unsplit remainder of the string, separators included.
/// A maxparts of 0 (or negative) means no limit, making this identical to strSplit().
///
/// @param out Pointer to string array to store results (cleared first)
/// @param s String to split
/// @param sep Separator string to split on
/// @param empty If true, empty segments are preserved; if false, they are skipped
/// @param maxparts Maximum number of segments, or 0 for unlimited
/// @return Number of segments created
///
/// Example:
/// @code
///   sa_string parts = { 0 };
///   strSplitMax(&parts, _SL("key=a=b"), _SL("="), true, 2);
///   // parts contains ["key", "a=b"]
///   saDestroy(&parts);
/// @endcode
int32 strSplitMax(_Inout_ sa_string* _Nonnull out, _In_opt_ strref s, _In_opt_ strref sep,
                  bool empty, int32 maxparts);

/// Splits a string at any of a set of delimiter bytes, up to a limit
///
/// Combines strSplitAny() and strSplitMax(): the string is divided at every byte in
/// 'chars', and the final element holds the unsplit remainder once maxparts segments
/// have been produced.
///
/// @param out Pointer to string array to store results (cleared first)
/// @param s String to split
/// @param chars Set of delimiter bytes to split on
/// @param empty If true, empty segments are preserved; if false, they are skipped
/// @param maxparts Maximum number of segments, or 0 for unlimited
/// @return Number of segments created
///
/// Example:
/// @code
///   sa_string parts = { 0 };
///   strSplitAnyMax(&parts, _SL("cmd arg1 arg2 arg3"), _SL(" "), false, 2);
///   // parts contains ["cmd", "arg1 arg2 arg3"]
///   saDestroy(&parts);
/// @endcode
int32 strSplitAnyMax(_Inout_ sa_string* _Nonnull out, _In_opt_ strref s, _In_opt_ strref chars,
                     bool empty, int32 maxparts);

/// Retrieves the next piece of a string being split, without building an array
///
/// Cursor-style alternative to strSplit() for callers that only need one segment at a
/// time. Initialize the cursor to 0 and call repeatedly until it returns false. Empty
/// segments are always produced, matching strSplit() with empty set to true.
///
/// The segment is still allocated (as a rope reference for large ones), but the
/// sa_string is never materialized.
///
/// @param s String to split (not modified)
/// @param pos Cursor; initialize to 0 before the first call, then leave it alone
/// @param sep Separator string to split on
/// @param out Output string receiving the segment (existing content destroyed)
/// @return true if a segment was produced, false once the string is exhausted
///
/// Example:
/// @code
///   int32 pos = 0;
///   string piece = 0;
///   while (strSplitNext(csv, &pos, _SL(","), &piece)) {
///       // ... use piece ...
///   }
///   strDestroy(&piece);
/// @endcode
bool strSplitNext(_In_opt_ strref s, _Inout_ int32* _Nonnull pos, _In_opt_ strref sep,
                  _Inout_ strhandle out);

/// Retrieves the next piece of a string being split at any of a set of bytes
///
/// Like strSplitNext(), but divides the string at every byte that appears anywhere in
/// 'chars' rather than at occurrences of a multi-byte separator.
///
/// @param s String to split (not modified)
/// @param pos Cursor; initialize to 0 before the first call, then leave it alone
/// @param chars Set of delimiter bytes to split on
/// @param out Output string receiving the segment (existing content destroyed)
/// @return true if a segment was produced, false once the string is exhausted
///
/// Example:
/// @code
///   int32 pos = 0;
///   string line = 0;
///   while (strSplitNextAny(text, &pos, _SL("\r\n"), &line)) {
///       // ... use line ...
///   }
///   strDestroy(&line);
/// @endcode
bool strSplitNextAny(_In_opt_ strref s, _Inout_ int32* _Nonnull pos, _In_opt_ strref chars,
                     _Inout_ strhandle out);

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
///   saPush(&parts, string, _SL("Hello"));
///   saPush(&parts, string, _SL("World"));
///   string result = 0;
///   strJoin(&result, parts, _SL(" "));
///   // result is "Hello World"
///   strDestroy(&result);
///   saDestroy(&parts);
/// @endcode
bool strJoin(_Inout_ strhandle out, _In_ sa_string arr, _In_opt_ strref sep);

/// Retrieves a single byte from a string
///
/// Gets the byte at position i in the string. Negative indices count from the end,
/// stopping at the start of the string rather than wrapping around. Returns 0 if the
/// index is out of bounds.
///
/// Indices resolve exactly as they do for strSetChar(), so the two are safe to pair up
/// on the same index.
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
///   uint8 ch = strGetChar(_SL("Hello"), 0);    // 'H'
///   ch = strGetChar(_SL("Hello"), -1);         // 'o' (last char)
///   ch = strGetChar(_SL("Hello"), -99);        // 'H' (clamped to the start)
///   ch = strGetChar(_SL("Hello"), 99);         // 0 (past the end)
/// @endcode
uint8 strGetChar(_In_opt_ strref str, int32 i);

/// Sets a single byte in a string
///
/// Modifies the byte at position i in the string. Negative indices count from the end,
/// stopping at the start of the string rather than wrapping around. Use strEnd for i to
/// append a byte to the end of the string.
///
/// If a positive index is beyond the current length, the string is grown and zero-padded.
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
///   strDup(&s, _SL("Hello"));
///   strSetChar(&s, 0, 'h');      // s is now "hello"
///   strSetChar(&s, strEnd, '!'); // s is now "hello!"
///   strDestroy(&s);
/// @endcode
void strSetChar(_Inout_ strhandle str, int32 i, uint8 ch);

/// @}  // end of string_manip group

CX_C_END
