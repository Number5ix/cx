// Loaded at runtime by dynlibtest.c; fails to load unless dyntestdep is next to it.
#include <cx/platform/base.h>

CX_IMPORT int dyntestDepValue(void);

CX_EXPORT int dyntestNeedsValue(void)
{
    return dyntestDepValue() + 1;
}
