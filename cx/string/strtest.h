#pragma once

#include <cx/string/strbase.h>

// These functions are hooks for the test suite and should not be used in production code!
int strTestRefCount(strref s);
int strTestRopeDepth(strref s);
bool strTestRopeNode(string *o, strref s, bool left);
