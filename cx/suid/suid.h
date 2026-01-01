#pragma once

/// @file suid.h
/// @brief Sortable Unique ID generation and manipulation

/// @defgroup suid Sortable Unique ID
/// @{
///
/// SUID (Sortable Unique ID) is a 128-bit identifier loosely based on Alizain Feerasta's ULID.
/// It provides lexicographically sortable unique identifiers with the following structure:
/// - 8 bits: Application-defined ID type
/// - 48 bits: Timestamp (milliseconds since UNIX epoch)
/// - 40 bits: Host ID
/// - 32 bits: Random/monotonic sequence
///
/// SUIDs are designed to be:
/// - **Sortable**: IDs generated later will sort after earlier IDs
/// - **Unique**: Combination of timestamp, host ID, and random data ensures uniqueness
/// - **Compact**: 128 bits encoded as 26-character base32 strings
/// - **Fast**: Efficient generation with thread-local state
///
/// Example usage:
/// @code
///   SUID id;
///   suidGen(&id, 1);  // Generate ID with type 1
///
///   string str = 0;
///   suidEncode(&str, &id);  // Encode to string
///   // str now contains something like: "01an4z07by79ka1307sr74wdk0"
///
///   SUID decoded;
///   if (suidDecode(&decoded, str)) {
///       // Successfully decoded
///   }
///   strDestroy(&str);
/// @endcode
///
/// See https://github.com/ulid/spec for ULID specification details.

#include <cx/platform/base.h>
#include <cx/platform/cpp.h>
#include <cx/stype/stype.h>

CX_C_BEGIN

/// 128-bit sortable unique identifier
typedef struct SUID {
    uint64 high;   ///< High 64 bits: type (8) + timestamp (48) + host ID high (8)
    uint64 low;    ///< Low 64 bits: host ID low (32) + random/sequence (32)
} SUID;

/// bool suidEq(const SUID *a, const SUID *b);
///
/// Compare two SUIDs for equality.
/// @param a First SUID to compare
/// @param b Second SUID to compare
/// @return true if the SUIDs are equal, false otherwise
_meta_inline bool suidEq(_In_ const SUID* a, _In_ const SUID* b)
{
    return (a->high == b->high) && (a->low == b->low);
}

/// int suidCmp(const SUID *a, const SUID *b);
///
/// Compare two SUIDs for sorting order.
/// SUIDs are compared lexicographically, with earlier timestamps sorting first.
/// @param a First SUID to compare
/// @param b Second SUID to compare
/// @return Negative if a < b, 0 if a == b, positive if a > b
_meta_inline int suidCmp(_In_ const SUID* a, _In_ const SUID* b)
{
    // can't just subtract since it might overflow even a signed int64
    if (a->high > b->high)
        return 1;
    if (a->high < b->high)
        return -1;
    if (a->low > b->low)
        return 1;
    if (a->low > b->low)
        return -1;
    return 0;
}

/// Generate a unique SUID using the system's host ID.
///
/// Generates a new SUID with the current timestamp and the machine's host ID.
/// The host ID is derived from hardware identifiers (MAC address, disk serial, etc.)
/// to ensure uniqueness across different machines. Multiple SUIDs generated within
/// the same millisecond will have monotonically increasing sequence numbers.
///
/// @param out Output SUID structure to populate
/// @param idtype Application-specific identifier (stored in high 8 bits)
void suidGen(_Out_ SUID* out, uint8 idtype);

/// Generate a unique SUID using a random host ID.
///
/// Similar to suidGen(), but uses a randomly generated host ID instead of the
/// system's hardware-derived host ID. This is useful for privacy-sensitive
/// applications where you don't want to leak hardware identifiers, or for
/// temporary/ephemeral identifiers.
///
/// @param out Output SUID structure to populate
/// @param idtype Application-specific identifier (stored in high 8 bits)
void suidGenPrivate(_Out_ SUID* out, uint8 idtype);

/// Encode a SUID into a string.
///
/// Encodes the SUID as a 26-character lowercase base32 string using the Crockford
/// base32 alphabet (0-9, a-z excluding i, l, o, u). The encoding is lexicographically
/// sortable - earlier SUIDs will sort before later ones when compared as strings.
///
/// @param out String to receive the encoded result (will be cleared first)
/// @param id SUID to encode
void suidEncode(_Inout_ string* out, _In_ const SUID* id);

/// Encode a SUID into a byte buffer.
///
/// Similar to suidEncode() but writes the 26-character encoded result directly
/// to a byte buffer instead of a string object. The buffer must be at least 26
/// bytes long.
///
/// @param buf Output buffer for 26-byte encoded result
/// @param id SUID to encode
void suidEncodeBytes(_Out_writes_all_(26) uint8 buf[26], _In_ const SUID* id);

#define _suidRetAnno _Success_(return) _Check_return_

/// Decode a SUID from a string.
///
/// Decodes a 26-character base32 encoded string back into a SUID structure.
/// The decoder is case-insensitive and accepts the Crockford base32 alphabet.
/// Ambiguous characters (i/l become 1, o becomes 0) are automatically normalized.
///
/// @param out Output SUID structure to populate
/// @param str String containing encoded SUID (must be at least 26 characters)
/// @return true if decoding succeeded, false if the string is invalid
_suidRetAnno bool suidDecode(_Out_ SUID* out, _In_ strref str);

/// Decode a SUID from a byte buffer.
///
/// Similar to suidDecode() but reads from a 26-byte buffer instead of a string
/// object. The buffer must contain exactly 26 bytes of valid base32 encoded data.
///
/// @param out Output SUID structure to populate
/// @param buf Buffer containing 26 bytes of encoded SUID
/// @return true if decoding succeeded, false if the buffer contains invalid data
_suidRetAnno bool suidDecodeBytes(_Out_ SUID* out, _In_reads_(26) const char buf[26]);

/// @}  // end of suid group

CX_C_END
