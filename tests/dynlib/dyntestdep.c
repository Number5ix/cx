// A dependency of dyntestneeds.c, linked at build time.
#include <cx/platform/base.h>

CX_EXPORT int dyntestDepValue(void)
{
    return 42;
}
