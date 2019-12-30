#pragma once

// This is the master include file going forward for all CX-based components.
// It should be included by all CX header files.
//
// Changing this file will require EVERYTHING to be rebuilt, so do so sparingly.

#ifdef _WINBASE_
#error("winbase.h has already been included before cx.h");
#endif

#include <cxmem/config.h>

#include <string.h>
#include <ctype.h>
#include <time.h>

#include <cx/platform/base.h>
#include <cx/core/cpp.h>
#include <cx/core/stype.h>
#include <cx/core/suid.h>
#include <cxmem/xalloc/xalloc.h>

#ifdef __cplusplus
#include <cstdio>
#else
#include <stdio.h>
#endif
#include <stdlib.h>

