#include <cx/string.h>

#define TEST_FILE harnesstest
#define TEST_FUNCS harnesstest_funcs
#include "common.h"

// Bit n is set when helper n runs, so a chain test can tell which members actually ran.
static uint32 harnessRan;

static int harnessPass1(void)
{
    harnessRan |= 1;
    return 0;
}

static int harnessPass2(void)
{
    harnessRan |= 2;
    return 0;
}

static int harnessSkip(void)
{
    harnessRan |= 4;
    TEST_SKIP(_SL("skipped on purpose"), stvNone);
}

static int harnessFail(void)
{
    harnessRan |= 8;
    return 5;
}

static int harnessChainPass(void)
{
    TEST_CHAIN(harnessPass1, harnessPass2);
}

static int harnessChainSkipMiddle(void)
{
    TEST_CHAIN(harnessPass1, harnessSkip, harnessPass2);
}

static int harnessChainSkipThenFail(void)
{
    TEST_CHAIN(harnessSkip, harnessFail, harnessPass2);
}

static int harnessChainFailThenSkip(void)
{
    TEST_CHAIN(harnessFail, harnessSkip);
}

static int test_harness_skip(void)
{
    harnessRan = 0;
    int res = harnessSkip();
    if (res != CX_TEST_SKIP || harnessRan != 4)
        TEST_FAIL(1, _SL("TEST_SKIP returned ${int}, ran ${uint} (want ${int}, 4)"),
                  stvar(int32, res), stvar(uint32, harnessRan), stvar(int32, CX_TEST_SKIP));
    return 0;
}

static int test_harness_chain(void)
{
    static const struct {
        const char *name;
        int (*fn)(void);
        int want;
        uint32 wantRan;
    } cases[] = {
        { "all pass", harnessChainPass, 0, 1 | 2 },
        { "skip in the middle", harnessChainSkipMiddle, CX_TEST_SKIP, 1 | 2 | 4 },
        { "skip then fail", harnessChainSkipThenFail, 5, 4 | 8 },
        { "fail then skip", harnessChainFailThenSkip, 5, 8 },
    };

    int ret = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        harnessRan = 0;
        int res = cases[i].fn();
        if (res != cases[i].want || harnessRan != cases[i].wantRan)
            TEST_FAILV(ret, 1, _SL("${string}: returned ${int}, ran ${uint} (want ${int}, ${uint})"),
                       stvar(strref, (strref)cases[i].name), stvar(int32, res), stvar(uint32, harnessRan),
                       stvar(int32, cases[i].want), stvar(uint32, cases[i].wantRan));
    }
    return ret;
}

testfunc harnesstest_funcs[] = {
    { "skip", test_harness_skip },
    { "chain", test_harness_chain },
    { 0, 0 }
};
