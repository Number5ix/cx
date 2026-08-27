#pragma once

#include "addr.h"
#include "net/flow.h"
#include "net/queue.h"
#include "net/socket.h"
#include <cx/time/clock.h>
#include <cx/utils/lazyinit.h>

extern LazyInitState _netInit_done;
void _netInit(void* unused);

bool netPlatformInit(void);
NetQueue* netPlatformCreateQueue(_In_ const NetQueueConfig* conf);

#if defined(_PLATFORM_WIN)
// Create the native completion-port backend directly, bypassing the netPlatformCreateQueue backend
// selection. Returns NULL if IOCP declines (under Wine, where it is emulated over the readiness
// path with no benefit -- see osIsWine() in cx/platform/win/win_os.h). Used by the IOCP test suite
// to exercise the backend explicitly regardless of what the default selection would pick. Windows-
// only: there is no completion-port concept on Unix, so this does not exist there even as a stub --
// code that needs to run on both must go through netPlatformCreateQueue() and NQ_SelectOnly instead.
_Ret_maybenull_ NetQueue* netPlatformCreateIOCP(_In_ const NetQueueConfig* conf);
#endif

// Create a platform socket of the given type (NetSocketWin, NetSocketPosix, ...). The shared
// NetQueueSelect calls this for its socket() factory so it does not have to know the concrete
// per-platform socket class.
_Ret_maybenull_ NetSocket* netPlatformCreateSocket(NetSocketType type);

// ---------------------------------------------------------------------------------------------
// Platform socket shims
//
// Thin wrappers over the OS socket calls, implemented per platform (win_net.c / unix_net.c).
// They exist so the backends in cx/net/ can move bytes and classify errors without including
// winsock2.h or sys/socket.h -- SOCKET vs int and WSAGetLastError vs errno are the whole of the
// difference, and it lives here rather than being duplicated in every backend.
// ---------------------------------------------------------------------------------------------

// Map the last socket error the OS recorded on this thread to a NetErrorCode. NERR_WouldBlock is
// the common, non-fatal one on a readiness backend; NERR_Interrupted asks for a retry. Call
// immediately after a failed socket operation, before anything else can overwrite it.
NetErrorCode netLastError(void);

// Outcome of starting a non-blocking connect(), classified so the portable state machine does not
// have to test WSAEWOULDBLOCK / EINPROGRESS itself.
typedef enum {
    NETCONN_Connected  = 0,   // connect completed synchronously (common on loopback)
    NETCONN_InProgress = 1,   // connect is pending; wait for writability / completion
    NETCONN_Failed     = 2,   // connect failed synchronously; *err carries the reason
} NetConnectStatus;

// Begin a non-blocking connect() on an already-reset handle of the right family. Returns whether
// the connect resolved immediately, is pending, or failed; *err is set on failure.
NetConnectStatus netSockConnect(NetSockHandle h, _In_ const NetAddr* addr, _Out_ NetErrorCode* err);

// Read the result of a completed non-blocking connect via getsockopt(SO_ERROR), mapped to a
// NetErrorCode (NERR_None on success). Used by readiness backends when a connecting socket signals
// writable or excepted.
NetErrorCode netSockConnectResult(NetSockHandle h);

// Close a socket's current OS handle and replace it with a fresh non-blocking handle of `family`,
// updating sock->handle. `bindAny` binds the new handle to the wildcard address of that family,
// which ConnectEx requires. A connect attempt needs a fresh handle because a socket with a
// failed/pending connect cannot be reliably reconnected, and a resolved list can mix families.
// Returns false on failure (handle left INVALID). Implemented per platform (win_net_socket.c).
bool netPlatformResetSocket(_Inout_ NetSocket* sock, NetAddrType family, bool bindAny);

// Resolve a host/port to a list of addresses via the platform name service (getaddrinfo). Runs on
// a resolver worker thread, never a net I/O thread. Pushes each result into `out` in the order the
// resolver returned them, with the port filled in. Returns NERR_None on success. Implemented per
// platform (win_net.c).
NetErrorCode netPlatformResolve(_In_opt_ strref host, uint16 port, _Inout_ sa_NetAddr* out);

// Look up the OS interface index for a named network interface, for IPv6 zone IDs like
// "fe80::1%eth0". Returns 0 if the name is unknown or the platform cannot do the lookup (numeric
// zone IDs never come through here; netAddrFromStr parses those itself). Implemented per platform
// (win_net.c).
uint32 netPlatformIfNameToIndex(_In_z_ const char* name);

// Receive from a connected (stream) socket into buf. Returns the byte count, 0 on an orderly peer
// shutdown, or -1 on error -- in which case *err carries the classified reason (NERR_WouldBlock
// when nothing was ready after all). Never blocks; the socket is non-blocking.
intptr netSockRecv(NetSockHandle h, _Out_writes_bytes_(len) void* buf, size_t len,
                   _Out_ NetErrorCode* err);

// Receive one datagram into buf and report its source address. Same return convention as
// netSockRecv(), except 0 is a legitimate zero-length datagram rather than a shutdown.
intptr netSockRecvFrom(NetSockHandle h, _Out_writes_bytes_(len) void* buf, size_t len,
                       _Out_ NetAddr* from, _Out_ NetErrorCode* err);

// Largest scatter/gather array the send path builds on the stack before a syscall. A neutral bound
// shared by the portable gather in socket.c and each platform's vector translation; the platform
// NetPlatIov arrays are sized to match.
#define NET_MAX_IOV 64

// Send from a scatter/gather vector on a connected (stream) socket. Returns the number of bytes the
// OS accepted (>= 0, possibly a partial write), or -1 with *err set -- NERR_WouldBlock when the send
// buffer is full. Never blocks. The neutral BufIov entries are translated into the platform's own
// vector type inside the shim, so no winsock/uio type escapes cx/net/.
intptr netSockSendv(NetSockHandle h, _In_reads_(niov) const BufIov* iov, size_t niov,
                    _Out_ NetErrorCode* err);

// Send one datagram to dest. Returns bytes sent, or -1 with *err set (NERR_WouldBlock when the send
// buffer is full). Never blocks.
intptr netSockSendTo(NetSockHandle h, _In_reads_bytes_(len) const void* buf, size_t len,
                     _In_ const NetAddr* dest, _Out_ NetErrorCode* err);

// ---------------------------------------------------------------------------------------------
// Select set (NetSelectSet)
//
// The one genuinely select-specific platform primitive behind the shared NetQueueSelect. Windows
// fd_set is an array of handles, Unix fd_set a bitmask indexed by fd; the API iterates ready
// handles (O(n) on both) rather than querying per socket (O(n^2) on Windows), and hides the wake
// mechanism, which has no portable shape -- a loopback UDP pair on Windows, a self-pipe/eventfd on
// Unix. Implemented in win_net_select.c / unix_net_select.c. Opaque by design; only the platform
// file sees the fd_set members.
// ---------------------------------------------------------------------------------------------

typedef struct NetSelectSet NetSelectSet;

// Create an empty select set with its wake mechanism armed, or NULL on failure.
_Ret_maybenull_ NetSelectSet* nselCreate(void);

// Destroy a select set and NULL the handle.
void nselDestroy(_Inout_ NetSelectSet** set);

// Drop every socket from the set (but keep the wake mechanism). Called at the top of each loop
// before re-adding the current sockets.
void nselClear(_Inout_ NetSelectSet* set);

// Add a socket to the set, watching it for readability, writability, or both.
void nselAdd(_Inout_ NetSelectSet* set, NetSockHandle h, bool read, bool write);

// Wait until at least one socket is ready, the timeout elapses, or nselWake() interrupts. The
// timeout is in cx microseconds, which select's timeval carries exactly; timeForever blocks until
// something happens. Returns the number of ready sockets, 0 on timeout, or -1 on error.
int nselWait(_Inout_ NetSelectSet* set, int64 timeoutUs);

// Iterate the sockets reported ready by the last nselWait(). Writes the handle and its
// read/write readiness and returns true, or returns false when iteration is exhausted. Each ready
// handle is reported exactly once.
_Success_(return) bool nselNext(_Inout_ NetSelectSet* set, _Out_ NetSockHandle* h, _Out_ bool* r,
                                _Out_ bool* w);

// Interrupt a blocked nselWait() from another thread. Idempotent; extra wakes coalesce.
void nselWake(_Inout_ NetSelectSet* set);

// ---------------------------------------------------------------------------------------------
// Messages
//
// One NetMessage header per packet in flight, drawn from the queue's NetPool (see pool.cxh) so
// that the steady-state datagram path allocates nothing. The kinds and flags below are internal;
// the allocate/retire API is public on the pool object.
//
// The rest of the internal net API lives on the classes themselves, as underscore-prefixed private
// methods -- see the PRIVATE IMPLEMENTATION DETAILS sections of flow.cxh, socket.cxh, and
// queue.cxh. Only the platform shims above, the standalone helpers below, and the resolver remain
// here.
// ---------------------------------------------------------------------------------------------

// What a NetMessage represents (stored in NetMessage.kind). Everything except NMSG_Data is
// internal plumbing riding the flow inbox so its event lands on a worker, ordered behind the
// packets already queued for the flow; drainFlow() translates each into a NetEvent and retires
// the message before any handler runs. Applications only ever see an NMSG_Data message, and only
// as the datagram container on NetEvent.recv.msg / refused.msg.
typedef enum {
    NMSG_Data         = 0,   // an ordinary packet or chunk of stream data
    NMSG_Terminal     = 1,   // flow teardown marker; deliver NET_FlowClosed (cause in `reason`)
    NMSG_SendReady    = 2,   // send buffer drained below the low watermark; deliver NET_SendReady
    NMSG_Connect      = 3,   // connect attempt resolved; deliver NET_Connection (NetErrorCode in `bytes`)
    NMSG_Accept       = 4,   // connection accepted on a listener; deliver NET_Accepted (socket in `asock`)
    NMSG_Error        = 5,   // a queued send failed asynchronously; deliver NET_Error (NetErrorCode in `bytes`)
    NMSG_FlowOpen     = 6,   // the flow was just created; deliver NET_FlowOpen ahead of its first packet
    NMSG_FilterNotify = 7,   // a filter raised a notification off-worker; deliver NET_FilterNotify
                             // (NetFilterNotify in `bytes`)
    NMSG_Timer        = 8    // an application timer reached its deadline; deliver NET_Timer
                             // (NetTimerId in `timerId`)
} NetMessageKind;

// Bits in NetMessage.flags. The only thing the message layer needs to remember about a payload is
// where it has to go back to: a packet from the wire and a filter's own output both come from the
// queue's buffer pool, while an oversized send payload is a plain heap buffer. Getting this wrong is
// not a crash but a slow leak of pool capacity (bufDestroy on a pooled buffer shrinks the pool for
// good), so it travels with the message rather than being inferred from which path frees it.
typedef enum {
    NMF_PoolBuf = 0x01   // `buf` came from the queue's receive pool; return it there
} NetMessageFlags;

// Most messages a filter chain stages are small (a handshake flight, an MTU-sized fragment), so the
// staging queue in front of a datagram chain is bounded by count rather than bytes. A stage that
// refuses to consume this many application messages is negotiating (or wedged); either way the
// honest answer to the next send is the same refusal the send watermark gives.
#define NET_FLOW_ENCQ_MAX 64

// ---------------------------------------------------------------------------------------------
// Shared inline helpers
//
// Small enough that the indirect call would cost more than the body, so they stay here as inlines
// rather than becoming private methods on the classes.
// ---------------------------------------------------------------------------------------------

// The queue's message pool, or NULL when there is no queue -- which is the state of a socket that
// was never added to one. Every NetPool entry point takes a NULL pool and falls back to the
// heap, so paths that can run either way need no branch of their own.
_Ret_maybenull_ _meta_inline NetPool* _netqueuePool(_In_opt_ NetQueue* q)
{
    return q ? q->pool : NULL;
}

// Coarse monotonic tick (~1.05s units) for flow->lastActive. 32 bits so the per-packet relaxed
// store stays a plain atomic on x86, which has no 64-bit atomics. Direct < comparison is safe --
// the value does not wrap until clockTimer() passes 2^52 microseconds (~143 years).
_meta_inline uint32 _netLruTick(void)
{
    return (uint32)(clockTimer() >> 20);
}

// True once shutdown has begun. Ingest paths check this to stop producing new work.
_meta_inline bool _netqueueShuttingDown(_In_ NetQueue* q)
{
    return atomicLoad(uint32, &q->shutdownReq, Acquire) != 0;
}

// ---------------------------------------------------------------------------------------------
// Backend poll timing (see queue.c)
// ---------------------------------------------------------------------------------------------

// Work out how long a backend's poll call may sleep, given the wait its caller asked for. Both
// take and return cx microseconds unless the name says otherwise: 0 means return immediately and
// timeForever means block until something happens.
//
// The answer is capped to the queue's nearest armed timer deadline, so a timer fires close to when
// it is due rather than on the next unrelated wakeup, and to a little under 24 days otherwise so
// every backend's own timeout type can hold it. Call either one immediately before the poll, since
// the cap is computed against the clock as of the call.
//
// Use this one for select and kqueue, whose timeout structs carry sub-millisecond resolution.
int64 netqueue_pollTimeout(_In_ NetQueue* q, int64 waitUs);

// Same, but in milliseconds, with -1 meaning block forever. For epoll and IOCP, which take a plain
// millisecond count and cannot express anything finer. A nonzero wait rounds up, never down, since
// truncating a short sleep to 0 would turn the caller's loop into a spin.
int64 netqueue_pollTimeoutMsec(_In_ NetQueue* q, int64 waitUs);

// ---------------------------------------------------------------------------------------------
// Name resolution (see resolver.c)
// ---------------------------------------------------------------------------------------------

// Callback delivered on a resolver worker thread once getaddrinfo finishes. `addrs` is the resolved
// list (in returned order) or NULL/empty on failure; `err` is NERR_None on success. The callback
// must not run application code inline -- it feeds the result back into the connect state machine,
// which delivers events through the flow.
typedef void (*NetResolveCB)(_In_opt_ sa_NetAddr* addrs, NetErrorCode err, _In_opt_ void* ctx);

// Submit an async resolution to the dedicated, bounded resolver queue (lazily created on first
// use, capped at a few concurrent getaddrinfo calls, torn down at exit). Returns false if the
// request could not be queued, in which case the callback will not run.
_Check_return_ bool _netResolveSubmit(_In_opt_ strref host, uint16 port, _In_ NetResolveCB cb,
                                      _In_opt_ void* ctx);

// ---------------------------------------------------------------------------------------------
// Accept (see socket.c)
// ---------------------------------------------------------------------------------------------

// Accept one pending connection off a listening socket's backlog. On success wraps the accepted OS
// handle in a platform NetSocket (NST_Stream, NS_Connected), returns it through `*out` owning one
// reference, fills `*peer` with the remote address, and returns NERR_None. Returns NERR_WouldBlock
// when the backlog is drained (nothing more to accept this readiness), or another code on a real
// error. Implemented per platform (win_net_socket.c); the readiness backend drains it in a loop.
NetErrorCode netPlatformAccept(NetSockHandle listener, _Outptr_result_maybenull_ NetSocket** out,
                               _Out_ NetAddr* peer);
