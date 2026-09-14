#include <cx/cx.h>
#include <cx/debug/error.h>
#include <cx/fs.h>
#include <cx/string.h>
#include <cx/sys.h>
#include <cx/thread.h>

#define TEST_FILE dynlibtest
#define TEST_FUNCS dynlibtest_funcs
#include "common.h"

// Built by tests/CMakeLists.txt next to test_runner. dyntestneeds cannot load from there because
// its dependency is in the dynlibdep subdirectory; dynlibpair holds a copy of both together.
#define kModName     _SL("cxdyntest")
#define kNeedsName   _SL("cxdyntestneeds")
#define kPairDir     _SL("dynlibpair")
#define kBadName     _SL("cx_dynlibtest_bad")
#define kMissingName _SL("cx_dynlibtest_missing")

typedef int (*AddFn)(int a, int b);
typedef int (*IntFn)(void);

#if !defined(_PLATFORM_WASM)

// Absolute path to a library in the test_runner directory, or a subdirectory of it
static void modPath(string* out, strref subdir, strref name)
{
    string fname = 0;
    dynlibFilename(&fname, name, 0);
    fsExeDir(out);
    if (!strEmpty(subdir))
        pathJoin(out, *out, subdir);
    pathJoin(out, *out, fname);
    strDestroy(&fname);
}

static void lastErrorText(string* out)
{
    dynlibLastError(out);
    if (strEmpty(*out))
        strDup(out, _SL("<empty>"));
}

static int test_dynlib_open_path(void)
{
    int ret     = 0;
    string path = 0, got = 0, msg = 0;

    modPath(&path, NULL, kModName);
    DynLib* lib = dynlibOpen(path, 0);
    if (!lib) {
        lastErrorText(&msg);
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}') failed, cxerr ${int}: ${string}"),
                   stvar(string, path), stvar(int32, cxerr), stvar(string, msg));
        goto out;
    }

    if (!dynlibPath(&got, lib)) {
        TEST_FAILV(ret, 1, _SL("dynlibPath failed, cxerr ${int}"), stvar(int32, cxerr));
    } else {
        string a = 0, b = 0;
        pathFilename(&a, got);
        pathFilename(&b, path);
        if (!strEqi(a, b))
            TEST_FAILV(ret, 1, _SL("dynlibPath gave '${string}', wanted a path to '${string}'"),
                       stvar(string, got), stvar(string, path));
        strDestroy(&a);
        strDestroy(&b);
    }

    dynlibRelease(&lib);
    if (lib)
        TEST_FAILV(ret, 1, _SL("dynlibRelease did not clear the handle"), stvNone);

    // Releasing NULL is harmless
    dynlibRelease(&lib);

out:
    strDestroy(&path);
    strDestroy(&got);
    strDestroy(&msg);
    return ret;
}

static int test_dynlib_open_search(void)
{
    int ret      = 0;
    string fname = 0, cwd = 0, exedir = 0, parent = 0, msg = 0;
    DynLib* lib  = NULL;

    dynlibFilename(&fname, kModName, 0);

    lib = dynlibOpen(fname, DYNLIB_ExeDirFirst);
    if (!lib) {
        lastErrorText(&msg);
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}', ExeDirFirst) failed, cxerr ${int}: ${string}"),
                   stvar(string, fname), stvar(int32, cxerr), stvar(string, msg));
    }
    dynlibRelease(&lib);

    // Without ExeDirFirst only the OS search applies. Windows includes the executable's directory
    // in that; Unix does not.
    cxerr = CX_Success;
    lib   = dynlibOpen(fname, 0);
#if defined(_PLATFORM_WIN)
    if (!lib)
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}') did not find it next to the executable, cxerr ${int}"),
                   stvar(string, fname), stvar(int32, cxerr));
    dynlibRelease(&lib);

    // System libraries are found by bare name
    lib = dynlibOpen(_SL("kernel32.dll"), 0);
    if (!lib)
        TEST_FAILV(ret, 1, _SL("dynlibOpen('kernel32.dll') failed, cxerr ${int}"), stvar(int32, cxerr));
#else
    if (lib)
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}') found it without ExeDirFirst"),
                   stvar(string, fname));
    else if (cxerr != CX_FileNotFound)
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}') set cxerr ${int}, wanted CX_FileNotFound"),
                   stvar(string, fname), stvar(int32, cxerr));
#endif
    dynlibRelease(&lib);

    // NoSearch treats a bare name as relative to the current directory
    fsCurDir(&cwd);
    fsExeDir(&exedir);
    pathParent(&parent, exedir);

    fsSetCurDir(exedir);
    lib = dynlibOpen(fname, DYNLIB_NoSearch);
    if (!lib)
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}', NoSearch) failed in '${string}', cxerr ${int}"),
                   stvar(string, fname), stvar(string, exedir), stvar(int32, cxerr));
    dynlibRelease(&lib);

    fsSetCurDir(parent);
    cxerr = CX_Success;
    lib   = dynlibOpen(fname, DYNLIB_NoSearch | DYNLIB_ExeDirFirst);
    if (lib)
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}', NoSearch) searched outside '${string}'"),
                   stvar(string, fname), stvar(string, parent));
    else if (cxerr != CX_FileNotFound)
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}', NoSearch) set cxerr ${int}, wanted CX_FileNotFound"),
                   stvar(string, fname), stvar(int32, cxerr));
    dynlibRelease(&lib);

    fsSetCurDir(cwd);

    strDestroy(&fname);
    strDestroy(&cwd);
    strDestroy(&exedir);
    strDestroy(&parent);
    strDestroy(&msg);
    return ret;
}

static int test_dynlib_symbols(void)
{
    int ret     = 0;
    string path = 0, msg = 0;

    modPath(&path, NULL, kModName);
    DynLib* lib = dynlibOpen(path, 0);
    if (!lib) {
        strDestroy(&path);
        TEST_FAIL(1, _SL("dynlibOpen failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    AddFn add = dynlibFunc(lib, AddFn, _SL("dyntestAdd"));
    if (!add)
        TEST_FAILV(ret, 1, _SL("dynlibFunc(dyntestAdd) failed, cxerr ${int}"), stvar(int32, cxerr));
    else if (add(3, 4) != 7)
        TEST_FAILV(ret, 1, _SL("dyntestAdd(3, 4) == ${int}"), stvar(int32, add(3, 4)));

    int* value = dynlibSymbol(lib, _SL("dyntestValue"));
    if (!value)
        TEST_FAILV(ret, 1, _SL("dynlibSymbol(dyntestValue) failed, cxerr ${int}"), stvar(int32, cxerr));
    else if (*value != 1234)
        TEST_FAILV(ret, 1, _SL("dyntestValue == ${int}, wanted 1234"), stvar(int32, *value));

    cxerr = CX_Success;
    if (dynlibSymbol(lib, _SL("dyntestMissing")))
        TEST_FAILV(ret, 1, _SL("dynlibSymbol found a symbol that does not exist"), stvNone);
    else if (cxerr != CX_SymbolNotFound)
        TEST_FAILV(ret, 1, _SL("missing symbol set cxerr ${int}, wanted CX_SymbolNotFound"),
                   stvar(int32, cxerr));

    dynlibLastError(&msg);
    if (strEmpty(msg))
        TEST_FAILV(ret, 1, _SL("dynlibLastError is empty after a missing symbol"), stvNone);

    cxerr = CX_Success;
    if (dynlibFunc(lib, IntFn, _SL("dyntestMissing")))
        TEST_FAILV(ret, 1, _SL("dynlibFunc found a function that does not exist"), stvNone);
    else if (cxerr != CX_SymbolNotFound)
        TEST_FAILV(ret, 1, _SL("missing function set cxerr ${int}, wanted CX_SymbolNotFound"),
                   stvar(int32, cxerr));

    dynlibRelease(&lib);
    strDestroy(&path);
    strDestroy(&msg);
    return ret;
}

static int test_dynlib_errors(void)
{
    int ret     = 0;
    string path = 0, cwd = 0, fname = 0, msg = 0;
    DynLib* lib = NULL;

    // A path to a file that does not exist
    modPath(&path, NULL, kMissingName);
    cxerr = CX_Success;
    lib   = dynlibOpen(path, 0);
    if (lib || cxerr != CX_FileNotFound)
        TEST_FAILV(ret, 1, _SL("missing path: handle ${ptr}, cxerr ${int}, wanted CX_FileNotFound"),
                   stvar(ptr, lib), stvar(int32, cxerr));
    dynlibRelease(&lib);

    // A bare name the OS search cannot find
    dynlibFilename(&fname, kMissingName, 0);
    cxerr = CX_Success;
    lib   = dynlibOpen(fname, DYNLIB_ExeDirFirst);
    if (lib || cxerr != CX_FileNotFound)
        TEST_FAILV(ret, 1, _SL("missing name: handle ${ptr}, cxerr ${int}, wanted CX_FileNotFound"),
                   stvar(ptr, lib), stvar(int32, cxerr));
    dynlibRelease(&lib);

    dynlibLastError(&msg);
    if (strEmpty(msg))
        TEST_FAILV(ret, 1, _SL("dynlibLastError is empty after a failed open"), stvNone);

    // A directory
    fsExeDir(&path);
    cxerr = CX_Success;
    lib   = dynlibOpen(path, 0);
    if (lib || cxerr != CX_IsDirectory)
        TEST_FAILV(ret, 1, _SL("directory: handle ${ptr}, cxerr ${int}, wanted CX_IsDirectory"),
                   stvar(ptr, lib), stvar(int32, cxerr));
    dynlibRelease(&lib);

    // No name at all
    cxerr = CX_Success;
    lib   = dynlibOpen(NULL, 0);
    if (lib || cxerr != CX_InvalidArgument)
        TEST_FAILV(ret, 1, _SL("empty name: handle ${ptr}, cxerr ${int}, wanted CX_InvalidArgument"),
                   stvar(ptr, lib), stvar(int32, cxerr));
    dynlibRelease(&lib);

    // A text file with a library's name
    fsCurDir(&cwd);
    dynlibFilename(&fname, kBadName, 0);
    pathJoin(&path, cwd, fname);
    FSFile* f = fsOpen(path, FS_Overwrite);
    if (!f) {
        TEST_FAILV(ret, 1, _SL("could not create '${string}'"), stvar(string, path));
    } else {
        size_t wrote = 0;
        fsWriteString(f, _SL("This is not a shared library.\n"), &wrote);
        fileClose(&f);

        cxerr = CX_Success;
        lib   = dynlibOpen(path, 0);
        if (lib || cxerr != CX_InvalidImage) {
            lastErrorText(&msg);
            TEST_FAILV(ret, 1, _SL("text file: handle ${ptr}, cxerr ${int}, wanted CX_InvalidImage: ${string}"),
                       stvar(ptr, lib), stvar(int32, cxerr), stvar(string, msg));
        }
        dynlibRelease(&lib);
        fsDelete(path);
    }

    strDestroy(&path);
    strDestroy(&cwd);
    strDestroy(&fname);
    strDestroy(&msg);
    return ret;
}

static int test_dynlib_dependency(void)
{
    int ret     = 0;
    string path = 0, msg = 0;
    DynLib* lib = NULL;

    // The library exists but its dependency is not where the loader looks
    modPath(&path, NULL, kNeedsName);
    cxerr = CX_Success;
    lib   = dynlibOpen(path, 0);
    if (lib || cxerr != CX_InvalidImage) {
        lastErrorText(&msg);
        TEST_FAILV(ret, 1, _SL("missing dependency: handle ${ptr}, cxerr ${int}, wanted CX_InvalidImage: ${string}"),
                   stvar(ptr, lib), stvar(int32, cxerr), stvar(string, msg));
    }
    dynlibRelease(&lib);

    // With the dependency beside it, it loads from its own directory
    modPath(&path, kPairDir, kNeedsName);
    lib = dynlibOpen(path, 0);
    if (!lib) {
        lastErrorText(&msg);
        TEST_FAILV(ret, 1, _SL("dynlibOpen('${string}') failed, cxerr ${int}: ${string}"),
                   stvar(string, path), stvar(int32, cxerr), stvar(string, msg));
    } else {
        IntFn fn = dynlibFunc(lib, IntFn, _SL("dyntestNeedsValue"));
        if (!fn)
            TEST_FAILV(ret, 1, _SL("dynlibFunc(dyntestNeedsValue) failed, cxerr ${int}"),
                       stvar(int32, cxerr));
        else if (fn() != 43)
            TEST_FAILV(ret, 1, _SL("dyntestNeedsValue() == ${int}, wanted 43"), stvar(int32, fn()));
    }
    dynlibRelease(&lib);

    strDestroy(&path);
    strDestroy(&msg);
    return ret;
}

static int test_dynlib_refcount(void)
{
    int ret     = 0;
    string path = 0;

    modPath(&path, NULL, kModName);
    DynLib* lib1 = dynlibOpen(path, 0);
    DynLib* lib2 = dynlibOpen(path, 0);
    if (!lib1 || !lib2) {
        dynlibRelease(&lib1);
        dynlibRelease(&lib2);
        strDestroy(&path);
        TEST_FAIL(1, _SL("dynlibOpen failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    // Both handles reach the same loaded copy, so they share its state
    IntFn bump1 = dynlibFunc(lib1, IntFn, _SL("dyntestBump"));
    IntFn bump2 = dynlibFunc(lib2, IntFn, _SL("dyntestBump"));
    if (!bump1 || bump1 != bump2)
        TEST_FAILV(ret, 1, _SL("dyntestBump resolved to ${ptr} and ${ptr}"),
                   stvar(ptr, (void*)(uintptr)bump1), stvar(ptr, (void*)(uintptr)bump2));

    if (dynlibSymbol(lib1, _SL("dyntestValue")) != dynlibSymbol(lib2, _SL("dyntestValue")))
        TEST_FAILV(ret, 1, _SL("dyntestValue has a different address through each handle"), stvNone);

    if (bump1 && bump2) {
        int a = bump1();
        int b = bump2();
        if (b != a + 1)
            TEST_FAILV(ret, 1, _SL("counter went ${int} then ${int}; handles do not share state"),
                       stvar(int32, a), stvar(int32, b));
    }

    // The library stays loaded while any handle remains
    dynlibRelease(&lib1);
    AddFn add = dynlibFunc(lib2, AddFn, _SL("dyntestAdd"));
    if (!add || add(20, 22) != 42)
        TEST_FAILV(ret, 1, _SL("library unusable after releasing the other handle"), stvNone);
    dynlibRelease(&lib2);

    strDestroy(&path);
    return ret;
}

#define CONC_THREADS 8
#define CONC_ITERS   200

static string concPath;

static int concThread(Thread* self)
{
    int fails = 0;
    for (int i = 0; i < CONC_ITERS; i++) {
        DynLib* lib = dynlibOpen(concPath, 0);
        AddFn add   = lib ? dynlibFunc(lib, AddFn, _SL("dyntestAdd")) : NULL;
        if (!add || add(i, 1) != i + 1)
            fails++;
        dynlibRelease(&lib);
    }
    return fails;
}

static int test_dynlib_concurrent(void)
{
    int ret = 0;
    Thread* threads[CONC_THREADS];

    modPath(&concPath, NULL, kModName);

    for (int i = 0; i < CONC_THREADS; i++) {
        threads[i] = thrCreate(concThread, _SL("dynlibtest"), stvNone);
        if (!threads[i])
            TEST_FAILV(ret, 1, _SL("thrCreate failed for thread ${int}"), stvar(int32, i));
    }

    for (int i = 0; i < CONC_THREADS; i++) {
        if (!threads[i])
            continue;
        thrWait(threads[i], timeForever);
        if (threads[i]->exitCode != 0)
            TEST_FAILV(ret, 1, _SL("thread ${int} failed ${int} of ${int} open/call/release cycles"),
                       stvar(int32, i), stvar(int32, threads[i]->exitCode), stvar(int32, CONC_ITERS));
        thrRelease(&threads[i]);
    }

    strDestroy(&concPath);
    return ret;
}

static int test_dynlib_self(void)
{
    int ret     = 0;
    string path = 0, exe = 0;

    DynLib* self = dynlibSelf();
    if (!self)
        TEST_FAIL(1, _SL("dynlibSelf failed, cxerr ${int}"), stvar(int32, cxerr));

    fsExe(&exe);
    if (!dynlibPath(&path, self))
        TEST_FAILV(ret, 1, _SL("dynlibPath(self) failed, cxerr ${int}"), stvar(int32, cxerr));
    else if (!strEqi(path, exe))
        TEST_FAILV(ret, 1, _SL("dynlibPath(self) == '${string}', wanted '${string}'"),
                   stvar(string, path), stvar(string, exe));

    cxerr = CX_Success;
    if (dynlibSymbol(self, _SL("cx_dynlibtest_no_such_symbol")) || cxerr != CX_SymbolNotFound)
        TEST_FAILV(ret, 1, _SL("lookup of a missing symbol in the executable set cxerr ${int}"),
                   stvar(int32, cxerr));

    dynlibRelease(&self);
    strDestroy(&path);
    strDestroy(&exe);
    return ret;
}

#else   // _PLATFORM_WASM

static int test_dynlib_unsupported(void)
{
    cxerr       = CX_Success;
    DynLib* lib = dynlibOpen(_SL("libfoo.so"), 0);
    if (lib || cxerr != CX_NotSupported)
        TEST_FAIL(1, _SL("dynlibOpen: handle ${ptr}, cxerr ${int}, wanted CX_NotSupported"),
                  stvar(ptr, lib), stvar(int32, cxerr));
    return 0;
}

#endif

static int checkFilename(strref base, flags_t flags, strref want)
{
    string got = 0;
    dynlibFilename(&got, base, flags);
    bool ok = strEq(got, want);
    if (!ok)
        logFmt(Error, _SL("dynlibFilename('${string}', ${uint}) == '${string}', wanted '${string}'"),
               stvar(strref, base), stvar(uint32, flags), stvar(string, got), stvar(strref, want));
    strDestroy(&got);
    return ok ? 0 : 1;
}

static int test_dynlib_filename(void)
{
    int ret = 0;
#if defined(_PLATFORM_WIN)
    ret |= checkFilename(_SL("render"), 0, _SL("render.dll"));
    ret |= checkFilename(_SL("render"), DYNLIB_NoPrefix, _SL("render.dll"));
    ret |= checkFilename(_SL("plugins/render"), 0, _SL("plugins/render.dll"));
#else
    ret |= checkFilename(_SL("render"), 0, _SL("librender.so"));
    ret |= checkFilename(_SL("render"), DYNLIB_NoPrefix, _SL("render.so"));
    ret |= checkFilename(_SL("plugins/render"), 0, _SL("plugins/librender.so"));
#endif
    return ret;
}

#if !defined(_PLATFORM_WASM)
int test_dynlib_grp_load(void)
{
    TEST_CHAIN(test_dynlib_filename, test_dynlib_open_path, test_dynlib_open_search,
               test_dynlib_symbols, test_dynlib_errors, test_dynlib_dependency);
}

int test_dynlib_grp_handles(void)
{
    TEST_CHAIN(test_dynlib_refcount, test_dynlib_concurrent, test_dynlib_self);
}
#endif

testfunc dynlibtest_funcs[] = {
    { "filename",    test_dynlib_filename    },
#if !defined(_PLATFORM_WASM)
    { "open_path",   test_dynlib_open_path   },
    { "open_search", test_dynlib_open_search },
    { "symbols",     test_dynlib_symbols     },
    { "errors",      test_dynlib_errors      },
    { "dependency",  test_dynlib_dependency  },
    { "refcount",    test_dynlib_refcount    },
    { "concurrent",  test_dynlib_concurrent  },
    { "self",        test_dynlib_self        },
    { "grp_load",    test_dynlib_grp_load    },
    { "grp_handles", test_dynlib_grp_handles },
#else
    { "unsupported", test_dynlib_unsupported },
#endif
    { 0,             0                       }
};
