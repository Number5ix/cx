#include "cx/sys/dynlib_private.h"
#include "cx/debug/error.h"
#include "cx/format.h"
#include "cx/fs/fs.h"
#include "cx/platform/win.h"
#include "cx/string.h"
#include "cx/utils/lazyinit.h"

// Older SDKs, including the one XP-compatible builds use, predate these
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x00001000
#endif

#define DYNLIB_ERRMODE (SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)

typedef BOOL(WINAPI* LPFN_STEM)(DWORD, LPDWORD);

static LPFN_STEM fnSetThreadErrorMode;
static bool haveSafeSearch;
static LazyInitState dynlibApiInitState;

static void dynlibApiInit(void* unused)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32)
        return;

    // The LOAD_LIBRARY_SEARCH_* flags arrived together with AddDllDirectory (Windows 8, or
    // KB2533623 on Vista and 7), and checking for that export is the documented way to tell
    // whether they are available. Passing them where they are not fails the load outright.
    haveSafeSearch       = GetProcAddress(kernel32, "AddDllDirectory") != NULL;
    fnSetThreadErrorMode = (LPFN_STEM)GetProcAddress(kernel32, "SetThreadErrorMode");
}

// LoadLibraryExW with the "cannot find a DLL" message box suppressed. Preserves GetLastError().
static HMODULE winLoad(_In_z_ const wchar_t* wpath, DWORD lflags)
{
    DWORD oldmode = 0;

    // The error mode can only be set, not read on its own, so it is set twice to add these bits
    // to whatever the caller had rather than replacing it.
    if (fnSetThreadErrorMode) {
        fnSetThreadErrorMode(DYNLIB_ERRMODE, &oldmode);
        fnSetThreadErrorMode(oldmode | DYNLIB_ERRMODE, NULL);
    } else {
        // XP has only the process-wide mode. A change another thread makes to it during the load
        // is undone by the restore below.
        oldmode = SetErrorMode(DYNLIB_ERRMODE);
        SetErrorMode(oldmode | DYNLIB_ERRMODE);
    }

    HMODULE h = LoadLibraryExW(wpath, NULL, lflags);
    DWORD err = GetLastError();

    if (fnSetThreadErrorMode)
        fnSetThreadErrorMode(oldmode, NULL);
    else
        SetErrorMode(oldmode);

    SetLastError(err);
    return h;
}

static void winErrorText(_Inout_ string* errmsg, DWORD err, _In_ strref name)
{
    wchar_t* buf = NULL;
    string msg   = 0;

    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                   FORMAT_MESSAGE_IGNORE_INSERTS,
                               NULL,
                               err,
                               0,
                               (LPWSTR)&buf,
                               0,
                               NULL);
    if (len > 0 && buf) {
        // System messages end with a line break
        while (len > 0 && (buf[len - 1] == L'\r' || buf[len - 1] == L'\n' || buf[len - 1] == L' '))
            len--;
        strFromUTF16(&msg, (uint16*)buf, len);
        // Some messages expect the filename as an insert, which IGNORE_INSERTS leaves as "%1"
        strReplace(&msg, msg, _SL("%1"), _SL("the file"), 0);
    }
    if (buf)
        LocalFree(buf);

    if (strEmpty(msg))
        strFormat(errmsg, _SL("${string}: error ${uint}"), stvar(strref, name), stvar(uint32, err));
    else
        strFormat(errmsg, _SL("${string}: ${string}"), stvar(strref, name), stvar(string, msg));

    strDestroy(&msg);
}

static void winMapLoadError(DWORD err)
{
    switch (err) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_MOD_NOT_FOUND:
        cxerr = CX_FileNotFound;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        cxerr = CX_AccessDenied;
        break;
    case ERROR_BAD_EXE_FORMAT:
    case ERROR_BAD_FORMAT:
    case ERROR_EXE_MACHINE_TYPE_MISMATCH:
    case ERROR_INVALID_IMAGE_HASH:
    case ERROR_DLL_INIT_FAILED:
    case ERROR_PROC_NOT_FOUND:
        cxerr = CX_InvalidImage;
        break;
    default:
        cxerr = CX_Unspecified;
    }
}

static bool winIsFile(_In_ strref native)
{
    DWORD attr = GetFileAttributesW(strToUTF16S(native));
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Bare-name search for systems without the LOAD_LIBRARY_SEARCH_* flags. A plain LoadLibrary
// there would also search the current directory, so instead this tries, by full path, the two
// directories the safe search order would have used.
static HMODULE winLegacySearch(_In_ strref name, _Out_ DWORD* err, _Out_ bool* existed)
{
    string dirs[2] = { 0 };
    string cand    = 0;
    HMODULE h      = NULL;

    fsExeDir(&dirs[0]);
    pathToPlatform(&dirs[0], dirs[0]);

    wchar_t sysdir[MAX_PATH + 1];
    UINT syslen = GetSystemDirectoryW(sysdir, MAX_PATH + 1);
    if (syslen > 0 && syslen <= MAX_PATH)
        strFromUTF16(&dirs[1], (uint16*)sysdir, syslen);

    *err     = ERROR_MOD_NOT_FOUND;
    *existed = false;

    for (int i = 0; i < 2 && !*existed; i++) {
        if (strEmpty(dirs[i]))
            continue;

        strNConcat(&cand, dirs[i], _SL("\\"), name);
        // A bare name without an extension gets ".dll", as LoadLibrary would add
        if (strFind(name, 0, _SL(".")) < 0)
            strAppend(&cand, _SL(".dll"));

        if (winIsFile(cand)) {
            *existed = true;
            h        = winLoad(strToUTF16S(cand), LOAD_WITH_ALTERED_SEARCH_PATH);
            *err     = h ? ERROR_SUCCESS : GetLastError();
        }
    }

    strDestroy(&dirs[0]);
    strDestroy(&dirs[1]);
    strDestroy(&cand);
    return h;
}

_Use_decl_annotations_
void* _dynlibPlatformOpen(strref native, bool search, flags_t flags, string* errmsg)
{
    lazyInit(&dynlibApiInitState, dynlibApiInit, NULL);

    HMODULE h    = NULL;
    DWORD err    = ERROR_SUCCESS;
    bool existed = !search;

    if (!search) {
        string path = 0;
        strDup(&path, native);

        // LoadLibrary adds ".dll" to a filename with no extension. A trailing dot prevents that,
        // so exactly the file the caller named is loaded.
        if (strFindR(path, strEnd, _SL(".")) <= strFindR(path, strEnd, _SL("\\")))
            strAppend(&path, _SL("."));

        // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR lets the library's own dependencies be found next to
        // it; LOAD_WITH_ALTERED_SEARCH_PATH is the older equivalent.
        DWORD lflags = haveSafeSearch ?
            (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) :
            LOAD_WITH_ALTERED_SEARCH_PATH;
        h   = winLoad(strToUTF16S(path), lflags);
        err = h ? ERROR_SUCCESS : GetLastError();
        strDestroy(&path);
    } else if (haveSafeSearch) {
        h   = winLoad(strToUTF16S(native), LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        err = h ? ERROR_SUCCESS : GetLastError();
    } else {
        h = winLegacySearch(native, &err, &existed);
    }

    if (!h) {
        winErrorText(errmsg, err, native);
        winMapLoadError(err);
        // The library was there, so a missing module must be one of its dependencies
        if (existed && cxerr == CX_FileNotFound)
            cxerr = CX_InvalidImage;
    }

    return h;
}

_Use_decl_annotations_
void _dynlibPlatformClose(void* handle)
{
    FreeLibrary((HMODULE)handle);
}

_Use_decl_annotations_
void* _dynlibPlatformSelf(string* errmsg)
{
    HMODULE h = NULL;

    // Unlike GetModuleHandleW, this adds a reference, so closing the handle later is balanced.
    if (!GetModuleHandleExW(0, NULL, &h)) {
        DWORD err = GetLastError();
        winErrorText(errmsg, err, _SL("executable"));
        cxerr = CX_Unspecified;
        return NULL;
    }

    return h;
}

_Use_decl_annotations_
void* _dynlibPlatformSymbol(void* handle, strref name, string* errmsg)
{
    FARPROC sym = GetProcAddress((HMODULE)handle, strC(name));
    if (!sym)
        winErrorText(errmsg, GetLastError(), name);

    return (void*)(uintptr)sym;
}

_Use_decl_annotations_
bool _dynlibPlatformPath(string* out, void* handle)
{
    DWORD sz     = MAX_PATH;
    wchar_t* buf = NULL;
    DWORD len;

    // There is no way to ask for the length, and a truncated result fills the buffer exactly
    for (;;) {
        xaResize(&buf, sz * sizeof(wchar_t));
        len = GetModuleFileNameW((HMODULE)handle, buf, sz);
        if (len == 0 || len < sz)
            break;
        sz *= 2;
    }

    bool ret = len > 0;
    if (ret)
        strFromUTF16(out, (uint16*)buf, len);
    else
        winMapLastError();

    xaFree(buf);
    return ret;
}
