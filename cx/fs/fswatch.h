#pragma once

/// @file fswatch.h
/// @brief Watching files and directories for changes

/// @defgroup fs_watch Change Notification
/// @ingroup fs
/// @{

#include <cx/closure/closure.h>
#include <cx/cx.h>
#include <cx/fs/fswatchobj.h>

CX_C_BEGIN

/// @defgroup fs_watch_overview Overview
/// @ingroup fs_watch
/// @{
///
/// A watch reports changes to files and directories as they happen, by calling a closure,
/// instead of making you poll with fsStat() or directory listings. It uses the operating
/// system's own change notification, so watching thousands of paths is cheap.
///
/// @section fs_watch_use Using a watch
///
/// Create a watch with a callback, then add as many paths to it as you like. Every change to
/// any of them calls the same callback, and one fsWatchCancel() stops them all.
///
/// @code
///   static void onChange(stvlist* cvars, FSWatch* watch, FSWatchEvent* ev)
///   {
///       if (ev->kind == FSWE_Modified) {
///           // ev->path changed
///       }
///   }
///
///   FSWatch* w = fsWatchCreate(closureCreateAs(FSWatchCB, onChange, stvNone));
///   fsWatchAdd(w, _SL("/etc/myapp"), FSW_Subtree);
///   fsWatchAdd(w, _SL("/home/me/.myapprc"), 0);
///   // ...
///   fsWatchCancel(w);
///   objRelease(&w);
/// @endcode
///
/// @section fs_watch_targets What a path covers
///
/// - **A directory** reports changes to the entries directly in it. With FSW_Subtree it reports
///   changes anywhere below it, including in directories created after the watch started. Add
///   FSW_Self to also hear about the directory itself: its attributes, and it being deleted or
///   renamed.
/// - **Anything else** -- a file, or a name that does not exist yet -- is watched by name. It
///   keeps working when the file is deleted and created again, or replaced by renaming another
///   file over it, which is how most programs save.
///
/// A directory that is deleted or moved away sends FSWE_Stopped -- preceded by FSWE_Removed
/// with FSW_Self -- and is no longer watched. A path watched by name stops only when the
/// directory it is in goes away, with FSWE_Removed and FSWE_Stopped. Moving a directory further
/// up than the watched path is not noticed; the watch keeps its old path.
///
/// Symbolic links inside a watched directory are reported like files, and never followed.
///
/// @section fs_watch_events Events
///
/// Each callback describes one change. ev->path is an absolute path, and ev->target is the path
/// you passed to fsWatchAdd() that it falls under. When a change falls under more than one of
/// a watch's paths, it is reported once, against the most specific one.
///
/// Treat events as hints that something changed, not as an exact log:
/// - The same change can be reported more than once, and several changes can arrive as one.
/// - Something created and deleted again very quickly may not be reported at all.
/// - A rename is reported as FSWE_Renamed only when both the old and new names are watched;
///   otherwise it arrives as FSWE_Removed or FSWE_Created.
/// - Whenever changes might have been missed, an FSWE_Rescan names the directory to look at
///   again. Nothing is ever lost without one.
///
/// @section fs_watch_threads Threads
///
/// Callbacks may run on any thread, so protect anything they share with the rest of your
/// program. A callback may add or remove paths, cancel or release any watch -- including its
/// own -- and make other filesystem calls.
///
/// fsWatchCancel() waits for a callback already running for that watch to return, unless it is
/// called from inside a callback. fsWatchRemove() does not wait, so an event for a path may
/// still arrive just after the path is removed.
///
/// Releasing the last reference also stops a watch, but a running callback holds a reference of
/// its own, so objRelease() never waits for one. Call fsWatchCancel() before releasing when the
/// callback uses anything you are about to free.
///
/// @section fs_watch_vfs Watching a VFS
///
/// vfsWatchCreate() makes a watch that takes and reports VFS paths. Each path is watched through
/// every mounted provider that can see it, so a change in any layer is reported. Providers that
/// cannot report changes are skipped; fsWatchAdd() fails with CX_NotSupported only when none of
/// them can.
///
/// - A change in a lower layer is reported even when a higher layer hides that file. Call
///   vfsStat() if you need to know what the VFS shows now.
/// - Mounting or unmounting under a watched path sends an FSWE_Rescan for the mount point, and
///   the watch then follows whatever is mounted there.
/// - A layer that does not have the watched path yet, or loses it while another layer still has
///   it, is watched for the path to appear there. When it does, an FSWE_Rescan is sent for the
///   target, since whatever appeared along with it was not seen.
/// - Changes made through the VFS to a provider that cannot be watched are not reported.
///
/// @section fs_watch_limits Platform limits
///
/// - **Linux** limits how many directories one user can watch (`fs.inotify.max_user_watches`).
///   A subtree needs one for each directory in it. Reaching the limit fails fsWatchAdd() with
///   CX_ResourceLimit.
/// - **FreeBSD** before 15 holds an open file for each watched directory, and for each file in a
///   directory where contents or attributes are watched, so large subtrees count against the
///   open file limit (CX_ResourceLimit when reached). A filesystem with watched directories on
///   it cannot be unmounted.
/// - **Windows** reports writes and attribute changes the same way. A watch that asked only for
///   FSW_Attributes gets writes as FSWE_Attributes; one that asked for FSW_Contents gets
///   attribute changes as FSWE_Modified. A directory watched with FSW_Self gets FSWE_Attributes
///   whenever its entries change, too. A folder that contains a watched directory cannot be
///   renamed while the watch is active. Without FSW_Self, a watched directory that is renamed is
///   noticed, and stopped, only when something next changes inside it; with FSW_Self its
///   parent directory is watched too, which notices at once.
/// - **WebAssembly** cannot watch the native filesystem; fsWatchAdd() fails with
///   CX_NotSupported.
///
/// @}  // end of fs_watch_overview group

/// Flags for fsWatchAdd()
///
/// Passing none of FSW_Names, FSW_Contents and FSW_Attributes is the same as passing all three.
enum FSWatchFlags {
    FSW_Subtree    = 0x0001,   ///< Watch everything below a directory, not only its entries
    FSW_Self       = 0x0002,   ///< Also report changes to the watched directory itself
    FSW_Names      = 0x0010,   ///< Report entries being created, removed and renamed
    FSW_Contents   = 0x0020,   ///< Report files being written to
    FSW_Attributes = 0x0040,   ///< Report changes to timestamps, permissions and similar
    FSW_Everything = FSW_Names | FSW_Contents | FSW_Attributes,   ///< All of the above
};

/// What happened to a watched path
typedef enum FSWatchEventKind {
    FSWE_Created = 1,   ///< A file or directory was created, or moved in
    FSWE_Removed,       ///< A file or directory was deleted, or moved away
    FSWE_Modified,      ///< A file's contents changed
    FSWE_Renamed,       ///< Renamed from oldpath to path
    FSWE_Attributes,    ///< Timestamps, permissions or similar changed
    FSWE_Rescan,        ///< Changes may have been missed; look at path again
    FSWE_Stopped,       ///< The target is no longer watched, because it went away; always sent
} FSWatchEventKind;

/// One change, as passed to an FSWatchCB callback
///
/// The strings belong to the watch and are valid only until the callback returns. Copy any you
/// need to keep.
typedef struct FSWatchEvent {
    FSWatchEventKind kind;   ///< What happened
    strref path;             ///< Absolute path of what changed; the new name for FSWE_Renamed
    strref oldpath;          ///< The old name for FSWE_Renamed, otherwise NULL
    strref target;           ///< The path passed to fsWatchAdd() that this change falls under
} FSWatchEvent;

/// Callback for a watch, called through a typed closure
///
/// @param cvars Variables captured when the closure was created
/// @param watch The watch reporting the change
/// @param ev The change
typedef void (*FSWatchCB)(_In_ stvlist* cvars, _In_ FSWatch* watch, _In_ FSWatchEvent* ev);

/// Creates a watch on the native filesystem
///
/// The watch starts out empty; add paths to it with fsWatchAdd(). Takes ownership of the
/// closure, which must have been made with closureCreateAs(FSWatchCB, ...).
///
/// @param cls Closure called for every change
/// @return The new watch; release it with objRelease()
///
/// Example:
/// @code
///   FSWatch* w = fsWatchCreate(closureCreateAs(FSWatchCB, onChange, stvar(ptr, state)));
///   if (!fsWatchAdd(w, _SL("/srv/data"), FSW_Subtree | FSW_Names))
///       // cxerr says why
/// @endcode
_Ret_valid_ FSWatch* fsWatchCreate(_In_ closure cls);

/// @}  // end of fs_watch group

CX_C_END
