// SSD tree backend.
//
// Has no encoding or parsing step, so a failure here points at the data model or the
// traverser rather than at a format bug. Also the least stream-like backend, which makes it
// a good test of the abstraction.

#include "cx/serialize/serssd.h"

#include "cx/container/sarray.h"
#include "cx/format.h"
#include "cx/ssdtree/node/ssdarraynode.h"
#include "cx/ssdtree/node/ssdhashnode.h"
#include "cx/ssdtree/node/ssdsinglenode.h"
#include "cx/string.h"
#include "cx/xalloc/xalloc.h"

// ---------------------------------------------------------------------------------------
// stvar construction
//
// The data model carries the declared stype alongside every numeric node precisely so that a
// lossless backend can keep the exact width and signedness. SSD stores stvars, so it can.
// ---------------------------------------------------------------------------------------

static void svFromInt(_Out_ stvar* out, int64 v, stype declared)
{
    stgeneric g = { 0 };
    if (!declared)
        declared = stType(int64);

    switch (stGetSize(declared)) {
    case 1:
        g.st_int8 = (int8)v;
        break;
    case 2:
        g.st_int16 = (int16)v;
        break;
    case 4:
        g.st_int32 = (int32)v;
        break;
    default:
        g.st_int64 = v;
        break;
    }
    _stvarInit(out, declared, g);
}

static void svFromUint(_Out_ stvar* out, uint64 v, stype declared)
{
    stgeneric g = { 0 };
    if (!declared)
        declared = stType(uint64);

    switch (stGetSize(declared)) {
    case 1:
        g.st_uint8 = (uint8)v;
        break;
    case 2:
        g.st_uint16 = (uint16)v;
        break;
    case 4:
        g.st_uint32 = (uint32)v;
        break;
    default:
        g.st_uint64 = v;
        break;
    }
    _stvarInit(out, declared, g);
}

static void svFromReal(_Out_ stvar* out, float64 v, stype declared)
{
    stgeneric g = { 0 };
    if (!declared)
        declared = stType(float64);

    if (stGetSize(declared) == 4)
        g.st_float32 = (float32)v;
    else
        g.st_float64 = v;

    _stvarInit(out, declared, g);
}

// ---------------------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------------------

typedef struct SsdWFrame {
    SSDNode* node;   // borrowed; owned by its parent, or by SerSsdWriter::root
    string key;      // pending map key
    int32 idx;       // next array index
    bool ismap;
} SsdWFrame;
saDeclare(SsdWFrame);

typedef struct SerSsdWriter {
    SerWriter w;   // must be first
    SSDTree* tree;
    SSDNode* root;
    sa_SsdWFrame stack;
} SerSsdWriter;

// Hands the value to whatever container is currently open, consuming it. With nothing open
// the value is the whole document, which means a single-value root node.
static bool ssdEmit(_Inout_ SerSsdWriter* sw, _Inout_ stvar* v)
{
    int32 n = saSize(sw->stack);
    bool ok = false;

    if (n == 0) {
        if (sw->root) {
            stvarDestroy(v);
            return serWriterFail(&sw->w,
                                 SER_Err_Data,
                                 _SL("document already has a root value"));
        }

        sw->root = ssdtreeCreateNode(sw->tree, SSD_Create_Single);
        ssdLockedTransaction(sw->root)
        {
            ok = ssdnodeSetC(sw->root, 0, NULL, v, _ssdCurrentLockState);
        }
        return ok;
    }

    SsdWFrame* f = &sw->stack.a[n - 1];

    if (f->ismap && !f->key) {
        stvarDestroy(v);
        return serWriterFail(&sw->w, SER_Err_Data, _SL("map value with no key"));
    }

    ssdLockedTransaction(f->node)
    {
        if (f->ismap)
            ok = ssdnodeSetC(f->node, SSD_ByName, f->key, v, _ssdCurrentLockState);
        else
            ok = ssdnodeSetC(f->node, f->idx, NULL, v, _ssdCurrentLockState);
    }

    if (f->ismap)
        strDestroy(&f->key);
    else
        f->idx++;

    return ok;
}

static bool ssdBeginNode(_Inout_ SerSsdWriter* sw, SSDCreateType crtype)
{
    SSDNode* node = ssdtreeCreateNode(sw->tree, crtype);
    if (!node)
        return serWriterFail(&sw->w, SER_Err_Backend, _SL("could not create an SSD node"));

    if (saSize(sw->stack) == 0 && !sw->root) {
        // A container at the root: the writer keeps the reference itself.
        sw->root = node;
    } else {
        // ssdnodeSetC transfers our reference to the parent, which keeps the node alive for
        // exactly as long as the tree does.
        stvar v = stvar(object, node);
        if (!ssdEmit(sw, &v))
            return false;
    }

    saPush(&sw->stack,
           opaque,
           ((SsdWFrame) { .node = node, .ismap = (crtype == SSD_Create_Hashtable) }));
    return true;
}

static bool ssdEndNode(_Inout_ SerSsdWriter* sw)
{
    int32 n = saSize(sw->stack);
    if (n == 0)
        return serWriterFail(&sw->w, SER_Err_Backend, _SL("unbalanced container end"));

    strDestroy(&sw->stack.a[n - 1].key);
    saSetSize(&sw->stack, n - 1);
    return true;
}

static bool ssdWNull(SerWriter* w)
{
    stvar v = stvNone;
    return ssdEmit((SerSsdWriter*)w, &v);
}

static bool ssdWBool(SerWriter* w, bool val)
{
    stvar v = stvar(bool, val);
    return ssdEmit((SerSsdWriter*)w, &v);
}

static bool ssdWInt(SerWriter* w, int64 val, stype declared)
{
    stvar v;
    svFromInt(&v, val, declared);
    return ssdEmit((SerSsdWriter*)w, &v);
}

static bool ssdWUint(SerWriter* w, uint64 val, stype declared)
{
    stvar v;
    svFromUint(&v, val, declared);
    return ssdEmit((SerSsdWriter*)w, &v);
}

static bool ssdWReal(SerWriter* w, float64 val, stype declared)
{
    stvar v;
    svFromReal(&v, val, declared);
    return ssdEmit((SerSsdWriter*)w, &v);
}

static bool ssdWStr(SerWriter* w, strref val)
{
    stvar v;
    _stvarInit(&v, stType(string), stgeneric(string, (string)val));
    return ssdEmit((SerSsdWriter*)w, &v);
}

static bool ssdWBytes(SerWriter* w, const void* p, size_t n)
{
    return serWriterFail(w, SER_Err_Unsupported, _SL("SSD trees have no byte-blob node"));
}

static bool ssdWArrBegin(SerWriter* w, int32 count)
{
    return ssdBeginNode((SerSsdWriter*)w, SSD_Create_Array);
}

static bool ssdWArrEnd(SerWriter* w) { return ssdEndNode((SerSsdWriter*)w); }

static bool ssdWMapBegin(SerWriter* w, int32 count)
{
    return ssdBeginNode((SerSsdWriter*)w, SSD_Create_Hashtable);
}

static bool ssdWMapKey(SerWriter* w, strref key)
{
    SerSsdWriter* sw = (SerSsdWriter*)w;
    int32 n          = saSize(sw->stack);

    if (n == 0 || !sw->stack.a[n - 1].ismap)
        return serWriterFail(w, SER_Err_Backend, _SL("map key outside a map"));

    strDup(&sw->stack.a[n - 1].key, key);
    return true;
}

static bool ssdWMapKeyTyped(SerWriter* w, const STypeInfoExt* kt, stgeneric key)
{
    // Not advertised, so the traverser projects non-string keys to pair arrays instead.
    return serWriterFail(w, SER_Err_Unsupported, _SL("SSD hashtable keys are strings"));
}

static bool ssdWMapEnd(SerWriter* w) { return ssdEndNode((SerSsdWriter*)w); }

static bool ssdWTypeTag(SerWriter* w, const STypeInfoExt* st)
{
    return serWriterFail(w, SER_Err_Unsupported, _SL("SSD trees do not carry type tags"));
}

static bool ssdWRef(SerWriter* w, uint32 id)
{
    return serWriterFail(w, SER_Err_Unsupported, _SL("SSD trees do not carry references"));
}

static void ssdWDestroy(SerWriter* w)
{
    SerSsdWriter* sw = (SerSsdWriter*)w;

    for (int32 i = 0; i < saSize(sw->stack); i++)
        strDestroy(&sw->stack.a[i].key);
    saDestroy(&sw->stack);

    objRelease(&sw->root);
    objRelease(&sw->tree);
    xaFree(sw);
}

static const SerWriterOps ssdWriterOps = {
    .writeNull   = ssdWNull,
    .writeBool   = ssdWBool,
    .writeInt    = ssdWInt,
    .writeUint   = ssdWUint,
    .writeReal   = ssdWReal,
    .writeStr    = ssdWStr,
    .writeBytes  = ssdWBytes,
    .arrBegin    = ssdWArrBegin,
    .arrEnd      = ssdWArrEnd,
    .mapBegin    = ssdWMapBegin,
    .mapKey      = ssdWMapKey,
    .mapKeyTyped = ssdWMapKeyTyped,
    .mapEnd      = ssdWMapEnd,
    .typeTag     = ssdWTypeTag,
    .refDef      = ssdWRef,
    .refUse      = ssdWRef,
    .destroy     = ssdWDestroy,
};

_Use_decl_annotations_
SerWriter* serSsdWriterCreate(flags_t flags)
{
    SerSsdWriter* sw = (SerSsdWriter*)_serWriterAlloc(sizeof(SerSsdWriter),
                                                      &ssdWriterOps,
                                                      SER_Cap_ExactInt | SER_Cap_Sizes,
                                                      flags);
    sw->tree = ssdtreeCreate(0);
    saInit(&sw->stack, opaque(SsdWFrame), 8);

    return &sw->w;
}

_Use_decl_annotations_
SSDNode* serSsdWriterRoot(SerWriter* w)
{
    SerSsdWriter* sw = (SerSsdWriter*)w;
    return sw->root ? objAcquire(sw->root) : NULL;
}

// ---------------------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------------------

typedef struct SsdRFrame {
    SSDNode* node;    // borrowed
    sa_string keys;   // map only; a snapshot taken at mapBegin
    int32 idx;        // cursor; -1 before the first entry
    int32 count;
    bool ismap;
} SsdRFrame;
saDeclare(SsdRFrame);

typedef struct SerSsdReader {
    SerReader r;   // must be first
    SSDNode* root;
    stvar rootval;   // stands in for the root when it is a container
    bool rootsingle;
    sa_SsdRFrame stack;
} SerSsdReader;

// The stvar the reader is currently positioned on. Iteration is driven by the traverser through
// arrNext/mapNext, so "position" is just a cursor in the innermost frame.
//
// The pointer is into the node's own storage and is returned after the transient lock is
// released, which is the same bargain ssdPtr() makes: valid for as long as nobody else
// mutates the tree. A reader is a read-only pass over a tree the caller is not touching.
static _Ret_opt_valid_ stvar* ssdCur(_Inout_ SerSsdReader* sr)
{
    stvar* v = NULL;
    int32 n  = saSize(sr->stack);

    if (n == 0) {
        if (!sr->rootsingle)
            return &sr->rootval;

        ssdLockedTransaction(sr->root)
        {
            v = ssdnodePtr(sr->root, 0, NULL, _ssdCurrentLockState);
        }
        return v;
    }

    SsdRFrame* f = &sr->stack.a[n - 1];
    if (f->idx < 0 || f->idx >= f->count)
        return NULL;

    ssdLockedTransaction(f->node)
    {
        v = f->ismap ? ssdnodePtr(f->node, SSD_ByName, f->keys.a[f->idx], _ssdCurrentLockState)
                     : ssdnodePtr(f->node, f->idx, NULL, _ssdCurrentLockState);
    }
    return v;
}

static SerNodeKind ssdRPeek(SerReader* r)
{
    stvar* v = ssdCur((SerSsdReader*)r);
    if (!v)
        return SER_EOF;

    stype t = stvarType(v);
    if (!t)
        return SER_Invalid;

    switch (t->id) {
    case STypeId_none:
        return SER_Null;
    case STypeId_bool:
        return SER_Bool;
    case STypeId_int8:
    case STypeId_int16:
    case STypeId_int32:
    case STypeId_int64:
        return SER_Int;
    case STypeId_uint8:
    case STypeId_uint16:
    case STypeId_uint32:
    case STypeId_uint64:
        return SER_Uint;
    case STypeId_float32:
    case STypeId_float64:
        return SER_Real;
    case STypeId_string:
        return SER_Str;
    case STypeId_object: {
        SSDNode* n = stvarObj(SSDNode, v);
        if (!n)
            return SER_Null;
        if (ssdnodeIsHashtable(n))
            return SER_MapBegin;
        if (ssdnodeIsArray(n))
            return SER_ArrayBegin;
        return SER_Invalid;
    }
    default:
        return SER_Invalid;
    }
}

// Numeric and string reads go through stConvert, which is where the width and signedness
// tolerance of §11.1 comes from: a document written with an int16 in a slot that is now an
// int32 still reads, and only a value that genuinely does not fit is an error.
static bool ssdConvertCur(_Inout_ SerReader* r, stype destst, _Out_ stgeneric* out)
{
    memset(out, 0, sizeof(stgeneric));

    stvar* v = ssdCur((SerSsdReader*)r);
    if (!v)
        return serReaderFail(r, SER_Err_Data, _SL("no value at this position"));

    if (!_stConvert(destst, out, stvarType(v), v->data, 0)) {
        string msg = 0;
        strref nm  = _serBuiltinName(destst->id);
        strFormat(&msg,
                  _SL("value cannot be read as ${string}"),
                  stvar(string, nm ? (string)nm : (string)_SL("the declared type")));
        bool ret = serReaderFail(r, SER_Err_Data, msg);
        strDestroy(&msg);
        return ret;
    }
    return true;
}

static bool ssdRNull(SerReader* r)
{
    // Nothing to consume: the cursor advances through arrNext/mapNext, not through the value
    // reads. A null is simply a slot the traverser chose not to take a value from.
    return true;
}

static bool ssdRBool(SerReader* r, bool* out)
{
    stgeneric g;
    if (!ssdConvertCur(r, stType(bool), &g))
        return false;
    *out = g.st_bool;
    return true;
}

static bool ssdRInt(SerReader* r, int64* out, stype declared)
{
    stgeneric g;
    if (!ssdConvertCur(r, stType(int64), &g))
        return false;
    *out = g.st_int64;
    return true;
}

static bool ssdRUint(SerReader* r, uint64* out, stype declared)
{
    stgeneric g;
    if (!ssdConvertCur(r, stType(uint64), &g))
        return false;
    *out = g.st_uint64;
    return true;
}

static bool ssdRReal(SerReader* r, float64* out, stype declared)
{
    stgeneric g;
    if (!ssdConvertCur(r, stType(float64), &g))
        return false;
    *out = g.st_float64;
    return true;
}

static bool ssdRStr(SerReader* r, string* out)
{
    stgeneric g;
    if (!ssdConvertCur(r, stType(string), &g))
        return false;

    strDestroy(out);
    *out = g.st_string;   // stConvert produced an owned string
    return true;
}

static bool ssdRBytes(SerReader* r, Buffer* out)
{
    return serReaderFail(r, SER_Err_Unsupported, _SL("SSD trees have no byte-blob node"));
}

static bool ssdRPushFrame(_Inout_ SerReader* r, bool ismap)
{
    SerSsdReader* sr = (SerSsdReader*)r;

    stvar* v   = ssdCur(sr);
    SSDNode* n = v ? stvarObj(SSDNode, v) : NULL;

    if (!n || (ismap ? !ssdnodeIsHashtable(n) : !ssdnodeIsArray(n))) {
        return serReaderFail(r,
                             SER_Err_Data,
                             ismap ? _SL("expected a map") : _SL("expected an array"));
    }

    SsdRFrame f = { .node = n, .idx = -1, .ismap = ismap };

    if (ismap) {
        // Snapshot the keys rather than holding an iterator open: the traverser nests, and an
        // SSD iterator holds a transient read lock for its whole lifetime.
        SSDIterator* it = ssdnodeIter(n);
        while (ssditeratorValid(it)) {
            string k = 0;
            strDup(&k, ssditeratorName(it));
            saPushC(&f.keys, string, &k);
            ssditeratorNext(it);
        }
        objRelease(&it);
        f.count = saSize(f.keys);
    } else {
        ssdLockedTransaction(n)
        {
            f.count = ssdnodeCount(n, _ssdCurrentLockState);
        }
    }

    saPush(&sr->stack, opaque, f);
    return true;
}

static bool ssdRPopFrame(_Inout_ SerReader* r)
{
    SerSsdReader* sr = (SerSsdReader*)r;
    int32 n          = saSize(sr->stack);

    if (n == 0)
        return serReaderFail(r, SER_Err_Backend, _SL("unbalanced container end"));

    saDestroy(&sr->stack.a[n - 1].keys);
    saSetSize(&sr->stack, n - 1);
    return true;
}

static bool ssdRArrBegin(SerReader* r, int32* count)
{
    if (!ssdRPushFrame(r, false))
        return false;

    SerSsdReader* sr = (SerSsdReader*)r;
    *count           = sr->stack.a[saSize(sr->stack) - 1].count;
    return true;
}

static bool ssdRNext(SerReader* r)
{
    SerSsdReader* sr = (SerSsdReader*)r;
    int32 n          = saSize(sr->stack);
    if (n == 0)
        return false;

    SsdRFrame* f = &sr->stack.a[n - 1];
    f->idx++;
    return f->idx < f->count;
}

static bool ssdRArrEnd(SerReader* r) { return ssdRPopFrame(r); }

static bool ssdRMapBegin(SerReader* r, int32* count)
{
    if (!ssdRPushFrame(r, true))
        return false;

    SerSsdReader* sr = (SerSsdReader*)r;
    *count           = sr->stack.a[saSize(sr->stack) - 1].count;
    return true;
}

static bool ssdRMapNext(SerReader* r, string* key)
{
    SerSsdReader* sr = (SerSsdReader*)r;
    int32 n          = saSize(sr->stack);
    if (n == 0)
        return false;

    SsdRFrame* f = &sr->stack.a[n - 1];
    f->idx++;
    if (f->idx >= f->count)
        return false;

    strDup(key, f->keys.a[f->idx]);
    return true;
}

static bool ssdRMapEnd(SerReader* r) { return ssdRPopFrame(r); }

static bool ssdRTypeTag(SerReader* r, string* name)
{
    return serReaderFail(r, SER_Err_Unsupported, _SL("SSD trees do not carry type tags"));
}

static bool ssdRRef(SerReader* r, uint32* id)
{
    return serReaderFail(r, SER_Err_Unsupported, _SL("SSD trees do not carry references"));
}

static bool ssdRSkip(SerReader* r)
{
    // The cursor is already past the value as far as this backend is concerned; a whole
    // subtree costs nothing to skip because nothing was decoded to reach it.
    return true;
}

static void ssdRDestroy(SerReader* r)
{
    SerSsdReader* sr = (SerSsdReader*)r;

    for (int32 i = 0; i < saSize(sr->stack); i++)
        saDestroy(&sr->stack.a[i].keys);
    saDestroy(&sr->stack);

    stvarDestroy(&sr->rootval);
    objRelease(&sr->root);
    xaFree(sr);
}

static const SerReaderOps ssdReaderOps = {
    .peek        = ssdRPeek,
    .readNull    = ssdRNull,
    .readBool    = ssdRBool,
    .readInt     = ssdRInt,
    .readUint    = ssdRUint,
    .readReal    = ssdRReal,
    .readStr     = ssdRStr,
    .readBytes   = ssdRBytes,
    .arrBegin    = ssdRArrBegin,
    .arrNext     = ssdRNext,
    .arrEnd      = ssdRArrEnd,
    .mapBegin    = ssdRMapBegin,
    .mapNext     = ssdRMapNext,
    .mapEnd      = ssdRMapEnd,
    .readTypeTag = ssdRTypeTag,
    .readRef     = ssdRRef,
    .skip        = ssdRSkip,
    .destroy     = ssdRDestroy,
};

_Use_decl_annotations_
SerReader* serSsdReaderCreate(SSDNode* root, flags_t flags)
{
    SerSsdReader* sr = (SerSsdReader*)_serReaderAlloc(sizeof(SerSsdReader),
                                                      &ssdReaderOps,
                                                      SER_Cap_ExactInt | SER_Cap_Sizes |
                                                          SER_Cap_Skip,
                                                      flags);
    sr->root = objAcquire(root);
    saInit(&sr->stack, opaque(SsdRFrame), 8);

    sr->rootsingle = !ssdnodeIsHashtable(root) && !ssdnodeIsArray(root);
    if (!sr->rootsingle)
        stvarCopy(&sr->rootval, stvar(object, root));

    return &sr->r;
}
