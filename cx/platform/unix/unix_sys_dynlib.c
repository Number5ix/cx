#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif
// dlinfo() is a GNU extension on glibc
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "cx/sys/dynlib_private.h"
#include "cx/debug/error.h"
#include "cx/fs/fs.h"
#include "cx/string.h"

#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <unistd.h>

// dlerror() is per-thread but cleared by reading it, so it has to be captured immediately after
// the call that failed, and cleared immediately before.
static void unixDlError(string* errmsg, strref fallback)
{
    const char* e = dlerror();
    if (e)
        strDup(errmsg, (strref)e);
    else
        strDup(errmsg, fallback);
}

_Use_decl_annotations_
void* _dynlibPlatformOpen(strref native, bool search, flags_t flags, string* errmsg)
{
    int mode = (flags & DYNLIB_Lazy) ? RTLD_LAZY : RTLD_NOW;
    mode |= (flags & DYNLIB_Global) ? RTLD_GLOBAL : RTLD_LOCAL;

    dlerror();
    void* handle = dlopen(strC(native), mode);
    if (handle)
        return handle;

    unixDlError(errmsg, _SL("dlopen failed"));
    cxerr = CX_Unspecified;
    if (!search && access(strC(native), R_OK) != 0 && errno == EACCES)
        cxerr = CX_AccessDenied;

    return NULL;
}

_Use_decl_annotations_
void _dynlibPlatformClose(void* handle)
{
    dlclose(handle);
}

_Use_decl_annotations_
void* _dynlibPlatformSelf(string* errmsg)
{
    dlerror();
    void* handle = dlopen(NULL, RTLD_LAZY);
    if (!handle) {
        unixDlError(errmsg, _SL("dlopen failed"));
        cxerr = CX_Unspecified;
    }
    return handle;
}

_Use_decl_annotations_
void* _dynlibPlatformSymbol(void* handle, strref name, string* errmsg)
{
    dlerror();
    void* sym = dlsym(handle, strC(name));
    if (!sym)
        unixDlError(errmsg, _SL("symbol has a NULL address"));
    return sym;
}

_Use_decl_annotations_
bool _dynlibPlatformPath(string* out, void* handle)
{
    struct link_map* lm = NULL;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || !lm) {
        cxerr = CX_Unspecified;
        return false;
    }

    // The main executable has an empty name in its link map entry.
    if (lm->l_name && lm->l_name[0]) {
        strDup(out, (strref)lm->l_name);
    } else {
        fsExe(out);
        pathToPlatform(out, *out);
    }
    return true;
}
