#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

// Compare two strings
// Slightly faster than strCmp you only care about equality.
// Returns: true if the strings are equal, otherwise false
bool strEq(strref s1, strref s2);
bool strEqi(strref s1, strref s2);

// Compare two strings
// This is purely a binary comparison and is not intended for lexical sorting.
// Returns: < 0 if s1 comes before s2
//            0 if the strings are equal
//          > 0 if s1 comes after s2
int32 strCmp(strref s1, strref s2);
int32 strCmpi(strref s1, strref s2);

bool strRangeEq(strref str, strref sub, int32 off, uint32 len);
bool strRangeEqi(strref str, strref sub, int32 off, uint32 len);
int32 strRangeCmp(strref str, strref sub, int32 off, uint32 len);
int32 strRangeCmpi(strref str, strref sub, int32 off, uint32 len);

bool strBeginsWith(strref str, strref sub);
bool strBeginsWithi(strref str, strref sub);
bool strEndsWith(strref str, strref sub);
bool strEndsWithi(strref str, strref sub);

CX_C_END
