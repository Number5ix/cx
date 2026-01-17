#pragma once

#include <cx/platform/base.h>
#include <intrin.h>

#define _CPU_PAUSE _mm_pause()
#define _CPU_BREAK __debugbreak()

#define _CPU_PREFETCH(ptr) _mm_prefetch((const char*)(ptr), _MM_HINT_T0)

_meta_inline int ctz32(unsigned long mask)
{
    unsigned long idx;
    _BitScanForward(&idx, mask);
    return (int)idx;
}

#ifdef _ARCH_X64

_meta_inline int ctz64(unsigned long long mask)
{
    unsigned long idx;
    _BitScanForward64(&idx, mask);
    return (int)idx;
}

#endif