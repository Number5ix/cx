/// @file ccallbacks.h
/// @brief Pre-built closure callback functions for common operations
///
/// @defgroup utils_ccallbacks Closure Callbacks
/// @ingroup utils
/// @{
///
/// Provides ready-to-use closure callback functions for common operations like event
/// signaling and task management. These functions are designed to work with the closure
/// and closure chain systems (see @ref closure_basic and @ref closure_chain).
///
/// These callbacks are particularly useful for deferred execution, event handling, and
/// coordination between asynchronous operations. Each callback expects specific captured
/// variables (cvars) as documented.
///
/// Example usage with closure chains:
/// @code
///   SharedEvent *sev = sheventCreate();
///   cchain myChain = NULL;
///
///   // Attach callback that will signal and release the shared event when chain is called
///   // Acquire a reference to hand off to the callback
///   cchainAttach(&myChain, ccbSignalSharedEvent, stvar(ptr, sheventAcquire(sev)));
///
///   // Later, when chain is called, the event will be signaled and released
///   cchainCall(&myChain);
///
///   // Wait for event on another thread
///   eventWait(&sev->ev, timeForever);
///   sheventRelease(&sev);
///
///   cchainDestroy(&myChain);
/// @endcode

#pragma once

#include <cx/closure/closure.h>

/// Signal an event
///
/// Closure callback that signals an Event. Expects the event pointer as the first captured
/// variable (cvar). Ignores call-time arguments.
///
/// Captured variables:
/// - cvar[0]: `Event*` - Pointer to event to signal
///
/// @param cvars Captured variables containing Event pointer
/// @param args Call-time arguments (ignored)
/// @return true on success, false if event pointer is NULL
bool ccbSignalEvent(stvlist* cvars, stvlist* args);

/// Signal and release a shared event
///
/// Closure callback that signals a SharedEvent and then releases the reference. Expects
/// the shared event pointer as the first captured variable. Ignores call-time arguments.
///
/// This is useful for one-time event notifications where the shared event reference should
/// be released after signaling.
///
/// Captured variables:
/// - cvar[0]: `SharedEvent*` - Pointer to shared event to signal and release
///
/// @param cvars Captured variables containing SharedEvent pointer
/// @param args Call-time arguments (ignored)
/// @return true on success, false if shared event pointer is NULL
bool ccbSignalSharedEvent(stvlist* cvars, stvlist* args);

/// Advance a complex task
///
/// Closure callback that advances a ComplexTask given a weak reference. Acquires the task
/// from the weak reference, advances it, then releases the task. If the weak reference is
/// invalid (task destroyed), does nothing. Ignores call-time arguments.
///
/// This is useful for scheduling task advancement in response to events or callbacks.
///
/// Captured variables:
/// - cvar[0]: `Weak(ComplexTask)*` - Weak reference to the task to advance
///
/// @param cvars Captured variables containing weak task reference
/// @param args Call-time arguments (ignored)
/// @return true on success, false if weak reference is NULL or task is destroyed
bool ccbAdvanceTask(stvlist* cvars, stvlist* args);

/// @}
