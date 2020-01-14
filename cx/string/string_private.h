#pragma once

#include "cx/string.h"
#include "cx/utils/compare.h"
#include "cx/thread/atomic.h"

// Flags field

#define STR_LEN0     0x00       // no length field, 8-bit ref count if STR_ALLOC
#define STR_LEN8     0x01       // 8-bit length, 8-bit ref count if STR_ALLOC
#define STR_LEN16    0x02       // 16-bit length, 16-bit ref count if STR_ALLOC
#define STR_LEN32    0x03       // 32-bit length, 16-bit ref count if STR_ALLOC
#define STR_LEN_MASK 0x03

#define STR_ROPE     0x08       // string is a rope node
#define STR_ALLOC    0x10       // string allocated by us, ref count field present
#define STR_UTF8     0x20       // string is UTF-8
#define STR_ASCII    0x40       // string is 7-bit ASCII
#define STR_CX       0x80       // string contains cx header, otherwise plain C string

#define STR_ENCODING_MASK (STR_UTF8 | STR_ASCII)

#define STR_HDRP(s) ((uint8*)(s))
#define STR_HDR(s) (((*STR_HDRP(s) & STR_CX) && STR_HDRP(s)[1] == 0xc1) ? *STR_HDRP(s) : 0)
#define STR_FIELD(s, off, typ) (*(typ*)(&STR_HDRP(s)[off]))

#define STR_OT_LEN 0
#define STR_OT_REF 1
#define STR_OT_STR 2
extern const uint8 _str_off[32];
// dirty trick here, since STR_ALLOC is defined as 16, it can be used
// as-is as a base index into the table without further masking or shifting
#define STR_OFF_BASE(hdr) ((hdr & STR_ALLOC) | (hdr & STR_LEN_MASK) << 2)
#define STR_OFF_LEN(hdr) (_str_off[STR_OFF_BASE(hdr)])
#define STR_OFF_REF(hdr) (_str_off[STR_OFF_BASE(hdr) | STR_OT_REF])
#define STR_OFF_STR(hdr) (hdr & STR_CX ? (_str_off[STR_OFF_BASE(hdr) | STR_OT_STR]) : 0)

#define STR_LEN8_LEN(s) STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint8)
#define STR_LEN16_LEN(s) STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint16)
#define STR_LEN32_LEN(s) STR_FIELD(s, STR_OFF_LEN(STR_HDR(s)), uint32)

#define STR_LEN8_REF(s) STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), uint8)
#define STR_LEN16_REF(s) STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), uint16)

#define STR_BUFFER(s) (&STR_FIELD(s, STR_OFF_STR(STR_HDR(s)), char))
#define STR_ROPEDATA(s) (&STR_FIELD(s, STR_OFF_STR(STR_HDR(s)), str_ropedata))

#define STR_CHECK_VALID(s) (s && *(uint8*)s)
#define STR_SAFE_DEREF(ps) ((ps && *ps) ? *ps : 0)

_meta_inline uint32 _strFastLen(string s)
{
    if (!(STR_HDR(s) & STR_CX))
        return (uint32)cstrLen((const char*)s);
    switch (STR_HDR(s) & STR_LEN_MASK) {
    case STR_LEN8:
        return STR_LEN8_LEN(s);
    case STR_LEN16:
        return STR_LEN16_LEN(s);
    case STR_LEN32:
        return STR_LEN32_LEN(s);
    default: // STR_LEN0
        return (uint32)cstrLen(STR_BUFFER(s));
    }
}

// must only be used on STR_CX | STR_ALLOC strings!
_meta_inline uint16 _strFastRef(string s)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        return atomic_load_uint8(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint8), ATOMIC_ACQUIRE);
    else
        return atomic_load_uint16(&STR_FIELD(s, STR_OFF_REF(STR_HDR(s)), atomic_uint16), ATOMIC_ACQUIRE);
}

_meta_inline uint16 _strFastRefNoSync(string s)
{
    int l = STR_HDR(s) & STR_LEN_MASK;

    if (l <= STR_LEN8)
        return STR_LEN8_REF(s);
    else
        return STR_LEN16_REF(s);
}

// Always round up allocations to avoid constant realloc
#define STR_ALLOC_SIZE 16

// rope tuning
// try not to create a rope smaller than this
#define ROPE_MIN_SIZE 64
// use a rope if final join size exceeds this
#define ROPE_JOIN_THRESH 128
// use a rope if substring size exceeds this
#define ROPE_SUBSTR_THRESH 96
// maximum size to merge together on joins
#define ROPE_MAX_MERGE 256

#include "string_private_utf8.h"

// dummy empty string used as input when NULL or invalid strings are passed in
extern string _strEmpty;

// resets the string internals, re-use memory if possible
void _strReset(string *s, uint32 minsz);

// these change the structure internally and can result in inconsistent state
// if not used with care
void _strSetLen(string s, uint32 len);
void _strSetRef(string s, uint16 ref);
void _strIncRef(string s);
uint16 _strDecRef(string s);

// ensure that ps is allocated by us and has a single reference
bool _strMakeUnique(string *ps, uint32 minszforcopy);
// like _strMakeUnique but also flattens ropes into plain strings
bool _strFlatten(string *ps, uint32 minszforcopy);
// resize ps in place if possible, or copy if necessary (changing ps)
bool _strResize(string *ps, uint32 len, bool unique);
// duplicates s and returns a copy, optionally with more reserved space allocated
string _strCopy(string s, uint32 minsz);
// direct copy of string buffer or rope internals, does not check destination size!
uint32 _strFastCopy(string s, uint32 off, char *buf, uint32 bytes);

// faster concatenation for internal use
bool _strConcatNoRope(string *o, string s1, string s2);

// rope data comes directly after string header if STR_ROPE is set
typedef struct str_roperef {
    string str;
    uint32 off;
    uint32 len;
} str_roperef;
typedef struct str_ropedata {
    str_roperef left;
    str_roperef right;
    int depth;
} str_ropedata;

string _strCreateRope(string left, uint32 left_off, uint32 left_len, string right, uint32 right_off, uint32 right_len, bool balance);
string _strCreateRope1(string s, uint32 off, uint32 len);
string _strCloneRope(string s);
void _strDestroyRope(string s);
uint32 _strRopeFastCopy(string s, uint32 off, char *buf, uint32 bytes);
bool _strRopeRealStr(string *s, uint32 off, string *rs, uint32 *rsoff, uint32 *rslen, bool writable);

// Finds first occurrence of find in s at or after start
int32 _strFindChar(string s, int32 start, char find);
// Finds last occurrence of find in s before end
// end can be 0 to indicate end of the string
int32 _strFindCharR(string s, int32 end, char find);

_meta_inline char _strFastChar(string s, uint32 i)
{
    if (!(STR_HDR(s) & STR_ROPE)) {
        return STR_BUFFER(s)[i];
    } else {
        string realstr;
        uint32 realoff, reallen;
        if (_strRopeRealStr(&s, i, &realstr, &realoff, &reallen, false))
            return STR_BUFFER(realstr)[realoff];
    }
    return 0;
}

extern char _strnum_udigits[];
extern char _strnum_ldigits[];

// ----- DEBUG STRUCTS -----
// These are not actually used by the code, but are for quick casting in the debugger
// to see string internals
#ifdef _MSC_VER
typedef __declspec(align(1)) struct _str8 {
    uint8 flags;
    uint8 magic_c1;
    uint8 refcount;
    uint8 len;
    char strdata[];
} _str8;
extern _str8 _dummy_str8;
typedef __declspec(align(1)) struct _str16 {
    uint8 flags;
    uint8 magic_c1;
    uint16 refcount;
    uint16 len;
    char strdata[];
} _str16;
extern _str16 _dummy_str16;
typedef __declspec(align(1)) struct _str32 {
    uint8 flags;
    uint8 magic_c1;
    uint16 refcount;
    uint32 len;
    char strdata[];
} _str32;
extern _str32 _dummy_str32;
typedef __declspec(align(1)) struct _rope {
    uint8 flags;
    uint8 magic_c1;
    uint16 refcount;
    uint32 len;
    struct str_ropedata;
} _rope;
extern _rope _dummy_rope;
#endif
