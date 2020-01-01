#include "cx/time/clock.h"
#include "unix_time.h"

uint64 clockWall()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return timeFromTimeSpec(&ts);
}

uint64 clockTimer()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    uint64 ret = (uint64)ts.tv_sec * 1000000;
    ret += ts.tv_nsec / 1000;

    return ret;
}
