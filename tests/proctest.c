#include <cx/container.h>
#include <cx/cx.h>
#include <cx/debug/error.h>
#include <cx/fs.h>
#include <cx/string.h>
#include <cx/closure.h>
#include <cx/platform/os.h>
#include <cx/thread.h>
#include <cx/sys.h>
#include <cx/time.h>

#define TEST_FILE proctest
#define TEST_FUNCS proctest_funcs
#include "common.h"

// Spelled as _SL() literals rather than STR_CONST, which declares a const strref that a
// hashtable key argument will not take.
#define kEnvVar  _SL("CX_PROCTEST_VAR")
#define kEnvVal1 _SL("value one")
#define kEnvVal2 _SL("value two")

// The same name in two spellings, for pinning down each platform's name matching.
#define kEnvMixed _SL("CX_ProcTest_Case")
#define kEnvUpper _SL("CX_PROCTEST_CASE")

// Checks that a variable reads back with the value it was given.
static void checkEnvVal(int* ret, strref name, strref want)
{
    string got = 0;

    if (!envGet(&got, name)) {
        TEST_FAILV(*ret, 1, _SL("envGet('${string}') reports the variable is not set"),
                   stvar(strref, name));
    } else if (!strEq(got, want)) {
        TEST_FAILV(*ret, 1, _SL("envGet('${string}') == '${string}', wanted '${string}'"),
                   stvar(strref, name), stvar(string, got), stvar(strref, want));
    }

    strDestroy(&got);
}

static int test_proc_env_getset(void)
{
    int ret = 0;

    if (!envSet(kEnvVar, kEnvVal1)) {
        TEST_FAILV(ret, 1, _SL("envSet('${string}', '${string}') failed"), stvar(strref, kEnvVar),
                   stvar(strref, kEnvVal1));
    } else {
        checkEnvVal(&ret, kEnvVar, kEnvVal1);
    }

    // Setting an existing variable replaces its value rather than adding a second one.
    if (!envSet(kEnvVar, kEnvVal2)) {
        TEST_FAILV(ret, 1, _SL("envSet('${string}', '${string}') failed overwriting"),
                   stvar(strref, kEnvVar), stvar(strref, kEnvVal2));
    } else {
        checkEnvVal(&ret, kEnvVar, kEnvVal2);
    }

    if (!envExists(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envExists('${string}') is false for a variable that is set"),
                   stvar(strref, kEnvVar));

    // A variable set to an empty value still exists -- that is the case envUnset is separate for.
    if (!envSet(kEnvVar, _SL(""))) {
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed with an empty value"),
                   stvar(strref, kEnvVar));
    } else {
        checkEnvVal(&ret, kEnvVar, _SL(""));

        if (!envExists(kEnvVar))
            TEST_FAILV(ret, 1, _SL("envExists('${string}') is false for a variable set to empty"),
                       stvar(strref, kEnvVar));
    }

    envUnset(kEnvVar);
    return ret;
}

static int test_proc_env_unset(void)
{
    int ret    = 0;
    string val = 0;

    if (!envSet(kEnvVar, kEnvVal1))
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed"), stvar(strref, kEnvVar));

    if (!envExists(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envExists('${string}') is false right after envSet"),
                   stvar(strref, kEnvVar));

    if (!envUnset(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envUnset('${string}') failed"), stvar(strref, kEnvVar));

    if (envExists(kEnvVar))
        TEST_FAILV(ret, 1, _SL("envExists('${string}') is still true after envUnset"),
                   stvar(strref, kEnvVar));

    // A lookup that finds nothing has to clear the output, not leave the caller's old value.
    strDup(&val, kEnvVal1);
    if (envGet(&val, kEnvVar)) {
        TEST_FAILV(ret, 1, _SL("envGet('${string}') found a value after envUnset"),
                   stvar(strref, kEnvVar));
    } else if (strLen(val) != 0) {
        TEST_FAILV(ret, 1, _SL("envGet left '${string}' in the output after finding nothing"),
                   stvar(string, val));
    }

    strDestroy(&val);
    return ret;
}

static int test_proc_env_enum(void)
{
    int ret = 0;
    hashtable env;
    string val = 0;

    if (!envSet(kEnvVar, kEnvVal1))
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed"), stvar(strref, kEnvVar));

    if (!envEnum(&env)) {
        envUnset(kEnvVar);
        TEST_FAIL(1, _SL("envEnum failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    if (htSize(env) == 0)
        TEST_FAILV(ret, 1, _SL("envEnum returned an empty table, though '${string}' is set"),
                   stvar(strref, kEnvVar));

    if (!htFind(env, strref, kEnvVar, string, &val)) {
        TEST_FAILV(ret, 1, _SL("envEnum's table has no '${string}', though it is set"),
                   stvar(strref, kEnvVar));
    } else if (!strEq(val, kEnvVal1)) {
        TEST_FAILV(ret, 1, _SL("envEnum gave '${string}' for '${string}', wanted '${string}'"),
                   stvar(string, val), stvar(strref, kEnvVar), stvar(strref, kEnvVal1));
    }

    strDestroy(&val);
    htDestroy(&env);
    envUnset(kEnvVar);
    return ret;
}

// Pins down each platform's name matching rather than assuming it: Windows treats the two
// spellings as one variable, Unix treats them as two.
static int test_proc_env_caseinsens(void)
{
    int ret    = 0;
    string val = 0;

    if (!envSet(kEnvMixed, kEnvVal1))
        TEST_FAILV(ret, 1, _SL("envSet('${string}') failed"), stvar(strref, kEnvMixed));

    bool found = envGet(&val, kEnvUpper);

#if defined(_PLATFORM_WIN)
    if (!found) {
        TEST_FAILV(ret, 1, _SL("envGet('${string}') found nothing; Windows should match '${string}'"),
                   stvar(strref, kEnvUpper), stvar(strref, kEnvMixed));
    } else if (!strEq(val, kEnvVal1)) {
        TEST_FAILV(ret, 1, _SL("envGet('${string}') == '${string}', wanted '${string}'"),
                   stvar(strref, kEnvUpper), stvar(string, val), stvar(strref, kEnvVal1));
    }
#else
    if (found)
        TEST_FAILV(ret, 1, _SL("envGet('${string}') == '${string}'; Unix should not match '${string}'"),
                   stvar(strref, kEnvUpper), stvar(string, val), stvar(strref, kEnvMixed));
#endif

    strDestroy(&val);
    envUnset(kEnvMixed);
    return ret;
}

// ---- process enumeration and handles -------------------------------------------------------

// This executable's filename, which is the name this process shows up under in a process list.
static void selfName(string* out)
{
    string exe = 0;
    fsExe(&exe);
    pathFilename(out, exe);
    strDestroy(&exe);
}

static int test_proc_enum_self(void)
{
    int ret        = 0;
    ProcessID self = procCurrentID();
    sa_ProcessInfo procs;

    if (!procEnum(&procs, 0))
        TEST_FAIL(1, _SL("procEnum failed, cxerr ${int}"), stvar(int32, cxerr));

    if (saSize(procs) == 0)
        TEST_FAILV(ret, 1,
                   _SL("procEnum succeeded but listed nothing, not even our own pid ${int}"),
                   stvar(int64, self));

    // The one process certain to be in the list is the one asking for it.
    bool found = false;
    for (int32 i = 0; i < saSize(procs); i++) {
        if (procs.a[i].pid != self)
            continue;

        found = true;
        if (strEmpty(procs.a[i].name))
            TEST_FAILV(ret, 1, _SL("procEnum gave pid ${int} an empty name"), stvar(int64, self));
    }

    if (!found)
        TEST_FAILV(ret, 1, _SL("procEnum listed ${int} processes but not our own pid ${int}"),
                   stvar(int32, saSize(procs)), stvar(int64, self));

    saDestroy(&procs);
    return ret;
}

static int test_proc_find_by_name(void)
{
    int ret        = 0;
    ProcessID self = procCurrentID();
    string name    = 0;
    sa_ProcessInfo found;

    selfName(&name);

    if (!procFind(&found, name, 0)) {
        strDestroy(&name);
        TEST_FAIL(1, _SL("procFind('${string}') failed"), stvar(string, name));
    }

    bool sawself = false;
    for (int32 i = 0; i < saSize(found); i++) {
        if (found.a[i].pid == self)
            sawself = true;
    }

    if (!sawself)
        TEST_FAILV(ret, 1,
                   _SL("procFind('${string}') returned ${int} processes, none our own pid ${int}"),
                   stvar(string, name), stvar(int32, saSize(found)), stvar(int64, self));

    saDestroy(&found);
    strDestroy(&name);
    return ret;
}

static int test_proc_open_by_id(void)
{
    int ret        = 0;
    ProcessID self = procCurrentID();
    string nm      = 0;

    Process* proc = procOpen(self);
    if (!proc)
        TEST_FAIL(1, _SL("procOpen(${int}) returned NULL"), stvar(int64, self));

    if (procID(proc) != self)
        TEST_FAILV(ret, 1, _SL("procID == ${int}, wanted ${int}"), stvar(int64, procID(proc)),
                   stvar(int64, self));

    if (!procRunning(proc))
        TEST_FAILV(ret, 1, _SL("procRunning is false for our own pid ${int}"),
                   stvar(int64, self));

    if (!procName(proc, &nm) || strEmpty(nm))
        TEST_FAILV(ret, 1, _SL("procName gave nothing for our own pid ${int}"),
                   stvar(int64, self));

    strDestroy(&nm);

    procRelease(&proc);
    if (proc)
        TEST_FAILV(ret, 1, _SL("procRelease left a non-NULL handle for pid ${int}"),
                   stvar(int64, self));

    return ret;
}

static int test_proc_getinfo_byid(void)
{
    int ret        = 0;
    ProcessID self = procCurrentID();
    ProcessInfo info, copy;

    procInfoInit(&info);

    if (!procGetInfoByID(&info, self, PROC_EnumFullPath)) {
        procInfoDestroy(&info);
        TEST_FAIL(1, _SL("procGetInfoByID(${int}) failed"), stvar(int64, self));
    }

    if (info.pid != self)
        TEST_FAILV(ret, 1, _SL("procGetInfoByID reported pid ${int}, wanted ${int}"),
                   stvar(int64, info.pid), stvar(int64, self));

    if (strEmpty(info.name))
        TEST_FAILV(ret, 1, _SL("procGetInfoByID gave pid ${int} an empty name"),
                   stvar(int64, self));

    // A path is best-effort for processes in general, but a process can always read its own.
    if (strEmpty(info.exepath))
        TEST_FAILV(ret, 1, _SL("procGetInfoByID gave pid ${int} an empty exepath"),
                   stvar(int64, self));

    // An entry copied out of a snapshot has to be an independent value.
    procInfoInit(&copy);
    procInfoCopy(&copy, &info);
    if (copy.pid != info.pid || !strEq(copy.name, info.name))
        TEST_FAILV(ret, 1, _SL("procInfoCopy produced '${string}' for pid ${int}, wanted '${string}' for ${int}"),
                   stvar(string, copy.name), stvar(int64, copy.pid), stvar(string, info.name),
                   stvar(int64, info.pid));
    procInfoDestroy(&copy);

    procInfoDestroy(&info);
    return ret;
}

// ---- child processes -------------------------------------------------------------------------
//
// These run as `test_runner proctest child_*`, launched by the parent tests below. They report
// back by writing to the file named by CX_PROCTEST_OUT, which the parent sets in their
// environment. None of them gets an add_test line or an alltests entry -- they are only ever
// invoked as children.

#define kOutVar _SL("CX_PROCTEST_OUT")

// Writes text to the file the parent asked this child to report through.
static int childReport(strref text)
{
    string path = 0;
    if (!envGet(&path, kOutVar)) {
        strDestroy(&path);
        return 90;
    }

    FSFile* f = fsOpen(path, FS_Overwrite);
    strDestroy(&path);
    if (!f)
        return 91;

    size_t wrote = 0;
    bool ok      = fileWriteString(f, text, &wrote);
    fileClose(&f);

    return ok ? 0 : 92;
}

static int test_proc_child_exit42(void)
{
    return 42;
}

static int test_proc_child_sleep(void)
{
    osSleep(timeS(5));
    return 0;
}

// Writes every argument this process received, one per line. The parent checks the tail of the
// list, so it does not matter how many leading entries the test driver consumed.
static int test_proc_child_echoargs(void)
{
    string all = 0;

    // Newline as a separator, never a terminator: an argument that is itself empty has to stay
    // distinguishable from the end of the list, and a trailing newline would add a phantom entry.
    for (int i = 0; i < cxTestArgc; i++) {
        if (i > 0)
            strAppendChar(&all, '\n');
        strAppend(&all, (string)cxTestArgv[i]);
    }

    int ret = childReport(all);
    strDestroy(&all);
    return ret;
}

static int test_proc_child_echoenv(void)
{
    string val = 0;
    if (!envGet(&val, kEnvVar))
        strDup(&val, _SL("<unset>"));

    int ret = childReport(val);
    strDestroy(&val);
    return ret;
}

static int test_proc_child_cwd(void)
{
    string cwd = 0;
    fsCurDir(&cwd);

    int ret = childReport(cwd);
    strDestroy(&cwd);
    return ret;
}

static int test_proc_child_sleep1(void)
{
    osSleep(timeS(1));
    return 0;
}

// Writes to stdout and stderr alternately, flushing each time, so the parent can check that a
// shared output file keeps the two in order.
static int test_proc_child_stdio(void)
{
    fputs("out1\n", stdout);
    fflush(stdout);
    fputs("err1\n", stderr);
    fflush(stderr);
    fputs("out2\n", stdout);
    fflush(stdout);
    return 0;
}

// Starts a grandchild that outlives this process, reports its pid, and exits without waiting
// for it. The grandchild is then a process the test's own process did not launch.
static int test_proc_child_spawn(void)
{
    string exe = 0, s = 0;
    sa_string argv;
    ProcessOpts opts;

    fsExe(&exe);
    saInit(&argv, string, 2);
    saPush(&argv, strref, _SL("proctest"));
    saPush(&argv, strref, _SL("child_sleep2"));

    procOptsInit(&opts);
    opts.stdio = PROC_StdioNull;
    opts.flags = PROC_Detached;

    Process* proc = procLaunch(exe, argv, &opts);
    int ret       = 80;
    if (proc) {
        strFromInt64(&s, procID(proc), 10);
        ret = childReport(s);
        procRelease(&proc);
    }

    strDestroy(&s);
    procOptsDestroy(&opts);
    saDestroy(&argv);
    strDestroy(&exe);
    return ret;
}

static int test_proc_child_sleep2(void)
{
    osSleep(timeS(2));
    return 0;
}

#if defined(_PLATFORM_UNIX)
#include <fcntl.h>

// Counts descriptors at or above 3 that are still open. cx sets close-on-exec on nothing, so
// without the launcher's own sweep the parent's files and sockets would all be sitting here.
static int test_proc_child_fdcheck(void)
{
    int open = 0;
    for (int fd = 3; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) != -1)
            open++;
    }

    string s = 0;
    strFromInt32(&s, open, 10);
    int ret = childReport(s);
    strDestroy(&s);
    return ret;
}
#endif

// The one genuine platform difference the overview calls out: on Unix only a parent can collect
// a child's status, so a process reached through procOpen has no exit code and never will.
// Windows has no such rule. Either way there is no code for a process that is still running --
// what this pins down is the reason each platform gives.
static int test_proc_exitcode_foreign(void)
{
    int ret        = 0;
    ProcessID self = procCurrentID();
    int32 code     = 0;

    Process* proc = procOpen(self);
    if (!proc)
        TEST_FAIL(1, _SL("procOpen(${int}) returned NULL"), stvar(int64, self));

    cxerr    = CX_Success;
    bool got = procExitCode(proc, &code);

    if (got) {
        TEST_FAILV(ret, 1,
                   _SL("procExitCode reported ${int} for pid ${int}, which is still running"),
                   stvar(int32, code), stvar(int64, self));
    }
#if !defined(_PLATFORM_WIN)
    else if (cxerr != CX_NotSupported) {
        TEST_FAILV(ret, 1,
                   _SL("cxerr is ${int} for a process cx did not launch, wanted CX_NotSupported (${int})"),
                   stvar(int32, cxerr), stvar(int32, (int32)CX_NotSupported));
    }
#endif

    procRelease(&proc);
    return ret;
}

// ---- launching --------------------------------------------------------------------------------

// Where a child reports back. Relative, so it lands in the working directory the test runs in.
#define kOutFile _SL("cx_proctest_out.txt")

// Runs this same test_runner as a child, on the named child_* subtest.
static Process* launchSelf(strref subtest, sa_string extra, ProcessOpts* opts)
{
    string exe = 0;
    sa_string argv;

    fsExe(&exe);
    saInit(&argv, string, 4);
    saPush(&argv, strref, _SL("proctest"));
    saPush(&argv, strref, subtest);
    for (int32 i = 0; i < saSize(extra); i++) saPush(&argv, string, extra.a[i]);

    Process* proc = procLaunch(exe, argv, opts);

    saDestroy(&argv);
    strDestroy(&exe);
    return proc;
}

// Reads back whatever the child reported.
static bool readReport(string* out, strref path)
{
    strClear(out);

    FSFile* f = fsOpen(path, FS_Read);
    if (!f)
        return false;

    char buf[4096];
    size_t got = 0;
    bool ok    = fileRead(f, buf, sizeof(buf), &got);
    fileClose(&f);

    if (!ok && got == 0)
        return false;

    strFromBytes(out, buf, (uint32)got);
    return true;
}

// Launch options that point a child at the report file.
static void reportingOpts(ProcessOpts* opts)
{
    procOptsInit(opts);
    procOptsSetEnv(opts, kOutVar, kOutFile);
}

static int test_proc_launch_exit(void)
{
    int ret = 0;
    sa_string noargs = saInitNone;
    int32 code       = 0;

    Process* proc = launchSelf(_SL("child_exit42"), noargs, NULL);
    if (!proc)
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));

    if (!procWait(proc, timeS(30))) {
        TEST_FAILV(ret, 1, _SL("child ${int} did not finish within 30s"),
                   stvar(int64, procID(proc)));
    } else if (!procExitCode(proc, &code)) {
        TEST_FAILV(ret, 1, _SL("no exit code for child ${int}, cxerr ${int}"),
                   stvar(int64, procID(proc)), stvar(int32, cxerr));
    } else if (code != 42) {
        TEST_FAILV(ret, 1, _SL("child exited with ${int}, wanted 42"), stvar(int32, code));
    }

    if (procRunning(proc))
        TEST_FAILV(ret, 1, _SL("procRunning still true for finished child ${int}"),
                   stvar(int64, procID(proc)));

    procRelease(&proc);
    return ret;
}

static int test_proc_launch_args(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string extra;
    string got = 0;

    // The awkward cases in Windows command-line quoting: spaces, embedded quotes, trailing
    // backslashes, and an empty argument.
    saInit(&extra, string, 4);
    saPush(&extra, strref, _SL("plain"));
    saPush(&extra, strref, _SL("has space"));
    saPush(&extra, strref, _SL("has\"quote"));
    saPush(&extra, strref, _SL("trailing\\\\"));
    saPush(&extra, strref, _SL(""));

    reportingOpts(&opts);
    fsDelete(kOutFile);

    Process* proc = launchSelf(_SL("child_echoargs"), extra, &opts);
    if (!proc) {
        saDestroy(&extra);
        procOptsDestroy(&opts);
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    procWait(proc, timeS(30));
    procRelease(&proc);

    if (!readReport(&got, kOutFile)) {
        TEST_FAILV(ret, 1, _SL("child wrote nothing to '${string}'"),
                   stvar(strref, kOutFile));
    } else {
        // The child prints every argv entry it saw; the ones sent are the last five, whatever
        // the test driver consumed ahead of them.
        sa_string lines;
        saInit(&lines, string, 8);
        strSplit(&lines, got, _SL("\n"), true);

        int32 n = saSize(lines);
        int32 base = n - saSize(extra);
        if (base < 0) {
            TEST_FAILV(ret, 1, _SL("child reported only ${int} argv entries, wanted at least ${int}"),
                       stvar(int32, n), stvar(int32, saSize(extra)));
        } else {
            for (int32 i = 0; i < saSize(extra); i++) {
                if (!strEq(lines.a[base + i], extra.a[i]))
                    TEST_FAILV(ret, 1,
                               _SL("argv[${int}] arrived as '${string}', sent '${string}'"),
                               stvar(int32, i), stvar(string, lines.a[base + i]),
                               stvar(string, extra.a[i]));
            }
        }
        saDestroy(&lines);
    }

    strDestroy(&got);
    fsDelete(kOutFile);
    saDestroy(&extra);
    procOptsDestroy(&opts);
    return ret;
}

static int test_proc_launch_env(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string noargs = saInitNone;
    string got       = 0;

    reportingOpts(&opts);
    procOptsSetEnv(&opts, kEnvVar, kEnvVal2);
    fsDelete(kOutFile);

    Process* proc = launchSelf(_SL("child_echoenv"), noargs, &opts);
    if (!proc) {
        procOptsDestroy(&opts);
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    procWait(proc, timeS(30));
    procRelease(&proc);

    if (!readReport(&got, kOutFile))
        TEST_FAILV(ret, 1, _SL("child wrote nothing to '${string}'"),
                   stvar(strref, kOutFile));
    else if (!strEq(got, kEnvVal2))
        TEST_FAILV(ret, 1, _SL("child saw '${string}' in ${string}, wanted '${string}'"),
                   stvar(string, got), stvar(strref, kEnvVar), stvar(strref, kEnvVal2));

    strDestroy(&got);
    fsDelete(kOutFile);
    procOptsDestroy(&opts);
    return ret;
}

static int test_proc_launch_cwd(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string noargs = saInitNone;
    string cwd = 0, sub = 0, got = 0, want = 0, outpath = 0;

    fsCurDir(&cwd);
    pathJoin(&sub, cwd, _S"cx_proctest_dir");
    fsCreateDir(sub);

    // The report file stays in the original directory, so it has to be named absolutely -- the
    // child is deliberately running somewhere else.
    pathJoin(&outpath, cwd, _S"cx_proctest_out.txt");

    procOptsInit(&opts);
    procOptsSetEnv(&opts, kOutVar, outpath);
    strDup(&opts.workdir, sub);
    fsDelete(outpath);

    Process* proc = launchSelf(_SL("child_cwd"), noargs, &opts);
    if (!proc) {
        ret = 1;
        TEST_FAILV(ret, 1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    } else {
        procWait(proc, timeS(30));
        procRelease(&proc);

        if (!readReport(&got, outpath)) {
            TEST_FAILV(ret, 1, _SL("child wrote nothing to '${string}'"),
                       stvar(strref, outpath));
        } else {
            // Compare canonically: the OS may hand back a resolved form of the path.
            pathMakeAbsolute(&want, sub);
            if (!strEqi(got, want))
                TEST_FAILV(ret, 1, _SL("child ran in '${string}', wanted '${string}'"),
                           stvar(string, got), stvar(string, want));
        }
    }

    fsDelete(outpath);
    fsRemoveDir(sub);
    strDestroy(&cwd);
    strDestroy(&sub);
    strDestroy(&got);
    strDestroy(&want);
    strDestroy(&outpath);
    procOptsDestroy(&opts);
    return ret;
}

static int test_proc_launch_stdionull(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string noargs = saInitNone;
    int32 code       = 0;

    procOptsInit(&opts);
    opts.stdio = PROC_StdioNull;

    Process* proc = launchSelf(_SL("child_exit42"), noargs, &opts);
    if (!proc) {
        procOptsDestroy(&opts);
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    if (!procWait(proc, timeS(30)))
        TEST_FAILV(ret, 1, _SL("child ${int} did not finish"), stvar(int64, procID(proc)));
    else if (!procExitCode(proc, &code) || code != 42)
        TEST_FAILV(ret, 1, _SL("child exited with ${int}, wanted 42"), stvar(int32, code));

    procRelease(&proc);
    procOptsDestroy(&opts);
    return ret;
}

// A program that does not exist must fail the launch outright, rather than producing a handle
// to a process that immediately dies -- and must leave nothing behind to reap.
static int test_proc_launch_missing(void)
{
    int ret = 0;
    sa_string noargs = saInitNone;
    string exe = 0, dir = 0;

    fsExeDir(&dir);
    pathJoin(&exe, dir, _S"cx_no_such_program_here");

    cxerr         = CX_Success;
    Process* proc = procLaunch(exe, noargs, NULL);

    if (proc) {
        TEST_FAILV(ret, 1, _SL("procLaunch of a missing program returned a handle for pid ${int}"),
                   stvar(int64, procID(proc)));
        procRelease(&proc);
    } else if (cxerr != CX_FileNotFound) {
        TEST_FAILV(ret, 1, _SL("cxerr is ${int}, wanted CX_FileNotFound (${int})"),
                   stvar(int32, cxerr), stvar(int32, (int32)CX_FileNotFound));
    }

    strDestroy(&exe);
    strDestroy(&dir);
    return ret;
}

// Reads a file and drops any carriage returns, so text written by a Windows child in text mode
// compares equal to the same text from Unix.
static bool readReportLF(string* out, strref path)
{
    if (!readReport(out, path))
        return false;

    string lf = 0;
    uint32 n  = strLen(*out);
    for (uint32 i = 0; i < n; i++) {
        uint8 c = strGetChar(*out, i);
        if (c != '\r')
            strAppendChar(&lf, c);
    }

    strDestroy(out);
    *out = lf;
    return true;
}

STR_CONST(kStdioOnce, "out1\nerr1\nout2\n");

// Launches child_stdio with its output sent to path, and waits for it. Returns false and records
// why on failure.
static bool launchStdioFile(int* ret, strref path, flags_t flags)
{
    ProcessOpts opts;
    sa_string noargs = saInitNone;

    procOptsInit(&opts);
    opts.stdio = PROC_StdioFile;
    opts.flags = flags;
    strDup(&opts.stdioPath, path);

    Process* proc = launchSelf(_SL("child_stdio"), noargs, &opts);
    procOptsDestroy(&opts);

    if (!proc) {
        TEST_FAILV(*ret, 1, _SL("procLaunch to '${string}' failed, cxerr ${int}"),
                   stvar(strref, path), stvar(int32, cxerr));
        return false;
    }

    if (!procWait(proc, timeS(30))) {
        TEST_FAILV(*ret, 1, _SL("child ${int} did not finish"), stvar(int64, procID(proc)));
        procTerminate(proc, true);
        procRelease(&proc);
        return false;
    }

    procRelease(&proc);
    return true;
}

static void checkStdioFile(int* ret, strref path, int copies)
{
    string got = 0, want = 0;

    for (int i = 0; i < copies; i++) strAppend(&want, kStdioOnce);

    if (!readReportLF(&got, path))
        TEST_FAILV(*ret, 1, _SL("nothing could be read from '${string}'"), stvar(strref, path));
    else if (!strEq(got, want))
        TEST_FAILV(*ret, 1, _SL("'${string}' holds '${string}', wanted '${string}'"),
                   stvar(strref, path), stvar(string, got), stvar(string, want));

    strDestroy(&got);
    strDestroy(&want);
}

// stdout and stderr land in one file, in order, and later launches append. The name has a space
// in it, and is used both relative -- resolved against cx's current directory -- and as an
// absolute cx path, which on Windows is the c:/ form rather than the native one.
static int test_proc_launch_stdiofile(void)
{
    int ret      = 0;
    string abs   = 0;
    strref rel   = _SL("cx proctest stdio.txt");

    pathMakeAbsolute(&abs, rel);
    fsDelete(abs);

    if (launchStdioFile(&ret, rel, 0))
        checkStdioFile(&ret, abs, 1);

    if (!ret && launchStdioFile(&ret, abs, 0))
        checkStdioFile(&ret, abs, 2);

    // A detached child must still get the file, rather than having its output quietly dropped.
    if (!ret && launchStdioFile(&ret, abs, PROC_Detached))
        checkStdioFile(&ret, abs, 3);

    fsDelete(abs);
    strDestroy(&abs);
    return ret;
}

// A file that cannot be opened fails the launch itself, and PROC_StdioFile without a path is a
// caller error.
static int test_proc_launch_stdiofile_bad(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string noargs = saInitNone;

    procOptsInit(&opts);
    opts.stdio = PROC_StdioFile;
    strDup(&opts.stdioPath, _SL("cx_proctest_no_such_dir/out.txt"));

    cxerr         = CX_Success;
    Process* proc = launchSelf(_SL("child_exit42"), noargs, &opts);
    if (proc) {
        TEST_FAILV(ret, 1, _SL("launch into a missing directory returned pid ${int}"),
                   stvar(int64, procID(proc)));
        procWait(proc, timeS(30));
        procRelease(&proc);
    } else if (cxerr == CX_Success) {
        TEST_FAILV(ret, 1, _SL("launch into '${string}' failed without setting cxerr"),
                   stvar(string, opts.stdioPath));
    }

    strDestroy(&opts.stdioPath);
    cxerr = CX_Success;
    proc  = launchSelf(_SL("child_exit42"), noargs, &opts);
    if (proc) {
        TEST_FAILV(ret, 1, _SL("PROC_StdioFile with no path returned pid ${int}"),
                   stvar(int64, procID(proc)));
        procWait(proc, timeS(30));
        procRelease(&proc);
    } else if (cxerr != CX_InvalidArgument) {
        TEST_FAILV(ret, 1, _SL("PROC_StdioFile with no path set cxerr ${int}, wanted ${int}"),
                   stvar(int32, cxerr), stvar(int32, (int32)CX_InvalidArgument));
    }

    procOptsDestroy(&opts);
    return ret;
}

// PROC_NewConsole conflicts with PROC_Detached and PROC_NoWindow. It is rejected on every
// platform, not only the one where the flags collide.
static int test_proc_launch_flagconflict(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string noargs = saInitNone;
    flags_t bad[2]   = { PROC_NewConsole | PROC_Detached, PROC_NewConsole | PROC_NoWindow };

    procOptsInit(&opts);

    for (int i = 0; i < 2; i++) {
        opts.flags    = bad[i];
        cxerr         = CX_Success;
        Process* proc = launchSelf(_SL("child_exit42"), noargs, &opts);

        if (proc) {
            TEST_FAILV(ret, 1, _SL("flags 0x${uint(hex)} were accepted"), stvar(uint32, bad[i]));
            procWait(proc, timeS(30));
            procRelease(&proc);
        } else if (cxerr != CX_InvalidArgument) {
            TEST_FAILV(ret, 1, _SL("flags 0x${uint(hex)} set cxerr ${int}, wanted ${int}"),
                       stvar(uint32, bad[i]), stvar(int32, cxerr),
                       stvar(int32, (int32)CX_InvalidArgument));
        }
    }

    procOptsDestroy(&opts);
    return ret;
}

#if defined(_PLATFORM_WIN)
static int test_proc_launch_newconsole(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string noargs = saInitNone;
    int32 code       = 0;

    procOptsInit(&opts);
    opts.flags = PROC_NewConsole;

    Process* proc = launchSelf(_SL("child_exit42"), noargs, &opts);
    procOptsDestroy(&opts);
    if (!proc)
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));

    if (!procWait(proc, timeS(30)))
        TEST_FAILV(ret, 1, _SL("child ${int} did not finish"), stvar(int64, procID(proc)));
    else if (!procExitCode(proc, &code) || code != 42)
        TEST_FAILV(ret, 1, _SL("child exited with ${int}, wanted 42"), stvar(int32, code));

    procRelease(&proc);
    return ret;
}
#endif

#if defined(_PLATFORM_UNIX)
// The child must not inherit the parent's open descriptors. cx sets close-on-exec on nothing,
// so this exercises the fork path's own sweep rather than any libc default -- without it every
// file, socket and epoll fd the parent holds would be sitting in the child.
static int test_proc_launch_fdsweep(void)
{
    int ret = 0;
    ProcessOpts opts;
    sa_string noargs = saInitNone;
    string got       = 0;

    // Deliberately hold descriptors open across the launch: this is the state that leaks.
    FSFile* held1 = fsOpen(_SL("cx_proctest_held1.txt"), FS_Overwrite);
    FSFile* held2 = fsOpen(_SL("cx_proctest_held2.txt"), FS_Overwrite);

    reportingOpts(&opts);
    fsDelete(kOutFile);

    Process* proc = launchSelf(_SL("child_fdcheck"), noargs, &opts);
    if (!proc) {
        TEST_FAILV(ret, 1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    } else {
        procWait(proc, timeS(30));
        procRelease(&proc);

        if (!readReport(&got, kOutFile))
            TEST_FAILV(ret, 1, _SL("child wrote nothing to '${string}'"),
                       stvar(strref, kOutFile));
        else if (!strEq(got, _SL("0")))
            TEST_FAILV(ret, 1, _SL("child inherited ${string} descriptors at or above 3, wanted 0"),
                       stvar(string, got));
    }

    strDestroy(&got);
    fsDelete(kOutFile);

    if (held1)
        fileClose(&held1);
    if (held2)
        fileClose(&held2);
    fsDelete(_SL("cx_proctest_held1.txt"));
    fsDelete(_SL("cx_proctest_held2.txt"));

    procOptsDestroy(&opts);
    return ret;
}
#endif

static int test_proc_wait_timeout(void)
{
    int ret = 0;
    sa_string noargs = saInitNone;

    Process* proc = launchSelf(_SL("child_sleep"), noargs, NULL);
    if (!proc)
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));

    int64 start = clockTimer();
    bool done   = procWait(proc, timeMS(200));
    int64 took  = clockTimer() - start;

    if (done)
        TEST_FAILV(ret, 1, _SL("procWait said a 5s child finished after ${int} ms"),
                   stvar(int64, timeToMsec(took)));

    // It must actually have waited, rather than returning false immediately.
    if (took < timeMS(100))
        TEST_FAILV(ret, 1, _SL("procWait returned after only ${int} ms, expected about 200"),
                   stvar(int64, timeToMsec(took)));

    procTerminate(proc, true);
    procWait(proc, timeS(30));
    procRelease(&proc);
    return ret;
}

static int test_proc_terminate(void)
{
    int ret = 0;
    sa_string noargs = saInitNone;

    Process* proc = launchSelf(_SL("child_sleep"), noargs, NULL);
    if (!proc)
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));

    if (!procTerminate(proc, true))
        TEST_FAILV(ret, 1, _SL("procTerminate failed for pid ${int}, cxerr ${int}"),
                   stvar(int64, procID(proc)), stvar(int32, cxerr));

    if (!procWait(proc, timeS(30)))
        TEST_FAILV(ret, 1, _SL("killed child ${int} never finished"), stvar(int64, procID(proc)));

    if (procRunning(proc))
        TEST_FAILV(ret, 1, _SL("procRunning still true after killing ${int}"),
                   stvar(int64, procID(proc)));

    procRelease(&proc);
    return ret;
}

// ---- exit notification -------------------------------------------------------------------

// Exit callbacks run on the watcher thread, so what they record is shared state. A mutex keeps
// it simple and correct rather than reasoning about which fields need which ordering.
typedef struct NotifyRec {
    Mutex lock;
    Event ev;
    int32 count;
    int64 pid;
    int32 code;
} NotifyRec;

static NotifyRec nrec;

static void notifyRecInit(void)
{
    memset(&nrec, 0, sizeof(nrec));
    mutexInit(&nrec.lock);
    // Locked-signal, not a broadcast: the process can finish before the test starts waiting,
    // and a plain broadcast raised with no waiter yet is simply dropped.
    eventInit(&nrec.ev);
}

static void notifyRecDestroy(void)
{
    eventDestroy(&nrec.ev);
    mutexDestroy(&nrec.lock);
}

static bool onExitCb(stvlist* cvars, stvlist* args)
{
    int64 pid;
    int32 code;

    if (!stvlNext(args, int64, &pid) || !stvlNext(args, int32, &code))
        return false;

    withMutex (&nrec.lock) {
        nrec.pid = pid;
        nrec.code = code;
        nrec.count++;
    }

    eventSignalLock(&nrec.ev);
    return true;
}

static int test_proc_notify_exit(void)
{
    int ret          = 0;
    sa_string noargs = saInitNone;

    notifyRecInit();

    Process* proc = launchSelf(_SL("child_exit42"), noargs, NULL);
    if (!proc) {
        notifyRecDestroy();
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    int64 pid = procID(proc);

    if (!procNotifyExit(proc, closureCreate(onExitCb, stvNone)))
        TEST_FAILV(ret, 1, _SL("procNotifyExit failed for pid ${int}"), stvar(int64, pid));

    if (!eventWaitTimeout(&nrec.ev, timeS(30)))
        TEST_FAILV(ret, 1, _SL("no exit callback for pid ${int} within 30s"), stvar(int64, pid));

    withMutex (&nrec.lock) {
        if (nrec.count != 1)
            TEST_FAILV(ret, 1, _SL("exit callback ran ${int} times, wanted 1"),
                       stvar(int32, nrec.count));
        if (nrec.pid != pid)
            TEST_FAILV(ret, 1, _SL("callback reported pid ${int}, wanted ${int}"),
                       stvar(int64, nrec.pid), stvar(int64, pid));
        if (nrec.code != 42)
            TEST_FAILV(ret, 1, _SL("callback reported exit code ${int}, wanted 42"),
                       stvar(int32, nrec.code));
    }

    procRelease(&proc);
    notifyRecDestroy();
    return ret;
}

// Registering on a process that has already finished must call the closure before returning,
// on this thread. That is what makes the obvious check-then-register race unwritable.
static int test_proc_notify_after_exit(void)
{
    int ret          = 0;
    sa_string noargs = saInitNone;

    notifyRecInit();

    Process* proc = launchSelf(_SL("child_exit42"), noargs, NULL);
    if (!proc) {
        notifyRecDestroy();
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    int64 pid = procID(proc);

    if (!procWait(proc, timeS(30)))
        TEST_FAILV(ret, 1, _SL("child ${int} did not finish"), stvar(int64, pid));

    if (!procNotifyExit(proc, closureCreate(onExitCb, stvNone)))
        TEST_FAILV(ret, 1, _SL("procNotifyExit failed for finished pid ${int}"),
                   stvar(int64, pid));

    // No waiting: it must already have run by the time the call returned.
    withMutex (&nrec.lock) {
        if (nrec.count != 1)
            TEST_FAILV(ret, 1,
                       _SL("callback ran ${int} times immediately after registering on a finished process, wanted 1"),
                       stvar(int32, nrec.count));
        if (nrec.code != 42)
            TEST_FAILV(ret, 1, _SL("callback reported exit code ${int}, wanted 42"),
                       stvar(int32, nrec.code));
    }

    procRelease(&proc);
    notifyRecDestroy();
    return ret;
}

#define PROC_MANY 80

// Eighty at once, well past the 64 handles WaitForMultipleObjects can watch. Each handle is
// released the moment its callback is registered, so this also pins down that a forgotten child
// is still reaped and still reports back.
static int test_proc_many(void)
{
    int ret          = 0;
    sa_string noargs = saInitNone;
    int launched     = 0;

    notifyRecInit();

    for (int i = 0; i < PROC_MANY; i++) {
        Process* proc = launchSelf(_SL("child_exit42"), noargs, NULL);
        if (!proc) {
            TEST_FAILV(ret, 1, _SL("launch ${int} of ${int} failed, cxerr ${int}"),
                       stvar(int32, i), stvar(int32, (int32)PROC_MANY), stvar(int32, cxerr));
            break;
        }

        procNotifyExit(proc, closureCreate(onExitCb, stvNone));
        procRelease(&proc);
        launched++;
    }

    // The event only says "at least one more finished", so re-check the count each time.
    int64 deadline = clockTimer() + timeS(60);
    for (;;) {
        int32 count = 0;
        withMutex (&nrec.lock) {
            count = nrec.count;
        }

        if (count >= launched)
            break;

        if (clockTimer() >= deadline) {
            TEST_FAILV(ret, 1, _SL("only ${int} of ${int} exit callbacks arrived within 60s"),
                       stvar(int32, count), stvar(int32, launched));
            break;
        }

        eventWaitTimeout(&nrec.ev, timeMS(100));
        eventReset(&nrec.ev);
    }

    withMutex (&nrec.lock) {
        if (!ret && nrec.code != 42)
            TEST_FAILV(ret, 1, _SL("last callback reported exit code ${int}, wanted 42"),
                       stvar(int32, nrec.code));
    }

    notifyRecDestroy();
    return ret;
}

// The exit code a callback should see for a process this test did not launch.
#if defined(_PLATFORM_WIN)
#define kForeignExitCode 0
#else
#define kForeignExitCode PROC_ExitCodeUnknown
#endif

#if defined(_PLATFORM_LINUX)
#include <dirent.h>

// Open descriptors in this process. A watch on a process cx did not launch holds a pidfd, so a
// count that comes back to where it started shows the watch was torn down.
static int countOpenFds(void)
{
    int n  = 0;
    DIR* d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    while (readdir(d)) n++;
    closedir(d);
    return n;
}

// User plus system CPU time used by this whole process, in clock ticks.
static int64 processCpuTicks(void)
{
    char buf[1024];
    FILE* f = fopen("/proc/self/stat", "r");
    if (!f)
        return -1;
    size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = 0;

    // Fields after the parenthesized name, which may itself contain spaces.
    char* p = strrchr(buf, ')');
    if (!p)
        return -1;

    long long utime = 0, stime = 0;
    if (sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lld %lld", &utime, &stime) != 2)
        return -1;
    return utime + stime;
}
#endif

// Waits for the next exit callback and checks what it reported.
static void expectCallback(int* ret, int64 pid, int32 code)
{
    if (!eventWaitTimeout(&nrec.ev, timeS(30))) {
        TEST_FAILV(*ret, 1, _SL("no exit callback for pid ${int} within 30s"), stvar(int64, pid));
        return;
    }

    withMutex (&nrec.lock) {
        if (nrec.count != 1)
            TEST_FAILV(*ret, 1, _SL("exit callback ran ${int} times, wanted 1"),
                       stvar(int32, nrec.count));
        if (nrec.pid != pid)
            TEST_FAILV(*ret, 1, _SL("callback reported pid ${int}, wanted ${int}"),
                       stvar(int64, nrec.pid), stvar(int64, pid));
        if (nrec.code != code)
            TEST_FAILV(*ret, 1, _SL("callback reported exit code ${int}, wanted ${int}"),
                       stvar(int32, nrec.code), stvar(int32, code));
    }
}

// A handle from procOpen must be told when its process exits, too. This one happens to be a
// child of this process, reached through a second handle; the launched handle must still
// collect the real exit code rather than having it taken by the opened one.
static int test_proc_notify_opened(void)
{
    int ret          = 0;
    sa_string noargs = saInitNone;
    int32 code       = -99;

    notifyRecInit();

    Process* lproc = launchSelf(_SL("child_sleep1"), noargs, NULL);
    if (!lproc) {
        notifyRecDestroy();
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    int64 pid      = procID(lproc);
    Process* oproc = procOpen(pid);

    if (!oproc) {
        TEST_FAILV(ret, 1, _SL("procOpen(${int}) failed, cxerr ${int}"), stvar(int64, pid),
                   stvar(int32, cxerr));
    } else {
        if (!procNotifyExit(oproc, closureCreate(onExitCb, stvNone)))
            TEST_FAILV(ret, 1, _SL("procNotifyExit failed for opened pid ${int}"),
                       stvar(int64, pid));
        else
            expectCallback(&ret, pid, kForeignExitCode);
    }

    if (!procWait(lproc, timeS(30)))
        TEST_FAILV(ret, 1, _SL("child ${int} did not finish"), stvar(int64, pid));
    else if (!procExitCode(lproc, &code) || code != 0)
        TEST_FAILV(ret, 1, _SL("launched handle reported exit code ${int}, wanted 0"),
                   stvar(int32, code));

#if !defined(_PLATFORM_WIN)
    // The opened handle knows the process finished, but must not claim to know how.
    if (oproc) {
        cxerr = CX_Success;
        if (procExitCode(oproc, &code))
            TEST_FAILV(ret, 1, _SL("opened handle reported exit code ${int} on Unix"),
                       stvar(int32, code));
        else if (cxerr != CX_NotSupported)
            TEST_FAILV(ret, 1, _SL("opened handle set cxerr ${int}, wanted CX_NotSupported"),
                       stvar(int32, cxerr));
    }
#endif

    procRelease(&oproc);
    procRelease(&lproc);
    notifyRecDestroy();
    return ret;
}

// A process whose parent is not this one: the watcher cannot collect it, and on Linux must not
// spin on a descriptor that stays readable after the exit.
static int test_proc_notify_nonchild(void)
{
    int ret          = 0;
    sa_string noargs = saInitNone;
    ProcessOpts opts;
    string got = 0;
    int64 gpid = 0;

    reportingOpts(&opts);
    fsDelete(kOutFile);

    Process* spawner = launchSelf(_SL("child_spawn"), noargs, &opts);
    procOptsDestroy(&opts);
    if (!spawner)
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));

    procWait(spawner, timeS(30));
    procRelease(&spawner);

    if (!readReport(&got, kOutFile) || !strToInt64(&gpid, got, 10, STRNUM_NoTrailing)) {
        TEST_FAILV(ret, 1, _SL("spawner reported '${string}', wanted a pid"), stvar(string, got));
        strDestroy(&got);
        fsDelete(kOutFile);
        return ret;
    }
    strDestroy(&got);
    fsDelete(kOutFile);

    notifyRecInit();

#if defined(_PLATFORM_LINUX)
    int fdsBefore = countOpenFds();
#endif

    Process* proc = procOpen(gpid);
    if (!proc) {
        TEST_FAILV(ret, 1, _SL("procOpen(${int}) failed, cxerr ${int}"), stvar(int64, gpid),
                   stvar(int32, cxerr));
    } else if (!procNotifyExit(proc, closureCreate(onExitCb, stvNone))) {
        TEST_FAILV(ret, 1, _SL("procNotifyExit failed for pid ${int}"), stvar(int64, gpid));
    } else {
        expectCallback(&ret, gpid, kForeignExitCode);
    }
    procRelease(&proc);

#if defined(_PLATFORM_LINUX)
    if (!ret) {
        // Delivery and teardown happen on the watcher thread just before and after the
        // callback, so give the teardown a moment before counting.
        int fdsAfter = -1;
        for (int i = 0; i < 50; i++) {
            fdsAfter = countOpenFds();
            if (fdsAfter == fdsBefore)
                break;
            osSleep(timeMS(10));
        }
        if (fdsAfter != fdsBefore)
            TEST_FAILV(ret, 1, _SL("${int} descriptors open after the watch ended, ${int} before"),
                       stvar(int32, fdsAfter), stvar(int32, fdsBefore));

        int64 before = processCpuTicks();
        osSleep(timeMS(500));
        int64 used = processCpuTicks() - before;
        if (before >= 0 && used > 20)
            TEST_FAILV(ret, 1, _SL("process used ${int} CPU ticks while idle for 500ms"),
                       stvar(int64, used));
    }
#endif

    notifyRecDestroy();
    return ret;
}

// Registering on an opened handle whose process has already finished calls the closure before
// returning, exactly as for a launched one.
static int test_proc_notify_opened_exited(void)
{
    int ret          = 0;
    sa_string noargs = saInitNone;

    notifyRecInit();

    Process* lproc = launchSelf(_SL("child_sleep1"), noargs, NULL);
    if (!lproc) {
        notifyRecDestroy();
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    int64 pid      = procID(lproc);
    Process* oproc = procOpen(pid);
    procWait(lproc, timeS(30));
    procRelease(&lproc);

    if (!oproc) {
        TEST_FAILV(ret, 1, _SL("procOpen(${int}) failed, cxerr ${int}"), stvar(int64, pid),
                   stvar(int32, cxerr));
    } else {
        if (!procNotifyExit(oproc, closureCreate(onExitCb, stvNone)))
            TEST_FAILV(ret, 1, _SL("procNotifyExit failed for finished pid ${int}"),
                       stvar(int64, pid));

        withMutex (&nrec.lock) {
            if (nrec.count != 1)
                TEST_FAILV(ret, 1,
                           _SL("callback ran ${int} times immediately after registering on a finished opened process, wanted 1"),
                           stvar(int32, nrec.count));
            else if (nrec.code != kForeignExitCode)
                TEST_FAILV(ret, 1, _SL("callback reported exit code ${int}, wanted ${int}"),
                           stvar(int32, nrec.code), stvar(int32, (int32)kForeignExitCode));
        }
    }

    procRelease(&oproc);
    notifyRecDestroy();
    return ret;
}

// Cancelling the last callback on an opened handle ends the watch: the handle is freed when the
// caller releases it, rather than held until the process exits, and nothing is called later.
static int test_proc_notify_cancel_opened(void)
{
    int ret          = 0;
    sa_string noargs = saInitNone;

    notifyRecInit();

    Process* lproc = launchSelf(_SL("child_sleep"), noargs, NULL);
    if (!lproc) {
        notifyRecDestroy();
        TEST_FAIL(1, _SL("procLaunch failed, cxerr ${int}"), stvar(int32, cxerr));
    }

    int64 pid = procID(lproc);

#if defined(_PLATFORM_LINUX)
    int fdsBefore = countOpenFds();
#endif

    Process* oproc = procOpen(pid);
    if (!oproc) {
        TEST_FAILV(ret, 1, _SL("procOpen(${int}) failed, cxerr ${int}"), stvar(int64, pid),
                   stvar(int32, cxerr));
    } else {
        if (!procNotifyExit(oproc, closureCreate(onExitCb, stvNone)))
            TEST_FAILV(ret, 1, _SL("procNotifyExit failed for pid ${int}"), stvar(int64, pid));

        Weak(Process)* weak = objGetWeak(Process, oproc);
        procNotifyCancel(oproc);
        procRelease(&oproc);

        Process* still = objAcquireFromWeak(Process, weak);
        if (still) {
            TEST_FAILV(ret, 1, _SL("opened handle for ${int} still alive after cancel and release"),
                       stvar(int64, pid));
            procRelease(&still);
        }
        objDestroyWeak(&weak);

#if defined(_PLATFORM_LINUX)
        int fdsAfter = countOpenFds();
        if (fdsAfter != fdsBefore)
            TEST_FAILV(ret, 1, _SL("${int} descriptors open after cancelling, ${int} before"),
                       stvar(int32, fdsAfter), stvar(int32, fdsBefore));
#endif
    }

    procTerminate(lproc, true);
    procWait(lproc, timeS(30));
    procRelease(&lproc);

    // Long enough for a stray delivery to have arrived if one were coming.
    osSleep(timeMS(200));
    withMutex (&nrec.lock) {
        if (nrec.count != 0)
            TEST_FAILV(ret, 1, _SL("cancelled callback still ran ${int} times"),
                       stvar(int32, nrec.count));
    }

    notifyRecDestroy();
    return ret;
}

// Runs the environment subtests in one process, so ctest spends one process launch on the group
// rather than one on each. Every subtest stays registered below for running one in isolation.
int test_proc_grp_env(void)
{
    TEST_CHAIN(test_proc_env_getset, test_proc_env_unset, test_proc_env_enum,
               test_proc_env_caseinsens);
}

int test_proc_grp_enum(void)
{
    TEST_CHAIN(test_proc_enum_self, test_proc_find_by_name, test_proc_open_by_id,
               test_proc_getinfo_byid, test_proc_exitcode_foreign);
}

int test_proc_grp_notify(void)
{
    TEST_CHAIN(test_proc_notify_exit, test_proc_notify_after_exit, test_proc_many,
               test_proc_notify_opened, test_proc_notify_nonchild,
               test_proc_notify_opened_exited, test_proc_notify_cancel_opened);
}

int test_proc_grp_launch(void)
{
#if defined(_PLATFORM_UNIX)
    TEST_CHAIN(test_proc_launch_exit, test_proc_launch_args, test_proc_launch_env,
               test_proc_launch_cwd, test_proc_launch_stdionull, test_proc_launch_missing,
               test_proc_launch_stdiofile, test_proc_launch_stdiofile_bad,
               test_proc_launch_flagconflict, test_proc_launch_fdsweep, test_proc_wait_timeout,
               test_proc_terminate);
#elif defined(_PLATFORM_WIN)
    TEST_CHAIN(test_proc_launch_exit, test_proc_launch_args, test_proc_launch_env,
               test_proc_launch_cwd, test_proc_launch_stdionull, test_proc_launch_missing,
               test_proc_launch_stdiofile, test_proc_launch_stdiofile_bad,
               test_proc_launch_flagconflict, test_proc_launch_newconsole,
               test_proc_wait_timeout, test_proc_terminate);
#else
    TEST_CHAIN(test_proc_launch_exit, test_proc_launch_args, test_proc_launch_env,
               test_proc_launch_cwd, test_proc_launch_stdionull, test_proc_launch_missing,
               test_proc_launch_stdiofile, test_proc_launch_stdiofile_bad,
               test_proc_launch_flagconflict, test_proc_wait_timeout, test_proc_terminate);
#endif
}

testfunc proctest_funcs[] = {
    { "env_getset",     test_proc_env_getset     },
    { "env_unset",      test_proc_env_unset      },
    { "env_enum",       test_proc_env_enum       },
    { "env_caseinsens", test_proc_env_caseinsens },
    { "enum_self",      test_proc_enum_self      },
    { "find_by_name",   test_proc_find_by_name   },
    { "open_by_id",     test_proc_open_by_id     },
    { "getinfo_byid",   test_proc_getinfo_byid   },
    { "exitcode_foreign", test_proc_exitcode_foreign },
    { "launch_exit",      test_proc_launch_exit      },
    { "launch_args",      test_proc_launch_args      },
    { "launch_env",       test_proc_launch_env       },
    { "launch_cwd",       test_proc_launch_cwd       },
    { "launch_stdionull", test_proc_launch_stdionull },
    { "launch_missing",   test_proc_launch_missing   },
    { "launch_stdiofile", test_proc_launch_stdiofile },
    { "launch_stdiofile_bad", test_proc_launch_stdiofile_bad },
    { "launch_flagconflict", test_proc_launch_flagconflict },
#if defined(_PLATFORM_WIN)
    { "launch_newconsole", test_proc_launch_newconsole },
#endif
#if defined(_PLATFORM_UNIX)
    { "launch_fdsweep",   test_proc_launch_fdsweep   },
#endif
    { "wait_timeout",     test_proc_wait_timeout     },
    { "terminate",        test_proc_terminate        },
    { "child_exit42",     test_proc_child_exit42     },
    { "child_sleep",      test_proc_child_sleep      },
    { "child_sleep1",     test_proc_child_sleep1     },
    { "child_sleep2",     test_proc_child_sleep2     },
    { "child_stdio",      test_proc_child_stdio      },
    { "child_spawn",      test_proc_child_spawn      },
    { "child_echoargs",   test_proc_child_echoargs   },
    { "child_echoenv",    test_proc_child_echoenv    },
    { "child_cwd",        test_proc_child_cwd        },
#if defined(_PLATFORM_UNIX)
    { "child_fdcheck",    test_proc_child_fdcheck    },
#endif
    { "notify_exit",       test_proc_notify_exit       },
    { "notify_after_exit", test_proc_notify_after_exit },
    { "many",              test_proc_many              },
    { "notify_opened",        test_proc_notify_opened        },
    { "notify_nonchild",      test_proc_notify_nonchild      },
    { "notify_opened_exited", test_proc_notify_opened_exited },
    { "notify_cancel_opened", test_proc_notify_cancel_opened },
    { "grp_env",        test_proc_grp_env        },
    { "grp_enum",       test_proc_grp_enum       },
    { "grp_launch",     test_proc_grp_launch     },
    { "grp_notify",     test_proc_grp_notify     },
    { 0,                0                        }
};
