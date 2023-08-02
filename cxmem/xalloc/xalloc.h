#pragma once

#include <cx/platform/cpp.h>

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

#define XA_Align(la) ((int)(la))
#define XA_Zero ((int)0x40)

#include "xalloc_mimalloc.h"

// String utilities because cstrDup is tied in with xalloc
// and there's no better place for them
#include <cxmem/cstrutil/cstrutil.h>

// Safe free
#define xaSFree(ptr) if (ptr) { xaFree((void*)ptr); (ptr) = NULL; }
