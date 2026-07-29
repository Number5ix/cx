#pragma once

#include <cx/net/net_private.h>
#include <cx/platform/unix.h>
#include <cx/buffer/buffer.h>

#include <limits.h>
#include <sys/socket.h>
#include <sys/uio.h>

bool netAddrToSockaddr(_In_ NetAddr* addr, _Out_ struct sockaddr_storage* sa, _Out_ int* sasz);

/// Fill a NetAddr from an OS sockaddr (the inverse of netAddrToSockaddr).
///
/// @param addr Destination address, populated on success
/// @param sa Source sockaddr, expected to be AF_INET or AF_INET6
/// @return true if the family was recognized and addr was written
bool netAddrFromSockaddr(_Out_ NetAddr* addr, _In_ const struct sockaddr* sa);

/// Platform scatter/gather vector, as the OS expects it.
///
/// struct iovec on unix, WSABUF on Windows. Spelled the same on both so that call sites which
/// gather into BufIov and then hand the result to the OS need no conditional compilation.
typedef struct iovec NetPlatIov;

/// Largest scatter/gather array this platform will be asked to accept.
///
/// POSIX guarantees IOV_MAX is at least 16 and it is commonly 1024, but writev() fails with EINVAL
/// rather than doing a short write if the count exceeds it, so the cap is not optional. NET_MAX_IOV
/// is already defined in net_private.h (64, to match Windows); only override it here if this
/// platform's IOV_MAX is stricter than that shared default.
#if defined(NET_MAX_IOV)
#undef NET_MAX_IOV
#endif
#if defined(IOV_MAX) && IOV_MAX < 64
#define NET_MAX_IOV IOV_MAX
#else
#define NET_MAX_IOV 64
#endif

/// Translate platform-neutral BufIov entries into the platform's own vector type.
///
/// Two stores per entry, immediately before the syscall. See BufIov for why the neutral type does
/// not simply alias the platform one.
///
/// @param out Array of platform vectors to fill, at least `count` entries
/// @param iov Source entries, normally from bufchainGatherIov()
/// @param count Number of entries to translate
/// @return `count`, so the call can be inlined into the syscall's argument list
///
/// Example:
/// @code
///   BufIov iov[NET_MAX_IOV];
///   size_t niov;
///   if (bufchainGatherIov(chain, iov, NET_MAX_IOV, &niov) > 0) {
///       NetPlatIov pv[NET_MAX_IOV];
///       ssize_t sent = writev(fd, pv, (int)netIovToPlatform(pv, iov, niov));
///   }
/// @endcode
_meta_inline size_t netIovToPlatform(_Out_writes_(count) NetPlatIov* out,
                                     _In_reads_(count) const BufIov* iov, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        out[i].iov_base = iov[i].data;
        out[i].iov_len  = iov[i].len;
    }
    return count;
}
