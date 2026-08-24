#pragma once

#include <cx/string/strbase.h>

/// @file strscan.h
/// @brief Cursor-based string scanner for hand-written parsers

/// @defgroup string_scan Scanning
/// @ingroup string
/// @{
///
/// A cursor that walks a string left to right, matching and extracting pieces as it goes.
/// This is the low-level half of cx's parsing support: use it for grammars that a pattern
/// cannot express, and use @ref string_parse "pattern matching" for everything else.
///
/// @section strscan_sticky The sticky error flag
///
/// A scanner carries an `ok` flag that starts true and turns false the first time
/// something does not match. Once it is false every later call does nothing and returns
/// false immediately. That means a grammar can be written as a straight run of calls with
/// a single check at the end, instead of an `if` around every step:
///
/// @code
///   strscan sc;
///   strscInit(&sc, line);
///
///   string method = 0, target = 0;
///   uint32 minor  = 0;
///
///   strscToken(&sc, &method, _SL(" "));
///   strscWS1(&sc);
///   strscToken(&sc, &target, _SL(" "));
///   strscWS1(&sc);
///   strscLit(&sc, _SL("HTTP/1."));
///   strscUInt32(&sc, &minor, 10);
///
///   if (!strscFinish(&sc)) {
///       // something above did not match; strscErrPos said where
///   }
/// @endcode
///
/// @section strscan_backtrack Backtracking
///
/// `strscMark()` records the current position and `strscRewind()` returns to it. Rewinding
/// also clears the error flag, so it is how alternatives are tried:
///
/// @code
///   int32 mark = strscMark(&sc);
///   if (!parseFirstForm(&sc)) {
///       strscRewind(&sc, mark);
///       parseSecondForm(&sc);
///   }
/// @endcode
///
/// @section strscan_lifetime Lifetime
///
/// The scanner borrows the string rather than holding a reference to it, so the string
/// must stay alive for as long as the scanner is in use. Extracted pieces are ordinary
/// strings that the caller owns and must destroy.

CX_C_BEGIN

/// Flags controlling how a scanner matches
enum STRSC_FLAGS {
    STRSC_CaseInsensitive = 0x01,   ///< Literals and character sets match without regard to case
};

/// String scanner state
///
/// Read `pos`, `errpos`, `ok` and `s` freely; change the position with strscSeek() or
/// strscRewind() rather than by assignment.
typedef struct strscan {
    strref s;        ///< String being scanned (borrowed)
    int32 pos;       ///< Current byte offset
    int32 errpos;    ///< Offset where the scan first failed, or -1
    bool ok;         ///< False once anything has failed; every later call is a no-op
    flags_t flags;   ///< STRSC_FLAGS the scanner was created with

    // Private members - do not access directly
    int32 _len;
    int32 _spanoff;
    int32 _spanlen;
} strscan;

// Internal - use the strscInit() macro
void _strscInit(_Out_ strscan* _Nonnull sc, _In_opt_ strref s, flags_t flags);

/// void strscInit(strscan *sc, strref s, [flags])
///
/// Starts a scan at the beginning of a string.
///
/// The string is borrowed, so it must outlive the scanner.
///
/// @param sc Scanner to initialize
/// @param s String to scan (NULL scans an empty string)
/// @param ... (flags) Optional: STRSC_FLAGS
///
/// Example:
/// @code
///   strscan sc;
///   strscInit(&sc, line);
///   strscInit(&sc, line, STRSC_CaseInsensitive);
/// @endcode
#define strscInit(sc, s, ...) _strscInit(sc, s, opt_flags(__VA_ARGS__))

/// Ends a scan and reports whether everything matched.
///
/// Does not check that the whole string was consumed - call strscDone() before this if the
/// grammar requires that.
///
/// The scanner lets go of the string here, so nothing may be matched or extracted
/// afterward; ok and errpos are still set, which is what an error report needs.
///
/// @param sc Scanner to finish
/// @return true if no step failed
bool strscFinish(_Inout_ strscan* _Nonnull sc);

/// bool strscDone(strscan *sc)
///
/// Tests whether the cursor has reached the end of the string.
///
/// @param sc Scanner to test
/// @return true if there is nothing left to read
_meta_inline bool strscDone(_In_ strscan* _Nonnull sc)
{
    return sc->pos >= sc->_len;
}

/// int32 strscMark(strscan *sc)
///
/// Records the current position so strscRewind() can come back to it.
/// Currently is the same as reading pos, but this may change so do not depend on it.
///
/// @param sc Scanner to inspect
/// @return Opaque position marker
_meta_inline int32 strscMark(_In_ strscan* _Nonnull sc)
{
    return sc->pos;
}

/// Returns to a recorded position and clears the error flag.
///
/// This is how alternatives are tried: mark, attempt one form, and rewind to attempt
/// another. Clearing the error is the point - a failed attempt that has been rewound
/// never happened.
///
/// @param sc Scanner to reposition
/// @param mark Position from strscMark()
void strscRewind(_Inout_ strscan* _Nonnull sc, int32 mark);

/// Moves the cursor to an absolute offset.
///
/// Unlike strscRewind(), this does not clear the error flag, and it fails if the offset
/// is outside the string.
///
/// @param sc Scanner to reposition
/// @param pos Byte offset to move to
/// @return true if the offset was in range
bool strscSeek(_Inout_ strscan* _Nonnull sc, int32 pos);

/// Marks the scan as failed at the current position.
///
/// For a check the scanner itself cannot make - a day number that parsed fine but is out
/// of range, say. Does nothing if the scan has already failed.
///
/// @param sc Scanner to fail
void strscFail(_Inout_ strscan* _Nonnull sc);

/// @defgroup string_scan_literal Literals and Characters
/// @ingroup string_scan
/// @{
///
/// `strscLit`/`strscChar` require a match and fail the scan if they do not get one.
/// `strscTry`/`strscTryChar` consume a match if there is one and report back without
/// failing. `strscPeek`/`strscPeekChar` look without consuming.

/// Matches a literal and consumes it.
///
/// @param sc Scanner to read from
/// @param lit Text that must appear at the cursor
/// @return true if it matched
bool strscLit(_Inout_ strscan* _Nonnull sc, _In_opt_ strref lit);

/// Matches a single byte and consumes it.
///
/// @param sc Scanner to read from
/// @param ch Byte that must appear at the cursor
/// @return true if it matched
bool strscChar(_Inout_ strscan* _Nonnull sc, char ch);

/// Consumes a literal if it is there, without failing the scan if it is not.
///
/// @param sc Scanner to read from
/// @param lit Text to look for
/// @return true if it matched and was consumed
bool strscTry(_Inout_ strscan* _Nonnull sc, _In_opt_ strref lit);

/// Consumes a single byte if it is there, without failing the scan if it is not.
///
/// @param sc Scanner to read from
/// @param ch Byte to look for
/// @return true if it matched and was consumed
bool strscTryChar(_Inout_ strscan* _Nonnull sc, char ch);

/// Tests for a literal at the cursor without consuming anything.
///
/// @param sc Scanner to read from
/// @param lit Text to look for
/// @return true if it is at the cursor
bool strscPeek(_In_ strscan* _Nonnull sc, _In_opt_ strref lit);

/// Returns the byte at the cursor without consuming it.
///
/// @param sc Scanner to read from
/// @return The byte, or 0 at the end of the string or after a failure
uint8 strscPeekChar(_In_ strscan* _Nonnull sc);

/// Skips any run of whitespace, including none at all.
///
/// @param sc Scanner to advance
/// @return true unless the scan had already failed
bool strscWS(_Inout_ strscan* _Nonnull sc);

/// Requires and skips a run of at least one whitespace byte.
///
/// @param sc Scanner to advance
/// @return true if there was whitespace to skip
bool strscWS1(_Inout_ strscan* _Nonnull sc);

/// @}  // end of string_scan_literal group

/// @defgroup string_scan_extract Extraction
/// @ingroup string_scan
/// @{
///
/// Every extraction function takes an output string handle that may be NULL, which
/// consumes the text without building a string for it. The span that was consumed is
/// always recorded and can be read back with strscSpan(), which is how a token is
/// compared against something without materializing it at all.

/// Reads a run of bytes up to the next delimiter.
///
/// Stops before the delimiter without consuming it. An empty token is a failure - use
/// strscUntil() where an empty result is meaningful.
///
/// @param sc Scanner to read from
/// @param out Receives the token, or NULL to consume it without building a string
/// @param delims Set of bytes that end the token
/// @return true if at least one byte was read
bool strscToken(_Inout_ strscan* _Nonnull sc, _Inout_opt_ strhandle out, _In_opt_ strref delims);

/// Reads everything up to the next occurrence of some text.
///
/// Stops before the text without consuming it, and fails if the text does not appear at
/// all. An empty result is fine - it just means the text is at the cursor already.
///
/// @param sc Scanner to read from
/// @param out Receives the text read, or NULL to consume it without building a string
/// @param text Text to stop before
/// @return true if the text was found
bool strscUntil(_Inout_ strscan* _Nonnull sc, _Inout_opt_ strhandle out, _In_opt_ strref text);

/// Reads a run of bytes drawn from a set.
///
/// The mirror of strscToken(): it stops at the first byte that is *not* in the set. An
/// empty run is a failure.
///
/// @param sc Scanner to read from
/// @param out Receives the run, or NULL to consume it without building a string
/// @param chars Set of bytes the run may contain
/// @return true if at least one byte was read
bool strscWhile(_Inout_ strscan* _Nonnull sc, _Inout_opt_ strhandle out, _In_opt_ strref chars);

/// Reads a double-quoted string and removes its escapes.
///
/// Requires a `"` at the cursor and reads to the closing one, turning `\x` into `x`
/// along the way. The quotes themselves are consumed but are not part of the result.
///
/// The recorded span covers the text between the quotes as it appeared in the input, so
/// it still contains any backslashes.
///
/// @param sc Scanner to read from
/// @param out Receives the unescaped contents, or NULL to consume them
/// @return true if a complete quoted string was read
bool strscQuoted(_Inout_ strscan* _Nonnull sc, _Inout_opt_ strhandle out);

/// Reads one line and consumes its terminator.
///
/// Accepts CRLF or a bare LF. The last line of a string needs no terminator. The result
/// never includes the terminator, and an empty line is a valid result.
///
/// @param sc Scanner to read from
/// @param out Receives the line, or NULL to consume it
/// @return true if there was a line left to read
bool strscLine(_Inout_ strscan* _Nonnull sc, _Inout_opt_ strhandle out);

/// Reads everything left and moves the cursor to the end.
///
/// An empty remainder is a valid result.
///
/// @param sc Scanner to read from
/// @param out Receives the remaining text, or NULL to consume it
/// @return true unless the scan had already failed
bool strscRest(_Inout_ strscan* _Nonnull sc, _Inout_opt_ strhandle out);

/// Reports the span of the most recent extraction.
///
/// The offset and length index into the scanner's own string, so the text can be compared
/// with strRangeEq() or copied out with strSubStr() without having built a string for it
/// in the first place.
///
/// @param sc Scanner to inspect
/// @param off Receives the byte offset of the span
/// @param len Receives the length of the span
///
/// Example:
/// @code
///   int32 off, len;
///   strscToken(&sc, NULL, _SL(" "));
///   strscSpan(&sc, &off, &len);
///   if (strRangeEqi(sc.s, _SL("GET"), off, len)) { ... }
/// @endcode
void strscSpan(_In_ strscan* _Nonnull sc, _Out_ int32* _Nonnull off, _Out_ int32* _Nonnull len);

/// @}  // end of string_scan_extract group

/// @defgroup string_scan_typed Typed Values
/// @ingroup string_scan
/// @{
///
/// The numeric readers consume exactly what their own syntax allows and stop there, so
/// `"12,"` reads as 12 and leaves the comma at the cursor. None of them skip leading
/// whitespace or accept a "0x" prefix - call strscWS() first if the grammar allows space
/// there, and pass base 16 for hex.

/// Reads a decimal or based integer.
///
/// @param sc Scanner to read from
/// @param out Receives the value
/// @param base Numeric base (2-36)
/// @return true if a value in range was read
_Success_(
    return) bool strscInt32(_Inout_ strscan* _Nonnull sc, _Out_ int32* _Nonnull out, int base);

/// Reads an unsigned integer. A sign is not accepted.
///
/// @param sc Scanner to read from
/// @param out Receives the value
/// @param base Numeric base (2-36)
/// @return true if a value in range was read
_Success_(
    return) bool strscUInt32(_Inout_ strscan* _Nonnull sc, _Out_ uint32* _Nonnull out, int base);

/// Reads a 64-bit integer.
///
/// @param sc Scanner to read from
/// @param out Receives the value
/// @param base Numeric base (2-36)
/// @return true if a value in range was read
_Success_(
    return) bool strscInt64(_Inout_ strscan* _Nonnull sc, _Out_ int64* _Nonnull out, int base);

/// Reads an unsigned 64-bit integer. A sign is not accepted.
///
/// @param sc Scanner to read from
/// @param out Receives the value
/// @param base Numeric base (2-36)
/// @return true if a value in range was read
_Success_(
    return) bool strscUInt64(_Inout_ strscan* _Nonnull sc, _Out_ uint64* _Nonnull out, int base);

/// Reads a floating point number, in decimal or scientific notation.
///
/// @param sc Scanner to read from
/// @param out Receives the value
/// @return true if a value was read
_Success_(return) bool strscFloat32(_Inout_ strscan* _Nonnull sc, _Out_ float32* _Nonnull out);

/// Reads a floating point number, in decimal or scientific notation.
///
/// @param sc Scanner to read from
/// @param out Receives the value
/// @return true if a value was read
_Success_(return) bool strscFloat64(_Inout_ strscan* _Nonnull sc, _Out_ float64* _Nonnull out);

/// Reads a boolean written as true/false, yes/no or 1/0.
///
/// The words are matched without regard to case.
///
/// @param sc Scanner to read from
/// @param out Receives the value
/// @return true if a boolean was read
_Success_(return) bool strscBool(_Inout_ strscan* _Nonnull sc, _Out_ bool* _Nonnull out);

// Internal - use the strscVal() macro
bool _strscVal(_Inout_ strscan* _Nonnull sc, stype st, _Out_ stgeneric* _Nonnull out);

/// bool strscVal(strscan *sc, type, type *pval)
///
/// Reads a value of any type the type system knows how to build from a string.
///
/// Numbers and booleans read exactly as the functions above do. Every other type reads a
/// whitespace-delimited word and converts it, which is how a scanner picks up SUIDs,
/// enums, custom types and anything else with a string conversion.
///
/// @param sc Scanner to read from
/// @param type Type name of the destination
/// @param pval Pointer to the variable that receives the value
/// @return true if a value was read and converted
///
/// Example:
/// @code
///   int32 count;
///   SUID id;
///   strscVal(&sc, int32, &count);
///   strscWS1(&sc);
///   strscVal(&sc, suid, &id);
/// @endcode
#define strscVal(sc, type, pval) _strscVal(sc, stCheckedPtrArg(type, pval))

/// @}  // end of string_scan_typed group

/// @}  // end of string_scan group

CX_C_END
