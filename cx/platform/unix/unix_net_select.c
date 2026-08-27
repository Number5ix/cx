#include "unix_net.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

// Unix NetSelectSet: fd_set is a bitmask indexed by fd, not Windows' array of handles, so there is
// no fd_count/fd_array to iterate. The set instead tracks every added handle itself (a plain growable
// array) and iterates that against FD_ISSET on the ready working sets, which is O(n) total exactly
// like the Windows array walk. POSIX signals a failed connect through writability + SO_ERROR rather
// than a separate except set, so there is only a read/write pair here, not read/write/except.

typedef struct NetSelectSet {
    fd_set masterRead;
    fd_set masterWrite;
    fd_set workRead;
    fd_set workWrite;
    int maxfd;   // highest fd currently in either master set, for select()'s nfds argument

    // Every handle nselAdd() was given this pass, so nselNext can iterate a short list with
    // FD_ISSET instead of scanning the whole bitmask up to maxfd.
    sa_intptr handles;

    size_t iterIdx;

    // Self-pipe used to interrupt a blocked select(). wakeRead is always in masterRead, so a byte
    // written to wakeWrite from any thread makes select() return; the byte is drained after.
    int wakeRead;
    int wakeWrite;
} NetSelectSet;

static void setNonBlocking(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0)
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

// Build the self-pipe. Returns false and leaves both ends at -1 on failure, which nselCreate
// treats as fatal -- a select set with no wake cannot be shut down cleanly.
static bool makeWakePipe(NetSelectSet* set)
{
    int fds[2];
    if (pipe(fds) != 0) {
        set->wakeRead  = -1;
        set->wakeWrite = -1;
        return false;
    }

    setNonBlocking(fds[0]);
    setNonBlocking(fds[1]);

    set->wakeRead  = fds[0];
    set->wakeWrite = fds[1];
    return true;
}

_Ret_maybenull_ NetSelectSet* nselCreate(void)
{
    NetSelectSet* set = xaAlloc(sizeof(NetSelectSet), XA_Zero);

    if (!makeWakePipe(set)) {
        xaFree(set);
        return NULL;
    }

    saInit(&set->handles, intptr, 16);
    nselClear(set);
    return set;
}

void nselDestroy(_Inout_ NetSelectSet** set)
{
    NetSelectSet* s = *set;
    if (!s)
        return;

    if (s->wakeRead >= 0)
        close(s->wakeRead);
    if (s->wakeWrite >= 0)
        close(s->wakeWrite);

    saDestroy(&s->handles);
    xaFree(s);
    *set = NULL;
}

void nselClear(_Inout_ NetSelectSet* set)
{
    FD_ZERO(&set->masterRead);
    FD_ZERO(&set->masterWrite);
    saClear(&set->handles);

    // The wake pipe is always watched for read, so a wake queued between loops is never missed.
    FD_SET(set->wakeRead, &set->masterRead);
    set->maxfd = set->wakeRead;
}

void nselAdd(_Inout_ NetSelectSet* set, NetSockHandle h, bool read, bool write)
{
    int fd = (int)h;

    // POSIX fd_set is a bitmask the kernel indexes by fd number; an fd >= FD_SETSIZE would write
    // out of bounds rather than merely failing to track a socket, unlike Windows' array-based
    // fd_set which just has a smaller-than-requested capacity. So this is a hard cap: the socket is
    // silently left unwatched this pass rather than corrupting the set. The design accepts select
    // as a fallback (epoll is the default on Linux) precisely because of this asymmetry.
    if (fd < 0 || fd >= FD_SETSIZE)
        return;

    if (!read && !write)
        return;

    if (read)
        FD_SET(fd, &set->masterRead);
    if (write)
        FD_SET(fd, &set->masterWrite);

    if (fd > set->maxfd)
        set->maxfd = fd;

    saPush(&set->handles, intptr, (intptr)fd);
}

int nselWait(_Inout_ NetSelectSet* set, int64 timeoutUs)
{
    set->workRead  = set->masterRead;
    set->workWrite = set->masterWrite;

    struct timeval tv;
    struct timeval* ptv = NULL;
    if (timeoutUs < timeForever) {
        tv.tv_sec  = (long)(timeoutUs / 1000000);
        tv.tv_usec = (long)(timeoutUs % 1000000);
        ptv        = &tv;
    }

    set->iterIdx = 0;

    int n = select(set->maxfd + 1, &set->workRead, &set->workWrite, NULL, ptv);
    if (n < 0) {
        // A signal interrupting the wait is not a real error; report no readiness and let the
        // caller loop back into another wait.
        return (errno == EINTR) ? 0 : -1;
    }

    // Drain the wake pipe and hide it from the caller. It never counts as an application-ready
    // socket, so subtract it from the reported count when it fired.
    if (FD_ISSET(set->wakeRead, &set->workRead)) {
        char sink[64];
        while (read(set->wakeRead, sink, sizeof(sink)) > 0)
            ;
        FD_CLR(set->wakeRead, &set->workRead);
        if (n > 0)
            n--;
    }

    return n;
}

_Success_(return) bool nselNext(_Inout_ NetSelectSet* set, _Out_ NetSockHandle* h, _Out_ bool* r,
                                _Out_ bool* w)
{
    while (set->iterIdx < (size_t)saSize(set->handles)) {
        int fd = (int)set->handles.a[set->iterIdx++];

        bool rr = FD_ISSET(fd, &set->workRead) ? true : false;
        bool ww = FD_ISSET(fd, &set->workWrite) ? true : false;
        if (!rr && !ww)
            continue;

        *h = (NetSockHandle)fd;
        *r = rr;
        *w = ww;
        return true;
    }

    return false;
}

void nselWake(_Inout_ NetSelectSet* set)
{
    // One byte is enough; a non-blocking write that fails because the pipe is already full just
    // means a wake is already pending, which is exactly the coalescing we want, so the result is
    // deliberately not checked.
    char b       = 1;
    ssize_t wrote = write(set->wakeWrite, &b, 1);
    unused_noeval(wrote);
}
