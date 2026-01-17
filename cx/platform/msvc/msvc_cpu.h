#pragma once

#include <intrin.h>

#define _CPU_PAUSE _mm_pause()
#define _CPU_BREAK __debugbreak()

#define _CPU_PREFETCH(ptr) _mm_prefetch((const char*)(ptr), _MM_HINT_T0)