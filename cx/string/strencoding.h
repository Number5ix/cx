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

/// @}  // end of string_encoding group

CX_C_END
