#pragma once

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

#include <cx/cx.h>

_EXTERN_C_BEGIN

typedef struct PcgState {   // Internals are *Private*.
    uint64 state;           // RNG state.  All values are possible.
    uint64 inc;             // Controls which RNG sequence (stream) is
                            // selected. Must *always* be odd.
} PcgState;

// Seeds the random number generator with an initial state and sequence (stream)
void pcgSeed(PcgState* rng, uint64 initstate, uint64 initseq);

// Seeds the random number generator using cryptographic-quality entropy.
// Does not permit stream selection as it's meaningless when not starting
// from a known state.
void pcgAutoSeed(PcgState* rng);

// Generates a random integer from [0..UINT32_MAX]
uint32 pcgRandom(PcgState *rng);

// Generates a random integer from [0..bound)
// unlike pcgRandom() % bound, does not suffer from bias
uint32 pcgBounded(PcgState *rng, uint32 bound);

// Generates a random integer from [lower..upper]
_meta_inline uint32 pcgRange(PcgState *rng, uint32 lower, uint32 upper)
{
    return lower + pcgBounded(rng, upper - lower + 1);
}

// Advances the RNG state by delta iterations
void pcgAdvance(PcgState *rng, uint64 delta);

_EXTERN_C_END
