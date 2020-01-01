#pragma once

#include <cx/string/strbase.h>

_EXTERN_C_BEGIN

// Finds first occurrence of find in s at or after start
int32 strFind(string s, int32 start, string find);
// Finds last occurrence of find in s before end
// end can be 0 to indicate end of the string
int32 strFindR(string s, int32 end, string find);

_EXTERN_C_END
