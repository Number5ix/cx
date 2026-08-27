#include "win_net.h"

// Windows NetSelectSet: fd_set is a struct { u_int fd_count; SOCKET fd_array[FD_SETSIZE]; }, an
// array of handles rather than a bitmask, so FD_SETSIZE only sizes the array and a socket count up
// to that bound is safe. The set keeps a master read/write pair that the caller rebuilds each
// loop, copies them into working sets that select() overwrites in place with the ready subset, and
// iterates that subset directly out of fd_array.

typedef struct NetSelectSet {
    fd_set masterRead;
    fd_set masterWrite;
    fd_set masterExcept;
    fd_set workRead;
    fd_set workWrite;
    fd_set workExcept;

    // Iteration cursor over the ready subset produced by the last nselWait(). Phase 0 walks the
    // ready read set (reporting write-readiness alongside via FD_ISSET on the write and except
    // sets); phase 1 walks the ready write set and phase 2 the ready except set, each skipping
    // handles already reported by an earlier phase. The except set carries failed connects, which
    // Windows signals through exceptfds rather than writefds.
    int iterPhase;
    u_int iterIdx;

    // Loopback UDP pair used to interrupt a blocked select(). wakeRecv is always in masterRead, so
    // a byte sent to it from any thread makes select() return; the bytes are drained after.
    SOCKET wakeRecv;
    SOCKET wakeSend;
} NetSelectSet;

// Build the self-connected loopback UDP pair. Returns false and leaves both sockets INVALID on
// failure, which nselCreate treats as fatal -- a select set with no wake cannot be shut down.
static bool makeWakePair(NetSelectSet* set)
{
    set->wakeRecv = INVALID_SOCKET;
    set->wakeSend = INVALID_SOCKET;

    SOCKET r = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (r == INVALID_SOCKET || s == INVALID_SOCKET)
        goto fail;

    struct sockaddr_in loop;
    memset(&loop, 0, sizeof(loop));
    loop.sin_family      = AF_INET;
    loop.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    loop.sin_port        = 0;   // ephemeral

    if (bind(r, (struct sockaddr*)&loop, sizeof(loop)) != 0)
        goto fail;

    // Learn the port the OS picked and point the send socket at it, so nselWake is a bare send().
    int len = sizeof(loop);
    if (getsockname(r, (struct sockaddr*)&loop, &len) != 0)
        goto fail;
    if (connect(s, (struct sockaddr*)&loop, sizeof(loop)) != 0)
        goto fail;

    u_long nonblocking = 1;
    ioctlsocket(r, FIONBIO, &nonblocking);
    ioctlsocket(s, FIONBIO, &nonblocking);

    set->wakeRecv = r;
    set->wakeSend = s;
    return true;

fail:
    if (r != INVALID_SOCKET)
        closesocket(r);
    if (s != INVALID_SOCKET)
        closesocket(s);
    return false;
}

_Ret_maybenull_ NetSelectSet* nselCreate(void)
{
    NetSelectSet* set = xaAlloc(sizeof(NetSelectSet), XA_Zero);

    if (!makeWakePair(set)) {
        xaFree(set);
        return NULL;
    }

    nselClear(set);
    return set;
}

void nselDestroy(_Inout_ NetSelectSet** set)
{
    NetSelectSet* s = *set;
    if (!s)
        return;

    if (s->wakeRecv != INVALID_SOCKET)
        closesocket(s->wakeRecv);
    if (s->wakeSend != INVALID_SOCKET)
        closesocket(s->wakeSend);

    xaFree(s);
    *set = NULL;
}

void nselClear(_Inout_ NetSelectSet* set)
{
    FD_ZERO(&set->masterRead);
    FD_ZERO(&set->masterWrite);
    FD_ZERO(&set->masterExcept);

    // The wake socket is always watched for read, so a wake queued between loops is never missed.
    FD_SET(set->wakeRecv, &set->masterRead);
}

void nselAdd(_Inout_ NetSelectSet* set, NetSockHandle h, bool read, bool write)
{
    SOCKET s = (SOCKET)h;
    if (read)
        FD_SET(s, &set->masterRead);
    if (write) {
        FD_SET(s, &set->masterWrite);
        // Windows reports a failed connect through exceptfds, not writefds, so write interest is
        // mirrored into the except set. The shared layer keys connect-vs-send on the socket's
        // state, so an ordinary writable socket also appearing here is harmless.
        FD_SET(s, &set->masterExcept);
    }
}

int nselWait(_Inout_ NetSelectSet* set, int64 timeoutUs)
{
    set->workRead   = set->masterRead;
    set->workWrite  = set->masterWrite;
    set->workExcept = set->masterExcept;

    struct timeval tv;
    struct timeval* ptv = NULL;
    if (timeoutUs < timeForever) {
        tv.tv_sec  = (long)(timeoutUs / 1000000);
        tv.tv_usec = (long)(timeoutUs % 1000000);
        ptv        = &tv;
    }

    // The first argument is ignored by Winsock; fd_count in each set bounds the scan. The except set
    // carries failed connects (Windows signals those through exceptfds).
    int n = select(0, &set->workRead, set->workWrite.fd_count ? &set->workWrite : NULL,
                   set->workExcept.fd_count ? &set->workExcept : NULL, ptv);

    // Reset the iteration cursor regardless of outcome so a stale nselNext after a timeout or
    // error reports nothing.
    set->iterPhase = 0;
    set->iterIdx   = 0;

    if (n == SOCKET_ERROR)
        return -1;

    // Drain the wake socket and hide it from the caller. It never counts as an application-ready
    // socket, so subtract it from the reported count when it fired.
    if (FD_ISSET(set->wakeRecv, &set->workRead)) {
        char sink[64];
        while (recv(set->wakeRecv, sink, sizeof(sink), 0) > 0)
            ;
        FD_CLR(set->wakeRecv, &set->workRead);
        if (n > 0)
            n--;
    }

    return n;
}

_Success_(return) bool nselNext(_Inout_ NetSelectSet* set, _Out_ NetSockHandle* h, _Out_ bool* r,
                                _Out_ bool* w)
{
    // Phase 0: every read-ready socket, once, carrying its write/except readiness too. A socket in
    // the except set is reported as write-ready (w = true) -- the shared layer disambiguates a
    // connect success (writable) from a connect failure (excepted) via SO_ERROR.
    if (set->iterPhase == 0) {
        if (set->iterIdx < set->workRead.fd_count) {
            SOCKET s = set->workRead.fd_array[set->iterIdx++];
            *h       = (NetSockHandle)s;
            *r       = true;
            *w       = (FD_ISSET(s, &set->workWrite) || FD_ISSET(s, &set->workExcept)) ? true : false;
            return true;
        }
        set->iterPhase = 1;
        set->iterIdx   = 0;
    }

    // Phase 1: write-ready sockets not already reported as read-ready.
    if (set->iterPhase == 1) {
        while (set->iterIdx < set->workWrite.fd_count) {
            SOCKET s = set->workWrite.fd_array[set->iterIdx++];
            if (FD_ISSET(s, &set->workRead))
                continue;   // already emitted in phase 0
            *h = (NetSockHandle)s;
            *r = false;
            *w = true;
            return true;
        }
        set->iterPhase = 2;
        set->iterIdx   = 0;
    }

    // Phase 2: excepted sockets (failed connects) not already reported in phase 0 or 1.
    while (set->iterIdx < set->workExcept.fd_count) {
        SOCKET s = set->workExcept.fd_array[set->iterIdx++];
        if (FD_ISSET(s, &set->workRead) || FD_ISSET(s, &set->workWrite))
            continue;   // already emitted
        *h = (NetSockHandle)s;
        *r = false;
        *w = true;
        return true;
    }

    return false;
}

void nselWake(_Inout_ NetSelectSet* set)
{
    // One byte is enough; a non-blocking send that fails because the buffer is already full just
    // means a wake is already pending, which is exactly the coalescing we want.
    char b = 1;
    send(set->wakeSend, &b, 1, 0);
}
