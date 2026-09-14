// Loaded at runtime by dynlibtest.c. Deliberately does not link cx.
#include <cx/platform/base.h>

static int bumpCount;

CX_EXPORT int dyntestValue = 1234;

CX_EXPORT int dyntestAdd(int a, int b)
{
    return a + b;
}

CX_EXPORT int dyntestBump(void)
{
    return ++bumpCount;
}
