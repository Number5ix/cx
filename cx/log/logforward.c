// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "logforward.h"
#include "logwire_private.h"
#include <cx/string.h>
#include <cx/time.h>

// The forwarder destination.
//
// A destination like any other -- a msgfunc, a closefunc, a group -- that happens to turn the
// records it receives into bytes and hand them to somebody else's transport. Everything that
// touches a wire is on the far side of one function pointer, which is what keeps cx/log from
// depending on cx/net: net already depends on log, and a forwarder that reached back the other
// way would rebuild exactly the loop this file spends its effort preventing.
//
// **Locking.** One mutex covers the encoder, the spool and the connection state. It is held
// across `send`, which is deliberate: the alternative is two threads interleaving frames into one
// stream, and there is no framing that survives that. `send` is expected not to block -- it
// returns false instead -- so holding the lock across it costs a queue push, not an I/O wait.
//
// The lock is never taken while _log_op_lock is held, and this file takes _log_op_lock only in
// logforwardRegister/Unregister, which no log call can reach.

STR_CONST(kForwardGroup, "remote");

// One spool segment: a run of complete frames that decodes on its own.
//
// Eviction is per segment rather than per frame, and that is forced rather than chosen. Frames
// within a segment share a dictionary and a set of declarations established by the frames in front
// of them, so dropping the front of a segment would leave everything behind it undecodable.
// Cutting the stream into segments and dropping whole ones is what makes drop-oldest mean anything.
typedef struct LogSpoolSeg LogSpoolSeg;
struct LogSpoolSeg {
    LogSpoolSeg* next;
    Buffer frames;
    uint64 count;      // records in this segment
    uint64 firstseq;
    uint64 lastseq;

    // A gap this segment reported on behalf of segments evicted before it. If this segment is
    // itself evicted the statement goes with it, so the numbers move back into the forwarder's
    // pending gap and are reported again by whatever segment comes next.
    uint64 gapcount;
    uint64 gapfirst;
    uint64 gaplast;
};

typedef struct LogForwarder {
    Mutex lock;
    LogDest* dest;
    const LogForwardHandlers* handlers;
    void* ctx;

    LogWireEncoder* enc;
    string origin;
    Buffer scratch;   // frames for the record being encoded

    uint64 spoolbytes;
    uint64 segbytes;
    uint32 maxhops;

    // The ceiling: what registration said this forwarder may ever send. A subscription selects
    // within it and is clamped to it, which is what makes the local configuration the thing that
    // decides what may leave the machine and the remote one merely what does.
    int ceiling;
    bool subscribed;
    bool lapsed;    // the expiry passed while a drain thread was delivering; see fwdReapLocked()
    int64 expiry;   // wall clock at which the subscription lapses; 0 for never

    LogWireDecoder* ctldec;   // the receiver's half of the connection, created on first use

    bool connected;   // the application says there is a transport
    bool refused;     // ...but it is not taking anything at the moment

    LogSpoolSeg* head;   // oldest
    LogSpoolSeg* tail;   // newest, and the one being appended to
    uint64 bytes;

    // Records dropped to stay within the bound and not yet reported to the receiver.
    uint64 gapcount;
    uint64 gapfirst;
    uint64 gaplast;

    LogForwardStats stats;
} LogForwarder;

// Puts the destination back to receiving nothing. Nothing is sent until a subscription arrives:
// there is no locally configured "forward everything to host X", so silence is the resting state
// rather than a mode.
//
// Reconfiguring a destination takes _log_op_lock, so this may only be called from an application
// thread. See fwdReapLocked() for the drain-thread half.
static void fwdSilenceLocked(_Inout_ LogForwarder* fwd)
{
    fwd->subscribed = false;
    fwd->lapsed     = false;
    fwd->expiry     = 0;
    logDestSetSubFilter(fwd->dest, NULL);
    logDestSetLevel(fwd->dest, -1);
}

// Finishes tearing down a subscription that ran out while the drain thread was delivering.
//
// The expiry itself is noticed on the drain thread, which may only mark it: a drain thread runs
// its destinations' callbacks with the group's dispatch lock held, and nothing holding a dispatch
// lock may acquire _log_op_lock -- that is the ordering logDestSetGroup() relies on, and taking
// the configuration lock from a msgfunc would deadlock against a concurrent move. So the records
// stop immediately and the routing catches up on the next call from an application thread, which
// for any live transport is the next resume or the next byte from its receiver.
static void fwdReapLocked(_Inout_ LogForwarder* fwd)
{
    if (fwd->lapsed)
        fwdSilenceLocked(fwd);
}

// ---------------------------------------------------------------------------------------
// Spool
// ---------------------------------------------------------------------------------------

static void spoolSegFree(_Pre_valid_ _Post_invalid_ LogSpoolSeg* seg)
{
    bufDestroy(&seg->frames);
    xaFree(seg);
}

// Starts a new spool segment, cutting the encoder's stream so the segment decodes on its own.
static _Ret_valid_ LogSpoolSeg* spoolNewSegLocked(_Inout_ LogForwarder* fwd)
{
    logWireEndSegment(fwd->enc);

    LogSpoolSeg* seg = xaAllocStruct(LogSpoolSeg, XA_Zero);
    if (fwd->tail)
        fwd->tail->next = seg;
    else
        fwd->head = seg;
    fwd->tail = seg;

    // Whatever has been dropped and not yet accounted for is stated at the top of the newest
    // segment, so a receiver learns about it as soon as it learns anything.
    if (fwd->gapcount > 0 &&
        logWireEncodeGap(fwd->enc, &seg->frames, fwd->gapcount, fwd->gapfirst, fwd->gaplast)) {
        seg->gapcount = fwd->gapcount;
        seg->gapfirst = fwd->gapfirst;
        seg->gaplast  = fwd->gaplast;
        fwd->bytes += bufLen(seg->frames);
        fwd->gapcount = 0;
        fwd->gapfirst = 0;
        fwd->gaplast  = 0;
    }

    return seg;
}

static void spoolEvictOldestLocked(_Inout_ LogForwarder* fwd)
{
    LogSpoolSeg* seg = fwd->head;
    if (!seg)
        return;

    fwd->head = seg->next;
    if (!fwd->head)
        fwd->tail = NULL;
    fwd->bytes -= bufLen(seg->frames);

    uint64 lost  = seg->count + seg->gapcount;
    uint64 first = seg->gapcount ? seg->gapfirst : seg->firstseq;
    uint64 last  = seg->lastseq ? seg->lastseq : seg->gaplast;

    if (lost > 0) {
        if (fwd->gapcount == 0)
            fwd->gapfirst = first;
        fwd->gapcount += lost;
        if (last > fwd->gaplast)
            fwd->gaplast = last;
        fwd->stats.dropped += seg->count;
    }

    spoolSegFree(seg);
}

static void spoolAppendLocked(_Inout_ LogForwarder* fwd, _In_opt_ Buffer frames, uint64 seq,
                              uint64 nrecs)
{
    if (!fwd->tail)
        spoolNewSegLocked(fwd);

    LogSpoolSeg* seg = fwd->tail;
    bufAppend(&seg->frames, frames);
    fwd->bytes += bufLen(frames);
    seg->count += nrecs;
    if (nrecs > 0) {
        if (seg->firstseq == 0)
            seg->firstseq = seq;
        seg->lastseq = seq;
    }

    // Never evict the segment currently being appended to: doing so would throw away the newest
    // record, which is the one closest to whatever is going wrong.
    while (fwd->bytes > fwd->spoolbytes && fwd->head && fwd->head != fwd->tail)
        spoolEvictOldestLocked(fwd);

    if (bufLen(seg->frames) >= fwd->segbytes)
        spoolNewSegLocked(fwd);
}

static void spoolClearLocked(_Inout_ LogForwarder* fwd)
{
    LogSpoolSeg* seg = fwd->head;
    while (seg) {
        LogSpoolSeg* next = seg->next;
        spoolSegFree(seg);
        seg = next;
    }
    fwd->head  = NULL;
    fwd->tail  = NULL;
    fwd->bytes = 0;
}

// ---------------------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------------------

// Every call into the application's transport goes through here, so the loop-prevention scope is
// in one place and cannot be forgotten on one of the paths. Records logged by the transport while
// this is on the stack are marked localonly and never come back to a forwarder.
static bool fwdSendLocked(_Inout_ LogForwarder* fwd, _In_opt_ Buffer frames)
{
    if (!fwd->handlers->send || !frames)
        return false;

    bool ret = false;
    withLogLocal() {
        ret = fwd->handlers->send(fwd->ctx, frames->data, frames->len);
    }

    return ret;
}

// Hands the spool to the transport oldest first, stopping at the first refusal.
static void fwdDrainLocked(_Inout_ LogForwarder* fwd)
{
    while (fwd->head && fwd->connected && !fwd->refused) {
        LogSpoolSeg* seg = fwd->head;
        if (bufLen(seg->frames) > 0 && !fwdSendLocked(fwd, seg->frames)) {
            fwd->refused = true;
            return;
        }

        fwd->head = seg->next;
        if (!fwd->head)
            fwd->tail = NULL;
        fwd->bytes -= bufLen(seg->frames);
        fwd->stats.sent += seg->count;
        spoolSegFree(seg);
    }
}

// ---------------------------------------------------------------------------------------
// Destination callbacks
// ---------------------------------------------------------------------------------------

static void logForwardMsg(_In_ const LogRecord* rec, _In_opt_ void* userdata)
{
    LogForwarder* fwd = (LogForwarder*)userdata;

    withMutex (&fwd->lock) {
        // A subscription that has run out stops the traffic rather than leaving this process
        // talking to a collector that went away without saying so. Only marked here; the
        // reconfiguration that makes it free rather than merely silent happens on an application
        // thread (see fwdReapLocked).
        if (fwd->subscribed && fwd->expiry != 0 && clockWall() >= fwd->expiry) {
            fwd->subscribed = false;
            fwd->lapsed     = true;
        }
        if (!fwd->subscribed)
            break;

        // Loop prevention across processes: a record that has been forwarded far enough, or that
        // this instance originally produced and is now seeing come back, stops here. The other
        // two layers are earlier -- cx/net never binds to a forwarder, and a record produced
        // inside a log-owned callback is never dispatched to one -- so anything arriving here has
        // already passed those.
        if (rec->hops >= fwd->maxhops ||
            (rec->origin && !strEmpty(fwd->origin) && strEq(rec->origin, fwd->origin))) {
            fwd->stats.looped++;
            break;
        }

        if (!logWireEncode(fwd->enc, &fwd->scratch, rec)) {
            fwd->stats.failed++;
            break;
        }

        if (fwd->connected && !fwd->refused && !fwd->head) {
            if (fwdSendLocked(fwd, fwd->scratch)) {
                fwd->stats.sent++;
                break;
            }

            // The transport said not now. What was just encoded continues the segment the
            // connection has been using, which the receiver will never see the rest of, so it
            // cannot go into the spool as it stands: re-encoding it after a segment cut is what
            // makes the first spooled bytes decodable on their own.
            fwd->refused = true;
            spoolNewSegLocked(fwd);
            if (!logWireEncode(fwd->enc, &fwd->scratch, rec)) {
                fwd->stats.failed++;
                break;
            }
        }

        spoolAppendLocked(fwd, fwd->scratch, rec->seq, 1);
        fwd->stats.spooled++;
    }
}

static void logForwardClose(_In_opt_ void* userdata)
{
    LogForwarder* fwd = (LogForwarder*)userdata;
    if (!fwd)
        return;

    logWireDecoderDestroy(&fwd->ctldec);

    if (fwd->handlers->close) {
        withLogLocal() {
            fwd->handlers->close(fwd->ctx);
        }
    }

    spoolClearLocked(fwd);
    logWireEncoderDestroy(&fwd->enc);
    bufDestroy(&fwd->scratch);
    strDestroy(&fwd->origin);
    mutexDestroy(&fwd->lock);
    xaFree(fwd);
}

// ---------------------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------------------

_Use_decl_annotations_
LogForwarder* logforwardRegister(int maxlevel, strref chanfilter,
                                 const LogForwardHandlers* handlers, void* ctx,
                                 const LogForwardConfig* config)
{
    if (!handlers || !handlers->send)
        return NULL;

    LogForwarder* fwd = xaAllocStruct(LogForwarder, XA_Zero);
    mutexInit(&fwd->lock);
    fwd->handlers  = handlers;
    fwd->ctx       = ctx;
    fwd->connected = true;

    fwd->ceiling    = maxlevel;
    fwd->spoolbytes = LOG_FORWARD_SPOOL_DEFAULT;
    fwd->segbytes   = LOG_FORWARD_SEGMENT_DEFAULT;
    fwd->maxhops    = LOG_FORWARD_MAXHOPS_DEFAULT;

    if (config) {
        strDup(&fwd->origin, config->origin);
        if (config->spoolbytes)
            fwd->spoolbytes = config->spoolbytes;
        if (config->segbytes)
            fwd->segbytes = config->segbytes;
        if (config->maxhops)
            fwd->maxhops = config->maxhops;
    }

    fwd->enc = logWireEncoderCreate(fwd->origin, 0);

    // The remote flag goes on before the destination is published, so the cx/net exclusion is in
    // force from its very first record rather than from a republish a moment later. The level is
    // -1 rather than the ceiling: a forwarder is silent until a receiver asks for something.
    fwd->dest = logRegisterRemoteDest(-1, chanfilter, logForwardMsg, NULL, logForwardClose, fwd);
    if (!fwd->dest) {
        logForwardClose(fwd);
        return NULL;
    }

    // §9's group isolation is what keeps a stalled forwarder from delaying the local file writes
    // needed to diagnose it.
    logDestSetGroup(fwd->dest, kForwardGroup);
    return fwd;
}

_Use_decl_annotations_
LogDest* logForwardDest(LogForwarder* fwd)
{
    return fwd->dest;
}

_Use_decl_annotations_
void logforwardUnregister(LogForwarder* fwd)
{
    if (!fwd)
        return;

    // The destination owns the forwarder from registration onwards; unregistering hands it to the
    // grace period, and logForwardClose() frees it once no drain thread can still be inside it.
    logUnregisterDest(fwd->dest);
}

_Use_decl_annotations_
void logForwardResume(LogForwarder* fwd)
{
    withMutex (&fwd->lock) {
        fwdReapLocked(fwd);
        fwd->refused = false;
        fwdDrainLocked(fwd);
    }
}

_Use_decl_annotations_
void logForwardDisconnected(LogForwarder* fwd)
{
    withMutex (&fwd->lock) {
        fwdReapLocked(fwd);
        if (!fwd->connected)
            break;
        fwd->connected = false;
        fwd->refused   = false;

        // What was already sent is gone with the connection, so the next thing spooled has to
        // stand on its own rather than continue a segment nobody will see the front of.
        spoolNewSegLocked(fwd);
    }
}

_Use_decl_annotations_
void logForwardConnected(LogForwarder* fwd)
{
    withMutex (&fwd->lock) {
        fwdReapLocked(fwd);

        // A new connection means a new decoder at the far end, so the backfill has to start at a
        // segment boundary. The spool is already cut into segments; this only closes the one that
        // was still being appended to.
        if (fwd->tail && bufLen(fwd->tail->frames) > 0)
            spoolNewSegLocked(fwd);

        fwd->connected = true;
        fwd->refused   = false;
        fwdDrainLocked(fwd);
    }
}

_Use_decl_annotations_
bool logForwardTake(LogForwarder* fwd, Buffer* out)
{
    bool ret = false;

    withMutex (&fwd->lock) {
        fwdReapLocked(fwd);

        // The segment still being appended to is closed first, so a caller draining to empty gets
        // everything rather than everything but the newest records.
        if (fwd->head && fwd->head == fwd->tail && bufLen(fwd->tail->frames) > 0)
            spoolNewSegLocked(fwd);

        while (fwd->head) {
            LogSpoolSeg* seg = fwd->head;
            fwd->head        = seg->next;
            if (!fwd->head)
                fwd->tail = NULL;
            fwd->bytes -= bufLen(seg->frames);

            bool empty = bufLen(seg->frames) == 0;
            if (!empty) {
                // The segment is about to be freed, so its bytes are handed over rather than
                // copied.
                bufDestroy(out);
                *out            = seg->frames;
                seg->frames     = NULL;
                fwd->stats.sent += seg->count;
                ret = true;
            }
            spoolSegFree(seg);
            if (!empty)
                break;
        }
    }

    return ret;
}

_Use_decl_annotations_
bool logForwardApplySub(LogForwarder* fwd, const LogSubSpec* spec)
{
    bool ret = false;

    withMutex (&fwd->lock) {
        if (!spec) {
            fwdSilenceLocked(fwd);
            ret = true;
            break;
        }

        // Clamped, never trusted: a receiver asking for Trace on a forwarder registered at Info
        // gets Info. The channel patterns are a second rule set the local filter is ANDed with,
        // so a subscription can only ever narrow what registration already allowed.
        int level = spec->maxlevel;
        if (level > fwd->ceiling)
            level = fwd->ceiling;

        logDestSetSubFilter(fwd->dest, &spec->patterns);
        ret = logDestSetLevel(fwd->dest, level);

        fwd->subscribed = ret;
        fwd->lapsed     = false;
        fwd->expiry     = spec->expiry;
    }

    return ret;
}

// The receiver's frames, which are control frames and nothing else. Anything that is not one --
// including a kind this build does not implement -- is ignored rather than refused, so a newer
// collector talking to an older leaf still gets a subscription applied.
static bool fwdControlFrame(const LogWireFrame* frame, void* ctx)
{
    LogForwarder* fwd = (LogForwarder*)ctx;

    if (frame->kind == LOG_WireSubscribe) {
        // The forwarder's own lock is not held here: logWireDecode() runs on the application's
        // thread with nothing of ours locked, and logForwardApplySub() takes it itself.
        logForwardApplySub(fwd, saSize(frame->sub->patterns) == 0 && frame->sub->maxlevel < 0
                                    ? NULL
                                    : frame->sub);
    }

    return true;
}

_Use_decl_annotations_
bool logForwardRecv(LogForwarder* fwd, const uint8* buf, size_t len)
{
    withMutex (&fwd->lock) {
        fwdReapLocked(fwd);
    }

    if (!fwd->ctldec)
        fwd->ctldec = logWireDecoderCreate();

    if (!logWireDecode(fwd->ctldec, buf, len, fwdControlFrame, fwd)) {
        // The decoder cannot be reused after a malformed stream; the caller is closing the
        // connection, and a fresh one gets a fresh decoder.
        logWireDecoderDestroy(&fwd->ctldec);
        return false;
    }

    return true;
}

typedef struct LogFwdCatalogWalk {
    LogWireChanInfo* chans;
    uint32 n;
    uint32 cap;
} LogFwdCatalogWalk;

static bool fwdCatalogChan(_In_ LogChannel* chan, _In_opt_ void* ctx)
{
    LogFwdCatalogWalk* w = (LogFwdCatalogWalk*)ctx;

    if (strEmpty(chan->path))
        return true;   // the root has no name to publish

    if (w->n == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 32;
        xaResize(&w->chans, sizeof(LogWireChanInfo) * w->cap);
    }

    w->chans[w->n].path     = chan->path;
    w->chans[w->n].flags    = chan->flags;
    w->chans[w->n].maxlevel = atomicLoad(int32, &chan->maxlevel, Relaxed);
    w->n++;
    return true;
}

_Use_decl_annotations_
bool logForwardCatalog(LogForwarder* fwd, Buffer* out)
{
    LogFwdCatalogWalk walk = { 0 };
    logEnumChans(fwdCatalogChan, &walk);

    bool ret = false;
    withMutex (&fwd->lock) {
        ret = logWireEncodeCatalog(fwd->enc, out, walk.chans, (int)walk.n);
    }

    xaFree(walk.chans);
    return ret;
}

_Use_decl_annotations_
void logForwardStats(LogForwarder* fwd, LogForwardStats* out)
{
    withMutex (&fwd->lock) {
        *out            = fwd->stats;
        out->pending    = fwd->bytes;
        out->subscribed = fwd->subscribed;
    }
}
