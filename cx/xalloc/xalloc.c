#include <cx/cx.h>
#include "xalloc.h"

#include <mimalloc.h>
#include <cx/utils/macros.h>
#include <string.h>

size_t xaSize(void *ptr)
{
    return mi_usable_size(ptr);
}

size_t xaOptSize(size_t sz)
{
    return mi_good_size(sz);
}

void xaFlush()
{
    mi_collect(true);
}
