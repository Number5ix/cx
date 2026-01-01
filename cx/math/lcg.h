/// @file lcg.h
/// @brief Simple linear congruential pseudo-random number generator
///
/// Basic LCG implementation for situations where simplicity and minimal state
/// are more important than statistical quality. For most applications, prefer
/// the PCG generator which offers superior quality with comparable performance.

#include <cx/cx.h>

/// @defgroup lcg Linear Congruential Generator (LCG)
/// @ingroup rng
/// @{
///
/// A basic linear congruential generator (LCG) using the classic parameters
/// from Numerical Recipes: multiplier 1103515245 and increment 12345.
///
/// **Characteristics:**
/// - Extremely simple and fast (single multiply-add-modulo operation)
/// - Minimal state (only 32 bits)
/// - Fully inline implementation with no function call overhead
/// - Poor statistical quality compared to modern PRNGs like PCG
/// - Predictable patterns in lower-order bits
/// - Short period relative to state size
///
/// **When to use:**
/// - Extremely constrained environments where 32 bits of state is critical
/// - Simple randomness where quality doesn't matter (e.g., non-critical visual effects)
/// - Educational purposes or algorithm prototyping
///
/// **When NOT to use:**
/// - Simulations requiring good statistical properties
/// - Security or cryptographic applications (never!)
/// - Any situation where PCG or another quality PRNG can be used
///
/// @note For better random number generation with excellent performance,
///       use the PCG generator instead (see pcg.h).
///
/// Example:
/// @code
///   uint32 state = 12345;  // Simple initialization
///   int32 val = lcgRandom(&state);  // Generate random value [0..LCG_MAX]
/// @endcode

/// Maximum value that can be returned by lcgRandom()
///
/// The LCG generates values in the range [0..LCG_MAX] inclusive.
/// This value is 0x7FFFFFFE (2147483646), slightly less than INT32_MAX.
#define LCG_MAX (0x7ffffffe)

/// int32 lcgRandom(uint32 *state)
///
/// Generates the next pseudo-random number in the LCG sequence
///
/// Updates the state and returns a random integer in the range [0..LCG_MAX].
/// The state is modified in place using the formula:
///
///     state = (state * 1103515245 + 12345) mod (LCG_MAX + 1)
///
/// These are the classic LCG parameters used in many implementations including
/// glibc's rand() (though the output transformation may differ).
///
/// **Important:** The state must be initialized to a non-zero value before first use.
/// Different initial values produce different sequences. For simple cases, any
/// non-zero value works; for reproducibility, use a specific seed value.
///
/// @param state Pointer to the 32-bit state variable (modified in place)
/// @return Random int32 value in range [0..LCG_MAX]
_meta_inline int32 lcgRandom(_Inout_ uint32* state)
{
    return ((*state = *state * 1103515245 + 12345) % ((uint32)LCG_MAX + 1));
}

/// @}  // end of lcg group
