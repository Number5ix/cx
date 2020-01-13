#include "cx/time/clock.h"
#include "cx/time/time.h"

int64 clockWall()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return timeFromAbsTimespec(&ts);
}

int64 clockTimer()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    int64 ret = (int64)ts.tv_sec * 1000000;
    ret += ts.tv_nsec / 1000;

    return ret;
}
