#include "cbhandle.h"
#include "cx/container.h"
#include "cx/utils/lazyinit.h"

typedef struct CallbackHandle {
    void *func;
    char *type;
} CallbackHandle;
saDeclare(CallbackHandle);

static LazyInitState cbinit;
static sa_CallbackHandle handles;
static hashtable handleidx;

static void callbackInit(void *data)
{
    CallbackHandle nhandle = { 0 };

    saInit(&handles, opaque(CallbackHandle), 0, 0);
    saPush(&handles, opaque, nhandle, 0);       // so that 0 is not a valid handle
    htInit(&handleidx, ptr, int32, 16, 0);
}

int _callbackGetHandle(const char *cbtype, GenericCallback func)
{
    lazyInit(&cbinit, callbackInit, 0);

    if (!func)
        return 0;

    int idx = 0;
    if (htFind(handleidx, ptr, func, int32, &idx, 0))
        return idx;

    CallbackHandle nhandle = { 0 };
    nhandle.func = func;
    nhandle.type = cstrDup(cbtype);
    saPush(&handles, opaque, nhandle, 0);
    htInsert(&handleidx, ptr, func, int32, saSize(handles) - 1, 0);
    return saSize(handles) - 1;
}

GenericCallback _callbackGetFunc(const char *cbtype, int handle)
{
    lazyInit(&cbinit, callbackInit, 0);

    if (handle < 1 || handle >= saSize(handles))
        return NULL;

    if (strcmp(cbtype, handles.a[handle].type) != 0)
        return NULL;

    return handles.a[handle].func;
}
