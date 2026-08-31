// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "logwire_private.h"
#include <cx/container/foreach.h>
#include <cx/format.h>
#include <cx/serialize/sbbuffer.h>
#include <cx/serialize/serbinary.h>
#include <cx/string.h>
#include "log/logsnapshot.h"

// The log wire format.
//
// Two layers, deliberately separated:
//
// - **Framing**, written here: `varint kind, varint length, payload`. It sits outside the
//   serialized stream so that a receiver can find frame boundaries, skip a kind it does not
//   understand, and store or drop whole frames without decoding them.
// - **Payloads**, written by serbinary. Reusing the binary backend rather than hand-rolling an
//   encoding is what makes every stvar type a log call can carry work here for free.
//
// serbinary runs with SER_Bin_NoStringDedup, which splits the interning exactly where it wants
// to be split: map keys and type names still go into the writer's dictionary, string *values* go
// out inline. So the dictionary holds field names, which are bounded by what is compiled into the
// binary, and never fills up with request ids or hostnames.
//
// Everything that is not a serbinary map key -- channel paths and message templates -- is
// declared by this file instead, once per segment, and referenced by a small integer afterwards.
//
// **Segments.** A segment is one complete serbinary document: its own header, its own dictionary,
// its own declarations, independently decodable. While a connection is up there is one segment
// for its whole life, which is what makes interning worth anything. A caller that may later have
// to throw away part of what it produced -- a spool evicting its oldest bytes -- calls
// logWireEndSegment() periodically, so that what survives still decodes. Without that, dropping
// the front of the stream would take the dictionary with it and leave everything after it
// undecodable.
//
// A serbinary document that is a *sequence* of top-level values is a documented contract of this
// file rather than an accident: the traverser has no one-value guard and the reader takes its
// header once at creation, so one writer emits as many top-level values as it is asked for.

// Distinct argument and context keys one segment's dictionary will hold before the segment is
// cut. Field names are bounded by the binary in every sane program, but stvarkn() takes a runtime
// name, so an application that generates them without bound would otherwise grow this forever.
// Cutting the segment starts a fresh dictionary, which is bounded and self-healing.
#define LOG_WIRE_DICT_MAX 4096

// Longest frame payload a decoder will accept. A frame is one log record; anything remotely near
// this is a malformed or hostile stream rather than a big message.
#define LOG_WIRE_FRAME_MAX (16 * 1024 * 1024)

// How the value of one argument is encoded, since not every stvar a log call can carry has a
// serbinary representation.
enum LOG_WIRE_ARG {
    LOG_WireArgPlain = 0,   // the value as itself
    LOG_WireArgText,        // a string standing in for a value whose type does not cross
};

STR_CONST(kwChan, "chan");
STR_CONST(kwLevel, "level");
STR_CONST(kwTs, "ts");
STR_CONST(kwSeq, "seq");
STR_CONST(kwBatch, "batch");
STR_CONST(kwSample, "sample");
STR_CONST(kwTrigger, "trigger");
STR_CONST(kwHops, "hops");
STR_CONST(kwOrigin, "origin");
STR_CONST(kwSite, "site");
STR_CONST(kwTmpl, "tmpl");
STR_CONST(kwIsTmpl, "istmpl");
STR_CONST(kwArgs, "args");
STR_CONST(kwKArgs, "kargs");
STR_CONST(kwCtx, "ctx");
STR_CONST(kwId, "id");
STR_CONST(kwPath, "path");
STR_CONST(kwFlags, "flags");
STR_CONST(kwMinLevel, "minlevel");
STR_CONST(kwCount, "count");
STR_CONST(kwFirst, "first");
STR_CONST(kwLast, "last");
STR_CONST(kwPatterns, "patterns");
STR_CONST(kwExpiry, "expiry");
STR_CONST(kwChans, "chans");
STR_CONST(kwObjFmt, "${object}");

#define LOG_WIRE_ENTRY_FIELDS 15
#define LOG_WIRE_CHAN_FIELDS  4
#define LOG_WIRE_SITE_FIELDS  3
#define LOG_WIRE_GAP_FIELDS   3
#define LOG_WIRE_SUB_FIELDS   3
#define LOG_WIRE_CAT_FIELDS   1
#define LOG_WIRE_CATCHAN_FIELDS 3

// ---------------------------------------------------------------------------------------
// Framing primitives
// ---------------------------------------------------------------------------------------

static void wireVarint(_Inout_ Buffer* out, uint64 v)
{
    uint8 buf[10];
    int n = 0;

    do {
        uint8 b = v & 0x7f;
        v >>= 7;
        if (v)
            b |= 0x80;
        buf[n++] = b;
    } while (v);

    bufAppendBytes(out, buf, n);
}

// Reads a varint out of a flat byte range. Returns false when the range does not hold a whole
// one, which is the ordinary "wait for more bytes" answer rather than an error.
static bool wireVarintRead(_In_reads_bytes_(len) const uint8* buf, size_t len, _Inout_ size_t* pos,
                           _Out_ uint64* out, _Out_ bool* bad)
{
    uint64 v  = 0;
    int shift = 0;
    size_t p  = *pos;
    *bad      = false;

    for (;;) {
        if (p >= len)
            return false;
        if (shift > 63) {
            *bad = true;
            return false;
        }

        uint8 b = buf[p++];
        v |= (uint64)(b & 0x7f) << shift;
        shift += 7;
        if (!(b & 0x80))
            break;
    }

    *pos = p;
    *out = v;
    return true;
}

// ---------------------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------------------

typedef struct LogWireEncoder {
    string origin;
    flags_t flags;

    // Segment state. The writer, the stream it produces into and everything declared through it
    // all live and die together; see logWireEndSegment().
    StreamBuffer* sb;
    SerWriter* w;
    Buffer accum;   // bytes the writer has produced since the last frame was harvested
    bool segopen;

    hashtable chanids;    // LogChannel* -> uint32, ids from 1
    hashtable siteids;    // const LogSite* -> uint32, ids from 1
    sa_string sitetmpl;   // template declared for site id i+1
    sa_uint32 siteflag;   // istmpl declared for site id i+1
    hashtable dictkeys;   // argument and context keys handed to the writer this segment
    uint32 nextchan;
    uint32 nextsite;
} LogWireEncoder;

_Use_decl_annotations_
LogWireEncoder* logWireEncoderCreate(strref origin, flags_t flags)
{
    LogWireEncoder* enc = xaAllocStruct(LogWireEncoder, XA_Zero);
    enc->flags          = flags;
    strDup(&enc->origin, origin);

    htInit(&enc->chanids, ptr, uint32, 16);
    htInit(&enc->siteids, ptr, uint32, 32);
    htInit(&enc->dictkeys, string, bool, 32);
    saInit(&enc->sitetmpl, string, 32);
    saInit(&enc->siteflag, uint32, 32);

    return enc;
}

// Tears down the writer and everything it interned, without emitting anything. The next encode
// opens a fresh segment.
static void wireEncCloseSegment(_Inout_ LogWireEncoder* enc)
{
    if (enc->w)
        serWriterDestroy(&enc->w);   // finishes the producer side, which releases its reference
    if (enc->sb)
        sbufRelease(&enc->sb);       // ...and this is the encoder's own

    bufClear(enc->accum);
    htClear(&enc->chanids);
    htClear(&enc->siteids);
    htClear(&enc->dictkeys);
    saClear(&enc->sitetmpl);
    saClear(&enc->siteflag);
    enc->nextchan = 0;
    enc->nextsite = 0;
    enc->segopen  = false;
}

_Use_decl_annotations_
void logWireEndSegment(LogWireEncoder* enc)
{
    wireEncCloseSegment(enc);
}

_Use_decl_annotations_
void logWireEncoderDestroy(LogWireEncoder** penc)
{
    LogWireEncoder* enc = *penc;
    if (!enc)
        return;
    *penc = NULL;

    wireEncCloseSegment(enc);

    htDestroy(&enc->chanids);
    htDestroy(&enc->siteids);
    htDestroy(&enc->dictkeys);
    saDestroy(&enc->sitetmpl);
    saDestroy(&enc->siteflag);
    bufDestroy(&enc->accum);
    strDestroy(&enc->origin);
    xaFree(enc);
}

// Wraps whatever the writer has produced since the last harvest in a frame header and moves it
// into the output buffer.
static bool wireEncEmit(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out, int kind)
{
    if (!enc->w || enc->w->err.code != SER_Err_None)
        return false;

    wireVarint(out, (uint64)kind);
    wireVarint(out, bufLen(enc->accum));
    bufAppend(out, enc->accum);
    bufClear(enc->accum);
    return true;
}

static bool wireEncOpenSegment(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out)
{
    if (enc->segopen)
        return true;

    // Direct mode: the consumer is a Buffer that always takes everything, so the stream buffer
    // needs no storage of its own and the writer's bytes land in accum with one copy.
    enc->sb = sbufCreate(0);
    if (!sbufBufCRegisterPush(enc->sb, &enc->accum)) {
        sbufRelease(&enc->sb);
        return false;
    }

    // The writer emits the document header as it is created, so what lands in accum here is
    // exactly the bytes a decoder has to see before anything else in this segment.
    enc->w = serBinaryWriterCreate(enc->sb, SER_Bin_NoStringDedup);
    if (enc->w->err.code != SER_Err_None) {
        wireEncCloseSegment(enc);
        return false;
    }

    enc->segopen = true;
    return wireEncEmit(enc, out, LOG_WireSegment);
}

// Every key that reaches serMapKey() interns into the writer's dictionary. The fixed field names
// above are bounded by this file; argument and context keys are not, so they are the ones counted
// against the cap.
static void wireEncCountKey(_Inout_ LogWireEncoder* enc, _In_opt_z_ const char* key)
{
    if (!key)
        return;
    htInsert(&enc->dictkeys, string, (string)(strref)key, bool, true);
}

// Can serbinary carry this variant as itself? Objects, structs and containers have no wire name a
// receiver could resolve without a schema it does not have, so they cross as text instead.
static bool wireEncPlain(stype st)
{
    if (!st)
        return false;

    switch (st->id) {
    case STypeId_object:
    case STypeId_structp:
    case STypeId_struct:
    case STypeId_stvar:
    case STypeId_sarray:
    case STypeId_hashtable:
    case STypeId_opaque:
        return false;
    default:
        return _serBuiltinName(st->id) != NULL;
    }
}

// Renders a variant the wire cannot carry as itself. Object arguments are already LogSnapshots by
// the time a record exists (see logCopyArg), so this is reading back a string that was captured at
// the call site rather than formatting anything live.
static void wireEncText(_Inout_ string* out, _In_ const stvar* v)
{
    strClear(out);

    if (stEq(stvarType(v), stType(object))) {
        if (_strFormat(out, kwObjFmt, 1, (stvar*)v))
            return;
        strClear(out);
        return;
    }

    stgeneric dest = { 0 };
    if (_stConvert(stType(string), &dest, stvarType(v), v->data, 0)) {
        strDup(out, dest.st_string);
        strDestroy(&dest.st_string);
    }
}

static bool wireEncValue(_Inout_ LogWireEncoder* enc, _In_ const stvar* src)
{
    SerWriter* w = enc->w;

    if (wireEncPlain(stvarType(src))) {
        // the key travels separately, so what goes out here is the bare value
        stvar v = { .data = src->data, ._type = src->_type, ._key = NULL };
        return serWriteUint(w, LOG_WireArgPlain, stType(uint8)) &&
               _serWrite(w, stExt(stvar), stArg(stvar, v));
    }

    string txt = 0;
    wireEncText(&txt, src);
    bool ret = serWriteUint(w, LOG_WireArgText, stType(uint8)) && serWriteStr(w, txt);
    strDestroy(&txt);
    return ret;
}

// Counts the fields of a record that become named wire fields, which the container header needs
// up front. Keyed arguments and context fields are disjoint sets and both are counted here so the
// two walks below cannot disagree with the count they were written under.
static int wireEncCountKeyed(_In_ const LogRecord* rec)
{
    int n = 0;
    for (int i = 0; i < rec->nargs; i++) {
        if (stvarKey(&rec->args[i]))
            ++n;
    }
    return n;
}

static int wireEncCountUnkeyed(_In_ const LogRecord* rec)
{
    int n = 0;
    for (int i = 0; i < rec->nargs; i++) {
        if (!stvarKey(&rec->args[i]))
            ++n;
    }
    return n;
}

static int wireEncCountCtx(_In_ const LogWireEncoder* enc, _In_ const LogRecord* rec)
{
    if (enc->flags & LOG_WireOmitCtx)
        return 0;

    int n = 0;
    for (const LogCtx* c = rec->ctx; c; c = logCtxParent(c)) {
        const stvar* vars = logCtxVars(c);
        uint32 nv         = logCtxNumVars(c);
        for (uint32 i = 0; i < nv; i++) {
            const char* key = stvarKey(&vars[i]);
            if (key && !logCtxShadowed(rec->ctx, c, i, key))
                ++n;
        }
    }
    return n;
}

static bool wireEncChanDecl(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out, _In_ LogChannel* chan,
                            _Out_ uint32* id)
{
    if (htFind(enc->chanids, ptr, chan, uint32, id))
        return true;

    *id = ++enc->nextchan;
    htInsert(&enc->chanids, ptr, chan, uint32, *id);

    SerWriter* w = enc->w;
    bool ok      = serMapBegin(w, LOG_WIRE_CHAN_FIELDS);
    ok = ok && serMapKey(w, kwId) && serWriteUint(w, *id, stType(uint32));
    ok = ok && serMapKey(w, kwPath) && serWriteStr(w, chan->path);
    ok = ok && serMapKey(w, kwFlags) && serWriteUint(w, chan->flags, stType(uint32));
    ok = ok && serMapKey(w, kwMinLevel) &&
         serWriteInt(w, atomicLoad(int32, &chan->maxlevel, Relaxed), stType(int32));
    ok = ok && serMapEnd(w);

    return ok && wireEncEmit(enc, out, LOG_WireChanDecl);
}

// Declares a call site's template, and reports the id to reference it by. Returns 0 when the
// record has to carry its template inline: it was logged with no call site, or -- vanishingly
// rare, but cheap to be right about -- this site logged a different template than the one it was
// declared with.
static uint32 wireEncSiteDecl(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out,
                              _In_ const LogRecord* rec, _Out_ bool* ok)
{
    *ok = true;
    if (!rec->site)
        return 0;

    uint32 id;
    if (htFind(enc->siteids, ptr, (void*)rec->site, uint32, &id)) {
        bool istmpl = enc->siteflag.a[id - 1] != 0;
        if (istmpl != rec->istmpl || !strEq(enc->sitetmpl.a[id - 1], rec->msgtmpl))
            return 0;
        return id;
    }

    id = ++enc->nextsite;
    htInsert(&enc->siteids, ptr, (void*)rec->site, uint32, id);
    saPush(&enc->sitetmpl, string, (string)rec->msgtmpl);
    saPush(&enc->siteflag, uint32, rec->istmpl ? 1 : 0);

    SerWriter* w = enc->w;
    bool r       = serMapBegin(w, LOG_WIRE_SITE_FIELDS);
    r = r && serMapKey(w, kwId) && serWriteUint(w, id, stType(uint32));
    r = r && serMapKey(w, kwTmpl) && serWriteStr(w, rec->msgtmpl);
    r = r && serMapKey(w, kwIsTmpl) && serWriteBool(w, rec->istmpl);
    r = r && serMapEnd(w);

    *ok = r && wireEncEmit(enc, out, LOG_WireSiteDecl);
    return *ok ? id : 0;
}

static bool wireEncEntry(_Inout_ LogWireEncoder* enc, _Inout_ Buffer* out,
                         _In_ const LogRecord* rec, uint32 chanid, uint32 siteid)
{
    SerWriter* w = enc->w;
    strref origin = rec->origin ? rec->origin : (strref)enc->origin;

    bool ok = serMapBegin(w, LOG_WIRE_ENTRY_FIELDS);
    ok = ok && serMapKey(w, kwChan) && serWriteUint(w, chanid, stType(uint32));
    ok = ok && serMapKey(w, kwLevel) && serWriteInt(w, rec->level, stType(int32));
    ok = ok && serMapKey(w, kwTs) && serWriteInt(w, rec->timestamp, stType(int64));
    ok = ok && serMapKey(w, kwSeq) && serWriteUint(w, rec->seq, stType(uint64));
    ok = ok && serMapKey(w, kwBatch) && serWriteUint(w, rec->batchid, stType(uint32));
    ok = ok && serMapKey(w, kwSample) && serWriteUint(w, rec->sample, stType(uint32));
    ok = ok && serMapKey(w, kwTrigger) && serWriteInt(w, rec->trigger, stType(int32));

    // A record that arrived from somewhere else keeps the origin it came with and counts one more
    // hop; one logged here takes this encoder's identity and starts at zero.
    ok = ok && serMapKey(w, kwHops) && serWriteUint(w, (uint64)rec->hops + 1, stType(uint32));
    ok = ok && serMapKey(w, kwOrigin) && serWriteStr(w, origin);

    ok = ok && serMapKey(w, kwSite) && serWriteUint(w, siteid, stType(uint32));
    ok = ok && serMapKey(w, kwTmpl) && serWriteStr(w, siteid ? NULL : rec->msgtmpl);
    ok = ok && serMapKey(w, kwIsTmpl) && serWriteBool(w, rec->istmpl);

    ok = ok && serMapKey(w, kwArgs) && serArrBegin(w, wireEncCountUnkeyed(rec));
    for (int i = 0; ok && i < rec->nargs; i++) {
        if (!stvarKey(&rec->args[i]))
            ok = wireEncValue(enc, &rec->args[i]);
    }
    ok = ok && serArrEnd(w);

    ok = ok && serMapKey(w, kwKArgs) && serMapBegin(w, wireEncCountKeyed(rec));
    for (int i = 0; ok && i < rec->nargs; i++) {
        const char* key = stvarKey(&rec->args[i]);
        if (!key)
            continue;
        wireEncCountKey(enc, key);
        ok = serMapKey(w, (strref)key) && wireEncValue(enc, &rec->args[i]);
    }
    ok = ok && serMapEnd(w);

    ok = ok && serMapKey(w, kwCtx) && serMapBegin(w, wireEncCountCtx(enc, rec));
    if (!(enc->flags & LOG_WireOmitCtx)) {
        for (const LogCtx* c = rec->ctx; ok && c; c = logCtxParent(c)) {
            const stvar* vars = logCtxVars(c);
            uint32 nv         = logCtxNumVars(c);
            for (uint32 i = 0; ok && i < nv; i++) {
                const char* key = stvarKey(&vars[i]);
                if (!key || logCtxShadowed(rec->ctx, c, i, key))
                    continue;
                wireEncCountKey(enc, key);
                ok = serMapKey(w, (strref)key) && wireEncValue(enc, &vars[i]);
            }
        }
    }
    ok = ok && serMapEnd(w);
    ok = ok && serMapEnd(w);

    return ok && wireEncEmit(enc, out, LOG_WireEntry);
}

_Use_decl_annotations_
bool logWireEncode(LogWireEncoder* enc, Buffer* out, const LogRecord* rec)
{
    bufClear(*out);

    if (!wireEncOpenSegment(enc, out))
        return false;

    uint32 chanid = 0;
    bool ok       = wireEncChanDecl(enc, out, rec->chan, &chanid);

    uint32 siteid = 0;
    if (ok)
        siteid = wireEncSiteDecl(enc, out, rec, &ok);

    ok = ok && wireEncEntry(enc, out, rec, chanid, siteid);

    if (!ok) {
        // The writer's errors are sticky and part of a frame may already be in the stream, so the
        // segment cannot be continued. Dropping it costs the declarations it had interned and
        // nothing else; the next record opens a new one. Whatever reached the output buffer is
        // part of that abandoned segment, so it goes too.
        wireEncCloseSegment(enc);
        bufClear(*out);
        return false;
    }

    if (htSize(enc->dictkeys) >= LOG_WIRE_DICT_MAX)
        wireEncCloseSegment(enc);

    return true;
}

_Use_decl_annotations_
bool logWireEncodeGap(LogWireEncoder* enc, Buffer* out, uint64 n, uint64 firstseq, uint64 lastseq)
{
    bufClear(*out);

    if (!wireEncOpenSegment(enc, out))
        return false;

    SerWriter* w = enc->w;
    bool ok      = serMapBegin(w, LOG_WIRE_GAP_FIELDS);
    ok = ok && serMapKey(w, kwCount) && serWriteUint(w, n, stType(uint64));
    ok = ok && serMapKey(w, kwFirst) && serWriteUint(w, firstseq, stType(uint64));
    ok = ok && serMapKey(w, kwLast) && serWriteUint(w, lastseq, stType(uint64));
    ok = ok && serMapEnd(w);
    ok = ok && wireEncEmit(enc, out, LOG_WireGap);

    if (!ok) {
        wireEncCloseSegment(enc);
        bufClear(*out);
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool logWireEncodeSub(LogWireEncoder* enc, Buffer* out, const LogSubSpec* spec)
{
    bufClear(*out);

    if (!wireEncOpenSegment(enc, out))
        return false;

    // A NULL spec cancels: no patterns, nothing wanted. It is the same frame rather than one of
    // its own, so a receiver has one thing to implement and unsubscribing cannot be a kind an
    // older sender does not recognize.
    uint32 npat = (spec && spec->patterns.a) ? saSize(spec->patterns) : 0;

    SerWriter* w = enc->w;
    bool ok      = serMapBegin(w, LOG_WIRE_SUB_FIELDS);
    ok = ok && serMapKey(w, kwLevel) && serWriteInt(w, spec ? spec->maxlevel : -1, stType(int32));
    ok = ok && serMapKey(w, kwExpiry) && serWriteInt(w, spec ? spec->expiry : 0, stType(int64));
    ok = ok && serMapKey(w, kwPatterns) && serArrBegin(w, (int32)npat);
    for (uint32 i = 0; ok && i < npat; i++)
        ok = serWriteStr(w, spec->patterns.a[i]);
    ok = ok && serArrEnd(w);
    ok = ok && serMapEnd(w);
    ok = ok && wireEncEmit(enc, out, LOG_WireSubscribe);

    if (!ok) {
        wireEncCloseSegment(enc);
        bufClear(*out);
        return false;
    }

    return true;
}

_Use_decl_annotations_
bool logWireEncodeCatalog(LogWireEncoder* enc, Buffer* out, const LogWireChanInfo* chans,
                          int nchans)
{
    bufClear(*out);

    if (!wireEncOpenSegment(enc, out))
        return false;
    if (nchans < 0)
        nchans = 0;

    SerWriter* w = enc->w;
    bool ok      = serMapBegin(w, LOG_WIRE_CAT_FIELDS);
    ok = ok && serMapKey(w, kwChans) && serArrBegin(w, nchans);
    for (int i = 0; ok && i < nchans; i++) {
        ok = serMapBegin(w, LOG_WIRE_CATCHAN_FIELDS);
        ok = ok && serMapKey(w, kwPath) && serWriteStr(w, chans[i].path);
        ok = ok && serMapKey(w, kwFlags) && serWriteUint(w, chans[i].flags, stType(uint32));
        ok = ok && serMapKey(w, kwMinLevel) && serWriteInt(w, chans[i].maxlevel, stType(int32));
        ok = ok && serMapEnd(w);
    }
    ok = ok && serArrEnd(w);
    ok = ok && serMapEnd(w);
    ok = ok && wireEncEmit(enc, out, LOG_WireCatalog);

    if (!ok) {
        wireEncCloseSegment(enc);
        bufClear(*out);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------------------
// Key interning
//
// stvarkn() pointer-copies the name it is given and never duplicates it, so a decoded field name
// has to outlive every variant that carries it -- which in practice means the process. The table
// is therefore permanent, exactly like the channel registry, and is the receiving-side twin of the
// encoder's dictionary cap: a sender that invents field names without bound would otherwise grow
// it forever.
// ---------------------------------------------------------------------------------------

static Mutex _logwire_keylock;
static hashtable _logwire_keys;      // key -> the interned char pointer for it
static sa_string _logwire_keystore;  // owns the flattened strings those pointers live inside
static LazyInitState _logwire_keyinit;

static void logWireKeyInit(void* unused)
{
    mutexInit(&_logwire_keylock);
    htInit(&_logwire_keys, string, ptr, 64);
    saInit(&_logwire_keystore, string, 64);
}

_Use_decl_annotations_
const char* logWireInternKey(strref key)
{
    if (strEmpty(key))
        return NULL;

    lazyInit(&_logwire_keyinit, logWireKeyInit, NULL);

    void* ret = NULL;
    withMutex (&_logwire_keylock) {
        if (htFind(_logwire_keys, strref, key, ptr, &ret))
            break;

        if (saSize(_logwire_keystore) >= LOG_WIRE_DICT_MAX)
            break;   // a sender inventing names without bound does not get to grow this forever

        string own = 0;
        strDup(&own, key);

        // Flatten before the store takes its reference, so the pointer handed out lives in the
        // buffer the store is keeping alive rather than in a rope that may be rearranged.
        ret = (void*)strPC(&own);
        saPush(&_logwire_keystore, string, own);
        htInsert(&_logwire_keys, string, own, ptr, ret);
        strDestroy(&own);
    }

    return (const char*)ret;
}

// ---------------------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------------------

// One declaration the current segment has made.
typedef struct LogWireDecl {
    string text;     // channel path, or message template
    flags_t flags;   // channel flags
    bool istmpl;
} LogWireDecl;

typedef struct LogWireDecoder {
    Buffer in;   // bytes received but not yet consumed by a complete frame

    // Segment state, torn down and rebuilt whenever a LOG_WireSegment frame arrives.
    StreamBuffer* sb;
    SerReader* r;
    const uint8* body;   // frame payload the reader is being fed from
    size_t bodylen;
    size_t bodypos;
    bool underrun;       // the reader asked for bytes the payload did not have

    LogWireDecl* chans;
    uint32 nchans;
    LogWireDecl* sites;
    uint32 nsites;

    bool failed;
} LogWireDecoder;

static void wireDeclFree(_Inout_ LogWireDecl** decls, _Inout_ uint32* n)
{
    for (uint32 i = 0; i < *n; i++)
        strDestroy(&(*decls)[i].text);
    xaDestroy(decls);
    *n = 0;
}

// Grows a declaration table to hold `id`, which the sender chose. Ids are dense and start at 1, so
// this is bounded by what the sender actually declares.
static _Ret_opt_valid_ LogWireDecl* wireDeclSlot(_Inout_ LogWireDecl** decls, _Inout_ uint32* n,
                                                 uint32 id)
{
    if (id == 0 || id > LOG_WIRE_DICT_MAX)
        return NULL;

    if (id > *n) {
        xaResize(decls, sizeof(LogWireDecl) * id);
        memset(*decls + *n, 0, sizeof(LogWireDecl) * (id - *n));
        *n = id;
    }

    return &(*decls)[id - 1];
}

static size_t wireDecPull(_Pre_valid_ StreamBuffer* sb, uint8* buf, size_t sz, void* ctx)
{
    LogWireDecoder* dec = (LogWireDecoder*)ctx;

    if (sz == 0)
        return 0;   // a status query, not a request for data

    size_t avail = dec->bodylen - dec->bodypos;
    if (avail == 0) {
        // The reader wants more of a value than the frame that declared its own length actually
        // holds, so the frame is truncated. Erroring the stream is what stops sbufCRead() from
        // calling this forever waiting for bytes that are never coming.
        dec->underrun = true;
        sbufError(sb);
        return 0;
    }

    size_t n = min(sz, avail);
    memcpy(buf, dec->body + dec->bodypos, n);
    dec->bodypos += n;
    return n;
}

static void wireDecCloseSegment(_Inout_ LogWireDecoder* dec)
{
    if (dec->r)
        serReaderDestroy(&dec->r);   // finishes the consumer side
    if (dec->sb) {
        sbufPFinish(dec->sb);        // ...and this the producer side registered below
        sbufRelease(&dec->sb);       // ...and this the decoder's own
        dec->sb = NULL;
    }

    wireDeclFree(&dec->chans, &dec->nchans);
    wireDeclFree(&dec->sites, &dec->nsites);
    dec->body     = NULL;
    dec->bodylen  = 0;
    dec->bodypos  = 0;
    dec->underrun = false;
}

static bool wireDecOpenSegment(_Inout_ LogWireDecoder* dec, _In_reads_bytes_(len) const uint8* body,
                               size_t len)
{
    wireDecCloseSegment(dec);

    dec->sb = sbufCreate(4096);
    if (!sbufPRegisterPull(dec->sb, wireDecPull, NULL, dec)) {
        sbufRelease(&dec->sb);
        return false;
    }

    // The reader consumes the document header as it is created, and the segment frame's payload is
    // exactly that header.
    dec->body    = body;
    dec->bodylen = len;
    dec->bodypos = 0;

    dec->r = serBinaryReaderCreate(dec->sb, 0);
    if (dec->r->err.code != SER_Err_None) {
        wireDecCloseSegment(dec);
        return false;
    }

    return true;
}

// Reads one variant written by wireEncValue().
static bool wireDecValue(_Inout_ LogWireDecoder* dec, _Inout_ stvar* out, _In_opt_z_ const char* key)
{
    SerReader* r = dec->r;

    uint64 kind = 0;
    if (!serReadUint(r, &kind, stType(uint8)))
        return false;

    if (kind == LOG_WireArgPlain) {
        if (!_serRead(r, stExt(stvar), stArgPtr(stvar, out)))
            return false;
        out->_key = key;
        return true;
    }

    if (kind != LOG_WireArgText)
        return false;

    // A value whose type could not cross came over as its rendering. Wrapping it back into a
    // LogSnapshot rather than leaving it a bare string is what keeps an ${object} placeholder in
    // the template resolving, since a placeholder matches by type.
    string txt = 0;
    if (!serReadStr(r, &txt)) {
        strDestroy(&txt);
        return false;
    }

    LogSnapshot* snap = logsnapshotCreate(txt);
    _stvarInitK(out, stType(object), stArg(object, snap), key);
    objRelease(&snap);
    strDestroy(&txt);
    return true;
}

static void wireDecFreeVars(_Inout_ stvar* vars, int n)
{
    for (int i = 0; i < n; i++)
        stvarDestroy(&vars[i]);
    xaFree(vars);
}

// Reads a keyed collection -- the keyed arguments or the context fields -- into a fresh array.
static bool wireDecKeyed(_Inout_ LogWireDecoder* dec, _Outptr_result_maybenull_ stvar** out,
                         _Out_ int* nout)
{
    *out  = NULL;
    *nout = 0;

    int32 count = 0;
    if (!serMapBeginR(dec->r, &count))
        return false;
    if (count < 0 || count > LOG_WIRE_DICT_MAX)
        return false;

    stvar* vars = (count > 0) ? xaAlloc(sizeof(stvar) * (size_t)count, XA_Zero) : NULL;
    int n       = 0;
    string key  = 0;
    bool ok     = true;

    while (ok && serMapNext(dec->r, &key)) {
        if (n >= count) {
            ok = false;
            break;
        }
        ok = wireDecValue(dec, &vars[n], logWireInternKey(key));
        if (ok)
            ++n;
    }
    strDestroy(&key);

    ok = ok && serMapEndR(dec->r);
    if (!ok) {
        wireDecFreeVars(vars, n);
        return false;
    }

    *out  = vars;
    *nout = n;
    return true;
}

static bool wireDecUnkeyed(_Inout_ LogWireDecoder* dec, _Outptr_result_maybenull_ stvar** out,
                           _Out_ int* nout)
{
    *out  = NULL;
    *nout = 0;

    int32 count = 0;
    if (!serArrBeginR(dec->r, &count))
        return false;
    if (count < 0 || count > LOG_WIRE_DICT_MAX)
        return false;

    stvar* vars = (count > 0) ? xaAlloc(sizeof(stvar) * (size_t)count, XA_Zero) : NULL;
    int n       = 0;
    bool ok     = true;

    while (ok && serArrNext(dec->r)) {
        if (n >= count) {
            ok = false;
            break;
        }
        ok = wireDecValue(dec, &vars[n], NULL);
        if (ok)
            ++n;
    }

    ok = ok && serArrEndR(dec->r);
    if (!ok) {
        wireDecFreeVars(vars, n);
        return false;
    }

    *out  = vars;
    *nout = n;
    return true;
}

static bool wireDecChanDecl(_Inout_ LogWireDecoder* dec)
{
    int32 count = 0;
    if (!serMapBeginR(dec->r, &count))
        return false;

    uint32 id    = 0;
    string path  = 0;
    uint64 flags = 0;
    string key   = 0;
    bool ok      = true;

    while (ok && serMapNext(dec->r, &key)) {
        uint64 uv;
        int64 iv;
        if (strEq(key, kwId)) {
            ok = serReadUint(dec->r, &uv, stType(uint32));
            id = (uint32)uv;
        } else if (strEq(key, kwPath)) {
            ok = serReadStr(dec->r, &path);
        } else if (strEq(key, kwFlags)) {
            ok    = serReadUint(dec->r, &flags, stType(uint32));
        } else if (strEq(key, kwMinLevel)) {
            ok = serReadInt(dec->r, &iv, stType(int32));
        } else {
            ok = serSkip(dec->r);   // a field this build does not know about
        }
    }
    strDestroy(&key);
    ok = ok && serMapEndR(dec->r);

    LogWireDecl* slot = ok ? wireDeclSlot(&dec->chans, &dec->nchans, id) : NULL;
    if (slot) {
        strDup(&slot->text, path);
        slot->flags = (flags_t)flags;
    }

    strDestroy(&path);
    return ok && slot != NULL;
}

static bool wireDecSiteDecl(_Inout_ LogWireDecoder* dec)
{
    int32 count = 0;
    if (!serMapBeginR(dec->r, &count))
        return false;

    uint32 id   = 0;
    string tmpl = 0;
    bool istmpl = false;
    string key  = 0;
    bool ok     = true;

    while (ok && serMapNext(dec->r, &key)) {
        uint64 uv;
        if (strEq(key, kwId)) {
            ok = serReadUint(dec->r, &uv, stType(uint32));
            id = (uint32)uv;
        } else if (strEq(key, kwTmpl)) {
            ok = serReadStr(dec->r, &tmpl);
        } else if (strEq(key, kwIsTmpl)) {
            ok = serReadBool(dec->r, &istmpl);
        } else {
            ok = serSkip(dec->r);
        }
    }
    strDestroy(&key);
    ok = ok && serMapEndR(dec->r);

    LogWireDecl* slot = ok ? wireDeclSlot(&dec->sites, &dec->nsites, id) : NULL;
    if (slot) {
        strDup(&slot->text, tmpl);
        slot->istmpl = istmpl;
    }

    strDestroy(&tmpl);
    return ok && slot != NULL;
}

static bool wireDecEntry(_Inout_ LogWireDecoder* dec, _In_ LogWireFrameCB cb, _In_opt_ void* ctx)
{
    int32 count = 0;
    if (!serMapBeginR(dec->r, &count))
        return false;

    LogWireRecord rec = { .trigger = -1, .sample = 1 };
    string origin = 0, tmpl = 0, key = 0;
    stvar *args = NULL, *kargs = NULL, *cvars = NULL;
    int nargs = 0, nkargs = 0, ncvars = 0;
    uint32 chanid = 0, siteid = 0;
    bool istmpl   = false;
    bool ok       = true;

    while (ok && serMapNext(dec->r, &key)) {
        uint64 uv;
        int64 iv;
        if (strEq(key, kwChan)) {
            ok     = serReadUint(dec->r, &uv, stType(uint32));
            chanid = (uint32)uv;
        } else if (strEq(key, kwLevel)) {
            ok        = serReadInt(dec->r, &iv, stType(int32));
            rec.level = (int)iv;
        } else if (strEq(key, kwTs)) {
            ok            = serReadInt(dec->r, &iv, stType(int64));
            rec.timestamp = iv;
        } else if (strEq(key, kwSeq)) {
            ok      = serReadUint(dec->r, &uv, stType(uint64));
            rec.seq = uv;
        } else if (strEq(key, kwBatch)) {
            ok          = serReadUint(dec->r, &uv, stType(uint32));
            rec.batchid = (uint32)uv;
        } else if (strEq(key, kwSample)) {
            ok         = serReadUint(dec->r, &uv, stType(uint32));
            rec.sample = (uint32)uv;
        } else if (strEq(key, kwTrigger)) {
            ok          = serReadInt(dec->r, &iv, stType(int32));
            rec.trigger = (int)iv;
        } else if (strEq(key, kwHops)) {
            ok       = serReadUint(dec->r, &uv, stType(uint32));
            rec.hops = (uv > 255) ? 255 : (uint8)uv;
        } else if (strEq(key, kwOrigin)) {
            ok = serReadStr(dec->r, &origin);
        } else if (strEq(key, kwSite)) {
            ok     = serReadUint(dec->r, &uv, stType(uint32));
            siteid = (uint32)uv;
        } else if (strEq(key, kwTmpl)) {
            ok = serReadStr(dec->r, &tmpl);
        } else if (strEq(key, kwIsTmpl)) {
            ok = serReadBool(dec->r, &istmpl);
        } else if (strEq(key, kwArgs)) {
            ok = wireDecUnkeyed(dec, &args, &nargs);
        } else if (strEq(key, kwKArgs)) {
            ok = wireDecKeyed(dec, &kargs, &nkargs);
        } else if (strEq(key, kwCtx)) {
            ok = wireDecKeyed(dec, &cvars, &ncvars);
        } else {
            ok = serSkip(dec->r);
        }
    }
    strDestroy(&key);
    ok = ok && serMapEndR(dec->r);

    // A record naming a channel or site the segment never declared is a broken stream, not a
    // record with a missing field.
    if (ok && (chanid == 0 || chanid > dec->nchans))
        ok = false;
    if (ok && siteid > dec->nsites)
        ok = false;

    // The two argument sets are separate on the wire because a keyed argument is invisible to an
    // unkeyed placeholder, so their relative order carries nothing. They are merged back into one
    // array because that is what a LogRecord holds.
    stvar* merged = NULL;
    int nmerged   = 0;
    if (ok && (nargs + nkargs) > 0) {
        merged = xaAlloc(sizeof(stvar) * (size_t)(nargs + nkargs), XA_Zero);
        for (int i = 0; i < nargs; i++)
            merged[nmerged++] = args[i];
        for (int i = 0; i < nkargs; i++)
            merged[nmerged++] = kargs[i];
    }

    if (ok) {
        rec.chanpath  = dec->chans[chanid - 1].text;
        rec.chanflags = dec->chans[chanid - 1].flags;
        rec.origin    = strEmpty(origin) ? NULL : origin;
        rec.msgtmpl   = siteid ? dec->sites[siteid - 1].text : tmpl;
        rec.istmpl    = siteid ? dec->sites[siteid - 1].istmpl : istmpl;
        rec.args      = merged;
        rec.nargs     = nmerged;
        rec.ctx       = cvars;
        rec.nctx      = ncvars;

        LogWireFrame frame = { .kind = LOG_WireEntry, .rec = &rec };
        ok                 = cb(&frame, ctx);
    }

    // the merged array borrows its elements from the two it was built from, so only those own
    xaFree(merged);
    wireDecFreeVars(args, nargs);
    wireDecFreeVars(kargs, nkargs);
    wireDecFreeVars(cvars, ncvars);
    strDestroy(&tmpl);
    strDestroy(&origin);
    return ok;
}

static bool wireDecGap(_Inout_ LogWireDecoder* dec, _In_ LogWireFrameCB cb, _In_opt_ void* ctx)
{
    int32 count = 0;
    if (!serMapBeginR(dec->r, &count))
        return false;

    LogWireGap gap = { 0 };
    string key     = 0;
    bool ok        = true;

    while (ok && serMapNext(dec->r, &key)) {
        uint64 uv;
        if (strEq(key, kwCount)) {
            ok        = serReadUint(dec->r, &uv, stType(uint64));
            gap.count = uv;
        } else if (strEq(key, kwFirst)) {
            ok           = serReadUint(dec->r, &uv, stType(uint64));
            gap.firstseq = uv;
        } else if (strEq(key, kwLast)) {
            ok          = serReadUint(dec->r, &uv, stType(uint64));
            gap.lastseq = uv;
        } else {
            ok = serSkip(dec->r);
        }
    }
    strDestroy(&key);
    ok = ok && serMapEndR(dec->r);

    if (!ok)
        return false;

    LogWireFrame frame = { .kind = LOG_WireGap, .gap = &gap };
    return cb(&frame, ctx);
}

static bool wireDecSub(_Inout_ LogWireDecoder* dec, _In_ LogWireFrameCB cb, _In_opt_ void* ctx)
{
    int32 count = 0;
    if (!serMapBeginR(dec->r, &count))
        return false;

    LogSubSpec spec = { .maxlevel = -1 };
    saInit(&spec.patterns, string, 4);
    string key = 0;
    bool ok    = true;

    while (ok && serMapNext(dec->r, &key)) {
        int64 iv;
        if (strEq(key, kwLevel)) {
            ok            = serReadInt(dec->r, &iv, stType(int32));
            spec.maxlevel = (int)iv;
        } else if (strEq(key, kwExpiry)) {
            ok          = serReadInt(dec->r, &iv, stType(int64));
            spec.expiry = iv;
        } else if (strEq(key, kwPatterns)) {
            int32 npat = 0;
            ok         = serArrBeginR(dec->r, &npat);
            if (ok && (npat < 0 || npat > LOG_WIRE_DICT_MAX))
                ok = false;
            string pat = 0;
            while (ok && serArrNext(dec->r)) {
                ok = serReadStr(dec->r, &pat);
                if (ok)
                    saPush(&spec.patterns, string, pat);
            }
            strDestroy(&pat);
            ok = ok && serArrEndR(dec->r);
        } else {
            ok = serSkip(dec->r);
        }
    }
    strDestroy(&key);
    ok = ok && serMapEndR(dec->r);

    if (ok) {
        LogWireFrame frame = { .kind = LOG_WireSubscribe, .sub = &spec };
        ok                 = cb(&frame, ctx);
    }

    saDestroy(&spec.patterns);
    return ok;
}

static bool wireDecCatalog(_Inout_ LogWireDecoder* dec, _In_ LogWireFrameCB cb, _In_opt_ void* ctx)
{
    int32 count = 0;
    if (!serMapBeginR(dec->r, &count))
        return false;

    LogWireChanInfo* chans = NULL;
    sa_string paths;
    saInit(&paths, string, 16);
    int nchans = 0;
    string key = 0;
    bool ok    = true;

    while (ok && serMapNext(dec->r, &key)) {
        if (!strEq(key, kwChans)) {
            ok = serSkip(dec->r);
            continue;
        }

        int32 n = 0;
        ok      = serArrBeginR(dec->r, &n);
        if (ok && (n < 0 || n > LOG_WIRE_DICT_MAX))
            ok = false;
        if (ok && n > 0)
            chans = xaAlloc(sizeof(LogWireChanInfo) * (size_t)n, XA_Zero);

        while (ok && serArrNext(dec->r)) {
            if (nchans >= n) {
                ok = false;
                break;
            }

            int32 nf = 0;
            ok       = serMapBeginR(dec->r, &nf);

            string path  = 0, ckey = 0;
            uint64 flags = 0;
            int64 lvl    = -1;
            while (ok && serMapNext(dec->r, &ckey)) {
                if (strEq(ckey, kwPath))
                    ok = serReadStr(dec->r, &path);
                else if (strEq(ckey, kwFlags))
                    ok = serReadUint(dec->r, &flags, stType(uint32));
                else if (strEq(ckey, kwMinLevel))
                    ok = serReadInt(dec->r, &lvl, stType(int32));
                else
                    ok = serSkip(dec->r);
            }
            strDestroy(&ckey);
            ok = ok && serMapEndR(dec->r);

            if (ok) {
                // The path strings outlive the loop because the array owns them; the descriptors
                // only borrow, exactly as the callback does.
                saPush(&paths, string, path);
                chans[nchans].path     = paths.a[saSize(paths) - 1];
                chans[nchans].flags    = (flags_t)flags;
                chans[nchans].maxlevel = (int)lvl;
                nchans++;
            }
            strDestroy(&path);
        }
        ok = ok && serArrEndR(dec->r);
    }
    strDestroy(&key);
    ok = ok && serMapEndR(dec->r);

    if (ok) {
        LogWireCatalog cat = { .chans = chans, .nchans = nchans };
        LogWireFrame frame = { .kind = LOG_WireCatalog, .cat = &cat };
        ok                 = cb(&frame, ctx);
    }

    xaFree(chans);
    saDestroy(&paths);
    return ok;
}

// Runs one complete frame body through the segment's reader.
static bool wireDecFrame(_Inout_ LogWireDecoder* dec, int kind,
                         _In_reads_bytes_(len) const uint8* body, size_t len,
                         _In_ LogWireFrameCB cb, _In_opt_ void* ctx)
{
    if (kind == LOG_WireSegment)
        return wireDecOpenSegment(dec, body, len);

    if (!dec->r) {
        // Everything but a segment frame refers to declarations and a dictionary that only a
        // segment can establish.
        return false;
    }

    dec->body    = body;
    dec->bodylen = len;
    dec->bodypos = 0;

    bool ok;
    switch (kind) {
    case LOG_WireChanDecl:
        ok = wireDecChanDecl(dec);
        break;
    case LOG_WireSiteDecl:
        ok = wireDecSiteDecl(dec);
        break;
    case LOG_WireEntry:
        ok = wireDecEntry(dec, cb, ctx);
        break;
    case LOG_WireGap:
        ok = wireDecGap(dec, cb, ctx);
        break;
    case LOG_WireSubscribe:
        ok = wireDecSub(dec, cb, ctx);
        break;
    case LOG_WireCatalog:
        ok = wireDecCatalog(dec, cb, ctx);
        break;
    default: {
        // A kind this build has no decoder for. The framing already said how long it was, so
        // stepping over it costs nothing and keeps an older receiver working against a newer
        // sender. Its payload is still part of the serbinary stream, so it has to be consumed.
        ok = serSkip(dec->r);
        if (ok) {
            LogWireFrame frame = { .kind = kind };
            ok                 = cb(&frame, ctx);
        }
        break;
    }
    }

    // Trailing bytes inside a frame the reader did not consume mean the sender and this decoder
    // disagree about the payload, which is a broken stream rather than a recoverable record.
    if (ok && dec->bodypos != dec->bodylen)
        ok = false;

    dec->body    = NULL;
    dec->bodylen = 0;
    dec->bodypos = 0;
    return ok && !dec->underrun;
}

_Use_decl_annotations_
LogWireDecoder* logWireDecoderCreate(void)
{
    return xaAllocStruct(LogWireDecoder, XA_Zero);
}

_Use_decl_annotations_
void logWireDecoderDestroy(LogWireDecoder** pdec)
{
    LogWireDecoder* dec = *pdec;
    if (!dec)
        return;
    *pdec = NULL;

    wireDecCloseSegment(dec);
    bufDestroy(&dec->in);
    xaFree(dec);
}

_Use_decl_annotations_
bool logWireDecode(LogWireDecoder* dec, const uint8* buf, size_t len, LogWireFrameCB cb, void* ctx)
{
    if (dec->failed)
        return false;

    if (len > 0)
        bufAppendBytes(&dec->in, buf, len);

    // Nothing buffered means no frame can be waiting. It also keeps everything below off a NULL
    // buffer, which is what a decoder that has not been handed any bytes yet still has.
    if (bufLen(dec->in) == 0)
        return true;

    size_t pos = 0;
    for (;;) {
        size_t hdr = pos;
        uint64 kind, flen;
        bool bad = false;

        if (!wireVarintRead(dec->in->data, dec->in->len, &hdr, &kind, &bad)) {
            if (bad)
                dec->failed = true;
            break;
        }
        if (!wireVarintRead(dec->in->data, dec->in->len, &hdr, &flen, &bad)) {
            if (bad)
                dec->failed = true;
            break;
        }

        if (flen > LOG_WIRE_FRAME_MAX || kind > INT32_MAX) {
            dec->failed = true;
            break;
        }
        if (dec->in->len - hdr < flen)
            break;   // the rest of the payload has not arrived yet

        if (!wireDecFrame(dec, (int)kind, dec->in->data + hdr, (size_t)flen, cb, ctx)) {
            dec->failed = true;
            break;
        }

        pos = hdr + (size_t)flen;
    }

    // whatever is left is the start of a frame that is still arriving
    if (pos > 0) {
        memmove(dec->in->data, dec->in->data + pos, dec->in->len - pos);
        dec->in->len -= pos;
    }

    return !dec->failed;
}
