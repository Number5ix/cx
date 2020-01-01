#pragma once

#include <cx/string/strbase.h>

_EXTERN_C_BEGIN

// Compare two strings
// Slightly faster than strCmp you only care about equality.
// Returns: true if the strings are equal, otherwise false
bool strEq(string s1, string s2);
bool strEqi(string s1, string s2);

// Compare two strings
// This is purely a binary comparison and is not intended for lexical sorting.
// Returns: < 0 if s1 comes before s2
//            0 if the strings are equal
//          > 0 if s1 comes after s2
int32 strCmp(string s1, string s2);
int32 strCmpi(string s1, string s2);

bool strRangeEq(string str, string sub, int32 off, uint32 len);
bool strRangeEqi(string str, string sub, int32 off, uint32 len);
int32 strRangeCmp(string str, string sub, int32 off, uint32 len);
int32 strRangeCmpi(string str, string sub, int32 off, uint32 len);

bool strBeginsWith(string str, string sub);
bool strBeginsWithi(string str, string sub);
bool strEndsWith(string str, string sub);
bool strEndsWithi(string str, string sub);

_EXTERN_C_END
