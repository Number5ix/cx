#pragma once

#include <cx/string/strbase.h>

/// @defgroup string_encoding Encoding
/// @ingroup string
/// @{
///
/// String encoding validation and conversion.
///
/// CX strings internally use UTF-8, ASCII, or unspecified/binary encoding.
/// This module provides validation and conversion to/from other encodings.

CX_C_BEGIN

/// Validates that a string contains valid UTF-8 sequences
///
/// Verifies that all byte sequences in the string form valid UTF-8 code points.
/// If validation succeeds, the UTF-8 flag is cached in the string header for
/// future reference (if the string was allocated by CX).
///
/// @param s String to validate
/// @return true if the string contains only valid UTF-8, false otherwise
///
/// Example:
/// @code
///   if (strValidUTF8(s)) {
///       // Safe to process as UTF-8 text
///   }
/// @endcode
_When_(s == NULL, _Post_equal_to_(false)) bool strValidUTF8(_In_opt_ strref s);

/// Validates that a string contains only ASCII characters
///
/// Verifies that all bytes in the string are in the ASCII range (0x00-0x7F).
/// If validation succeeds, both the ASCII and UTF-8 flags are cached in the
/// string header (since ASCII is a subset of UTF-8).
///
/// @param s String to validate
/// @return true if the string contains only ASCII characters, false otherwise
///
/// Example:
/// @code
///   if (strValidASCII(filename)) {
///       // Safe to use with ASCII-only APIs
///   }
/// @endcode
_When_(s == NULL, _Post_equal_to_(false)) bool strValidASCII(_In_opt_ strref s);

/// Counts the number of UTF-8 code points in a string
///
/// Returns the number of code points, as opposed to strLen() which returns the number of
/// bytes. The two are equal only for pure ASCII. If the string is not valid UTF-8, 0 is
/// returned — as it also is for an empty string, which is the same answer either way.
///
/// This is an O(n) scan; the length is not cached. For ASCII strings the byte length is
/// used directly when the string is already known to be ASCII.
///
/// @param s String to measure
/// @return Number of code points, or 0 if the string is empty or not valid UTF-8
///
/// Example:
/// @code
///   uint32 chars = strU8Len(_SLU("H\xC3\xA9llo"));   // 5 code points, 6 bytes
/// @endcode
uint32 strU8Len(_In_opt_ strref s);

/// Converts a UTF-8 code point index to a byte offset
///
/// Finds the byte offset where the code point at index charIdx begins. This is the
/// bridge between code point positions and the byte offsets that strSubStr(), strFind(),
/// and the rest of the API work in.
///
/// Negative indices count from the end of the string, so -1 is the last code point.
/// strEnd, and an index exactly equal to the code point count, both give the byte length
/// of the string — one past the last code point, which is what a range endpoint needs.
/// strEnd is answered from the byte length directly and does not validate the encoding.
///
/// @param s String to index into
/// @param charIdx Code point index (negative = from end, strEnd = end of string)
/// @return Byte offset of that code point, or -1 if out of range or not valid UTF-8
///
/// Example:
/// @code
///   int32 off = strU8Offset(s, 3);       // byte offset of the 4th code point
///   int32 last = strU8Offset(s, -1);     // byte offset of the last code point
/// @endcode
int32 strU8Offset(_In_opt_ strref s, int32 charIdx);

/// Replaces invalid UTF-8 sequences with the Unicode replacement character
///
/// Writes s to o with every byte that is not part of a valid UTF-8 sequence replaced by
/// U+FFFD (the replacement character). The result is always valid UTF-8, which makes this
/// the way to make untrusted input safe to treat as text.
///
/// Valid input is passed through without copying, so this is cheap to call defensively.
///
/// The output handle may be the same as the source, which sanitizes in place:
/// @code
///   strSanitizeUTF8(&s, s);
/// @endcode
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @return true on success, false on error
///
/// Example:
/// @code
///   string clean = 0;
///   strSanitizeUTF8(&clean, untrusted);
///   // ... clean is guaranteed valid UTF-8 ...
///   strDestroy(&clean);
/// @endcode
bool strSanitizeUTF8(_Inout_ strhandle o, _In_opt_ strref s);

/// Extracts a substring by UTF-8 code point index
///
/// Like strSubStr(), but b and e are code point indices rather than byte offsets, so a
/// multi-byte sequence is never sliced in half. Negative indices count from the end and
/// strEnd means the end of the string, exactly as for strSubStr(); out-of-range indices
/// are clamped the same way.
///
/// The string must be valid UTF-8. Sanitize it with strSanitizeUTF8() first if that is
/// not guaranteed.
///
/// @param o Output string (existing content destroyed, may be the same handle as s)
/// @param s Source string (not modified)
/// @param b Starting code point (negative = from end)
/// @param e Ending code point (negative = from end, strEnd = end of string)
/// @return true on success, false if the source is NULL or not valid UTF-8
///
/// Example:
/// @code
///   string sub = 0;
///   strSubStrU8(&sub, _SLU("H\xC3\xA9llo"), 0, 2);   // "H\xC3\xA9" -- 2 code points
///   strDestroy(&sub);
/// @endcode
bool strSubStrU8(_Inout_ strhandle o, _In_opt_ strref s, int32 b, int32 e);

/// Converts a UTF-8 string to UTF-16 encoding
///
/// Encodes the string as UTF-16 code units, including surrogate pairs for code
/// points outside the Basic Multilingual Plane. The string must be valid UTF-8
/// or this function will fail.
///
/// This function can be called twice: first with buf=NULL to query the required
/// buffer size, then with an allocated buffer to perform the conversion.
///
/// @param s UTF-8 string to convert
/// @param buf Output buffer for UTF-16 code units (NULL to query size)
/// @param wsz Size of output buffer in uint16 elements
/// @return Number of uint16 elements required (including null terminator), or 0 on error
///
/// Example:
/// @code
///   size_t sz = strToUTF16(s, NULL, 0);  // Query size
///   uint16 *buf = xaAlloc(sz * sizeof(uint16));
///   strToUTF16(s, buf, sz);              // Convert
///   // ... use buf ...
///   xaFree(buf);
/// @endcode
size_t strToUTF16(_In_opt_ strref s, _Out_writes_opt_(wsz) uint16* _Nullable buf, size_t wsz);

/// Converts a UTF-8 string to UTF-16 in an allocated buffer
///
/// Convenience wrapper around strToUTF16() that allocates the buffer automatically.
/// The returned buffer must be freed with xaFree() when no longer needed.
///
/// @param s UTF-8 string to convert
/// @return Allocated UTF-16 buffer (null-terminated), or NULL on error. Caller must free with
/// xaFree()
///
/// Example:
/// @code
///   uint16 *wide = strToUTF16A(s);
///   if (wide) {
///       // ... use wide ...
///       xaFree(wide);
///   }
/// @endcode
_Ret_opt_valid_ uint16* _Nullable strToUTF16A(_In_opt_ strref s);

/// Converts a UTF-8 string to UTF-16 in a scratch buffer
///
/// Convenience wrapper around strToUTF16() that uses a temporary scratch buffer.
/// This is useful for passing to OS APIs that require UTF-16 strings.
///
/// IMPORTANT: The returned buffer is temporary and may be overwritten by other
/// operations (see cx/utils/scratch.h). Use or copy the result immediately.
///
/// @param s UTF-8 string to convert
/// @return Scratch buffer with UTF-16 encoding (null-terminated), or NULL on error. Do not free -
/// buffer is managed by scratch system
///
/// Example:
/// @code
///   uint16 *wide = strToUTF16S(path);
///   if (wide) {
///       CreateFileW(wide, ...);  // Use immediately
///   }
/// @endcode
_Ret_opt_valid_ uint16* _Nullable strToUTF16S(_In_opt_ strref s);

/// Converts a UTF-16 encoded buffer to a UTF-8 string
///
/// Decodes UTF-16 code units (including surrogate pairs) into a UTF-8 string.
/// The function validates the UTF-16 encoding and will fail if invalid sequences
/// are encountered. The buffer size should NOT include a null terminator if present.
///
/// Any existing string in the output parameter is destroyed first.
///
/// @param o Pointer to output string variable
/// @param buf Buffer containing UTF-16 code units
/// @param wsz Number of uint16 elements in buffer (excluding null terminator)
/// @return true on success, false if UTF-16 encoding is invalid
///
/// Example:
/// @code
///   string s = 0;
///   if (strFromUTF16(&s, wideBuf, cstrLenw(wideBuf))) {
///       // Conversion successful
///   }
///   strDestroy(&s);
/// @endcode
bool strFromUTF16(_Inout_ strhandle o, _In_reads_(wsz) const uint16* _Nonnull buf, size_t wsz);

/// Encodes binary data as a base64 string
///
/// Converts arbitrary binary data into base64 text encoding. Supports both
/// standard base64 and URL-safe base64 (using '-' and '_' instead of '+' and '/').
///
/// Any existing string in the output parameter is destroyed first.
///
/// @param out Pointer to output string variable
/// @param buf Binary data to encode
/// @param sz Size of binary data in bytes
/// @param urlsafe Use URL-safe base64 alphabet if true
/// @return true on success, false on error
///
/// Example:
/// @code
///   string encoded = 0;
///   strB64Encode(&encoded, data, dataSize, false);
///   // ... use encoded ...
///   strDestroy(&encoded);
/// @endcode
bool strB64Encode(_Inout_ strhandle out, _In_reads_bytes_(sz) const uint8* _Nonnull buf, uint32 sz,
                  bool urlsafe);

/// Decodes a base64 string to binary data
///
/// Converts base64 text encoding back to binary data. Supports both standard
/// and URL-safe base64 encodings automatically.
///
/// This function can be called twice: first with buf=NULL to query the required
/// buffer size, then with an allocated buffer to perform the decoding.
///
/// @param s Base64 encoded string
/// @param buf Output buffer for binary data (NULL to query size)
/// @param sz Size of output buffer in bytes
/// @return Number of bytes required for decoded data, or 0 on error
///
/// Example:
/// @code
///   uint32 sz = strB64Decode(encoded, NULL, 0);  // Query size
///   uint8 *data = xaAlloc(sz);
///   strB64Decode(encoded, data, sz);             // Decode
///   // ... use data ...
///   xaFree(data);
/// @endcode
uint32 strB64Decode(_In_opt_ strref s, _Out_writes_bytes_opt_(sz) uint8* _Nullable buf, uint32 sz);

/// Encodes binary data as a hexadecimal string
///
/// Converts arbitrary binary data into a hex string of exactly two characters per input
/// byte, with no separators or prefix.
///
/// Any existing string in the output parameter is destroyed first.
///
/// @param out Pointer to output string variable
/// @param buf Binary data to encode
/// @param sz Size of binary data in bytes
/// @param upper Use uppercase hex digits (A-F) if true, lowercase (a-f) if false
/// @return true on success, false on error
///
/// Example:
/// @code
///   string encoded = 0;
///   strHexEncode(&encoded, hash, hashSize, false);
///   // ... use encoded ...
///   strDestroy(&encoded);
/// @endcode
bool strHexEncode(_Inout_ strhandle out, _In_reads_bytes_(sz) const uint8* _Nonnull buf, uint32 sz,
                  bool upper);

/// Decodes a hexadecimal string to binary data
///
/// Converts a hex string back to binary data. Both uppercase and lowercase digits are
/// accepted, and may be mixed. The input must contain nothing but hex digits and must
/// have an even length; anything else is rejected by returning 0.
///
/// This function can be called twice: first with buf=NULL to query the required
/// buffer size, then with an allocated buffer to perform the decoding.
///
/// @param s Hex encoded string
/// @param buf Output buffer for binary data (NULL to query size)
/// @param sz Size of output buffer in bytes
/// @return Number of bytes required for (or written to) the decoded data, or 0 on error
///
/// Example:
/// @code
///   uint32 sz = strHexDecode(encoded, NULL, 0);   // Query size
///   uint8 *data = xaAlloc(sz);
///   strHexDecode(encoded, data, sz);              // Decode
///   // ... use data ...
///   xaFree(data);
/// @endcode
uint32 strHexDecode(_In_opt_ strref s, _Out_writes_bytes_opt_(sz) uint8* _Nullable buf, uint32 sz);

/// @}  // end of string_encoding group

CX_C_END
