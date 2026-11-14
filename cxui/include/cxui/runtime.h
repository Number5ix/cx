#pragma once

/// @file runtime.h
/// @brief Starting and stopping cxui, and running work on the UI thread

#include <cx/closure.h>
#include <cx/taskqueue.h>
#include <cxui/cxui_shared.h>

CX_C_BEGIN

/// @defgroup cxui_runtime Runtime
/// @ingroup cxui
/// @{
/// Starting and stopping the UI thread, and getting work onto it.
///
/// These functions are safe to call from **any thread**. Every other cxui function must be called
/// on the UI thread, except where its documentation says otherwise.
///
/// A program that uses cxui defines its entry point with CXUI_DEFINE_ENTRY_POINT (cxui/entry.h)
/// rather than DEFINE_ENTRY_POINT. On macOS, where the user interface must run on the process's
/// main thread, entryPoint() then runs on another thread and the main thread is kept for cxui.

/// Called when the user asks to quit the program from outside its windows
///
/// Raised on the UI thread, with no arguments, for Quit in the macOS Dock; other platforms don't
/// have one. The program isn't ended: it decides what to do. Attach with cchainAttach().
extern cchain cxuiQuitRequestNotify;

/// Called when the UI is about to stop
///
/// Raised on the UI thread, with no arguments, by the cxuiShutdown() that stops the UI, before
/// windows are closed and queued work is dropped. Code that put widgets into a program's windows,
/// such as a library sharing the program's UI, takes them down here. The closures are removed
/// once called, so attach again after the next cxuiStart(). Attach with cchainAttach(), from any
/// thread.
///
/// Example:
/// @code
///   if (cxuiIsRunning()) {
///       cchainAttach(&cxuiShutdownNotify, consoleDetach, stvar(ptr, con));
///       cxuiPost(closureCreate(consoleAttach, stvar(ptr, con)));
///   }
/// @endcode
extern cchain cxuiShutdownNotify;

/// Starts the UI thread, or joins the one already running
///
/// Each successful call must be matched by a call to cxuiShutdown(). If the UI is already running,
/// the call only counts the new caller, and `desc` is ignored.
///
/// @param desc Options, or NULL for the defaults
/// @return true if the UI is running
///
/// Example:
/// @code
///   if (!cxuiStart(&(CxUiStartDesc){ .name = _S"Debug console" }))
///       return false;
/// @endcode
_Check_return_ bool cxuiStart(_In_opt_ const CxUiStartDesc* desc);

/// Stops the UI thread once every cxuiStart() has been matched
///
/// The last call closes all windows, drops any work still queued, and waits for the UI thread to
/// finish. That last call must not be made on the UI thread.
void cxuiShutdown(void);

/// Checks whether the UI is running
/// @return true between a successful cxuiStart() and the cxuiShutdown() that stops the UI
bool cxuiIsRunning(void);

/// Checks whether the calling thread is the UI thread
/// @return true on the UI thread
bool cxuiIsUIThread(void);

/// Gets the UI queue
///
/// Work added to this queue runs on the UI thread, including scheduled and deferred tasks.
///
/// @return The UI queue with a reference acquired (release it with tqRelease()), or NULL if the
/// UI isn't running
_Ret_maybenull_ TaskQueue* cxuiQueue(void);

/// Runs a closure on the UI thread
///
/// Returns at once; the closure runs later. It is called as
/// `closureCall(cls, stvar(object, queue))`. Ownership of the closure passes to cxui whatever
/// happens: it is destroyed after it runs, or right away if the UI isn't running.
///
/// @param cls Closure to run
/// @return true if the closure was queued
///
/// Example:
/// @code
///   cxuiPost(closureCreate(updateStatus, stvar(ptr, app)));
/// @endcode
bool cxuiPost(_In_ closure cls);

/// Runs a closure on the UI thread and waits for it to finish
///
/// On the UI thread the closure runs at once. Ownership of the closure passes to cxui whatever
/// happens.
///
/// Code on the UI thread must never wait for a thread that may be inside cxuiCallSync(): the UI
/// thread would never get to run the call it is waiting for.
///
/// @param cls Closure to run; it is called with no arguments
/// @return The closure's return value, or false if the UI isn't running or stopped before the
/// closure could run
bool cxuiCallSync(_In_ closure cls);

/// Lays out windows with pending changes, handles pending input and painting, and runs queued
/// work, without waiting
///
/// For code on the UI thread that runs for a long time and wants the UI to stay responsive, such
/// as a loop updating a progress window. A nested call does nothing. UI thread only.
void cxuiFlush(void);

/// @}  // end of cxui_runtime group

CX_C_END
