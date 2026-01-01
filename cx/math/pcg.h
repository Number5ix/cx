#pragma once

/// @file pcg.h
/// @brief High-quality pseudo-random number generation using PCG algorithm
///
/// Somewhat modified version of PCG (Permuted Congruential Generator).
/// Extremely fast pseudo-random number generator with excellent statistical properties.
///
/// PCG Random Number Generation for C.
///
/// Copyright 2014 Melissa O'Neill <oneill@pcg-random.org>
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0

#include <cx/cx.h>
#include <cx/debug/assert.h>

CX_C_BEGIN

/// @addtogroup rng
/// @{
///
/// The PCG (Permuted Congruential Generator) is a family of simple fast space-efficient
/// statistically good algorithms for random number generation. PCG combines carefully
/// chosen linear congruential generator with a permutation function to produce high-quality
/// random output.
///
/// Key features:
/// - Very fast generation (comparable to or faster than other PRNGs)
/// - Excellent statistical properties (passes TestU01 BigCrush)
/// - Small state size (only 16 bytes)
/// - Multiple independent streams via sequence selection
/// - Predictable and reproducible with known seeds
/// - Both cryptographic and time-based seeding options
///
/// Basic usage:
/// @code
///   PcgState rng;
///   pcgAutoSeed(&rng);  // Seed with cryptographic-quality entropy
///
///   uint32 val = pcgRandom(&rng);           // Generate random uint32
///   uint32 dice = pcgRange(&rng, 1, 6);     // Simulate a 6-sided die
///   bool coin = pcgFlip(&rng);              // Flip a coin
/// @endcode

/// PCG random number generator state
///
/// The internal state is private and should not be accessed directly.
/// Always use the provided functions to manipulate and query the generator.
typedef struct PcgState {
    uint64 state;   ///< RNG state - all values are possible
    uint64 inc;     ///< Controls which RNG sequence (stream) is selected - must always be odd
} PcgState;

/// @defgroup pcg_init Initialization
/// @ingroup rng
/// @{
///
/// Functions for initializing PCG generator state.

/// Seeds the random number generator with an initial state and sequence
///
/// This function initializes the RNG with deterministic values, allowing for
/// reproducible random sequences. The sequence parameter selects one of 2^63
/// possible independent random streams.
///
/// Use this when you need reproducible results or want to select a specific
/// stream for parallel random number generation.
///
/// @param rng Pointer to uninitialized PCG state
/// @param initstate Initial state value (seed)
/// @param initseq Sequence selector (stream) - determines which independent random sequence to use
void pcgSeed(_Out_ PcgState* rng, uint64 initstate, uint64 initseq);

/// Seeds the random number generator using cryptographic-quality entropy
///
/// Uses the operating system's cryptographic random number generator (e.g., /dev/urandom,
/// CryptGenRandom, or getrandom()) to obtain high-quality random seeds. Falls back to
/// time-based seeding if the OS random source is unavailable.
///
/// This is the recommended seeding method for most use cases where reproducibility is
/// not required. Note that stream selection is not meaningful when using non-deterministic
/// seeding, so the stream is also randomly chosen.
///
/// @param rng Pointer to uninitialized PCG state
void pcgAutoSeed(_Out_ PcgState* rng);

/// @}  // end of pcg_init group

/// @defgroup pcg_generate Random Number Generation
/// @ingroup rng
/// @{
///
/// Core random number generation functions.

/// Generates a uniformly distributed random 32-bit integer
///
/// Produces random values in the full range [0..UINT32_MAX]. This is the
/// fundamental generation function that all other PCG functions build upon.
///
/// @param rng Pointer to initialized PCG state
/// @return Random uint32 value in range [0..UINT32_MAX]
uint32 pcgRandom(_Inout_ PcgState* rng);

/// Generates a uniformly distributed random integer in the range [0..bound)
///
/// Uses an unbiased rejection sampling algorithm to ensure perfect uniformity.
/// Unlike the naive approach of using `pcgRandom() % bound`, this function
/// eliminates modulo bias that would otherwise favor smaller values.
///
/// On average, this function completes in 1.33 iterations or less for any bound.
/// The worst case (bound = 2^31 + 1) requires ~2 iterations on average.
///
/// @param rng Pointer to initialized PCG state
/// @param bound Upper bound (exclusive) - must be > 0 for meaningful results
/// @return Random uint32 value in range [0..bound-1], or 0 if bound is 0
uint32 pcgBounded(_Inout_ PcgState* rng, uint32 bound);

/// Simulates a coin flip with 50/50 probability
///
/// Efficiently generates a random boolean value by testing a single bit
/// from the random number generator output.
///
/// @param rng Pointer to initialized PCG state
/// @return true or false with equal probability
bool pcgFlip(_Inout_ PcgState* rng);

/// int32 pcgSBounded(PcgState *rng, int32 bound)
///
/// Generates a uniformly distributed signed random integer in the range [0..bound)
///
/// Signed integer version of pcgBounded(). The bound must be non-negative.
/// Uses the same unbiased rejection sampling algorithm as pcgBounded().
///
/// @param rng Pointer to initialized PCG state
/// @param bound Upper bound (exclusive) - must be >= 0
/// @return Random int32 value in range [0..bound-1]
_meta_inline int32 pcgSBounded(_Inout_ PcgState* rng, int32 bound)
{
    devAssert(bound >= 0);
    return (int32)pcgBounded(rng, bound);
}

/// uint32 pcgRange(PcgState *rng, uint32 lower, uint32 upper)
///
/// Generates a uniformly distributed random integer in the range [lower..upper]
///
/// Both bounds are inclusive. If lower >= upper, returns lower.
/// Useful for generating values in an arbitrary range, such as dice rolls.
///
/// Example:
/// @code
///   uint32 d6 = pcgRange(&rng, 1, 6);      // Roll a 6-sided die
///   uint32 d20 = pcgRange(&rng, 1, 20);    // Roll a 20-sided die
///   uint32 percent = pcgRange(&rng, 0, 99); // Random percentage [0..99]
/// @endcode
///
/// @param rng Pointer to initialized PCG state
/// @param lower Lower bound (inclusive)
/// @param upper Upper bound (inclusive)
/// @return Random uint32 value in range [lower..upper]
_meta_inline uint32 pcgRange(_Inout_ PcgState* rng, uint32 lower, uint32 upper)
{
    if (lower >= upper)
        return lower;

    return lower + pcgBounded(rng, upper - lower + 1);
}

/// int32 pcgSRange(PcgState *rng, int32 lower, int32 upper)
///
/// Generates a uniformly distributed signed random integer in the range [lower..upper]
///
/// Both bounds are inclusive. If lower >= upper, returns lower.
/// Signed integer version of pcgRange().
///
/// Example:
/// @code
///   int32 temp = pcgSRange(&rng, -20, 40);  // Random temperature in Celsius
///   int32 offset = pcgSRange(&rng, -5, 5);  // Small random offset
/// @endcode
///
/// @param rng Pointer to initialized PCG state
/// @param lower Lower bound (inclusive)
/// @param upper Upper bound (inclusive)
/// @return Random int32 value in range [lower..upper]
_meta_inline int32 pcgSRange(_Inout_ PcgState* rng, int32 lower, int32 upper)
{
    if (lower >= upper)
        return lower;

    return (int32)(lower + pcgBounded(rng, (uint32)(upper - lower + 1)));
}

/// Generates a uniformly distributed random 64-bit integer
///
/// Produces random values in the full range [0..UINT64_MAX] by combining
/// two 32-bit random values. Slightly slower than pcgRandom() due to
/// requiring two generation steps.
///
/// @param rng Pointer to initialized PCG state
/// @return Random uint64 value in range [0..UINT64_MAX]
uint64 pcgRandom64(_Inout_ PcgState* rng);

/// Generates a uniformly distributed random 64-bit integer in the range [0..bound)
///
/// 64-bit version of pcgBounded(). Uses the same unbiased rejection sampling
/// algorithm to ensure perfect uniformity across the full 64-bit range.
///
/// @param rng Pointer to initialized PCG state
/// @param bound Upper bound (exclusive) - must be > 0 for meaningful results
/// @return Random uint64 value in range [0..bound-1], or 0 if bound is 0
uint64 pcgBounded64(_Inout_ PcgState* rng, uint64 bound);

/// uint64 pcgRange64(PcgState *rng, uint64 lower, uint64 upper)
///
/// Generates a uniformly distributed random 64-bit integer in the range [lower..upper]
///
/// Both bounds are inclusive. If lower >= upper, returns lower.
/// 64-bit version of pcgRange().
///
/// @param rng Pointer to initialized PCG state
/// @param lower Lower bound (inclusive)
/// @param upper Upper bound (inclusive)
/// @return Random uint64 value in range [lower..upper]
_meta_inline uint64 pcgRange64(_Inout_ PcgState* rng, uint64 lower, uint64 upper)
{
    if (lower >= upper)
        return lower;

    return lower + pcgBounded64(rng, upper - lower + 1);
}

/// int64 pcgSRange64(PcgState *rng, int64 lower, int64 upper)
///
/// Generates a uniformly distributed signed 64-bit random integer in the range [lower..upper]
///
/// Both bounds are inclusive. If lower >= upper, returns lower.
/// Signed 64-bit version of pcgRange64().
///
/// @param rng Pointer to initialized PCG state
/// @param lower Lower bound (inclusive)
/// @param upper Upper bound (inclusive)
/// @return Random int64 value in range [lower..upper]
_meta_inline int64 pcgSRange64(_Inout_ PcgState* rng, int64 lower, int64 upper)
{
    if (lower >= upper)
        return lower;

    return (int64)(lower + pcgBounded64(rng, (uint64)(upper - lower + 1)));
}

/// Generates a uniformly distributed random floating-point number in the range [lower..upper]
///
/// Produces float32 values with uniform distribution. The precision is limited
/// by the underlying 32-bit random integer and IEEE 754 float32 representation.
///
/// If lower >= upper, returns lower.
///
/// Example:
/// @code
///   float32 speed = pcgFRange(&rng, 0.5f, 2.5f);  // Random speed multiplier
///   float32 angle = pcgFRange(&rng, 0.0f, 6.28f); // Random angle in radians
/// @endcode
///
/// @param rng Pointer to initialized PCG state
/// @param lower Lower bound (inclusive)
/// @param upper Upper bound (inclusive)
/// @return Random float32 value in range [lower..upper]
float32 pcgFRange(_Inout_ PcgState* rng, float32 lower, float32 upper);

/// Generates a uniformly distributed random double-precision floating-point number in the range
/// [lower..upper]
///
/// Produces float64 values with uniform distribution. Uses pcgRandom64() for
/// higher precision than pcgFRange().
///
/// If lower >= upper, returns lower.
///
/// @param rng Pointer to initialized PCG state
/// @param lower Lower bound (inclusive)
/// @param upper Upper bound (inclusive)
/// @return Random float64 value in range [lower..upper]
float64 pcgFRange64(_Inout_ PcgState* rng, float64 lower, float64 upper);

/// @}  // end of pcg_generate group

/// @defgroup pcg_advance State Manipulation
/// @ingroup rng
/// @{
///
/// Advanced functions for manipulating generator state.

/// Advances the RNG state forward (or backward) by a specified number of steps
///
/// Efficiently jumps the RNG state forward or backward without generating intermediate
/// values. This is useful for:
/// - Skipping ahead in a sequence for parallel generation
/// - Implementing "save states" in simulations
/// - Jumping to specific points in a reproducible sequence
///
/// The algorithm uses O(log(delta)) time complexity based on fast exponentiation,
/// making even very large jumps efficient.
///
/// Note: Passing a negative value (as uint64) will advance backward, effectively
/// "jumping back" in the sequence.
///
/// Example:
/// @code
///   PcgState rng1, rng2;
///   pcgSeed(&rng1, 42, 54);
///   pcgSeed(&rng2, 42, 54);
///
///   pcgAdvance(&rng2, 1000);  // Jump ahead 1000 steps
///
///   // rng1 and rng2 are now at different points in the same sequence
///   // Generate 1000 values from rng1 would bring it to rng2's state
/// @endcode
///
/// @param rng Pointer to initialized PCG state
/// @param delta Number of steps to advance (can be negative to go backward)
void pcgAdvance(_Inout_ PcgState* rng, uint64 delta);

/// @}  // end of pcg_advance group

/// @}  // end of rng group

CX_C_END
