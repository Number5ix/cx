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
        for (uint32 i = 0; i + (i >> 2) + 1 > i; i += (i >> 2) + 1) {
            r = pcgBounded(&rng, i);
            if (r >= i && i != 0)
                return 1;
        }

        for (uint32 i = 0; i + (i >> 2) + 1 > i; i += (i >> 2) + 1) {
            r = pcgRange(&rng, (i >> 2), i);
            if (r < (i >> 2) || r > i)
                return 1;
        }

        for (int32 i = 0; i + (i >> 2) + 1 > i; i += (i >> 2) + 1) {
            sr = pcgSRange(&rng, -i, i);
            if (sr < -i || sr > i)
                return 1;
        }

        for (uint64 i = 0; i + (i >> 2) + 1 > i; i += (i >> 2) + 1) {
            v = pcgBounded64(&rng, i);
            if (v >= i && v != 0)
                return 1;
        }

        for (uint64 i = 0; i + (i >> 2) + 1 > i; i += (i >> 2) + 1) {
            v = pcgRange64(&rng, (i >> 2), i);
            if (v < (i >> 2) || v > i)
                return 1;
        }

        for (int64 i = 0; i + (i >> 2) + 1 > i; i += (i >> 2) + 1) {
            sv = pcgSRange64(&rng, (i >> 2), i);
            if (sv < (i >> 2) || sv > i)
                return 1;
        }
    }

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
                return 1;
        }

        for (float64 i = 0; i < 1e+40; i += i/2 + 1) {
            v = pcgFRange64(&rng, -i, i);
            if (v < -i || v > i)
                return 1;
        }
    }

    return 0;
}

static int test_math_pcgerror()
{
    PcgState rng;
    pcgAutoSeed(&rng);

    // try a lot of things that should not work and verify they return
    // expected results

    if (pcgBounded(&rng, 0) != 0)
        return 1;

    if (pcgBounded64(&rng, 0) != 0)
        return 1;

    if (pcgRange(&rng, 5, 5) != 5)
        return 1;

    if (pcgRange(&rng, 9, 5) != 9)
        return 1;

    if (pcgRange64(&rng, 9000000000000LL, 5000000000000LL) != 9000000000000LL)
        return 1;

    if (pcgSRange(&rng, -50, -90) != -50)
        return 1;

    if (pcgFRange(&rng, 401, 5) != 401)
        return 1;

    if (pcgFRange64(&rng, -4029413, -9999999999) != -4029413)
        return 1;

    return 0;
}

testfunc mathtest_funcs[] = {
    { "pcgint", test_math_pcgint },
    { "pcgfloat", test_math_pcgfloat },
    { "pcgerror", test_math_pcgerror },
    { 0, 0 }
};
