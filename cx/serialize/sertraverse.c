// The traverser: one traversal of cx values that serves every format.
//
// Serialization is a (type x format) matrix and only one axis can be a dispatch table. The
// format axis is the vtable (SerWriterOps / SerReaderOps), which leaves the type axis to the
// single switch below. That switch is legitimate where a per-type vtable was not: STypeId_*
// is a closed set owned by cx, and this is one switch in one file rather than one per type.
// Extensibility lives in two other places -- a new format implements the vtable, and a new
// type registers a custom stype with the serialize/deserialize hooks.

#include "cx/serialize/serreader.h"
#include "cx/serialize/serwriter.h"

#include "cx/buffer/buffer.h"
#include "cx/container/hashtable.h"
#include "cx/container/sarray.h"
#include "cx/format.h"
#include "cx/obj/objclass.h"
#include "cx/obj/objimpl.h"
#include "cx/obj/objstdif.h"
#include "cx/string.h"
#include "cx/struct/struct.h"
#include "cx/xalloc/xalloc.h"

// ---------------------------------------------------------------------------------------
// Path stack
//
// A frame per level, pushed on the way down and popped on the way up. Nothing is formatted
// unless an error is actually raised, so the success path costs a push and a pop.
// ---------------------------------------------------------------------------------------

static void pushName(_Inout_ SerTraverseState* ws, _In_opt_ strref name)
{
    saPush(&ws->path, opaque, ((SerPathFrame) { .name = name }));
}

static void pushIdx(_Inout_ SerTraverseState* ws, int32 idx)
{
    saPush(&ws->path, opaque, ((SerPathFrame) { .idx = idx }));
}

static void setIdx(_Inout_ SerTraverseState* ws, int32 idx)
{
    int32 n = saSize(ws->path);
    if (n > 0)
        ws->path.a[n - 1].idx = idx;
}

static void setName(_Inout_ SerTraverseState* ws, _In_opt_ strref name)
{
    int32 n = saSize(ws->path);
    if (n > 0)
        ws->path.a[n - 1].name = name;
}

static void popFrame(_Inout_ SerTraverseState* ws)
{
    int32 n = saSize(ws->path);
    if (n > 0)
        saSetSize(&ws->path, n - 1);
}

// ---------------------------------------------------------------------------------------
// Write
// ---------------------------------------------------------------------------------------

static bool writeValue(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema, stype st,
                       stgeneric val);

// The name is only for the message, so an unnamed type falls back to its raw id rather than
// blocking the report.
static void typeName(_Inout_ string* out, stype st)
{
    strref nm = _serBuiltinName(st->id);
    if (nm)
        strDup(out, nm);
    else
        strFormat(out, _SL("id ${uint}"), stvar(uint32, st->id));
}

static bool badWriteType(_Inout_ SerWriter* w, stype st)
{
    string msg = 0, nm = 0;
    typeName(&nm, st);
    strFormat(&msg, _SL("cannot serialize type ${string}"), stvar(string, nm));
    bool ret = serWriterFail(w, SER_Err_Unsupported, msg);
    strDestroy(&msg);
    strDestroy(&nm);
    return ret;
}

static bool writeSuid(_Inout_ SerWriter* w, _In_opt_ SUID* id)
{
    if (!id)
        return serWriteNull(w);

    if (serWriterCan(w, Bytes)) {
        // big endian so that it's in the same logical order as the text form
        uint8 raw[16];
        for (int i = 0; i < 8; i++) {
            raw[i]     = (uint8)(id->high >> (56 - i * 8));
            raw[8 + i] = (uint8)(id->low >> (56 - i * 8));
        }
        return serWriteBytes(w, raw, sizeof(raw));
    }

    // use the text representation (base32)
    string s = 0;
    suidEncode(&s, id);
    bool ret = serWriteStr(w, s);
    strDestroy(&s);
    return ret;
}

static bool writeBuffer(_Inout_ SerWriter* w, _In_opt_ Buffer buf)
{
    if (!buf)
        return serWriteNull(w);
    return serWriteBytes(w, buf->data, buf->len);
}

static bool writeArray(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema, sa_ref ref)
{
    // A never-initialized array is a distinct state from an empty one, and writing it as null
    // is what lets it come back as the same state rather than as an empty array.
    if (!ref.a)
        return serWriteNull(w);

    stype et = saElemType(ref);
    int32 n  = saSize(ref);

    if (!serArrBegin(w, n))
        return false;

    size_t esz             = stGetSize(et);
    const STypeInfoExt* eschema = schema->param[0];

    pushIdx(&w->traverse, 0);
    for (int32 i = 0; i < n; i++) {
        setIdx(&w->traverse, i);
        if (!writeValue(w, eschema, et, stStored(et, (char*)ref.a + (size_t)i * esz))) {
            popFrame(&w->traverse);
            return false;
        }
    }
    popFrame(&w->traverse);

    return serArrEnd(w);
}

static bool writeHashtable(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema, hashtable ht)
{
    if (!ht)
        return serWriteNull(w);

    stype kt = htKeyType(ht), vt = htValType(ht);
    const STypeInfoExt* kschema = schema->param[0];
    const STypeInfoExt* vschema = schema->param[1];
    int32 n                = (int32)htSize(ht);

    // Three encodings, in preference order: a map keyed by strings, a map keyed by typed values
    // where the backend supports that, and -- for everything else -- an array of two-element
    // key/value pairs, which is the projection every format can represent.
    bool strkeys = stEq(kt, stType(string));
    bool pairs   = !strkeys && !serWriterCan(w, NonStringKeys);

    if (!(pairs ? serArrBegin(w, n) : serMapBegin(w, n)))
        return false;

    bool ok = true;
    int32 i = 0;
    pushIdx(&w->traverse, 0);

    htiter iter;
    htiInit(&iter, ht);
    while (ok && htiValid(&iter)) {
        void* kp = _hteElemKeyPtr(iter.hdr, iter.slot);
        void* vp = stGetSize(vt) > 0 ? _hteElemValPtr(iter.hdr, iter.slot) : NULL;

        if (pairs) {
            setIdx(&w->traverse, i);
            ok = serArrBegin(w, 2) && writeValue(w, kschema, kt, stStored(kt, kp));
        } else if (strkeys) {
            strref key = *(strref*)kp;
            setName(&w->traverse, key);
            ok = serMapKey(w, key);
        } else {
            setIdx(&w->traverse, i);
            ok = w->ops->mapKeyTyped(w, kschema, stStored(kt, kp));
        }

        if (ok) {
            // A value type of `none` is a hashed set; there is no value to write.
            ok = vp ? writeValue(w, vschema, vt, stStored(vt, vp)) : serWriteNull(w);
        }
        if (ok && pairs)
            ok = serArrEnd(w);

        i++;
        htiNext(&iter);
    }
    // the iterator holds a lock on the table; this has to run on every path out
    htiFinish(&iter);

    popFrame(&w->traverse);

    if (!ok)
        return false;

    return pairs ? serArrEnd(w) : serMapEnd(w);
}

// A member holding its default value is omitted, which is what makes "absent means default" the
// natural reading on the way back in, and what keeps a mostly-empty struct from writing out a
// screen of zeroes.
//
// The comparison is over the member's bytes. That is exact for scalars, and for a member holding
// a pointer -- a string, a container -- a match means pointer identity, so it can never mistake
// a live value for a default. The reverse, a value equal to the default that does not compare
// bitwise equal to it, only costs a member that did not need writing.
static bool memberIsDefault(_In_opt_ const void* defaults, _In_ const StructMemberDesc* mem,
                            _In_ const void* base)
{
    size_t n = stGetSize(mem->schema->type) * (mem->arrsize ? mem->arrsize : 1);

    if (defaults)
        return memcmp(base, (const char*)defaults + mem->offset, n) == 0;

    const uint8* p = (const uint8*)base;
    for (size_t i = 0; i < n; i++) {
        if (p[i])
            return false;
    }
    return true;
}

static bool memberSkipped(_Inout_ SerWriter* w, _In_opt_ const void* defaults,
                          _In_ const StructMemberDesc* mem, _In_ const void* s)
{
    if (mem->flags & STRUCT_NoSerialize)
        return true;

    return !(w->flags & SER_EmitDefaults) &&
        memberIsDefault(defaults, mem, (const char*)s + mem->offset);
}

// Structs and classes describe their members with the same record, so everything from here to
// the end of the member loop is shared. What differs is only where the table comes from: a
// struct has one, a class has one per level of its parent chain.

static int32 countMembers(_Inout_ SerWriter* w, _In_opt_ const StructMemberDesc* members, int n,
                          _In_opt_ const void* defaults, _In_ const void* s)
{
    int32 count = 0;
    for (int m = 0; m < n; m++) {
        if (!memberSkipped(w, defaults, &members[m], s))
            count++;
    }
    return count;
}

// Emits key/value for each member that survives the skip test. The caller owns the surrounding
// map and the path frame, so a class can run this once per level into a single flat map.
static bool writeMembers(_Inout_ SerWriter* w, _In_opt_ const StructMemberDesc* members, int n,
                         _In_opt_ const void* defaults, _In_ const void* s)
{
    for (int m = 0; m < n; m++) {
        const StructMemberDesc* mem = &members[m];

        // NoSerialize members are present in the table with a valid type and offset but an
        // empty name, so this has to test the flag rather than the name.
        if (memberSkipped(w, defaults, mem, s))
            continue;

        setName(&w->traverse, mem->name);
        if (!serMapKey(w, mem->name))
            return false;

        char* base    = (char*)s + mem->offset;
        stype mt      = mem->schema->type;
        size_t stride = stGetSize(mt);

        if (mem->arrsize == 0) {
            if (!writeValue(w, mem->schema, mt, stStored(mt, base)))
                return false;
            continue;
        }

        // A fixed C array member maps to an array of exactly arrsize elements.
        if (!serArrBegin(w, (int32)mem->arrsize))
            return false;
        pushIdx(&w->traverse, 0);
        for (uint32 e = 0; e < mem->arrsize; e++) {
            setIdx(&w->traverse, (int32)e);
            if (!writeValue(w, mem->schema, mt, stStored(mt, base + e * stride))) {
                popFrame(&w->traverse);
                return false;
            }
        }
        popFrame(&w->traverse);
        if (!serArrEnd(w))
            return false;
    }

    return true;
}

static bool writeStruct(_Inout_ SerWriter* w, _In_ const StructInfo* si, _In_opt_ StructBase* s)
{
    if (!s)
        return serWriteNull(w);

    if (!serMapBegin(w, countMembers(w, si->members, si->nmembers, si->defaults, s)))
        return false;

    pushName(&w->traverse, NULL);
    bool ok = writeMembers(w, si->members, si->nmembers, si->defaults, s);
    popFrame(&w->traverse);

    return ok && serMapEnd(w);
}

// The declared vocabulary of a dynamic structp slot, or NULL where the slot names no set.
//
// The runtime descriptor carries the set too -- parameterized `structp` predates the schema
// split -- and reading it from there would be wrong. The registry keys a descriptor on id, size
// and params and not on `ext` (cx/stype/stregistry.c), so every `structp[AnySet]` descriptor is
// one key: interning merges them, and the survivor's `ext` is whichever set happened to
// register first. The schema descriptors are file-static and never interned, so the schema is
// the only side that reliably knows which set this slot meant.
static _Ret_maybenull_ const StructSet* structpSet(_In_ const STypeInfoExt* schema)
{
    if (schema->type == stType(structp) && (schema->flags & STIE_TypeSet))
        return (const StructSet*)schema->detail;
    return NULL;
}

// A structp is an owning pointer: assignment deep-copies it and destruction frees the pointee.
// Two live slots holding one address is therefore not a state cx can represent -- it would
// double-free -- so a structp graph is always a tree, and writing the pointee inline is exact
// rather than a flattening that loses sharing. It is also why reference dedup does not apply to
// structp: there is no sharing to preserve, and resolving a back-reference to the same pointer
// on the way in would manufacture the double-free that the write side cannot even encounter.
static bool writeStructp(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema,
                         _In_opt_ StructBase* s)
{
    if (!s)
        return serWriteNull(w);

    if (schema->type != stType(structp))
        return serWriterFail(w, SER_Err_Schema, _SL("no schema for this structp value"));

    // The statically typed form: the slot names one struct, so the pointee needs no tag.
    if (schema->param[0]) {
        const StructInfo* psi = (const StructInfo*)schema->param[0]->detail;
        if (!psi)
            return serWriterFail(w, SER_Err_Schema, _SL("structp target carries no StructInfo"));
        return writeStruct(w, psi, s);
    }

    // The dynamic form -- `structp[SomeSet]` -- names a set of struct types rather than one, so
    // which of them is behind the pointer is recoverable only from a tag. The instance knows
    // its own type even though the slot does not: every struct carries its StructInfo.
    const StructInfo* si = s->structinfo;
    if (!si)
        return serWriterFail(w,
                             SER_Err_Schema,
                             _SL("this struct instance carries no StructInfo, so nothing can "
                                 "name it on the wire"));

    // The set is the slot's declared vocabulary, and the read side resolves through it alone.
    // Writing a struct that is not in it produces a document this same schema cannot read back,
    // which is worth refusing here -- where the path names the offending member -- rather than
    // discovering on the way in.
    const StructSet* set = structpSet(schema);
    if (set && structSetFind(set, si->name) != si) {
        string msg = 0;
        strFormat(&msg,
                  _SL("this slot is declared over a struct set that does not contain "
                      "${string}"),
                  stvar(string, (string)si->name));
        bool ret = serWriterFail(w, SER_Err_Type, msg);
        strDestroy(&msg);
        return ret;
    }

    if (!serWriterCan(w, TypeTags)) {
        string msg = 0;
        strFormat(&msg,
                  _SL("this format cannot carry the type tag a dynamic structp slot holding a "
                      "${string} needs"),
                  stvar(string, (string)si->name));
        bool ret = serWriterFail(w, SER_Err_Unsupported, msg);
        strDestroy(&msg);
        return ret;
    }

    STypeInfoExt tag = { .type = si->type, .name = si->name };
    if (!w->ops->typeTag(w, &tag))
        return false;

    return writeStruct(w, si, s);
}

// ---------------------------------------------------------------------------------------
// Objects
//
// A class describes only the members it declares and chains to its parent, which is how init
// and destroy already work. Base members go out first, matching construction order, and an
// unannotated level in the middle of a chain simply contributes nothing without stopping its
// ancestors from contributing.
// ---------------------------------------------------------------------------------------

static int32 countClassMembers(_Inout_ SerWriter* w, _In_opt_ const ObjClassInfo* cls,
                               _In_ const void* inst)
{
    if (!cls)
        return 0;
    return countClassMembers(w, cls->parent, inst) +
        countMembers(w, cls->sermembers, cls->nsermembers, NULL, inst);
}

static bool writeClassMembers(_Inout_ SerWriter* w, _In_opt_ const ObjClassInfo* cls,
                              _In_ const void* inst)
{
    if (!cls)
        return true;
    return writeClassMembers(w, cls->parent, inst) &&
        writeMembers(w, cls->sermembers, cls->nsermembers, NULL, inst);
}

// The declared class of a slot, or NULL where the schema does not name one. Only an `object`
// level carries a class: at any other level `detail` means something else entirely, and on an
// `object` level declared over a `classset` it holds the set instead.
static _Ret_maybenull_ const ObjClassInfo* schemaClass(_In_ const STypeInfoExt* schema)
{
    if (schema->type != stType(object) || (schema->flags & STIE_TypeSet))
        return NULL;
    return (const ObjClassInfo*)schema->detail;
}

// The declared vocabulary of an `object[SomeSet]` slot, or NULL where the slot names no set.
// Unlike a structp there is no runtime fallback: the class of an object slot is erased to
// `object` in every descriptor, so the schema is the only side that ever knew.
static _Ret_maybenull_ const ClassSet* schemaClassSet(_In_ const STypeInfoExt* schema)
{
    if (schema->type != stType(object) || !(schema->flags & STIE_TypeSet))
        return NULL;
    return (const ClassSet*)schema->detail;
}

// A tag goes out whenever the schema does not already name the instance's exact class -- a
// dynamic slot, or a base-class slot holding a derived instance. Writing the derived members
// under a name the reader would resolve to the base is the one outcome worth refusing outright:
// it reads back as a different type without saying so.
static bool writeObjTypeTag(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema,
                            _In_ const ObjClassInfo* cls)
{
    if (schemaClass(schema) == cls)
        return true;

    if (!serWriterCan(w, TypeTags)) {
        string msg = 0;
        strFormat(&msg,
                  _SL("this format cannot carry the type tag a ${string} in a dynamic or "
                      "base-class slot needs"),
                  stvar(string, (string)cls->name));
        bool ret = serWriterFail(w, SER_Err_Unsupported, msg);
        strDestroy(&msg);
        return ret;
    }

    STypeInfoExt tag = { .type = &_sti_object, .name = cls->name, .detail = cls };
    return w->ops->typeTag(w, &tag);
}

// References are in play only where the document asked for them and the format can carry them.
// Everything else falls back to the cycle guard, which asks the same question of a narrower set
// and has nothing better than an error to answer it with.
static bool refsEnabled(_In_ const SerWriter* w)
{
    return (w->flags & SER_Refs) && serWriterCan(w, Refs);
}

static bool writeObject(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema, _In_opt_ ObjInst* obj)
{
    if (!obj)
        return serWriteNull(w);

    bool refs = refsEnabled(w);

    if (refs) {
        // Sharing and cycles are the same lookup: an object already written is written again as
        // the id it got the first time, whether that first time has finished or is still on the
        // stack below this call.
        uint32 id;
        if (w->refids && htFind(w->refids, ptr, obj, uint32, &id))
            return w->ops->refUse(w, id);
    } else {
        // Objects are the only shared, cyclic thing cx has, so this is where a graph can eat the
        // stack. Without references there is nothing to represent the back edge with, so the
        // best available answer is to say so.
        for (int32 i = 0; i < saSize(w->inprogress); i++) {
            if (w->inprogress.a[i] == obj)
                return serWriterFail(w,
                                     SER_Err_Data,
                                     _SL("this object graph contains a cycle, which needs "
                                         "references to represent"));
        }
    }

    const ObjClassInfo* cls = objClsInfo(obj);

    // No wire name means the class never opted in, and a name is what the reader resolves back
    // into a class to construct. Writing the instance anyway would produce a document that only
    // its own schema could read, and only by guessing.
    if (!cls->name)
        return serWriterFail(w,
                             SER_Err_Unsupported,
                             _SL("this object's class is neither [serialize] nor Serializable, "
                                 "so it has no wire name"));

    // A slot declared over a classset accepts exactly the classes in it, and the read side
    // resolves through that set alone. Writing an instance of anything else -- including a
    // subclass of something in the set, which the set does not cover -- produces a document
    // this same schema cannot read back.
    const ClassSet* set = schemaClassSet(schema);
    if (set && classSetFind(set, cls->name) != cls) {
        string msg = 0;
        strFormat(&msg,
                  _SL("this slot is declared over a class set that does not contain ${string}"),
                  stvar(string, (string)cls->name));
        bool ret = serWriterFail(w, SER_Err_Type, msg);
        strDestroy(&msg);
        return ret;
    }

    if (!writeObjTypeTag(w, schema, cls))
        return false;

    if (refs) {
        // The id is claimed and recorded before a single member goes out, which is the whole
        // trick: a member that points back here finds it and writes a use instead of recursing.
        // It follows the type tag so that a use, which carries no tag, still resolves to
        // something the reader knows how to construct.
        if (!w->refids)
            htInit(&w->refids, ptr, uint32, 16);
        uint32 id = w->nextrefid++;
        htInsert(&w->refids, ptr, obj, uint32, id);
        if (!w->ops->refDef(w, id))
            return false;
    } else {
        saPush(&w->inprogress, ptr, obj);
    }

    bool ok;
    Serializable* ser = objInstIf(obj, Serializable);
    if (ser) {
        ok = ser->serialize(obj, w);
    } else {
        ok = serMapBegin(w, countClassMembers(w, cls, obj));
        if (ok) {
            pushName(&w->traverse, NULL);
            ok = writeClassMembers(w, cls, obj);
            popFrame(&w->traverse);
            ok = ok && serMapEnd(w);
        }
    }

    // The seen-map is document-scoped and outlives the value, unlike the guard stack: an object
    // written and finished can still be referred to from somewhere else entirely.
    if (!refs)
        saSetSize(&w->inprogress, saSize(w->inprogress) - 1);
    return ok;
}

static bool writeValue(_Inout_ SerWriter* w, _In_ const STypeInfoExt* schema, stype st,
                       stgeneric val)
{
    if (!st)
        return serWriterFail(w, SER_Err_Type, _SL("no type descriptor for value"));

    // A custom type serves every format from one implementation, because the hook decomposes
    // into data-model nodes and never asks what format it is writing to.
    if (st->ops.serialize)
        return st->ops.serialize(st, val, w);

    switch (st->id) {
    case STypeId_none:
        return serWriteNull(w);
    case STypeId_bool:
        return serWriteBool(w, val.st_bool);

    case STypeId_int8:
        return serWriteInt(w, val.st_int8, st);
    case STypeId_int16:
        return serWriteInt(w, val.st_int16, st);
    case STypeId_int32:
        return serWriteInt(w, val.st_int32, st);
    case STypeId_int64:
        return serWriteInt(w, val.st_int64, st);

    case STypeId_uint8:
        return serWriteUint(w, val.st_uint8, st);
    case STypeId_uint16:
        return serWriteUint(w, val.st_uint16, st);
    case STypeId_uint32:
        return serWriteUint(w, val.st_uint32, st);
    case STypeId_uint64:
        return serWriteUint(w, val.st_uint64, st);

    case STypeId_float32:
        return serWriteReal(w, val.st_float32, st);
    case STypeId_float64:
        return serWriteReal(w, val.st_float64, st);

    case STypeId_string:   // STypeId_strref shares this value
        return serWriteStr(w, val.st_string);
    case STypeId_suid:
        return writeSuid(w, val.st_suid);
    case STypeId_buffer:
        return writeBuffer(w, val.st_buffer);

    case STypeId_sarray:
        return writeArray(w, schema, val.st_sarray);
    case STypeId_hashtable:
        return writeHashtable(w, schema, val.st_hashtable);
    case STypeId_struct: {
        if (schema->type->id != STypeId_struct)
            return serWriterFail(w, SER_Err_Schema, _SL("no schema for this struct value"));
        const StructInfo* si = (const StructInfo*)schema->detail;
        if (!si)
            return serWriterFail(w, SER_Err_Schema, _SL("struct schema carries no StructInfo"));
        devAssert(si->structsize == stGetSize(st));
        return writeStruct(w, si, val.st_struct);
    }
    case STypeId_structp:
        return writeStructp(w, schema, val.st_structp);
    case STypeId_object:
        return writeObject(w, schema, val.st_object);

    default:
        return badWriteType(w, st);
    }
}

_Use_decl_annotations_
bool _serWrite(SerWriter* w, const STypeInfoExt* schema, stgeneric val)
{
    if (!w || !schema)
        return false;
    if (w->err.code != SER_Err_None)
        return false;

    if (writeValue(w, schema, schema->type, val))
        return true;

    // A backend op is allowed to just return false; the traverser guarantees that a failed
    // traversal always leaves something in `err` for the caller to report.
    if (w->err.code == SER_Err_None)
        serWriterFail(w, SER_Err_Backend, _SL("the backend rejected a value"));
    return false;
}

// ---------------------------------------------------------------------------------------
// Read
// ---------------------------------------------------------------------------------------

static bool readValue(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema, stype st,
                      _Inout_ void* storage);

static bool overflow(_Inout_ SerReader* r, stype st)
{
    string msg = 0, nm = 0;
    typeName(&nm, st);
    strFormat(&msg, _SL("value does not fit in a ${string}"), stvar(string, nm));
    bool ret = serReaderFail(r, SER_Err_Overflow, msg);
    strDestroy(&msg);
    strDestroy(&nm);
    return ret;
}

// Reading a signed integer narrower than int64 is the width-and-signedness tolerance of
// §11.1: the backend hands back the widest form it has and the traverser range-checks it into
// the slot the schema asked for.
static bool readIntInto(_Inout_ SerReader* r, stype st, _Inout_ void* storage)
{
    int64 v;
    if (!serReadInt(r, &v, st))
        return false;

    switch (st->size) {
    case 1:
        if (v < INT8_MIN || v > INT8_MAX)
            return overflow(r, st);
        *(int8*)storage = (int8)v;
        return true;
    case 2:
        if (v < INT16_MIN || v > INT16_MAX)
            return overflow(r, st);
        *(int16*)storage = (int16)v;
        return true;
    case 4:
        if (v < INT32_MIN || v > INT32_MAX)
            return overflow(r, st);
        *(int32*)storage = (int32)v;
        return true;
    default:
        *(int64*)storage = v;
        return true;
    }
}

static bool readUintInto(_Inout_ SerReader* r, stype st, _Inout_ void* storage)
{
    uint64 v;
    if (!serReadUint(r, &v, st))
        return false;

    switch (st->size) {
    case 1:
        if (v > UINT8_MAX)
            return overflow(r, st);
        *(uint8*)storage = (uint8)v;
        return true;
    case 2:
        if (v > UINT16_MAX)
            return overflow(r, st);
        *(uint16*)storage = (uint16)v;
        return true;
    case 4:
        if (v > UINT32_MAX)
            return overflow(r, st);
        *(uint32*)storage = (uint32)v;
        return true;
    default:
        *(uint64*)storage = v;
        return true;
    }
}

static bool readSuid(_Inout_ SerReader* r, _Inout_ void* storage)
{
    SUID* out = (SUID*)storage;

    if (serPeek(r) == SER_Null) {
        out->high = out->low = 0;
        return serReadNull(r);
    }

    if (serPeek(r) == SER_Bytes) {
        Buffer buf = NULL;
        if (!serReadBytes(r, &buf))
            return false;

        if (buf->len != sizeof(SUID)) {
            bufDestroy(&buf);
            return serReaderFail(r, SER_Err_Data, _SL("a suid is exactly 16 bytes"));
        }

        uint64 hi = 0, lo = 0;
        for (int i = 0; i < 8; i++) {
            hi = (hi << 8) | buf->data[i];
            lo = (lo << 8) | buf->data[8 + i];
        }
        out->high = hi;
        out->low  = lo;
        bufDestroy(&buf);
        return true;
    }

    string s = 0;
    if (!serReadStr(r, &s))
        return false;

    bool ok = suidDecode(out, s);
    strDestroy(&s);
    return ok ? true : serReaderFail(r, SER_Err_Data, _SL("value is not a valid suid"));
}

static bool readBuffer(_Inout_ SerReader* r, _Inout_ void* storage)
{
    Buffer* out = (Buffer*)storage;

    if (serPeek(r) == SER_Null) {
        bufDestroy(out);
        return serReadNull(r);
    }

    // serReadBytes replaces the buffer if it exists
    return serReadBytes(r, out);
}

// Allocate scratch storage for one value of the given schema, ready to be read into.
static _Ret_notnull_ void* scratchAlloc(_In_ const STypeInfoExt* schema)
{
    void* p = xaAlloc(stGetSize(schema->type), XA_Zero);
    if (schema->type->id == stTypeId(struct) && schema->detail)
        _structInitMany((StructBase*)p, (const StructInfo*)schema->detail, 1);
    return p;
}

static bool readArray(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema, _Inout_ void* storage)
{
    sahandle h = (sahandle)storage;

    if (serPeek(r) == SER_Null) {
        _saDestroy(h);
        return serReadNull(r);
    }

    const STypeInfoExt* eschema = schema->param[0];
    stype et               = eschema ? eschema->type : NULL;
    if (!et)
        return serReaderFail(r, SER_Err_Schema, _SL("sarray descriptor carries no element type"));

    _saDestroy(h);
    _saInit(h, et, 0, false, 0);

    int32 count;
    if (!serArrBeginR(r, &count))
        return false;

    size_t esz = stGetSize(et);
    bool ok    = true;

    pushIdx(&r->traverse, 0);
    while (ok && serArrNext(r)) {
        int32 idx = saSize(*h);
        setIdx(&r->traverse, idx);

        // Grow in place and read straight into the slot: saSetSize zero-fills what it adds,
        // which is exactly the state readValue expects to overwrite.
        _saSetSize(h, idx + 1);
        void* slot = (char*)h->a + (size_t)idx * esz;
        if (et->id == stTypeId(struct) && eschema->detail)
            _structInitMany((StructBase*)slot, (const StructInfo*)eschema->detail, 1);

        ok = readValue(r, eschema, et, slot);
    }
    popFrame(&r->traverse);

    return ok && serArrEndR(r);
}

static bool readHashtable(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema,
                          _Inout_ void* storage)
{
    hashtable* hp = (hashtable*)storage;

    if (serPeek(r) == SER_Null) {
        htDestroy(hp);
        return serReadNull(r);
    }

    const STypeInfoExt* kschema = schema->param[0];
    const STypeInfoExt* vschema = schema->param[1];
    stype kt               = kschema ? kschema->type : NULL;
    stype vt               = vschema ? vschema->type : NULL;
    if (!kt || !vt)
        return serReaderFail(r,
                             SER_Err_Schema,
                             _SL("hashtable descriptor carries no key/value types"));

    htDestroy(hp);
    _htInit(hp, kt, vt, 8, 0);

    // Whichever encoding the writer chose; peek tells us which without having to re-derive the
    // capability decision.
    bool pairs = serPeek(r) == SER_ArrayBegin;
    int32 count;

    if (!(pairs ? serArrBeginR(r, &count) : serMapBeginR(r, &count)))
        return false;

    bool ok    = true;
    int32 i    = 0;
    string key = 0;
    pushIdx(&r->traverse, 0);

    for (;;) {
        void *kscratch = NULL, *vscratch = NULL;

        if (pairs) {
            if (!serArrNext(r))
                break;
            setIdx(&r->traverse, i);

            int32 pcount;
            if (!serArrBeginR(r, &pcount) || !serArrNext(r)) {
                ok = false;
                break;
            }
            kscratch = scratchAlloc(kschema);
            ok       = readValue(r, kschema, kt, kscratch);
            if (ok && !serArrNext(r))
                ok = serReaderFail(r, SER_Err_Data, _SL("key/value pair has no value"));
        } else {
            if (!serMapNext(r, &key))
                break;
            setName(&r->traverse, key);

            if (!stEq(kt, stType(string))) {
                ok = serReaderFail(r,
                                   SER_Err_Data,
                                   _SL("document keys this map by name, but its key type "
                                       "is not string"));
            } else {
                kscratch = scratchAlloc(kschema);
                strDup((string*)kscratch, key);
            }
        }

        if (ok) {
            if (stGetSize(vt) > 0) {
                vscratch = scratchAlloc(vschema);
                ok       = readValue(r, vschema, vt, vscratch);
            } else {
                ok = serReadNull(r);
            }
        }

        if (ok && pairs)
            ok = serArrEndR(r);

        if (ok) {
            // The value is consumed into the table, so the scratch block is freed without
            // being destroyed; the key is copied, so its scratch is destroyed as well.
            _htInsertPtr(hp,
                         stStored(kt, kscratch),
                         vscratch ? stStoredPtr(vt, vscratch) : NULL,
                         vscratch ? HTINT_Consume : 0);
            if (kscratch)
                _stDestroy(kt, stStoredPtr(kt, kscratch), 0);
        } else {
            if (kscratch)
                _stDestroy(kt, stStoredPtr(kt, kscratch), 0);
            if (vscratch)
                _stDestroy(vt, stStoredPtr(vt, vscratch), 0);
        }

        xaFree(kscratch);
        xaFree(vscratch);

        if (!ok)
            break;
        i++;
    }

    strDestroy(&key);
    popFrame(&r->traverse);

    if (!ok)
        return false;

    return pairs ? serArrEndR(r) : serMapEndR(r);
}

static _Ret_maybenull_ const StructMemberDesc*
findMember(_In_opt_ const StructMemberDesc* members, int n, _In_opt_ strref key)
{
    for (int m = 0; m < n; m++) {
        if (!(members[m].flags & STRUCT_NoSerialize) && strEq(members[m].name, key))
            return &members[m];
    }
    return NULL;
}

// Derived first, so a class chain resolves a name the way C scoping would if it allowed
// shadowing. It cannot -- the flattened instance struct would not compile -- so this only ever
// finds one.
static _Ret_maybenull_ const StructMemberDesc*
findClassMember(_In_opt_ const ObjClassInfo* cls, _In_opt_ strref key)
{
    for (; cls; cls = cls->parent) {
        const StructMemberDesc* mem = findMember(cls->sermembers, cls->nsermembers, key);
        if (mem)
            return mem;
    }
    return NULL;
}

static bool readMemberValue(_Inout_ SerReader* r, _In_ const StructMemberDesc* mem, _Inout_ void* s)
{
    char* base    = (char*)s + mem->offset;
    stype mt      = mem->schema->type;
    size_t stride = stGetSize(mt);

    if (mem->arrsize == 0)
        return readValue(r, mem->schema, mt, base);

    int32 acount;
    if (!serArrBeginR(r, &acount))
        return false;

    bool ok  = true;
    uint32 e = 0;
    pushIdx(&r->traverse, 0);
    while (ok && serArrNext(r)) {
        setIdx(&r->traverse, (int32)e);
        if (e >= mem->arrsize) {
            // A long array is an error: there is nowhere to put the extra elements, and
            // silently dropping them would make the round trip lossy without saying so.
            ok = serReaderFail(r, SER_Err_Data, _SL("more elements than the member holds"));
            break;
        }
        ok = readValue(r, mem->schema, mt, base + e * stride);
        e++;
    }
    popFrame(&r->traverse);

    // A short array leaves the remaining elements at whatever the destination was initialized
    // with, which for a freshly initialized one is zero.
    return ok && serArrEndR(r);
}

// The map body shared by structs and classes: exactly one of `si` and `cls` says where the
// member tables come from.
static bool readMembers(_Inout_ SerReader* r, _In_opt_ const StructInfo* si,
                        _In_opt_ const ObjClassInfo* cls, _Inout_ void* s)
{
    int32 count;
    if (!serMapBeginR(r, &count))
        return false;

    bool ok    = true;
    string key = 0;
    pushName(&r->traverse, NULL);

    while (ok && serMapNext(r, &key)) {
        setName(&r->traverse, key);

        const StructMemberDesc* mem = si ?
            findMember(si->members, si->nmembers, key) :
            findClassMember(cls, key);

        if (!mem) {
            // Members the schema does not know about are skipped, which is what makes a
            // document written by a newer build readable by an older one.
            if (r->flags & SER_Strict)
                ok = serReaderFail(r, SER_Err_Data, _SL("unknown member"));
            else
                ok = serSkip(r);
            continue;
        }

        ok = readMemberValue(r, mem, s);
    }

    strDestroy(&key);
    popFrame(&r->traverse);

    return ok && serMapEndR(r);
}

static bool readStruct(_Inout_ SerReader* r, _In_ const StructInfo* si, _Inout_ StructBase* s)
{
    if (!s)
        return serReaderFail(r, SER_Err_Data, _SL("no destination struct"));

    if (serPeek(r) == SER_Null)
        return serReadNull(r);

    // The document is the authority on every member it could have carried, so one it omits has
    // to read back as the default -- that is what makes omitting defaults on the way out
    // lossless. Resetting rather than merging also makes reading over a live struct do exactly
    // what reading into a fresh one does.
    _structDestroyMembersMany(s, 1);
    _structInitMany(s, si, 1);

    return readMembers(r, si, NULL, s);
}

// The struct a dynamic structp slot's type tag names.
//
// A slot declared over a set resolves through that set and nothing else. The set is the
// declared vocabulary -- it is what makes `structp[SomeSet]` self-sufficient, with no resolver
// to register -- and the write side refuses to put anything else in the slot, so letting a
// resolver widen it here would only admit documents that cx would not have written. A slot with
// no set has nothing but the reader's resolvers to ask.
static _Ret_maybenull_ const StructInfo*
resolveStructp(_Inout_ SerReader* r, _In_opt_ const StructSet* set, _In_opt_ strref name)
{
    if (set)
        return structSetFind(set, name);

    SerResolved res;
    if (!serReaderResolve(&res, r, name))
        return NULL;
    return res.structinfo;
}

static bool readStructp(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema, _Inout_ void* storage)
{
    StructBase** slot = (StructBase**)storage;

    if (serPeek(r) == SER_Null) {
        _structDestroy(slot);
        return serReadNull(r);
    }

    if (schema->type != stType(structp))
        return serReaderFail(r, SER_Err_Schema, _SL("no schema for this structp value"));

    const StructInfo* si = NULL;

    if (schema->param[0]) {
        si = (const StructInfo*)schema->param[0]->detail;
        if (!si)
            return serReaderFail(r, SER_Err_Schema, _SL("structp target carries no StructInfo"));
    } else {
        // The dynamic form: the slot names a set of structs, or nothing at all, so the value has
        // to name itself.
        if (serPeek(r) != SER_TypeTag)
            return serReaderFail(r,
                                 SER_Err_Data,
                                 _SL("this structp slot has a dynamic type, so the value needs "
                                     "a type tag to say which struct it is"));

        string name = 0;
        if (!r->ops->readTypeTag(r, &name)) {
            strDestroy(&name);
            return false;
        }

        si = resolveStructp(r, structpSet(schema), name);
        if (!si) {
            string msg = 0;
            strFormat(&msg,
                      _SL("nothing resolves a struct named ${string} for this slot"),
                      stvar(string, name));
            serReaderFail(r, SER_Err_Type, msg);
            strDestroy(&msg);
            strDestroy(&name);
            return false;
        }
        strDestroy(&name);
    }

    // The slot owns its pointee, so reading over a live one frees what was there and allocates
    // fresh. Populating in place would be equivalent -- readStruct resets the struct anyway --
    // but only when the existing pointee is the same type, which nothing here guarantees.
    _structDestroy(slot);
    *slot = _structAlloc(si);

    return readStruct(r, si, *slot);
}

// The class a type tag names, resolved the same way a dynamic structp's is: through the slot's
// declared set when it has one, and through the reader's resolvers when it does not.
static _Ret_maybenull_ const ObjClassInfo*
resolveClass(_Inout_ SerReader* r, _In_opt_ const ClassSet* set, _In_opt_ strref name)
{
    if (set)
        return classSetFind(set, name);

    SerResolved res;
    if (!serReaderResolve(&res, r, name))
        return NULL;
    return res.clsinfo;
}

// A back-reference: the instance already exists, so this slot takes a second owning reference to
// it rather than constructing anything.
static bool readObjRefUse(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema,
                          _Inout_ ObjInst** slot)
{
    uint32 id;
    if (!r->ops->readRef(r, &id))
        return false;

    ObjInst* tgt = NULL;
    if (r->refs)
        htFind(r->refs, uint32, id, ptr, &tgt);

    if (!tgt)
        return serReaderFail(r,
                             SER_Err_Data,
                             _SL("this reference names an id that was never defined, or that "
                                 "was defined inside a value the schema skipped over"));

    // The slot's declared class still binds. A document that points a typed slot at an unrelated
    // instance is malformed, and storing it anyway would hand the consumer a pointer of the wrong
    // type with nothing left to notice by. A slot declared over a set binds the same way, to the
    // set's membership rather than to one class.
    const ObjClassInfo* want = schemaClass(schema);
    const ClassSet* set      = schemaClassSet(schema);
    if (want && !_objDynCast(tgt, (ObjClassInfo*)want))
        return serReaderFail(r,
                             SER_Err_Type,
                             _SL("this reference names an object that is not of the class this "
                                 "slot was declared to hold"));
    if (set && classSetFind(set, objClsInfo(tgt)->name) != objClsInfo(tgt))
        return serReaderFail(r,
                             SER_Err_Type,
                             _SL("this reference names an object whose class is not in the set "
                                 "this slot was declared over"));

    objRelease(slot);
    *slot = objAcquire(tgt);
    return true;
}

static bool readObject(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema, _Inout_ void* storage)
{
    ObjInst** slot = (ObjInst**)storage;

    if (serPeek(r) == SER_Null) {
        objRelease(slot);
        return serReadNull(r);
    }

    if (serPeek(r) == SER_RefUse)
        return readObjRefUse(r, schema, slot);

    const ObjClassInfo* cls = schemaClass(schema);

    // A tag overrides the schema rather than agreeing with it: the writer only emitted one
    // because the schema was not the whole answer -- a dynamic slot, or a derived instance in a
    // base-class slot.
    if (serPeek(r) == SER_TypeTag) {
        string name = 0;
        bool ok     = r->ops->readTypeTag(r, &name);

        if (ok) {
            const ObjClassInfo* tagged = resolveClass(r, schemaClassSet(schema), name);
            if (tagged) {
                cls = tagged;
            } else {
                string msg = 0;
                strFormat(&msg,
                          _SL("nothing resolves a class named ${string} for this slot"),
                          stvar(string, name));
                ok = serReaderFail(r, SER_Err_Type, msg);
                strDestroy(&msg);
            }
        }
        strDestroy(&name);
        if (!ok)
            return false;
    }

    // A definition sits between the tag and the value it names, mirroring the write side: the tag
    // says what to construct and the id has to be recorded before the members are read, so that a
    // member pointing back at this object finds it.
    bool defined = false;
    uint32 defid = 0;
    if (serPeek(r) == SER_RefDef) {
        if (!r->ops->readRef(r, &defid))
            return false;
        if (r->refs && htFind(r->refs, uint32, defid, none, NULL))
            return serReaderFail(r,
                                 SER_Err_Data,
                                 _SL("this document defines the same reference id twice"));
        defined = true;
    }

    if (!cls)
        return serReaderFail(r,
                             SER_Err_Schema,
                             _SL("this object slot names no class and the document carries no "
                                 "type tag to supply one"));
    if (cls->_abstract)
        return serReaderFail(r, SER_Err_Type, _SL("cannot construct an abstract class"));

    ObjInst* inst = _objInstCreate((ObjClassInfo*)cls);

    // Recorded before the members are read, and borrowed: the instance is owned by `inst` until
    // it is handed to the slot, and by the slot after that. A read that fails abandons the whole
    // graph, so an entry left pointing at something released is never looked at again.
    if (defined) {
        if (!r->refs)
            htInit(&r->refs, uint32, ptr, 16);
        htInsert(&r->refs, uint32, defid, ptr, inst);
    }

    // Init runs *before* the document is applied, not after as a factory would. The generated
    // init is what puts members into their default state -- it allocates containers and assigns
    // declared defaults -- so running it afterwards would overwrite and leak everything just
    // read. Doing it first is also what makes an omitted member read back as its default. The
    // cost is that a hand-written init sees defaults rather than the document's values.
    bool ok = _objInstInit(inst, (ObjClassInfo*)cls);
    if (!ok) {
        serReaderFail(r, SER_Err_Data, _SL("this object's class failed to initialize"));
    } else {
        Serializable* ser = objInstIf(inst, Serializable);
        ok                = ser ? ser->deserialize(inst, r) : readMembers(r, NULL, cls, inst);
    }

    if (!ok) {
        objRelease(&inst);
        return false;
    }

    objRelease(slot);
    *slot = inst;
    return true;
}

static bool readValue(_Inout_ SerReader* r, _In_ const STypeInfoExt* schema, stype st,
                      _Inout_ void* storage)
{
    if (!st)
        return serReaderFail(r, SER_Err_Type, _SL("no type descriptor for value"));

    if (st->ops.deserialize)
        return st->ops.deserialize(st, stStoredPtr(st, storage), r);

    switch (st->id) {
    case STypeId_none:
        return serReadNull(r);

    case STypeId_bool:
        return serReadBool(r, (bool*)storage);

    case STypeId_int8:
    case STypeId_int16:
    case STypeId_int32:
    case STypeId_int64:
        return readIntInto(r, st, storage);

    case STypeId_uint8:
    case STypeId_uint16:
    case STypeId_uint32:
    case STypeId_uint64:
        return readUintInto(r, st, storage);

    case STypeId_float32: {
        float64 v;
        if (!serReadReal(r, &v, st))
            return false;
        *(float32*)storage = (float32)v;
        return true;
    }
    case STypeId_float64:
        return serReadReal(r, (float64*)storage, st);

    case STypeId_string: {
        string* sp = (string*)storage;
        if (serPeek(r) == SER_Null) {
            strDestroy(sp);
            return serReadNull(r);
        }
        strDestroy(sp);
        return serReadStr(r, sp);
    }

    case STypeId_suid:
        return readSuid(r, storage);
    case STypeId_buffer:
        return readBuffer(r, storage);

    case STypeId_sarray:
        return readArray(r, schema, storage);
    case STypeId_hashtable:
        return readHashtable(r, schema, storage);
    case STypeId_struct: {
        if (schema->type->id != STypeId_struct)
            return serReaderFail(r, SER_Err_Schema, _SL("no schema for this struct value"));
        const StructInfo* si = (const StructInfo*)schema->detail;
        if (!si)
            return serReaderFail(r, SER_Err_Schema, _SL("struct schema carries no StructInfo"));
        devAssert(si->structsize == stGetSize(st));
        return readStruct(r, si, (StructBase*)storage);
    }
    case STypeId_structp:
        return readStructp(r, schema, storage);
    case STypeId_object:
        return readObject(r, schema, storage);

    default: {
        string msg = 0, nm = 0;
        typeName(&nm, st);
        strFormat(&msg, _SL("cannot deserialize type ${string}"), stvar(string, nm));
        bool ret = serReaderFail(r, SER_Err_Unsupported, msg);
        strDestroy(&msg);
        strDestroy(&nm);
        return ret;
    }
    }
}

_Use_decl_annotations_
bool _serRead(SerReader* r, const STypeInfoExt* schema, stgeneric* val)
{
    if (!r || !schema || !val)
        return false;
    if (r->err.code != SER_Err_None)
        return false;

    stype st = schema->type;
    if (readValue(r, schema, st, stGenPtr(st, *val)))
        return true;

    if (r->err.code == SER_Err_None)
        serReaderFail(r, SER_Err_Data, _SL("the document does not match the schema"));
    return false;
}
