#include "pcg.h"

#include <mbedtls/entropy.h>

// Somewhat modified version of PCG, coverd by APL below:
// Extremely fast pseudo RNG with good statistical properties.

/*
* PCG Random Number Generation for C.
*
* Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*/

// extern inline uint32 pcgRange(PcgState *rng, uint32 lower, uint32 upper);

void pcgSeed(PcgState* rng, uint64 initstate, uint64 initseq)
{
    rng->state = 0U;
    rng->inc = (initseq << 1u) | 1u;
    pcgRandom(rng);
    rng->state += initstate;
    pcgRandom(rng);
}

void pcgAutoSeed(PcgState* rng)
{
    mbedtls_entropy_context entropy;
    uint64_t randbuf[2];

    mbedtls_entropy_init(&entropy);
    mbedtls_entropy_func(&entropy, (unsigned char*)randbuf, sizeof(randbuf));
    mbedtls_entropy_free(&entropy);

    pcgSeed(rng, randbuf[0], randbuf[1]);
}

uint32 pcgRandom(PcgState *rng)
{
    uint64 oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + rng->inc;
    uint32 xorshifted = (uint32)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32 rot = oldstate >> 59u;
    return (xorshifted >> rot) | (xorshifted << ((1+~rot) & 31));
}

uint32 pcgBounded(PcgState *rng, uint32 bound)
{
    // To avoid bias, we need to make the range of the RNG a multiple of
    // bound, which we do by dropping output less than a threshold.
    // A naive scheme to calculate the threshold would be
    //
    //     uint32 threshold = 0x100000000ull % bound;
    //
    // but 64-bit div/mod is slower than 32-bit div/mod (especially on
    // 32-bit platforms).  In essence, we calculate
    //
    //     uint32 threshold = (0x100000000ull-bound) % bound;
    //
    // because this version will calculate the same modulus, but the LHS
    // value is less than 2^32.

    uint32 threshold = (1+~bound) % bound;

    // Uniformity guarantees that this loop will terminate.  In practice, it
    // should usually terminate quickly; on average (assuming all bounds are
    // equally likely), 82.25% of the time, we can expect it to require just
    // one iteration.  In the worst case, someone passes a bound of 2^31 + 1
    // (i.e., 2147483649), which invalidates almost 50% of the range.  In 
    // practice, bounds are typically small and only a tiny amount of the range
    // is eliminated.
    for (;;) {
        uint32 r = pcgRandom(rng);
        if (r >= threshold)
            return r % bound;
    }
}

/* Multi-step advance functions (jump-ahead, jump-back)
*
* The method used here is based on Brown, "Random Number Generation
* with Arbitrary Stride,", Transactions of the American Nuclear
* Society (Nov. 1994).  The algorithm is very similar to fast
* exponentiation.
*
* Even though delta is an unsigned integer, we can pass a
* signed integer to go backwards, it just goes "the long way round".
*/
void pcgAdvance(PcgState *rng, uint64 delta)
{
    uint64 cur_mult = 6364136223846793005ULL;
    uint64 cur_plus = 1442695040888963407ULL;
    uint64 acc_mult = 1u;
    uint64 acc_plus = 0u;
    while (delta > 0) {
        if (delta & 1) {
            acc_mult *= cur_mult;
            acc_plus = acc_plus * cur_mult + cur_plus;
        }
        cur_plus = (cur_mult + 1) * cur_plus;
        cur_mult *= cur_mult;
        delta /= 2;
    }
    rng->state = acc_mult * rng->state + acc_plus;
}
