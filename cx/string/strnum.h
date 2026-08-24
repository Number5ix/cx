#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

/// @defgroup string_num Numeric Conversion
/// @ingroup string
/// @{
///
/// Functions for converting between strings and numeric values.
/// Both integer and floating-point conversions are supported.
///
/// @section string_num_int Integer parsing
///
/// Integer parsing supports bases 2-36. Unlike standard C library functions, a leading
/// '0' does NOT imply octal (base 8) - base 10 is always the default unless explicitly
/// specified or the string begins with "0x" for hexadecimal.
///
/// @section string_num_flags Parsing flags
///
/// Every string-to-number function takes a `flags` parameter built from STRNUM_FLAGS.
/// Passing 0 is the most permissive setting: leading whitespace is skipped, a sign is
/// accepted, a "0x" prefix is honored, and anything after the number is ignored. Each
/// flag only ever tightens that, so a call site reads as a list of what is *not* allowed:
///
/// @code
///   uint64 len;
///   // no trailing junk, no leading spaces, no "0x" - exactly the digits and nothing else
///   strToUInt64(&len, s, 10, STRNUM_NoTrailing | STRNUM_NoWS | STRNUM_NoPrefix);
/// @endcode
///
/// @section string_num_float Floating-point conversion
///
/// Float-to-string conversion uses the Grisu2 algorithm for efficient and accurate
/// representation. The output format is chosen automatically (plain, decimal, or
/// scientific notation) based on the magnitude of the value.
///
/// @section string_num_error Error handling
///
/// Conversion functions return false on error and set cxerr:
/// - CX_InvalidArgument - Invalid input format or base
/// - CX_Range - Value exceeds the range of the target type

// for keeping annotations consistent and prototypes on a single line
#define _strNumSuccess _Success_(return)

/// Flags controlling how strictly a string is parsed as a number.
///
/// All of these are restrictions, so 0 is the most permissive setting and every flag
/// only ever tightens what is accepted.
enum STRNUM_FLAGS {
    STRNUM_NoTrailing = 0x01,   ///< Reject anything following the number
    STRNUM_NoWS       = 0x02,   ///< Do not skip leading whitespace
    STRNUM_NoPrefix   = 0x04,   ///< Do not accept a "0x" base prefix
    STRNUM_NoSign     = 0x08,   ///< Reject a leading '+' or '-'
};

/// Converts a string to a 32-bit signed integer
///
/// Parses a string as a signed integer in the specified base. By default leading
/// whitespace is skipped, an optional '+' or '-' sign is recognized, a "0x" or "0X"
/// prefix selects hexadecimal, and trailing characters are ignored. Pass STRNUM_FLAGS
/// values to disallow any of those.
///
/// Unlike strtol(), a leading '0' does NOT imply octal - base 10 is the default.
///
/// @param out Pointer to receive the parsed value
/// @param s String to parse
/// @param base Numeric base (2-36), or 0 for auto-detect (10 or 16)
/// @param flags Combination of STRNUM_FLAGS, or 0 to accept everything
/// @return true on success, false on error (invalid format or out of range)
///
/// Example:
/// @code
///   int32 value;
///   strToInt32(&value, _SL("123"), 10, STRNUM_NoTrailing);       // value = 123
///   strToInt32(&value, _SL("-42"), 10, STRNUM_NoTrailing);       // value = -42
///   strToInt32(&value, _SL("0xFF"), 0, STRNUM_NoTrailing);       // value = 255
///   strToInt32(&value, _SL("123abc"), 10, 0);                    // value = 123, ok
///   strToInt32(&value, _SL("123abc"), 10, STRNUM_NoTrailing);    // false
/// @endcode
_strNumSuccess bool strToInt32(_Out_ int32* _Nonnull out, _In_opt_ strref s, int base,
                               flags_t flags);

/// Converts a string to a 32-bit unsigned integer
///
/// Like strToInt32(), but parses as an unsigned value. A leading '-' sign is allowed
/// (unless STRNUM_NoSign is set) and results in two's complement representation of the
/// negative value.
///
/// @param out Pointer to receive the parsed value
/// @param s String to parse
/// @param base Numeric base (2-36), or 0 for auto-detect (10 or 16)
/// @param flags Combination of STRNUM_FLAGS, or 0 to accept everything
/// @return true on success, false on error (invalid format or out of range)
///
/// Example:
/// @code
///   uint32 value;
///   strToUInt32(&value, _SL("255"), 10, STRNUM_NoTrailing);      // value = 255
///   strToUInt32(&value, _SL("0xFF"), 0, STRNUM_NoTrailing);      // value = 255
/// @endcode
_strNumSuccess bool strToUInt32(_Out_ uint32* _Nonnull out, _In_opt_ strref s, int base,
                                flags_t flags);

/// Converts a string to a 64-bit signed integer
///
/// Like strToInt32(), but for 64-bit values.
///
/// @param out Pointer to receive the parsed value
/// @param s String to parse
/// @param base Numeric base (2-36), or 0 for auto-detect (10 or 16)
/// @param flags Combination of STRNUM_FLAGS, or 0 to accept everything
/// @return true on success, false on error (invalid format or out of range)
_strNumSuccess bool strToInt64(_Out_ int64* _Nonnull out, _In_opt_ strref s, int base,
                               flags_t flags);

/// Converts a string to a 64-bit unsigned integer
///
/// Like strToUInt32(), but for 64-bit values.
///
/// @param out Pointer to receive the parsed value
/// @param s String to parse
/// @param base Numeric base (2-36), or 0 for auto-detect (10 or 16)
/// @param flags Combination of STRNUM_FLAGS, or 0 to accept everything
/// @return true on success, false on error (invalid format or out of range)
_strNumSuccess bool strToUInt64(_Out_ uint64* _Nonnull out, _In_opt_ strref s, int base,
                                flags_t flags);

/// Converts a 32-bit signed integer to a string
///
/// Formats the integer in the specified base. Negative values are prefixed with '-'.
/// For bases > 10, lowercase letters are used (a-z).
///
/// @param out Output string (existing content destroyed)
/// @param i Integer value to convert
/// @param base Numeric base (2-36), typically 10 or 16
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strFromInt32(&s, 123, 10);    // "123"
///   strFromInt32(&s, -42, 10);    // "-42"
///   strFromInt32(&s, 255, 16);    // "ff"
///   strDestroy(&s);
/// @endcode
bool strFromInt32(_Inout_ strhandle out, int32 i, uint16 base);

/// Converts a 32-bit unsigned integer to a string
///
/// Formats the integer in the specified base. For bases > 10, lowercase letters
/// are used (a-z).
///
/// @param out Output string (existing content destroyed)
/// @param i Integer value to convert
/// @param base Numeric base (2-36), typically 10 or 16
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strFromUInt32(&s, 255, 10);   // "255"
///   strFromUInt32(&s, 255, 16);   // "ff"
///   strDestroy(&s);
/// @endcode
bool strFromUInt32(_Inout_ strhandle out, uint32 i, uint16 base);

/// Converts a 64-bit signed integer to a string
///
/// Like strFromInt32(), but for 64-bit values.
///
/// @param out Output string (existing content destroyed)
/// @param i Integer value to convert
/// @param base Numeric base (2-36), typically 10 or 16
/// @return true on success, false on error
bool strFromInt64(_Inout_ strhandle out, int64 i, uint16 base);

/// Converts a 64-bit unsigned integer to a string
///
/// Like strFromUInt32(), but for 64-bit values.
///
/// @param out Output string (existing content destroyed)
/// @param i Integer value to convert
/// @param base Numeric base (2-36), typically 10 or 16
/// @return true on success, false on error
bool strFromUInt64(_Inout_ strhandle out, uint64 i, uint16 base);

/// Converts a string to a 32-bit floating-point value
///
/// Parses a string as a single-precision float. Supports standard decimal notation,
/// scientific notation (e.g., "1.23e-4"), and special values ("inf", "nan").
/// Leading whitespace is skipped unless STRNUM_NoWS is set.
///
/// STRNUM_NoPrefix rejects a hexadecimal float such as "0x1p3".
///
/// @param out Pointer to receive the parsed value
/// @param s String to parse
/// @param flags Combination of STRNUM_FLAGS, or 0 to accept everything
/// @return true on success, false on error (invalid format)
///
/// Example:
/// @code
///   float32 value;
///   strToFloat32(&value, _SL("3.14"), STRNUM_NoTrailing);       // value = 3.14
///   strToFloat32(&value, _SL("-1.5e2"), STRNUM_NoTrailing);     // value = -150.0
///   strToFloat32(&value, _SL("inf"), STRNUM_NoTrailing);        // value = infinity
/// @endcode
_strNumSuccess bool strToFloat32(_Out_ float32* _Nonnull out, _In_opt_ strref s, flags_t flags);

/// Converts a string to a 64-bit floating-point value
///
/// Like strToFloat32(), but parses as double-precision. Supports decimal notation,
/// scientific notation, and special values.
///
/// @param out Pointer to receive the parsed value
/// @param s String to parse
/// @param flags Combination of STRNUM_FLAGS, or 0 to accept everything
/// @return true on success, false on error (invalid format)
///
/// Example:
/// @code
///   float64 value;
///   strToFloat64(&value, _SL("3.141592653589793"), STRNUM_NoTrailing);
///   strToFloat64(&value, _SL("2.5e-10"), STRNUM_NoTrailing);
/// @endcode
_strNumSuccess bool strToFloat64(_Out_ float64* _Nonnull out, _In_opt_ strref s, flags_t flags);

/// Converts a 32-bit floating-point value to a string
///
/// Formats the float using the Grisu2 algorithm for efficient and accurate conversion.
/// The output format (plain decimal, scientific notation) is chosen automatically based
/// on the magnitude. Special values are rendered as "inf", "-inf", or "nan".
///
/// @param out Output string (existing content destroyed)
/// @param f Float value to convert
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strFromFloat32(&s, 3.14f);     // "3.14"
///   strFromFloat32(&s, 1.5e10f);   // "1.5e+10"
///   strFromFloat32(&s, 0.0f);      // "0"
///   strDestroy(&s);
/// @endcode
bool strFromFloat32(_Inout_ strhandle out, float32 f);

/// Converts a 64-bit floating-point value to a string
///
/// Like strFromFloat32(), but for double-precision values. Uses Grisu2 algorithm
/// for accurate conversion with automatic format selection.
///
/// @param out Output string (existing content destroyed)
/// @param f Double value to convert
/// @return true on success, false on error
///
/// Example:
/// @code
///   string s = 0;
///   strFromFloat64(&s, 3.141592653589793);  // "3.141592653589793"
///   strFromFloat64(&s, 2.5e-10);            // "2.5e-10"
///   strDestroy(&s);
/// @endcode
bool strFromFloat64(_Inout_ strhandle out, float64 f);

/// @defgroup string_num_internal Internal conversion functions
/// @ingroup string_num
/// @{
///
/// Low-level helpers used by the format module and other internal components.
/// They work with fixed-size buffers and return raw digit arrays.
/// Most code should use the higher-level str* functions above.

/// Buffer size for integer-to-string conversion (64-bit binary + sign + null terminator)
#define STRNUM_INTBUF 66

/// Converts a 64-bit unsigned integer to ASCII digits
///
/// Internal function for integer formatting. Converts the value to the specified base
/// and writes ASCII digits into the provided buffer, returning a pointer to the start
/// of the converted string within the buffer.
///
/// @param buf Fixed-size buffer (STRNUM_INTBUF bytes) to write into
/// @param len Optional pointer to receive the length of the result (excluding null terminator)
/// @param value Value to convert
/// @param base Numeric base (2-36)
/// @param mindigits Minimum number of digits (zero-padded if necessary)
/// @param sign Sign character to prepend (0 for none, '-' or '+' typically)
/// @param upper If true, use uppercase letters for bases > 10; if false, use lowercase
/// @return Pointer into buf where the converted string starts (null-terminated)
_Ret_valid_ uint8* _Nonnull _strnum_u64toa(_Out_writes_(STRNUM_INTBUF) uint8 buf[STRNUM_INTBUF],
                                           _Out_opt_ uint32* _Nullable len, uint64 value,
                                           uint16 base, uint32 mindigits, char sign, bool upper);

/// Maximum meaningful floating-point digits (53-bit mantissa precision)
#define STRNUM_FPDIGITS 18

/// Buffer size for floating-point-to-string conversion (sign + digits + exponent + null)
#define STRNUM_FPBUF 25

/// Converts a 64-bit float to ASCII string
///
/// Internal function using Grisu2 algorithm. Automatically selects format (plain,
/// decimal, or scientific notation) and handles special values.
///
/// @param d Double value to convert
/// @param dest Buffer to write result (STRNUM_FPBUF bytes)
/// @return Length of resulting string (excluding null terminator)
uint32 _strnum_f64toa(float64 d, _Out_writes_(STRNUM_FPBUF) uint8 dest[STRNUM_FPBUF]);

/// Converts a 32-bit float to ASCII string
///
/// Like _strnum_f64toa(), but for single-precision floats.
///
/// @param f Float value to convert
/// @param dest Buffer to write result (STRNUM_FPBUF bytes)
/// @return Length of resulting string (excluding null terminator)
uint32 _strnum_f32toa(float32 f, _Out_writes_(STRNUM_FPBUF) uint8 dest[STRNUM_FPBUF]);

/// Low-level Grisu2 algorithm for 32-bit float
///
/// Extracts decimal digits and exponent for a float. This is the core of the Grisu2
/// algorithm and is used internally by _strnum_f32toa().
///
/// @param f Float value to convert
/// @param digits Buffer to receive decimal digits (no decimal point, 18 bytes)
/// @param K Pointer to exponent value (input/output)
/// @return Number of significant digits written to the digits buffer
int32 _strnum_grisu2_32(float32 f, _Out_writes_(18) uint8* _Nonnull digits,
                        _Inout_ int32* _Nonnull K);

/// Low-level Grisu2 algorithm for 64-bit double
///
/// Like _strnum_grisu2_32(), but for double-precision values.
///
/// @param d Double value to convert
/// @param digits Buffer to receive decimal digits (no decimal point, 18 bytes)
/// @param K Pointer to exponent value (input/output)
/// @return Number of significant digits written to the digits buffer
int32 _strnum_grisu2_64(float64 d, _Out_writes_(18) uint8* _Nonnull digits,
                        _Inout_ int32* _Nonnull K);

/// @}  // end of string_num_internal group

/// @}  // end of string_num group

CX_C_END
