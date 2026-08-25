#include <cx/container/hashtable.h>
#include <cx/container/sarray.h>
#include <cx/serialize/sertype.h>
#include <cx/string.h>
#include <cx/struct/struct.h>

#include "objtestobj.h"

#define TEST_FILE  structtest
#define TEST_FUNCS structtest_funcs
#include "common.h"

// Logs which assertion failed and returns -- a bare `return 1` is not diagnosable in tests
// this size, and these exercise code paths that had never been run at all before.
#define CHK(cond)                                                          \
    do {                                                                   \
        if (!(cond))                                                       \
            TEST_FAIL(1, _SL("assertion failed: ${string}"), stvar(strref, _SL(#cond))); \
    } while (0)

// ---------------------------------------------------------------------------------------
// Deep structural comparison
//
// stCmp_struct is a raw memcmp over the struct's bytes, so two structurally equal instances
// holding a string or a container compare unequal -- their members are distinct allocations.
// Anything that wants to assert "these two structs hold the same values" has to walk
// StructInfo::members[] and compare field by field, which is what this does.
//
// Round-trip assertions in the serialization work will need exactly this, so it lives at
// file scope rather than inside a single test.
// ---------------------------------------------------------------------------------------

static bool deepEqValue(stype st, const void* ap, const void* bp);

bool structDeepEq(const StructBase* a, const StructBase* b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->structinfo != b->structinfo)
        return false;

    const StructInfo* si = a->structinfo;
    if (!si)
        return false;

    for (int i = 0; i < si->nmembers; i++) {
        const StructMemberDesc* m = &si->members[i];
        uint32 n                  = m->arrsize ? m->arrsize : 1;
        size_t stride             = stGetSize(m->schema->type);

        for (uint32 e = 0; e < n; e++) {
            const char* ep = (const char*)a + m->offset + e * stride;
            const char* fp = (const char*)b + m->offset + e * stride;
            if (!deepEqValue(m->schema->type, ep, fp))
                return false;
        }
    }
    return true;
}

static bool deepEqArray(stype st, const void* ap, const void* bp)
{
    sa_ref ra = *(const sa_ref*)ap;
    sa_ref rb = *(const sa_ref*)bp;

    if (!ra.a || !rb.a)
        return ra.a == rb.a;

    stype et = _saHdr(ra)->elemtype;
    if (!stEq(et, _saHdr(rb)->elemtype))
        return false;

    int32 n = _saHdr(ra)->count;
    if (n != _saHdr(rb)->count)
        return false;

    size_t sz = stGetSize(et);
    for (int32 i = 0; i < n; i++) {
        if (!deepEqValue(et, (const char*)ra.a + i * sz, (const char*)rb.a + i * sz))
            return false;
    }
    return true;
}

static bool deepEqHashtable(stype st, const void* ap, const void* bp)
{
    hashtable ha = *(const hashtable*)ap;
    hashtable hb = *(const hashtable*)bp;

    if (!ha || !hb)
        return ha == hb;
    if (htSize(ha) != htSize(hb))
        return false;
    if (!stEq(htKeyType(ha), htKeyType(hb)) || !stEq(htValType(ha), htValType(hb)))
        return false;

    stype kt = htKeyType(ha), vt = htValType(ha);
    bool ok = true;

    // Iteration order is insertion order, which two independently built tables need not
    // share, so each key is looked up in the other table rather than compared positionally.
    htiter iter;
    htiInit(&iter, ha);
    while (htiValid(&iter) && ok) {
        void* kp = _hteElemKeyPtr(iter.hdr, iter.slot);
        void* vp = _hteElemValPtr(iter.hdr, iter.slot);

        htelem found = _htFind(hb, stStored(kt, kp), NULL, 0);
        if (!found) {
            ok = false;
            break;
        }
        if (stGetSize(vt) > 0)
            ok = deepEqValue(vt, vp, _hteElemValPtr(_htHdr(hb), found));

        htiNext(&iter);
    }
    // the iterator holds a lock on the table; this has to run on every path out
    htiFinish(&iter);

    return ok;
}

static bool deepEqValue(stype st, const void* ap, const void* bp)
{
    if (!st)
        return false;

    switch (st->id) {
    case stTypeId(sarray):
        return deepEqArray(st, ap, bp);
    case stTypeId(hashtable):
        return deepEqHashtable(st, ap, bp);
    case stTypeId(struct):
        // an embedded struct is stored inline, so the storage *is* the StructBase
        return structDeepEq((const StructBase*)ap, (const StructBase*)bp);
    case stTypeId(structp): {
        const StructBase* pa = *(const StructBase* const*)ap;
        const StructBase* pb = *(const StructBase* const*)bp;
        return structDeepEq(pa, pb);
    }
    default:
        // scalars, string, object, suid: their own cmp op is already value-based
        return _stCmp(st, stStored(st, ap), stStored(st, bp), 0) == 0;
    }
}

// ---------------------------------------------------------------------------------------

// Same comparison for a single value rather than a whole struct. Exported for the
// serialization tests, whose round trips include bare containers as well as structs.
bool structDeepEqValue(stype st, const void* ap, const void* bp)
{
    return deepEqValue(st, ap, bp);
}

// ---------------------------------------------------------------------------------------

static const StructMemberDesc* findMember(const StructInfo* si, strref name)
{
    for (int i = 0; i < si->nmembers; i++) {
        if (strEq(si->members[i].name, name))
            return &si->members[i];
    }
    return NULL;
}

// Fill a TestStr1 with values distinct enough that a member-shuffling bug shows up
static void fillTestStr1(TestStr1* s, int32 seed)
{
    string tmp = 0;

    s->intval = seed;
    saClear(&s->arrval);
    for (int32 i = 0; i < 3; i++) saPush(&s->arrval, int32, seed * 100 + i);
    strFromInt32(&tmp, seed, 10);
    strPrepend(_S"str-", &tmp);
    strDup(&s->strval, tmp);

    strDestroy(&tmp);
}

// ---------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------

// Compound descriptors are gone: every sarray/hashtable/structp-typed member, regardless of
// what it is declared over, carries the single bare unparameterized token for its type. Type
// identity across two structs declaring "the same" compound member type is therefore trivial
// now -- there is only one descriptor to agree on -- and the parameterization that used to
// live on the runtime type lives entirely on the member's schema instead.
static int test_struct_typeidentity()
{
    const StructMemberDesc *a, *b;

    // sarray[int32], declared by both TestStr1 and TestStrDup: the same bare sarray token
    a = findMember(&TestStr1_structinfo, _S"arrval");
    b = findMember(&TestStrDup_structinfo, _S"arrval");
    CHK(a && b);
    CHK(a->schema->type == stType(sarray) && b->schema->type == stType(sarray));
    CHK(a->schema && a->schema->param[0] && a->schema->param[0]->type == stType(int32));

    // hashtable[string, int32], declared by both TestStr2 and TestStrDup
    a = findMember(&TestStr2_structinfo, _S"tags");
    b = findMember(&TestStrDup_structinfo, _S"tags");
    CHK(a && b);
    CHK(a->schema->type == stType(hashtable) && b->schema->type == stType(hashtable));

    // structp[TestStr1], declared by both TestStrP and TestStrDup
    a = findMember(&TestStrP_structinfo, _S"knownptr");
    b = findMember(&TestStrDup_structinfo, _S"knownptr");
    CHK(a && b);
    CHK(a->schema->type == stType(structp) && b->schema->type == stType(structp));

    // A sarray of structs is the same bare sarray token as a sarray of ints -- the element
    // identity that used to distinguish them lives in the schema now, not the runtime type.
    a = findMember(&TestStr1_structinfo, _S"arrval");
    b = findMember(&TestStr2_structinfo, _S"arrayofstructs");
    CHK(a && b);
    CHK(a->schema->type == b->schema->type);

    // A container built at runtime carries the same bare descriptor as a statically declared
    // member -- there is no separate compound identity left to disagree about.
    sa_int32 arr;
    saInit(&arr, int32, 4);
    CHK(stEq(stType(sarray), findMember(&TestStr1_structinfo, _S"arrval")->schema->type));
    saDestroy(&arr);

    return 0;
}

// hashofdblarrstr is hashtable[string, sarray[sarray[struct[TestStr1]]]], but with compound
// descriptors gone its runtime type is just the bare hashtable token -- the full declared type
// lives in the member's schema now. See test_ser_generated (sertest.c) for the full
// schema-chain assertions; this only checks that the runtime side stayed bare.
static int test_struct_typenesting()
{
    const StructMemberDesc* m = findMember(&TestStr2_structinfo, _S"hashofdblarrstr");
    CHK(m);

    CHK(m->schema->type == stType(hashtable));
    CHK(m->schema && m->schema->type == stType(hashtable));
    CHK(m->schema->param[1] && m->schema->param[1]->type == stType(sarray));

    // The inner sarray[struct[TestStr1]] is the same bare sarray token TestStr2::arrayofstructs
    // declares -- there is no separate compound identity left to distinguish them.
    const StructMemberDesc* aos = findMember(&TestStr2_structinfo, _S"arrayofstructs");
    CHK(aos);
    CHK(aos->schema->type == stType(sarray));

    return 0;
}

// structInit / structDestroyMembers, and the "many" variants, which indexed a StructBase*
// (pointer-sized) rather than stepping by structsize.
static int test_struct_initdestroy()
{
    TestStr1 s;
    structInit(TestStr1, &s);

    CHK(s.structinfo == &TestStr1_structinfo);
    if (s.intval != 0)
        TEST_FAIL(1, _SL("freshly-init s.intval=${int} != 0"), stvar(int32, s.intval));
    CHK(s.strval == NULL);

    fillTestStr1(&s, 1);
    if (s.intval != 1)
        TEST_FAIL(1, _SL("s.intval=${int} != 1"), stvar(int32, s.intval));
    CHK(!strEmpty(s.strval));
    if (saSize(s.arrval) != 3)
        TEST_FAIL(1, _SL("saSize(s.arrval)=${uint} != 3"), stvar(uint64, (uint64)saSize(s.arrval)));

    structDestroyMembers(&s);

    // Four consecutive instances. Before the stride fix these overlapped each other, so
    // every instance past the first was initialized into the middle of its predecessor.
    TestStr1 many[4];
    structInitMany(TestStr1, many, 4);
    for (int i = 0; i < 4; i++) {
        CHK(many[i].structinfo == &TestStr1_structinfo);
        fillTestStr1(&many[i], i + 1);
    }
    // each must still hold its own value, not a neighbour's
    for (int i = 0; i < 4; i++) {
        if (many[i].intval != i + 1)
            TEST_FAIL(1, _SL("many[${int}].intval=${int} != ${int}"), stvar(int32, i), stvar(int32, many[i].intval), stvar(int32, i + 1));
        if (saSize(many[i].arrval) != 3)
            TEST_FAIL(1, _SL("saSize(many[${int}].arrval)=${uint} != 3"), stvar(int32, i), stvar(uint64, (uint64)saSize(many[i].arrval)));
        if (many[i].arrval.a[0] != (i + 1) * 100)
            TEST_FAIL(1, _SL("many[${int}].arrval.a[0]=${int} != ${int}"), stvar(int32, i), stvar(int32, many[i].arrval.a[0]), stvar(int32, (i + 1) * 100));
    }
    structDestroyMembersMany(many, 4);

    // Heap allocation path. structCreate never compiled before _structAlloc was made to
    // take a const StructInfo*, since every generated structinfo is const.
    TestStr1* h = structCreate(TestStr1);
    CHK(h);
    CHK(h->structinfo == &TestStr1_structinfo);
    fillTestStr1(h, 9);
    structDestroy(&h);
    CHK(h == NULL);

    return 0;
}

// Copying the simple fixture. structCopy's memset used to write structsize bytes from each
// member's offset, running off the end of the struct and wiping members already copied.
static int test_struct_copy()
{
    TestStr1 src, dst;
    structInit(TestStr1, &src);
    structInit(TestStr1, &dst);

    fillTestStr1(&src, 7);

    stCopy(TestStr1, &dst, src);

    CHK(dst.structinfo == &TestStr1_structinfo);
    if (dst.intval != 7)
        TEST_FAIL(1, _SL("dst.intval=${int} != 7"), stvar(int32, dst.intval));
    if (!strEq(dst.strval, src.strval))
        TEST_FAIL(1, _SL("copy mismatch: dst.strval='${string}' src.strval='${string}'"), stvar(strref, dst.strval), stvar(strref, src.strval));
    if (saSize(dst.arrval) != 3)
        TEST_FAIL(1, _SL("saSize(dst.arrval)=${uint} != 3"), stvar(uint64, (uint64)saSize(dst.arrval)));
    for (int32 i = 0; i < 3; i++) {
        if (dst.arrval.a[i] != src.arrval.a[i])
            TEST_FAIL(1, _SL("dst.arrval.a[${int}]=${int} != src.arrval.a[${int}]=${int}"), stvar(int32, i), stvar(int32, dst.arrval.a[i]), stvar(int32, i), stvar(int32, src.arrval.a[i]));
    }
    // the copy must own its own array, not alias the source's
    if (dst.arrval.a == src.arrval.a)
        TEST_FAIL(1, _SL("copy aliases source array: dst.arrval.a=${ptr} src.arrval.a=${ptr}"), stvar(ptr, dst.arrval.a), stvar(ptr, src.arrval.a));

    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    // and the deep comparator must actually be able to tell them apart
    dst.intval = 8;
    CHK(!structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));
    dst.intval = 7;
    strDestroy(&dst.strval);
    strDup(&dst.strval, _S"different");
    CHK(!structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// The rich fixture: nested struct, sarray of structs, hashtable, and a hashtable whose
// values are sarray[sarray[struct]].
static int test_struct_nested()
{
    TestStr2 src, dst;
    structInit(TestStr2, &src);
    structInit(TestStr2, &dst);

    strDup(&src.yahoo, _S"top");

    // nested embedded struct -- this never copied at all before, because the destination's
    // structinfo was garbage and stCopy_struct refused what looked like a type mismatch
    fillTestStr1(&src.subst, 5);

    // structInit only zero-fills, so container members start NULL. sarrays self-initialize
    // on first push, but hashtables have to be created with their declared key/value types.
    htInit(&src.tags, string, int32, 4);
    htInsert(&src.tags, string, _S"one", int32, 1);
    htInsert(&src.tags, string, _S"two", int32, 2);

    for (int32 i = 0; i < 3; i++) {
        TestStr1 e;
        structInit(TestStr1, &e);
        fillTestStr1(&e, 10 + i);
        saPush(&src.arrayofstructs, TestStr1, e);
        structDestroyMembers(&e);
    }

    // hashtable[string, sarray[sarray[struct[TestStr1]]]] -- the deepest type the fixtures
    // have, and the one most likely to expose a stride or ownership mistake. Compound
    // descriptors are gone, so these nested containers carry no identity beyond their own
    // (sarray/hashtable) -- only the schema knows what is nested inside.
    {
        htInit(&src.hashofdblarrstr, string, sarray, 2);

        sa_ref outer = { 0 };
        saInit(&outer, sarray, 2);
        for (int32 i = 0; i < 2; i++) {
            sa_ref inner = { 0 };
            saInit(&inner, TestStr1, 2);
            for (int32 j = 0; j < 2; j++) {
                TestStr1 e;
                structInit(TestStr1, &e);
                fillTestStr1(&e, 20 + i * 10 + j);
                saPush(&inner, TestStr1, e);
                structDestroyMembers(&e);
            }
            saPush(&outer, sarray, inner);
            saDestroy(&inner);
        }
        htInsert(&src.hashofdblarrstr, string, _S"deep", sarray, outer);
        saDestroy(&outer);
    }

    for (int32 i = 0; i < 50; i++) src.fixedarr[i] = i * 3;

    stCopy(TestStr2, &dst, src);

    if (!strEq(dst.yahoo, _S"top"))
        TEST_FAIL(1, _SL("dst.yahoo='${string}' != 'top'"), stvar(strref, dst.yahoo));

    // nested struct
    CHK(dst.subst.structinfo == &TestStr1_structinfo);
    if (dst.subst.intval != 5)
        TEST_FAIL(1, _SL("dst.subst.intval=${int} != 5"), stvar(int32, dst.subst.intval));
    if (!strEq(dst.subst.strval, src.subst.strval))
        TEST_FAIL(1, _SL("nested-struct copy mismatch: dst='${string}' src='${string}'"), stvar(strref, dst.subst.strval), stvar(strref, src.subst.strval));
    if (saSize(dst.subst.arrval) != 3)
        TEST_FAIL(1, _SL("saSize(dst.subst.arrval)=${uint} != 3"), stvar(uint64, (uint64)saSize(dst.subst.arrval)));

    // hashtable
    if (htSize(dst.tags) != 2)
        TEST_FAIL(1, _SL("htSize(dst.tags)=${uint} != 2"), stvar(uint64, (uint64)htSize(dst.tags)));
    int32 v = 0;
    CHK(htFind(dst.tags, string, _S"two", int32, &v));
    if (v != 2)
        TEST_FAIL(1, _SL("dst.tags[two]=${int} != 2"), stvar(int32, v));

    // sarray of structs
    if (saSize(dst.arrayofstructs) != 3)
        TEST_FAIL(1, _SL("saSize(dst.arrayofstructs)=${uint} != 3"), stvar(uint64, (uint64)saSize(dst.arrayofstructs)));
    for (int32 i = 0; i < 3; i++) {
        if (dst.arrayofstructs.a[i].intval != 10 + i)
            TEST_FAIL(1, _SL("dst.arrayofstructs.a[${int}].intval=${int} != ${int}"), stvar(int32, i), stvar(int32, dst.arrayofstructs.a[i].intval), stvar(int32, 10 + i));
        if (!strEq(dst.arrayofstructs.a[i].strval, src.arrayofstructs.a[i].strval))
            TEST_FAIL(1, _SL("dst.arrayofstructs.a[${int}].strval='${string}' != src '${string}'"), stvar(int32, i), stvar(strref, dst.arrayofstructs.a[i].strval), stvar(strref, src.arrayofstructs.a[i].strval));
    }

    // deeply nested container
    if (htSize(dst.hashofdblarrstr) != 1)
        TEST_FAIL(1, _SL("htSize(dst.hashofdblarrstr)=${uint} != 1"), stvar(uint64, (uint64)htSize(dst.hashofdblarrstr)));

    // fixedarr is int32[50]; the memset bug made the tail of the struct collateral damage
    for (int32 i = 0; i < 50; i++) {
        if (dst.fixedarr[i] != i * 3)
            TEST_FAIL(1, _SL("dst.fixedarr[${int}]=${int} != ${int}"), stvar(int32, i), stvar(int32, dst.fixedarr[i]), stvar(int32, i * 3));
    }

    // the whole thing, compared field by field
    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// Fixed C array members with managed element types. arrsize was emitted by cxautogen and
// read by nothing, so copy and destroy touched only element 0 -- a leak for every element
// past the first, and a copy that silently dropped them.
static int test_struct_fixedarray()
{
    TestStrFixed src, dst;
    structInit(TestStrFixed, &src);
    structInit(TestStrFixed, &dst);

    for (int i = 0; i < 4; i++) {
        string tmp = 0;
        strFromInt32(&tmp, i, 10);
        strPrepend(_S"name-", &tmp);
        strDup(&src.names[i], tmp);
        strDestroy(&tmp);
    }
    for (int i = 0; i < 3; i++) {
        // structInit reaches into embedded struct members, including every element of a
        // fixed array of them, so these already carry their type
        CHK(src.substs[i].structinfo == &TestStr1_structinfo);
        fillTestStr1(&src.substs[i], 30 + i);
    }
    for (int i = 0; i < 8; i++) src.nums[i] = i * 11;

    stCopy(TestStrFixed, &dst, src);

    // every element must have been copied, not just element 0
    for (int i = 0; i < 4; i++) {
        CHK(!strEmpty(dst.names[i]));
        if (!strEq(dst.names[i], src.names[i]))
            TEST_FAIL(1, _SL("dst.names[${int}]='${string}' != src '${string}'"), stvar(int32, i), stvar(strref, dst.names[i]), stvar(strref, src.names[i]));
    }
    for (int i = 0; i < 3; i++) {
        if (dst.substs[i].intval != 30 + i)
            TEST_FAIL(1, _SL("dst.substs[${int}].intval=${int} != ${int}"), stvar(int32, i), stvar(int32, dst.substs[i].intval), stvar(int32, 30 + i));
        if (!strEq(dst.substs[i].strval, src.substs[i].strval))
            TEST_FAIL(1, _SL("dst.substs[${int}].strval='${string}' != src '${string}'"), stvar(int32, i), stvar(strref, dst.substs[i].strval), stvar(strref, src.substs[i].strval));
        if (saSize(dst.substs[i].arrval) != 3)
            TEST_FAIL(1, _SL("saSize(dst.substs[${int}].arrval)=${uint} != 3"), stvar(int32, i), stvar(uint64, (uint64)saSize(dst.substs[i].arrval)));
        // its own allocation
        if (dst.substs[i].arrval.a == src.substs[i].arrval.a)
            TEST_FAIL(1, _SL("dst.substs[${int}].arrval aliases source: ${ptr}"), stvar(int32, i), stvar(ptr, dst.substs[i].arrval.a));
    }
    for (int i = 0; i < 8; i++) {
        if (dst.nums[i] != i * 11)
            TEST_FAIL(1, _SL("dst.nums[${int}]=${int} != ${int}"), stvar(int32, i), stvar(int32, dst.nums[i]), stvar(int32, i * 11));
    }

    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    // A difference in a non-zero element must be detected. If the comparator only looked at
    // element 0 it would miss this -- the same bug in a different place.
    strDestroy(&dst.names[2]);
    strDup(&dst.names[2], _S"changed");
    CHK(!structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    // destroying must release every element; a leak-checking build catches the rest
    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// structp: heap-allocated struct pointers, including a dynamic one through a StructSet
static int test_struct_structp()
{
    TestStrP src, dst;
    structInit(TestStrP, &src);
    structInit(TestStrP, &dst);

    src.knownptr = structCreate(TestStr1);
    fillTestStr1(src.knownptr, 42);

    src.dynptr = STRUCTBASE(structCreate(TestStr2));
    strDup(&((TestStr2*)src.dynptr)->yahoo, _S"dynamic");

    for (int32 i = 0; i < 2; i++) {
        TestStr1* p = structCreate(TestStr1);
        fillTestStr1(p, 50 + i);
        saPush(&src.arrknown, structp, (StructBase*)p);
        structDestroy(&p);
    }

    stCopy(TestStrP, &dst, src);

    CHK(dst.knownptr);
    // must be a distinct allocation
    if (dst.knownptr == src.knownptr)
        TEST_FAIL(1, _SL("dst.knownptr aliases src.knownptr: ${ptr}"), stvar(ptr, dst.knownptr));
    if (dst.knownptr->intval != 42)
        TEST_FAIL(1, _SL("dst.knownptr->intval=${int} != 42"), stvar(int32, dst.knownptr->intval));
    if (!strEq(dst.knownptr->strval, src.knownptr->strval))
        TEST_FAIL(1, _SL("dst.knownptr->strval='${string}' != src '${string}'"), stvar(strref, dst.knownptr->strval), stvar(strref, src.knownptr->strval));

    CHK(dst.dynptr);
    if (dst.dynptr == src.dynptr)
        TEST_FAIL(1, _SL("dst.dynptr aliases src.dynptr: ${ptr}"), stvar(ptr, dst.dynptr));
    CHK(dst.dynptr->structinfo == &TestStr2_structinfo);
    if (!strEq(((TestStr2*)dst.dynptr)->yahoo, _S"dynamic"))
        TEST_FAIL(1, _SL("dst.dynptr yahoo='${string}' != 'dynamic'"), stvar(strref, ((TestStr2*)dst.dynptr)->yahoo));

    if (saSize(dst.arrknown) != 2)
        TEST_FAIL(1, _SL("saSize(dst.arrknown)=${uint} != 2"), stvar(uint64, (uint64)saSize(dst.arrknown)));
    for (int32 i = 0; i < 2; i++) {
        if (dst.arrknown.a[i] == src.arrknown.a[i])
            TEST_FAIL(1, _SL("dst.arrknown.a[${int}] aliases src: ${ptr}"), stvar(int32, i), stvar(ptr, dst.arrknown.a[i]));
        if (dst.arrknown.a[i]->intval != 50 + i)
            TEST_FAIL(1, _SL("dst.arrknown.a[${int}]->intval=${int} != ${int}"), stvar(int32, i), stvar(int32, dst.arrknown.a[i]->intval), stvar(int32, 50 + i));
    }

    CHK(structDeepEq(STRUCTBASE(&src), STRUCTBASE(&dst)));

    structDestroyMembers(&src);
    structDestroyMembers(&dst);
    return 0;
}

// StructSet lookup, the mechanism a dynamic structp resolves through
static int test_struct_structset()
{
    const StructInfo* si = structSetFind(&TestStrSet_structset, _S"TestStr1");
    CHK(si == &TestStr1_structinfo);

    si = structSetFind(&TestStrSet_structset, _S"TestStr2");
    CHK(si == &TestStr2_structinfo);

    CHK(structSetFind(&TestStrSet_structset, _S"NoSuchStruct") == NULL);
    CHK(structSetFind(&TestStrSet_structset, NULL) == NULL);

    return 0;
}

testfunc structtest_funcs[] = {
    { "typeidentity", test_struct_typeidentity },
    { "typenesting",  test_struct_typenesting  },
    { "initdestroy",  test_struct_initdestroy  },
    { "copy",         test_struct_copy         },
    { "nested",       test_struct_nested       },
    { "fixedarray",   test_struct_fixedarray   },
    { "structp",      test_struct_structp      },
    { "structset",    test_struct_structset    },
    { 0,              0                        }
};
