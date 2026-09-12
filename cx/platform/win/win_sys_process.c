#include "cx/sys/process_private.h"
#include "cx/debug/error.h"
#include "cx/fs/path.h"
#include "cx/platform/win.h"
#include "cx/platform/win/win_sys_processobj.h"
#include "cx/string.h"
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

_Use_decl_annotations_
ProcessID procCurrentID(void)
{
    return (ProcessID)GetCurrentProcessId();
}
