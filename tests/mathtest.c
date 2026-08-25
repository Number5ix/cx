#include <stdio.h>
#include <cx/math.h>
#include <cx/string.h>

#define TEST_FILE mathtest
#define TEST_FUNCS mathtest_funcs
#include "common.h"

static int test_math_pcgint()
{
    PcgState rng;
    pcgAutoSeed(&rng);
    uint32 r;
    int32 sr;
    uint64 v;
    int64 sv;

    // generate a bunch of random numbers and verify they fall in the expected range

    for (int loop = 0; loop < 10000; loop++) {
        for (uint32 i = 0; i < 2147483647; i += (i >> 2) + 1) {
            r = pcgBounded(&rng, i);
            if (r >= i && i != 0)
                TEST_FAIL(1, _SL("pcgBounded(${uint}) returned ${uint}, out of range"), stvar(uint32, i), stvar(uint32, r));
        }

        for (uint32 i = 0; i < 2147483647; i += (i >> 2) + 1) {
            r = pcgRange(&rng, (i >> 2), i);
            if (r < (i >> 2) || r > i)
                TEST_FAIL(1, _SL("pcgRange(${uint},${uint}) returned ${uint}, out of range"), stvar(uint32, i >> 2), stvar(uint32, i), stvar(uint32, r));
        }

        for (int32 i = 0; i < 1073741823; i += (i >> 2) + 1) {
            sr = pcgSRange(&rng, -i, i);
            if (sr < -i || sr > i)
                TEST_FAIL(1, _SL("pcgSRange(${int},${int}) returned ${int}, out of range"), stvar(int32, -i), stvar(int32, i), stvar(int32, sr));
        }

        for (uint64 i = 0; i < 9223372036854775807ULL; i += (i >> 2) + 1) {
            v = pcgBounded64(&rng, i);
            if (v >= i && v != 0)
                TEST_FAIL(1, _SL("pcgBounded64(${uint}) returned ${uint}, out of range"), stvar(uint64, i), stvar(uint64, v));
        }

        for (uint64 i = 0; i < 9223372036854775807ULL; i += (i >> 2) + 1) {
            v = pcgRange64(&rng, (i >> 2), i);
            if (v < (i >> 2) || v > i)
                TEST_FAIL(1, _SL("pcgRange64(${uint},${uint}) returned ${uint}, out of range"), stvar(uint64, i >> 2), stvar(uint64, i), stvar(uint64, v));
        }

        for (int64 i = 0; i < 4611686018427387903LL; i += (i >> 2) + 1) {
            sv = pcgSRange64(&rng, (i >> 2), i);
            if (sv < (i >> 2) || sv > i)
                TEST_FAIL(1, _SL("pcgSRange64(${int},${int}) returned ${int}, out of range"), stvar(int64, i >> 2), stvar(int64, i), stvar(int64, sv));
        }
    }

    // check that range actually returns everything
    // count should never reach anywhere remotely close to 10000,
    // it's an extreme limit to keep it from running forever if generation is broken

    uint32 seen = 0;
    int count = 0;

    for (count = 0; seen != 0x3ff && count < 10000; count++) {
        r = pcgBounded(&rng, 10);
        if (r >= 10)
            TEST_FAIL(1, _SL("pcgBounded(10) returned ${uint}, out of range"), stvar(uint32, r));

        seen |= 1 << r;
    }

    if (seen != 0x3ff)
        TEST_FAIL(1, _SL("pcgBounded(10) did not cover all values after ${int} draws: seen=${uint}"), stvar(int32, count), stvar(uint32, seen));

    seen = 0;

    for (count = 0; seen != 0x3ff && count < 10000; count++) {
        r = pcgRange(&rng, 31, 40);
        if (r < 31 || r > 40)
            TEST_FAIL(1, _SL("pcgRange(31,40) returned ${uint}, out of range"), stvar(uint32, r));

        seen |= 1 << (r-31);
    }

    if (seen != 0x3ff)
        TEST_FAIL(1, _SL("pcgRange(31,40) did not cover all values after ${int} draws: seen=${uint}"), stvar(int32, count), stvar(uint32, seen));

    return 0;
}

static int test_math_pcgfloat()
{
    PcgState rng;
    pcgAutoSeed(&rng);

    float32 r;
    float64 v;

    for (int loop = 0; loop < 10000; loop++) {
        for (float32 i = 0; i < 1e+20; i += i/2 + 1) {
            r = pcgFRange(&rng, -i, i);
            if (r < -i || r > i)
                TEST_FAIL(1, _SL("pcgFRange(${float},${float}) returned ${float}, out of range"), stvar(float64, (float64)-i), stvar(float64, (float64)i), stvar(float64, (float64)r));
        }

        //NOLINTNEXTLINE
        for (float64 i = 0; i < 1e+40; i += i/2 + 1) {
            v = pcgFRange64(&rng, -i, i);
            if (v < -i || v > i)
                TEST_FAIL(1, _SL("pcgFRange64(${float},${float}) returned ${float}, out of range"), stvar(float64, -i), stvar(float64, i), stvar(float64, v));
        }
    }

    // check floating point range
    uint32 seen = 0;
    int count = 0;
    uint32 bit = 0;

    for (count = 0; seen != 0xffffffff && count < 100000; count++) {
        r = pcgFRange(&rng, 7, 10.1f);
        bit = (uint32)((r - 7) * 10 + 0.5);
        if (bit > 31)
            TEST_FAIL(1, _SL("pcgFRange(7,10.1) returned ${float}, bit=${uint} out of range"), stvar(float64, (float64)r), stvar(uint32, bit));

        seen |= (1 << bit);
    }

    if (seen != 0xffffffff)
        TEST_FAIL(1, _SL("pcgFRange(7,10.1) did not cover all bits after ${int} draws: seen=${uint}"), stvar(int32, count), stvar(uint32, seen));
    seen = 0;

    for (count = 0; seen != 0xffffffff && count < 100000; count++) {
        v = pcgFRange64(&rng, -18.1, -15);
        bit = (uint32)((-v - 15) * 10 + 0.5);
        if (bit > 31)
            TEST_FAIL(1, _SL("pcgFRange64(-18.1,-15) returned ${float}, bit=${uint} out of range"), stvar(float64, v), stvar(uint32, bit));

        seen |= (1 << bit);
    }

    if (seen != 0xffffffff)
        TEST_FAIL(1, _SL("pcgFRange64(-18.1,-15) did not cover all bits after ${int} draws: seen=${uint}"), stvar(int32, count), stvar(uint32, seen));

    return 0;
}

static int test_math_pcgerror()
{
    PcgState rng;
    pcgAutoSeed(&rng);

    // try a lot of things that should not work and verify they return
    // expected results

    uint32 ur;
    uint64 uv;
    int32 sr;
    float32 fr;
    float64 fv;

    if ((ur = pcgBounded(&rng, 0)) != 0)
        TEST_FAIL(1, _SL("pcgBounded(0) returned ${uint} (want 0)"), stvar(uint32, ur));

    if ((uv = pcgBounded64(&rng, 0)) != 0)
        TEST_FAIL(1, _SL("pcgBounded64(0) returned ${uint} (want 0)"), stvar(uint64, uv));

    if ((ur = pcgRange(&rng, 5, 5)) != 5)
        TEST_FAIL(1, _SL("pcgRange(5,5) returned ${uint} (want 5)"), stvar(uint32, ur));

    if ((ur = pcgRange(&rng, 9, 5)) != 9)
        TEST_FAIL(1, _SL("pcgRange(9,5) (inverted bounds) returned ${uint} (want 9)"), stvar(uint32, ur));

    if ((uv = pcgRange64(&rng, 9000000000000LL, 5000000000000LL)) != 9000000000000LL)
        TEST_FAIL(1, _SL("pcgRange64(9000000000000,5000000000000) (inverted bounds) returned ${uint} (want 9000000000000)"), stvar(uint64, uv));

    if ((sr = pcgSRange(&rng, -50, -90)) != -50)
        TEST_FAIL(1, _SL("pcgSRange(-50,-90) (inverted bounds) returned ${int} (want -50)"), stvar(int32, sr));

    if ((fr = pcgFRange(&rng, 401, 5)) != 401)
        TEST_FAIL(1, _SL("pcgFRange(401,5) (inverted bounds) returned ${float} (want 401)"), stvar(float64, (float64)fr));

    if ((fv = pcgFRange64(&rng, -4029413, -9999999999)) != -4029413)
        TEST_FAIL(1, _SL("pcgFRange64(-4029413,-9999999999) (inverted bounds) returned ${float} (want -4029413)"), stvar(float64, fv));

    return 0;
}

static int test_math_floatcmp()
{
    // these two numbers are only 1 ULP apart
    float32 float1 = 80087352;
    float32 float2 = 80087360;

    // IEEE-754 sanity check
    if (float1 == float2)
        TEST_FAIL(1, _SL("float1=${float} == float2=${float}, expected 1-ULP values to differ"), stvar(float64, (float64)float1), stvar(float64, (float64)float2));

    intptr cr;
    if ((cr = stCmp(float32, float1, float2)) != 0)
        TEST_FAIL(1, _SL("stCmp(float32, ${float}, ${float}) = ${int} (want 0)"), stvar(float64, (float64)float1), stvar(float64, (float64)float2), stvar(int32, (int32)cr));

    float64 float3 = 0.865887489;
    float64 float4 = 8.65887489000000121208699965791E-1;
    float64 float5 = 0.865887488;
    float64 float6 = 0.865887490;

    if (float1 == float2)
        TEST_FAIL(1, _SL("float1=${float} == float2=${float}, expected 1-ULP values to differ"), stvar(float64, (float64)float1), stvar(float64, (float64)float2));

    if ((cr = stCmp(float64, float3, float4)) != 0)
        TEST_FAIL(1, _SL("stCmp(float64, ${float}, ${float}) = ${int} (want 0)"), stvar(float64, float3), stvar(float64, float4), stvar(int32, (int32)cr));

    if ((cr = stCmp(float64, float3, float5)) != 1)
        TEST_FAIL(1, _SL("stCmp(float64, ${float}, ${float}) = ${int} (want 1)"), stvar(float64, float3), stvar(float64, float5), stvar(int32, (int32)cr));

    if ((cr = stCmp(float64, float3, float6)) != -1)
        TEST_FAIL(1, _SL("stCmp(float64, ${float}, ${float}) = ${int} (want -1)"), stvar(float64, float3), stvar(float64, float6), stvar(int32, (int32)cr));

    float32 fz1 = 0.0;
    float32 fz2 = -0.0;
    float32 fnz1 = 1.40129846432e-45f;
    float32 fnz2 = -1.40129846432e-45f;

    // 0 should equal -0
    if ((cr = stCmp(float32, fz1, fz2)) != 0)
        TEST_FAIL(1, _SL("stCmp(float32, 0, -0) = ${int} (want 0)"), stvar(int32, (int32)cr));

    // these are close enough to zero they should be considered equivalent
    if ((cr = stCmp(float32, fz1, fnz1)) != 0)
        TEST_FAIL(1, _SL("stCmp(float32, 0, ${float}) = ${int} (want 0)"), stvar(float64, (float64)fnz1), stvar(int32, (int32)cr));
    if ((cr = stCmp(float32, fz2, fnz2)) != 0)
        TEST_FAIL(1, _SL("stCmp(float32, -0, ${float}) = ${int} (want 0)"), stvar(float64, (float64)fnz2), stvar(int32, (int32)cr));

    // but differ by sign even though they're within the threshold
    if ((cr = stCmp(float32, fnz1, fnz2)) != 1)
        TEST_FAIL(1, _SL("stCmp(float32, ${float}, ${float}) = ${int} (want 1)"), stvar(float64, (float64)fnz1), stvar(float64, (float64)fnz2), stvar(int32, (int32)cr));
    if ((cr = stCmp(float32, fnz2, fnz1)) != -1)
        TEST_FAIL(1, _SL("stCmp(float32, ${float}, ${float}) = ${int} (want -1)"), stvar(float64, (float64)fnz2), stvar(float64, (float64)fnz1), stvar(int32, (int32)cr));

    if ((cr = stCmp(float32, fz2, fnz1)) != -1)
        TEST_FAIL(1, _SL("stCmp(float32, -0, ${float}) = ${int} (want -1)"), stvar(float64, (float64)fnz1), stvar(int32, (int32)cr));
    if ((cr = stCmp(float32, fz1, fnz2)) != 1)
        TEST_FAIL(1, _SL("stCmp(float32, 0, ${float}) = ${int} (want 1)"), stvar(float64, (float64)fnz2), stvar(int32, (int32)cr));

    return 0;
}

testfunc mathtest_funcs[] = {
    { "pcgint", test_math_pcgint },
    { "pcgfloat", test_math_pcgfloat },
    { "pcgerror", test_math_pcgerror },
    { "floatcmp", test_math_floatcmp },
    { 0, 0 }
};
