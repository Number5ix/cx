#include <cx/container.h>
#include <cx/cx.h>
#include <cx/debug/error.h>
#include <cx/fs.h>
#include <cx/string.h>
#include <cx/platform/os.h>
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
    bool ok      = fsWriteString(f, text, &wrote);
    fsClose(f);

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
    bool ok    = fsRead(f, buf, sizeof(buf), &got);
    fsClose(f);

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
        fsClose(held1);
    if (held2)
        fsClose(held2);
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
               test_proc_getinfo_byid);
}

int test_proc_grp_launch(void)
{
#if defined(_PLATFORM_UNIX)
    TEST_CHAIN(test_proc_launch_exit, test_proc_launch_args, test_proc_launch_env,
               test_proc_launch_cwd, test_proc_launch_stdionull, test_proc_launch_missing,
               test_proc_launch_fdsweep, test_proc_wait_timeout, test_proc_terminate);
#else
    TEST_CHAIN(test_proc_launch_exit, test_proc_launch_args, test_proc_launch_env,
               test_proc_launch_cwd, test_proc_launch_stdionull, test_proc_launch_missing,
               test_proc_wait_timeout, test_proc_terminate);
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
    { "launch_exit",      test_proc_launch_exit      },
    { "launch_args",      test_proc_launch_args      },
    { "launch_env",       test_proc_launch_env       },
    { "launch_cwd",       test_proc_launch_cwd       },
    { "launch_stdionull", test_proc_launch_stdionull },
    { "launch_missing",   test_proc_launch_missing   },
#if defined(_PLATFORM_UNIX)
    { "launch_fdsweep",   test_proc_launch_fdsweep   },
#endif
    { "wait_timeout",     test_proc_wait_timeout     },
    { "terminate",        test_proc_terminate        },
    { "child_exit42",     test_proc_child_exit42     },
    { "child_sleep",      test_proc_child_sleep      },
    { "child_echoargs",   test_proc_child_echoargs   },
    { "child_echoenv",    test_proc_child_echoenv    },
    { "child_cwd",        test_proc_child_cwd        },
#if defined(_PLATFORM_UNIX)
    { "child_fdcheck",    test_proc_child_fdcheck    },
#endif
    { "grp_env",        test_proc_grp_env        },
    { "grp_enum",       test_proc_grp_enum       },
    { "grp_launch",     test_proc_grp_launch     },
    { 0,                0                        }
};
