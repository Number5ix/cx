#include "dynlib_private.h"

#include <cx/debug/error.h>
#include <cx/fs/fs.h>
#include <cx/fs/path.h>
#include <cx/log.h>
#include <cx/string.h>
#include <cx/thread/tlscleanup.h>
#include <cx/utils/lazyinit.h>

#if defined(_PLATFORM_WIN)
STR_CONST(kDynlibPrefix, "");
STR_CONST(kDynlibSuffix, ".dll");
#else
STR_CONST(kDynlibPrefix, "lib");
STR_CONST(kDynlibSuffix, ".so");
#endif

static LogChannel* dynlibLogChan;
static LazyInitState dynlibLogInitState;

static void dynlibLogInit(void* unused)
{
    dynlibLogChan = logChan(_SL("cx/sys/dynlib"));
}

static _Thread_local string dynlibLastErr;
static _Thread_local bool dynlibLastErrRegistered;

static void dynlibLastErrCleanup(void* unused)
{
    strDestroy(&dynlibLastErr);
    dynlibLastErrRegistered = false;
}

// Records a failure for dynlibLastError() and logs it. cxerr must already be set; the log call
// is made after copying it, since logging may overwrite it.
static void dynlibFail(strref what, strref name, strref msg)
{
    int err = cxerr;

    if (!dynlibLastErrRegistered) {
        thrRegisterCleanup(dynlibLastErrCleanup, NULL);
        dynlibLastErrRegistered = true;
    }
    strDup(&dynlibLastErr, msg);

    lazyInit(&dynlibLogInitState, dynlibLogInit, NULL);
    logFmtC(Diag, dynlibLogChan, _SL("${string} '${string}' failed: ${string} (${string})"),
            stvar(strref, what), stvar(strref, name), stvar(strref, msg),
            stvar(strref, (string)cxErrMsg(err)));

    cxerr = err;
}

// Loads a library from an explicit cx path.
static void* dynlibOpenFile(strref path, flags_t flags)
{
    string abspath = 0, native = 0, errmsg = 0;
    void* handle   = NULL;

    pathMakeAbsolute(&abspath, path);
    pathToPlatform(&native, abspath);

    // Checking first is what lets a missing file be told apart from a file that exists but will
    // not load. The OS loaders report both the same way when a dependency is what is missing.
    FSPathStat st = fsStat(abspath, NULL);
    if (st == FS_Nonexistent) {
        cxerr = CX_FileNotFound;
        strConcat(&errmsg, native, _SL(": file not found"));
    } else if (st == FS_Directory) {
        cxerr = CX_IsDirectory;
        strConcat(&errmsg, native, _SL(": is a directory"));
    } else {
        handle = _dynlibPlatformOpen(native, false, flags, &errmsg);
        if (!handle && (cxerr == CX_FileNotFound || cxerr == CX_Unspecified))
            cxerr = CX_InvalidImage;
    }

    if (!handle)
        dynlibFail(_SL("Loading"), native, errmsg);

    strDestroy(&abspath);
    strDestroy(&native);
    strDestroy(&errmsg);
    return handle;
}

_Use_decl_annotations_
DynLib* dynlibOpen(strref path, flags_t flags)
{
    if (strEmpty(path)) {
        cxerr = CX_InvalidArgument;
        dynlibFail(_SL("Loading"), path, _SL("no library name given"));
        return NULL;
    }

#if defined(_PLATFORM_WASM)
    cxerr = CX_NotSupported;
    dynlibFail(_SL("Loading"), path, _SL("shared libraries are not supported on this platform"));
    return NULL;
#endif

    void* handle = NULL;
    string fname = 0;

    // pathFilename() reports whether there was anything besides the filename. A namespace alone
    // counts, so "c:foo.dll" is treated as a path.
    bool hasdir = pathFilename(&fname, path) || !strEq(fname, path);

    if (hasdir || (flags & DYNLIB_NoSearch)) {
        handle = dynlibOpenFile(path, flags);
    } else {
        bool tried = false;

        if (flags & DYNLIB_ExeDirFirst) {
            string exepath = 0;
            fsExeDir(&exepath);
            pathJoin(&exepath, exepath, path);

            // Only a file that exists here counts. Anything else moves on to the normal search,
            // but a file that is here and fails to load is reported rather than skipped.
            if (fsStat(exepath, NULL) == FS_File) {
                handle = dynlibOpenFile(exepath, flags);
                tried  = true;
            }
            strDestroy(&exepath);
        }

        if (!tried) {
            string errmsg = 0;
            handle = _dynlibPlatformOpen(path, true, flags, &errmsg);
            if (!handle) {
                if (cxerr == CX_Unspecified)
                    cxerr = CX_FileNotFound;
                dynlibFail(_SL("Loading"), path, errmsg);
            }
            strDestroy(&errmsg);
        }
    }

    strDestroy(&fname);
    return handle ? _dynlibobjCreate(handle) : NULL;
}

_Use_decl_annotations_
DynLib* dynlibSelf(void)
{
    string errmsg = 0;
    void* handle  = _dynlibPlatformSelf(&errmsg);
    if (!handle)
        dynlibFail(_SL("Opening"), _SL("the executable"), errmsg);

    strDestroy(&errmsg);
    return handle ? _dynlibobjCreate(handle) : NULL;
}

static void* dynlibLookup(DynLib* lib, strref name)
{
    if (!lib || strEmpty(name)) {
        cxerr = CX_InvalidArgument;
        dynlibFail(_SL("Looking up"), name, _SL("no library or symbol name given"));
        return NULL;
    }

    string errmsg = 0;
    void* sym     = _dynlibPlatformSymbol(lib->handle, name, &errmsg);
    if (!sym) {
        cxerr = CX_SymbolNotFound;
        dynlibFail(_SL("Looking up"), name, errmsg);
    }

    strDestroy(&errmsg);
    return sym;
}

_Use_decl_annotations_
void* dynlibSymbol(DynLib* lib, strref name)
{
    return dynlibLookup(lib, name);
}

_Use_decl_annotations_
DynLibFunc dynlibSymbolFn(DynLib* lib, strref name)
{
    // ISO C has no conversion between object and function pointers, but every platform cx
    // supports represents both the same way, which dlsym() itself relies on.
    return (DynLibFunc)(uintptr)dynlibLookup(lib, name);
}

_Use_decl_annotations_
bool dynlibPath(string* out, DynLib* lib)
{
    if (!lib) {
        cxerr = CX_InvalidArgument;
        return false;
    }

    if (!_dynlibPlatformPath(out, lib->handle))
        return false;

    pathFromPlatform(out, *out);
    pathNormalize(out);
    return true;
}

_Use_decl_annotations_
void dynlibFilename(string* out, strref basename, flags_t flags)
{
    string dir = 0, fname = 0;

    if (pathFilename(&fname, basename))
        pathParent(&dir, basename);

    if (!(flags & DYNLIB_NoPrefix))
        strPrepend(kDynlibPrefix, &fname);
    strAppend(&fname, kDynlibSuffix);

    if (strEmpty(dir))
        strDup(out, fname);
    else
        pathJoin(out, dir, fname);

    strDestroy(&dir);
    strDestroy(&fname);
}

_Use_decl_annotations_
void dynlibLastError(string* out)
{
    strDup(out, dynlibLastErr);
}
