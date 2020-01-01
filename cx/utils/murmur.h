#pragma once

#include <cx/cx.h>

_EXTERN_C_BEGIN

uint32 hashMurmur3(const uint8* key, size_t len);
uint32 hashMurmur3i(const uint8* key, size_t len);
uint32 hashMurmur3Str(string s);
uint32 hashMurmur3Stri(string s);

_EXTERN_C_END
