#pragma once

#include "cx/cx.h"
#include <time.h>

// time interval conversion
_meta_inline int64 timeToSeconds(int64 time)
{
    return time / 1000000;
}

_meta_inline int64 timeFromSeconds(int64 seconds)
{
    return seconds * 1000000;
}

_meta_inline int64 timeToMsec(int64 time)
{
    return time / 1000;
}

_meta_inline int64 timeFromMsec(int64 msec)
{
    return msec * 1000;
}

// relative timespec

_meta_inline int64 timeFromRelTimespec(struct timespec *ts)
{
    int64 ret = (int64)ts->tv_sec * 1000000;
    ret += ts->tv_nsec / 1000;
    return ret;
}

_meta_inline void timeToRelTimespec(struct timespec *ts, int64 time)
{
    ts->tv_sec = time / 1000000;
    ts->tv_nsec = (time % 1000000) * 1000;
}

// unix absolute timespec

_meta_inline int64 timeFromAbsTimespec(struct timespec *ts)
{
    int64 ret = (int64)ts->tv_sec * 1000000;
    ret += ts->tv_nsec / 1000;

    // Unix epoch is midnight on Jan 1, 1970
    // Which is a julian date of 2440587.50000
    // That's 210866760000 in seconds, or in microseconds...

    ret += 210866760000000000LL;       // adjust epoch

    // This still leaves us over 280,000 years before overflow

    return ret;
}

_meta_inline void timeToAbsTimespec(struct timespec *ts, int64 time)
{
    time -= 210866760000000000LL;       // adjust epoch

    ts->tv_sec = time / 1000000;
    ts->tv_nsec = (time % 1000000) * 1000;
}

// unix time_t

_meta_inline int64 timeFromTimeT(time_t tt)
{
    int64 ret = (int64)tt * 1000000;
    ret += 210866760000000000LL;       // adjust epoch
    return ret;
}

_meta_inline time_t timeToTimeT(int64 time)
{
    time -= 210866760000000000LL;       // adjust epoch
    return (time_t)(time / 1000000);
}
