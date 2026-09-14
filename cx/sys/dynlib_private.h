#pragma once

#include <cx/sys/dynlib.h>

CX_C_BEGIN

// Platform layer for dynlib.c. Paths passed in and out here are native paths, not cx paths.

// Loads a library. With `search` false, `native` is an absolute path to a file that the caller
// has already checked exists. With `search` true it is a bare filename for the OS search rules.
// On failure returns NULL, sets cxerr to the best guess the platform can make, and writes the
// OS error text to `errmsg`.
_Ret_opt_valid_ void* _dynlibPlatformOpen(_In_ strref native, bool search, flags_t flags,
                                          _Inout_ string* errmsg);

// Releases one OS reference taken by _dynlibPlatformOpen or _dynlibPlatformSelf.
void _dynlibPlatformClose(_In_ void* handle);

// Takes a reference to the main executable. Same failure contract as _dynlibPlatformOpen.
_Ret_opt_valid_ void* _dynlibPlatformSelf(_Inout_ string* errmsg);

// Looks up an exported symbol. Returns NULL with the OS error text in `errmsg` if absent.
_Ret_opt_valid_ void* _dynlibPlatformSymbol(_In_ void* handle, _In_ strref name,
                                            _Inout_ string* errmsg);

// Native path of a loaded module.
bool _dynlibPlatformPath(_Inout_ string* out, _In_ void* handle);

CX_C_END
