#include <cx/container/hashtable.h>
#include <cx/container/sarray.h>
#include <cx/format.h>
#include <cx/serialize/sbstring.h>
#include <cx/serialize/serbinary.h>
#include <cx/serialize/serjson.h>
#include <cx/serialize/serssd.h>
#include <cx/ssdtree.h>
#include <cx/string.h>
#include <cx/struct/struct.h>

#include <math.h>

#include "objtestobj.h"

#define TEST_FILE  sertest
#define TEST_FUNCS sertest_funcs
#include "common.h"

#define CHK(cond) \
    do { \
        if (!(cond)) \
            TEST_FAIL(1, _SL("assertion failed: ${string}"), stvar(strref, _S #cond)); \
    } while (0)

// Deep, field-by-field comparison, exported from structtest.c. stCmp_struct is a raw memcmp
// over the struct's bytes, so two structurally equal instances holding a string or a container
// always compare unequal -- which makes it useless for asserting a round trip.
bool structDeepEq(const StructBase* a, const StructBase* b);
bool structDeepEqValue(stype st, const void* ap, const void* bp);

// ---------------------------------------------------------------------------------------
// Round-trip harness
// ---------------------------------------------------------------------------------------

// Writes one value into a fresh SSD tree. On failure the writer's error is moved out to the
// caller so it survives the writer.
static SSDNode* toSsd(const STypeInfoExt* schema, stgeneric val, SerError* err)
{
    SerWriter* w  = serSsdWriterCreate(0);
    SSDNode* tree = NULL;

    if (_serWrite(w, schema, val) && serWriterFinish(w)) {
        tree = serSsdWriterRoot(w);
    } else if (err) {
        *err   = w->err;
        w->err = (SerError) { 0 };
    }

    serWriterDestroy(&w);
    return tree;
}

static bool fromSsd(SSDNode* tree, const STypeInfoExt* schema, stgeneric* val)
{
    SerReader* r = serSsdReaderCreate(tree, 0);
    bool ok      = _serRead(r, schema, val);
    serReaderDestroy(&r);
    return ok;
}

// The JSON pair, over a string transport. The stream buffer is the caller's to create and
// release in this backend, so both halves of the round trip set one up and tear it down.
static bool toJson(string* out, const STypeInfoExt* schema, stgeneric val, flags_t flags,
                   SerError* err)
{
    strClear(out);

    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrCRegisterPush(sb, out)) {
        sbufRelease(&sb);
        return false;
    }

    SerWriter* w = serJsonWriterCreate(sb, flags);
    bool ok      = _serWrite(w, schema, val) && serWriterFinish(w);
    if (!ok && err) {
        *err   = w->err;
        w->err = (SerError) { 0 };
    }

    serWriterDestroy(&w);
    sbufRelease(&sb);
    return ok;
}

static bool fromJson(strref json, const STypeInfoExt* schema, stgeneric* val, flags_t flags,
                     SerError* err)
{
    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrPRegisterPull(sb, json)) {
        sbufRelease(&sb);
        return false;
    }

    SerReader* r = serJsonReaderCreate(sb, flags);
    bool ok      = _serRead(r, schema, val);
    if (!ok && err) {
        *err   = r->err;
        r->err = (SerError) { 0 };
    }

    serReaderDestroy(&r);
    sbufRelease(&sb);
    return ok;
}

// The binary pair, over the same string transport. A cx string carries its length, so it holds
// a byte run with embedded NULs as happily as it holds text.
static bool toBinary(string* out, const STypeInfoExt* schema, stgeneric val, flags_t flags,
                     SerError* err)
{
    // An output parameter replaces what was there; the string consumer appends, so the clear
    // has to happen here rather than being remembered at every call site.
    strClear(out);

    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrCRegisterPush(sb, out)) {
        sbufRelease(&sb);
        return false;
    }

    SerWriter* w = serBinaryWriterCreate(sb, flags);
    bool ok      = _serWrite(w, schema, val) && serWriterFinish(w);
    if (!ok && err) {
        *err   = w->err;
        w->err = (SerError) { 0 };
    }

    serWriterDestroy(&w);
    sbufRelease(&sb);
    return ok;
}

static bool fromBinary(strref doc, const STypeInfoExt* schema, stgeneric* val, flags_t flags,
                       SerError* err)
{
    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrPRegisterPull(sb, doc)) {
        sbufRelease(&sb);
        return false;
    }

    SerReader* r = serBinaryReaderCreate(sb, flags);
    bool ok      = _serRead(r, schema, val);
    if (!ok && err) {
        *err   = r->err;
        r->err = (SerError) { 0 };
    }

    serReaderDestroy(&r);
    sbufRelease(&sb);
    return ok;
}

static void bytesToStr(string* out, const uint8* p, uint32 n)
{
    uint8* buf = strBuffer(out, n);
    memcpy(buf, p, n);
    strSetLen(out, n);
}

static const StructMemberDesc* findMember(const StructInfo* si, strref name)
{
    for (int i = 0; i < si->nmembers; i++) {
        if (strEq(si->members[i].name, name))
            return &si->members[i];
    }
    return NULL;
}

// The class counterpart: a class's table covers only the members it declares itself, so this
// does not walk the parent chain.
static const StructMemberDesc* findClassMember(const ObjClassInfo* cls, strref name)
{
    for (int32 i = 0; i < cls->nsermembers; i++) {
        if (strEq(cls->sermembers[i].name, name))
            return &cls->sermembers[i];
    }
    return NULL;
}

static void fillTestStr1(TestStr1* s, int32 seed)
{
    string tmp = 0;

    s->intval = seed;
    saClear(&s->arrval);
    for (int32 i = 0; i < 3; i++) saPush(&s->arrval, int32, seed * 100 + i);

    strFromInt32(&tmp, seed, 10);
    strPrepend(_S "str-", &tmp);
    strDup(&s->strval, tmp);

    strDestroy(&tmp);
}

// ---------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------

// Every scalar the traverser knows, straight through the model and back. SSD stores stvars, so
// the declared stype that rides along with each numeric node is preserved exactly -- which is
// what the SER_Cap_ExactInt capability is claiming.
static int test_ser_scalars(void)
{
    SSDNode* t;

#define RT_SCALAR(type, value)                                                            \
    do {                                                                                  \
        stStorageType(type) src = (value), dst = 0;                                       \
        t = toSsd(stExt(type), stArg(type, src), NULL);                  \
        CHK(t);                                                                           \
        CHK(fromSsd(t, stExt(type), stArgPtr(type, &dst)));   \
        CHK(dst == src);                                                                  \
        objRelease(&t);                                                                   \
    } while (0)

    RT_SCALAR(bool, true);
    RT_SCALAR(int8, -0x7f);
    RT_SCALAR(int16, -0x7ffe);
    RT_SCALAR(int32, -1234567);
    RT_SCALAR(int64, INT64_MIN + 1);
    RT_SCALAR(uint8, 0xfe);
    RT_SCALAR(uint16, 0xfffe);
    RT_SCALAR(uint32, 0xfffffffeu);
    RT_SCALAR(uint64, UINT64_MAX);
    RT_SCALAR(float32, 0.5f);
    RT_SCALAR(float64, 1.0 / 3.0);

#undef RT_SCALAR

    return 0;
}

static int test_ser_string(void)
{
    string src = 0, dst = 0;
    strDup(&src, _S "hello ünïcöde world");

    SSDNode* t = toSsd(stExt(string), stArg(string, src), NULL);
    CHK(t);
    CHK(fromSsd(t, stExt(string), stArgPtr(string, &dst)));
    if (!strEq(src, dst))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, src), stvar(strref, dst));

    objRelease(&t);
    strDestroy(&src);
    strDestroy(&dst);
    return 0;
}

// suid is the type SER_Cap_Bytes was introduced for: a format that can carry 16 raw bytes does,
// and one that cannot spells it in base32 -- the canonical text form, not the backend's base64
// projection, because base32 is the spelling that keeps a suid sorting the way the type promises.
// The read side takes either regardless of what it would have written itself.
static int test_ser_suid(void)
{
    SUID src, dst;
    suidGen(&src, 7);

    string enc = 0;
    suidEncode(&enc, &src);

    // SSD advertises no byte node, so this is the text path, and it round-trips through it.
    memset(&dst, 0, sizeof(dst));
    SSDNode* t = toSsd(stExt(suid), stArg(suid, src), NULL);
    CHK(t);
    CHK(fromSsd(t, stExt(suid), stArgPtr(suid, &dst)));
    CHK(suidEq(&src, &dst));
    objRelease(&t);

    // JSON: the base32 spelling, and nothing else on the wire.
    string doc = 0, expected = 0;
    strFormat(&expected, _SL("\"${string}\""), stvar(string, enc));
    CHK(toJson(&doc, stExt(suid), stArg(suid, src), 0, NULL));
    if (!strEq(doc, expected))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, doc), stvar(strref, expected));

    memset(&dst, 0, sizeof(dst));
    CHK(fromJson(doc, stExt(suid), stArgPtr(suid, &dst), 0, NULL));
    CHK(suidEq(&src, &dst));

    // Binary advertises SER_Cap_Bytes, so the text form does not appear at all.
    CHK(toBinary(&doc, stExt(suid), stArg(suid, src), 0, NULL));
    CHK(strFind(doc, 0, enc) == -1);

    memset(&dst, 0, sizeof(dst));
    CHK(fromBinary(doc, stExt(suid), stArgPtr(suid, &dst), 0, NULL));
    CHK(suidEq(&src, &dst));

    // Raw suids are big-endian, so the wire order matches the sort order the string form has.
    // Two ids differing only in the top byte of `high` must differ in the first byte written.
    SUID a = { .high = 0x0102030405060708ULL, .low = 0x090a0b0c0d0e0f10ULL };
    string adoc = 0;
    CHK(toBinary(&adoc, stExt(suid), stArg(suid, a), 0, NULL));
    CHK(strFind(adoc, 0, _S "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10") >=
        0);

    strDestroy(&adoc);
    strDestroy(&expected);
    strDestroy(&doc);
    strDestroy(&enc);
    return 0;
}

// A buffer is a byte run and nothing else: the backend decides whether that is raw bytes, base64,
// or a refusal, and the traverser hands it over without asking which.
static int test_ser_buffer(void)
{
    // Embedded NULs and high bytes -- the parts a text projection has to survive.
    static const uint8 raw[] = { 0x00, 0xff, 0x41, 0x00, 0x80, 0x7f, 0x01 };

    Buffer src = bufCreate(sizeof(raw));
    memcpy(src->data, raw, sizeof(raw));
    src->len = sizeof(raw);

    Buffer dst = NULL;
    string doc = 0;

    CHK(toJson(&doc, stExt(buffer), stArg(buffer, src), 0, NULL));
    if (!strEq(doc, _S "\"AP9BAIB/AQ==\""))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, doc), stvar(strref, _S "\"AP9BAIB/AQ==\""));
    CHK(fromJson(doc, stExt(buffer), stArgPtr(buffer, &dst), 0, NULL));
    CHK(dst && dst->len == sizeof(raw) && memcmp(dst->data, raw, sizeof(raw)) == 0);

    // Reading into a live buffer replaces it rather than leaking it.
    CHK(toBinary(&doc, stExt(buffer), stArg(buffer, src), 0, NULL));
    CHK(fromBinary(doc, stExt(buffer), stArgPtr(buffer, &dst), 0, NULL));
    CHK(dst && dst->len == sizeof(raw) && memcmp(dst->data, raw, sizeof(raw)) == 0);

    // An SSD tree has no node a byte run maps onto, which is a refusal and not a projection.
    SerError err = { 0 };
    SSDNode* t   = toSsd(stExt(buffer), stArg(buffer, src), &err);
    CHK(!t);
    if (err.code != SER_Err_Unsupported)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
    serErrorDestroy(&err);

    // A NULL buffer is a distinct state from an empty one and comes back as the same state.
    Buffer none = NULL;
    CHK(toJson(&doc, stExt(buffer), stArg(buffer, none), 0, NULL));
    if (!strEq(doc, _S "null"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, doc), stvar(strref, _S "null"));
    CHK(fromJson(doc, stExt(buffer), stArgPtr(buffer, &dst), 0, NULL));
    CHK(!dst);

    strDestroy(&doc);
    bufDestroy(&dst);
    bufDestroy(&src);
    return 0;
}

static int test_ser_array(void)
{
    sa_int32 src = { 0 }, dst = { 0 };
    saInit(&src, int32, 4);
    for (int32 i = 0; i < 5; i++) saPush(&src, int32, i * 7 - 3);

    const STypeInfoExt arrschema = { .type = stType(sarray), .param = { stExt(int32) } };
    SSDNode* t              = toSsd(&arrschema, stArg(sarray, src), NULL);
    CHK(t);
    CHK(fromSsd(t, &arrschema, stArgPtr(sarray, &dst)));

    CHK(saSize(dst) == 5);
    for (int32 i = 0; i < 5; i++) {
        if (dst.a[i] != i * 7 - 3)
            TEST_FAIL(1, _SL("dst.a[${int}]: expected ${int}, got ${int}"), stvar(int32, i),
                      stvar(int32, i * 7 - 3), stvar(int32, dst.a[i]));
    }

    objRelease(&t);
    saDestroy(&src);
    saDestroy(&dst);
    return 0;
}

static int test_ser_hashtable(void)
{
    hashtable src = NULL, dst = NULL;
    htInit(&src, string, int32, 4);
    htInsert(&src, string, _S "one", int32, 1);
    htInsert(&src, string, _S "two", int32, 2);
    htInsert(&src, string, _S "three", int32, 3);

    const STypeInfoExt htschema = {
        .type  = stType(hashtable),
        .param = { stExt(string), stExt(int32) }
    };
    stype ht   = stType(hashtable);
    SSDNode* t = toSsd(&htschema, stArg(hashtable, src), NULL);
    CHK(t);
    CHK(fromSsd(t, &htschema, stArgPtr(hashtable, &dst)));

    CHK(structDeepEqValue(ht, &src, &dst));

    objRelease(&t);
    htDestroy(&src);
    htDestroy(&dst);
    return 0;
}

// SSD hashtable nodes are keyed by strings, so the backend does not advertise
// SER_Cap_NonStringKeys and the traverser takes the pair-array projection instead. That path is
// shared with the JSON backend, which has the same restriction.
static int test_ser_intkeys(void)
{
    hashtable src = NULL, dst = NULL;
    htInit(&src, int32, string, 4);
    htInsert(&src, int32, 10, string, _S "ten");
    htInsert(&src, int32, 20, string, _S "twenty");

    const STypeInfoExt htschema = {
        .type  = stType(hashtable),
        .param = { stExt(int32), stExt(string) }
    };
    stype ht   = stType(hashtable);
    SSDNode* t = toSsd(&htschema, stArg(hashtable, src), NULL);
    CHK(t);

    // the projection is an array of two-element arrays, not a map
    CHK(ssdnodeIsArray(t));
    CHK(ssdCount(t, NULL, false) == 2);

    CHK(fromSsd(t, &htschema, stArgPtr(hashtable, &dst)));
    CHK(structDeepEqValue(ht, &src, &dst));

    objRelease(&t);
    htDestroy(&src);
    htDestroy(&dst);
    return 0;
}

static int test_ser_struct(void)
{
    TestStr1 src, dst;
    structInit(TestStr1, &src);
    structInit(TestStr1, &dst);
    fillTestStr1(&src, 7);

    SSDNode* t = toSsd(stExt(TestStr1), stArg(TestStr1, src), NULL);
    CHK(t);

    // a struct is a map keyed by member name
    CHK(ssdnodeIsHashtable(t));
    CHK(ssdVal(int32, t, _S "intval", 0) == 7);

    CHK(fromSsd(t, stExt(TestStr1), stArgPtr(TestStr1, &dst)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    objRelease(&t);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// TestStr2 is the deep case: an embedded struct, a hashtable, an array of structs, a fixed C
// array, and a hashtable whose values are arrays of arrays of structs. Every backend gets put
// through it, so building one lives here rather than in any one test.
static int fillTestStr2(TestStr2* src)
{
    strDup(&src->yahoo, _S "top level");
    fillTestStr1(&src->subst, 1);

    for (int32 i = 0; i < 50; i++) src->fixedarr[i] = i * 3;

    htInit(&src->tags, string, int32, 4);
    htInsert(&src->tags, string, _S "alpha", int32, 100);
    htInsert(&src->tags, string, _S "beta", int32, 200);

    for (int32 i = 0; i < 3; i++) {
        TestStr1 tmp;
        structInit(TestStr1, &tmp);
        fillTestStr1(&tmp, 10 + i);
        saPush(&src->arrayofstructs, TestStr1, tmp);
        structDestroyMembers(&tmp);
    }

    // hashtable[string, sarray[sarray[struct[TestStr1]]]] -- the containers below hold no
    // identity beyond their own (bare sarray/hashtable), matching what the read path now
    // produces: only the schema, not the runtime type, carries the nominal element type.
    htInit(&src->hashofdblarrstr, string, sarray, 2);
    {
        sa_ref oarr = { 0 };
        saInit(&oarr, sarray, 2);

        for (int32 i = 0; i < 2; i++) {
            sa_ref iarr = { 0 };
            saInit(&iarr, TestStr1, 2);

            for (int32 j = 0; j < 2; j++) {
                TestStr1 tmp;
                structInit(TestStr1, &tmp);
                fillTestStr1(&tmp, 20 + i * 10 + j);
                saPush(&iarr, TestStr1, tmp);
                structDestroyMembers(&tmp);
            }

            saPush(&oarr, sarray, iarr);
            saDestroy(&iarr);
        }

        htInsert(&src->hashofdblarrstr, string, _S "deep", sarray, oarr);
        saDestroy(&oarr);
    }

    return 0;
}

// Schema is mandatory for a struct, so this always drives the round trip through the
// generated one -- test_ser_schema exercises the same fixture through member-level schemas too.
static int nestedRoundTrip(const STypeInfoExt* schema)
{
    TestStr2 src, dst;
    structInit(TestStr2, &src);
    structInit(TestStr2, &dst);
    CHK(fillTestStr2(&src) == 0);

    SSDNode* t = toSsd(schema, stArg(TestStr2, src), NULL);
    CHK(t);
    CHK(fromSsd(t, schema, stArgPtr(TestStr2, &dst)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    objRelease(&t);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

static int test_ser_nested(void)
{
    return nestedRoundTrip(stExt(TestStr2));
}

// arrsize was emitted by cxautogen and read by nothing until the struct repairs; a fixed
// member with a managed element type is where getting it wrong shows up.
static int test_ser_fixedarray(void)
{
    TestStrFixed src, dst;
    structInit(TestStrFixed, &src);
    structInit(TestStrFixed, &dst);

    for (int i = 0; i < 4; i++) {
        string tmp = 0;
        strFromInt32(&tmp, i, 10);
        strPrepend(_S "name-", &tmp);
        strDup(&src.names[i], tmp);
        strDestroy(&tmp);
    }
    for (int i = 0; i < 3; i++) fillTestStr1(&src.substs[i], 30 + i);
    for (int i = 0; i < 8; i++) src.nums[i] = i * i;

    SSDNode* t =
        toSsd(stExt(TestStrFixed), stArg(TestStrFixed, src), NULL);
    CHK(t);

    // exactly arrsize elements, not one
    SSDNode* names = ssdSubtree(t, _S "names", SSD_Create_None);
    CHK(names);
    CHK(ssdCount(names, NULL, false) == 4);
    objRelease(&names);

    CHK(fromSsd(t, stExt(TestStrFixed), stArgPtr(TestStrFixed, &dst)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    objRelease(&t);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// Reading into a struct that already holds values replaces them rather than leaking or
// appending. The traverser destroys what is in each slot before writing, so it is equally safe
// to read into a fresh struct or over a live one.
static int test_ser_overwrite(void)
{
    TestStr1 src, dst;
    structInit(TestStr1, &src);
    structInit(TestStr1, &dst);

    fillTestStr1(&src, 5);
    fillTestStr1(&dst, 99);   // dst starts out holding a different string and array

    SSDNode* t = toSsd(stExt(TestStr1), stArg(TestStr1, src), NULL);
    CHK(t);
    CHK(fromSsd(t, stExt(TestStr1), stArgPtr(TestStr1, &dst)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));
    CHK(saSize(dst.arrval) == 3);

    objRelease(&t);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// A type the traverser cannot decompose has to fail with a usable location, not silently produce
// a partial document.
static int test_ser_errors(void)
{
    TestStrP src;
    structInit(TestStrP, &src);
    // A member holding its default is omitted, so the unsupported one has to actually hold
    // something for the traverser to reach it.
    src.testobj = testcls4Create();

    SerError err = { 0 };
    SSDNode* t   = toSsd(stExt(TestStrP), stArg(TestStrP, src), &err);

    CHK(!t);
    if (err.code != SER_Err_Unsupported)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
    // TestCls4 never opted in to serialization, so it has no wire name and cannot be written
    if (!strEq(err.path, _S "/testobj"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, err.path), stvar(strref, _S "/testobj"));

    serErrorDestroy(&err);
    structDestroyMembers(&src);

    // A never-initialized container is a distinct state from an empty one, and comes back as
    // the same state rather than as an empty container.
    TestStr1 empty, back;
    structInit(TestStr1, &empty);
    structInit(TestStr1, &back);

    t = toSsd(stExt(TestStr1), stArg(TestStr1, empty), NULL);
    CHK(t);
    CHK(fromSsd(t, stExt(TestStr1), stArgPtr(TestStr1, &back)));
    CHK(!back.arrval.a);
    CHK(structDeepEq(STRUCTBASE(&empty), STRUCTBASE(&back)));

    objRelease(&t);
    structDestroyMembers(&empty);
    structDestroyMembers(&back);
    return 0;
}

// A document written by a newer build carries members this one has never heard of. Skipping
// them is what makes the format forward-readable; SER_Strict turns that back into an error
// for callers who would rather know.
static int test_ser_unknown(void)
{
    SSDNode* t = ssdCreateHashtable();
    ssdSet(t, _S "intval", true, stvar(int32, 42));
    ssdSet(t, _S "strval", true, stvar(string, _S "kept"));
    ssdSet(t, _S "mystery", true, stvar(int32, 9));

    TestStr1 dst;
    structInit(TestStr1, &dst);

    SerReader* r = serSsdReaderCreate(t, 0);
    CHK(_serRead(r, stExt(TestStr1), stArgPtr(TestStr1, &dst)));
    serReaderDestroy(&r);

    if (dst.intval != 42)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 42), stvar(int32, dst.intval));
    if (!strEq(dst.strval, _S "kept"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, dst.strval), stvar(strref, _S "kept"));
    // a member the document does not mention keeps whatever the struct was initialized with
    CHK(!dst.arrval.a);

    TestStr1 strict;
    structInit(TestStr1, &strict);

    r = serSsdReaderCreate(t, SER_Strict);
    CHK(!_serRead(r, stExt(TestStr1), stArgPtr(TestStr1, &strict)));
    if (r->err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, r->err.code));
    if (!strEq(r->err.path, _S "/mystery"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, r->err.path), stvar(strref, _S "/mystery"));
    serReaderDestroy(&r);

    structDestroyMembers(&dst);
    structDestroyMembers(&strict);
    objRelease(&t);
    return 0;
}

// ---------------------------------------------------------------------------------------
// Schema descriptors and type resolution
// ---------------------------------------------------------------------------------------

static bool testResolver(SerResolved* out, strref name, void* user)
{
    memset(out, 0, sizeof(SerResolved));
    if (!strEq(name, _S "TestStr1"))
        return false;

    out->type       = stType(TestStr1);
    out->structinfo = &TestStr1_structinfo;
    return true;
}

static int test_ser_schema(void)
{
    // Carrying a schema alongside the value changes nothing for a backend that ignores type
    // tags, which is the point: the schema exists to avoid paying for tags when the type is
    // statically known, not to make the traversal work.
    TestStr1 src, dst;
    structInit(TestStr1, &src);
    structInit(TestStr1, &dst);
    fillTestStr1(&src, 3);

    SSDNode* t = toSsd(stExt(TestStr1), stArg(TestStr1, src), NULL);
    CHK(t);
    CHK(fromSsd(t, stExt(TestStr1), stArgPtr(TestStr1, &dst)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));
    objRelease(&t);

    structDestroyMembers(&src);
    structDestroyMembers(&dst);

    // A parameterized schema descends with the value: param[0] is what the element level is
    // declared to be.
    const StructMemberDesc* marr = findMember(&TestStr1_structinfo, _S "arrval");
    CHK(marr && marr->schema);

    sa_int32 arr = { 0 }, arrback = { 0 };
    saInit(&arr, int32, 4);
    saPush(&arr, int32, 11);
    saPush(&arr, int32, 22);

    t        = toSsd(marr->schema, stArg(sarray, arr), NULL);
    CHK(t);
    CHK(fromSsd(t, marr->schema, stArgPtr(sarray, &arrback)));
    CHK(saSize(arrback) == 2 && arrback.a[1] == 22);

    objRelease(&t);
    saDestroy(&arr);
    saDestroy(&arrback);

    // The whole deeply nested fixture, driven by its generated schema rather than NULL.
    return nestedRoundTrip(stExt(TestStr2));
}

// What cxautogen emitted. stype answers "how do I operate on this value" and answers it
// loosely -- a class collapses to object, and a container's element types live in the container
// header rather than the descriptor. STypeInfoExt answers "what is this slot declared to be",
// nominally and exactly, at every level of nesting, which is what these assertions are for.
static int test_ser_generated(void)
{
    // A struct level is nominal: it carries the wire name and its StructInfo.
    const STypeInfoExt* s1 = stExt(TestStr1);
    CHK(s1->type == stType(TestStr1));
    if (!strEq(s1->name, _S "TestStr1"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, s1->name), stvar(strref, _S "TestStr1"));
    CHK(s1->detail == &TestStr1_structinfo);

    // and the back-edge, so anything that arrives at a struct by name can reach its descriptor
    CHK(TestStr1_structinfo.type == stType(TestStr1));

    // Structural levels carry no name -- the wire-name table in sertype.c names them, so the
    // format owns its own contract rather than inheriting a spelling from a .cxh file.
    const StructMemberDesc* m = findMember(&TestStr1_structinfo, _S "arrval");
    CHK(m && m->schema);
    CHK(m->schema->type == stType(sarray) && !m->schema->name);
    CHK(m->schema->param[0] && m->schema->param[0]->type == stType(int32));

    // hashtable[string, sarray[sarray[struct[TestStr1]]]] -- the deep case, exact at every
    // level, where the runtime descriptor for the innermost array knows only "struct".
    m = findMember(&TestStr2_structinfo, _S "hashofdblarrstr");
    CHK(m && m->schema);
    CHK(m->schema->type == stType(hashtable));
    CHK(m->schema->param[0] == stExt(string));
    const STypeInfoExt* outer = m->schema->param[1];
    CHK(outer && outer->type == stType(sarray));
    const STypeInfoExt* inner = outer->param[0];
    CHK(inner && inner->type == stType(sarray));
    CHK(inner->param[0] == stExt(TestStr1));

    // Descriptors dedupe across the whole file: two structs declaring sarray[int32] share one,
    // and so do a struct member and the element level of a container that holds it.
    CHK(findMember(&TestStr1_structinfo, _S "arrval")->schema ==
        findMember(&TestStrDup_structinfo, _S "arrval")->schema);
    CHK(findMember(&TestStr2_structinfo, _S "tags")->schema ==
        findMember(&TestStrDup_structinfo, _S "tags")->schema);
    CHK(findMember(&TestStr2_structinfo, _S "subst")->schema == stExt(TestStr1));

    // A fixed C array member is described by its element type; arrsize is what makes it a
    // sequence, and it lives on the member rather than in the schema.
    m = findMember(&TestStrFixed_structinfo, _S "substs");
    CHK(m && m->arrsize == 3);
    CHK(m->schema == stExt(TestStr1));

    // structp is structural too: the name belongs to the struct it points at.
    m = findMember(&TestStrP_structinfo, _S "knownptr");
    CHK(m && m->schema);
    CHK(m->schema->type == stType(structp) && !m->schema->name);
    CHK(m->schema->param[0] == stExt(TestStr1));

    // ...unless the pointee is a whole set, in which case the value names itself and the set
    // is what resolves the name on the way back in.
    m = findMember(&TestStrP_structinfo, _S "dynptr");
    CHK(m && m->schema);
    CHK(m->schema->type == stType(structp) && !m->schema->param[0]);
    CHK(m->schema->flags & STIE_TypeSet);
    CHK(m->schema->detail == &TestStrSet_structset);

    // An object slot over a classset is the same arrangement one level up: STIE_TypeSet is what
    // says `ext` is a vocabulary rather than the one class the slot holds.
    m = findClassMember(&SerSetHolder_clsinfo, _S "item");
    CHK(m && m->schema);
    CHK(m->schema->type == stType(object) && !m->schema->name);
    CHK(m->schema->flags & STIE_TypeSet);
    CHK(m->schema->detail == &SerClsSet_classset);
    CHK(m->schema == stExt(SerClsSet));

    // ...and it composes, so an array of them is an array level over the set's descriptor
    // rather than over the bare `object` one a dynamic slot would use.
    m = findClassMember(&SerSetHolder_clsinfo, _S "items");
    CHK(m && m->schema && m->schema->type == stType(sarray));
    CHK(m->schema->param[0] == stExt(SerClsSet));
    CHK(m->schema->param[0] != stExt(object));

    // A single named class still puts the class itself in ext, with no set flag to confuse it
    // for a vocabulary.
    m = findClassMember(&SerHolder_clsinfo, _S "child");
    CHK(m && m->schema && m->schema->type == stType(object));
    CHK(!(m->schema->flags & STIE_TypeSet));
    CHK(m->schema->detail == &SerCls1_clsinfo);

    return 0;
}

// Wire names for structural levels come from one compile-time table that also backs the
// built-in resolver, so the write and read sides cannot disagree about them.
static int test_ser_resolve(void)
{
    if (!strEq(_serBuiltinName(STypeId_int32), _S "int32"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, _serBuiltinName(STypeId_int32)), stvar(strref, _S "int32"));
    if (!strEq(_serBuiltinName(STypeId_string), _S "string"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, _serBuiltinName(STypeId_string)), stvar(strref, _S "string"));
    if (!strEq(_serBuiltinName(STypeId_hashtable), _S "hashtable"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, _serBuiltinName(STypeId_hashtable)), stvar(strref, _S "hashtable"));
    // strref shares an ID with string, and the concrete spelling is the one on the wire
    if (!strEq(_serBuiltinName(STypeId_strref), _S "string"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, _serBuiltinName(STypeId_strref)), stvar(strref, _S "string"));

    SerResolved res;
    CHK(_serResolveBuiltin(&res, _S "uint16"));
    CHK(stEq(res.type, stType(uint16)));
    CHK(!res.structinfo && !res.clsinfo);

    // A bare structural name that needs parameters is not resolvable on its own
    CHK(!_serResolveBuiltin(&res, _S "sarray"));
    CHK(!_serResolveBuiltin(&res, _S "TestStr1"));

    // Resolution is per-reader with no global registry, so the meaning of a stream never
    // depends on what happened to be linked into the process.
    SSDNode* tree = ssdCreateSingle();
    SerReader* r  = serSsdReaderCreate(tree, 0);

    CHK(!serReaderResolve(&res, r, _S "TestStr1"));
    serReaderAddResolver(r, testResolver, NULL);
    CHK(serReaderResolve(&res, r, _S "TestStr1"));
    CHK(res.structinfo == &TestStr1_structinfo);

    // cx's own names still resolve behind an application resolver that declines
    CHK(serReaderResolve(&res, r, _S "float64"));
    CHK(stEq(res.type, stType(float64)));

    serReaderDestroy(&r);

    // A structset is a resolver: cxautogen already emits the sorted table, so registering one
    // is all it takes to make every struct in the set constructible from its wire name.
    r = serSsdReaderCreate(tree, 0);
    serReaderAddResolver(r, serStructSetResolver, (void*)&TestStrSet_structset);

    CHK(serReaderResolve(&res, r, _S "TestStr2"));
    CHK(res.structinfo == &TestStr2_structinfo);
    CHK(stEq(res.type, stType(TestStr2)));
    // a struct outside the set stays unknown -- the set is the declared vocabulary
    CHK(!serReaderResolve(&res, r, _S "TestStrFixed"));

    serReaderDestroy(&r);
    objRelease(&tree);
    return 0;
}

// ---------------------------------------------------------------------------------------
// JSON backend
// ---------------------------------------------------------------------------------------

// The text, exactly. A round trip proves the two halves agree with each other; only pinning the
// output proves they agree with JSON.
static int test_ser_json(void)
{
    TestStr1 src, dst;
    structInit(TestStr1, &src);
    structInit(TestStr1, &dst);
    fillTestStr1(&src, 7);

    string js = 0;
    CHK(toJson(&js, stExt(TestStr1), stArg(TestStr1, src), 0, NULL));
    if (!strEq(js, _S "{ \"intval\": 7, \"arrval\": [ 700, 701, 702 ], \"strval\": \"str-7\" }"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "{ \"intval\": 7, \"arrval\": [ 700, 701, 702 ], \"strval\": \"str-7\" }"));

    CHK(fromJson(js, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    // Pretty and compact are the same document with different whitespace, so both read back.
    string pretty = 0, compact = 0;
    CHK(toJson(&pretty,
               stExt(TestStr1),
               stArg(TestStr1, src),
               SER_JSON_Pretty,
               NULL));
    CHK(toJson(&compact,
               stExt(TestStr1),
               stArg(TestStr1, src),
               SER_JSON_Compact,
               NULL));
    if (!strEq(compact, _S "{\"intval\":7,\"arrval\":[700,701,702],\"strval\":\"str-7\"}"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, compact), stvar(strref, _S "{\"intval\":7,\"arrval\":[700,701,702],\"strval\":\"str-7\"}"));
    CHK(strLen(pretty) > strLen(js));

    structDestroyMembers(&dst);
    structInit(TestStr1, &dst);
    CHK(fromJson(pretty, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    strDestroy(&js);
    strDestroy(&pretty);
    strDestroy(&compact);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// The whole nested fixture, and the fixed-array fixture, through text and back. This is where a
// projection that only looks right in isolation falls over.
static int test_ser_jsonnested(void)
{
    TestStrFixed src, dst;
    structInit(TestStrFixed, &src);
    structInit(TestStrFixed, &dst);

    for (int i = 0; i < 4; i++) {
        string tmp = 0;
        strFromInt32(&tmp, i, 10);
        strPrepend(_S "name-", &tmp);
        strDup(&src.names[i], tmp);
        strDestroy(&tmp);
    }
    for (int i = 0; i < 3; i++) fillTestStr1(&src.substs[i], 30 + i);
    for (int i = 0; i < 8; i++) src.nums[i] = i * i;

    string js = 0;
    CHK(toJson(&js,
               stExt(TestStrFixed),
               stArg(TestStrFixed, src),
               0,
               NULL));
    CHK(fromJson(js,
                 stExt(TestStrFixed),
                 stArgPtr(TestStrFixed, &dst),
                 0,
                 NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    strDestroy(&js);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);

    // A hashtable with non-string keys takes the pair-array projection, since JSON object keys
    // are strings and the backend says so by not advertising the capability.
    hashtable hsrc = NULL, hdst = NULL;
    htInit(&hsrc, int32, string, 4);
    htInsert(&hsrc, int32, 10, string, _S "ten");

    const STypeInfoExt htschema = {
        .type  = stType(hashtable),
        .param = { stExt(int32), stExt(string) }
    };
    stype ht = stType(hashtable);
    CHK(toJson(&js, &htschema, stArg(hashtable, hsrc), SER_JSON_Compact, NULL));
    if (!strEq(js, _S "[[10,\"ten\"]]"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "[[10,\"ten\"]]"));
    CHK(fromJson(js, &htschema, stArgPtr(hashtable, &hdst), 0, NULL));
    CHK(structDeepEqValue(ht, &hsrc, &hdst));

    strDestroy(&js);
    htDestroy(&hsrc);
    htDestroy(&hdst);

    // A byte run becomes base64. Nothing the traverser knows how to decompose reaches for this
    // yet -- it is there for a custom type's write hook -- so it is exercised through the node
    // emitters directly, the same way such a hook would.
    static const uint8 bytes[] = { 0x00, 0xff, 0x10, 0x42, 0xc3, 0x7a, 0x01 };
    StreamBuffer* sb           = sbufCreate(256);
    CHK(sbufStrCRegisterPush(sb, &js));

    SerWriter* w = serJsonWriterCreate(sb, SER_JSON_Compact);
    CHK(serWriteBytes(w, bytes, sizeof(bytes)));
    CHK(serWriterFinish(w));
    serWriterDestroy(&w);
    sbufRelease(&sb);
    if (!strEq(js, _S "\"AP8QQsN6AQ==\""))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "\"AP8QQsN6AQ==\""));

    sb = sbufCreate(256);
    CHK(sbufStrPRegisterPull(sb, js));
    SerReader* r = serJsonReaderCreate(sb, 0);
    Buffer back  = NULL;
    CHK(serReadBytes(r, &back));
    serReaderDestroy(&r);
    sbufRelease(&sb);

    CHK(back && back->len == sizeof(bytes));
    CHK(memcmp(back->data, bytes, sizeof(bytes)) == 0);
    bufDestroy(&back);

    strDestroy(&js);
    return 0;
}

// JSON has one number type and it is a double. Every row here is a place the data model is
// wider than that, and the projection has to be defined rather than improvised.
static int test_ser_jsonnum(void)
{
    string js = 0;

#define RT_JSON(type, value)                                                                     \
    do {                                                                                         \
        stStorageType(type) src = (value), dst = 0;                                              \
        strClear(&js);                                                                           \
        CHK(toJson(&js, stExt(type), stArg(type, src), SER_JSON_Compact, NULL)); \
        CHK(fromJson(js, stExt(type), stArgPtr(type, &dst), 0, NULL)); \
        CHK(dst == src);                                                                         \
    } while (0)

    // Inside the range a double holds exactly, an integer is a JSON number.
    RT_JSON(int32, -1234567);
    if (!strEq(js, _S "-1234567"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "-1234567"));
    RT_JSON(int64, 9007199254740991);   // 2^53 - 1, the last exact one
    if (!strEq(js, _S "9007199254740991"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "9007199254740991"));

    // Outside it, a decimal string -- a consumer that only has doubles would round the number
    // form silently, and the reader takes either.
    RT_JSON(int64, INT64_MIN + 1);
    if (!strEq(js, _S "\"-9223372036854775807\""))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "\"-9223372036854775807\""));
    RT_JSON(uint64, UINT64_MAX);
    if (!strEq(js, _S "\"18446744073709551615\""))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "\"18446744073709551615\""));

    // A float32 is written in the shortest form that reads back as the same float32, not as the
    // double it becomes when widened.
    RT_JSON(float32, 0.1f);
    if (!strEq(js, _S "0.1"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "0.1"));
    RT_JSON(float64, 1.0 / 3.0);
    RT_JSON(bool, true);
    if (!strEq(js, _S "true"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "true"));

#undef RT_JSON

    // NaN and Inf have no JSON spelling, so they go out as null and come back as NaN.
    float32 nan32 = NAN, back = 0.0f;
    strClear(&js);
    CHK(toJson(&js, stExt(float32), stArg(float32, nan32), 0, NULL));
    if (!strEq(js, _S "null"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "null"));
    CHK(fromJson(js, stExt(float32), stArgPtr(float32, &back), 0, NULL));
    CHK(isnan(back));

    // ...unless the caller would rather be told.
    SerError err = { 0 };
    strClear(&js);
    CHK(!toJson(&js, stExt(float32), stArg(float32, nan32), SER_Strict, &err));
    if (err.code != SER_Err_Unsupported)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
    serErrorDestroy(&err);

    // The reader takes the number form of a big integer too, which is what a hand-written
    // document or another producer will have.
    int64 big = 0;
    CHK(fromJson(_S "9223372036854775807", stExt(int64), stArgPtr(int64, &big), 0, NULL));
    if (big != INT64_MAX)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int64, INT64_MAX), stvar(int64, big));

    strDestroy(&js);
    return 0;
}

// Omitting members that hold their default is what keeps the output small; it only works
// because an absent member reads back as the default rather than as whatever was in the slot.
static int test_ser_jsondefaults(void)
{
    TestStr1 empty, dst;
    structInit(TestStr1, &empty);
    structInit(TestStr1, &dst);

    string js = 0;
    CHK(toJson(&js,
               stExt(TestStr1),
               stArg(TestStr1, empty),
               SER_JSON_Compact,
               NULL));
    if (!strEq(js, _S "{}"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "{}"));

    // ...and SER_EmitDefaults says so explicitly, for a consumer that wants every field present
    strClear(&js);
    CHK(toJson(&js,
               stExt(TestStr1),
               stArg(TestStr1, empty),
               SER_JSON_Compact | SER_EmitDefaults,
               NULL));
    if (!strEq(js, _S "{\"intval\":0,\"arrval\":null,\"strval\":\"\"}"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "{\"intval\":0,\"arrval\":null,\"strval\":\"\"}"));

    // Reading a document that omits a member resets that member rather than leaving whatever
    // the destination happened to hold, which is what makes the omission lossless.
    fillTestStr1(&dst, 42);
    CHK(fromJson(_S "{\"intval\":5}",
                 stExt(TestStr1),
                 stArgPtr(TestStr1, &dst),
                 0,
                 NULL));
    if (dst.intval != 5)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 5), stvar(int32, dst.intval));
    CHK(strEmpty(dst.strval));
    CHK(!dst.arrval.a);

    strDestroy(&js);
    structDestroyMembers(&empty);
    structDestroyMembers(&dst);
    return 0;
}

// A document is the one thing in the system the program did not write, so every way it can be
// wrong needs to come back as a located error rather than as a partially filled struct.
static int test_ser_jsonerrors(void)
{
    TestStr1 dst;
    structInit(TestStr1, &dst);
    SerError err = { 0 };

    // An unknown key is skipped, whatever it contains -- that is what makes a document written
    // by a newer build readable by this one.
    CHK(fromJson(_S "{\"intval\":1,\"mystery\":{\"a\":[1,2,{\"b\":null}]},\"strval\":\"kept\"}",
                 stExt(TestStr1),
                 stArgPtr(TestStr1, &dst),
                 0,
                 NULL));
    if (dst.intval != 1)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 1), stvar(int32, dst.intval));
    if (!strEq(dst.strval, _S "kept"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, dst.strval), stvar(strref, _S "kept"));

    // ...or an error, for a caller who would rather know
    CHK(!fromJson(_S "{\"intval\":1,\"mystery\":2}",
                  stExt(TestStr1),
                  stArgPtr(TestStr1, &dst),
                  SER_Strict,
                  &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    if (!strEq(err.path, _S "/mystery"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, err.path), stvar(strref, _S "/mystery"));
    serErrorDestroy(&err);

    // A value of the wrong type is located at the member it was found in
    CHK(!fromJson(_S "{\"intval\":\"seven\"}",
                  stExt(TestStr1),
                  stArgPtr(TestStr1, &dst),
                  0,
                  &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    if (!strEq(err.path, _S "/intval"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, err.path), stvar(strref, _S "/intval"));
    serErrorDestroy(&err);

    // ...and so is a syntax error, with the parser's own message carried through
    CHK(!fromJson(_S "{\"intval\": }",
                  stExt(TestStr1),
                  stArgPtr(TestStr1, &dst),
                  0,
                  &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    if (!strEq(err.path, _S "/intval"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, err.path), stvar(strref, _S "/intval"));
    CHK(!strEmpty(err.msg));
    serErrorDestroy(&err);

    // An integer that does not fit the slot it was declared for is an overflow, not a wrap
    CHK(!fromJson(_S "{\"intval\":4294967296}",
                  stExt(TestStr1),
                  stArgPtr(TestStr1, &dst),
                  0,
                  &err));
    if (err.code != SER_Err_Overflow)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Overflow), stvar(int32, err.code));
    serErrorDestroy(&err);

    structDestroyMembers(&dst);
    return 0;
}

// ---------------------------------------------------------------------------------------
// Binary backend
// ---------------------------------------------------------------------------------------

// Byte offsets into the header, which every document starts with.
#define BIN_HDR_FLAGS 5

static int test_ser_binary(void)
{
    TestStr1 src, dst;
    structInit(TestStr1, &src);
    structInit(TestStr1, &dst);
    fillTestStr1(&src, 7);

    string doc = 0;
    CHK(toBinary(&doc, stExt(TestStr1), stArg(TestStr1, src), 0, NULL));

    // The header identifies the document and records how the body was encoded, so a reader
    // needs nothing told to it in advance.
    CHK(strLen(doc) > 6);
    CHK(strBeginsWith(doc, _S "CXSB"));
    CHK(strGetChar(doc, 4) == 1);                           // version
    CHK((strGetChar(doc, BIN_HDR_FLAGS) & 0x03) == 0x03);   // self-describing, string dedup

    CHK(fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    // Compact drops the tag from every value whose type the schema already fixes, and says so
    // in the header. The reader takes the mode from there rather than from its own flags, so
    // the same read call handles both.
    string compact = 0;
    CHK(toBinary(&compact,
                 stExt(TestStr1),
                 stArg(TestStr1, src),
                 SER_Bin_Compact,
                 NULL));
    CHK((strGetChar(compact, BIN_HDR_FLAGS) & 0x01) == 0);
    CHK(strLen(compact) < strLen(doc));

    structDestroyMembers(&dst);
    structInit(TestStr1, &dst);
    CHK(fromBinary(compact,
                   stExt(TestStr1),
                   stArgPtr(TestStr1, &dst),
                   0,
                   NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    // Interning is the other half of the size story: with dedup off, repeated string values pay
    // for themselves every time.
    string nodedup = 0;
    CHK(toBinary(&nodedup,
                 stExt(TestStr1),
                 stArg(TestStr1, src),
                 SER_Bin_NoStringDedup,
                 NULL));
    CHK((strGetChar(nodedup, BIN_HDR_FLAGS) & 0x02) == 0);

    structDestroyMembers(&dst);
    structInit(TestStr1, &dst);
    CHK(fromBinary(nodedup,
                   stExt(TestStr1),
                   stArgPtr(TestStr1, &dst),
                   0,
                   NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    strDestroy(&doc);
    strDestroy(&compact);
    strDestroy(&nodedup);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// Same fixtures the JSON tests use, because the point of the traverser is that a backend does
// not get its own idea of what a struct is.
static int test_ser_binnested(void)
{
    TestStrFixed src, dst;
    structInit(TestStrFixed, &src);
    structInit(TestStrFixed, &dst);

    for (int i = 0; i < 4; i++) {
        string tmp = 0;
        strFromInt32(&tmp, i, 10);
        strPrepend(_S "name-", &tmp);
        strDup(&src.names[i], tmp);
        strDestroy(&tmp);
    }
    for (int i = 0; i < 3; i++) fillTestStr1(&src.substs[i], 30 + i);
    for (int i = 0; i < 8; i++) src.nums[i] = i * i;

    string doc = 0;
    for (int pass = 0; pass < 2; pass++) {
        flags_t f = pass ? SER_Bin_Compact : 0;
        strClear(&doc);
        CHK(toBinary(&doc,
                     stExt(TestStrFixed),
                     stArg(TestStrFixed, src),
                     f,
                     NULL));

        structDestroyMembers(&dst);
        structInit(TestStrFixed, &dst);
        CHK(fromBinary(doc,
                       stExt(TestStrFixed),
                       stArgPtr(TestStrFixed, &dst),
                       0,
                       NULL));
        CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));
    }

    structDestroyMembers(&src);
    structDestroyMembers(&dst);

    // The deep fixture, whose worst member is hashtable[string, sarray[sarray[struct]]].
    TestStr2 d2src, d2dst;
    structInit(TestStr2, &d2src);
    structInit(TestStr2, &d2dst);
    CHK(fillTestStr2(&d2src) == 0);
    CHK(toBinary(&doc, stExt(TestStr2), stArg(TestStr2, d2src), 0, NULL));
    CHK(fromBinary(doc, stExt(TestStr2), stArgPtr(TestStr2, &d2dst), 0, NULL));
    CHK(structDeepEq(STRUCTBASE(&d2src), STRUCTBASE(&d2dst)));

    structDestroyMembers(&d2src);
    structDestroyMembers(&d2dst);

    // Non-string keys take the pair-array projection here too. The binary reader could decode a
    // typed key, but SerReaderOps has no op to hand one back, so the capability stays off and
    // both directions use the projection every format can represent.
    hashtable hsrc = NULL, hdst = NULL;
    htInit(&hsrc, int32, string, 4);
    htInsert(&hsrc, int32, 10, string, _S "ten");
    htInsert(&hsrc, int32, -20, string, _S "minus twenty");

    const STypeInfoExt htschema = {
        .type  = stType(hashtable),
        .param = { stExt(int32), stExt(string) }
    };
    stype ht = stType(hashtable);
    CHK(toBinary(&doc, &htschema, stArg(hashtable, hsrc), 0, NULL));
    CHK(fromBinary(doc, &htschema, stArgPtr(hashtable, &hdst), 0, NULL));
    CHK(structDeepEqValue(ht, &hsrc, &hdst));

    htDestroy(&hsrc);
    htDestroy(&hdst);

    // A byte run is a native node here rather than base64, which is what SER_Cap_Bytes claims.
    static const uint8 bytes[] = { 0x00, 0xff, 0x10, 0x42, 0xc3, 0x7a, 0x01 };
    strClear(&doc);
    StreamBuffer* sb = sbufCreate(256);
    CHK(sbufStrCRegisterPush(sb, &doc));

    SerWriter* w = serBinaryWriterCreate(sb, 0);
    CHK(serWriterCan(w, Bytes));
    CHK(serWriteBytes(w, bytes, sizeof(bytes)));
    CHK(serWriterFinish(w));
    serWriterDestroy(&w);
    sbufRelease(&sb);
    CHK(strLen(doc) == 6 + 1 + 1 + sizeof(bytes));   // header, tag, length, payload

    sb = sbufCreate(256);
    CHK(sbufStrPRegisterPull(sb, doc));
    SerReader* r = serBinaryReaderCreate(sb, 0);
    Buffer back  = NULL;
    CHK(serReadBytes(r, &back));
    serReaderDestroy(&r);
    sbufRelease(&sb);

    CHK(back && back->len == sizeof(bytes));
    CHK(memcmp(back->data, bytes, sizeof(bytes)) == 0);
    bufDestroy(&back);

    strDestroy(&doc);
    return 0;
}

// Where JSON needs a projection table, binary needs none: every scalar comes back bit for bit,
// which is what SER_Cap_ExactInt and SER_Cap_Bytes are claiming.
static int test_ser_binnum(void)
{
    string doc = 0;

#define RT_BIN(type, value, flags)                                                                  \
    do {                                                                                            \
        stStorageType(type) src = (value), dst = 0;                                                 \
        strClear(&doc);                                                                             \
        CHK(toBinary(&doc, stExt(type), stArg(type, src), flags, NULL));           \
        CHK(fromBinary(doc, stExt(type), stArgPtr(type, &dst), 0, NULL)); \
        CHK(dst == src);                                                                            \
    } while (0)

#define RT_BIN_BOTH(type, value) \
    RT_BIN(type, value, 0);      \
    RT_BIN(type, value, SER_Bin_Compact)

    RT_BIN_BOTH(bool, true);
    RT_BIN_BOTH(bool, false);
    RT_BIN_BOTH(int8, -0x7f);
    RT_BIN_BOTH(int16, -0x7ffe);
    RT_BIN_BOTH(int32, -1234567);
    RT_BIN_BOTH(int32, 1234567);
    RT_BIN_BOTH(int64, INT64_MIN + 1);
    RT_BIN_BOTH(int64, INT64_MAX);
    RT_BIN_BOTH(uint8, 0xfe);
    RT_BIN_BOTH(uint16, 0xfffe);
    RT_BIN_BOTH(uint32, 0xfffffffeu);
    RT_BIN_BOTH(uint64, UINT64_MAX);

    // The full 64-bit range, with none of JSON's decimal-string detour
    RT_BIN_BOTH(uint64, 9007199254740993ull);
    CHK(strLen(doc) < 6 + 10);

    // A float32 goes out as four bytes, not as a widened double -- and in compact mode the
    // declared width is the only thing that says so, which is why the read ops take it.
    RT_BIN_BOTH(float32, 0.1f);
    RT_BIN(float32, 0.1f, SER_Bin_Compact);
    CHK(strLen(doc) == 6 + 4);
    RT_BIN_BOTH(float64, 1.0 / 3.0);
    RT_BIN(float64, 1.0 / 3.0, SER_Bin_Compact);
    CHK(strLen(doc) == 6 + 8);

    // NaN and the infinities have exact bit patterns here, so they survive as themselves
    // rather than collapsing to null the way JSON makes them.
    float64 nan64 = NAN, backd = 0;
    strClear(&doc);
    CHK(toBinary(&doc, stExt(float64), stArg(float64, nan64), 0, NULL));
    CHK(fromBinary(doc, stExt(float64), stArgPtr(float64, &backd), 0, NULL));
    CHK(isnan(backd));

    float32 inf32 = (float32)INFINITY, backf = 0;
    strClear(&doc);
    CHK(toBinary(&doc, stExt(float32), stArg(float32, inf32), 0, NULL));
    CHK(fromBinary(doc, stExt(float32), stArgPtr(float32, &backf), 0, NULL));
    CHK(isinf(backf) && backf > 0);

    // A non-negative signed value is written unsigned, because zigzag would spend a bit on a
    // sign it is not using. Self-describing only -- compact has no tag to say which it was.
    strClear(&doc);
    int32 small = 63;
    CHK(toBinary(&doc, stExt(int32), stArg(int32, small), 0, NULL));
    CHK(strLen(doc) == 6 + 2);   // tag + one varint byte, not two

#undef RT_BIN_BOTH
#undef RT_BIN

    strDestroy(&doc);
    return 0;
}

// Default omission and reset-on-read live in the traverser, so they have to behave identically
// here and in JSON. Asserting that is the point.
static int test_ser_bindefaults(void)
{
    TestStr1 empty, dst;
    structInit(TestStr1, &empty);
    structInit(TestStr1, &dst);

    // An all-default struct is an empty map: the tag, and a count of zero.
    string doc = 0;
    CHK(toBinary(&doc, stExt(TestStr1), stArg(TestStr1, empty), 0, NULL));
    CHK(strLen(doc) == 6 + 2);

    string full = 0;
    CHK(toBinary(&full,
                 stExt(TestStr1),
                 stArg(TestStr1, empty),
                 SER_EmitDefaults,
                 NULL));
    CHK(strLen(full) > strLen(doc));

    // Reading a document that omits a member resets it rather than leaving what was there.
    fillTestStr1(&dst, 42);
    CHK(fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, NULL));
    if (dst.intval != 0)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 0), stvar(int32, dst.intval));
    CHK(strEmpty(dst.strval));
    CHK(!dst.arrval.a);

    // A never-initialized array is a distinct state from an empty one; SER_EmitDefaults writes
    // it as null, and null is what has to come back.
    structDestroyMembers(&dst);
    structInit(TestStr1, &dst);
    fillTestStr1(&dst, 42);
    CHK(fromBinary(full, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, NULL));
    CHK(!dst.arrval.a);

    strDestroy(&doc);
    strDestroy(&full);
    structDestroyMembers(&empty);
    structDestroyMembers(&dst);
    return 0;
}

// Every way a document can be wrong, and the one way a schema can be out of date.
static int test_ser_binerrors(void)
{
    TestStr1 dst;
    structInit(TestStr1, &dst);
    SerError err = { 0 };
    string doc   = 0;

    // A document keyed by name with a member this schema has never heard of. A hashtable
    // produces exactly the map encoding a struct does, which is what makes it a stand-in for a
    // document written by a build that knows more members than this one.
    hashtable fut = NULL;
    htInit(&fut, string, int32, 4);
    htInsert(&fut, string, _S "intval", int32, 1);
    htInsert(&fut, string, _S "mystery", int32, 99);

    CHK(toBinary(&doc, stExt(hashtable), stArg(hashtable, fut), 0, NULL));
    CHK(fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, NULL));
    if (dst.intval != 1)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 1), stvar(int32, dst.intval));

    // ...unless the caller would rather be told
    CHK(!fromBinary(doc,
                    stExt(TestStr1),
                    stArgPtr(TestStr1, &dst),
                    SER_Strict,
                    &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    if (!strEq(err.path, _S "/mystery"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, err.path), stvar(strref, _S "/mystery"));
    serErrorDestroy(&err);

    // ...and in compact mode there is nothing to be told from: with no tag on the value there
    // is no way to know how far to step over it, so an unknown member is fatal rather than
    // recoverable. That is the trade the mode exists to make.
    strClear(&doc);
    CHK(toBinary(&doc, stExt(hashtable), stArg(hashtable, fut), SER_Bin_Compact, NULL));
    CHK(!fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, &err));
    if (err.code != SER_Err_Unsupported)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
    serErrorDestroy(&err);
    htDestroy(&fut);

    // A value of the wrong type is located at the member it was found in
    hashtable wrong = NULL;
    htInit(&wrong, string, string, 4);
    htInsert(&wrong, string, _S "intval", string, _S "seven");
    CHK(toBinary(&doc, stExt(hashtable), stArg(hashtable, wrong), 0, NULL));
    CHK(!fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    if (!strEq(err.path, _S "/intval"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, err.path), stvar(strref, _S "/intval"));
    serErrorDestroy(&err);
    htDestroy(&wrong);

    // Something that is not one of ours at all
    strClear(&doc);
    bytesToStr(&doc, (const uint8*)"NOPE\x01\x03", 6);
    CHK(!fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "not a cx binary document") >= 0);
    serErrorDestroy(&err);

    // A version this build does not know is refused rather than guessed at
    strClear(&doc);
    bytesToStr(&doc, (const uint8*)"CXSB\x63\x03", 6);
    CHK(!fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "version") >= 0);
    serErrorDestroy(&err);

    // A document cut short mid-value, which is the failure a stream actually has
    string ok = 0;
    TestStr1 src;
    structInit(TestStr1, &src);
    fillTestStr1(&src, 9);
    CHK(toBinary(&ok, stExt(TestStr1), stArg(TestStr1, src), 0, NULL));

    for (uint32 cut = 7; cut < strLen(ok); cut += 3) {
        strClear(&doc);
        strSubStr(&doc, ok, 0, cut);
        structDestroyMembers(&dst);
        structInit(TestStr1, &dst);
        CHK(!fromBinary(doc,
                        stExt(TestStr1),
                        stArgPtr(TestStr1, &dst),
                        0,
                        &err));
        if (err.code == SER_Err_None)
            TEST_FAIL(1, _SL("expected ${int} to differ from ${int}, but got the same value"), stvar(int32, SER_Err_None), stvar(int32, err.code));
        serErrorDestroy(&err);
    }

    // An integer that does not fit the slot it was declared for is an overflow, not a wrap
    hashtable big = NULL;
    htInit(&big, string, int64, 4);
    htInsert(&big, string, _S "intval", int64, 4294967296ll);
    strClear(&doc);
    CHK(toBinary(&doc, stExt(hashtable), stArg(hashtable, big), 0, NULL));
    structDestroyMembers(&dst);
    structInit(TestStr1, &dst);
    CHK(!fromBinary(doc, stExt(TestStr1), stArgPtr(TestStr1, &dst), 0, &err));
    if (err.code != SER_Err_Overflow)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Overflow), stvar(int32, err.code));
    serErrorDestroy(&err);
    htDestroy(&big);

    strDestroy(&doc);
    strDestroy(&ok);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// Three backends, one traverser. If a value survives all three round trips and the results agree
// with each other, the decomposition is the same one every time -- which is the entire claim the
// architecture makes.
static int test_ser_crossformat(void)
{
    TestStr2 src;
    structInit(TestStr2, &src);
    CHK(fillTestStr2(&src) == 0);

    TestStr2 viassd, viajson, viabin;
    structInit(TestStr2, &viassd);
    structInit(TestStr2, &viajson);
    structInit(TestStr2, &viabin);

    SSDNode* tree = toSsd(stExt(TestStr2), stArg(TestStr2, src), NULL);
    CHK(tree);
    CHK(fromSsd(tree, stExt(TestStr2), stArgPtr(TestStr2, &viassd)));
    objRelease(&tree);

    string js = 0, bin = 0;
    CHK(toJson(&js, stExt(TestStr2), stArg(TestStr2, src), 0, NULL));
    CHK(fromJson(js, stExt(TestStr2), stArgPtr(TestStr2, &viajson), 0, NULL));

    CHK(toBinary(&bin, stExt(TestStr2), stArg(TestStr2, src), 0, NULL));
    CHK(fromBinary(bin,
                   stExt(TestStr2),
                   stArgPtr(TestStr2, &viabin),
                   0,
                   NULL));

    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&viassd)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&viajson)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&viabin)));
    CHK(structDeepEq(STRUCTBASE(&viajson), STRUCTBASE(&viabin)));

    strDestroy(&js);
    strDestroy(&bin);
    structDestroyMembers(&src);
    structDestroyMembers(&viassd);
    structDestroyMembers(&viajson);
    structDestroyMembers(&viabin);
    return 0;
}

// A structp member is written as the pointee inline, and read back into a fresh allocation.
// TestStrDup is the fixture that carries one alongside a container or two, so the pointer is
// exercised in the middle of a struct rather than on its own.
static int fillTestStrDup(TestStrDup* src)
{
    htInit(&src->tags, string, int32, 4);
    htInsert(&src->tags, string, _S "one", int32, 1);

    saInit(&src->arrval, int32, 4);
    for (int32 i = 0; i < 4; i++) saPush(&src->arrval, int32, i * 11);

    src->knownptr = (TestStr1*)_structAlloc(&TestStr1_structinfo);
    fillTestStr1(src->knownptr, 42);
    return 0;
}

static int test_ser_structp(void)
{
    TestStrDup src;
    structInit(TestStrDup, &src);
    CHK(fillTestStrDup(&src) == 0);

    TestStrDup viassd, viajson, viabin;
    structInit(TestStrDup, &viassd);
    structInit(TestStrDup, &viajson);
    structInit(TestStrDup, &viabin);

    SSDNode* tree = toSsd(stExt(TestStrDup), stArg(TestStrDup, src), NULL);
    CHK(tree);
    CHK(fromSsd(tree, stExt(TestStrDup), stArgPtr(TestStrDup, &viassd)));
    objRelease(&tree);

    string js = 0, bin = 0;
    CHK(toJson(&js, stExt(TestStrDup), stArg(TestStrDup, src), 0, NULL));
    CHK(fromJson(js,
                 stExt(TestStrDup),
                 stArgPtr(TestStrDup, &viajson),
                 0,
                 NULL));
    CHK(toBinary(&bin, stExt(TestStrDup), stArg(TestStrDup, src), 0, NULL));
    CHK(fromBinary(bin,
                   stExt(TestStrDup),
                   stArgPtr(TestStrDup, &viabin),
                   0,
                   NULL));

    // The pointee has to come back as an independent allocation holding an equal value, which is
    // exactly what structDeepEq follows a structp member into.
    CHK(viassd.knownptr && viassd.knownptr != src.knownptr);
    CHK(viajson.knownptr && viajson.knownptr != src.knownptr);
    CHK(viabin.knownptr && viabin.knownptr != src.knownptr);

    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&viassd)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&viajson)));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&viabin)));

    // Reading over a live struct has to free the pointee that was there rather than leak it or
    // populate it in place; a leak build is what actually catches the first of those.
    CHK(fromBinary(bin,
                   stExt(TestStrDup),
                   stArgPtr(TestStrDup, &viabin),
                   0,
                   NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&viabin)));

    strDestroy(&js);
    strDestroy(&bin);
    structDestroyMembers(&src);
    structDestroyMembers(&viassd);
    structDestroyMembers(&viajson);
    structDestroyMembers(&viabin);
    return 0;
}

// A NULL structp is a distinct state from a pointer to a default-valued struct, and the null
// projection is what keeps them apart across a round trip.
static int test_ser_structpnull(void)
{
    TestStrDup src, dst;
    structInit(TestStrDup, &src);
    structInit(TestStrDup, &dst);

    // dst starts out holding one, so this also covers replacing a live pointer with nothing.
    dst.knownptr = (TestStr1*)_structAlloc(&TestStr1_structinfo);
    fillTestStr1(dst.knownptr, 9);

    string bin = 0;
    CHK(toBinary(&bin,
                 stExt(TestStrDup),
                 stArg(TestStrDup, src),
                 SER_EmitDefaults,
                 NULL));
    CHK(fromBinary(bin,
                   stExt(TestStrDup),
                   stArgPtr(TestStrDup, &dst),
                   0,
                   NULL));
    CHK(dst.knownptr == NULL);

    strDestroy(&bin);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// A structp declared over a structset has no single target type, so the value names itself with
// a type tag and the set turns that name back into a struct on the way in. Nothing is registered
// on the reader: declaring `structp[TestStrSet]` is the whole of what it takes, which is the
// point of parameterizing the slot rather than resolving it by hand.
static int test_ser_structpdynamic(void)
{
    const StructMemberDesc* mdyn = findMember(&TestStrP_structinfo, _S "dynptr");
    CHK(mdyn);

    TestStr1* one = (TestStr1*)_structAlloc(&TestStr1_structinfo);
    fillTestStr1(one, 3);

    string js = 0, bin = 0;
    CHK(toJson(&js, mdyn->schema, stgeneric(structp, STRUCTBASE(one)), 0, NULL));
    CHK(toBinary(&bin, mdyn->schema, stgeneric(structp, STRUCTBASE(one)), 0, NULL));

    // The tag is the struct's own name, wrapping the value the way every JSON tag does.
    CHK(strFind(js, 0, _S "\"$type\": \"TestStr1\"") >= 0);

    StructBase* viajson = NULL;
    StructBase* viabin  = NULL;
    CHK(fromJson(js, mdyn->schema, stStoredPtr(mdyn->schema->type, &viajson), 0, NULL));
    CHK(fromBinary(bin, mdyn->schema, stStoredPtr(mdyn->schema->type, &viabin), 0, NULL));

    CHK(viajson && viajson->structinfo == &TestStr1_structinfo);
    CHK(viabin && viabin->structinfo == &TestStr1_structinfo);
    CHK(structDeepEq(STRUCTBASE(one), viajson));
    CHK(structDeepEq(STRUCTBASE(one), viabin));

    // The other struct in the same set, into the same slot: which one is behind the pointer is
    // a property of the value, so the second document says something different from the first.
    TestStr2 two;
    structInit(TestStr2, &two);
    CHK(fillTestStr2(&two) == 0);

    strClear(&js);
    CHK(toJson(&js, mdyn->schema, stgeneric(structp, STRUCTBASE(&two)), 0, NULL));
    CHK(strFind(js, 0, _S "\"$type\": \"TestStr2\"") >= 0);

    // Reading over a live pointee frees it and allocates the type the document names, rather
    // than populating a TestStr1 with a TestStr2's members.
    CHK(fromJson(js, mdyn->schema, stStoredPtr(mdyn->schema->type, &viajson), 0, NULL));
    CHK(viajson && viajson->structinfo == &TestStr2_structinfo);
    CHK(structDeepEq(STRUCTBASE(&two), viajson));

    _structDestroy(&viajson);
    _structDestroy(&viabin);
    _structDestroy((StructBase**)&one);
    structDestroyMembers(&two);
    strDestroy(&js);
    strDestroy(&bin);
    return 0;
}

// The set is the slot's declared vocabulary, in both directions. A struct outside it cannot be
// written -- the same schema could not read the document back -- and a document naming one
// cannot be read, however well the name resolves elsewhere.
static int test_ser_structpsetbounds(void)
{
    const StructMemberDesc* mdyn = findMember(&TestStrP_structinfo, _S "dynptr");
    CHK(mdyn);

    TestStrFixed* outside = (TestStrFixed*)_structAlloc(&TestStrFixed_structinfo);

    SerError err = { 0 };
    string bin   = 0;
    CHK(!toBinary(&bin,
                  mdyn->schema,
                  stgeneric(structp, STRUCTBASE(outside)),
                  0,
                  &err));
    if (err.code != SER_Err_Type)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "TestStrFixed") >= 0);
    serErrorDestroy(&err);

    // The very same struct goes into the neighbouring slot without complaint, because that one
    // is declared over a set that does contain it. Both slots' runtime descriptors are the same
    // interned pointer -- a set is not part of a descriptor's registry key -- so this only comes
    // out right because each slot takes its set from its own schema.
    const StructMemberDesc* mdyn2 = findMember(&TestStrP_structinfo, _S "dynptr2");
    CHK(mdyn2 && mdyn2->schema && mdyn2->schema->detail == &TestStrSet2_structset);
    CHK(stEq(mdyn->schema->type, mdyn2->schema->type));

    CHK(toBinary(&bin,
                 mdyn2->schema,
                 stgeneric(structp, STRUCTBASE(outside)),
                 0,
                 NULL));
    StructBase* crossed = NULL;
    CHK(fromBinary(bin, mdyn2->schema, stStoredPtr(mdyn2->schema->type, &crossed), 0, NULL));
    CHK(crossed && crossed->structinfo == &TestStrFixed_structinfo);
    _structDestroy(&crossed);

    // ...and the document that slot produced does not read back through the first one.
    CHK(!fromBinary(bin, mdyn->schema, stStoredPtr(mdyn->schema->type, &crossed), 0, &err));
    if (err.code != SER_Err_Type)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
    serErrorDestroy(&err);

    // A hand-built document naming a struct the set does not contain. TestStrFixed is a real
    // struct with a perfectly good name, so what refuses this is the set and only the set.
    StructBase* dst = NULL;
    CHK(!fromJson(_S "{ \"$type\": \"TestStrFixed\", \"$value\": { \"nums\": [] } }",
                  mdyn->schema,
                  stStoredPtr(mdyn->schema->type, &dst),
                  0,
                  &err));
    if (err.code != SER_Err_Type)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
    CHK(dst == NULL);
    serErrorDestroy(&err);

    // An untagged value cannot be read into a dynamic slot either: nothing would say what to
    // allocate.
    CHK(!fromJson(_S "{ \"intval\": 1 }",
                  mdyn->schema,
                  stStoredPtr(mdyn->schema->type, &dst),
                  0,
                  &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    serErrorDestroy(&err);

    // SSD cannot carry a tag at all, so a dynamic slot is simply not representable there.
    TestStr1* inside = (TestStr1*)_structAlloc(&TestStr1_structinfo);
    fillTestStr1(inside, 1);
    CHK(!toSsd(mdyn->schema, stgeneric(structp, STRUCTBASE(inside)), &err));
    if (err.code != SER_Err_Unsupported)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
    serErrorDestroy(&err);

    _structDestroy((StructBase**)&outside);
    _structDestroy((StructBase**)&inside);
    strDestroy(&bin);
    return 0;
}

// ---------------------------------------------------------------------------------------
// Classes
// ---------------------------------------------------------------------------------------

// There is no generated set for classes the way structset gives one for structs, so a reader
// that has to resolve a wire name gets handed the list directly.
static ObjClassInfo* serTestClasses[] = {
    &SerCls1_clsinfo, &SerCls2_clsinfo,  &SerCls3_clsinfo,   &SerHolder_clsinfo,
    &SerAny_clsinfo,  &SerCycle_clsinfo, &SerCustom_clsinfo, NULL
};

static bool fromJsonCls(strref json, const STypeInfoExt* schema, stgeneric* val, SerError* err)
{
    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrPRegisterPull(sb, json)) {
        sbufRelease(&sb);
        return false;
    }

    SerReader* r = serJsonReaderCreate(sb, 0);
    serReaderAddResolver(r, serObjClassResolver, serTestClasses);
    bool ok = _serRead(r, schema, val);
    if (!ok && err) {
        *err   = r->err;
        r->err = (SerError) { 0 };
    }

    serReaderDestroy(&r);
    sbufRelease(&sb);
    return ok;
}

static bool fromBinaryCls(strref doc, const STypeInfoExt* schema, stgeneric* val,
                          SerError* err)
{
    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrPRegisterPull(sb, doc)) {
        sbufRelease(&sb);
        return false;
    }

    SerReader* r = serBinaryReaderCreate(sb, 0);
    serReaderAddResolver(r, serObjClassResolver, serTestClasses);
    bool ok = _serRead(r, schema, val);
    if (!ok && err) {
        *err   = r->err;
        r->err = (SerError) { 0 };
    }

    serReaderDestroy(&r);
    sbufRelease(&sb);
    return ok;
}

// A two-level hierarchy, both levels annotated. Base members go out first, the derived level
// adds its own, and a [noserialize] member is absent in both directions.
static int test_ser_class(void)
{
    SerCls2* src = sercls2Create();
    strDup(&src->title, _S "Original");
    src->revision = 7;
    strDup(&src->scratch, _S "not written");
    saPush(&src->nums, int32, 11);
    saPush(&src->nums, int32, 22);
    fillTestStr1(&src->sub, 3);

    string json = 0, bin = 0;
    CHK(toJson(&json, stExt(SerCls2), stArg(SerCls2, src), 0, NULL));
    CHK(toBinary(&bin, stExt(SerCls2), stArg(SerCls2, src), 0, NULL));

    // The schema names this exact class, so nothing had to be tagged; and scratch never
    // appears, because it opted out.
    CHK(strFind(json, 0, _S "$type") == -1);
    CHK(strFind(json, 0, _S "scratch") == -1);
    CHK(strFind(json, 0, _S "\"title\": \"Original\"") >= 0);

    SerCls2* viajson = NULL;
    SerCls2* viabin  = NULL;
    CHK(fromJsonCls(json, stExt(SerCls2), stArgPtr(SerCls2, &viajson), NULL));
    CHK(fromBinaryCls(bin, stExt(SerCls2), stArgPtr(SerCls2, &viabin), NULL));

    SerCls2* results[] = { viajson, viabin };
    for (int i = 0; i < 2; i++) {
        SerCls2* r = results[i];
        CHK(r);
        CHK(objClsInfo(r) == &SerCls2_clsinfo);
        if (!strEq(r->title, _S "Original"))
            TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, r->title), stvar(strref, _S "Original"));
        if (r->revision != 7)
            TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 7), stvar(int32, r->revision));
        CHK(strEmpty(r->scratch));
        CHK(saSize(r->nums) == 2 && r->nums.a[0] == 11 && r->nums.a[1] == 22);
        CHK(structDeepEq(STRUCTBASE(&src->sub), STRUCTBASE(&r->sub)));
    }

    // Reading over a live object replaces it rather than merging into it.
    CHK(fromBinaryCls(bin, stExt(SerCls2), stArgPtr(SerCls2, &viabin), NULL));
    CHK(viabin && strEq(viabin->title, _S "Original"));

    // Compact mode drops the tags the schema makes redundant, and an object's are not among
    // them: a map, a null and a type tag are all positions the reader has to peek at.
    string compact      = 0;
    SerCls2* viacompact = NULL;
    CHK(toBinary(&compact,
                 stExt(SerCls2),
                 stArg(SerCls2, src),
                 SER_Bin_Compact,
                 NULL));
    CHK(strLen(compact) < strLen(bin));
    CHK(fromBinaryCls(compact,
                      stExt(SerCls2),
                      stArgPtr(SerCls2, &viacompact),
                      NULL));
    CHK(viacompact && strEq(viacompact->title, _S "Original") && viacompact->revision == 7);
    CHK(saSize(viacompact->nums) == 2 && viacompact->nums.a[1] == 22);
    strDestroy(&compact);
    objRelease(&viacompact);

    strDestroy(&json);
    strDestroy(&bin);
    objRelease(&viajson);
    objRelease(&viabin);
    objRelease(&src);
    return 0;
}

// An unannotated level between two annotated ones contributes nothing but does not break the
// chain: SerCls3's document carries its base's members and its own, and never `hidden`.
static int test_ser_classgap(void)
{
    SerCls3* src = sercls3Create();
    strDup(&src->title, _S "Deep");
    src->revision = 2;
    src->hidden   = 99;
    strDup(&src->leaf, _S "bottom");

    string json = 0;
    CHK(toJson(&json, stExt(SerCls3), stArg(SerCls3, src), 0, NULL));
    CHK(strFind(json, 0, _S "hidden") == -1);
    CHK(strFind(json, 0, _S "title") >= 0);
    CHK(strFind(json, 0, _S "leaf") >= 0);

    SerCls3* back = NULL;
    CHK(fromJsonCls(json, stExt(SerCls3), stArgPtr(SerCls3, &back), NULL));
    CHK(back && strEq(back->title, _S "Deep") && back->revision == 2);
    if (!strEq(back->leaf, _S "bottom"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, back->leaf), stvar(strref, _S "bottom"));
    if (back->hidden != 0)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 0), stvar(int32, back->hidden));

    // The unannotated class itself has no wire name, so an instance of it cannot be written at
    // all -- there would be nothing for a reader to resolve back into a class.
    SerPlain* plain = serplainCreate();
    SerError err    = { 0 };
    string bad      = 0;
    CHK(!toJson(&bad, stExt(object), stArg(SerPlain, plain), 0, &err));
    if (err.code != SER_Err_Unsupported)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "[serialize]") >= 0);

    serErrorDestroy(&err);
    strDestroy(&json);
    strDestroy(&bad);
    objRelease(&back);
    objRelease(&plain);
    objRelease(&src);
    return 0;
}

// Polymorphism, which is the whole reason objects need type tags: a derived instance in a
// base-class slot, an instance in a fully dynamic slot, and both again inside a container.
static int test_ser_classpoly(void)
{
    SerHolder* src = serholderCreate();

    SerCls2* child = sercls2Create();
    strDup(&child->title, _S "derived");
    saPush(&child->nums, int32, 5);
    src->child = (SerCls1*)objAcquire(child);

    SerCustom* any = sercustomCreate();
    any->magic     = 0x1234;
    strDup(&any->label, _S "hand-rolled");
    src->anyobj = ObjInst(objAcquire(any));

    SerCls1* plainkid = sercls1Create();
    strDup(&plainkid->title, _S "exact");
    saPush(&src->kids, object, plainkid);
    saPush(&src->kids, object, child);
    objRelease(&plainkid);

    string json = 0, bin = 0;
    CHK(toJson(&json, stExt(SerHolder), stArg(SerHolder, src), 0, NULL));
    CHK(toBinary(&bin, stExt(SerHolder), stArg(SerHolder, src), 0, NULL));

    // The derived child and the dynamic slot are tagged; the exactly-typed element of kids is
    // not, so a tag costs nothing where the schema already answers the question.
    CHK(strFind(json, 0, _S "\"$type\": \"SerCls2\"") >= 0);
    CHK(strFind(json, 0, _S "\"$type\": \"SerCustom\"") >= 0);

    SerHolder* viajson = NULL;
    SerHolder* viabin  = NULL;
    CHK(fromJsonCls(json,
                    stExt(SerHolder),
                    stArgPtr(SerHolder, &viajson),
                    NULL));
    CHK(fromBinaryCls(bin,
                      stExt(SerHolder),
                      stArgPtr(SerHolder, &viabin),
                      NULL));

    SerHolder* results[] = { viajson, viabin };
    for (int i = 0; i < 2; i++) {
        SerHolder* h = results[i];
        CHK(h);
        CHK(h->child && objClsInfo(h->child) == &SerCls2_clsinfo);
        if (!strEq(h->child->title, _S "derived"))
            TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, h->child->title), stvar(strref, _S "derived"));
        CHK(saSize(((SerCls2*)h->child)->nums) == 1);

        CHK(h->anyobj && objClsInfo(h->anyobj) == &SerCustom_clsinfo);
        CHK(((SerCustom*)h->anyobj)->magic == 0x1234);
        if (!strEq(((SerCustom*)h->anyobj)->label, _S "hand-rolled"))
            TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, ((SerCustom*)h->anyobj)->label), stvar(strref, _S "hand-rolled"));

        CHK(saSize(h->kids) == 2);
        CHK(objClsInfo(h->kids.a[0]) == &SerCls1_clsinfo);
        if (!strEq(((SerCls1*)h->kids.a[0])->title, _S "exact"))
            TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, ((SerCls1*)h->kids.a[0])->title), stvar(strref, _S "exact"));
        CHK(objClsInfo(h->kids.a[1]) == &SerCls2_clsinfo);
    }

    strDestroy(&json);
    strDestroy(&bin);
    objRelease(&viajson);
    objRelease(&viabin);
    objRelease(&any);
    objRelease(&child);
    objRelease(&src);
    return 0;
}

static int test_ser_classcustom(void)
{
    SerCustom* src = sercustomCreate();
    src->magic     = -42;
    strDup(&src->label, _S "mine");

    string json = 0, bin = 0;
    CHK(toJson(&json, stExt(SerCustom), stArg(SerCustom, src), 0, NULL));
    CHK(toBinary(&bin, stExt(SerCustom), stArg(SerCustom, src), 0, NULL));
    if (!strEq(json, _S "[ -42, \"mine\" ]"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, json), stvar(strref, _S "[ -42, \"mine\" ]"));

    SerCustom* viajson = NULL;
    SerCustom* viabin  = NULL;
    CHK(fromJsonCls(json,
                    stExt(SerCustom),
                    stArgPtr(SerCustom, &viajson),
                    NULL));
    CHK(fromBinaryCls(bin,
                      stExt(SerCustom),
                      stArgPtr(SerCustom, &viabin),
                      NULL));

    CHK(viajson && viajson->magic == -42 && strEq(viajson->label, _S "mine"));
    CHK(viabin && viabin->magic == -42 && strEq(viabin->label, _S "mine"));

    strDestroy(&json);
    strDestroy(&bin);
    objRelease(&viajson);
    objRelease(&viabin);
    objRelease(&src);
    return 0;
}

// The class counterpart of test_ser_structpdynamic: a slot declared over a classset holds any
// class in the set, and the set resolves the wire name on the way back in. These readers are the
// plain ones with no resolver registered at all -- the declaration is what makes the document
// readable, which is the whole reason to declare a set.
static int test_ser_classset(void)
{
    SerSetHolder* src = sersetholderCreate();

    SerCls1* one = sercls1Create();
    strDup(&one->title, _S "in the set");
    one->revision = 4;
    src->item     = ObjInst(objAcquire(one));

    SerCustom* two = sercustomCreate();
    two->magic     = 7;
    strDup(&two->label, _S "also in the set");

    saPush(&src->items, object, one);
    saPush(&src->items, object, two);

    string json = 0, bin = 0;
    CHK(toJson(&json,
               stExt(SerSetHolder),
               stArg(SerSetHolder, src),
               0,
               NULL));
    CHK(toBinary(&bin,
                 stExt(SerSetHolder),
                 stArg(SerSetHolder, src),
                 0,
                 NULL));

    // Every slot over a set is tagged: the set says which names are allowed, never which one
    // this value is.
    CHK(strFind(json, 0, _S "\"$type\": \"SerCls1\"") >= 0);
    CHK(strFind(json, 0, _S "\"$type\": \"SerCustom\"") >= 0);

    SerSetHolder* viajson = NULL;
    SerSetHolder* viabin  = NULL;
    CHK(fromJson(json,
                 stExt(SerSetHolder),
                 stArgPtr(SerSetHolder, &viajson),
                 0,
                 NULL));
    CHK(fromBinary(bin,
                   stExt(SerSetHolder),
                   stArgPtr(SerSetHolder, &viabin),
                   0,
                   NULL));

    SerSetHolder* results[] = { viajson, viabin };
    for (int i = 0; i < 2; i++) {
        SerSetHolder* h = results[i];
        CHK(h);
        CHK(h->item && objClsInfo(h->item) == &SerCls1_clsinfo);
        if (!strEq(((SerCls1*)h->item)->title, _S "in the set"))
            TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, ((SerCls1*)h->item)->title), stvar(strref, _S "in the set"));
        CHK(((SerCls1*)h->item)->revision == 4);

        CHK(saSize(h->items) == 2);
        CHK(objClsInfo(h->items.a[0]) == &SerCls1_clsinfo);
        CHK(objClsInfo(h->items.a[1]) == &SerCustom_clsinfo);
        CHK(((SerCustom*)h->items.a[1])->magic == 7);
    }

    strDestroy(&json);
    strDestroy(&bin);
    objRelease(&viajson);
    objRelease(&viabin);
    objRelease(&one);
    objRelease(&two);
    objRelease(&src);
    return 0;
}

// The set bounds the slot in both directions, and by exact class. SerCls3 is [serialize] and
// perfectly writable elsewhere; SerCls2 even derives from a class that is in the set. Neither
// is in it, so neither goes in this slot.
static int test_ser_classsetbounds(void)
{
    SerSetHolder* src = sersetholderCreate();
    SerCls3* outside  = sercls3Create();
    strDup(&outside->leaf, _S "elsewhere");
    src->item = ObjInst(objAcquire(outside));

    SerError err = { 0 };
    string bin   = 0;
    CHK(!toBinary(&bin,
                  stExt(SerSetHolder),
                  stArg(SerSetHolder, src),
                  0,
                  &err));
    if (err.code != SER_Err_Type)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "SerCls3") >= 0);
    serErrorDestroy(&err);

    // A derived instance in a set that names only its base is the same refusal: the set is a
    // list of concrete types, and reading the name back would construct the base.
    SerCls2* derived = sercls2Create();
    objRelease(&src->item);
    src->item = ObjInst(objAcquire(derived));
    CHK(!toBinary(&bin,
                  stExt(SerSetHolder),
                  stArg(SerSetHolder, src),
                  0,
                  &err));
    if (err.code != SER_Err_Type)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
    serErrorDestroy(&err);

    // And on the way in, whatever the reader's own resolvers would have said: this one knows
    // SerCls3 and the slot still refuses it.
    SerSetHolder* dst = NULL;
    CHK(!fromJsonCls(_S "{ \"item\": { \"$type\": \"SerCls3\", \"$value\": { \"leaf\": \"x\" } } }",
                     stExt(SerSetHolder),
                     stArgPtr(SerSetHolder, &dst),
                     &err));
    if (err.code != SER_Err_Type)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
    serErrorDestroy(&err);

    strDestroy(&bin);
    objRelease(&dst);
    objRelease(&derived);
    objRelease(&outside);
    objRelease(&src);
    return 0;
}

// Without references a cycle has no representation, so the traverser has to notice one rather
// than recurse into it until the stack runs out.
static int test_ser_classcycle(void)
{
    SerCycle* a = sercycleCreate();
    SerCycle* b = sercycleCreate();
    strDup(&a->tag, _S "a");
    strDup(&b->tag, _S "b");
    a->next = objAcquire(b);
    b->next = objAcquire(a);

    SerError err = { 0 };
    string json  = 0;
    CHK(!toJson(&json, stExt(SerCycle), stArg(SerCycle, a), 0, &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "cycle") >= 0);
    if (!strEq(err.path, _S "/next/next"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, err.path), stvar(strref, _S "/next/next"));

    serErrorDestroy(&err);

    // The same graph without the back edge is a plain chain and writes fine, which is what
    // makes the guard about cycles rather than about repeated visits.
    objRelease(&b->next);
    CHK(toJson(&json, stExt(SerCycle), stArg(SerCycle, a), 0, NULL));

    SerCycle* back = NULL;
    CHK(fromJsonCls(json, stExt(SerCycle), stArgPtr(SerCycle, &back), NULL));
    CHK(back && strEq(back->tag, _S "a"));
    CHK(back->next && strEq(back->next->tag, _S "b"));
    CHK(!back->next->next);

    strDestroy(&json);
    objRelease(&back);
    objRelease(&a);
    objRelease(&b);
    return 0;
}

// SSD has no way to carry a type tag, so it round-trips an exactly-typed object and refuses a
// polymorphic one rather than writing a document that reads back as the wrong class.
static int test_ser_classssd(void)
{
    SerCls1* src = sercls1Create();
    strDup(&src->title, _S "tree");
    src->revision = 4;

    SSDNode* t = toSsd(stExt(SerCls1), stArg(SerCls1, src), NULL);
    CHK(t);

    SerCls1* back = NULL;
    CHK(fromSsd(t, stExt(SerCls1), stArgPtr(SerCls1, &back)));
    CHK(back && strEq(back->title, _S "tree") && back->revision == 4);

    SerCls2* derived = sercls2Create();
    SerError err     = { 0 };
    SSDNode* bad =
        toSsd(stExt(SerCls1), stArg(SerCls1, (SerCls1*)derived), &err);
    CHK(!bad);
    if (err.code != SER_Err_Unsupported)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "type tag") >= 0);

    serErrorDestroy(&err);
    objRelease(&t);
    objRelease(&back);
    objRelease(&derived);
    objRelease(&src);
    return 0;
}

// [serializeas] decouples the wire name from the C identifier. The document is the only place
// the override is visible: nothing in the struct's own code changes, and neither does anything
// in the traverser -- the generated name constant simply holds different text.
static int test_ser_rename(void)
{
    TestStrRename src, dst;
    structInit(TestStrRename, &src);
    structInit(TestStrRename, &dst);
    src.ident = 12;
    strDup(&src.label, _S "hello");
    src.plain = 5;

    string js = 0;
    CHK(toJson(&js,
               stExt(TestStrRename),
               stArg(TestStrRename, src),
               SER_JSON_Compact,
               NULL));
    if (!strEq(js, _S "{\"id\":12,\"display-name\":\"hello\",\"plain\":5}"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, js), stvar(strref, _S "{\"id\":12,\"display-name\":\"hello\",\"plain\":5}"));

    CHK(fromJson(js,
                 stExt(TestStrRename),
                 stArgPtr(TestStrRename, &dst),
                 0,
                 NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    // The C name is not an alias for the wire name -- it is simply not in the vocabulary, and
    // a member the reader does not know is skipped like any other.
    structDestroyMembers(&dst);
    structInit(TestStrRename, &dst);
    CHK(fromJson(_S "{\"ident\":12,\"label\":\"hello\",\"plain\":5}",
                 stExt(TestStrRename),
                 stArgPtr(TestStrRename, &dst),
                 0,
                 NULL));
    CHK(dst.ident == 0 && strEmpty(dst.label) && dst.plain == 5);

    // Binary names members through its dictionary, so the rename has to reach that too.
    string bin = 0;
    structDestroyMembers(&dst);
    structInit(TestStrRename, &dst);
    CHK(toBinary(&bin,
                 stExt(TestStrRename),
                 stArg(TestStrRename, src),
                 0,
                 NULL));
    CHK(strFind(bin, 0, _S "display-name") >= 0);
    CHK(fromBinary(bin,
                   stExt(TestStrRename),
                   stArgPtr(TestStrRename, &dst),
                   0,
                   NULL));
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    strDestroy(&js);
    strDestroy(&bin);
    structDestroyMembers(&src);
    structDestroyMembers(&dst);

    // Class members take the annotation the same way, and only the level that declares one is
    // affected: `title` and `revision` come from an unrenamed base.
    SerRenamed* cls = serrenamedCreate();
    strDup(&cls->title, _S "top");
    cls->revision = 1;
    strDup(&cls->category, _S "widget");
    cls->skipped = 42;

    string cjs = 0;
    CHK(toJson(&cjs,
               stExt(SerRenamed),
               stArg(SerRenamed, cls),
               SER_JSON_Compact,
               NULL));
    if (!strEq(cjs, _S "{\"title\":\"top\",\"revision\":1,\"kind\":\"widget\"}"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, cjs), stvar(strref, _S "{\"title\":\"top\",\"revision\":1,\"kind\":\"widget\"}"));

    SerRenamed* back = NULL;
    CHK(fromJsonCls(cjs,
                    stExt(SerRenamed),
                    stArgPtr(SerRenamed, &back),
                    NULL));
    CHK(back && strEq(back->title, _S "top") && back->revision == 1);
    if (!strEq(back->category, _S "widget"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, back->category), stvar(strref, _S "widget"));
    if (back->skipped != 0)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, 0), stvar(int32, back->skipped));

    strDestroy(&cjs);
    objRelease(&back);
    objRelease(&cls);
    return 0;
}

// Shared substructure: one object reached by three paths goes out once and is named twice more.
// Pointer identity on the way back is the property that matters -- a document that wrote three
// copies would still read into three perfectly valid objects.
static int test_ser_refs(void)
{
    SerHolder* src = serholderCreate();

    SerCls1* shared = sercls1Create();
    strDup(&shared->title, _S "shared");
    shared->revision = 3;

    src->child = objAcquire(shared);
    saPush(&src->kids, object, shared);
    saPush(&src->kids, object, shared);

    string json = 0, bin = 0;
    CHK(toJson(&json,
               stExt(SerHolder),
               stArg(SerHolder, src),
               SER_Refs,
               NULL));
    CHK(toBinary(&bin,
                 stExt(SerHolder),
                 stArg(SerHolder, src),
                 SER_Refs,
                 NULL));

    // The holder is id 0 and the shared child is id 1; the two later occurrences are the id and
    // nothing else. Ids run sequentially from zero in the order the definitions were emitted.
    CHK(strFind(json, 0, _S "\"$id\": 0") >= 0);
    CHK(strFind(json, 0, _S "\"$id\": 1") >= 0);
    CHK(strFind(json, 0, _S "\"$ref\": 1") >= 0);
    CHK(strFind(json, 0, _S "\"$id\": 2") == -1);

    SerHolder* viajson = NULL;
    SerHolder* viabin  = NULL;
    CHK(fromJsonCls(json,
                    stExt(SerHolder),
                    stArgPtr(SerHolder, &viajson),
                    NULL));
    CHK(fromBinaryCls(bin,
                      stExt(SerHolder),
                      stArgPtr(SerHolder, &viabin),
                      NULL));

    SerHolder* results[] = { viajson, viabin };
    for (int i = 0; i < 2; i++) {
        SerHolder* h = results[i];
        CHK(h);
        CHK(h->child && strEq(h->child->title, _S "shared") && h->child->revision == 3);
        CHK(saSize(h->kids) == 2);
        CHK(h->kids.a[0] == h->child);
        CHK(h->kids.a[1] == h->child);
    }

    // The same graph without the flag is the behaviour references replace: three independent
    // copies, all equal and none identical.
    string plain      = 0;
    SerHolder* viadup = NULL;
    CHK(toJson(&plain, stExt(SerHolder), stArg(SerHolder, src), 0, NULL));
    CHK(strFind(plain, 0, _S "$ref") == -1);
    CHK(fromJsonCls(plain,
                    stExt(SerHolder),
                    stArgPtr(SerHolder, &viadup),
                    NULL));
    CHK(viadup && saSize(viadup->kids) == 2);
    CHK(viadup->kids.a[0] != viadup->child);
    if (!strEq(viadup->kids.a[0]->title, _S "shared"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, viadup->kids.a[0]->title), stvar(strref, _S "shared"));

    strDestroy(&json);
    strDestroy(&bin);
    strDestroy(&plain);
    objRelease(&viajson);
    objRelease(&viabin);
    objRelease(&viadup);
    objRelease(&shared);
    objRelease(&src);
    return 0;
}

// The cycle Stage 8 could only report. A back edge is a use that resolves to an ancestor still
// being built, which is exactly why the id is recorded before the members are read.
static int test_ser_refcycle(void)
{
    SerCycle* a = sercycleCreate();
    SerCycle* b = sercycleCreate();
    strDup(&a->tag, _S "a");
    strDup(&b->tag, _S "b");
    a->next = objAcquire(b);
    b->next = objAcquire(a);

    string json = 0, bin = 0;
    CHK(toJson(&json, stExt(SerCycle), stArg(SerCycle, a), SER_Refs, NULL));
    CHK(toBinary(&bin, stExt(SerCycle), stArg(SerCycle, a), SER_Refs, NULL));
    CHK(strFind(json, 0, _S "\"$ref\": 0") >= 0);

    SerCycle* viajson = NULL;
    SerCycle* viabin  = NULL;
    CHK(fromJsonCls(json,
                    stExt(SerCycle),
                    stArgPtr(SerCycle, &viajson),
                    NULL));
    CHK(fromBinaryCls(bin,
                      stExt(SerCycle),
                      stArgPtr(SerCycle, &viabin),
                      NULL));

    SerCycle* results[] = { viajson, viabin };
    for (int i = 0; i < 2; i++) {
        SerCycle* c = results[i];
        CHK(c && strEq(c->tag, _S "a"));
        CHK(c->next && strEq(c->next->tag, _S "b"));
        CHK(c->next->next == c);

        // A cycle of strong references keeps itself alive, so the test has to cut it before
        // dropping its own handle -- reading one back is not a leak, holding one is.
        objRelease(&c->next->next);
    }

    // The degenerate case: an object whose own member points at it. The definition has to be
    // recorded before the members go out, or the writer never sees the id it just assigned.
    SerCycle* self = sercycleCreate();
    strDup(&self->tag, _S "me");
    self->next = objAcquire(self);

    string selfjson   = 0;
    SerCycle* viaself = NULL;
    CHK(toJson(&selfjson,
               stExt(SerCycle),
               stArg(SerCycle, self),
               SER_Refs,
               NULL));
    CHK(fromJsonCls(selfjson,
                    stExt(SerCycle),
                    stArgPtr(SerCycle, &viaself),
                    NULL));
    CHK(viaself && viaself->next == viaself);
    objRelease(&viaself->next);

    objRelease(&self->next);
    objRelease(&self);
    strDestroy(&selfjson);
    strDestroy(&json);
    strDestroy(&bin);
    objRelease(&viaself);
    objRelease(&viajson);
    objRelease(&viabin);
    objRelease(&a->next);
    objRelease(&b->next);
    objRelease(&a);
    objRelease(&b);
    return 0;
}

// A Serializable class writes whatever it likes, and an array has nowhere to put a `"$id"` key.
// JSON falls back to the same `$value` wrapper a type tag uses, and both nest.
static int test_ser_refwrap(void)
{
    SerCustom* shared = sercustomCreate();
    shared->magic     = 7;
    strDup(&shared->label, _S "wrapped");

    // At the root the schema names the exact class, so there is no tag to nest inside -- just
    // the wrapper the id needed.
    string bare = 0;
    CHK(toJson(&bare,
               stExt(SerCustom),
               stArg(SerCustom, shared),
               SER_Refs,
               NULL));
    if (!strEq(bare, _S "{ \"$id\": 0, \"$value\": [ 7, \"wrapped\" ] }"))
        TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, bare), stvar(strref, _S "{ \"$id\": 0, \"$value\": [ 7, \"wrapped\" ] }"));

    SerCustom* viabare = NULL;
    CHK(fromJsonCls(bare,
                    stExt(SerCustom),
                    stArgPtr(SerCustom, &viabare),
                    NULL));
    CHK(viabare && viabare->magic == 7 && strEq(viabare->label, _S "wrapped"));

    // Two dynamic slots holding one instance: the first is tagged and defines the id, the second
    // is the id alone. A use carries no tag because it does not need one.
    SerAny* src = seranyCreate();
    src->one    = ObjInst(objAcquire(shared));
    src->two    = ObjInst(objAcquire(shared));

    string json = 0, bin = 0;
    CHK(toJson(&json, stExt(SerAny), stArg(SerAny, src), SER_Refs, NULL));
    CHK(toBinary(&bin, stExt(SerAny), stArg(SerAny, src), SER_Refs, NULL));
    CHK(strFind(json, 0, _S "\"$type\": \"SerCustom\"") >= 0);
    CHK(strFind(json, 0, _S "\"$ref\": 1") >= 0);

    SerAny* viajson = NULL;
    SerAny* viabin  = NULL;
    CHK(fromJsonCls(json, stExt(SerAny), stArgPtr(SerAny, &viajson), NULL));
    CHK(fromBinaryCls(bin, stExt(SerAny), stArgPtr(SerAny, &viabin), NULL));

    SerAny* results[] = { viajson, viabin };
    for (int i = 0; i < 2; i++) {
        SerAny* h = results[i];
        CHK(h && h->one && h->one == h->two);
        CHK(objClsInfo(h->one) == &SerCustom_clsinfo);
        CHK(((SerCustom*)h->one)->magic == 7);
    }

    strDestroy(&bare);
    strDestroy(&json);
    strDestroy(&bin);
    objRelease(&viabare);
    objRelease(&viajson);
    objRelease(&viabin);
    objRelease(&src);
    objRelease(&shared);
    return 0;
}

// The ways a document can lie about a reference, and the one backend that has no answer for one.
static int test_ser_referrors(void)
{
    SerError err   = { 0 };
    SerAny* victim = NULL;

    // An id nothing defined. The reader has no way to invent the object, so this is where it has
    // to stop rather than leaving a NULL somewhere the schema promised an instance.
    CHK(!fromJsonCls(_S "{ \"one\": { \"$ref\": 4 }, \"two\": null }",
                     stExt(SerAny),
                     stArgPtr(SerAny, &victim),
                     &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "never defined") >= 0);
    serErrorDestroy(&err);

    // The same id defined twice, which no writer of ours emits and which would silently make the
    // second definition unreachable.
    CHK(!fromJsonCls(_S "{ \"$id\": 0, \"one\": { \"$id\": 0, \"$type\": \"SerCls1\" } }",
                     stExt(SerAny),
                     stArgPtr(SerAny, &victim),
                     &err));
    if (err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "twice") >= 0);
    serErrorDestroy(&err);

    // A use that resolves to an instance the slot was not declared to hold. The wire name is
    // only on the definition, so without this check the wrong pointer lands in a typed member.
    SerHolder* holder = NULL;
    CHK(!fromJsonCls(_S "{ \"anyobj\": { \"$type\": \"SerCustom\", \"$value\": "
                       "{ \"$id\": 0, \"$value\": [ 1, \"x\" ] } }, "
                       "\"child\": { \"$ref\": 0 } }",
                     stExt(SerHolder),
                     stArgPtr(SerHolder, &holder),
                     &err));
    if (err.code != SER_Err_Type)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
    CHK(strFind(err.msg, 0, _S "declared to hold") >= 0);
    serErrorDestroy(&err);

    // SSD advertises no reference capability, so asking for them changes nothing there: shared
    // values are copied, and a cycle is still the error it was before references existed.
    SerCycle* a = sercycleCreate();
    SerCycle* b = sercycleCreate();
    a->next     = objAcquire(b);
    b->next     = objAcquire(a);

    SerWriter* w = serSsdWriterCreate(SER_Refs);
    CHK(!_serWrite(w, stExt(SerCycle), stArg(SerCycle, a)));
    if (w->err.code != SER_Err_Data)
        TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, w->err.code));
    CHK(strFind(w->err.msg, 0, _S "cycle") >= 0);
    serWriterDestroy(&w);

    objRelease(&a->next);
    objRelease(&b->next);
    objRelease(&a);
    objRelease(&b);
    objRelease(&holder);
    objRelease(&victim);
    return 0;
}

// ---------------------------------------------------------------------------------------
// stvar
// ---------------------------------------------------------------------------------------

// The struct counterpart of fromJsonCls/fromBinaryCls: a stvar declares no vocabulary of its
// own, so resolving a structp held inside one needs a StructSet resolver registered on the
// reader, the same way a bare top-level structp document would.
static bool fromJsonStruct(strref json, const STypeInfoExt* schema, stgeneric* val, SerError* err)
{
    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrPRegisterPull(sb, json)) {
        sbufRelease(&sb);
        return false;
    }

    SerReader* r = serJsonReaderCreate(sb, 0);
    serReaderAddResolver(r, serStructSetResolver, (void*)&TestStrSet_structset);
    bool ok = _serRead(r, schema, val);
    if (!ok && err) {
        *err   = r->err;
        r->err = (SerError) { 0 };
    }

    serReaderDestroy(&r);
    sbufRelease(&sb);
    return ok;
}

static bool fromBinaryStruct(strref doc, const STypeInfoExt* schema, stgeneric* val,
                             SerError* err)
{
    StreamBuffer* sb = sbufCreate(1024);
    if (!sbufStrPRegisterPull(sb, doc)) {
        sbufRelease(&sb);
        return false;
    }

    SerReader* r = serBinaryReaderCreate(sb, 0);
    serReaderAddResolver(r, serStructSetResolver, (void*)&TestStrSet_structset);
    bool ok = _serRead(r, schema, val);
    if (!ok && err) {
        *err   = r->err;
        r->err = (SerError) { 0 };
    }

    serReaderDestroy(&r);
    sbufRelease(&sb);
    return ok;
}

// stvar is the one fully dynamic slot in the model: no schema names a concrete type, so every
// value carries its own tag and the reader resolves it exactly the way a dynamic structp or a
// base-class object slot already does. Only what a flat wire name can spell is covered here --
// scalars, string, suid, buffer, a class instance, a structp naming its pointee, and a nested
// stvar recursing on itself. Containers and by-value structs are covered by test_ser_stvarerr;
// this is a deliberate, permanent limit of the construction, not a placeholder for a future
// compound-name syntax.
static int test_ser_stvar(void)
{
    // Every scalar width and both floats, straight through JSON and binary, keeping the exact
    // declared type -- not just the bit pattern -- since that is the whole point of a variant.
#define RT_STVAR_SCALAR(type, value)                                                \
    do {                                                                            \
        stvar src  = stvar(type, (value));                                          \
        stvar dstj = stvNone, dstb = stvNone;                                       \
        string js = 0, bn = 0;                                                      \
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));                 \
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));               \
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));           \
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));         \
        CHK(stvarIs(&dstj, type) && dstj.data.st_##type == (value));                \
        CHK(stvarIs(&dstb, type) && dstb.data.st_##type == (value));                \
        stvarDestroy(&dstj);                                                        \
        stvarDestroy(&dstb);                                                        \
        strDestroy(&js);                                                            \
        strDestroy(&bn);                                                            \
    } while (0)

    RT_STVAR_SCALAR(bool, true);
    RT_STVAR_SCALAR(int8, -0x7f);
    RT_STVAR_SCALAR(int16, -0x7ffe);
    RT_STVAR_SCALAR(int32, -1234567);
    RT_STVAR_SCALAR(int64, INT64_MIN + 1);
    RT_STVAR_SCALAR(uint8, 0xfe);
    RT_STVAR_SCALAR(uint16, 0xfffe);
    RT_STVAR_SCALAR(uint32, 0xfffffffeu);
    RT_STVAR_SCALAR(uint64, UINT64_MAX);
    RT_STVAR_SCALAR(float32, 0.5f);
    RT_STVAR_SCALAR(float64, 1.0 / 3.0);
#undef RT_STVAR_SCALAR

    // string, including NULL -- string and strref share a wire name and stype id, so this is
    // also the strref case.
    {
        stvar src  = stvar(string, _S "hello stvar");
        stvar dstj = stvNone, dstb = stvNone;
        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));
        CHK(stvarIs(&dstj, string) && strEq(dstj.data.st_string, _S "hello stvar"));
        CHK(stvarIs(&dstb, string) && strEq(dstb.data.st_string, _S "hello stvar"));
        stvarDestroy(&dstj);
        stvarDestroy(&dstb);
        strDestroy(&js);
        strDestroy(&bn);
    }
    {
        string none = 0;
        stvar src   = stvar(string, none);
        stvar dstj = stvNone, dstb = stvNone;
        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));
        CHK(stvarIs(&dstj, string) && strEmpty(dstj.data.st_string));
        CHK(stvarIs(&dstb, string) && strEmpty(dstb.data.st_string));
        stvarDestroy(&dstj);
        stvarDestroy(&dstb);
        strDestroy(&js);
        strDestroy(&bn);
    }

    // suid and buffer -- the two types this whole effort started with -- behave inside a stvar
    // exactly as they do in a plain member slot, just behind a tag.
    {
        SUID sid;
        suidGen(&sid, 11);
        stvar src  = stvar(suid, sid);
        stvar dstj = stvNone, dstb = stvNone;
        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));
        CHK(stvarIs(&dstj, suid) && dstj.data.st_suid->high == sid.high &&
            dstj.data.st_suid->low == sid.low);
        CHK(stvarIs(&dstb, suid) && dstb.data.st_suid->high == sid.high &&
            dstb.data.st_suid->low == sid.low);
        stvarDestroy(&dstj);
        stvarDestroy(&dstb);
        strDestroy(&js);
        strDestroy(&bn);
    }
    {
        static const uint8 raw[] = { 0x00, 0xff, 0x41, 0x00, 0x80, 0x7f, 0x01 };
        Buffer buf                = bufCreate(sizeof(raw));
        memcpy(buf->data, raw, sizeof(raw));
        buf->len = sizeof(raw);

        stvar src  = stvar(buffer, buf);
        stvar dstj = stvNone, dstb = stvNone;
        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));
        CHK(stvarIs(&dstj, buffer) && dstj.data.st_buffer->len == sizeof(raw) &&
            memcmp(dstj.data.st_buffer->data, raw, sizeof(raw)) == 0);
        CHK(stvarIs(&dstb, buffer) && dstb.data.st_buffer->len == sizeof(raw) &&
            memcmp(dstb.data.st_buffer->data, raw, sizeof(raw)) == 0);
        stvarDestroy(&dstj);
        stvarDestroy(&dstb);
        bufDestroy(&buf);
        strDestroy(&js);
        strDestroy(&bn);
    }

    // An explicit none -- a stvar that never held anything -- writes as a tag naming itself,
    // exactly like every other value, and reading it resets whatever the destination held
    // rather than leaving it alone.
    {
        stvar src = stvNone;
        stvar dstj = stvar(int32, 1), dstb = stvar(int32, 1);
        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(strFind(js, 0, _S "\"$type\": \"none\"") >= 0);
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));
        CHK(stvarIs(&dstj, none));
        CHK(stvarIs(&dstb, none));
        strDestroy(&js);
        strDestroy(&bn);
    }

    // A stvar nested inside a stvar -- free, since "stvar" is itself one of cx's own flat wire
    // names and the recursion is just another call to writeValue/readValue.
    {
        stvar inner = stvar(int32, 77);
        stvar outer = stvNone;
        stvarSet(&outer, stvar, inner);
        CHK(stvarIs(&outer, stvar) && _stvarOwns(&outer));

        stvar dstj = stvNone, dstb = stvNone;
        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, outer), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, outer), 0, NULL));
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));

        CHK(stvarIs(&dstj, stvar));
        stvar* nestedj = dstj.data.st_stvar;
        CHK(nestedj && stvarIs(nestedj, int32) && nestedj->data.st_int32 == 77);

        CHK(stvarIs(&dstb, stvar));
        stvar* nestedb = dstb.data.st_stvar;
        CHK(nestedb && stvarIs(nestedb, int32) && nestedb->data.st_int32 == 77);

        stvarDestroy(&outer);
        stvarDestroy(&dstj);
        stvarDestroy(&dstb);
        strDestroy(&js);
        strDestroy(&bn);
    }

    // An object, tagged by its class's wire name -- the read side needs the same resolver a
    // bare top-level object document would, since a stvar carries no classset of its own.
    {
        SerCls1* obj = sercls1Create();
        strDup(&obj->title, _S "via stvar");
        obj->revision = 9;

        stvar src = stvNone;
        stvarSet(&src, object, ObjInst(obj));

        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(strFind(js, 0, _S "\"$type\": \"SerCls1\"") >= 0);

        stvar dstj = stvNone, dstb = stvNone;
        CHK(fromJsonCls(js, stExt(stvar), stArgPtr(stvar, &dstj), NULL));
        CHK(fromBinaryCls(bn, stExt(stvar), stArgPtr(stvar, &dstb), NULL));

        CHK(stvarIs(&dstj, object) && objClsInfo(dstj.data.st_object) == &SerCls1_clsinfo);
        if (!strEq(((SerCls1*)dstj.data.st_object)->title, _S "via stvar"))
            TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, ((SerCls1*)dstj.data.st_object)->title), stvar(strref, _S "via stvar"));
        CHK(((SerCls1*)dstj.data.st_object)->revision == 9);

        CHK(stvarIs(&dstb, object) && objClsInfo(dstb.data.st_object) == &SerCls1_clsinfo);
        if (!strEq(((SerCls1*)dstb.data.st_object)->title, _S "via stvar"))
            TEST_FAIL(1, _SL("strEq mismatch: a='${string}', b='${string}'"), stvar(strref, ((SerCls1*)dstb.data.st_object)->title), stvar(strref, _S "via stvar"));

        stvarDestroy(&src);
        stvarDestroy(&dstj);
        stvarDestroy(&dstb);
        objRelease(&obj);
        strDestroy(&js);
        strDestroy(&bn);
    }

    // A NULL object variant: tag "object" -- the class-less descriptor, resolvable through cx's
    // own builtin table with nothing registered -- then null.
    {
        ObjInst* none = NULL;
        stvar src     = stvNone;
        stvarSet(&src, object, none);

        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(strFind(js, 0, _S "\"$type\": \"object\"") >= 0);

        stvar dstj = stvar(int32, 1), dstb = stvar(int32, 1);
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));
        CHK(stvarIs(&dstj, object) && !dstj.data.st_object);
        CHK(stvarIs(&dstb, object) && !dstb.data.st_object);

        strDestroy(&js);
        strDestroy(&bn);
    }

    // A structp, tagged by the pointee's own struct name. Unlike a dynamic structp[SomeSet]
    // slot, a stvar carries no set of its own, so the read side needs a StructSet resolver
    // registered exactly the way a bare top-level structp document would.
    {
        TestStr1* one = (TestStr1*)_structAlloc(&TestStr1_structinfo);
        fillTestStr1(one, 5);

        stvar src = stvNone;
        stvarSet(&src, structp, STRUCTBASE(one));

        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(strFind(js, 0, _S "\"$type\": \"TestStr1\"") >= 0);

        stvar dstj = stvNone, dstb = stvNone;
        CHK(fromJsonStruct(js, stExt(stvar), stArgPtr(stvar, &dstj), NULL));
        CHK(fromBinaryStruct(bn, stExt(stvar), stArgPtr(stvar, &dstb), NULL));

        CHK(stvarTypeId(&dstj) == stTypeId(structp));
        CHK(dstj.data.st_structp && dstj.data.st_structp->structinfo == &TestStr1_structinfo);
        CHK(structDeepEq(STRUCTBASE(one), dstj.data.st_structp));

        CHK(stvarTypeId(&dstb) == stTypeId(structp));
        CHK(dstb.data.st_structp && dstb.data.st_structp->structinfo == &TestStr1_structinfo);
        CHK(structDeepEq(STRUCTBASE(one), dstb.data.st_structp));

        stvarDestroy(&src);
        stvarDestroy(&dstj);
        stvarDestroy(&dstb);
        _structDestroy((StructBase**)&one);
        strDestroy(&js);
        strDestroy(&bn);
    }

    // A NULL structp variant: tag "structp" -- the bare, pointee-less pointer type, resolvable
    // through cx's builtin table the same way "object" is -- then null. It comes back typed
    // structp with a NULL pointer, not collapsed to stvNone.
    {
        stvar src = stvNone;
        stvarSet(&src, structp, (StructBase*)NULL);

        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(stvar), stArg(stvar, src), 0, NULL));
        CHK(strFind(js, 0, _S "\"$type\": \"structp\"") >= 0);

        stvar dstj = stvar(int32, 1), dstb = stvar(int32, 1);
        CHK(fromJson(js, stExt(stvar), stArgPtr(stvar, &dstj), 0, NULL));
        CHK(fromBinary(bn, stExt(stvar), stArgPtr(stvar, &dstb), 0, NULL));
        CHK(stvarTypeId(&dstj) == stTypeId(structp) && !dstj.data.st_structp);
        CHK(stvarTypeId(&dstb) == stTypeId(structp) && !dstb.data.st_structp);

        strDestroy(&js);
        strDestroy(&bn);
    }

    // A zeroed stvar member is its type's default (structInit zero-fills, so .simple._type is
    // NULL, i.e. stType(none)) and is omitted like any other default -- unless SER_EmitDefaults
    // asks for it, in which case it goes out as an explicit "none" tag rather than vanishing.
    {
        TestStrVar def;
        structInit(TestStrVar, &def);
        CHK(stvarIs(&def.simple, none));

        string omitted = 0, forced = 0;
        CHK(toJson(&omitted, stExt(TestStrVar), stArg(TestStrVar, def), 0, NULL));
        CHK(strFind(omitted, 0, _S "simple") == -1);

        CHK(toJson(&forced, stExt(TestStrVar), stArg(TestStrVar, def), SER_EmitDefaults, NULL));
        CHK(strFind(forced, 0, _S "\"$type\": \"none\"") >= 0);

        TestStrVar back;
        structInit(TestStrVar, &back);
        CHK(fromJson(forced, stExt(TestStrVar), stArgPtr(TestStrVar, &back), 0, NULL));
        CHK(stvarIs(&back.simple, none));

        strDestroy(&omitted);
        strDestroy(&forced);
        structDestroyMembers(&def);
        structDestroyMembers(&back);
    }

    // TestStrVar end to end: the container-of-stvar case this feature exists for. A container
    // only ever knows its element type is "stvar" -- each slot is a self-contained tagged value.
    {
        TestStrVar src;
        structInit(TestStrVar, &src);
        stvarSet(&src.simple, int32, 42);

        saInit(&src.vals, stvar, 4);
        stvar v1 = stvar(int32, 1), v2 = stvar(string, _S "two"), v3 = stvNone;
        saPush(&src.vals, stvar, v1);
        saPush(&src.vals, stvar, v2);
        saPush(&src.vals, stvar, v3);

        htInit(&src.tagged, string, stvar, 4);
        stvar tv = stvar(bool, true);
        htInsert(&src.tagged, string, _S "flag", stvar, tv);

        string js = 0, bn = 0;
        CHK(toJson(&js, stExt(TestStrVar), stArg(TestStrVar, src), 0, NULL));
        CHK(toBinary(&bn, stExt(TestStrVar), stArg(TestStrVar, src), 0, NULL));

        TestStrVar viajson, viabin;
        structInit(TestStrVar, &viajson);
        structInit(TestStrVar, &viabin);
        CHK(fromJson(js, stExt(TestStrVar), stArgPtr(TestStrVar, &viajson), 0, NULL));
        CHK(fromBinary(bn, stExt(TestStrVar), stArgPtr(TestStrVar, &viabin), 0, NULL));

        TestStrVar* results[] = { &viajson, &viabin };
        for (int i = 0; i < 2; i++) {
            TestStrVar* r = results[i];
            CHK(stvarIs(&r->simple, int32) && r->simple.data.st_int32 == 42);
            CHK(saSize(r->vals) == 3);
            CHK(stvarIs(&r->vals.a[0], int32) && r->vals.a[0].data.st_int32 == 1);
            CHK(stvarIs(&r->vals.a[1], string) && strEq(r->vals.a[1].data.st_string, _S "two"));
            CHK(stvarIs(&r->vals.a[2], none));

            stvar found = stvNone;
            CHK(htFind(r->tagged, string, _S "flag", stvar, &found));
            CHK(stvarIs(&found, bool) && found.data.st_bool == true);
            stvarDestroy(&found);
        }

        structDestroyMembers(&src);
        structDestroyMembers(&viajson);
        structDestroyMembers(&viabin);
        strDestroy(&js);
        strDestroy(&bn);
    }

    return 0;
}

// Everything a stvar refuses to hold, and the one format it can never reach regardless of what
// it holds: SSD advertises no type tags, and every stvar needs one.
static int test_ser_stvarerr(void)
{
    // A container has no flat wire name -- refused outright as an unsupported use of stvar, not
    // deferred to some future compound-name syntax.
    {
        sa_int32 arr = { 0 };
        saInit(&arr, int32, 4);
        saPush(&arr, int32, 1);

        stvar v = stvNone;
        stvarSet(&v, sarray, arr);

        SerError err = { 0 };
        string js    = 0;
        CHK(!toJson(&js, stExt(stvar), stArg(stvar, v), 0, &err));
        if (err.code != SER_Err_Unsupported)
            TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
        CHK(!strEmpty(err.path));

        serErrorDestroy(&err);
        stvarDestroy(&v);
        saDestroy(&arr);
        strDestroy(&js);
    }
    {
        hashtable ht = NULL;
        htInit(&ht, string, int32, 4);
        htInsert(&ht, string, _S "a", int32, 1);

        stvar v = stvNone;
        stvarSet(&v, hashtable, ht);

        SerError err = { 0 };
        string js    = 0;
        CHK(!toJson(&js, stExt(stvar), stArg(stvar, v), 0, &err));
        if (err.code != SER_Err_Unsupported)
            TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
        CHK(!strEmpty(err.path));

        serErrorDestroy(&err);
        stvarDestroy(&v);
        htDestroy(&ht);
        strDestroy(&js);
    }

    // A struct held by value has no flat name distinct from its structp spelling -- accepting
    // both would make one of them read back as the other -- so it is refused the same way.
    {
        TestStr1* one = (TestStr1*)_structAlloc(&TestStr1_structinfo);
        fillTestStr1(one, 1);

        stvar v = stvNone;
        stvarSet(&v, TestStr1, *one);

        SerError err = { 0 };
        string js    = 0;
        CHK(!toJson(&js, stExt(stvar), stArg(stvar, v), 0, &err));
        if (err.code != SER_Err_Unsupported)
            TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
        CHK(!strEmpty(err.path));

        serErrorDestroy(&err);
        stvarDestroy(&v);
        _structDestroy((StructBase**)&one);
        strDestroy(&js);
    }

    // SSD advertises no type tags at all, so nothing dynamic can reach it -- a stvar least of
    // all, regardless of what it holds.
    {
        stvar v = stvar(int32, 5);

        SerError err = { 0 };
        SSDNode* t   = toSsd(stExt(stvar), stArg(stvar, v), &err);
        CHK(!t);
        if (err.code != SER_Err_Unsupported)
            TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Unsupported), stvar(int32, err.code));
        CHK(strFind(err.msg, 0, _S "type tag") >= 0);
        serErrorDestroy(&err);
    }

    // A tag naming nothing any resolver knows.
    {
        stvar dst    = stvNone;
        SerError err = { 0 };
        CHK(!fromJson(_S "{ \"$type\": \"NoSuchType\", \"$value\": 1 }",
                      stExt(stvar),
                      stArgPtr(stvar, &dst),
                      0,
                      &err));
        if (err.code != SER_Err_Type)
            TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Type), stvar(int32, err.code));
        CHK(strFind(err.msg, 0, _S "NoSuchType") >= 0);
        serErrorDestroy(&err);
    }

    // A stvar's value is fully dynamic, so a document that never says what it holds -- a bare
    // scalar with no wrapper -- cannot be read into one.
    {
        stvar dst    = stvNone;
        SerError err = { 0 };
        CHK(!fromJson(_S "42", stExt(stvar), stArgPtr(stvar, &dst), 0, &err));
        if (err.code != SER_Err_Data)
            TEST_FAIL(1, _SL("expected ${int}, got ${int}"), stvar(int32, SER_Err_Data), stvar(int32, err.code));
        serErrorDestroy(&err);
    }

    return 0;
}

testfunc sertest_funcs[] = {
    { "scalars",        test_ser_scalars          },
    { "string",         test_ser_string           },
    { "suid",           test_ser_suid             },
    { "buffer",         test_ser_buffer           },
    { "array",          test_ser_array            },
    { "hashtable",      test_ser_hashtable        },
    { "intkeys",        test_ser_intkeys          },
    { "struct",         test_ser_struct           },
    { "structp",        test_ser_structp          },
    { "structpnull",    test_ser_structpnull      },
    { "structpdyn",     test_ser_structpdynamic   },
    { "structpbounds",  test_ser_structpsetbounds },
    { "nested",         test_ser_nested           },
    { "fixedarray",     test_ser_fixedarray       },
    { "overwrite",      test_ser_overwrite        },
    { "errors",         test_ser_errors           },
    { "unknown",        test_ser_unknown          },
    { "schema",         test_ser_schema           },
    { "generated",      test_ser_generated        },
    { "resolve",        test_ser_resolve          },
    { "json",           test_ser_json             },
    { "jsonnested",     test_ser_jsonnested       },
    { "jsonnum",        test_ser_jsonnum          },
    { "jsondef",        test_ser_jsondefaults     },
    { "jsonerrors",     test_ser_jsonerrors       },
    { "binary",         test_ser_binary           },
    { "binnested",      test_ser_binnested        },
    { "binnum",         test_ser_binnum           },
    { "bindef",         test_ser_bindefaults      },
    { "binerrors",      test_ser_binerrors        },
    { "cross",          test_ser_crossformat      },
    { "class",          test_ser_class            },
    { "classgap",       test_ser_classgap         },
    { "classpoly",      test_ser_classpoly        },
    { "classcustom",    test_ser_classcustom      },
    { "classset",       test_ser_classset         },
    { "classsetbounds", test_ser_classsetbounds   },
    { "classcycle",     test_ser_classcycle       },
    { "classssd",       test_ser_classssd         },
    { "rename",         test_ser_rename           },
    { "refs",           test_ser_refs             },
    { "refcycle",       test_ser_refcycle         },
    { "refwrap",        test_ser_refwrap          },
    { "referrors",      test_ser_referrors        },
    { "stvar",          test_ser_stvar            },
    { "stvarerr",       test_ser_stvarerr         },
    { 0,                0                         }
};
