#pragma once

#include <cx/container/hashtable.h>
#include <cx/container/sarray.h>
#include <cx/fs/fswatch.h>
#include <cx/fs/fswatchnative.h>
#include <cx/fs/fswatchrooted.h>

CX_C_BEGIN

// Paths compare case-insensitively where the native filesystem does.
#if defined(_PLATFORM_WIN)
#define FSWATCH_CASEI 1
#else
#define FSWATCH_CASEI 0
#endif

// ---- dispatch (fswatch.c) ------------------------------------------------------------------
//
// Every callback runs on one dispatch thread, fed by a FIFO queue. Queued events hold a weak
// reference to their watch, so nothing internal keeps a watch alive: releasing the last
// reference destroys it, and anything still queued for it is dropped.

// Starts the dispatch thread if it is not running yet. Called whenever a watch is created.
void _fsWatchDispatchInit(void);

// Is the calling thread the one that runs callbacks?
bool _fsWatchOnDispatchThread(void);

// Queues an event for a watch. w must stay valid for the duration of the call, but need not
// be held; wref is cloned for the queue. Drops the event if the watch is cancelled, and turns
// it into an owed Rescan if the watch's backlog is full.
void _fsWatchQueue(_In_ FSWatch* w, _In_ FSWatch_WeakRef* wref, FSWatchEventKind kind,
                   _In_opt_ strref path, _In_opt_ strref oldpath, _In_opt_ strref target);

// Hands an event to a watch: calls it right away when already on the dispatch thread,
// otherwise queues it. For layered watches forwarding what an inner watch reported.
void _fsWatchDeliver(_In_ FSWatch* w, _In_ FSWatchEvent* ev);

// Stops a watch like fsWatchCancel(), but without waiting for a callback that is already
// running. For a watch used internally, whose callbacks cope with outliving it: waiting can
// deadlock when the running callback needs a lock the canceller holds.
void _fsWatchStop(_In_ FSWatch* w);

// Keep a watch's list of target paths current, for the Rescan owed after an overflow.
void _fsWatchTargetAdded(_In_ FSWatch* w, _In_ strref path);
void _fsWatchTargetRemoved(_In_ FSWatch* w, _In_ strref path);

// How many undelivered events one watch may have queued before further events are dropped
// and replaced by a Rescan. For tests; returns the previous value.
int32 _fsWatchSetBacklogCap(int32 cap);

// Is path the same as base, or somewhere below it?
bool _fsWatchPathWithin(_In_ strref path, _In_ strref base, bool casei);

// Would a target at tpath -- a directory target when tisdir -- report this kind of change at
// path, ignoring its filter flags? Shared by every kind of watch, so they all agree.
bool _fsWatchCovers(_In_ strref tpath, flags_t tflags, bool tisdir, _In_ strref path,
                    FSWatchEventKind kind, bool casei);

// The FSW_ filter flag a target needs to hear about this kind of change.
flags_t _fsWatchKindFilter(FSWatchEventKind kind);

// path relative to root: "" for root itself. A path not under root is copied unchanged.
void _fsWatchStripRoot(_Inout_ string* out, _In_ strref path, _In_ strref root, bool casei);

// ---- rooted watches (fswatchrooted.c) -----------------------------------------------------

// The closure to create a rooted watch's inner watch with. Typical use:
//   FSWatchRooted* w = fswatchrootedCreate(root, casei, cls);
//   w->inner = fsWatchCreate(_fsWatchRootedInnerCls(w));
_Ret_valid_ closure _fsWatchRootedInnerCls(_In_ FSWatchRooted* self);

// ---- native watches (fswatchnative.c) ------------------------------------------------------
//
// All native watch state belongs to one I/O thread. fsWatchAdd() and friends send it commands
// and wait for the answer; the platform backend reports what the OS says from inside
// _fsWatchPlatformWait(), which only the I/O thread calls. None of the functions below take a
// lock, and none may be called from any other thread.
//
// A directory the OS is asked to watch is an FSWDir. It is shared by every target that needs
// it -- a watched directory, the directory holding a file watched by name, or each directory
// inside a watched subtree -- and exists for as long as any of them does.

typedef struct FSWDir {
    string path;       // absolute cx path
    flags_t mask;      // FSW_Names/Contents/Attributes wanted by any user
    bool recursive;    // FSWATCH_NATIVE_RECURSIVE backends: also report everything below it
    bool nofollow;     // reached by walking a subtree, so a symlink here must not be followed
    sa_ptr users;      // FSWTarget* relying on this directory
    void* os;          // backend state
} FSWDir;

// FSWATCH_NATIVE_RECURSIVE: one OS watch can cover a whole subtree, so the core need not walk
// it. FSWATCH_NO_SELF_EVENTS: a watched directory is never told about its own rename, so for a
// target with FSW_Self the core also watches its parent.
#if defined(_PLATFORM_WIN)
#define FSWATCH_NATIVE_RECURSIVE 1
#define FSWATCH_NO_SELF_EVENTS   1
#else
#define FSWATCH_NATIVE_RECURSIVE 0
#define FSWATCH_NO_SELF_EVENTS   0
#endif

// Called by fswatchnative.c, only on the I/O thread.

// One-time setup. Returning false means this platform cannot watch the native filesystem;
// fsWatchAdd() then fails with CX_NotSupported and no I/O thread is started.
bool _fsWatchPlatformInit(void);

// Start, adjust or stop watching a directory. AddDir and UpdateDir set cxerr on failure.
// RemoveDir is called exactly once for each directory AddDir succeeded on.
bool _fsWatchPlatformAddDir(_Inout_ FSWDir* d);
bool _fsWatchPlatformUpdateDir(_Inout_ FSWDir* d);
void _fsWatchPlatformRemoveDir(_Inout_ FSWDir* d);

// Lists a directory's entries, without following symbolic links: a link is reported as not a
// directory, whatever it points to.
typedef void (*FSWListCB)(_In_ void* ctx, _In_ strref name, bool isdir);
bool _fsWatchPlatformList(_In_ strref path, _In_ FSWListCB cb, _In_ void* ctx);

// Block up to timeout for the OS to report changes, passing them to the functions below. Must
// return early when _fsWatchPlatformWake() is called.
void _fsWatchPlatformWait(int64 timeout);

// Nudge a blocked _fsWatchPlatformWait, from any thread.
void _fsWatchPlatformWake(void);

// Called by the backend from inside _fsWatchPlatformWait.

// Something changed at an absolute path. isdir is 1 or 0 when the OS says whether the entry is
// a directory, and -1 when it does not. oldpath is only for FSWE_Renamed.
void _fsWatchRaw(FSWatchEventKind kind, _In_ strref path, _In_opt_ strref oldpath, int isdir);

// A watched directory was deleted, moved, or its filesystem unmounted. d may be freed before
// this returns.
void _fsWatchDirGone(_In_ FSWDir* d);

// Is there a directory target at path that wants to hear about itself (FSW_Self)? For backends
// that cannot tell a directory's own attribute changes from its contents changing.
bool _fsWatchIsSelfTarget(_In_ strref path);

// The OS dropped events it could not queue: for one directory, or for all of them when d is NULL.
void _fsWatchOverflow(_In_opt_ FSWDir* d);

CX_C_END
