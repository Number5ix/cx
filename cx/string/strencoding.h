#pragma once

#include <cx/string/strbase.h>

// strings themselves are either UTF-8, ASCII, or unspecified...
// but they can be converted to/from other encodings

CX_C_BEGIN

bool strValidUTF8(strref s);
bool strValidASCII(strref s);

// Returns the number of code points required for the buffer, or 0 on error
// UTF-16 encoding will include the terminating null in the buffer size returned
size_t strToUTF16(strref s, uint16 *buf, size_t wsz);

// Variant of strToUTF16 that allocates a buffer. Must be freed with xaFree!
uint16 *strToUTF16A(strref s);

// Variant of strToUTF16 that returns a scratch buffer, for convenience in calling
// OS APIs that accept UTF-16 strings. See utils/scratch.h for details about the
// caveats of using scratch buffers.
uint16 *strToUTF16S(strref s);

// UTF-16 decoding however, should NOT include the null terminator in wsz.
// Just pass the output from cstrLenw as-is.
bool strFromUTF16(string *o, const uint16 *buf, size_t wsz);

// Binary data to/from base64 encoded strings
bool strB64Encode(string *out, const uint8 *buf, size_t sz, bool urlsafe);
// Returns the number of bytes required for the buffer, or 0 on error
size_t strB64Decode(string *s, uint8 *buf, size_t sz);
CX_C_END
