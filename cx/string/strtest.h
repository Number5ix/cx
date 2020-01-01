#pragma once

#include <cx/string/strbase.h>

// These functions are hooks for the test suite and should not be used in production code!
int strTestRefCount(string s);
int strTestRopeDepth(string s);
bool strTestRopeNode(string *o, string s, bool left);
