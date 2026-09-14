#include "cx/sys/dynlib_private.h"
#include "cx/debug/error.h"
#include "cx/string.h"

STR_CONST(kNotSupported, "shared libraries are not supported on this platform");

_Use_decl_annotations_
void* _dynlibPlatformOpen(strref native, bool search, flags_t flags, string* errmsg)
{
    strDup(errmsg, kNotSupported);
    cxerr = CX_NotSupported;
    return NULL;
}

_Use_decl_annotations_
void _dynlibPlatformClose(void* handle)
{
}

_Use_decl_annotations_
void* _dynlibPlatformSelf(string* errmsg)
{
    strDup(errmsg, kNotSupported);
    cxerr = CX_NotSupported;
    return NULL;
}

_Use_decl_annotations_
void* _dynlibPlatformSymbol(void* handle, strref name, string* errmsg)
{
    strDup(errmsg, kNotSupported);
    return NULL;
}

_Use_decl_annotations_
bool _dynlibPlatformPath(string* out, void* handle)
{
    cxerr = CX_NotSupported;
    return false;
}
