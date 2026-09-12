#ifdef __STRICT_ANSI__
#undef __STRICT_ANSI__
#endif

#include "cx/sys/process_private.h"
#include "cx/container/foreach.h"
#include "cx/debug/error.h"
#include "cx/platform/os.h"
#include "cx/platform/unix.h"
#include "cx/platform/unix/unix_sys_processobj.h"
#include "cx/string.h"
#include "cx/sys/env.h"
#include "cx/thread/atomic.h"
#include "cx/thread/mutex.h"
#include "cx/time/clock.h"
#include "cx/utils/compare.h"
#include "cx/utils/lazyinit.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

// The fd the child reports a failed exec on. Chosen above the three standard descriptors and
// closed by the kernel at exec, so its closing is what tells the parent exec succeeded.
#define PROC_ERRFD 3

extern char** environ;

// Every child cx forks is registered here, whether or not the caller asked to be told when it
// exits, because something has to reap it. The registry holds a reference, which is what makes
// launch-and-forget safe: procLaunch() followed immediately by procRelease() still gets reaped.
//
// Lock order is childLock -> Process::lock, never the reverse.
static Mutex childLock;
static sa_Process children;
static LazyInitState childInitState;

static void childInit(void* data)
{
    mutexInit(&childLock);
    saInit(&children, object, 8);
}

bool _procUnixAlive(ProcessID pid)
{
    if (pid <= 0)
        return false;

    // Signal 0 performs the permission and existence checks without sending anything. EPERM
    // means the process is there but belongs to someone else, which still answers the question.
    if (kill((pid_t)pid, 0) == 0)
        return true;

    return errno == EPERM;
}

// True if this handle's pid still refers to the process it was opened for.
//
// A pid identifies a process only while it is running; once it exits the number is free to be
// given to something unrelated. A handle from procLaunch needs no check -- cx is the process's
// parent, so the id cannot be reused before cx collects it -- but one from procOpen has no such
// protection, and acting on a stale one would mean signalling a stranger.
static bool procUnixSameProcess(Process* proc)
{
    // Nothing recorded to compare against: the start time was unreadable when the handle was
    // opened, so this check cannot say anything either way.
    if (proc->ischild || proc->starttime == 0)
        return true;

    int64 now = 0;
    if (!_procUnixStartTime(proc->pid, &now))
        return false;   // gone, or no longer readable

    return now == proc->starttime;
}

// Cache a finished child's outcome on its handle. After this the status lives entirely in the
// object, which is what lets procExitCode() keep working once the process itself is gone.
static void publishExit(Process* proc, int status)
{
    mutexAcquire(&proc->lock);

    if (WIFEXITED(status)) {
        proc->exitcode   = WEXITSTATUS(status);
        proc->termsignal = 0;
    } else if (WIFSIGNALED(status)) {
        // No exit code exists for a process a signal killed. Report the shell's convention so
        // the caller still gets a number that reflects what happened.
        proc->termsignal = WTERMSIG(status);
        proc->exitcode   = 128 + proc->termsignal;
    }

    atomicStore(bool, &proc->exited, true, Release);
    mutexRelease(&proc->lock);
}

void _procReapPending(void)
{
    lazyInit(&childInitState, childInit, NULL);

    // Handles whose registry reference is being dropped. They are released after childLock is
    // let go: releasing the last reference runs Process destroy, which sweeps again, and this
    // mutex is not reentrant.
    sa_Process done;
    saInit(&done, object, 4);

    withMutex (&childLock) {
        for (int32 i = saSize(children) - 1; i >= 0; i--) {
            Process* proc = children.a[i];
            int status    = 0;

            // Never waitpid(-1): a library that wildcard-waits silently steals the host
            // application's own children. Always this exact pid.
            pid_t r = waitpid((pid_t)proc->pid, &status, WNOHANG);

            if (r == 0)
                continue;   // still running

            if (r < 0 && errno == EINTR)
                continue;

            if (r > 0) {
                publishExit(proc, status);
            } else {
                // Gone, but collected by someone else, so there is no status to report.
                // Publishing the zeroed `status` here would claim a clean exit that never
                // happened -- record only that it finished.
                mutexAcquire(&proc->lock);
                atomicStore(bool, &proc->exited, true, Release);
                mutexRelease(&proc->lock);
            }

            // Acquire into `done` before removing, so the refcount cannot reach zero while the
            // lock is held.
            saPush(&done, object, proc);
            saRemove(&children, i);
        }
    }

    saDestroy(&done);
}

// ---- launching ------------------------------------------------------------------------------

// Copies a string into a freshly allocated NUL-terminated C string. Everything the child needs
// is built with this *before* forking: after fork() in a threaded process only async-signal-safe
// calls are legal, and allocating is not one of them -- another thread may have held the
// allocator's lock at the moment of the fork, and in the child that lock is held forever.
static char* procCStr(strref s)
{
    uint32 len = strLen(s);
    char* ret  = xaAlloc(len + 1);
    strCopyOut(s, 0, (uint8*)ret, len + 1);
    return ret;
}

static void freeCArray(char** arr)
{
    if (!arr)
        return;

    for (char** p = arr; *p; p++) xaFree(*p);
    xaFree(arr);
}

// argv for the child. argv[0] is the executable path as given, which is the conventional thing
// for a program to see, and cx supplies it so call sites do not have to on any platform.
static char** buildArgv(strref exe, sa_string args)
{
    int32 n     = saSize(args);
    char** argv = xaAlloc((size_t)(n + 2) * sizeof(char*), XA_Zero);

    argv[0] = procCStr(exe);
    for (int32 i = 0; i < n; i++) argv[i + 1] = procCStr(args.a[i]);
    argv[n + 1] = NULL;

    return argv;
}

// The child's environment: this process's, plus the caller's overrides, minus the caller's
// removals. Returns NULL when the caller asked for no changes, meaning "inherit exactly".
static char** buildEnvp(const ProcessOpts* opts)
{
    if (!opts || (htSize(opts->env) == 0 && saSize(opts->envUnset) == 0))
        return NULL;

    hashtable env;
    if (!envEnum(&env))
        return NULL;

    foreach (hashtable, it, opts->env) {
        htInsert(&env, strref, htiKey(string, it), strref, htiVal(string, it));
    }

    for (int32 i = 0; i < saSize(opts->envUnset); i++) htRemove(&env, strref, opts->envUnset.a[i]);

    char** envp = xaAlloc((size_t)(htSize(env) + 1) * sizeof(char*), XA_Zero);
    int32 n     = 0;
    string ent  = 0;

    foreach (hashtable, it, env) {
        strNConcat(&ent, htiKey(string, it), _SL("="), htiVal(string, it));
        envp[n++] = procCStr(ent);
    }

    envp[n] = NULL;
    strDestroy(&ent);
    htDestroy(&env);
    return envp;
}

// Close every descriptor at or above `from`. cx sets close-on-exec on nothing, so without this
// the child would inherit every file, socket, epoll fd and self-pipe the parent has open.
static void closeFrom(int from)
{
#if defined(SYS_close_range)
    if (syscall(SYS_close_range, (unsigned)from, ~0U, 0) == 0)
        return;
#endif
#if defined(_PLATFORM_FBSD)
    closefrom(from);
    return;
#endif

    // No bulk primitive: walk what is actually open rather than guessing at a ceiling.
    DIR* d = opendir("/proc/self/fd");
    if (d) {
        int dfd = dirfd(d);
        struct dirent* de;
        while ((de = readdir(d))) {
            int fd = atoi(de->d_name);
            if (fd >= from && fd != dfd)
                close(fd);
        }
        closedir(d);
        return;
    }

    int max = (int)sysconf(_SC_OPEN_MAX);
    if (max < 0)
        max = 4096;
    for (int fd = from; fd < max; fd++) close(fd);
}

// Everything from here to execve runs in the forked child, where only async-signal-safe calls
// are legal. Nothing here allocates: every string the child needs arrives as a C string built
// before the fork. The lone exception is closeFrom()'s opendir() fallback, which is only
// reached on a Linux kernel without close_range.
static _Noreturn void childExec(char** argv, char** envp, const ProcessOpts* opts,
                                const char* workdir, int errfd)
{
    // A host process that ignores SIGPIPE must not impose that on the child, so start from a
    // clean slate: no blocked signals and every handler back to its default.
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, NULL);

    for (int sig = 1; sig < NSIG; sig++) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigaction(sig, &sa, NULL);
    }

    if (errfd != PROC_ERRFD) {
        if (dup2(errfd, PROC_ERRFD) < 0)
            _exit(127);
        close(errfd);
    }

    // dup2 clears FD_CLOEXEC on the new descriptor. Without setting it again the pipe would
    // survive the exec, the parent's read would never see EOF, and the launch would hang.
    fcntl(PROC_ERRFD, F_SETFD, FD_CLOEXEC);

    if (opts && opts->stdio == PROC_StdioNull) {
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO)
                close(devnull);
        }
    }

    closeFrom(PROC_ERRFD + 1);

    if (workdir) {
        if (chdir(workdir) != 0)
            goto failed;
    }

    if (opts && (opts->flags & PROC_Detached))
        setsid();
    else if (opts && (opts->flags & PROC_NewGroup))
        setpgid(0, 0);

    execve(argv[0], argv, envp ? envp : environ);

failed:;
    // Hand the real reason back to the parent, which is what turns a failed exec into a failed
    // launch instead of a child that briefly exists and dies with 127.
    int err = errno;
    ssize_t ignored = write(PROC_ERRFD, &err, sizeof(err));
    (void)ignored;
    _exit(127);
}

_Use_decl_annotations_
Process* _procPlatformLaunch(strref exe, sa_string args, const ProcessOpts* opts)
{
    lazyInit(&childInitState, childInit, NULL);

    int errpipe[2] = { -1, -1 };

    // The read end must not leak into the child, or nothing would ever close it and the parent
    // would block forever waiting for EOF.
    if (pipe2(errpipe, O_CLOEXEC) != 0) {
        if (pipe(errpipe) != 0)
            return (unixMapErrno(), NULL);
        fcntl(errpipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(errpipe[1], F_SETFD, FD_CLOEXEC);
    }

    // Built before the fork; see procCStr.
    char** argv   = buildArgv(exe, args);
    char** envp   = buildEnvp(opts);
    char* workdir = (opts && !strEmpty(opts->workdir)) ? procCStr(opts->workdir) : NULL;

    pid_t pid = fork();

    if (pid < 0) {
        unixMapErrno();
        close(errpipe[0]);
        close(errpipe[1]);
        freeCArray(argv);
        freeCArray(envp);
        xaFree(workdir);
        return NULL;
    }

    if (pid == 0)
        childExec(argv, envp, opts, workdir, errpipe[1]);

    close(errpipe[1]);
    freeCArray(argv);
    freeCArray(envp);
    xaFree(workdir);

    // EOF means the exec succeeded and the kernel closed the pipe. Any bytes are the child's
    // errno, written just before it gave up.
    int childerr = 0;
    ssize_t got;
    do {
        got = read(errpipe[0], &childerr, sizeof(childerr));
    } while (got < 0 && errno == EINTR);
    close(errpipe[0]);

    if (got == (ssize_t)sizeof(childerr)) {
        // Collect the corpse now: this process never became the caller's, so no handle will
        // ever be returned for it and nothing else would reap it.
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

        errno = childerr;
        unixMapErrno();
        return NULL;
    }

    UnixProcess* uproc = _unixprocobjCreate();
    uproc->pid         = (ProcessID)pid;
    uproc->ischild     = true;

    // Exactly one reaper per child. The watcher takes it if it can, and only what it will not
    // take goes on the sweep list -- two reapers racing for the same status means the loser
    // gets ECHILD and the exit code is lost.
    if (!_procWatchRegister(Process(uproc))) {
        withMutex (&childLock) {
            saPush(&children, object, Process(uproc));
        }
    }

    return Process(uproc);
}

bool _procPlatformWait(Process* proc, int64 timeout)
{
    int64 deadline = (timeout == timeForever) ? INT64_MAX : clockTimer() + timeout;

    // Polling, because until the watcher lands there is nothing to be woken by. Starts tight so
    // a short-lived child is noticed immediately and backs off so a long wait costs little.
    int64 nap = timeMS(1);

    for (;;) {
        _procReapPending();

        if (atomicLoad(bool, &proc->exited, Acquire))
            return true;

        // Not our child, so there is no status to collect -- gone is the whole answer.
        if (!proc->ischild && !_procUnixAlive(proc->pid))
            return true;

        int64 now = clockTimer();
        if (now >= deadline)
            return false;

        osSleep(min(nap, deadline - now));
        nap = min(nap * 2, timeMS(20));
    }
}

bool _procPlatformTerminate(Process* proc, bool force)
{
    // Refuse rather than signal whatever inherited the id.
    if (!procUnixSameProcess(proc)) {
        cxerr = CX_FileNotFound;
        return false;
    }

    if (kill((pid_t)proc->pid, force ? SIGKILL : SIGTERM) == 0)
        return true;

    return unixMapErrno();
}

bool _procPlatformExitCode(Process* proc, int32* code)
{
    // Only a parent can collect a child's status, so for anything cx did not fork there is no
    // exit code now and never will be. Saying that plainly is more use than looking like a
    // process that simply has not finished yet.
    if (!proc->ischild)
        cxerr = CX_NotSupported;

    return false;
}

_Use_decl_annotations_
Process* _procPlatformOpen(ProcessID pid)
{
    if (!_procUnixAlive(pid)) {
        cxerr = CX_FileNotFound;
        return NULL;
    }

    UnixProcess* uproc = _unixprocobjCreate();
    uproc->pid         = pid;
    uproc->ischild     = false;

    // Best effort: remembered so a later query can tell this process from a different one given
    // the same id after it exits. Stays 0 if unreadable, which disables the check rather than
    // guessing.
    _procUnixStartTime(pid, &uproc->starttime);

    return Process(uproc);
}

bool _procPlatformRunning(Process* proc)
{
    // A finished child stays alive as a zombie until it is reaped, so ask the reaper first or
    // it would look like it is still running.
    if (proc->ischild) {
        _procReapPending();
        if (atomicLoad(bool, &proc->exited, Acquire))
            return false;
    }

    // A different process wearing the same id is not this one still running.
    if (!procUnixSameProcess(proc))
        return false;

    return _procUnixAlive(proc->pid);
}

_Use_decl_annotations_
ProcessID procCurrentID(void)
{
    return (ProcessID)getpid();
}
