#include <stdio.h>
#include <string.h>
#include <cx/stype/stvar.h>
#include <cx/suid/suid.h>
#include <cx/ssdtree.h>
#include <cx/string.h>
#include <cx/string/strtest.h>
#include "objtestobj.h"

#define TEST_FILE stvartest
#define TEST_FUNCS stvartest_funcs
#include "common.h"

// A plain POD blob larger than the 8-byte stgeneric union, used to exercise the
// opaque/PassPtr storage path.
typedef struct TestBlob {
    int32 a;
    float64 b;
    char s[24];
} TestBlob;

// Put a suid (a 128-bit PassPtr value) into an stvar, copy it, then destroy the
// original and confirm the copy is independent and intact.
static int test_suid()
{
    int ret = 0;

    SUID s1 = { .high = 0x1122334455667788ULL, .low = 0x99aabbccddeeff00ULL };

    stvar v1 = stvNone;
    stvarSet(&v1, suid, s1);

    // stvar should now own its own heap copy of the suid
    if (!stvarIs(&v1, suid) || !_stvarOwns(&v1))
        TEST_FAILV(ret, 1, _SL("!stvarIs(&v1, suid) || !_stvarOwns(&v1)"), stvNone);

    void* p1 = v1.data.st_ptr;
    if (!p1 || memcmp(p1, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("!p1 || memcmp mismatch, p1=${ptr}"), stvar(ptr, p1));

    // copy to a second variant; it must allocate its own storage
    stvar v2 = stvNone;
    stvarCopy(&v2, v1);

    if (!_stvarOwns(&v2) || v2.data.st_ptr == p1 ||
        memcmp(v2.data.st_ptr, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("!_stvarOwns(&v2) || v2.data.st_ptr=${ptr} == p1=${ptr} || memcmp mismatch"),
                   stvar(ptr, v2.data.st_ptr), stvar(ptr, p1));

    // destroying the original must not disturb the copy
    stvarDestroy(&v1);
    if (!stvarIs(&v1, none) || _stvarOwns(&v1))
        TEST_FAILV(ret, 1, _SL("!stvarIs(&v1, none) || _stvarOwns(&v1)"), stvNone);
    if (memcmp(v2.data.st_ptr, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("v2 no longer matches s1 after destroying v1"), stvNone);

    stvarDestroy(&v2);
    if (!stvarIs(&v2, none) || _stvarOwns(&v2))
        TEST_FAILV(ret, 1, _SL("!stvarIs(&v2, none) || _stvarOwns(&v2)"), stvNone);

    return ret;
}

// Same idea with an oversized opaque POD blob.
static int test_opaque()
{
    int ret = 0;

    TestBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.a = 42;
    blob.b = 3.14159;
    memcpy(blob.s, "hello opaque world", sizeof("hello opaque world"));

    stvar v = stvNone;
    stvarSet(&v, opaque, blob);

    if (!stHasFlag(stvarType(&v), PassPtr) || !_stvarOwns(&v) ||
        stvarType(&v)->size != sizeof(TestBlob))
        TEST_FAILV(ret, 1, _SL("PassPtr/owns check failed, or stvarType(&v)->size=${uint} != sizeof(TestBlob)=${uint}"),
                   stvar(uint64, (uint64)stvarType(&v)->size), stvar(uint64, (uint64)sizeof(TestBlob)));

    // must be a distinct heap copy, byte-identical to the source
    if (v.data.st_ptr == &blob || memcmp(v.data.st_ptr, &blob, sizeof(TestBlob)) != 0)
        TEST_FAILV(ret, 1, _SL("v.data.st_ptr=${ptr} aliases &blob=${ptr} or memcmp mismatch"),
                   stvar(ptr, v.data.st_ptr), stvar(ptr, &blob));

    stvar copy = stvNone;
    stvarCopy(&copy, v);
    if (copy.data.st_ptr == v.data.st_ptr ||
        memcmp(copy.data.st_ptr, &blob, sizeof(TestBlob)) != 0)
        TEST_FAILV(ret, 1, _SL("copy.data.st_ptr=${ptr} aliases v.data.st_ptr=${ptr} or memcmp mismatch"),
                   stvar(ptr, copy.data.st_ptr), stvar(ptr, v.data.st_ptr));

    // destroy the original; copy remains valid
    stvarDestroy(&v);
    if (memcmp(copy.data.st_ptr, &blob, sizeof(TestBlob)) != 0)
        TEST_FAILV(ret, 1, _SL("copy no longer matches blob after destroying v"), stvNone);

    // stvarSet with replace semantics: overwrite the owned value with a new one
    stvarSet(&copy, int32, 99);
    if (!stvarIs(&copy, int32) || _stvarOwns(&copy) ||
        copy.data.st_int32 != 99)
        TEST_FAILV(ret, 1, _SL("type/owns check failed, or copy.data.st_int32=${int} != 99"),
                   stvar(int32, copy.data.st_int32));

    stvarDestroy(&copy);

    return ret;
}

// A transient variant created inline for varargs use must remain a plain, non-owning
// pointer to the caller's temporary and extract correctly through a stvlist.
static int test_transient()
{
    int ret = 0;

    SUID s1 = { .high = 0xdeadbeefcafef00dULL, .low = 0x0123456789abcdefULL };

    stvar args[] = {
        stvar(suid, s1),
        stvar(int32, 7),
    };

    // transient variants do NOT own storage
    if (_stvarOwns(&args[0]) || _stvarOwns(&args[1]))
        TEST_FAILV(ret, 1, _SL("_stvarOwns(&args[0]) || _stvarOwns(&args[1])"), stvNone);

    stvlist l;
    stvlInit(&l, 2, args);

    SUID got;
    memset(&got, 0, sizeof(got));
    if (!stvlNext(&l, suid, &got) || memcmp(&got, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("stvlNext(suid) failed or got != s1"), stvNone);

    int32 n = 0;
    if (!stvlNext(&l, int32, &n) || n != 7)
        TEST_FAILV(ret, 1, _SL("stvlNext(int32) failed or n=${int} != 7"), stvar(int32, n));

    // destroying a non-owning transient must be safe and must not free anything
    stvarDestroy(&args[0]);
    if (_stvarOwns(&args[0]) || !stvarIs(&args[0], none))
        TEST_FAILV(ret, 1, _SL("_stvarOwns(&args[0]) || !stvarIs(&args[0], none)"), stvNone);

    return ret;
}

// Round-trip a PassPtr value through an ssdtree node to confirm persistence is
// value-semantic and does not dangle after the source temporary is gone.
static int test_ssd_roundtrip()
{
    int ret = 0;

    SUID s1 = { .high = 0x1020304050607080ULL, .low = 0x90a0b0c0d0e0f000ULL };

    SSDNode* tree = ssdCreateHashtable();

    // the transient stvar(suid, ...) points at a stack temporary; ssdSet must deep-copy
    ssdSet(tree, _S"id", true, stvar(suid, s1));

    stvar outvar = { 0 };
    if (!ssdGet(tree, _S"id", &outvar) || !stvarIs(&outvar, suid) ||
        memcmp(outvar.data.st_ptr, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("ssdGet failed, type mismatch, or memcmp mismatch"), stvNone);

    stvarDestroy(&outvar);
    objRelease(&tree);

    return ret;
}

// A variant can itself hold another variant. Because stvar is a PassPtr type, the
// outer variant owns a deep, recursive copy of the inner one — including any storage
// the inner variant owns (here, a nested suid). Destroying the outer must recursively
// free everything.
static int test_nested()
{
    int ret = 0;

    SUID s1 = { .high = 0xa1a2a3a4a5a6a7a8ULL, .low = 0xb1b2b3b4b5b6b7b8ULL };

    // inner variant owns its own heap copy of the suid
    stvar inner = stvNone;
    stvarSet(&inner, suid, s1);
    if (!stvarIs(&inner, suid) || !_stvarOwns(&inner))
        TEST_FAILV(ret, 1, _SL("!stvarIs(&inner, suid) || !_stvarOwns(&inner)"), stvNone);

    // outer variant owns a heap copy of the inner variant
    stvar outer = stvNone;
    stvarSet(&outer, stvar, inner);
    if (!stvarIs(&outer, stvar) || !_stvarOwns(&outer))
        TEST_FAILV(ret, 1, _SL("!stvarIs(&outer, stvar) || !_stvarOwns(&outer)"), stvNone);

    // the nested variant must be a distinct object that in turn owns its own distinct
    // copy of the suid
    stvar* nested = outer.data.st_stvar;
    if (nested == &inner || !stvarIs(nested, suid) || !_stvarOwns(nested) ||
        nested->data.st_ptr == inner.data.st_ptr ||
        memcmp(nested->data.st_ptr, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("nested=${ptr} aliasing/type/owns/memcmp check failed"), stvar(ptr, nested));

    // copy the outer variant; the copy is fully independent all the way down
    stvar outer2 = stvNone;
    stvarCopy(&outer2, outer);
    stvar* nested2 = outer2.data.st_stvar;
    if (nested2 == nested || nested2->data.st_ptr == nested->data.st_ptr ||
        memcmp(nested2->data.st_ptr, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("nested2=${ptr} aliasing/memcmp check failed vs nested=${ptr}"),
                   stvar(ptr, nested2), stvar(ptr, nested));

    // destroy the original inner and outer; the independent copy survives intact
    stvarDestroy(&inner);
    stvarDestroy(&outer);
    if (!stvarIs(&outer, none) ||
        memcmp(outer2.data.st_stvar->data.st_ptr, &s1, sizeof(SUID)) != 0)
        TEST_FAILV(ret, 1, _SL("!stvarIs(&outer, none) || outer2's nested copy no longer matches s1"), stvNone);

    // recursive teardown of the surviving copy (frees nested suid + nested variant)
    stvarDestroy(&outer2);
    if (!stvarIs(&outer2, none))
        TEST_FAILV(ret, 1, _SL("!stvarIs(&outer2, none)"), stvNone);

    return ret;
}

// Positional and keyed reads that return the value directly. Keyed variants are interleaved with the
// positional ones on purpose: positions must count only the unkeyed variants, so a keyed argument
// added anywhere never shifts one.
static int test_accessors()
{
    int ret = 0;

    string str = 0;
    strCopy(&str, _S"accessor");
    int marker    = 0;
    TestCls1* obj = testcls1Create();
    obj->data     = 99;
    SUID s1       = { .high = 0xdeadbeefcafef00dULL, .low = 0x0123456789abcdefULL };

    stvar args[] = {
        stvark(count, int32, 7),
        stvar(int32, 42),
        stvark(name, string, str),
        stvar(string, str),
        stvar(ptr, &marker),
        stvark(obj, object, obj),
        stvar(suid, s1),
        stvar(object, obj),
        stvark(where, ptr, &marker),
    };
    stvlist list;
    stvlInit(&list, sizeof(args) / sizeof(args[0]), args);

    // positional: keyed variants are skipped when counting
    int32 num     = stvlAt(&list, 0, int32);
    strref pstr   = stvlAt(&list, 1, strref);
    void* pptr    = stvlAtPtr(&list, 2);
    SUID* pid     = stvlAt(&list, 3, suid);   // oversized: a pointer to the stored value
    TestCls1* po  = stvlAtObj(&list, 4, TestCls1);
    if (num != 42 || !strEq(pstr, str) || pptr != &marker || !pid || !suidEq(pid, &s1) ||
        !po || po->data != 99)
        TEST_FAILV(ret, 1, _SL("positional: num=${int} str='${string}' ptrok=${bool} suidok=${bool} objok=${bool} (want 42, 'accessor', true, true, true)"),
                   stvar(int32, num), stvar(strref, pstr), stvar(bool, pptr == &marker),
                   stvar(bool, pid && suidEq(pid, &s1)), stvar(bool, po && po->data == 99));

    // positional reads do not move the walker
    if (list.cursor != 0)
        TEST_FAILV(ret, 1, _SL("cursor=${int} after positional reads (want 0)"), stvar(int32, list.cursor));

    // keyed, read in a different order than stored
    TestCls1* ko = stvlFindObj(&list, obj, TestCls1);
    void* kptr   = stvlFindPtr(&list, where);
    strref kstr  = stvlFindVal(&list, name, strref);
    int32 knum   = stvlFindVal(&list, count, int32);
    if (knum != 7 || !strEq(kstr, str) || kptr != &marker || !ko || ko->data != 99)
        TEST_FAILV(ret, 1, _SL("keyed: num=${int} str='${string}' ptrok=${bool} objok=${bool} (want 7, 'accessor', true, true)"),
                   stvar(int32, knum), stvar(strref, kstr), stvar(bool, kptr == &marker),
                   stvar(bool, ko && ko->data == 99));

    // a missing key, or a key holding another type, reads as zero
    int32 missing  = stvlFindVal(&list, nosuchkey, int32);
    int32 wrongtyp = stvlFindVal(&list, name, int32);
    if (missing != 0 || wrongtyp != 0)
        TEST_FAILV(ret, 1, _SL("missing=${int} wrongtype=${int} (want 0, 0)"), stvar(int32, missing),
                   stvar(int32, wrongtyp));

    // nothing was copied out: reading borrowed, so no references were taken
    if (strTestRefCount(str) != 1 || atomicLoad(uintptr, &obj->_ref, Acquire) != 1)
        TEST_FAILV(ret, 1, _SL("after reads: str refcount=${int} obj refcount=${uint} (want 1, 1)"),
                   stvar(int32, strTestRefCount(str)),
                   stvar(uint64, (uint64)atomicLoad(uintptr, &obj->_ref, Acquire)));

    objRelease(&obj);
    strDestroy(&str);
    return ret;
}

testfunc stvartest_funcs[] = {
    { "suid", test_suid },
    { "opaque", test_opaque },
    { "transient", test_transient },
    { "ssd_roundtrip", test_ssd_roundtrip },
    { "nested", test_nested },
    { "accessors", test_accessors },
    { 0, 0 }
};
