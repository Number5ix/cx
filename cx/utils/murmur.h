#pragma once

#include <cx/cx.h>

CX_C_BEGIN

uint32 hashMurmur3(const uint8* key, size_t len);
uint32 hashMurmur3i(const uint8* key, size_t len);
uint32 hashMurmur3Str(strref s);
uint32 hashMurmur3Stri(strref s);

CX_C_END
