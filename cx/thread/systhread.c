#include "thread.h"
#include "event.h"
#include "mutex.h"
#include <cx/utils/lazyinit.h>
#include <cx/container/sarray.h>
#include <cx/container/foreach.h>

typedef struct SysThread {
    Thread *thr;
    Event *notify;
} SysThread;

static Mutex systhreadLock;
static SysThread* systhreads;
static LazyInitState systhreadInitState;

static void SysThread_destroy(stype st, stgeneric *gen, uint32 flags)
{
    SysThread *sthr = gen->st_opaque;
    thrDestroy(&sthr->thr);
}

static STypeOps SysThread_ops = {
    .dtor = SysThread_destroy
};

static void systhreadAtExit(void)
{
    mutexAcquire(&systhreadLock);
    for (int idx = 0, idxmax = saSize(&systhreads); idx < idxmax; idx++) {
        thrRequestExit(systhreads[idx].thr);
        eventSignalLock(systhreads[idx].notify);
    }

    // this will call SysThread_destroy, and destroying threads with thrDestroy
    // waits for them to exit (up to 30 sec)
    saDestroy(&systhreads);

    mutexDestroy(&systhreadLock);
}

static void systhreadInit(void *unused)
{
    mutexInit(&systhreadLock);
    systhreads = saCreate(custom(opaque(SysThread), SysThread_ops), 8);
    atexit(systhreadAtExit);
}

void thrRegisterSysThread(Thread *thread, Event *notify)
{
    lazyInit(&systhreadInitState, systhreadInit, NULL);
    mutexAcquire(&systhreadLock);
    SysThread st = {
        .thr = thread,
        .notify = notify
    };
    saPush(&systhreads, opaque, st);
    mutexRelease(&systhreadLock);
}
