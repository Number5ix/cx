#include "crash_private.h"

#include "cx/thread/mutex.h"
#include "cx/container/sarray.h"
#include "cx/debug/blackbox.h"

// most of the interesting stuff is in the platform-specific crash module

Mutex *_dbgCrashMutex;

LazyInitState _dbgCrashInitState;
atomic_uint32_t _dbgCrashMode;
static dbgCrashCallback *callbacks;
CrashExtraMeta *_dbgCrashExtraMeta;

void dbgCrashSetMode(uint32 mode)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    atomic_store_uint32(&_dbgCrashMode, mode, ATOMIC_SEQ_CST);
}

uint32 dbgCrashGetMode()
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    return atomic_load_uint32(&_dbgCrashMode, ATOMIC_SEQ_CST);
}

void dbgCrashAddCallback(dbgCrashCallback cb)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    mutexAcquire(_dbgCrashMutex);
    if (!callbacks)
        callbacks = saCreate(ptr, 0, 0);

    saPush(&callbacks, ptr, cb, SA_Unique);
    mutexRelease(_dbgCrashMutex);
}

void dbgCrashRemoveCallback(dbgCrashCallback cb)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    mutexAcquire(_dbgCrashMutex);
    saFind(&cb, ptr, cb, SA_Destroy);
    mutexRelease(_dbgCrashMutex);
}

bool _dbgCrashTriggerCallbacks(bool after)
{
    // caller should be holding mutex
    int i;
    for (i = 0; i < saSize(&callbacks); i++) {
        if (!callbacks[i](after))
            return false;
    }

    return true;
}

// TODO: Hashtable instead?
static intptr extraMetaCmp(stype st, const void *a, const void *b, uint32 flags)
{
    CrashExtraMeta *m1 = (CrashExtraMeta*)a;
    CrashExtraMeta *m2 = (CrashExtraMeta*)b;

    return cstrCmpi(m1->name, m2->name);
}

static void extraMetaDtor(stype st, void *ptr, uint32 flags)
{
    CrashExtraMeta *m = (CrashExtraMeta*)ptr;
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
    char *valcopy = xaAlloc(len + 1, 0);

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
            valcopy = xaResize(valcopy, len + 1, 0);
            valcopy[ci++] = '\\';
            valcopy[ci++] = extra;
        }
    }
    valcopy[ci++] = 0;

    mutexAcquire(_dbgCrashMutex);

    nmeta.name = name;
    nmeta.str = valcopy;
    nmeta.version = version;
    saPush(&_dbgCrashExtraMeta, opaque, nmeta, SA_Unique);
    mutexRelease(_dbgCrashMutex);
}

static void _dbgCrashAddMetaInt(const char *name, int val, bool version)
{
    lazyInit(&_dbgCrashInitState, _dbgCrashInit, 0);
    mutexAcquire(_dbgCrashMutex);

    CrashExtraMeta nmeta = { 0 };
    nmeta.name = name;
    nmeta.val = val;
    nmeta.version = version;
    saPush(&_dbgCrashExtraMeta, opaque, nmeta, SA_Unique);
    mutexRelease(_dbgCrashMutex);
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
    _dbgCrashMutex = mutexCreate();
    _dbgCrashExtraMeta = saCreate(custom(opaque(CrashExtraMeta), extraMetaOps), 1, SA_Sorted);
    bboxInit();

    _dbgCrashPlatformInit();
}
