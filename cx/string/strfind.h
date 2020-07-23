#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

// Finds first occurrence of find in s at or after start
int32 strFind(strref s, int32 start, strref find);
// Finds last occurrence of find in s before end
// end can be 0 to indicate end of the string
int32 strFindR(strref s, int32 end, strref find);

CX_C_END
