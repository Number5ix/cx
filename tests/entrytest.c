// A program built with _CX_DEFINE_ENTRY_POINT_RUNNER: the runner keeps the main thread and runs
// entryPoint() on another one, the way a UI library hands the main thread to its event loop.
//
// It exits 0 only if the runner was called, entryPoint() saw the parsed arguments ("alpha" and
// "beta gamma", passed by ctest) on a thread other than main, and the runner's return value became
// the exit code.

#include <cx/string.h>
#include <cx/sys/entry.h>
#include <cx/thread.h>
#include <stdio.h>

#define ENTRY_OK 7

static intptr mainThread;
static int entryResult = -1;

static int entryThread(Thread* thr)
{
    int (*entry)(void) = (int (*)(void))stvlAtPtr(&thr->args, 0);
    entryResult        = entry();
    return 0;
}

static int threadRunner(int (*entry)(void))
{
    mainThread  = thrCurrentOSThreadID();
    Thread* thr = thrCreate(entryThread, _S"entry", stvar(ptr, (void*)entry));
    if (!thr || !thrWait(thr, timeS(30)))
        return 2;
    thrRelease(&thr);

    // the entry's result only becomes a success after passing through here
    return entryResult == ENTRY_OK ? 0 : 3;
}

int entryPoint(void)
{
    if (mainThread == 0 || thrCurrentOSThreadID() == mainThread) {
        fprintf(stderr, "entryPoint ran on the main thread\n");
        return 4;
    }
    if (saSize(cmdArgs) != 2 || !strEq(cmdArgs.a[0], _SL("alpha")) ||
        !strEq(cmdArgs.a[1], _SL("beta gamma"))) {
        fprintf(stderr, "unexpected arguments (%d)\n", saSize(cmdArgs));
        return 5;
    }
    if (strEmpty(cmdProgram)) {
        fprintf(stderr, "no program name\n");
        return 6;
    }
    return ENTRY_OK;
}

_CX_DEFINE_ENTRY_POINT_RUNNER(threadRunner)
