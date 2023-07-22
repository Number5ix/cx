#pragma once

// sortable unique id
// loosely based on Alizain Feerasta's ulid
// https://github.com/ulid/

#include <cx/platform/base.h>
#include <cx/core/cpp.h>
#include <cx/core/stype.h>

CX_C_BEGIN

typedef struct SUID {
    uint64 high;
    uint64 low;
} SUID;

_meta_inline bool suidEq(const SUID *a, const SUID *b)
{
    return (a->high == b->high) && (a->low == b->low);
}

_meta_inline int suidCmp(const SUID *a, const SUID *b)
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

// Generate a unique ID (SUID)
// idtype is an application-specific identifier
bool suidGen(SUID *out, uint8 idtype);

// Generate a unique ID (SUID)
// use a random host ID
bool suidGenPrivate(SUID *out, uint8 idtype);

// Encodes a SUID into string form
bool suidEncode(string *out, const SUID *id);
bool suidEncodeBytes(uint8 buf[26], const SUID *id);

// Decodes a SUID from a string
bool suidDecode(SUID *out, strref str);
bool suidDecodeBytes(SUID *out, const char buf[26]);

CX_C_END
