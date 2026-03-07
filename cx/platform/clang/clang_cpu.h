#pragma once

#include <cx/platform/base.h>

#if defined(__aarch64__)
#define _CPU_PAUSE __asm__ volatile("yield")
#elif defined(__EMSCRIPTEN__)
// WebAssembly doesn't have a spinloop hint
#define _CPU_PAUSE
#else
#define _CPU_PAUSE __asm__ volatile("pause")
#endif

#define _CPU_PREFETCH(ptr) __builtin_prefetch((ptr), 0, 0)

_meta_inline int ctz32(unsigned long mask)
{
    return __builtin_ctz(mask);
}

#if defined (_ARCH_X64) || defined(_ARCH_ARM64)

_meta_inline int ctz64(unsigned long long mask)
{
    return __builtin_ctzll(mask);
}

#endif
