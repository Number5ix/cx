#include <stdio.h>
#include <string.h>
#include <cx/stype/stvar.h>
#include <cx/suid/suid.h>
#include <cx/ssdtree.h>
#include <cx/string.h>

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
        ret = 1;

    void* p1 = v1.data.st_ptr;
    if (!p1 || memcmp(p1, &s1, sizeof(SUID)) != 0)
        ret = 1;

    // copy to a second variant; it must allocate its own storage
    stvar v2 = stvNone;
    stvarCopy(&v2, v1);

    if (!_stvarOwns(&v2) || v2.data.st_ptr == p1 ||
        memcmp(v2.data.st_ptr, &s1, sizeof(SUID)) != 0)
        ret = 1;

    // destroying the original must not disturb the copy
    stvarDestroy(&v1);
    if (!stvarIs(&v1, none) || _stvarOwns(&v1))
        ret = 1;
    if (memcmp(v2.data.st_ptr, &s1, sizeof(SUID)) != 0)
        ret = 1;

    stvarDestroy(&v2);
    if (!stvarIs(&v2, none) || _stvarOwns(&v2))
        ret = 1;

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
        ret = 1;

    // must be a distinct heap copy, byte-identical to the source
    if (v.data.st_ptr == &blob || memcmp(v.data.st_ptr, &blob, sizeof(TestBlob)) != 0)
        ret = 1;

    stvar copy = stvNone;
    stvarCopy(&copy, v);
    if (copy.data.st_ptr == v.data.st_ptr ||
        memcmp(copy.data.st_ptr, &blob, sizeof(TestBlob)) != 0)
        ret = 1;

    // destroy the original; copy remains valid
    stvarDestroy(&v);
    if (memcmp(copy.data.st_ptr, &blob, sizeof(TestBlob)) != 0)
        ret = 1;

    // stvarSet with replace semantics: overwrite the owned value with a new one
    stvarSet(&copy, int32, 99);
    if (!stvarIs(&copy, int32) || _stvarOwns(&copy) ||
        copy.data.st_int32 != 99)
        ret = 1;

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
        ret = 1;

    stvlist l;
    stvlInit(&l, 2, args);

    SUID got;
    memset(&got, 0, sizeof(got));
    if (!stvlNext(&l, suid, &got) || memcmp(&got, &s1, sizeof(SUID)) != 0)
        ret = 1;

    int32 n = 0;
    if (!stvlNext(&l, int32, &n) || n != 7)
        ret = 1;

    // destroying a non-owning transient must be safe and must not free anything
    stvarDestroy(&args[0]);
    if (_stvarOwns(&args[0]) || !stvarIs(&args[0], none))
        ret = 1;

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
        ret = 1;

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
        ret = 1;

    // outer variant owns a heap copy of the inner variant
    stvar outer = stvNone;
    stvarSet(&outer, stvar, inner);
    if (!stvarIs(&outer, stvar) || !_stvarOwns(&outer))
        ret = 1;

    // the nested variant must be a distinct object that in turn owns its own distinct
    // copy of the suid
    stvar* nested = outer.data.st_stvar;
    if (nested == &inner || !stvarIs(nested, suid) || !_stvarOwns(nested) ||
        nested->data.st_ptr == inner.data.st_ptr ||
        memcmp(nested->data.st_ptr, &s1, sizeof(SUID)) != 0)
        ret = 1;

    // copy the outer variant; the copy is fully independent all the way down
    stvar outer2 = stvNone;
    stvarCopy(&outer2, outer);
    stvar* nested2 = outer2.data.st_stvar;
    if (nested2 == nested || nested2->data.st_ptr == nested->data.st_ptr ||
        memcmp(nested2->data.st_ptr, &s1, sizeof(SUID)) != 0)
        ret = 1;

    // destroy the original inner and outer; the independent copy survives intact
    stvarDestroy(&inner);
    stvarDestroy(&outer);
    if (!stvarIs(&outer, none) ||
        memcmp(outer2.data.st_stvar->data.st_ptr, &s1, sizeof(SUID)) != 0)
        ret = 1;

    // recursive teardown of the surviving copy (frees nested suid + nested variant)
    stvarDestroy(&outer2);
    if (!stvarIs(&outer2, none))
        ret = 1;

    return ret;
}

testfunc stvartest_funcs[] = {
    { "suid", test_suid },
    { "opaque", test_opaque },
    { "transient", test_transient },
    { "ssd_roundtrip", test_ssd_roundtrip },
    { "nested", test_nested },
    { 0, 0 }
};
