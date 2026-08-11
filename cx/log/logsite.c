// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/time.h>

// Per-call-site rate limiting.
//
// The state a gate needs is small enough to live in the call site's own static, which is what
// makes this cost nothing to a call site that does not use it and lets two identical messages in
// the same function keep separate budgets. Nothing is registered anywhere and no key is derived
// from the message text.
//
// None of it is serialized. Two threads arriving at the same site concurrently can both pass,
// because making a rate limit exact needs a lock per call site -- which costs more, in the case
// that matters (a site being hit hard enough to need limiting), than the handful of extra
// messages it would suppress.

// Coarse monotonic milliseconds for the interval gate. clockTimer() is microseconds, but there is
// no atomic(int64) on 32-bit targets at all -- cx/thread/atomic.h only generates 64-bit atomics
// under _64BIT -- so the stamp is truncated to 32 bits and compared with wraparound in mind, the
// same discipline logSeqBefore() uses. It wraps every 49.7 days, which is correct for any
// interval shorter than about 24 days.
static uint32 logSiteNow(void)
{
    return (uint32)timeToMsec(clockTimer());
}

_Use_decl_annotations_
bool _logSiteGate(LogSite* site, int gate, int64 garg)
{
    switch (gate) {
    case LOG_SiteOnce:
        return atomicFetchAdd(uint32, &site->count, 1, Relaxed) == 0;

    case LOG_SiteEveryN: {
        uint32 n = (garg > 1) ? (uint32)garg : 1;
        return (atomicFetchAdd(uint32, &site->count, 1, Relaxed) % n) == 0;
    }

    case LOG_SiteEveryT: {
        uint32 now   = logSiteNow();
        uint32 ival  = (uint32)timeToMsec(garg);
        uint32 count = atomicFetchAdd(uint32, &site->count, 1, Relaxed);

        // the first arrival always emits, which also establishes the reference stamp -- a
        // zero-initialized `last` is a real timestamp as far as the comparison is concerned
        if (count != 0 && (int32)(now - atomicLoad(uint32, &site->last, Relaxed)) < (int32)ival)
            return false;

        atomicStore(uint32, &site->last, now, Relaxed);
        return true;
    }
    }

    return true;
}
