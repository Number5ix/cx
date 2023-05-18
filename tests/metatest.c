#include <stdio.h>
#include <cx/meta.h>
#include <cx/string.h>

#define TEST_FILE metatest
#define TEST_FUNCS metatest_funcs
#include "common.h"

static int test_meta_wrap()
{
    int ret = 0;

    // volatile because the MSVC optimizer deduced that the result of the loops is in fact
    // predetermined and cannot possibly have any other result, so it reduced the entire function
    // to just "return 0;"
    // While that's a good proof that the logic is correct, volatile makes the test suite actually
    // go through the motions and execute it.
    volatile int tvar1, tvar2;

    // test basic block wrapping
    tvar1 = tvar2 = 0;
    blkWrap(tvar1 = 17, tvar2 = tvar2 / 4)
    {
        tvar1 *= 2;
        tvar2 += 28;
    }
    if (tvar1 != 34 || tvar2 != 7)
        ret = 1;

    tvar1 = tvar2 = 0;
    blkWrap(tvar1 = 17, tvar2 = tvar2 / 4)
    {
        tvar1 *= 2;
        tvar2 += 28;
        tvar1 *= 2;
        tvar2 += 28;
    }
    if (tvar1 == 34 || tvar2 == 7)
        ret = 1;

    // test 'continue' to break out
    tvar1 = tvar2 = 0;
    blkWrap(tvar1 = 17, tvar2 = tvar2 / 4)
    {
        tvar1 *= 2;
        tvar2 += 28;
        continue;
        tvar1 *= 2;
        tvar2 += 28;
    }
    if (tvar1 != 34 || tvar2 != 7)
        ret = 1;

    // test 'break' to break out
    tvar1 = tvar2 = 0;
    blkWrap(tvar1 = 17, tvar2 = tvar2 / 4)
    {
        tvar1 *= 2;
        tvar2 += 28;
        break;
        tvar1 *= 2;
        tvar2 += 28;
    }
    if (tvar1 != 34 || tvar2 != 7)
        ret = 1;

    // nested blocks
    tvar1 = tvar2 = 0;
    blkWrap(tvar1 = 17, tvar2 = tvar2 / 4)
    {
        tvar1 *= 2;
        tvar2 += 28;
        blkWrap(tvar1 += 7, (tvar2 += 16, tvar1++))
        {
            tvar1 -= 32;
            tvar2 *= 2;
            continue;
            tvar1 -= 32;
            tvar2 *= 2;
        }
        break;
        tvar1 *= 2;
        tvar2 += 28;
    }
    if (tvar1 != 10 || tvar2 != 18)
        ret = 1;

    return ret;
}

static int test_meta_pblock_basic(string *pout)
{
    // check basic protected block
    strClear(pout);
    pblock{
        strAppend(pout, _S"A");
PBLOCK_AFTER:
        strAppend(pout, _S"B");
    }

    if (!strEq(*pout, _S"AB"))
        return 1;

    return 0;
}

static int test_meta_pblock_unwind(string *pout)
{
    // basic unwind test
    strClear(pout);
    pblock {
        strAppend(pout, _S"A");
        pblockUnwind(-1);
        strAppend(pout, _S"B");
PBLOCK_AFTER:
        strAppend(pout, _S"C");
    };

    if (!strEq(*pout, _S"AC"))
        return 1;

    return 0;
}

static void test_meta_pblock_unwind_nested(string *pout, int depth, int unwind)
{
    strClear(pout);
    pblock{
        strAppend(pout, _S"A1");
        pblock {
            strAppend(pout, _S"B1");
            pblock {
                strAppend(pout, _S"C1");
                pblock {
                    strAppend(pout, _S"D1");
                    pblock{
                        strAppend(pout, _S"E1");
                        pblock {
                            strAppend(pout, _S"F1");
                            pblock {
                                strAppend(pout, _S"G1");
                                if (depth == 7) pblockUnwind(unwind);
                                strAppend(pout, _S"G2");
                                PBLOCK_AFTER: strAppend(pout, _S"G3");
                            };
                            if (depth == 6) pblockUnwind(unwind);
                            strAppend(pout, _S"F2");
                            PBLOCK_AFTER: strAppend(pout, _S"F3");
                        };
                        if (depth == 5) pblockUnwind(unwind);
                        strAppend(pout, _S"E2");
                        PBLOCK_AFTER: strAppend(pout, _S"E3");
                    };
                    if (depth == 4) pblockUnwind(unwind);
                    strAppend(pout, _S"D2");
                    PBLOCK_AFTER: strAppend(pout, _S"D3");
                };
                if (depth == 3) pblockUnwind(unwind);
                strAppend(pout, _S"C2");
                PBLOCK_AFTER: strAppend(pout, _S"C3");
            };
            if (depth == 2) pblockUnwind(unwind);
            strAppend(pout, _S"B2");
            PBLOCK_AFTER: strAppend(pout, _S"B3");
        };
        if (depth == 1) pblockUnwind(unwind);
        strAppend(pout, _S"A2");
        PBLOCK_AFTER: strAppend(pout, _S"A3");
    };
}

// "A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B2B3A2A3"

static int test_meta_pblock_return_sub(string *pout, int depthf1, int depthf2, int rv)
{
    volatile int rval = rv;

    pblock{
        strAppend(pout, _S"D1");
        pblock{
            strAppend(pout, _S"E1");
            pblock {
                strAppend(pout, _S"F1");
                if (depthf2 == 3) pblockReturn rval * 1000;
                strAppend(pout, _S"F2");
                PBLOCK_AFTER: strAppend(pout, _S"F3");
            };
            if (depthf2 == 2) pblockReturn rval * 1000;
            strAppend(pout, _S"E2");
            PBLOCK_AFTER: strAppend(pout, _S"E3");
        };
        if (depthf2 == 1) pblockReturn rval * 1000;
        strAppend(pout, _S"D2");
        PBLOCK_AFTER: strAppend(pout, _S"D3");
    };

    strAppend(pout, _S"_func2_");
    return 0;
}

static int test_meta_pblock_return(string *pout, int depthf1, int depthf2, int rv)
{
    // For strict adherence to the spec these need to be volatile, since the pblockReturn
    // expression is evaluated after a longjmp back from the outer blocks.
    volatile int rval = rv;
    volatile int ret = 0;

    strClear(pout);
    pblock{
        strAppend(pout, _S"A1");
        pblock {
            strAppend(pout, _S"B1");
            pblock {
                strAppend(pout, _S"C1");
                if (depthf1 == 3) pblockReturn rval + ret + 1;
                // test calling another function that uses pblockReturn from inside a pblock
                ret = test_meta_pblock_return_sub(pout, depthf1, depthf2, rval);
                strAppend(pout, _S"C2");
                PBLOCK_AFTER: strAppend(pout, _S"C3");
            };
            if (depthf1 == 2) pblockReturn rval + ret + 1;
            strAppend(pout, _S"B2");
            PBLOCK_AFTER: strAppend(pout, _S"B3");
        };
        if (depthf1 == 1) pblockReturn rval + ret + 1;
        strAppend(pout, _S"A2");
        PBLOCK_AFTER: strAppend(pout, _S"A3");
    };

    strAppend(pout, _S"_func1_");

    return ret;
}

static int test_meta_protect()
{
    int ret = 0;
    string out = 0;

    ret |= test_meta_pblock_basic(&out);
    ret |= test_meta_pblock_unwind(&out);

    test_meta_pblock_unwind_nested(&out, 0, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 1, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B2B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 2, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 3, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 4, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D3C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 5, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E3D3C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 6, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F3E3D3C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 7, -1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G3F3E3D3C3B3A3")) ret = 1;

    test_meta_pblock_unwind_nested(&out, 0, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 1, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B2B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 2, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 3, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 4, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D3C2C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 5, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E3D2D3C2C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 6, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F3E2E3D2D3C2C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 7, 1);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G3F2F3E2E3D2D3C2C3B2B3A2A3")) ret = 1;


    test_meta_pblock_unwind_nested(&out, 0, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 1, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B2B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 2, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C2C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 3, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D2D3C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 4, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E2E3D3C3B3A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 5, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F2F3E3D3C3B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 6, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G2G3F3E3D3C3B2B3A2A3")) ret = 1;
    test_meta_pblock_unwind_nested(&out, 7, 4);
    if (!strEq(out, _S"A1B1C1D1E1F1G1G3F3E3D3C2C3B2B3A2A3")) ret = 1;

    int rval;
    rval = test_meta_pblock_return(&out, 0, 0, 5);
    if (rval != 0 || !strEq(out, _S"A1B1C1D1E1F1F2F3E2E3D2D3_func2_C2C3B2B3A2A3_func1_")) ret = 1;
    rval = test_meta_pblock_return(&out, 1, 0, 6);
    if (rval != 7 || !strEq(out, _S"A1B1C1D1E1F1F2F3E2E3D2D3_func2_C2C3B2B3A3")) ret = 1;
    rval = test_meta_pblock_return(&out, 2, 0, 7);
    if (rval != 8 || !strEq(out, _S"A1B1C1D1E1F1F2F3E2E3D2D3_func2_C2C3B3A3")) ret = 1;
    rval = test_meta_pblock_return(&out, 3, 0, 8);
    if (rval != 9 || !strEq(out, _S"A1B1C1C3B3A3")) ret = 1;


    rval = test_meta_pblock_return(&out, 0, 1, 9);
    if (rval != 9000 || !strEq(out, _S"A1B1C1D1E1F1F2F3E2E3D3C2C3B2B3A2A3_func1_")) ret = 1;
    rval = test_meta_pblock_return(&out, 0, 2, 10);
    if (rval != 10000 || !strEq(out, _S"A1B1C1D1E1F1F2F3E3D3C2C3B2B3A2A3_func1_")) ret = 1;
    rval = test_meta_pblock_return(&out, 0, 3, 11);
    if (rval != 11000 || !strEq(out, _S"A1B1C1D1E1F1F3E3D3C2C3B2B3A2A3_func1_")) ret = 1;


    rval = test_meta_pblock_return(&out, 2, 1, 12);
    if (rval != 12013 || !strEq(out, _S"A1B1C1D1E1F1F2F3E2E3D3C2C3B3A3")) ret = 1;
    rval = test_meta_pblock_return(&out, 1, 3, 13);
    if (rval != 13014 || !strEq(out, _S"A1B1C1D1E1F1F3E3D3C2C3B2B3A3")) ret = 1;
    rval = test_meta_pblock_return(&out, 3, 3, 14);
    if (rval != 15 || !strEq(out, _S"A1B1C1C3B3A3")) ret = 1;
    rval = test_meta_pblock_return(&out, 2, 1, 15);
    if (rval != 15016 || !strEq(out, _S"A1B1C1D1E1F1F2F3E2E3D3C2C3B3A3")) ret = 1;

    strDestroy(&out);
    return ret;
}

testfunc metatest_funcs[] = {
    { "wrap", test_meta_wrap },
    { "protect", test_meta_protect },
    { 0, 0 }
};
