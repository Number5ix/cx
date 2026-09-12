#include "cx/sys/env.h"
#include "cx/debug/error.h"
#include "cx/platform/win.h"
#include "cx/string.h"

#include <wchar.h>

// Entries in the environment block are "name=value", so a name containing '=' could not be read
// back, and an empty one has nothing to look up.
static bool envNameValid(strref name)
{
    if (strLen(name) == 0 || strFind(name, 0, _SL("=")) != -1) {
        cxerr = CX_InvalidArgument;
        return false;
    }
    return true;
}

_Use_decl_annotations_
bool envGet(string* out, strref name)
{
    strClear(out);

    if (!envNameValid(name))
        return false;

    wchar_t* wname = strToUTF16S(name);
    wchar_t stackbuf[256];

    // A variable that exists but holds an empty string reports a length of 0 -- the same value
    // that means "no such variable". GetLastError is the only thing separating the two, and
    // Win32 does not clear it on success, so a stale error from an earlier call would otherwise
    // read as "not found" here.
    SetLastError(ERROR_SUCCESS);

    // With a buffer, this returns the length written; when the buffer is too small it returns
    // the size needed instead, counting the NUL.
    DWORD got = GetEnvironmentVariableW(wname, stackbuf, ARRAYSIZE(stackbuf));
    if (got == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            cxerr = CX_FileNotFound;
            return false;
        }

        // Set, but empty. The output was cleared on entry, which is already the right value.
        return true;
    }

    if (got < ARRAYSIZE(stackbuf)) {
        strFromUTF16(out, stackbuf, got);
        return true;
    }

    wchar_t* buf = xaAlloc(got * sizeof(wchar_t));
    DWORD len    = GetEnvironmentVariableW(wname, buf, got);
    bool ret     = false;

    if (len > 0 && len < got) {
        strFromUTF16(out, buf, len);
        ret = true;
    } else {
        winMapLastError();
    }

    xaFree(buf);
    return ret;
}

_Use_decl_annotations_
bool envSet(strref name, strref val)
{
    if (!envNameValid(name))
        return false;


    wchar_t* wname = strToUTF16S(name);
    wchar_t* wval  = strLen(val) > 0 ? strToUTF16S(val) : NULL;

    // A NULL value deletes the variable, so an empty value has to be spelled as an empty string.
    // Keeping those apart is why envUnset is a separate call.
    bool ret = !!SetEnvironmentVariableW(wname, wval ? wval : L"");
    if (!ret)
        winMapLastError();

    return ret;
}

_Use_decl_annotations_
bool envUnset(strref name)
{
    if (!envNameValid(name))
        return false;

    bool ret = !!SetEnvironmentVariableW(strToUTF16S(name), NULL);

    // Removing a variable that was never set is not a failure, which is how Unix behaves too.
    if (!ret && GetLastError() == ERROR_ENVVAR_NOT_FOUND)
        ret = true;
    else if (!ret)
        winMapLastError();

    return ret;
}

_Use_decl_annotations_
bool envEnum(hashtable* out)
{
    // Windows matches variable names without regard to case, so the table does too. A
    // case-sensitive table would let PATH and Path both into a merged child environment.
    htInit(out, string, string, 32, HT_CaseInsensitive);

    wchar_t* block = GetEnvironmentStringsW();
    if (!block)
        return winMapLastError();

    string name = 0, val = 0;

    for (wchar_t* e = block; *e; e += cstrLenw(e) + 1) {
        // Windows keeps per-drive current directories in here as "=C:=C:\dir", where the leading
        // '=' belongs to the name, so the search for the separator starts one character in.
        wchar_t* eq = wcschr(e + 1, L'=');
        if (!eq)
            continue;

        strFromUTF16(&name, e, (size_t)(eq - e));
        strFromUTF16(&val, eq + 1, cstrLenw(eq + 1));
        htInsert(out, string, name, string, val);
    }

    strDestroy(&name);
    strDestroy(&val);
    FreeEnvironmentStringsW(block);
    return true;
}
