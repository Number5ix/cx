#include "process_private.h"

#include <cx/container/foreach.h>
#include <cx/debug/error.h>
#include <cx/string.h>

STR_CONST(kExeSuffix, ".exe");

// The stype ops below delegate to the public functions, so there is exactly one implementation
// of what it means to destroy or copy a ProcessInfo.

static void procInfoDtorOp(stype st, stgeneric* g, uint32 flags)
{
    procInfoDestroy((ProcessInfo*)g->st_opaque);
}

// A copy op is handed raw destination storage rather than a live value, so it initializes
// before copying. procInfoCopy releases whatever the destination held, which would be garbage
// without this.
static void procInfoCopyOp(stype st, stgeneric* gdest, stgeneric gsrc, uint32 flags)
{
    ProcessInfo* dest = (ProcessInfo*)gdest->st_opaque;
    procInfoInit(dest);
    procInfoCopy(dest, (const ProcessInfo*)gsrc.st_opaque);
}

stDefine(ProcessInfo) {
    .id    = stTypeId(opaque),
    .size  = sizeof(ProcessInfo),
    .flags = stFlag(PassPtr),
    .ops   = { .dtor = procInfoDtorOp, .copy = procInfoCopyOp }
};

_Use_decl_annotations_
void procInfoInit(ProcessInfo* info)
{
    memset(info, 0, sizeof(ProcessInfo));
    info->pid  = PROCESS_InvalidID;
    info->ppid = PROCESS_InvalidID;
}

_Use_decl_annotations_
void procInfoDestroy(ProcessInfo* info)
{
    strDestroy(&info->name);
    strDestroy(&info->exepath);
}

_Use_decl_annotations_
void procInfoCopy(ProcessInfo* dest, const ProcessInfo* src)
{
    dest->pid  = src->pid;
    dest->ppid = src->ppid;
    strDup(&dest->name, src->name);
    strDup(&dest->exepath, src->exepath);
}

// Grow the array by one and hand back the new slot. saSetSize zeroes what it adds, so only the
// fields whose "unknown" value is not zero need setting.
ProcessInfo* _procInfoPush(sa_ProcessInfo* out)
{
    int32 n = saSize(*out);
    saSetSize(out, n + 1);

    ProcessInfo* info = &out->a[n];
    info->pid         = PROCESS_InvalidID;
    info->ppid        = PROCESS_InvalidID;
    return info;
}

_Use_decl_annotations_
bool procEnum(sa_ProcessInfo* out, flags_t flags)
{
    saInit(out, ProcessInfo, 128);
    return _procPlatformEnum(out, flags);
}

// Compares two process names the way a person means them: ignoring case, and ignoring a
// Windows ".exe" suffix on either side so one spelling finds the process on every platform.
static bool procNameMatches(strref got, strref want)
{
    if (strEqi(got, want))
        return true;

    string a = 0, b = 0;
    bool ret = false;

    if (strEndsWithi(got, kExeSuffix))
        strSubStr(&a, got, 0, strLen(got) - strLen(kExeSuffix));
    if (strEndsWithi(want, kExeSuffix))
        strSubStr(&b, want, 0, strLen(want) - strLen(kExeSuffix));

    if (a || b)
        ret = strEqi(a ? a : got, b ? b : want);

    strDestroy(&a);
    strDestroy(&b);
    return ret;
}

_Use_decl_annotations_
bool procFind(sa_ProcessInfo* out, strref name, flags_t flags)
{
    sa_ProcessInfo all;

    if (!procEnum(&all, flags))
        return false;

    saInit(out, ProcessInfo, 8);

    for (int32 i = 0; i < saSize(all); i++) {
        if (procNameMatches(all.a[i].name, name))
            saPush(out, ProcessInfo, all.a[i]);
    }

    saDestroy(&all);
    return true;
}

_Use_decl_annotations_
bool procGetInfoByID(ProcessInfo* out, ProcessID pid, flags_t flags)
{
    return _procPlatformGetInfo(out, pid, flags);
}

_Use_decl_annotations_
bool procGetInfo(ProcessInfo* out, Process* proc, flags_t flags)
{
    if (!proc)
        return false;

    return _procPlatformGetInfo(out, proc->pid, flags);
}

_Use_decl_annotations_
Process* procOpen(ProcessID pid)
{
    if (pid == PROCESS_InvalidID) {
        cxerr = CX_InvalidArgument;
        return NULL;
    }

    return _procPlatformOpen(pid);
}

_Use_decl_annotations_
ProcessID procID(Process* proc)
{
    return proc ? proc->pid : PROCESS_InvalidID;
}

_Use_decl_annotations_
bool procRunning(Process* proc)
{
    if (!proc)
        return false;

    // Once the outcome is known it is cached on the object, and stays true after the process is
    // gone. That is what lets the status outlive the process itself.
    if (atomicLoad(bool, &proc->exited, Acquire))
        return false;

    return _procPlatformRunning(proc);
}

// Fill in the object's cached name and path, if they have not been read yet. Both are
// best-effort: a process belonging to another user routinely refuses to report its path.
static void procFillCached(Process* proc)
{
    if (proc->name && proc->exepath)
        return;

    ProcessInfo info;
    procInfoInit(&info);

    if (_procPlatformGetInfo(&info, proc->pid, PROC_EnumFullPath)) {
        if (!proc->name)
            strDup(&proc->name, info.name);
        if (!proc->exepath)
            strDup(&proc->exepath, info.exepath);
    }

    procInfoDestroy(&info);
}

_Use_decl_annotations_
bool procName(Process* proc, string* out)
{
    strClear(out);

    if (!proc)
        return false;

    procFillCached(proc);
    if (strEmpty(proc->name))
        return false;

    strDup(out, proc->name);
    return true;
}

_Use_decl_annotations_
bool procExePath(Process* proc, string* out)
{
    strClear(out);

    if (!proc)
        return false;

    procFillCached(proc);
    if (strEmpty(proc->exepath))
        return false;

    strDup(out, proc->exepath);
    return true;
}
