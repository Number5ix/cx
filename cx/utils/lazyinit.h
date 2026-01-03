/// @file lazyinit.h
/// @brief Thread-safe lazy initialization
///
/// @defgroup utils_lazyinit Lazy Initialization
/// @ingroup utils
/// @{
///
/// Provides thread-safe lazy initialization that ensures an initialization function is
/// called exactly once, even when multiple threads attempt initialization simultaneously.
/// Once initialized, the overhead is minimal (single boolean check).
///
/// The lazy init system uses atomic operations and spin-waiting to coordinate between
/// threads. The first thread to reach the initialization point executes the callback,
/// while concurrent threads spin until initialization completes. After the first
/// initialization, subsequent calls only perform a fast boolean check.
///
/// Example:
/// @code
///   static LazyInitState configState;
///   static Config *globalConfig;
///
///   static void initConfig(void *data) {
///       globalConfig = loadConfigFromDisk();
///   }
///
///   void useConfig() {
///       lazyInit(&configState, initConfig, NULL);
///       // globalConfig is now guaranteed to be initialized
///       processConfig(globalConfig);
///   }
/// @endcode

#pragma once

#include <cx/cx.h>

CX_C_BEGIN

/// State tracker for lazy initialization
typedef struct LazyInitState {
    bool init;           ///< True after initialization completes
    bool initProgress;   ///< True while initialization is in progress
} LazyInitState;

/// Callback function type for lazy initialization
///
/// @param userData Optional user data passed through from lazyInit()
typedef void (*LazyInitCallback)(void* userData);

// Internal implementation for lazy initialization (do not call directly)
void _lazyInitInternal(_Inout_ bool* init, _Inout_ bool* initProgress,
                       _In_ LazyInitCallback initfunc, _In_opt_ void* userData);

/// void lazyInit(LazyInitState *state, LazyInitCallback initfunc, void *userData)
///
/// Ensures the initialization callback is executed exactly once in a thread-safe manner.
///
/// When multiple threads call this function concurrently with the same state:
/// - The first thread executes the callback
/// - Other threads spin-wait until initialization completes
/// - All threads proceed only after initialization finishes
///
/// Once initialized, subsequent calls only check a boolean flag with minimal overhead.
/// The state must remain valid for the lifetime of the lazy-initialized resource.
///
/// @param state Pointer to lazy initialization state (must be zero-initialized)
/// @param initfunc Callback to execute exactly once
/// @param userData Optional user data passed to the callback
_meta_inline void lazyInit(_Inout_ LazyInitState* state, _In_ LazyInitCallback initfunc,
                           _In_opt_ void* userData)
{
    if (!state->init)
        _lazyInitInternal(&state->init, &state->initProgress, initfunc, userData);
}

CX_C_END

/// @}
