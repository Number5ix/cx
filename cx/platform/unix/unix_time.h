#pragma once

#include "cx/cx.h"
#include <time.h>

_meta_inline uint64 timeFromTimeSpec(struct timespec *ts)
{
    uint64 ret = (uint64)ts->tv_sec * 1000000;
    ret += ts->tv_nsec / 1000;

    // Unix epoch is midnight on Jan 1, 1970
    // Which is a julian date of 2440587.50000
    // That's 210866760000 in seconds, or in microseconds...

    ret += 210866760000000000ULL;       // adjust epoch

    // This still leaves us over 500,000 years before overflow

    return ret;
}

_meta_inline uint64 timeFromTimeT(time_t tt)
{
    uint64 ret = (uint64)tt * 1000000;
    ret += 210866760000000000ULL;       // adjust epoch
    return ret;
}
