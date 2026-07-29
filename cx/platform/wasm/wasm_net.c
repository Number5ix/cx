#include "wasm_net.h"
#include "platform/wasm/wasm_net_socket.h"
#include "net/queue_select.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>

// select is the lowest-common-denominator backend and the only one WASM gets: Emscripten's
// syscall emulation (bridged to Node's net/dgram modules when node-hosted, the target this was
// built and tested against) caps out at select()/poll() -- there is no epoll/kqueue/IOCP
// equivalent to prefer, so netPlatformCreateQueue() below has no backend-selection branch at all,
// unlike unix_net.c/win_net.c.

bool netPlatformInit(void)
{
    // A write to a peer-closed stream socket raises SIGPIPE, whose default disposition kills the
    // process; harmless to arm even if Emscripten's signal() emulation turns out to be a no-op,
    // and every non-blocking send already reports the failure through netLastError() regardless
    // (EPIPE -> NERR_ConnectionReset).
    signal(SIGPIPE, SIG_IGN);
    return true;
}

NetQueue* netPlatformCreateQueue(_In_ const NetQueueConfig* conf)
{
    return (NetQueue*)netqueueselectCreate((NetQueueConfig*)conf);
}

_Use_decl_annotations_
NetSocket* netPlatformCreateSocket(NetSocketType type)
{
    NetSocketPosix* ps = netsocketposixCreate(type);
    return ps ? NetSocket(ps) : NULL;
}

_Use_decl_annotations_
bool netAddrToSockaddr(NetAddr* addr, struct sockaddr_storage* sa, int* sasz)
{
    if (addr->type == NA_IPv4) {
        struct sockaddr_in* in4 = (struct sockaddr_in*)sa;
        memset(in4, 0, sizeof(struct sockaddr_in));
        in4->sin_family = AF_INET;
        in4->sin_port   = htons(addr->port);

        // ipv4[0] is the least significant octet (host order); pack big-endian onto the wire.
        uint32_t be = ((uint32_t)addr->ipv4[3] << 24) | ((uint32_t)addr->ipv4[2] << 16) |
                     ((uint32_t)addr->ipv4[1] << 8) | (uint32_t)addr->ipv4[0];
        in4->sin_addr.s_addr = htonl(be);
        *sasz                = sizeof(struct sockaddr_in);
        return true;
    } else if (addr->type == NA_IPv6) {
        struct sockaddr_in6* in6 = (struct sockaddr_in6*)sa;
        memset(in6, 0, sizeof(struct sockaddr_in6));
        in6->sin6_family = AF_INET6;
        in6->sin6_port   = htons(addr->port);

        // ipv6 is already the on-wire network-order byte string; copy straight through.
        memcpy(in6->sin6_addr.s6_addr, addr->ipv6, 16);

        in6->sin6_scope_id = addr->scope;
        *sasz               = sizeof(struct sockaddr_in6);
        return true;
    }
    return false;
}

_Use_decl_annotations_
bool netAddrFromSockaddr(NetAddr* addr, const struct sockaddr* sa)
{
    memset(addr, 0, sizeof(NetAddr));

    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in* in4 = (const struct sockaddr_in*)sa;
        addr->type                    = NA_IPv4;
        addr->port                    = ntohs(in4->sin_port);

        // Reverse of netAddrToSockaddr: ipv4[0] is the least significant octet.
        uint32_t be   = ntohl(in4->sin_addr.s_addr);
        addr->ipv4[3] = (uint8)(be >> 24);
        addr->ipv4[2] = (uint8)(be >> 16);
        addr->ipv4[1] = (uint8)(be >> 8);
        addr->ipv4[0] = (uint8)be;
        return true;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6* in6 = (const struct sockaddr_in6*)sa;
        addr->type                     = NA_IPv6;
        addr->port                     = ntohs(in6->sin6_port);
        addr->scope                    = in6->sin6_scope_id;

        memcpy(addr->ipv6, in6->sin6_addr.s6_addr, 16);
        return true;
    }
    return false;
}

// Map an errno value to a NetErrorCode. Split out from netLastError() so it can also classify the
// value returned by getsockopt(SO_ERROR) on a completed non-blocking connect.
static NetErrorCode mapErrnoError(int e)
{
    switch (e) {
    case 0:
        return NERR_None;
    case EINPROGRESS:
        return NERR_WouldBlock;
    case EINTR:
        return NERR_Interrupted;
    case ECONNREFUSED:
        return NERR_ConnectionRefused;
    case ETIMEDOUT:
        return NERR_Timeout;
    case ENETUNREACH:
        return NERR_NetworkUnreachable;
    case EHOSTUNREACH:
        return NERR_HostUnreachable;
    case ENETDOWN:
        return NERR_NetworkDown;
    case EADDRINUSE:
        return NERR_AddressInUse;
    case EISCONN:
        return NERR_AlreadyConnected;
    case ENOTCONN:
        return NERR_NotConnected;
    case ECONNRESET:
    case EPIPE:
        return NERR_ConnectionReset;
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
        return NERR_WouldBlock;
#endif
    case EWOULDBLOCK:
        return NERR_WouldBlock;
    default:
        return NERR_Unknown;
    }
}

_Use_decl_annotations_
NetErrorCode netLastError(void)
{
    return mapErrnoError(errno);
}

_Use_decl_annotations_
NetConnectStatus netSockConnect(NetSockHandle h, const NetAddr* addr, NetErrorCode* err)
{
    struct sockaddr_storage sa;
    int sasz = 0;
    if (!netAddrToSockaddr((NetAddr*)addr, &sa, &sasz)) {
        *err = NERR_Unknown;
        return NETCONN_Failed;
    }

    if (connect((int)h, (struct sockaddr*)&sa, (socklen_t)sasz) == 0) {
        *err = NERR_None;
        return NETCONN_Connected;   // connected synchronously (common on loopback)
    }

    if (errno == EINPROGRESS) {
        *err = NERR_None;
        return NETCONN_InProgress;   // pending; the backend watches for completion
    }

    *err = mapErrnoError(errno);
    return NETCONN_Failed;
}

_Use_decl_annotations_
NetErrorCode netSockConnectResult(NetSockHandle h)
{
    int so_err        = 0;
    socklen_t len      = sizeof(so_err);
    if (getsockopt((int)h, SOL_SOCKET, SO_ERROR, &so_err, &len) != 0)
        return netLastError();
    return mapErrnoError(so_err);
}

_Use_decl_annotations_
NetErrorCode netPlatformResolve(strref host, uint16 port, sa_NetAddr* out)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;   // return both IPv4 and IPv6 candidates
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_ADDRCONFIG;   // suppress a family the host has no address for at all

    // A NULL/empty host resolves to loopback (no AI_PASSIVE), which is what a bare connect wants.
    const char* node = strEmpty(host) ? NULL : strC(host);

    struct addrinfo* res = NULL;
    int rc                = getaddrinfo(node, NULL, &hints, &res);
    if (rc != 0)
        return NERR_HostUnreachable;

    // Push every resolved address in the order the resolver returned them; the connect state
    // machine tries them sequentially. The service was left NULL, so set the caller's port
    // explicitly.
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        NetAddr a;
        if (netAddrFromSockaddr(&a, ai->ai_addr)) {
            a.port = port;
            saPush(out, NetAddr, a);
        }
    }
    freeaddrinfo(res);

    return saSize(*out) > 0 ? NERR_None : NERR_HostUnreachable;
}

_Use_decl_annotations_
uint32 netPlatformIfNameToIndex(const char* name)
{
    // Emscripten provides if_nametoindex; whether it knows any real interfaces depends on the
    // host environment. Returns 0 for an unknown name, same as the other platforms.
    return (uint32)if_nametoindex(name);
}

_Use_decl_annotations_
intptr netSockRecv(NetSockHandle h, void* buf, size_t len, NetErrorCode* err)
{
    ssize_t n = recv((int)h, buf, len, 0);
    if (n < 0) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return n;   // 0 means the peer closed the connection cleanly
}

_Use_decl_annotations_
intptr netSockRecvFrom(NetSockHandle h, void* buf, size_t len, NetAddr* from, NetErrorCode* err)
{
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);

    ssize_t n = recvfrom((int)h, buf, len, 0, (struct sockaddr*)&sa, &salen);
    if (n < 0) {
        *err = netLastError();
        return -1;
    }

    if (from)
        netAddrFromSockaddr(from, (struct sockaddr*)&sa);
    *err = NERR_None;
    return n;   // 0 is a legitimate zero-length datagram here, not a shutdown
}

_Use_decl_annotations_
intptr netSockSendv(NetSockHandle h, const BufIov* iov, size_t niov, NetErrorCode* err)
{
    // No writev()/scatter-gather here (see wasm_net.h) -- one send() per entry, in order, stopping
    // at the first short send or error. This mirrors a partial writev() from the caller's point of
    // view: it returns however many bytes actually made it out, and socket.c's send path already
    // handles a partial result by leaving the rest queued for the next call.
    if (niov == 0) {
        *err = NERR_None;
        return 0;
    }

    intptr total = 0;
    for (size_t i = 0; i < niov; i++) {
        if (iov[i].len == 0)
            continue;

        ssize_t sent = send((int)h, iov[i].data, iov[i].len, 0);
        if (sent < 0) {
            if (total > 0)
                break;   // keep what already went out; the error surfaces on the next call instead
            *err = netLastError();
            return -1;
        }

        total += sent;
        if ((size_t)sent < iov[i].len)
            break;   // short send: the OS send buffer is full, same stopping point as a short writev
    }

    *err = NERR_None;
    return total;
}

_Use_decl_annotations_
intptr netSockSendTo(NetSockHandle h, const void* buf, size_t len, const NetAddr* dest,
                     NetErrorCode* err)
{
    struct sockaddr_storage sa;
    int sasz = 0;
    if (!netAddrToSockaddr((NetAddr*)dest, &sa, &sasz)) {
        *err = NERR_Unknown;
        return -1;
    }

    ssize_t n = sendto((int)h, buf, len, 0, (struct sockaddr*)&sa, (socklen_t)sasz);
    if (n < 0) {
        *err = netLastError();
        return -1;
    }
    *err = NERR_None;
    return n;
}
