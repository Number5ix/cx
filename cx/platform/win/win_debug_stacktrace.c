#include "cx/platform/win.h"
#include "win_stacktrace.h"

_no_inline int dbgStackTrace(int nskip, int nframes, uintptr_t* addrs)
{
    return RtlCaptureStackBackTrace(nskip + 1, nframes, (void**)addrs, NULL);
}
