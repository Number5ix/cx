#include "crash_private.h"

#include "cx/thread/mutex.h"
#include "cx/container/sarray.h"
#include "cx/debug/blackbox.h"

// most of the interesting stuff is in the platform-specific crash module

Mutex _dbgCrashMutex;

LazyInitState _dbgCrashInitState;
atomic(uint32) _dbgCrashMode;

saDeclare(dbgCrashCallback);
static sa_dbgCrashCallback callbacks;
sa_CrashExtraMeta _dbgCrashExtraMeta;

void dbgCrashSetMode(uint32 mode)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    atomicStore(uint32, &_dbgCrashMode, mode, SeqCst);
}

uint32 dbgCrashGetMode()
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    return atomicLoad(uint32, &_dbgCrashMode, SeqCst);
}

void dbgCrashAddCallback(dbgCrashCallback cb)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    mutexAcquire(&_dbgCrashMutex);
    if (!callbacks.a)
        saInit(&callbacks, ptr, 0);

    saPush(&callbacks, ptr, cb, Unique);
    mutexRelease(&_dbgCrashMutex);
}

void dbgCrashRemoveCallback(dbgCrashCallback cb)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    mutexAcquire(&_dbgCrashMutex);
    saFind(&callbacks, ptr, cb, Destroy);
    mutexRelease(&_dbgCrashMutex);
}

bool _dbgCrashTriggerCallbacks(bool after)
{
    // caller should be holding mutex
    foreach(sarray, i, dbgCrashCallback, callback, &callbacks) {
        if (!callback(after))
            return false;
    } endforeach;

    return true;
}

// TODO: Hashtable instead?
static intptr extraMetaCmp(stype st, stgeneric a, stgeneric b, uint32 flags)
{
    CrashExtraMeta *m1 = (CrashExtraMeta*)a.st_opaque;
    CrashExtraMeta *m2 = (CrashExtraMeta*)b.st_opaque;

    return cstrCmpi(m1->name, m2->name);
}

static void extraMetaDtor(stype st, stgeneric *gen, uint32 flags)
{
    CrashExtraMeta *m = (CrashExtraMeta*)gen->st_opaque;
    xaSFree(m->str);
}

static STypeOps extraMetaOps = {
    .cmp = extraMetaCmp,
    .dtor = extraMetaDtor
};

static void _dbgCrashAddMetaStr(const char *name, const char *val, bool version)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);

    CrashExtraMeta nmeta = { 0 };
    size_t origlen = cstrLen(val);
    size_t len = origlen, ci = 0;
    char *valcopy = xaAlloc(len + 1);

    // do JSON escaping here so the exception handler doesn't have to deal with it
    for (size_t i = 0; i < origlen; i++) {
        char extra = 0;
        switch (val[i]) {
        case '\"':
            extra = '\"';
            break;
        case '\\':
            extra = '\\';
            break;
        case '\b':
            extra = 'b';
            break;
        case '\f':
            extra = 'f';
            break;
        case '\n':
            extra = 'n';
            break;
        case '\r':
            extra = 'r';
            break;
        case '\t':
            extra = 't';
            break;
        }

        if (!extra) {
            valcopy[ci++] = val[i];
        } else {
            len++;
            valcopy = xaResize(valcopy, len + 1);
            valcopy[ci++] = '\\';
            valcopy[ci++] = extra;
        }
    }
    valcopy[ci++] = 0;

    mutexAcquire(&_dbgCrashMutex);

    nmeta.name = name;
    nmeta.str = valcopy;
    nmeta.version = version;
    saPush(&_dbgCrashExtraMeta, opaque, nmeta, Unique);
    mutexRelease(&_dbgCrashMutex);
}

static void _dbgCrashAddMetaInt(const char *name, int val, bool version)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    mutexAcquire(&_dbgCrashMutex);

    CrashExtraMeta nmeta = { 0 };
    nmeta.name = name;
    nmeta.val = val;
    nmeta.version = version;
    saPush(&_dbgCrashExtraMeta, opaque, nmeta, Unique);
    mutexRelease(&_dbgCrashMutex);
}

void dbgCrashAddMetaStr(const char *name, const char *val)
{
    _dbgCrashAddMetaStr(name, val, false);
}

void dbgCrashAddMetaInt(const char *name, int val)
{
    _dbgCrashAddMetaInt(name, val, false);
}

void dbgCrashAddVersionStr(const char *name, const char *val)
{
    _dbgCrashAddMetaStr(name, val, true);
}

void dbgCrashAddVersionInt(const char *name, int val)
{
    _dbgCrashAddMetaInt(name, val, true);
}

void _dbgCrashInit(void *data)
{
    mutexInit(&_dbgCrashMutex);
    saInit(&_dbgCrashExtraMeta, custom(opaque(CrashExtraMeta), extraMetaOps), 1, Sorted);
    bboxInit();

    _dbgCrashPlatformInit();
}
