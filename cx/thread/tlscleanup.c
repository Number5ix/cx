#include "tlscleanup.h"
#include "tlscleanup_private.h"
#include "cx/platform/base.h"
#include "cx/container/sarray.h"

typedef struct CleanupEnt {
    TLSCleanupCB cb;
    void* data;
} CleanupEnt;
saDeclare(CleanupEnt);

static _Thread_local sa_CleanupEnt cleanupFuncs;

void thrRegisterCleanup(TLSCleanupCB cb, void* data)
{
    if (!cleanupFuncs.a) {
        saInit(&cleanupFuncs, opaque(CleanupEnt), 4);

        // notify platform that this thread needs its cleanup function called at exit
        _thrPlatformTLSCleanupEnable();
    }

    CleanupEnt ent = { cb, data };
    saPush(&cleanupFuncs, opaque, ent);
}

void _thrTLSCleanup(void)
{
    for (int i = 0; i < saSize(cleanupFuncs); i++) {
        CleanupEnt* ent = &cleanupFuncs.a[i];
        if (ent->cb)
            ent->cb(ent->data);
    }
    saDestroy(&cleanupFuncs);
}