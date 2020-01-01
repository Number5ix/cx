#include "win_time.h"
#include "cx/utils/lazyinit.h"
#include "cx/platform/win.h"

uint64 clockWall()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return timeFromFileTime(&ft);
}

static LazyInitState qpcInitState;
static uint64 qpcMult;
static uint64 qpcDivisor;

static void qpcInit(void *data)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    if (freq.QuadPart > 1000000) {
        qpcMult = 10;
        qpcDivisor = freq.QuadPart / 100000;
    } else {
        qpcMult = 100000 / freq.QuadPart;
        qpcDivisor = 10;
    }
}

uint64 clockTimer()
{
    lazyInit(&qpcInitState, qpcInit, 0);

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    return counter.QuadPart * qpcMult / qpcDivisor;
}
