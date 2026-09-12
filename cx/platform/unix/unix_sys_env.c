#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/env.h"
#include "cx/debug/error.h"
#include "cx/platform/unix.h"
#include "cx/string.h"
#include "cx/thread/rwlock.h"
#include "cx/utils/lazyinit.h"

#include <stdlib.h>
#include <unistd.h>

// POSIX puts the environment block in a global rather than declaring it in a header.
extern char** environ;

static LazyInitState envInitState;
static RWLock envLock;

static void envLockInit(void* data)
{
    rwlockInit(&envLock);
}

// Entries in the environment block are "name=value", so a name containing '=' could not be
// read back, and an empty one has nothing to look up.
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
    lazyInit(&envInitState, envLockInit, NULL);

    strClear(out);

    if (!envNameValid(name))
        return false;

    bool ret = false;

    withReadLock (&envLock) {
        const char* val = getenv(strC(name));
        if (val) {
            strDup(out, (string)val);
            ret = true;
        }
    }

    if (!ret)
        cxerr = CX_FileNotFound;

    return ret;
}

_Use_decl_annotations_
bool envSet(strref name, strref val)
{
    lazyInit(&envInitState, envLockInit, NULL);

    if (!envNameValid(name))
        return false;

    bool ret = false;

    // strC() hands back the next buffer in a rotating per-thread pool, so the two results stay
    // valid alongside each other, and setenv() copies both before it returns.
    withWriteLock (&envLock) {
        ret = setenv(strC(name), strC(val), 1) == 0;
    }

    if (!ret)
        unixMapErrno();

    return ret;
}

_Use_decl_annotations_
bool envUnset(strref name)
{
    lazyInit(&envInitState, envLockInit, NULL);

    if (!envNameValid(name))
        return false;

    bool ret = false;

    withWriteLock (&envLock) {
        ret = unsetenv(strC(name)) == 0;
    }

    if (!ret)
        unixMapErrno();

    return ret;
}

_Use_decl_annotations_
bool envEnum(hashtable* out)
{
    lazyInit(&envInitState, envLockInit, NULL);

    // Unix compares variable names exactly, so the table does too.
    htInit(out, string, string, 32);

    withReadLock (&envLock) {
        for (char** e = environ; e && *e; e++) {
            // A name cannot contain '=', so the first one separates name from value.
            const char* eq = strchr(*e, '=');
            if (!eq)
                continue;

            string name = 0, val = 0;
            strFromBytes(&name, *e, (uint32)(eq - *e));
            strDup(&val, (string)(eq + 1));
            htInsert(out, string, name, string, val);
            strDestroy(&name);
            strDestroy(&val);
        }
    }

    return true;
}
