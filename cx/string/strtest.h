#pragma once

#include <cx/string/strbase.h>

CX_C_BEGIN

// These functions are hooks for the test suite and should not be used in production code!
int strTestRefCount(_In_opt_ strref s);
int strTestRopeDepth(_In_opt_ strref s);
bool strTestRopeNode(_Inout_ strhandle o, _In_opt_ strref s, bool left);

CX_C_END
