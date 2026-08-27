#pragma once

#include <cx/net/net_private.h>
#include <cx/platform/win.h>

#include <cx/buffer/buffer.h>

#include <limits.h>
#include <ws2tcpip.h>

// Map a raw winsock error code to a NetErrorCode. For codes that do not come from the thread-local
// WSAGetLastError() value: getsockopt(SO_ERROR) results, and the IOCP backend's completion errors
// (which route through its own mapper first, since a failed overlapped op reports Win32/NTSTATUS-
// derived codes that only partially overlap the WSAE* space).
NetErrorCode _netMapWsaError(int e);

bool netAddrToSockaddr(_In_ const NetAddr* addr, _Out_ struct sockaddr_storage* sa,
                       _Out_ int* sasz);

/// Fill a NetAddr from an OS sockaddr (the inverse of netAddrToSockaddr).
///
/// @param addr Destination address, populated on success
/// @param sa Source sockaddr, expected to be AF_INET or AF_INET6
/// @return true if the family was recognized and addr was written
bool netAddrFromSockaddr(_Out_ NetAddr* addr, _In_ const struct sockaddr* sa);

/// Platform scatter/gather vector, as the OS expects it.
///
/// WSABUF on Windows, struct iovec on unix. Spelled the same on both so that call sites which
/// gather into BufIov and then hand the result to the OS need no conditional compilation.
typedef WSABUF NetPlatIov;

// NET_MAX_IOV (the shared scatter/gather bound) is defined in net_private.h so the portable gather in
// socket.c and this platform translation agree on the array size.

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
///       WSASend(s, pv, (DWORD)netIovToPlatform(pv, iov, niov), &sent, 0, NULL, NULL);
///   }
/// @endcode
_meta_inline size_t netIovToPlatform(_Out_writes_(count) NetPlatIov* out,
                                     _In_reads_(count) const BufIov* iov, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        // WSABUF counts bytes in a ULONG, which is 32 bits even on 64-bit Windows. A segment
        // that large should be impossible, but truncating silently would send the wrong length.
        devAssertMsg(iov[i].len <= ULONG_MAX, "iov entry too large for WSABUF");
        out[i].buf = (CHAR*)iov[i].data;
        out[i].len = (ULONG)iov[i].len;
    }
    return count;
}