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
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>

#if defined(_PLATFORM_MACOS)
#include <mach-o/dyld.h>
#else
#include <link.h>
#endif

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

#if defined(_PLATFORM_MACOS)
// dyld searches the current directory for a bare name, between the run path and the fallback
// directories. Repeating its search without that step keeps a library planted in the current
// directory from being loaded, as on the other platforms. "@rpath/" covers DYLD_LIBRARY_PATH and
// the run path; the fallbacks are tried by full path, which also finds OS libraries that exist
// only in the dyld cache.
static void* macSearchOpen(strref name, int mode, string* errmsg)
{
    string path  = 0;
    sa_string dirs;
    saInit(&dirs, string, 4);

    strConcat(&path, _SL("@rpath/"), name);
    dlerror();
    void* handle = dlopen(strC(path), mode);
    if (!handle)
        unixDlError(errmsg, _SL("dlopen failed"));

    const char* env = getenv("DYLD_FALLBACK_LIBRARY_PATH");
    strSplit(&dirs, (env && *env) ? (strref)env : _SL("/usr/local/lib:/usr/lib"), _SL(":"), false);
    for (int32 i = 0; !handle && i < saSize(dirs); i++) {
        strNConcat(&path, dirs.a[i], _SL("/"), name);
        handle = dlopen(strC(path), mode);
    }

    saDestroy(&dirs);
    strDestroy(&path);
    return handle;
}
#endif

_Use_decl_annotations_
void* _dynlibPlatformOpen(strref native, bool search, flags_t flags, string* errmsg)
{
    int mode = (flags & DYNLIB_Lazy) ? RTLD_LAZY : RTLD_NOW;
    mode |= (flags & DYNLIB_Global) ? RTLD_GLOBAL : RTLD_LOCAL;

#if defined(_PLATFORM_MACOS)
    if (search) {
        void* handle = macSearchOpen(native, mode, errmsg);
        if (!handle)
            cxerr = CX_Unspecified;
        return handle;
    }
#endif

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

static void realPathOf(string* out, const char* name)
{
    char real[PATH_MAX];
    strDup(out, (strref)(realpath(name, real) ? real : name));
}

#if defined(_PLATFORM_MACOS)
_Use_decl_annotations_
bool _dynlibPlatformPath(string* out, void* handle)
{
    // There is no reverse lookup from a handle. Opening a loaded image again with RTLD_NOLOAD
    // returns the same handle the original dlopen() did, so match each loaded image that way.
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char* name = _dyld_get_image_name(i);
        if (!name)
            continue;
        void* h = dlopen(name, RTLD_LAZY | RTLD_NOLOAD);
        if (!h)
            continue;
        dlclose(h);   // drops the reference RTLD_NOLOAD just took
        if (h == handle) {
            realPathOf(out, name);
            return true;
        }
    }

    // The executable opened as dlopen(NULL) may not compare equal to the image by name.
    void* self = dlopen(NULL, RTLD_LAZY);
    if (self) {
        dlclose(self);
        if (self == handle) {
            fsExe(out);
            pathToPlatform(out, *out);
            return true;
        }
    }

    cxerr = CX_Unspecified;
    return false;
}
#else
_Use_decl_annotations_
bool _dynlibPlatformPath(string* out, void* handle)
{
    struct link_map* lm = NULL;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &lm) != 0 || !lm) {
        cxerr = CX_Unspecified;
        return false;
    }

    // The name is whatever path the loader was given: possibly relative, or through a symlink.
    // Resolve it, so the answer is the same however the library -- or on FreeBSD, the executable,
    // whose entry is named after the path it was started by -- was reached. glibc leaves the
    // executable's name empty instead.
    if (lm->l_name && lm->l_name[0]) {
        realPathOf(out, lm->l_name);
    } else {
        fsExe(out);
        pathToPlatform(out, *out);
    }
    return true;
}
#endif
