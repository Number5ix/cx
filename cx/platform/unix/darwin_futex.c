#include <cx/platform/cpu.h>
#include <cx/platform/os.h>
#include <cx/thread/futex.h>
#include <cx/time.h>
#include <cx/utils.h>

#include <os/os_sync_wait_on_address.h>
#include <errno.h>

void futexInit(Futex* ftx, int32 val)
{
    atomicStore(int32, &ftx->val, val, Relaxed);
    atomicStore(uint16, &ftx->_ps, 0, Relaxed);
    atomicStore(uint8, &ftx->_ps_lock, 0, Relaxed);
}

int futexWait(Futex* ftx, int32 oldval, int64 timeout)
{
    // early out if the value already doesn't match
    if (atomicLoad(int32, &ftx->val, Relaxed) != oldval)
        return FUTEX_Retry;

    int sret;
    // A timeout too large to express in nanoseconds is as good as forever.
    if (timeout == timeForever || timeout > INT64_MAX / 1000) {
        sret = os_sync_wait_on_address(&ftx->val,
                                       (uint64_t)(uint32_t)oldval,
                                       sizeof(ftx->val),
                                       OS_SYNC_WAIT_ON_ADDRESS_NONE);
    } else {
        // the kernel rejects a zero timeout with EINVAL rather than treating it as a poll
        if (timeout <= 0)
            return FUTEX_Timeout;
        sret = os_sync_wait_on_address_with_timeout(&ftx->val,
                                                    (uint64_t)(uint32_t)oldval,
                                                    sizeof(ftx->val),
                                                    OS_SYNC_WAIT_ON_ADDRESS_NONE,
                                                    OS_CLOCK_MACH_ABSOLUTE_TIME,
                                                    (uint64_t)timeout * 1000);
    }

    // Like FreeBSD, a value mismatch found by the kernel returns success, so it can't be told apart
    // from a real wakeup. Callers loop on FUTEX_Waited anyway; EINTR is a spurious wakeup too.
    if (sret >= 0 || errno == EINTR)
        return FUTEX_Waited;
    if (errno == ETIMEDOUT)
        return FUTEX_Timeout;
    return FUTEX_Error;
}

void futexWake(Futex* ftx)
{
    os_sync_wake_by_address_any(&ftx->val, sizeof(ftx->val), OS_SYNC_WAKE_BY_ADDRESS_NONE);
}

void futexWakeMany(Futex* ftx, int count)
{
    // There is no wake-N; wake one at a time until the count is met or no waiters remain (ENOENT).
    for (int i = 0; i < count; i++) {
        if (os_sync_wake_by_address_any(&ftx->val, sizeof(ftx->val), OS_SYNC_WAKE_BY_ADDRESS_NONE) != 0)
            break;
    }
}

void futexWakeAll(Futex* ftx)
{
    os_sync_wake_by_address_all(&ftx->val, sizeof(ftx->val), OS_SYNC_WAKE_BY_ADDRESS_NONE);
}
