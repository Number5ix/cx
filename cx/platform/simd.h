#pragma once

#include <cx/platform/base.h>

#if defined(_ARCH_X86) || defined(_ARCH_X64)

// All three compilers support emmintrin.h for SSE2
#include <emmintrin.h>
#define _SIMD 1
#define _SIMD_SSE2 1

typedef __m128i simd128;

#define CX_SIMD_SSE2 1

_meta_inline simd128 simd128Set1_u8(uint8 val)
{
    return _mm_set1_epi8((char)val);
}

_meta_inline simd128 simd128Load(const void *ptr)
{
    return _mm_loadu_si128((const __m128i*)ptr);
}

_meta_inline uint32 simd128CmpEq_u8_mask(simd128 a, simd128 b)
{
    return (uint32)_mm_movemask_epi8(_mm_cmpeq_epi8(a, b));
}

#elif defined(_ARCH_ARM64)

#include <arm_neon.h>
#define _SIMD 1
#define _SIMD_NEON 1

typedef uint8x16_t simd128;

_meta_inline simd128 simd128Set1_u8(uint8 val)
{
    return vdupq_n_u8(val);
}

_meta_inline simd128 simd128Load(const void *ptr)
{
    return vld1q_u8((const uint8_t*)ptr);
}

_meta_inline uint32 simd128CmpEq_u8_mask(simd128 a, simd128 b)
{
    // ARM NEON workaround - expensive!
    uint8x16_t cmp = vceqq_u8(a, b);
    
    // Extract each byte's high bit and pack into 16-bit result
    static const uint8_t bit_positions[16] = {
        1<<0, 1<<1, 1<<2, 1<<3, 1<<4, 1<<5, 1<<6, 1<<7,
        1<<0, 1<<1, 1<<2, 1<<3, 1<<4, 1<<5, 1<<6, 1<<7
    };
    
    uint8x16_t bits = vandq_u8(cmp, vld1q_u8(bit_positions));
    uint8x8_t low = vget_low_u8(bits);
    uint8x8_t high = vget_high_u8(bits);
    
    return vaddv_u8(low) | ((uint32)vaddv_u8(high) << 8);
}

#endif
