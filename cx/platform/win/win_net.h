#pragma once

#include <cx/net/net_private.h>
#include <cx/platform/win.h>

#include <cx/buffer/buffer.h>

#include <limits.h>
#include <ws2tcpip.h>
#include <mswsock.h>   // WSAID_WSARECVMSG / WSAID_WSASENDMSG, which are extensions, not exports

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

// WSARecvMsg and WSASendMsg are the only Winsock calls that carry control messages, and both are
// Vista-era additions: an XP build targets a _WIN32_WINNT the SDK headers do not declare the
// message structure or the packet-info option under, so this whole path compiles out there. Where
// it is missing, everything ancillary falls back to plain recvfrom/sendto and reports nothing,
// which is the same answer a path that strips ECN gives.
#if defined(WSA_CMSG_FIRSTHDR) && defined(IP_PKTINFO)
#define NET_HAVE_WSAMSG 1
#endif

#if defined(NET_HAVE_WSAMSG)

// Enough room for the control messages one datagram carries in either direction, in either address
// family: a packet info structure and a traffic class byte.
#define NET_CMSG_SPACE 128

typedef INT(WSAAPI* NetRecvMsgFn)(SOCKET, LPWSAMSG, LPDWORD, LPWSAOVERLAPPED,
                                  LPWSAOVERLAPPED_COMPLETION_ROUTINE);

typedef INT(WSAAPI* NetSendMsgFn)(SOCKET, LPWSAMSG, DWORD, LPDWORD, LPWSAOVERLAPPED,
                                  LPWSAOVERLAPPED_COMPLETION_ROUTINE);

// The WSARecvMsg / WSASendMsg extensions, or NULL if they could not be loaded. Fetched once from a
// throwaway socket; the pointers are good for every socket in the process.
NetRecvMsgFn _netRecvMsgFn(void);
NetSendMsgFn _netSendMsgFn(void);

// Writes the control messages that ask for a local address and an ECN mark into a message whose
// Control field already points at a buffer of at least NET_CMSG_SPACE bytes. Control.len is left
// holding what was actually used, which is zero when the platform can express neither -- and a
// message with a control buffer but nothing in it is not the same as one with no control buffer,
// so a caller must clear Control entirely in that case.
//
// Returns true if anything was written.
bool _netPktInfoToCmsg(_Inout_ WSAMSG* mh, _In_ const NetPktInfo* info, int family);

// Reads the local address and ECN mark out of a received message's control data, and clears what
// was not there.
void _netCmsgToPktInfo(_Inout_ WSAMSG* mh, _Out_ NetPktInfo* info);

#endif

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