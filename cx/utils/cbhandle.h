#pragma once

// Generic callback handles

#include <cx/cx.h>

typedef void(*GenericCallback)();

int _callbackGetHandle(const char *cbtype, GenericCallback func);
GenericCallback _callbackGetFunc(const char *cbtype, int handle);

#define callbackGetHandle(cbtype, func) _callbackGetHandle(#cbtype, (GenericCallback)((cbtype)func))
#define callbackGetFunc(cbtype, handle) ((cbtype)_callbackGetFunc(#cbtype, handle))
