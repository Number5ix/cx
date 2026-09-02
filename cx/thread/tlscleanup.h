/// @file tlscleanup.h
/// @brief Per-thread cleanup callback registration
/// @defgroup thread_tlscleanup Thread-Local Cleanup
/// @ingroup thread
/// @{
///
/// Lets code register a callback to run automatically when the calling thread exits,
/// so thread-local resources can be freed without every thread entry point having to
/// know about them.
///
/// A typical use is lazily-allocated thread-local state: allocate it on first use,
/// and register a cleanup callback at the same time so it gets freed when the thread
/// ends, even if the thread never explicitly tears it down.
///
/// @code
///   static _Thread_local MyPerThreadData* data;
///
///   static void myDataCleanup(void* unused)
///   {
///       xaDestroy(&data);
///   }
///
///   MyPerThreadData* getMyData(void)
///   {
///       if (!data) {
///           data = xaAllocStruct(MyPerThreadData);
///           thrRegisterCleanup(myDataCleanup, NULL);
///       }
///       return data;
///   }
/// @endcode
///
/// A callback must be registered from the thread it applies to. There is no way to
/// register a cleanup callback for a different thread.

#pragma once

#include <cx/cx.h>

/// void (*TLSCleanupCB)(void *data)
///
/// Callback invoked when the registering thread exits.
///
/// @param data The pointer passed to thrRegisterCleanup() alongside this callback
typedef void (*TLSCleanupCB)(void* data);

/// Register a callback to run when the calling thread exits
///
/// The callback is invoked once, on the same thread that registered it, as that
/// thread exits. Multiple callbacks may be registered by the same thread; they run
/// in the order they were registered.
/// @param cb Callback to invoke on thread exit
/// @param data Value passed to the callback
CX_C void thrRegisterCleanup(TLSCleanupCB cb, void* data);

/// @}
// end of thread_tlscleanup group
