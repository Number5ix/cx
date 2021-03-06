#pragma once

#include <cxmem/config.h>
#include <cx/core/cpp.h>

/* XAlloc: Memory allocation interface based on jemalloc's mallocx family of functions */

#ifdef XALLOC_REMAP_MALLOC
// these need to be pulled in first so our #defines don't cause chaos when they
// are included
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// never use this; we have heap profilers these days
#undef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#endif

// These defines must match up with jemalloc MALLOCX_*!
#define XAFUNC_ 0
#define XAFUNC_Align(la) ((int)(la))
#define XAFUNC_Zero ((int)0x40)

#if XALLOC_USE_JEMALLOC
#include "xalloc_jemalloc.h"
#elif XALLOC_USE_MSVCRT
#include "xalloc_msvcrt.h"
#else
#error cxmem requires either XALLOC_USE_JEMALLOC or XALLOC_USE_MSVCRT to be set!
#endif

// String utilities because cstrDup is tied in with xalloc
// and there's no better place for them
#include <cxmem/cstrutil/cstrutil.h>

// Safe free
#define xaSFree(ptr) if (ptr) { xaFree((void*)ptr); (ptr) = NULL; }
