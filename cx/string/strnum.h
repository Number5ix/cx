#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

// Convert a string to a number
// strict indicates whether the entire string must be a number or if
// it may be followed by non-numeric characters
bool strToInt32(int32 *out, string s, int base, bool strict);
bool strToUInt32(uint32 *out, string s, int base, bool strict);
bool strToInt64(int64 *out, string s, int base, bool strict);
bool strToUInt64(uint64 *out, string s, int base, bool strict);

bool strFromInt32(string *out, int32 i, uint16 base);
bool strFromUInt32(string *out, uint32 i, uint16 base);
bool strFromInt64(string *out, int64 i, uint16 base);
bool strFromUInt64(string *out, uint64 i, uint16 base);

bool strToFloat32(float32 *out, string s, bool strict);
bool strToFloat64(float64 *out, string s, bool strict);

bool strFromFloat32(string *out, float32 f);
bool strFromFloat64(string *out, float64 f);

// digit conversion internals, for format module or similar uses

// must contain enough characters to represent a 64-bit integer in base 2
// with preceding sign and null terminator
#define STRNUM_INTBUF 66
char *_strnum_u64toa(char buf[STRNUM_INTBUF], uint32 *len, uint64 val, uint16 base, uint32 mindigits, char sign, bool upper);

// 18 floating point digits is the maximum representable with 53 bit mantissa
#define STRNUM_FPDIGITS 18
// includes sign, scientific notation, and null terminator
#define STRNUM_FPBUF 25

// higher level interface -- gets actual number
uint32 _strnum_f64toa(float64 d, char dest[STRNUM_FPBUF]);
uint32 _strnum_f32toa(float32 f, char dest[STRNUM_FPBUF]);
// low level interface -- gets digits and exponent only
int32 _strnum_grisu2_32(float32 f, char* digits, int32* K);
int32 _strnum_grisu2_64(float64 d, char* digits, int32* K);

CX_C_END
