#include "thread.h"
#include "event.h"
#include "mutex.h"
#include <cx/utils/lazyinit.h>
#include <cx/container/sarray.h>
#include <cx/container/foreach.h>

typedef struct SysThread {
    Thread *thr;
    Event notify;
} SysThread;

saDeclarePtr(SysThread);
static Mutex systhreadLock;
static sa_SysThread systhreads;
static LazyInitState systhreadInitState;

static void SysThread_destroy(stype st, stgeneric *gen, uint32 flags)
{
    SysThread *sthr = gen->st_ptr;
    thrDestroy(&sthr->thr);
    eventDestroy(&sthr->notify);
    xaFree(sthr);
}

static STypeOps SysThread_ops = {
    .dtor = SysThread_destroy
};

static void systhreadAtExit(void)
{
    static bool once = true;

    if (!once)
        return;

    once = false;

    mutexAcquire(&systhreadLock);
    for (int idx = 0, idxmax = saSize(systhreads); idx < idxmax; idx++) {
        thrRequestExit(systhreads.a[idx]->thr);
        eventSignalLock(&systhreads.a[idx]->notify);
    }

    // this will call SysThread_destroy, and destroying threads with thrDestroy
    // waits for them to exit (up to 30 sec)
    saDestroy(&systhreads);

    mutexDestroy(&systhreadLock);
}

static void systhreadInit(void *unused)
{
    mutexInit(&systhreadLock);
    saInit(&systhreads, custom(ptr, SysThread_ops), 8);
    atexit(systhreadAtExit);
}

void thrRegisterSysThread(Thread *thread, Event **notify_out)
{
    lazyInit(&systhreadInitState, systhreadInit, NULL);
    mutexAcquire(&systhreadLock);

    SysThread *st = xaAlloc(sizeof(SysThread), XA_Zero);
    st->thr = thread;
    eventInit(&st->notify);
    saPush(&systhreads, ptr, st);

    *notify_out = &st->notify;
    mutexRelease(&systhreadLock);
}
