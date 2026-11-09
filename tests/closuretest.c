#include <stdio.h>
#include <cx/string.h>
#include <cx/closure.h>
#include <cx/format.h>
#include <cx/string/strtest.h>

#define TEST_FILE closuretest
#define TEST_FUNCS closuretest_funcs
#include "common.h"

typedef struct ClosureTestData
{
    int accum;
    int sref1;
    int sref2;
    int count1;
    int count2;
} ClosureTestData;

static ClosureTestData ctd;

static bool ctest1(stvlist *cvars, stvlist *args)
{
    int n1, n2;
    string s1, s2;
    if(!stvlNext(cvars, int32, &n1) ||
       !stvlNext(cvars, string, &s1) ||
       !stvlNext(args, int32, &n2) ||
       !stvlNext(args, string, &s2))
        return false;

    ctd.accum += n1 + n2;
    ctd.sref1 = strTestRefCount(s1);
    ctd.sref2 = strTestRefCount(s2);
    ctd.count1++;

    return true;
}

static bool ctest2(stvlist *cvars, stvlist *args)
{
    int n1, n2;
    string s1, s2;
    if(!stvlNext(cvars, int32, &n1) ||
       !stvlNext(cvars, string, &s1) ||
       !stvlNext(args, int32, &n2) ||
       !stvlNext(args, string, &s2))
        return false;

    if(!strEq(s1, s2))
        return false;

    ctd.accum += n1 + n2;
    ctd.sref1 = strTestRefCount(s1);
    ctd.sref2 = strTestRefCount(s2);
    ctd.count2++;

    return true;
}

static int test_closuretest_closure(void)
{
    int ret = 0;
    memset(&ctd, 0, sizeof(ctd));

    string teststr1 = 0, teststr2 = 0;
    strCopy(&teststr1, _S"Closure Test String 1");
    strCopy(&teststr2, _S"Closure Test String 2");

    int r1, r2, accum = 0;

    r1 = rand(); r2 = rand();
    accum = r1 + r2;
    closure cls = closureCreate(ctest1, stvar(int32, r1), stvar(string, teststr1));
    if(strTestRefCount(teststr1) != 2)
        TEST_FAILV(ret, 1, _SL("teststr1 refcount=${int} after closureCreate (want 2)"), stvar(int32, strTestRefCount(teststr1)));

    if(!closureCall(cls, stvar(int32, r2), stvar(string, teststr2)))
        TEST_FAILV(ret, 1, _SL("closureCall(ctest1) failed"), stvNone);
    if(ctd.count1 != 1 || ctd.sref1 != 2 || ctd.sref2 != 1 || ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after closureCall(ctest1): count1=${int} sref1=${int} sref2=${int} accum=${int} (want 1,2,1,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    closureDestroy(&cls);
    if(strTestRefCount(teststr1) != 1)
        TEST_FAILV(ret, 1, _SL("teststr1 refcount=${int} after closureDestroy (want 1)"), stvar(int32, strTestRefCount(teststr1)));

    r1 = rand(); r2 = rand();
    cls = closureCreate(ctest2, stvar(int32, r1), stvar(string, teststr1));

    // this should fail!
    if(closureCall(cls, stvar(int32, r2), stvar(string, teststr2)))
        TEST_FAILV(ret, 1, _SL("closureCall(ctest2) with mismatched strings unexpectedly succeeded"), stvNone);
    // and should not change any values
    if(ctd.count1 != 1 || ctd.count2 != 0 || ctd.sref1 != 2 || ctd.sref2 != 1 || ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after failed closureCall(ctest2): count1=${int} count2=${int} sref1=${int} sref2=${int} accum=${int} (want 1,0,2,1,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.count2), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    if(!closureCall(cls, stvar(int32, r2), stvar(string, teststr1)))
        TEST_FAILV(ret, 1, _SL("closureCall(ctest2) with matching strings failed"), stvNone);
    accum += r1 + r2;
    if(ctd.count1 != 1 || ctd.count2 != 1 || ctd.sref1 != 2 || ctd.sref2 != 2 || ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after closureCall(ctest2): count1=${int} count2=${int} sref1=${int} sref2=${int} accum=${int} (want 1,1,2,2,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.count2), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    closureDestroy(&cls);
    if(strTestRefCount(teststr1) != 1)
        TEST_FAILV(ret, 1, _SL("teststr1 refcount=${int} after closureDestroy (want 1)"), stvar(int32, strTestRefCount(teststr1)));

    strDestroy(&teststr1);
    strDestroy(&teststr2);

    return ret;
}

static int test_closuretest_chain(void)
{
    int ret = 0;
    memset(&ctd, 0, sizeof(ctd));

    string teststr1 = 0, teststr2 = 0;
    strCopy(&teststr1, _S"Closure Test String 1");
    strCopy(&teststr2, _S"Closure Test String 2");

    int r1, r2, r3, r4, accum = 0;

    r1 = rand(); r2 = rand();
    r3 = rand(); r4 = rand();
    accum = r1 + r2;

    cchain cch = NULL;
    cchainAttach(&cch, ctest1, stvar(int32, r1), stvar(string, teststr1));
    if(strTestRefCount(teststr1) != 2)
        TEST_FAILV(ret, 1, _SL("teststr1 refcount=${int} after cchainAttach (want 2)"), stvar(int32, strTestRefCount(teststr1)));

    if(!cchainCall(&cch, stvar(int32, r2), stvar(string, teststr2)))
        TEST_FAILV(ret, 1, _SL("cchainCall(ctest1) failed"), stvNone);
    if (ctd.count1 != 1 || ctd.sref1 != 2 || ctd.sref2 != 1 || ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after cchainCall(ctest1): count1=${int} sref1=${int} sref2=${int} accum=${int} (want 1,2,1,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    cchainAttachToken(&cch, ctest2, 17, stvar(int32, r3), stvar(string, teststr1));
    if(strTestRefCount(teststr1) != 3)
        TEST_FAILV(ret, 1, _SL("teststr1 refcount=${int} after cchainAttachToken (want 3)"), stvar(int32, strTestRefCount(teststr1)));

    // both functions should be called but the overall result should be false
    // and only ctest1 should modify the state
    accum += r1 + r2;
    if(cchainCall(&cch, stvar(int32, r2), stvar(string, teststr2)))
        TEST_FAILV(ret, 1, _SL("cchainCall() with mismatched strings unexpectedly succeeded overall"), stvNone);
    if (ctd.count1 != 2 || ctd.count2 != 0 || ctd.sref1 != 3 || ctd.sref2 != 1 ||
        ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after mismatched cchainCall(): count1=${int} count2=${int} sref1=${int} sref2=${int} accum=${int} (want 2,0,3,1,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.count2), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    // both functions should be called
    accum += r1 + r2 + r3 + r2;
    if(!cchainCall(&cch, stvar(int32, r2), stvar(string, teststr1)))
        TEST_FAILV(ret, 1, _SL("cchainCall() with matching strings failed"), stvNone);
    if (ctd.count1 != 3 || ctd.count2 != 1 || ctd.sref1 != 3 || ctd.sref2 != 3 ||
        ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after matched cchainCall(): count1=${int} count2=${int} sref1=${int} sref2=${int} accum=${int} (want 3,1,3,3,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.count2), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    // attach a second ctest1
    cchainAttach(&cch, ctest1, stvar(int32, r4), stvar(string, teststr2));
    if(strTestRefCount(teststr2) != 2)
        TEST_FAILV(ret, 1, _SL("teststr2 refcount=${int} after second cchainAttach (want 2)"), stvar(int32, strTestRefCount(teststr2)));

    accum += r1 + r2 + r3 + r2 + r4 + r2;
    if(!cchainCall(&cch, stvar(int32, r2), stvar(string, teststr1)))
        TEST_FAILV(ret, 1, _SL("cchainCall() after second attach failed"), stvNone);
    if (ctd.count1 != 5 || ctd.count2 != 2 || ctd.sref1 != 3 || ctd.sref2 != 3 ||
        ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after second-attach cchainCall(): count1=${int} count2=${int} sref1=${int} sref2=${int} accum=${int} (want 5,2,3,3,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.count2), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    // detach the ctest2 in the middle
    if(!cchainDetach(&cch, ctest2, 17))
        TEST_FAILV(ret, 1, _SL("cchainDetach(ctest2, 17) failed"), stvNone);

    accum += r1 + r2 + r4 + r2;
    if(!cchainCall(&cch, stvar(int32, r2), stvar(string, teststr2)))
        TEST_FAILV(ret, 1, _SL("cchainCall() after detach failed"), stvNone);
    if (ctd.count1 != 7 || ctd.count2 != 2 || ctd.sref1 != 2 || ctd.sref2 != 2 ||
        ctd.accum != accum)
        TEST_FAILV(ret, 1, _SL("after detach cchainCall(): count1=${int} count2=${int} sref1=${int} sref2=${int} accum=${int} (want 7,2,2,2,${int})"), stvar(int32, ctd.count1), stvar(int32, ctd.count2), stvar(int32, ctd.sref1), stvar(int32, ctd.sref2), stvar(int32, ctd.accum), stvar(int32, accum));

    cchainDestroy(&cch);
    if(strTestRefCount(teststr1) != 1 || strTestRefCount(teststr2) != 1)
        TEST_FAILV(ret, 1, _SL("refcounts after cchainDestroy: teststr1=${int} teststr2=${int} (want 1,1)"), stvar(int32, strTestRefCount(teststr1)), stvar(int32, strTestRefCount(teststr2)));

    strDestroy(&teststr1);
    strDestroy(&teststr2);

    return ret;
}

// Each chain-order handler appends its one-digit id here.
static char ctorder[16];
static int ctorderLen;

static void ctorderReset(void)
{
    memset(ctorder, 0, sizeof(ctorder));
    ctorderLen = 0;
}

static void ctorderAdd(int32 id)
{
    if (ctorderLen < (int)sizeof(ctorder) - 1)
        ctorder[ctorderLen++] = (char)('0' + id);
}

#define CTEST_ORDER(what, want)                                                                    \
    if (!strEq((strref)ctorder, _SL(want)))                                                        \
        TEST_FAILV(ret, 1, _SL("${string}: handlers ran as '${string}' (want '${string}')"),       \
                   stvar(strref, _SL(what)), stvar(strref, (strref)ctorder), stvar(strref, _SL(want)))

// Captures: id, return value
static bool ctestOrder(stvlist* cvars, stvlist* args)
{
    ctorderAdd(stvlAt(cvars, 0, int32));
    return stvlAt(cvars, 1, bool);
}

// Handlers run newest first, all of them run even when one returns false, and a detach removes
// only the newest match.
static int test_closuretest_chainorder(void)
{
    int ret   = 0;
    cchain ch = NULL;

    ctorderReset();
    if (!cchainCall(&ch, stvNone))
        TEST_FAILV(ret, 1, _SL("cchainCall on an empty chain returned false"), stvNone);
    CTEST_ORDER("empty chain", "");

    cchainAttach(&ch, ctestOrder, stvar(int32, 1), stvar(bool, true));
    cchainAttachToken(&ch, ctestOrder, 7, stvar(int32, 2), stvar(bool, false));
    cchainAttach(&ch, ctestOrder, stvar(int32, 3), stvar(bool, true));

    ctorderReset();
    if (cchainCall(&ch, stvNone))
        TEST_FAILV(ret, 1, _SL("cchainCall returned true with a handler returning false"), stvNone);
    CTEST_ORDER("three handlers", "321");

    if (!cchainDetach(&ch, ctestOrder, 7))
        TEST_FAILV(ret, 1, _SL("cchainDetach with token 7 found nothing"), stvNone);
    ctorderReset();
    if (!cchainCall(&ch, stvNone))
        TEST_FAILV(ret, 1, _SL("cchainCall returned false after detaching the false handler"), stvNone);
    CTEST_ORDER("after token detach", "31");

    if (!cchainDetach(&ch, ctestOrder, 0))
        TEST_FAILV(ret, 1, _SL("cchainDetach with no token found nothing"), stvNone);
    ctorderReset();
    cchainCall(&ch, stvNone);
    CTEST_ORDER("after tokenless detach", "1");

    cchainDestroy(&ch);
    return ret;
}

// Captures: id, chain, whether it has run
static bool ctestEditDuringCall(stvlist* cvars, stvlist* args)
{
    ctorderAdd(stvlAt(cvars, 0, int32));
    cchain* ch = stvlAtPtr(cvars, 1);
    bool* ran  = stvlAtPtr(cvars, 2);

    if (!*ran) {
        *ran = true;
        cchainAttach(ch, ctestOrder, stvar(int32, 9), stvar(bool, true));
        cchainDetach(ch, ctestOrder, 5);
    }
    return true;
}

// A call runs the handlers that were attached when it started. Attaching or detaching from inside
// a handler takes effect from the next call.
static int test_closuretest_chainsnapshot(void)
{
    int ret   = 0;
    cchain ch = NULL;
    bool ran  = false;

    cchainAttachToken(&ch, ctestOrder, 5, stvar(int32, 2), stvar(bool, true));
    cchainAttach(&ch, ctestEditDuringCall, stvar(int32, 1), stvar(ptr, &ch), stvar(ptr, &ran));

    ctorderReset();
    cchainCall(&ch, stvNone);
    CTEST_ORDER("call that edits the chain", "12");

    ctorderReset();
    cchainCall(&ch, stvNone);
    CTEST_ORDER("next call", "91");

    cchainDestroy(&ch);
    return ret;
}

// Captures: id, chain, whether it has run, cchainAttach result out
static bool ctestCallOnceDuringCall(stvlist* cvars, stvlist* args)
{
    ctorderAdd(stvlAt(cvars, 0, int32));
    cchain* ch     = stvlAtPtr(cvars, 1);
    bool* ran      = stvlAtPtr(cvars, 2);
    bool* reattach = stvlAtPtr(cvars, 3);

    if (!*ran) {
        *ran = true;
        cchainCallOnce(ch, stvNone);
    } else {
        // inside cchainCallOnce: the chain is already consumed
        *reattach = cchainAttach(ch, ctestOrder, stvar(int32, 9), stvar(bool, true));
    }
    return true;
}

// cchainCallOnce from inside a cchainCall on the same chain must leave the outer call's handlers
// and their captures intact until it finishes.
static int test_closuretest_chainonce(void)
{
    int ret       = 0;
    cchain ch     = NULL;
    bool ran      = false;
    bool reattach = true;
    string str    = 0;
    strCopy(&str, _S"captured");

    cchainAttach(&ch, ctestOrder, stvar(int32, 2), stvar(bool, true), stvar(string, str));
    cchainAttach(&ch,
                 ctestCallOnceDuringCall,
                 stvar(int32, 1),
                 stvar(ptr, &ch),
                 stvar(ptr, &ran),
                 stvar(ptr, &reattach));

    ctorderReset();
    if (!cchainCall(&ch, stvNone))
        TEST_FAILV(ret, 1, _SL("outer cchainCall returned false"), stvNone);
    CTEST_ORDER("cchainCallOnce inside cchainCall", "1122");
    if (reattach)
        TEST_FAILV(ret, 1, _SL("cchainAttach succeeded inside cchainCallOnce"), stvNone);
    if (strTestRefCount(str) != 1)
        TEST_FAILV(ret, 1, _SL("capture refcount=${int} after cchainCallOnce (want 1)"),
                   stvar(int32, strTestRefCount(str)));

    if (cchainAttach(&ch, ctestOrder, stvar(int32, 3), stvar(bool, true)))
        TEST_FAILV(ret, 1, _SL("cchainAttach succeeded after cchainCallOnce"), stvNone);
    if (cchainCall(&ch, stvNone))
        TEST_FAILV(ret, 1, _SL("cchainCall returned true on a consumed chain"), stvNone);
    if (!cchainReset(&ch))
        TEST_FAILV(ret, 1, _SL("cchainReset failed on a consumed chain"), stvNone);
    if (!cchainAttach(&ch, ctestOrder, stvar(int32, 3), stvar(bool, true)))
        TEST_FAILV(ret, 1, _SL("cchainAttach failed after cchainReset"), stvNone);

    cchainDestroy(&ch);
    strDestroy(&str);
    return ret;
}

static int test_closuretest_grp_chain(void)
{
    TEST_CHAIN(test_closuretest_chain,
               test_closuretest_chainorder,
               test_closuretest_chainsnapshot,
               test_closuretest_chainonce);
}

// A typed signature: non-bool return and raw buffer arguments, neither of which a generic closure
// can express.
typedef size_t (*ClosureTestFill)(stvlist* cvars, uint8* buf, size_t sz);

static size_t ctestFill(stvlist* cvars, uint8* buf, size_t sz)
{
    uint8 fill    = stvlAt(cvars, 0, uint8);
    strref prefix = stvlAt(cvars, 1, strref);

    size_t plen = min((size_t)strLen(prefix), sz);
    strCopyRaw(prefix, 0, buf, (uint32)plen);
    for (size_t i = plen; i < sz; i++)
        buf[i] = fill;
    return sz - plen;
}

static int test_closuretest_typed(void)
{
    int ret        = 0;
    string prefix = 0;
    strCopy(&prefix, _S"ab");

    closure cls = closureCreateAs(ClosureTestFill, ctestFill, stvar(uint8, 'x'), stvar(string, prefix));
    if (strTestRefCount(prefix) != 2)
        TEST_FAILV(ret, 1, _SL("prefix refcount=${int} after closureCreateAs (want 2)"),
                   stvar(int32, strTestRefCount(prefix)));

    uint8 buf[6]  = { 0 };
    size_t filled = closureCallAs(ClosureTestFill, cls, buf, sizeof(buf));
    if (filled != 4 || memcmp(buf, "abxxxx", 6) != 0)
        TEST_FAILV(ret, 1, _SL("closureCallAs filled=${uint} buf=${int},${int},${int} (want 4, 'abx...')"),
                   stvar(uint64, (uint64)filled), stvar(int32, buf[0]), stvar(int32, buf[1]),
                   stvar(int32, buf[2]));

    // the clone is independent: it holds its own reference, and outlives the original
    closure clone = closureClone(cls);
    if (strTestRefCount(prefix) != 3)
        TEST_FAILV(ret, 1, _SL("prefix refcount=${int} after closureClone (want 3)"),
                   stvar(int32, strTestRefCount(prefix)));

    closureDestroy(&cls);
    if (cls || strTestRefCount(prefix) != 2)
        TEST_FAILV(ret, 1, _SL("after destroying the original: cls=${ptr} prefix refcount=${int} (want NULL, 2)"),
                   stvar(ptr, cls), stvar(int32, strTestRefCount(prefix)));

    memset(buf, 0, sizeof(buf));
    filled = closureCallAs(ClosureTestFill, clone, buf, 3);
    if (filled != 1 || memcmp(buf, "abx", 3) != 0)
        TEST_FAILV(ret, 1, _SL("clone closureCallAs filled=${uint} (want 1)"), stvar(uint64, (uint64)filled));

    closureDestroy(&clone);
    if (strTestRefCount(prefix) != 1)
        TEST_FAILV(ret, 1, _SL("prefix refcount=${int} after destroying the clone (want 1)"),
                   stvar(int32, strTestRefCount(prefix)));

    // destroying an unset closure, or through a NULL handle, is harmless
    closureDestroy(&clone);
    closureDestroy(NULL);
    if (closureClone(NULL) != NULL)
        TEST_FAIL(1, _SL("closureClone(NULL) returned non-NULL"), stvNone);

    strDestroy(&prefix);
    return ret;
}

typedef struct ClosureTestDestroy {
    int runs;
    bool captureintact;
} ClosureTestDestroy;

static void ctestDestroy(stvlist* cvars)
{
    ClosureTestDestroy* td = stvlAtPtr(cvars, 0);
    td->runs++;
    td->captureintact = strEq(stvlAt(cvars, 1, strref), _SL("still here"));
}

static bool ctestNoop(stvlist* cvars, stvlist* args)
{
    return true;
}

// A destroy function runs exactly once, from closureDestroy(), while the captures can still be read.
static int test_closuretest_destroy(void)
{
    int ret                = 0;
    ClosureTestDestroy td  = { 0 };
    string str             = 0;
    strCopy(&str, _S"still here");

    closure cls = closureCreate(ctestNoop, stvar(ptr, &td), stvar(string, str));
    closureSetDestroy(cls, ctestDestroy);

    closureCall(cls, stvNone);
    if (td.runs != 0)
        TEST_FAILV(ret, 1, _SL("destroy function ran ${int} times before closureDestroy"), stvar(int32, td.runs));

    closureDestroy(&cls);
    closureDestroy(&cls);   // already gone: must not run it again
    if (td.runs != 1 || !td.captureintact)
        TEST_FAILV(ret, 1, _SL("destroy runs=${int} captureintact=${bool} (want 1, true)"),
                   stvar(int32, td.runs), stvar(bool, td.captureintact));
    if (strTestRefCount(str) != 1)
        TEST_FAILV(ret, 1, _SL("capture refcount=${int} after destroy (want 1)"), stvar(int32, strTestRefCount(str)));

    strDestroy(&str);
    return ret;
}

static int ctestEvalCalls;

static int32 ctestEvalNext(void)
{
    return ++ctestEvalCalls;
}

typedef int32 (*ClosureTestEval)(stvlist* cvars, int32 n);

static int32 ctestEvalTyped(stvlist* cvars, int32 n)
{
    return n;
}

// Runs one macro call whose stvar arguments are x++ and ctestEvalNext(), then checks that each
// was evaluated exactly once.
#define CTEST_EVAL_ONCE(name, call)                                                             \
    do {                                                                                        \
        x              = 0;                                                                     \
        ctestEvalCalls = 0;                                                                     \
        call;                                                                                   \
        if (x != 1 || ctestEvalCalls != 1)                                                      \
            TEST_FAILV(ret, 1, _SL("${string}: x=${int} calls=${int} (want 1, 1)"),             \
                       stvar(strref, _SL(name)), stvar(int32, x), stvar(int32, ctestEvalCalls)); \
    } while (0)

// Macros that take an stvar list also check at compile time that the list isn't empty, using a
// sizeof over the same arguments. That operand must never be evaluated: every argument's side
// effects have to happen exactly once.
static int test_closuretest_argseval(void)
{
    int ret     = 0;
    int x       = 0;
    closure cls = NULL;
    cchain ch   = NULL;
    string out  = 0;

    CTEST_EVAL_ONCE("closureCreate",
                    cls = closureCreate(ctestNoop, stvar(int32, x++), stvar(int32, ctestEvalNext())));
    closureDestroy(&cls);

    CTEST_EVAL_ONCE("closureCreateAs",
                    cls = closureCreateAs(ClosureTestEval, ctestEvalTyped, stvar(int32, x++),
                                          stvar(int32, ctestEvalNext())));
    closureDestroy(&cls);

    cls = closureCreate(ctestNoop, stvNone);
    CTEST_EVAL_ONCE("closureCall", closureCall(cls, stvar(int32, x++), stvar(int32, ctestEvalNext())));
    closureDestroy(&cls);

    CTEST_EVAL_ONCE("cchainAttach",
                    cchainAttach(&ch, ctestNoop, stvar(int32, x++), stvar(int32, ctestEvalNext())));
    CTEST_EVAL_ONCE("cchainAttachToken",
                    cchainAttachToken(&ch, ctestNoop, 1, stvar(int32, x++), stvar(int32, ctestEvalNext())));
    CTEST_EVAL_ONCE("cchainCall", cchainCall(&ch, stvar(int32, x++), stvar(int32, ctestEvalNext())));
    CTEST_EVAL_ONCE("cchainCallOnce", cchainCallOnce(&ch, stvar(int32, x++), stvar(int32, ctestEvalNext())));
    cchainDestroy(&ch);

    // the values that arrive must be the ones from the single evaluation
    CTEST_EVAL_ONCE("strFormat",
                    strFormat(&out, _S"${int} ${int}", stvar(int32, x++), stvar(int32, ctestEvalNext())));
    if (!strEq(out, _SL("0 1")))
        TEST_FAILV(ret, 1, _SL("strFormat produced '${string}' (want '0 1')"), stvar(string, out));

    strDestroy(&out);
    return ret;
}

testfunc closuretest_funcs[] = {
    { "closure", test_closuretest_closure },
    { "chain", test_closuretest_chain },
    { "chainorder", test_closuretest_chainorder },
    { "chainsnapshot", test_closuretest_chainsnapshot },
    { "chainonce", test_closuretest_chainonce },
    { "grp_chain", test_closuretest_grp_chain },
    { "typed", test_closuretest_typed },
    { "destroy", test_closuretest_destroy },
    { "argseval", test_closuretest_argseval },
    { 0, 0 }
};
