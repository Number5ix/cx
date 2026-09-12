#pragma once

/// @file process.h
/// @brief Launching, inspecting and controlling operating system processes

/// @defgroup sys_process Processes
/// @ingroup sys
/// @{

#include <cx/container/hashtable.h>
#include <cx/container/sarray.h>
#include <cx/cx.h>
#include <cx/sys/processobj.h>

CX_C_BEGIN

/// @defgroup sys_process_overview Overview
/// @ingroup sys_process
/// @{
///
/// Two things live in this module: a handle to one process, and a snapshot of the processes
/// running on the machine.
///
/// @section sys_process_handle The process handle
///
/// A Process is a reference-counted object. procOpen() attaches to a process by id and returns
/// one. Release it with procRelease() when finished, or objAcquire() it to keep a second
/// reference. Releasing is safe at any time and from any thread; it does not disturb the
/// process itself, only cx's handle on it.
///
/// @code
///   Process* proc = procOpen(pid);
///   if (proc) {
///       if (procRunning(proc)) {
///           // still alive
///       }
///       procRelease(&proc);
///   }
/// @endcode
///
/// @section sys_process_besteffort What the system will and will not tell you
///
/// A process id and a process name can always be read. A parent process id usually can. A full
/// executable path frequently cannot: on Unix it is readable only for processes belonging to
/// the same user, and on Windows some protected processes refuse to be opened at all. None of
/// this is treated as an error. A field the system will not report comes back as an empty
/// string, or as PROCESS_InvalidID for a parent id, and the call still succeeds.
///
/// Names are shortened by the operating system too: Linux reports the first 15 characters and
/// FreeBSD about 19. Where the executable path is readable, its filename is used for the name
/// instead, so the name is the real one wherever permissions allow.
///
/// @section sys_process_snapshot Enumeration is a snapshot
///
/// procEnum() returns a list of the processes running at the moment it was called, not a live
/// view. Entries can be gone by the time the list is read, and processes started afterwards do
/// not appear. Every platform produces the list this way, so nothing is lost by saying so.
///
/// procEnum() tells failure apart from emptiness: it returns false only when the process list
/// could not be read at all. Individual processes that disappear while the list is being built
/// are skipped, and the call still succeeds.
///
/// Resolving executable paths costs one extra system call per process, and on Windows a burst
/// of them is exactly the pattern endpoint security products flag, so it is opt-in through
/// PROC_EnumFullPath rather than always on.
///
/// @section sys_process_exitcode Exit codes
///
/// procWait() and procRunning() work for any process on every platform. Reading an exit *code*
/// with procExitCode() is the one place the platforms genuinely differ: on Unix only a process's
/// own parent can collect its exit status, so a process reached through procOpen() rather than
/// procLaunch() cannot report one. There procExitCode() returns false and sets cxerr to
/// CX_NotSupported. Windows has no such restriction. A process cx launched itself reports its
/// exit code everywhere.
///
/// Once a launched process has finished, its exit code is remembered on the handle, so
/// procRunning(), procWait() and procExitCode() keep answering correctly afterwards and never
/// have to ask the operating system again.
///
/// @section sys_process_shell No shell
///
/// procLaunch() runs an executable directly and never goes through a shell, so nothing in the
/// arguments is expanded, redirected or split. On Windows that means a `.bat` or `.cmd` file
/// cannot be launched on its own -- run `cmd.exe` with `/c` and the script as arguments.
///
/// @}

/// Operating system process id
typedef int64 ProcessID;

/// A process id that does not refer to anything
#define PROCESS_InvalidID ((ProcessID)-1)

/// One process in a snapshot of the running process list
///
/// Follows the Init/Destroy pattern: the struct itself is the caller's, and procInfoDestroy()
/// releases what is inside it without freeing the struct.
typedef struct ProcessInfo {
    ProcessID pid;    ///< Process id; always present
    ProcessID ppid;   ///< Parent process id, or PROCESS_InvalidID if it could not be read
    string name;      ///< Process name; always present
    string exepath;   ///< Full path to the executable, or empty if it could not be read
} ProcessInfo;

stDeclare(ProcessInfo);
saDeclare(ProcessInfo);
#define SType_ProcessInfo                         ProcessInfo*
#define STStorageType_ProcessInfo                 ProcessInfo
#define STypeArg_ProcessInfo(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_ProcessInfo(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_ProcessInfo(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_ProcessInfo(type, val) stType(type), stArgPtr(type, val)

/// Flags for procEnum() and procFind()
enum ProcEnumFlags {
    /// @brief Also resolve each process's full executable path
    ///
    /// Much slower, and the path is still unreadable for processes belonging to other users.
    PROC_EnumFullPath = 0x0001,
};

/// Initializes a ProcessInfo to empty.
///
/// @param info Structure to initialize
///
/// Example:
/// @code
///   ProcessInfo info;
///   procInfoInit(&info);
/// @endcode
void procInfoInit(_Out_ ProcessInfo* info);

/// Releases everything inside a ProcessInfo, leaving the struct itself in place.
///
/// @param info Structure to clean up
///
/// Example:
/// @code
///   procInfoDestroy(&info);
/// @endcode
void procInfoDestroy(_Inout_ ProcessInfo* info);

/// Copies one ProcessInfo over another.
///
/// @param dest Initialized structure to copy into; its previous contents are released
/// @param src Structure to copy from
///
/// Example:
/// @code
///   ProcessInfo copy;
///   procInfoInit(&copy);
///   procInfoCopy(&copy, &list.a[0]);
/// @endcode
void procInfoCopy(_Inout_ ProcessInfo* dest, _In_ const ProcessInfo* src);

/// Lists the processes running on the machine.
///
/// @param out Receives the list; initialized by this call, destroy it with saDestroy()
/// @param flags Optional ProcEnumFlags
/// @return true on success, false if the process list could not be read at all
///
/// Example:
/// @code
///   sa_ProcessInfo procs;
///   if (procEnum(&procs, 0)) {
///       foreach (sarray, i, ProcessInfo, p, procs) {
///           // use p.pid and p.name
///       }
///       saDestroy(&procs);
///   }
/// @endcode
bool procEnum(_Inout_ sa_ProcessInfo* out, flags_t flags);

/// Lists the running processes whose name matches.
///
/// Matching ignores case, and an ".exe" suffix on either side, on every platform. Looking for
/// "test_runner" therefore finds "test_runner.exe" on Windows.
///
/// @param out Receives the matching processes; initialized by this call, destroy it with
///            saDestroy()
/// @param name Process name to look for
/// @param flags Optional ProcEnumFlags
/// @return true on success, false if the process list could not be read at all
///
/// Example:
/// @code
///   sa_ProcessInfo found;
///   if (procFind(&found, _SL("test_runner"), 0)) {
///       // saSize(found) processes are running under that name
///       saDestroy(&found);
///   }
/// @endcode
bool procFind(_Inout_ sa_ProcessInfo* out, _In_ strref name, flags_t flags);

/// Looks up one process by id.
///
/// @param out Initialized structure to fill in
/// @param pid Process to look up
/// @param flags Optional ProcEnumFlags
/// @return true if the process exists, false otherwise
///
/// Example:
/// @code
///   ProcessInfo info;
///   procInfoInit(&info);
///   if (procGetInfoByID(&info, pid, PROC_EnumFullPath)) {
///       // use info.name
///   }
///   procInfoDestroy(&info);
/// @endcode
bool procGetInfoByID(_Inout_ ProcessInfo* out, ProcessID pid, flags_t flags);

/// Looks up the process a handle refers to.
///
/// @param out Initialized structure to fill in
/// @param proc Process to look up
/// @param flags Optional ProcEnumFlags
/// @return true if the process exists, false otherwise
///
/// Example:
/// @code
///   ProcessInfo info;
///   procInfoInit(&info);
///   procGetInfo(&info, proc, 0);
///   procInfoDestroy(&info);
/// @endcode
bool procGetInfo(_Inout_ ProcessInfo* out, _In_ Process* proc, flags_t flags);

/// Returns the id of the calling process.
///
/// @return This process's id
///
/// Example:
/// @code
///   ProcessID self = procCurrentID();
/// @endcode
ProcessID procCurrentID(void);

/// Where a launched process's standard input, output and error go
typedef enum ProcStdioEnum {
    PROC_StdioInherit = 0,   ///< The child shares the caller's stdin, stdout and stderr
    PROC_StdioNull,          ///< The child's stdio is discarded (/dev/null, or NUL on Windows)
} ProcStdio;

/// Flags for procLaunch()
enum ProcLaunchFlags {
    /// @brief Put the child in its own session so it outlives the caller
    PROC_Detached = 0x0001,

    /// @brief Give the child its own process group, so it can be signalled as a group
    PROC_NewGroup = 0x0002,

    /// @brief Windows: do not give the child a console window. Ignored elsewhere.
    PROC_NoWindow = 0x0004,
};

/// Options for procLaunch()
///
/// Initialize with procOptsInit() and clean up with procOptsDestroy(). Passing NULL to
/// procLaunch() instead inherits everything from the calling process.
typedef struct ProcessOpts {
    string workdir;      ///< Directory to start the child in; empty inherits the caller's
    ProcStdio stdio;     ///< What to do with the child's stdin, stdout and stderr
    hashtable env;       ///< Variables to set in the child, name to value
    sa_string envUnset;  ///< Variables to remove from the child
    flags_t flags;       ///< ProcLaunchFlags
} ProcessOpts;

/// Initializes launch options to "inherit everything".
///
/// @param opts Options to initialize
///
/// Example:
/// @code
///   ProcessOpts opts;
///   procOptsInit(&opts);
/// @endcode
void procOptsInit(_Out_ ProcessOpts* opts);

/// Releases everything inside a ProcessOpts, leaving the struct itself in place.
///
/// @param opts Options to clean up
///
/// Example:
/// @code
///   procOptsDestroy(&opts);
/// @endcode
void procOptsDestroy(_Inout_ ProcessOpts* opts);

/// Sets an environment variable in the child, on top of what it inherits.
///
/// @param opts Options to modify
/// @param name Variable name
/// @param val Value to give it; NULL or empty gives the child an empty variable
///
/// Example:
/// @code
///   procOptsSetEnv(&opts, _SL("CX_MODE"), _SL("fast"));
/// @endcode
void procOptsSetEnv(_Inout_ ProcessOpts* opts, _In_ strref name, _In_opt_ strref val);

/// Removes an inherited environment variable from the child.
///
/// This is separate from procOptsSetEnv() with an empty value, because removing a variable and
/// setting it to nothing are different things to the program that reads it.
///
/// @param opts Options to modify
/// @param name Variable name
///
/// Example:
/// @code
///   procOptsUnsetEnv(&opts, _SL("LD_PRELOAD"));
/// @endcode
void procOptsUnsetEnv(_Inout_ ProcessOpts* opts, _In_ strref name);

/// Launches a program.
///
/// @param exe Path to the executable to run
/// @param args Arguments to pass, not counting the program name itself, which cx supplies
/// @param opts Launch options, or NULL to inherit everything from this process
/// @return A handle to the new process, or NULL if it could not be started; sets cxerr
///
/// Example:
/// @code
///   sa_string args;
///   saInit(&args, string, 2);
///   saPush(&args, string, _SL("--verbose"));
///
///   Process* proc = procLaunch(_SL("/usr/bin/tool"), args, NULL);
///   if (proc) {
///       procWait(proc, timeForever);
///       procRelease(&proc);
///   }
///   saDestroy(&args);
/// @endcode
_Ret_opt_valid_ Process* procLaunch(_In_ strref exe, sa_string args, _In_opt_ ProcessOpts* opts);

/// Attaches to a process that is already running.
///
/// @param pid Process to attach to
/// @return A new handle, or NULL if the process does not exist or cannot be opened; sets cxerr
///
/// Example:
/// @code
///   Process* proc = procOpen(pid);
///   if (proc)
///       procRelease(&proc);
/// @endcode
_Ret_opt_valid_ Process* procOpen(ProcessID pid);

/// void procRelease(Process **pproc);
///
/// Releases a reference to a process handle.
///
/// The handle is destroyed once the last reference goes, and the pointer is set to NULL. This
/// does nothing to the process itself.
///
/// @param pproc Pointer to the handle to release
///
/// Example:
/// @code
///   procRelease(&proc);
/// @endcode
#define procRelease(pproc) objRelease(pproc)

/// Returns the id of the process a handle refers to.
///
/// @param proc Process handle
/// @return The process id
///
/// Example:
/// @code
///   ProcessID pid = procID(proc);
/// @endcode
ProcessID procID(_In_ Process* proc);

/// Checks whether a process is still running.
///
/// @param proc Process handle
/// @return true if the process is still running
///
/// Example:
/// @code
///   if (procRunning(proc)) {
///       // still alive
///   }
/// @endcode
bool procRunning(_In_ Process* proc);

/// Waits for a process to finish.
///
/// @param proc Process handle
/// @param timeout How long to wait, in microseconds; timeForever waits indefinitely
/// @return true if the process has finished, false if the timeout ran out first
///
/// Example:
/// @code
///   if (!procWait(proc, timeS(5))) {
///       // still running after five seconds
///       procTerminate(proc, false);
///   }
/// @endcode
bool procWait(_In_ Process* proc, int64 timeout);

/// Reads the exit code of a finished process.
///
/// Only meaningful once the process has finished. See the overview for the one case this
/// cannot answer: a process on Unix that cx did not launch itself.
///
/// @param proc Process handle
/// @param code Receives the exit code
/// @return true if an exit code is available; sets cxerr to CX_NotSupported where it can
///         never be
///
/// Example:
/// @code
///   int32 code;
///   procWait(proc, timeForever);
///   if (procExitCode(proc, &code)) {
///       // the process exited with 'code'
///   }
/// @endcode
bool procExitCode(_In_ Process* proc, _Out_ int32* code);

/// Asks a process to stop, or forces it to.
///
/// A polite request lets the process shut down on its own terms and can be ignored; a forced
/// stop cannot be caught and gives the process no chance to clean up.
///
/// @param proc Process handle
/// @param force false asks the process to stop (SIGTERM); true kills it outright (SIGKILL, or
///              TerminateProcess on Windows, which is always immediate)
/// @return true if the request was delivered
///
/// Example:
/// @code
///   procTerminate(proc, false);
///   if (!procWait(proc, timeS(5)))
///       procTerminate(proc, true);
/// @endcode
bool procTerminate(_In_ Process* proc, bool force);

/// Reads the name of the process a handle refers to.
///
/// @param out Receives the name; prior contents are destroyed
/// @param proc Process handle
/// @return true if a name could be read
///
/// Example:
/// @code
///   string name = 0;
///   procName(proc, &name);
///   strDestroy(&name);
/// @endcode
bool procName(_In_ Process* proc, _Inout_ string* out);

/// Reads the full path to a process's executable.
///
/// Frequently unavailable for processes belonging to other users, in which case this returns
/// false and clears the output.
///
/// @param out Receives the path; prior contents are destroyed
/// @param proc Process handle
/// @return true if a path could be read
///
/// Example:
/// @code
///   string path = 0;
///   if (procExePath(proc, &path)) {
///       // use path
///   }
///   strDestroy(&path);
/// @endcode
bool procExePath(_In_ Process* proc, _Inout_ string* out);

CX_C_END

/// @}
