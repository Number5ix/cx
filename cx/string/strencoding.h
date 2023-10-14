#pragma once

#include <cx/string/strbase.h>

// strings themselves are either UTF-8, ASCII, or unspecified...
// but they can be converted to/from other encodings

CX_C_BEGIN

bool strValidUTF8(_In_opt_ strref s);
bool strValidASCII(_In_opt_ strref s);

// Returns the number of code points required for the buffer, or 0 on error
// UTF-16 encoding will include the terminating null in the buffer size returned
size_t strToUTF16(_In_opt_ strref s, _Out_writes_opt_(wsz) uint16 *buf, size_t wsz);

// Variant of strToUTF16 that allocates a buffer. Must be freed with xaFree!
uint16 *strToUTF16A(_In_opt_ strref s);

// Variant of strToUTF16 that returns a scratch buffer, for convenience in calling
// OS APIs that accept UTF-16 strings. See utils/scratch.h for details about the
// caveats of using scratch buffers.
uint16 *strToUTF16S(_In_opt_ strref s);

// UTF-16 decoding however, should NOT include the null terminator in wsz.
// Just pass the output from cstrLenw as-is.
bool strFromUTF16(_Inout_ string *o, _In_reads_(wsz) const uint16 *buf, size_t wsz);

// Binary data to/from base64 encoded strings
bool strB64Encode(_Inout_ string *out, _In_reads_bytes_(sz) const uint8 *buf, uint32 sz, bool urlsafe);
// Returns the number of bytes required for the buffer, or 0 on error
uint32 strB64Decode(_In_opt_ strref s, _Out_writes_bytes_opt_(sz) uint8 *buf, uint32 sz);
CX_C_END
