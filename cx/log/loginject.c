// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "logwire_private.h"
#include <cx/string.h>
#include <cx/utils/lazyinit.h>

// Re-injecting a record that was logged somewhere else.
//
// The rule this file follows is that a forwarded record is an ordinary record from the moment it
// arrives: same entry allocation, same fan-out, same retention rings, same per-channel level
// check, same routing. Only where it came from is different, and that is a field. A record that
// does not make it to a destination here was therefore dropped for a reason a locally logged
// record would have been dropped for too, which is the only way a receiver's configuration can
// mean anything.
//
// Nothing here takes _log_op_lock while an entry is in flight, for the same reason logring.c does
// not: injection runs on an application thread that may itself be inside a transport callback, and
// a destination's closefunc runs under that lock.

STR_CONST(kInjectOrigin, "origin");

// One context node per origin, shared by every record that arrives from it.
//
// The origin is a field like any other -- §10.3's point is that the channel path stays exactly
// what the sender logged it to, so a rule written for a subsystem matches wherever the record came
// from, and the machine it came from is something a destination reads rather than something baked
// into the path. Caching the node means a record costs one refcount rather than an allocation and
// a string copy.
static Mutex _loginject_lock;
static hashtable _loginject_origins;   // origin -> LogCtx*
static LazyInitState _loginject_init;

// Distinct origins the cache will hold. A collector fronting a fleet has as many as it has
// senders; past this the node is built per record instead, which is slower but still correct.
#define LOG_INJECT_ORIGIN_MAX 4096

static void logInjectInit(void* unused)
{
    mutexInit(&_loginject_lock);
    htInit(&_loginject_origins, string, ptr, 16);
}

// The shared context node for an origin, with a reference taken for the caller.
static _Ret_opt_valid_ LogCtx* logInjectOriginCtx(_In_opt_ strref origin)
{
    if (strEmpty(origin))
        return NULL;

    lazyInit(&_loginject_init, logInjectInit, NULL);

    LogCtx* ret = NULL;
    withMutex (&_loginject_lock) {
        void* found;
        if (htFind(_loginject_origins, strref, origin, ptr, &found)) {
            ret = logCtxAcquire((LogCtx*)found);
            break;
        }

        const char* key = logWireInternKey(kInjectOrigin);
        if (!key)
            break;

        stvar var = stvarkn(key, strref, origin);
        ret       = logCtxCreate(NULL, 1, &var);

        if (htSize(_loginject_origins) < LOG_INJECT_ORIGIN_MAX)
            htInsert(&_loginject_origins, string, (string)origin, ptr, logCtxAcquire(ret));
    }

    return ret;
}

_Use_decl_annotations_
bool logInject(strref chanpath, const LogWireRecord* rec)
{
    logCheckInit();
    if (!atomicLoad(bool, &_log_running, Acquire) || !rec)
        return false;

    LogChannel* chan = logChanApplyRemote(chanpath, rec->chanflags);
    if (!chan)
        return false;

    // The same gate a local call site passes. Nothing was saved by checking it earlier -- the
    // record already exists, it arrived over a wire -- but a receiver that has asked for Warn and
    // above has to get Warn and above whatever the sender chose to send.
    if (rec->level > atomicLoad(int32, &chan->maxlevel, Relaxed))
        return false;

    LogCtx* octx = logInjectOriginCtx(rec->origin);
    LogCtx* ctx  = octx;
    if (rec->nctx > 0)
        ctx = logCtxCreate(octx, rec->nctx, rec->ctx);

    LogEntry* ent = logEntryCreate(rec->level,
                                   rec->timestamp,
                                   chan,
                                   NULL,   // the sender's call site is not an address here
                                   rec->msgtmpl,
                                   rec->nargs,
                                   (stvar*)rec->args,
                                   ctx);

    if (ctx != octx)
        logCtxRelease(&ctx);
    logCtxRelease(&octx);

    if (!ent)
        return false;

    ent->istmpl = rec->istmpl;
    ent->sample = rec->sample;
    ent->trigger = rec->trigger;
    ent->hops   = rec->hops;
    strDup(&ent->origin, rec->origin);

    // The sender's number, not a fresh one: it is what identifies this record on the instance that
    // produced it, and what recovers the sender's ordering across a collector's several
    // connections. The local counter still advanced when the entry was built, so nothing here
    // renumbers anything local. The batch id is the exception and is assigned by fan-out below,
    // because a batch is a local delivery grouping -- what a destination's batch-done callback
    // and a log file's rotation boundary are about -- rather than a property of the record.
    ent->seq = rec->seq;

    logRingCapture(ent);
    logFanout(ent, true);
    return true;
}
