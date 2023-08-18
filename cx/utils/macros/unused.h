#pragma once

#if defined(__GNUC__)
#include <stdint.h>
__attribute__((always_inline)) inline int _unused_helper(__attribute__((ununsed)) intptr_t a) { return 0; }
#define unused_noeval(x) (_unused_helper(true ? (intptr_t)0 : (intptr_t)(x)))
#else
#define unused_noeval(x) (true ? (void)0 : (void)(x))
#endif
#define nop_stmt ((void)0)
