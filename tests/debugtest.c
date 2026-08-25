#include <cx/debug.h>
#include <string.h>

#define TEST_FILE debugtest
#define TEST_FUNCS debugtest_funcs
#include "common.h"

static int assertcount = 0;
static int asserttest = 0;
static bool assertfail = false;

static int assertCb(const char *expr, const char *msg, const char *file, int line)
{
    assertcount++;

    switch (asserttest) {
    case 0:
        if (strcmp(expr, "0 == 1") != 0 || msg != NULL)
            TEST_FAILV(assertfail, true, _SL("case 0: expr='${string}' msg=${ptr} (want expr='0 == 1', msg=NULL)"), stvar(strref, (strref)expr), stvar(ptr, (void*)msg));
        break;
    case 1:
        if (strcmp(expr, "0 == 1") != 0 || strcmp(msg, "Test 1-2-3") != 0)
            TEST_FAILV(assertfail, true, _SL("case 1: expr='${string}' msg='${string}' (want expr='0 == 1', msg='Test 1-2-3')"), stvar(strref, (strref)expr), stvar(strref, (strref)msg));
        break;
    case 2:
        // we should never get 2 because it's a true expression
        TEST_FAILV(assertfail, true, _SL("case 2: assertCb() called for a true expression '${string}'"), stvar(strref, (strref)expr));
        break;
    case 3:
        if (strcmp(expr, "false") != 0 || strcmp(msg, "Dev Only") != 0)
            TEST_FAILV(assertfail, true, _SL("case 3: expr='${string}' msg='${string}' (want expr='false', msg='Dev Only')"), stvar(strref, (strref)expr), stvar(strref, (strref)msg));
        break;
    case 4:
        if (expr != NULL || strcmp(msg, "OH NO!") != 0)
            TEST_FAILV(assertfail, true, _SL("case 4: expr=${ptr} msg='${string}' (want expr=NULL, msg='OH NO!')"), stvar(ptr, (void*)expr), stvar(strref, (strref)msg));
        break;
    }

    return ASSERT_Ignore;
}

static int test_assert()
{
    int ret = 0;
    int lasttest = 0;

    // install assertion handler so these don't all crash
    dbgAssertAddCallback(assertCb);

    relAssert(0 == 1);
    asserttest++;
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after relAssert(0==1): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));

    relAssertMsg(0 == 1, "Test 1-2-3");
    asserttest++;
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after relAssertMsg(0==1): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));

    lasttest = asserttest;
    relAssert(5 == 5);
    asserttest++;
    if (assertfail || assertcount != lasttest)
        TEST_FAILV(ret, 1, _SL("after relAssert(5==5): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, lasttest));
    assertcount = asserttest;           // resync

    if (devVerifyMsg(false, "Dev Only"))
        TEST_FAILV(ret, 1, _SL("devVerifyMsg(false, 'Dev Only') returned true"), stvNone);

    asserttest++;
#if DEBUG_LEVEL > 0
    // Should have asserted in dev / debug builds
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after devVerifyMsg (dev build): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));
#else
    // Should NOT have asserted in release builds
    if (assertfail || assertcount != asserttest - 1)
        TEST_FAILV(ret, 1, _SL("after devVerifyMsg (release build): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest - 1));
    assertcount++;      // pretend it did
#endif

    relFatalError("OH NO!");
    asserttest++;
    if (assertfail || assertcount != asserttest)
        TEST_FAILV(ret, 1, _SL("after relFatalError('OH NO!'): assertfail=${int} assertcount=${int} (want false, ${int})"), stvar(int32, (int32)assertfail), stvar(int32, assertcount), stvar(int32, asserttest));

    return ret;
}

testfunc debugtest_funcs[] = {
    { "assert", test_assert },
    { 0, 0 }
};
