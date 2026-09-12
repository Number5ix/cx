#include "cx/sys/process_private.h"
#include "cx/container/foreach.h"
#include "cx/debug/error.h"
#include "cx/fs/fs.h"
#include "cx/fs/path.h"
#include "cx/platform/win/win_fs.h"
#include "cx/platform/win.h"
#include "cx/platform/win/win_sys_processobj.h"
#include "cx/string.h"
#include "cx/sys/env.h"
#include "cx/thread/atomic.h"
#include "cx/thread/event.h"
#include "cx/thread/mutex.h"
#include "cx/time/clock.h"
#include "cx/time/time.h"
#include "cx/utils/lazyinit.h"

#include <psapi.h>
#include <tlhelp32.h>

// Three ways to ask for a process's image path, none of which works everywhere.
// QueryFullProcessImageNameW is Vista and later; the other two come from psapi and go back
// further. All are resolved at runtime, so a single binary works on every OS cx supports and
// none of them becomes a load-time import that would stop the process from starting at all.
typedef BOOL(WINAPI* LPFN_QFPINW)(HANDLE, DWORD, LPWSTR, PDWORD);
typedef DWORD(WINAPI* LPFN_GPIFNW)(HANDLE, LPWSTR, DWORD);
typedef DWORD(WINAPI* LPFN_GMFNEW)(HANDLE, HMODULE, LPWSTR, DWORD);

static LPFN_QFPINW fnQueryFullProcessImageNameW;
static LPFN_GPIFNW fnGetProcessImageFileNameW;
static LPFN_GMFNEW fnGetModuleFileNameExW;

static LazyInitState procApiInitState;

static void procApiInit(void* unused)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE psapi    = LoadLibraryW(L"psapi.dll");

    if (kernel32)
        fnQueryFullProcessImageNameW = (LPFN_QFPINW)GetProcAddress(kernel32,
                                                                   "QueryFullProcessImageNameW");

    if (psapi) {
        fnGetProcessImageFileNameW = (LPFN_GPIFNW)GetProcAddress(psapi,
                                                                 "GetProcessImageFileNameW");
        fnGetModuleFileNameExW     = (LPFN_GMFNEW)GetProcAddress(psapi, "GetModuleFileNameExW");
    }
}

// Opens a process with the rights cx needs, stepping down until one is granted.
//
// PROCESS_QUERY_LIMITED_INFORMATION comes first deliberately: it is enough for liveness, exit
// codes, image paths and the timing/memory counters a future stats call would want, and it is
// granted for processes that refuse PROCESS_QUERY_INFORMATION outright. On an OS that predates
// it, OpenProcess just fails and the next rung runs.
static HANDLE procOpenHandle(ProcessID pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (h)
        return h;

    h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (h)
        return h;

    return OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, (DWORD)pid);
}

// Best-effort image path for an open handle.
static bool procPathFromHandle(string* out, HANDLE h)
{
    lazyInit(&procApiInitState, procApiInit, NULL);

    wchar_t buf[1024];
    DWORD len = ARRAYSIZE(buf);

    if (fnQueryFullProcessImageNameW && fnQueryFullProcessImageNameW(h, 0, buf, &len) && len > 0) {
        strFromUTF16(out, buf, len);
        return true;
    }

    if (fnGetModuleFileNameExW) {
        len = fnGetModuleFileNameExW(h, NULL, buf, ARRAYSIZE(buf));
        if (len > 0) {
            strFromUTF16(out, buf, len);
            return true;
        }
    }

    if (fnGetProcessImageFileNameW) {
        len = fnGetProcessImageFileNameW(h, buf, ARRAYSIZE(buf));
        if (len > 0) {
            strFromUTF16(out, buf, len);
            return true;
        }
    }

    return false;
}

static void procResolvePath(ProcessInfo* info, flags_t flags)
{
    if (!(flags & PROC_EnumFullPath))
        return;

    HANDLE h = procOpenHandle(info->pid);
    if (!h)
        return;

    if (procPathFromHandle(&info->exepath, h)) {
        // th32ExeFile is only the file name, and it is the same one the path ends with, so
        // this changes nothing except where the value came from -- but it keeps name and
        // exepath consistent on every platform.
        pathFilename(&info->name, info->exepath);
    }

    CloseHandle(h);
}

// Takes a Toolhelp snapshot, retrying briefly on ERROR_BAD_LENGTH. That error means the process
// list changed while the snapshot was being taken on a busy machine, and it succeeds on a
// retry; it is not a real failure.
static HANDLE procSnapshot(void)
{
    for (int attempt = 0; attempt < 4; attempt++) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE)
            return snap;

        if (GetLastError() != ERROR_BAD_LENGTH)
            break;
    }

    return INVALID_HANDLE_VALUE;
}

_Use_decl_annotations_
bool _procPlatformEnum(sa_ProcessInfo* out, flags_t flags)
{
    HANDLE snap = procSnapshot();
    if (snap == INVALID_HANDLE_VALUE)
        return winMapLastError();

    PROCESSENTRY32W pe = { 0 };
    pe.dwSize          = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            ProcessInfo* info = _procInfoPush(out);
            info->pid         = (ProcessID)pe.th32ProcessID;
            info->ppid        = (ProcessID)pe.th32ParentProcessID;
            strFromUTF16(&info->name, pe.szExeFile, cstrLenw(pe.szExeFile));

            procResolvePath(info, flags);
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return true;
}

_Use_decl_annotations_
bool _procPlatformGetInfo(ProcessInfo* out, ProcessID pid, flags_t flags)
{
    HANDLE snap = procSnapshot();
    if (snap == INVALID_HANDLE_VALUE)
        return winMapLastError();

    PROCESSENTRY32W pe = { 0 };
    pe.dwSize          = sizeof(pe);
    bool found         = false;

    if (Process32FirstW(snap, &pe)) {
        do {
            if ((ProcessID)pe.th32ProcessID != pid)
                continue;

            out->pid  = pid;
            out->ppid = (ProcessID)pe.th32ParentProcessID;
            strFromUTF16(&out->name, pe.szExeFile, cstrLenw(pe.szExeFile));
            procResolvePath(out, flags);
            found = true;
            break;
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);

    if (!found)
        cxerr = CX_FileNotFound;

    return found;
}

_Use_decl_annotations_
Process* _procPlatformOpen(ProcessID pid)
{
    HANDLE h = procOpenHandle(pid);
    if (!h) {
        winMapLastError();
        return NULL;
    }

    WinProcess* wproc = _winprocobjCreate();
    wproc->h          = h;
    wproc->pid        = pid;
    wproc->ischild    = false;

    return Process(wproc);
}

bool _procPlatformRunning(Process* proc)
{
    WinProcess* wproc = objDynCast(WinProcess, proc);
    if (!wproc || !wproc->h)
        return false;

    // Deliberately not GetExitCodeProcess/STILL_ACTIVE: a process that exits with code 259 is
    // indistinguishable from a running one that way. Waiting with a zero timeout answers the
    // question exactly.
    return WaitForSingleObject(wproc->h, 0) == WAIT_TIMEOUT;
}

// ---- launching -------------------------------------------------------------------------------

// Vista and later. Declared locally because the XP-era SDKs cx can be built with do not have
// them, and because the attribute-list type itself may be missing -- void* keeps these typedefs
// compilable everywhere.
#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif
#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002
#endif

typedef BOOL(WINAPI* LPFN_IPTAL)(void*, DWORD, DWORD, PSIZE_T);
typedef BOOL(WINAPI* LPFN_UPTA)(void*, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T);
typedef VOID(WINAPI* LPFN_DPTAL)(void*);

static LPFN_IPTAL fnInitializeProcThreadAttributeList;
static LPFN_UPTA fnUpdateProcThreadAttribute;
static LPFN_DPTAL fnDeleteProcThreadAttributeList;

static LazyInitState procAttrInitState;

// Serializes launches when the attribute list is unavailable; see procLaunchFallbackNote below.
static Mutex procLaunchLock;

static void procAttrInit(void* unused)
{
    mutexInit(&procLaunchLock);

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!kernel32)
        return;

    fnInitializeProcThreadAttributeList = (LPFN_IPTAL)GetProcAddress(kernel32,
                                                                     "InitializeProcThreadAttributeList");
    fnUpdateProcThreadAttribute         = (LPFN_UPTA)GetProcAddress(kernel32,
                                                            "UpdateProcThreadAttribute");
    fnDeleteProcThreadAttributeList     = (LPFN_DPTAL)GetProcAddress(kernel32,
                                                                 "DeleteProcThreadAttributeList");
}

static bool procHaveAttrList(void)
{
    lazyInit(&procAttrInitState, procAttrInit, NULL);
    return fnInitializeProcThreadAttributeList && fnUpdateProcThreadAttribute &&
           fnDeleteProcThreadAttributeList;
}

// Does this argument have to be quoted for CommandLineToArgvW to give it back unchanged?
static bool argNeedsQuotes(strref a)
{
    if (strEmpty(a))
        return true;   // an empty argument only survives as ""

    uint32 n = strLen(a);
    for (uint32 i = 0; i < n; i++) {
        uint8 c = strGetChar(a, i);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '"')
            return true;
    }

    return false;
}

// Appends one argument using CommandLineToArgvW's quoting rules, which are not the same as any
// shell's:
//
//   - a run of N backslashes immediately before a quote becomes 2N backslashes plus \",
//   - a run of N backslashes at the end of a quoted argument becomes 2N,
//   - backslashes anywhere else are literal.
//
// Nothing here escapes cmd.exe metacharacters, because cx never launches through a shell.
static void appendArg(string* cmd, strref arg)
{
    if (!argNeedsQuotes(arg)) {
        strAppend(cmd, arg);
        return;
    }

    uint32 n = strLen(arg);
    strAppendChar(cmd, '"');

    for (uint32 i = 0; i < n; i++) {
        uint32 slashes = 0;
        while (i < n && strGetChar(arg, i) == '\\') {
            slashes++;
            i++;
        }

        if (i == n) {
            // Trailing run, about to meet the closing quote: double it so the quote stays a
            // delimiter rather than being escaped by the last backslash.
            for (uint32 j = 0; j < slashes * 2; j++) strAppendChar(cmd, '\\');
            break;
        }

        uint8 c = strGetChar(arg, i);
        if (c == '"') {
            for (uint32 j = 0; j < slashes * 2 + 1; j++) strAppendChar(cmd, '\\');
        } else {
            for (uint32 j = 0; j < slashes; j++) strAppendChar(cmd, '\\');
        }

        strAppendChar(cmd, c);
    }

    strAppendChar(cmd, '"');
}

// argv[0] is special: CommandLineToArgvW does no escape processing on it and ends it at the
// first space unless the whole thing is quoted.
static void appendArgv0(string* cmd, strref exe)
{
    if (strFind(exe, 0, _SL(" ")) == -1) {
        strAppend(cmd, exe);
        return;
    }

    strAppendChar(cmd, '"');
    strAppend(cmd, exe);
    strAppendChar(cmd, '"');
}

static void buildCommandLine(string* cmd, strref exe, sa_string args)
{
    strClear(cmd);
    appendArgv0(cmd, exe);

    for (int32 i = 0; i < saSize(args); i++) {
        strAppendChar(cmd, ' ');
        appendArg(cmd, args.a[i]);
    }
}

// The child's environment block: NAME=VALUE runs separated by NULs and terminated by an extra
// one. Returns NULL when the caller asked for no changes, meaning "inherit exactly".
static wchar_t* buildEnvBlock(const ProcessOpts* opts)
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

    sa_string entries;
    saInit(&entries, string, 32);
    string ent = 0;

    foreach (hashtable, it, env) {
        strNConcat(&ent, htiKey(string, it), _SL("="), htiVal(string, it));
        saPush(&entries, string, ent);
    }

    strDestroy(&ent);
    htDestroy(&env);

    // Conventionally the block is sorted. Windows does not enforce it for the wide form, but
    // matching what it hands out costs nothing.
    saSort(&entries, false);

    // One wide buffer holding every "NAME=VALUE\0" back to back, plus the NUL that terminates
    // the block. strToUTF16 counts the entry's own NUL in the size it reports, so each entry
    // advances by exactly that much -- adding one more would leave a second NUL after every
    // entry, and a double NUL is what ends the block. Windows would then see an environment
    // holding only the first variable.
    size_t wtotal = 1;
    for (int32 i = 0; i < saSize(entries); i++) wtotal += strToUTF16(entries.a[i], NULL, 0);

    wchar_t* block = xaAlloc(wtotal * sizeof(wchar_t), XA_Zero);
    size_t off     = 0;

    for (int32 i = 0; i < saSize(entries); i++) {
        size_t need = strToUTF16(entries.a[i], NULL, 0);
        strToUTF16(entries.a[i], (uint16*)(block + off), need);
        off += need;
    }

    block[off] = 0;

    saDestroy(&entries);
    return block;
}

// An inheritable copy of a handle, or NULL.
//
// The child can only receive a handle that is marked inheritable, and the attribute list refuses
// one that is not. Duplicating rather than calling SetHandleInformation leaves the caller's own
// std handles untouched, and it makes the three entries distinct even when stdout and stderr are
// the same handle -- which they are whenever the output is redirected to one place, and which a
// handle list will not accept twice.
static HANDLE procDupInheritable(HANDLE h)
{
    HANDLE dup = NULL;

    if (!h || h == INVALID_HANDLE_VALUE)
        return NULL;

    if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, TRUE,
                         DUPLICATE_SAME_ACCESS))
        return NULL;

    return dup;
}

_Use_decl_annotations_
Process* _procPlatformLaunch(strref exe, sa_string args, const ProcessOpts* opts)
{
    lazyInit(&procAttrInitState, procAttrInit, NULL);

    // cx paths are normalized with forward slashes; Win32 wants the platform form, and every
    // other Win32 call site in cx converts before handing one over. argv[0] gets the plain
    // platform spelling, while the application name goes through the NT form, which also lifts
    // the MAX_PATH limit.
    string platexe = 0;
    pathToPlatform(&platexe, exe);

    string cmdline = 0;
    buildCommandLine(&cmdline, platexe, args);

    wchar_t* wcmd   = strToUTF16A(cmdline);
    wchar_t* wenv   = buildEnvBlock(opts);
    wchar_t* wdir   = (opts && !strEmpty(opts->workdir)) ? strToUTF16A(opts->workdir) : NULL;
    strDestroy(&cmdline);
    strDestroy(&platexe);

    DWORD flags = 0;
    if (wenv)
        flags |= CREATE_UNICODE_ENVIRONMENT;
    if (opts && (opts->flags & PROC_NewGroup))
        flags |= CREATE_NEW_PROCESS_GROUP;
    if (opts && (opts->flags & PROC_NoWindow))
        flags |= CREATE_NO_WINDOW;

    bool detached = opts && (opts->flags & PROC_Detached);
    if (detached)
        flags |= DETACHED_PROCESS;

    HANDLE devnull = INVALID_HANDLE_VALUE;
    if (opts && opts->stdio == PROC_StdioNull) {
        devnull = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    }

    HANDLE srcin  = (devnull != INVALID_HANDLE_VALUE) ? devnull : GetStdHandle(STD_INPUT_HANDLE);
    HANDLE srcout = (devnull != INVALID_HANDLE_VALUE) ? devnull : GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE srcerr = (devnull != INVALID_HANDLE_VALUE) ? devnull : GetStdHandle(STD_ERROR_HANDLE);

    // Inheritable copies, so nothing here depends on how the caller's own handles happen to be
    // marked. Any of the three may be absent -- a process run by a build server often has no
    // stdin at all -- and passing one of those on would fail the launch outright.
    HANDLE hin  = procDupInheritable(srcin);
    HANDLE hout = procDupInheritable(srcout);
    HANDLE herr = procDupInheritable(srcerr);

    // DETACHED_PROCESS conflicts with handing over std handles, and all three have to be real
    // for STARTF_USESTDHANDLES to describe a complete set.
    bool usestd = !detached && hin && hout && herr;

    STARTUPINFOEXW six;
    memset(&six, 0, sizeof(six));
    six.StartupInfo.cb = sizeof(STARTUPINFOW);

    if (usestd) {
        six.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
        six.StartupInfo.hStdInput  = hin;
        six.StartupInfo.hStdOutput = hout;
        six.StartupInfo.hStdError  = herr;
    }

    PROCESS_INFORMATION pinfo = { 0 };
    HANDLE hlist[3]           = { hin, hout, herr };
    void* attrlist            = NULL;
    bool locked               = false;
    BOOL ok                   = FALSE;

    if (usestd && procHaveAttrList()) {
        // Naming exactly these three handles is the only way to inherit them without also
        // handing the child every other inheritable handle in the process -- including one
        // another thread happens to create while this call is running. They are distinct
        // duplicates, so the list never repeats a handle.
        SIZE_T sz = 0;
        fnInitializeProcThreadAttributeList(NULL, 1, 0, &sz);
        attrlist = xaAlloc(sz);

        if (fnInitializeProcThreadAttributeList(attrlist, 1, 0, &sz) &&
            fnUpdateProcThreadAttribute(attrlist, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hlist,
                                        sizeof(hlist), NULL, NULL)) {
            six.StartupInfo.cb  = sizeof(six);
            six.lpAttributeList = attrlist;
            flags |= EXTENDED_STARTUPINFO_PRESENT;
        } else {
            xaFree(attrlist);
            attrlist = NULL;
        }
    } else if (usestd) {
        // No attribute list on this OS, so the child inherits everything inheritable. cx's own
        // handles are never marked inheritable, so the exposure is the host application's own,
        // and serializing launches keeps cx's calls from widening each other's window.
        mutexAcquire(&procLaunchLock);
        locked = true;
    }

    // fsPathToNT hands back a scratch buffer, so it is converted here and consumed in the same
    // expression rather than carried across the setup above.
    ok = CreateProcessW(fsPathToNT(exe), wcmd, NULL, NULL, usestd ? TRUE : FALSE, flags, wenv,
                        wdir, &six.StartupInfo, &pinfo);

    if (locked)
        mutexRelease(&procLaunchLock);

    if (attrlist) {
        fnDeleteProcThreadAttributeList(attrlist);
        xaFree(attrlist);
    }

    // The child has its own copies now.
    if (hin)
        CloseHandle(hin);
    if (hout)
        CloseHandle(hout);
    if (herr)
        CloseHandle(herr);

    if (devnull != INVALID_HANDLE_VALUE)
        CloseHandle(devnull);

    xaFree(wcmd);
    if (wenv)
        xaFree(wenv);
    if (wdir)
        xaFree(wdir);

    if (!ok) {
        winMapLastError();
        return NULL;
    }

    // The thread handle is of no use here.
    CloseHandle(pinfo.hThread);

    WinProcess* wproc = _winprocobjCreate();
    wproc->h          = pinfo.hProcess;
    wproc->pid        = (ProcessID)pinfo.dwProcessId;
    wproc->ischild    = true;

    _procWatchRegister(Process(wproc));

    return Process(wproc);
}

// Cache a finished process's exit code on the handle, so it survives however long the caller
// keeps asking.
static void publishExit(Process* proc, DWORD code)
{
    mutexAcquire(&proc->lock);
    proc->exitcode   = (int32)code;
    proc->termsignal = 0;
    atomicStore(bool, &proc->exited, true, Release);
    mutexRelease(&proc->lock);
}

bool _procPlatformWait(Process* proc, int64 timeout)
{
    WinProcess* wproc = objDynCast(WinProcess, proc);
    if (!wproc || !wproc->h)
        return false;

    DWORD ms = (timeout == timeForever) ? INFINITE : (DWORD)timeToMsec(timeout);
    if (WaitForSingleObject(wproc->h, ms) != WAIT_OBJECT_0)
        return false;

    DWORD code = 0;
    if (GetExitCodeProcess(wproc->h, &code))
        publishExit(proc, code);

    return true;
}

bool _procPlatformTerminate(Process* proc, bool force)
{
    WinProcess* wproc = objDynCast(WinProcess, proc);
    if (!wproc || !wproc->h)
        return false;

    // Windows has no deliverable "please stop" for an arbitrary process, so both forms are
    // immediate here. The header says so.
    if (TerminateProcess(wproc->h, force ? 1 : 0))
        return true;

    return winMapLastError();
}

bool _procPlatformExitCode(Process* proc, int32* code)
{
    WinProcess* wproc = objDynCast(WinProcess, proc);
    if (!wproc || !wproc->h)
        return false;

    // Confirm it has actually exited before reading the code: a process that exits with 259 is
    // indistinguishable from a running one by GetExitCodeProcess alone.
    if (WaitForSingleObject(wproc->h, 0) != WAIT_OBJECT_0)
        return false;

    DWORD raw = 0;
    if (!GetExitCodeProcess(wproc->h, &raw))
        return winMapLastError();

    publishExit(proc, raw);
    *code = (int32)raw;
    return true;
}

void _procReapPending(void)
{
    // Nothing to do: a Windows process handle keeps the exit status available on its own, so
    // there is no such thing as a zombie to collect here.
}

_Use_decl_annotations_
ProcessID procCurrentID(void)
{
    return (ProcessID)GetCurrentProcessId();
}

// ---- exit watcher ------------------------------------------------------------------------
//
// RegisterWaitForSingleObject rather than WaitForMultipleObjects: the latter is capped at
// MAXIMUM_WAIT_OBJECTS, which with a wake handle leaves 63 watchable processes and simply fails
// past that. Registered waits have no such limit, and UnregisterWaitEx with INVALID_HANDLE_VALUE
// is exactly the "block until any in-flight callback has returned" primitive shutdown needs.
// It is a Windows 2000 API, so no XP split is needed here.

static Event procWatchEvent;
static bool procWatchReady;

// The system pool callback deliberately does no user work: it records the outcome, hands the
// process to cx's own watcher thread and returns. That keeps every user callback on one cx
// thread, one at a time, on every platform -- rather than on a pool thread with its own rules
// about what may block.
// Set while the pool callback is running, so _procWatchPlatformRemove can tell whether it is
// being called from inside the very callback it would otherwise block on.
static atomic(uintptr) procWatchCbThread;

static DWORD procWatchCallbackThread(void)
{
    return (DWORD)atomicLoad(uintptr, &procWatchCbThread, Acquire);
}

static VOID CALLBACK procWaitCallback(PVOID param, BOOLEAN timedout)
{
    atomicStore(uintptr, &procWatchCbThread, (uintptr)GetCurrentThreadId(), Release);

    Process* proc     = (Process*)param;
    WinProcess* wproc = objDynCast(WinProcess, proc);

    if (timedout || !wproc) {
        atomicStore(uintptr, &procWatchCbThread, 0, Release);
        return;
    }

    DWORD code = 0;
    if (GetExitCodeProcess(wproc->h, &code))
        publishExit(proc, code);

    _procWatchCompleted(proc);

    atomicStore(uintptr, &procWatchCbThread, 0, Release);
}

bool _procWatchPlatformInit(void)
{
    eventInit(&procWatchEvent);
    procWatchReady = true;
    return true;
}

bool _procWatchPlatformAdd(Process* proc)
{
    WinProcess* wproc = objDynCast(WinProcess, proc);
    if (!wproc || !wproc->h)
        return false;

    HANDLE wait = NULL;
    if (!RegisterWaitForSingleObject(&wait, wproc->h, procWaitCallback, proc, INFINITE,
                                     WT_EXECUTEONLYONCE))
        return false;

    wproc->wait = wait;
    return true;
}

void _procWatchPlatformRemove(Process* proc)
{
    WinProcess* wproc = objDynCast(WinProcess, proc);
    if (!wproc || !wproc->wait)
        return;

    HANDLE wait = wproc->wait;
    wproc->wait = NULL;

    // INVALID_HANDLE_VALUE means "wait for a callback that is already running to finish", which
    // would deadlock if this were called from inside that callback. It is not: removal happens
    // on the watcher thread, or from a registration that failed before any callback could run.
    UnregisterWaitEx(wait, (GetCurrentThreadId() == procWatchCallbackThread())
                               ? NULL
                               : INVALID_HANDLE_VALUE);
}

void _procWatchPlatformWake(void)
{
    if (procWatchReady)
        eventSignal(&procWatchEvent);
}

void _procWatchPlatformWait(int64 timeout)
{
    if (!procWatchReady)
        return;

    // eventSignal, never eventSignalAll: a broadcast raised while nothing is waiting is dropped,
    // where a plain signal stays latched until the single waiter arrives. The waiter re-checks
    // the completion list after waking regardless.
    eventWaitTimeout(&procWatchEvent, (uint64)timeout);
}
