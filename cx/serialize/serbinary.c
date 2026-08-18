// Binary backend.
//
// Every node has an exact representation here, unlike serjson.c, so there's no projection
// table -- just a byte grammar, split into a write half and a read half below. See
// serbinary.h for the on-disk encoding.
//
// Single-pass in both directions: the writer never seeks back, the reader never rewinds.

#include "cx/serialize/serbinary.h"

#include "cx/container/foreach.h"
#include "cx/container/hashtable.h"
#include "cx/container/sarray.h"
#include "cx/format.h"
#include "cx/string.h"
#include "cx/xalloc/xalloc.h"

// Node tags. Only a value carries one; a map key is always a dictionary reference at a position
// the reader already knows, so it needs no tag of its own.
enum BinTag {
    BIN_Null    = 0x00,
    BIN_False   = 0x01,
    BIN_True    = 0x02,
    BIN_Int     = 0x03,   // zigzag varint
    BIN_Uint    = 0x04,   // varint
    BIN_Float32 = 0x05,   // 4 bytes LE
    BIN_Float64 = 0x06,   // 8 bytes LE
    BIN_Str     = 0x07,   // varint length + utf8
    BIN_StrDict = 0x08,   // dictref
    BIN_Bytes   = 0x09,   // varint length + raw
    BIN_Array   = 0x0a,   // varint count, then values
    BIN_Map     = 0x0b,   // varint count, then dictref + value pairs
    BIN_TypeTag = 0x0c,   // dictref, then the value it annotates
    BIN_RefDef  = 0x0d,   // varint id, then the value
    BIN_RefUse  = 0x0e,   // varint id
};

#define BIN_HDR_SIZE 6

// A dictionary reference of 0 means a definition follows and takes the next id, so a real id is
// always encoded one higher. One encoding for define and reference is what keeps map keys and
// interned strings free of a tag byte.
#define BIN_DICT_DEF 0

// Zigzag, so that a small negative number stays a small varint.
_meta_inline uint64 zigzag(int64 v) { return ((uint64)v << 1) ^ (uint64)(v >> 63); }
_meta_inline int64 unzigzag(uint64 v) { return (int64)(v >> 1) ^ -(int64)(v & 1); }

// ---------------------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------------------

typedef struct SerBinWriter {
    SerWriter w;   // must be first
    StreamBuffer* sb;
    hashtable dict;   // string -> uint32, ids assigned in order of first definition
    bool tags;        // self-describing: values carry a tag byte
    bool strdedup;    // string values intern rather than going out inline
    bool streamdone;  // the producer side of the stream has been finished
} SerBinWriter;

// Errors are sticky: once one is recorded every subsequent write is a no-op, so a chain of
// emitters can be written without a check between each one.
static bool bwRaw(_Inout_ SerBinWriter* bw, _In_reads_bytes_(n) const void* p, size_t n)
{
    if (bw->w.err.code != SER_Err_None)
        return false;
    if (n == 0)
        return true;

    if (!sbufPWrite(bw->sb, (const uint8*)p, n))
        return serWriterFail(&bw->w, SER_Err_Backend, _SL("stream write failed"));

    return true;
}

static bool bwByte(_Inout_ SerBinWriter* bw, uint8 b) { return bwRaw(bw, &b, 1); }

// A tag the reader always needs.
static bool bwTag(_Inout_ SerBinWriter* bw, uint8 tag) { return bwByte(bw, tag); }

// A tag the schema makes redundant, and that compact mode therefore drops. This is exactly the
// set of nodes the traverser never peeks at; see serbinary.h.
static bool bwOptTag(_Inout_ SerBinWriter* bw, uint8 tag)
{
    return bw->tags ? bwByte(bw, tag) : true;
}

static bool bwVarint(_Inout_ SerBinWriter* bw, uint64 v)
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

    return bwRaw(bw, buf, n);
}

// Fixed-width scalars go out byte by byte from the value's bit pattern rather than by copying
// its storage, so the encoding is little-endian on a big-endian host too.
static bool bwFixed(_Inout_ SerBinWriter* bw, uint64 bits, int nbytes)
{
    uint8 buf[8];
    for (int i = 0; i < nbytes; i++)
        buf[i] = (uint8)(bits >> (i * 8));
    return bwRaw(bw, buf, nbytes);
}

static bool bwStrBytes(_Inout_ SerBinWriter* bw, _In_opt_ strref s)
{
    // A rope holds its bytes in pieces; iterating is how they are reached without flattening.
    foreach (string, si, s) {
        bwRaw(bw, si.bytes, si.len);
    }
    return bw->w.err.code == SER_Err_None;
}

static bool bwDictRef(_Inout_ SerBinWriter* bw, _In_opt_ strref s)
{
    uint32 id;
    if (htFind(bw->dict, string, (string)s, uint32, &id))
        return bwVarint(bw, (uint64)id + 1);

    id = (uint32)htSize(bw->dict);
    htInsert(&bw->dict, string, (string)s, uint32, id);

    return bwVarint(bw, BIN_DICT_DEF) && bwVarint(bw, strLen(s)) && bwStrBytes(bw, s);
}

static bool binWNull(SerWriter* w) { return bwTag((SerBinWriter*)w, BIN_Null); }

static bool binWBool(SerWriter* w, bool v)
{
    // The tag is the whole value, so compact mode has nothing to save here and keeps it.
    return bwTag((SerBinWriter*)w, v ? BIN_True : BIN_False);
}

static bool binWInt(SerWriter* w, int64 v, stype declared)
{
    SerBinWriter* bw = (SerBinWriter*)w;

    // Zigzag spends a bit on the sign, so a signed value that is not actually negative is
    // cheaper written unsigned. Only worth doing when there is a tag to say which it was;
    // compact mode has to stay predictable for the reader, so it always zigzags.
    if (bw->tags && v >= 0)
        return bwTag(bw, BIN_Uint) && bwVarint(bw, (uint64)v);

    return bwOptTag(bw, BIN_Int) && bwVarint(bw, zigzag(v));
}

static bool binWUint(SerWriter* w, uint64 v, stype declared)
{
    SerBinWriter* bw = (SerBinWriter*)w;
    return bwOptTag(bw, BIN_Uint) && bwVarint(bw, v);
}

static bool binWReal(SerWriter* w, float64 v, stype declared)
{
    SerBinWriter* bw = (SerBinWriter*)w;

    // The declared width is what decides the encoding, which is also why the read side takes
    // the declared type: in compact mode there is no tag to say whether 4 bytes or 8 follow.
    if (declared && stGetSize(declared) == 4) {
        float32 f = (float32)v;
        uint32 bits;
        memcpy(&bits, &f, 4);
        return bwOptTag(bw, BIN_Float32) && bwFixed(bw, bits, 4);
    }

    uint64 bits;
    memcpy(&bits, &v, 8);
    return bwOptTag(bw, BIN_Float64) && bwFixed(bw, bits, 8);
}

static bool binWStr(SerWriter* w, strref v)
{
    SerBinWriter* bw = (SerBinWriter*)w;

    if (bw->strdedup)
        return bwTag(bw, BIN_StrDict) && bwDictRef(bw, v);

    return bwTag(bw, BIN_Str) && bwVarint(bw, strLen(v)) && bwStrBytes(bw, v);
}

static bool binWBytes(SerWriter* w, const void* p, size_t n)
{
    SerBinWriter* bw = (SerBinWriter*)w;
    return bwOptTag(bw, BIN_Bytes) && bwVarint(bw, n) && bwRaw(bw, p, n);
}

// A count is not optional here: it is what lets the reader answer arrNext/mapNext without
// lookahead, and the traverser always has one to give.
static bool bwCounted(_Inout_ SerBinWriter* bw, uint8 tag, int32 count)
{
    if (count < 0)
        return serWriterFail(&bw->w,
                             SER_Err_Unsupported,
                             _SL("the binary format needs a container count up front"));

    return bwTag(bw, tag) && bwVarint(bw, (uint64)count);
}

static bool binWArrBegin(SerWriter* w, int32 count)
{
    return bwCounted((SerBinWriter*)w, BIN_Array, count);
}

// The count told the reader how many elements to expect, so there is nothing to close.
static bool binWArrEnd(SerWriter* w) { return w->err.code == SER_Err_None; }

static bool binWMapBegin(SerWriter* w, int32 count)
{
    return bwCounted((SerBinWriter*)w, BIN_Map, count);
}

static bool binWMapKey(SerWriter* w, strref key) { return bwDictRef((SerBinWriter*)w, key); }

static bool binWMapKeyTyped(SerWriter* w, const STypeInfoExt* kt, stgeneric key)
{
    // Encoding one would be easy; reading it back is not, because SerReaderOps has no typed
    // counterpart to mapNext. Not advertising the capability sends the traverser down the
    // pair-array projection, which both directions already handle.
    return serWriterFail(w, SER_Err_Unsupported, _SL("binary map keys are strings"));
}

static bool binWMapEnd(SerWriter* w) { return w->err.code == SER_Err_None; }

static bool binWTypeTag(SerWriter* w, const STypeInfoExt* st)
{
    SerBinWriter* bw = (SerBinWriter*)w;

    if (!st->name)
        return serWriterFail(w, SER_Err_Type, _SL("cannot tag a value with an unnamed type"));

    // The tag byte is kept even in compact mode: a tagged value is by definition one the schema
    // does not pin down, so the reader has to be able to peek at this position.
    return bwTag(bw, BIN_TypeTag) && bwDictRef(bw, st->name);
}

// Both keep their tag byte in compact mode, on the same rule the type tag follows: a position
// that may hold a reference instead of the value the schema names is one the reader has to peek
// at, and there would be nothing there to peek at without the tag.
static bool binWRefDef(SerWriter* w, uint32 id)
{
    SerBinWriter* bw = (SerBinWriter*)w;
    return bwTag(bw, BIN_RefDef) && bwVarint(bw, id);
}

static bool binWRefUse(SerWriter* w, uint32 id)
{
    SerBinWriter* bw = (SerBinWriter*)w;
    return bwTag(bw, BIN_RefUse) && bwVarint(bw, id);
}

static void bwStreamDone(_Inout_ SerBinWriter* bw)
{
    if (bw->streamdone)
        return;
    bw->streamdone = true;
    sbufPFinish(bw->sb);
}

static bool binWFinish(SerWriter* w)
{
    SerBinWriter* bw = (SerBinWriter*)w;
    bwStreamDone(bw);
    return w->err.code == SER_Err_None;
}

static void binWDestroy(SerWriter* w)
{
    SerBinWriter* bw = (SerBinWriter*)w;

    // A writer destroyed without being finished still has to release the producer side, even
    // though the document it produced is truncated.
    bwStreamDone(bw);

    htDestroy(&bw->dict);
    xaFree(bw);
}

static const SerWriterOps binWriterOps = {
    .writeNull   = binWNull,
    .writeBool   = binWBool,
    .writeInt    = binWInt,
    .writeUint   = binWUint,
    .writeReal   = binWReal,
    .writeStr    = binWStr,
    .writeBytes  = binWBytes,
    .arrBegin    = binWArrBegin,
    .arrEnd      = binWArrEnd,
    .mapBegin    = binWMapBegin,
    .mapKey      = binWMapKey,
    .mapKeyTyped = binWMapKeyTyped,
    .mapEnd      = binWMapEnd,
    .typeTag     = binWTypeTag,
    .refDef      = binWRefDef,
    .refUse      = binWRefUse,
    .finish      = binWFinish,
    .destroy     = binWDestroy,
};

_Use_decl_annotations_
SerWriter* serBinaryWriterCreate(StreamBuffer* sb, flags_t flags)
{
    // No SER_Cap_Skip: skipping is something a reader does, and whether this document will be
    // skippable is what the compact flag and the header bit already say.
    flags_t caps =
        SER_Cap_Bytes | SER_Cap_ExactInt | SER_Cap_Sizes | SER_Cap_TypeTags | SER_Cap_Refs;

    SerBinWriter* bw =
        (SerBinWriter*)_serWriterAlloc(sizeof(SerBinWriter), &binWriterOps, caps, flags);

    bw->sb       = sb;
    bw->tags     = !(flags & SER_Bin_Compact);
    bw->strdedup = !(flags & SER_Bin_NoStringDedup);
    htInit(&bw->dict, string, uint32, 32);

    if (!sbufPRegisterPush(sb, NULL, NULL)) {
        serWriterFail(&bw->w, SER_Err_Backend, _SL("could not register with the stream buffer"));
        bw->streamdone = true;   // nothing was registered, so nothing may be finished
        return &bw->w;
    }

    uint8 hdr[BIN_HDR_SIZE];
    memcpy(hdr, SER_BIN_MAGIC, 4);
    hdr[4] = SER_BIN_VERSION;
    hdr[5] = (uint8)((bw->tags ? SER_BinHdr_SelfDescribing : 0) |
                     (bw->strdedup ? SER_BinHdr_StringDedup : 0) |
                     ((flags & SER_Refs) ? SER_BinHdr_Refs : 0));
    bwRaw(bw, hdr, BIN_HDR_SIZE);

    return &bw->w;
}

// ---------------------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------------------

typedef struct SerBinReader {
    SerReader r;   // must be first
    StreamBuffer* sb;
    sa_string dict;    // ids in order of definition, so a reference is an index
    sa_int32 counts;   // remaining entries per open container, innermost last
    bool tags;         // taken from the document header, not from the caller's flags
    bool havetag;
    uint8 tag;
    bool streamdone;   // the consumer side of the stream has been finished
} SerBinReader;

static bool brRaw(_Inout_ SerBinReader* br, _Out_writes_bytes_(n) void* p, size_t n)
{
    if (br->r.err.code != SER_Err_None)
        return false;
    if (n == 0)
        return true;

    size_t got = 0;
    if (!sbufCRead(br->sb, (uint8*)p, n, &got) || got != n)
        return serReaderFail(&br->r, SER_Err_Data, _SL("unexpected end of document"));

    return true;
}

static bool brSkipBytes(_Inout_ SerBinReader* br, uint64 n)
{
    uint8 scratch[256];
    while (n > 0) {
        size_t chunk = (n > sizeof(scratch)) ? sizeof(scratch) : (size_t)n;
        if (!brRaw(br, scratch, chunk))
            return false;
        n -= chunk;
    }
    return true;
}

static bool brVarint(_Inout_ SerBinReader* br, _Out_ uint64* out)
{
    uint64 v  = 0;
    int shift = 0;
    uint8 b;

    do {
        if (shift > 63)
            return serReaderFail(&br->r, SER_Err_Data, _SL("malformed varint"));
        if (!brRaw(br, &b, 1))
            return false;
        v |= (uint64)(b & 0x7f) << shift;
        shift += 7;
    } while (b & 0x80);

    *out = v;
    return true;
}

static bool brFixed(_Inout_ SerBinReader* br, _Out_ uint64* out, int nbytes)
{
    uint8 buf[8];
    if (!brRaw(br, buf, nbytes))
        return false;

    uint64 bits = 0;
    for (int i = 0; i < nbytes; i++)
        bits |= (uint64)buf[i] << (i * 8);

    *out = bits;
    return true;
}

// One tag byte of lookahead, held until something consumes it. That is the entirety of the
// reader's buffering, and the reason the format can be decoded straight out of a stream.
static bool brTag(_Inout_ SerBinReader* br, _Out_ uint8* out)
{
    if (!br->havetag) {
        if (!brRaw(br, &br->tag, 1))
            return false;
        br->havetag = true;
    }
    *out = br->tag;
    return true;
}

static void brTakeTag(_Inout_ SerBinReader* br) { br->havetag = false; }

static bool brUnexpected(_Inout_ SerBinReader* br, _In_ strref want)
{
    string msg = 0;

    // Only name a tag if one was actually read. Compact mode reaches here with nothing in the
    // lookahead, and reporting whatever byte was last there would be worse than saying nothing.
    if (br->havetag) {
        strFormat(&msg,
                  _SL("expected ${string}, found tag ${uint}"),
                  stvar(string, (string)want),
                  stvar(uint32, (uint32)br->tag));
    } else {
        strFormat(&msg, _SL("expected ${string}"), stvar(string, (string)want));
    }

    bool ret = serReaderFail(&br->r, SER_Err_Data, msg);
    strDestroy(&msg);
    return ret;
}

// Resolves a dictionary reference, interning the entry if this is its definition. The returned
// string is borrowed from the dictionary and stays valid for the life of the reader.
static bool brDictRef(_Inout_ SerBinReader* br, _Out_ strref* out)
{
    uint64 v;
    if (!brVarint(br, &v))
        return false;

    if (v != BIN_DICT_DEF) {
        if (v - 1 >= (uint64)saSize(br->dict))
            return serReaderFail(&br->r, SER_Err_Data, _SL("dictionary reference out of range"));
        *out = br->dict.a[v - 1];
        return true;
    }

    uint64 len;
    if (!brVarint(br, &len))
        return false;
    if (len > UINT32_MAX)
        return serReaderFail(&br->r, SER_Err_Data, _SL("dictionary entry is too large"));

    string s   = 0;
    uint8* buf = strBuffer(&s, (uint32)len);
    if (!brRaw(br, buf, (size_t)len)) {
        strDestroy(&s);
        return false;
    }
    strSetLen(&s, (uint32)len);

    saPush(&br->dict, string, s);
    strDestroy(&s);

    // Hand back the array's copy rather than the local, so the caller is holding something the
    // dictionary owns for the life of the reader.
    *out = br->dict.a[saSize(br->dict) - 1];
    return true;
}

static SerNodeKind binRPeek(SerReader* r)
{
    SerBinReader* br = (SerBinReader*)r;

    if (r->err.code != SER_Err_None)
        return SER_Invalid;

    // Running out of data at a peek is the end of the document, not a failure -- so ask the
    // stream rather than letting brTag record an error over it.
    if (!br->havetag && sbufCAvail(br->sb) == 0 && !sbufCFeed(br->sb, 1))
        return SER_EOF;

    uint8 t;
    if (!brTag(br, &t))
        return SER_Invalid;

    switch (t) {
    case BIN_Null:
        return SER_Null;
    case BIN_False:
    case BIN_True:
        return SER_Bool;
    case BIN_Int:
        return SER_Int;
    case BIN_Uint:
        return SER_Uint;
    case BIN_Float32:
    case BIN_Float64:
        return SER_Real;
    case BIN_Str:
    case BIN_StrDict:
        return SER_Str;
    case BIN_Bytes:
        return SER_Bytes;
    case BIN_Array:
        return SER_ArrayBegin;
    case BIN_Map:
        return SER_MapBegin;
    case BIN_TypeTag:
        return SER_TypeTag;
    case BIN_RefDef:
        return SER_RefDef;
    case BIN_RefUse:
        return SER_RefUse;
    default:
        return SER_Invalid;
    }
}

// Consumes a tag the reader always expects to find, whatever the mode.
static bool brExpect(_Inout_ SerBinReader* br, uint8 tag, _In_ strref want)
{
    uint8 t;
    if (!brTag(br, &t))
        return false;
    if (t != tag)
        return brUnexpected(br, want);
    brTakeTag(br);
    return true;
}

static bool binRNull(SerReader* r) { return brExpect((SerBinReader*)r, BIN_Null, _S "null"); }

static bool binRBool(SerReader* r, bool* out)
{
    SerBinReader* br = (SerBinReader*)r;

    uint8 t;
    if (!brTag(br, &t))
        return false;
    if (t != BIN_False && t != BIN_True)
        return brUnexpected(br, _S "a boolean");

    *out = (t == BIN_True);
    brTakeTag(br);
    return true;
}

static bool binRInt(SerReader* r, int64* out, stype declared)
{
    SerBinReader* br = (SerBinReader*)r;
    uint64 v;

    if (!br->tags) {
        // Compact mode: the writer always zigzags, because there is no tag to say otherwise.
        if (!brVarint(br, &v))
            return false;
        *out = unzigzag(v);
        return true;
    }

    uint8 t;
    if (!brTag(br, &t))
        return false;

    switch (t) {
    case BIN_Int:
        brTakeTag(br);
        if (!brVarint(br, &v))
            return false;
        *out = unzigzag(v);
        return true;
    case BIN_Uint:
        brTakeTag(br);
        if (!brVarint(br, &v))
            return false;
        if (v > (uint64)INT64_MAX)
            return serReaderFail(r, SER_Err_Overflow, _SL("value does not fit in an int64"));
        *out = (int64)v;
        return true;
    default:
        return brUnexpected(br, _S "an integer");
    }
}

static bool binRUint(SerReader* r, uint64* out, stype declared)
{
    SerBinReader* br = (SerBinReader*)r;
    uint64 v;

    if (!br->tags)
        return brVarint(br, out);

    uint8 t;
    if (!brTag(br, &t))
        return false;

    switch (t) {
    case BIN_Uint:
        brTakeTag(br);
        return brVarint(br, out);
    case BIN_Int: {
        brTakeTag(br);
        if (!brVarint(br, &v))
            return false;
        int64 sv = unzigzag(v);
        if (sv < 0)
            return serReaderFail(r, SER_Err_Overflow, _SL("negative value in an unsigned slot"));
        *out = (uint64)sv;
        return true;
    }
    default:
        return brUnexpected(br, _S "an unsigned integer");
    }
}

static bool binRReal(SerReader* r, float64* out, stype declared)
{
    SerBinReader* br = (SerBinReader*)r;
    uint64 bits;
    int nbytes = 8;

    if (br->tags) {
        uint8 t;
        if (!brTag(br, &t))
            return false;
        if (t != BIN_Float32 && t != BIN_Float64)
            return brUnexpected(br, _S "a real number");
        brTakeTag(br);
        nbytes = (t == BIN_Float32) ? 4 : 8;
    } else if (declared && stGetSize(declared) == 4) {
        nbytes = 4;
    }

    if (!brFixed(br, &bits, nbytes))
        return false;

    if (nbytes == 4) {
        float32 f;
        uint32 b32 = (uint32)bits;
        memcpy(&f, &b32, 4);
        *out = f;
    } else {
        memcpy(out, &bits, 8);
    }

    return true;
}

static bool binRStr(SerReader* r, string* out)
{
    SerBinReader* br = (SerBinReader*)r;

    uint8 t;
    if (!brTag(br, &t))
        return false;

    if (t == BIN_StrDict) {
        brTakeTag(br);
        strref s;
        if (!brDictRef(br, &s))
            return false;
        strDup(out, s);
        return true;
    }

    if (t != BIN_Str)
        return brUnexpected(br, _S "a string");
    brTakeTag(br);

    uint64 len;
    if (!brVarint(br, &len))
        return false;
    if (len > UINT32_MAX)
        return serReaderFail(r, SER_Err_Data, _SL("string is too large"));

    // An output string is replaced, not appended to or grown in place over whatever it held.
    strDestroy(out);
    uint8* buf = strBuffer(out, (uint32)len);
    if (!brRaw(br, buf, (size_t)len))
        return false;
    strSetLen(out, (uint32)len);

    return true;
}

static bool binRBytes(SerReader* r, Buffer* out)
{
    SerBinReader* br = (SerBinReader*)r;

    if (br->tags && !brExpect(br, BIN_Bytes, _S "a byte run"))
        return false;

    uint64 len;
    if (!brVarint(br, &len))
        return false;
    if (len > UINT32_MAX)
        return serReaderFail(r, SER_Err_Data, _SL("byte run is too large"));

    bufDestroy(out);
    *out = bufCreate((uint32)len);
    if (!brRaw(br, (*out)->data, (size_t)len))
        return false;
    (*out)->len = (uint32)len;

    return true;
}

// The count from the container's header, decremented by arrNext/mapNext. Nesting means a stack.
static bool brPushCount(_Inout_ SerBinReader* br, uint8 tag, _Out_ int32* count, _In_ strref want)
{
    *count = 0;

    if (!brExpect(br, tag, want))
        return false;

    uint64 n;
    if (!brVarint(br, &n))
        return false;
    if (n > INT32_MAX)
        return serReaderFail(&br->r, SER_Err_Data, _SL("container is too large"));

    *count = (int32)n;
    saPush(&br->counts, int32, *count);
    return true;
}

static bool brNextIn(_Inout_ SerBinReader* br)
{
    if (br->r.err.code != SER_Err_None)
        return false;

    int32 depth = saSize(br->counts);
    if (depth == 0 || br->counts.a[depth - 1] <= 0)
        return false;

    br->counts.a[depth - 1]--;
    return true;
}

static bool brPopCount(_Inout_ SerBinReader* br)
{
    if (br->r.err.code != SER_Err_None)
        return false;

    int32 depth = saSize(br->counts);
    if (depth == 0)
        return serReaderFail(&br->r, SER_Err_Data, _SL("closed a container that was not open"));

    int32 left = br->counts.a[depth - 1];
    saSetSize(&br->counts, depth - 1);

    if (left != 0)
        return serReaderFail(&br->r, SER_Err_Data, _SL("container has unread entries"));

    return true;
}

static bool binRArrBegin(SerReader* r, int32* count)
{
    return brPushCount((SerBinReader*)r, BIN_Array, count, _S "an array");
}

static bool binRArrNext(SerReader* r) { return brNextIn((SerBinReader*)r); }
static bool binRArrEnd(SerReader* r) { return brPopCount((SerBinReader*)r); }

static bool binRMapBegin(SerReader* r, int32* count)
{
    return brPushCount((SerBinReader*)r, BIN_Map, count, _S "a map");
}

static bool binRMapNext(SerReader* r, string* key)
{
    SerBinReader* br = (SerBinReader*)r;

    if (!brNextIn(br))
        return false;

    strref k = 0;
    if (!brDictRef(br, &k))
        return false;

    strDup(key, k);
    return true;
}

static bool binRMapEnd(SerReader* r) { return brPopCount((SerBinReader*)r); }

static bool binRTypeTag(SerReader* r, string* name)
{
    SerBinReader* br = (SerBinReader*)r;

    if (!brExpect(br, BIN_TypeTag, _S "a type tag"))
        return false;

    strref s;
    if (!brDictRef(br, &s))
        return false;

    strDup(name, s);
    return true;
}

// One op for both, as on the read side of a type tag: the traverser already peeked to learn which
// of the two is here, so all that is left is to consume it.
static bool binRRef(SerReader* r, uint32* id)
{
    SerBinReader* br = (SerBinReader*)r;

    uint8 t;
    if (!brTag(br, &t))
        return false;
    if (t != BIN_RefDef && t != BIN_RefUse)
        return brUnexpected(br, _S "a reference");
    brTakeTag(br);

    uint64 v;
    if (!brVarint(br, &v))
        return false;
    if (v > UINT32_MAX)
        return serReaderFail(r, SER_Err_Data, _SL("reference id is out of range"));

    *id = (uint32)v;
    return true;
}

// Only reachable in self-describing mode -- SER_Cap_Skip is not advertised otherwise, because
// without a tag there is nothing to say how far a value extends.
static bool binRSkip(SerReader* r)
{
    SerBinReader* br = (SerBinReader*)r;

    if (!br->tags)
        return serReaderFail(r,
                             SER_Err_Unsupported,
                             _SL("a compact document cannot skip an unknown value"));

    uint8 t;
    if (!brTag(br, &t))
        return false;
    brTakeTag(br);

    uint64 n;

    switch (t) {
    case BIN_Null:
    case BIN_False:
    case BIN_True:
        return true;

    case BIN_Int:
    case BIN_Uint:
        return brVarint(br, &n);

    case BIN_Float32:
        return brSkipBytes(br, 4);
    case BIN_Float64:
        return brSkipBytes(br, 8);

    case BIN_Str:
    case BIN_Bytes:
        return brVarint(br, &n) && brSkipBytes(br, n);

    case BIN_StrDict: {
        // A definition has to be interned even when its value is thrown away, or every later
        // id in the document is off by one.
        strref s;
        return brDictRef(br, &s);
    }

    case BIN_Array:
        if (!brVarint(br, &n))
            return false;
        for (uint64 i = 0; i < n; i++) {
            if (!binRSkip(r))
                return false;
        }
        return true;

    case BIN_Map:
        if (!brVarint(br, &n))
            return false;
        for (uint64 i = 0; i < n; i++) {
            strref k;
            if (!brDictRef(br, &k) || !binRSkip(r))
                return false;
        }
        return true;

    case BIN_TypeTag: {
        // The name still has to be interned, for the same reason a discarded string definition
        // does, and the value it annotates is a whole node of its own.
        strref s;
        return brDictRef(br, &s) && binRSkip(r);
    }

    case BIN_RefUse:
        return brVarint(br, &n);

    case BIN_RefDef:
        // The definition is lost along with the value it named -- nothing was constructed, so
        // there is nothing to record it against -- but the value is still a node to step over.
        return brVarint(br, &n) && binRSkip(r);

    default:
        return brUnexpected(br, _S "a value to skip over");
    }
}

static void binRDestroy(SerReader* r)
{
    SerBinReader* br = (SerBinReader*)r;

    // Registering as the consumer took a reference on the stream, so failing to finish leaks
    // the whole buffer even though the caller released its own handle.
    if (!br->streamdone) {
        br->streamdone = true;
        sbufCFinish(br->sb);
    }

    saDestroy(&br->dict);
    saDestroy(&br->counts);
    xaFree(br);
}

static const SerReaderOps binReaderOps = {
    .peek        = binRPeek,
    .readNull    = binRNull,
    .readBool    = binRBool,
    .readInt     = binRInt,
    .readUint    = binRUint,
    .readReal    = binRReal,
    .readStr     = binRStr,
    .readBytes   = binRBytes,
    .arrBegin    = binRArrBegin,
    .arrNext     = binRArrNext,
    .arrEnd      = binRArrEnd,
    .mapBegin    = binRMapBegin,
    .mapNext     = binRMapNext,
    .mapEnd      = binRMapEnd,
    .readTypeTag = binRTypeTag,
    .readRef     = binRRef,
    .skip        = binRSkip,
    .destroy     = binRDestroy,
};

// The header is read here rather than lazily, so that the mode is settled before the first op
// and a document that is not one of ours fails at create rather than as a decode error.
static void brReadHeader(_Inout_ SerBinReader* br, flags_t* caps)
{
    uint8 hdr[BIN_HDR_SIZE];
    if (!brRaw(br, hdr, BIN_HDR_SIZE)) {
        serReaderFail(&br->r, SER_Err_Data, _SL("document is too short to hold a header"));
        return;
    }

    if (memcmp(hdr, SER_BIN_MAGIC, 4) != 0) {
        serReaderFail(&br->r, SER_Err_Data, _SL("not a cx binary document"));
        return;
    }

    if (hdr[4] != SER_BIN_VERSION) {
        string msg = 0;
        strFormat(&msg,
                  _SL("binary format version ${uint} is not supported"),
                  stvar(uint32, (uint32)hdr[4]));
        serReaderFail(&br->r, SER_Err_Data, msg);
        strDestroy(&msg);
        return;
    }

    br->tags = (hdr[5] & SER_BinHdr_SelfDescribing) != 0;
    if (br->tags)
        *caps |= SER_Cap_Skip;
}

_Use_decl_annotations_
SerReader* serBinaryReaderCreate(StreamBuffer* sb, flags_t flags)
{
    flags_t caps =
        SER_Cap_Bytes | SER_Cap_ExactInt | SER_Cap_Sizes | SER_Cap_TypeTags | SER_Cap_Refs;

    SerBinReader* br =
        (SerBinReader*)_serReaderAlloc(sizeof(SerBinReader), &binReaderOps, caps, flags);

    br->sb = sb;
    saInit(&br->dict, string, 32);
    saInit(&br->counts, int32, 8);

    if (!sbufCRegisterPull(sb, NULL, NULL)) {
        serReaderFail(&br->r, SER_Err_Backend, _SL("could not register with the stream buffer"));
        br->streamdone = true;   // nothing was registered, so nothing may be finished
        return &br->r;
    }

    brReadHeader(br, &caps);
    br->r.caps = caps;

    return &br->r;
}
