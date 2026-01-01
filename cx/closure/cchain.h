/// @file cchain.h
/// @brief Thread-safe closure chains for event callbacks
/// @defgroup closure_chain Closure Chains
/// @ingroup closure
/// @{
///
/// Thread-safe linked lists of closures for implementing multi-callback event systems.
///
/// Closure chains are the recommended way to implement event handlers that can have multiple
/// subscribers. They provide thread-safe attachment and detachment of callbacks, with optional
/// tokens for selective removal.
///
/// Key features:
/// - Thread-safe attachment/detachment using atomic operations
/// - Optional tokens to identify specific callback instances
/// - One-shot mode with cchainCallOnce() for non-renewable events
/// - Transfer and clone operations for chain management
///
/// The token, if provided, is automatically added as the last captured variable in each
/// closure, after any variables passed to cchainAttach().
///
/// Basic usage:
/// @code
///   cchain eventChain = NULL;
///
///   bool handler1(stvlist *cvars, stvlist *args) {
///       string event = stvlNextPtr(string, args);
///       // handle event...
///       return true;
///   }
///
///   bool handler2(stvlist *cvars, stvlist *args) {
///       int captured = stvlNextInt(cvars);
///       // handle event with captured data...
///       return true;
///   }
///
///   // Attach handlers
///   cchainAttach(&eventChain, handler1);
///   cchainAttach(&eventChain, handler2, stvar(int32, 42));
///
///   // Fire event - calls all handlers
///   cchainCall(&eventChain, stvar(string, _S"myEvent"));
///
///   // Cleanup
///   cchainDestroy(&eventChain);
/// @endcode
///
/// Using tokens for selective detachment:
/// @code
///   intptr token = 12345;
///   cchainAttachToken(&chain, myHandler, token, stvar(int32, data));
///   // Later...
///   cchainDetach(&chain, myHandler, token);  // Removes only this instance
/// @endcode

#pragma once

#include <cx/closure/closure.h>
#include <cx/thread/atomic.h>

typedef struct cchain_ref
{
    void* _is_closure_chain;
} cchain_ref;

_Success_(return) bool _cchainAttach(_Inout_ptr_opt_ cchain* chain, _In_ closureFunc func, intptr token, int n,
                   stvar cvars[]);

/// bool cchainAttach(cchain *chain, closureFunc func, ...)
///
/// Attach a closure to the chain without a token.
///
/// Adds a new closure to the chain with the specified function and captured variables.
/// Can fail if the chain has been consumed by cchainCallOnce().
///
/// @param chain Pointer to closure chain (may be NULL, will be initialized)
/// @param func Closure function to attach
/// @param ... Zero or more stvar arguments to capture
/// @return true on success, false if the chain is invalid
#define cchainAttach(chain, func, ...) _cchainAttach(chain, func, 0, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

/// bool cchainAttachToken(cchain *chain, closureFunc func, intptr token, ...)
///
/// Attach a closure to the chain with an identifying token.
///
/// The token should be a nonzero value that can be used to selectively detach this specific
/// closure instance using cchainDetach(). The token is automatically added as the last
/// captured variable passed to the closure function.
///
/// @param chain Pointer to closure chain (may be NULL, will be initialized)
/// @param func Closure function to attach
/// @param token Nonzero identifier for this closure instance
/// @param ... Zero or more stvar arguments to capture
/// @return true on success, false if the chain is invalid
#define cchainAttachToken(chain, func, token, ...) _cchainAttach(chain, func, token, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

/// Detach a closure from the chain
///
/// Attempts to remove a previously attached closure from the chain. The function pointer
/// and token must both match the values used when attaching (use 0 for token if none was
/// specified).
///
/// @param chain Pointer to closure chain
/// @param func Closure function to detach
/// @param token Token that was used when attaching (0 if none)
/// @return true if the closure was found and removed, false otherwise
_Success_(
    return) bool cchainDetach(_Inout_ptr_opt_ cchain* chain, _In_ closureFunc func, intptr token);

_Success_(return) bool _cchainCall(_In_ptr_opt_ cchain* chain, int n, stvar args[]);

/// bool cchainCall(cchain *chain, ...)
///
/// Call all closures in the chain with the given arguments.
///
/// Invokes each closure in the chain, passing the provided arguments. Returns true only if
/// all closures return true. The chain is cloned internally before calling, so callbacks
/// can take arbitrary time without blocking other threads.
///
/// Note: Unlike closureCall(), this requires a pointer to the chain (not the chain itself)
/// to perform atomic reads for thread safety.
///
/// @param chain Pointer to closure chain
/// @param ... Zero or more stvar arguments to pass to each closure
/// @return true if all closures returned true, false otherwise or if chain is invalid
#define cchainCall(chain, ...) _cchainCall(chain, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

_Success_(return) bool _cchainCallOnce(_Inout_ptr_opt_ cchain* chain, int n, stvar args[]);

/// bool cchainCallOnce(cchain *chain, ...)
///
/// Call all closures in the chain once, then invalidate the chain.
///
/// Atomically calls all closures and marks the chain as invalid, preventing future attachments.
/// This is useful for one-time events to avoid race conditions where callbacks might be added
/// after the event has already fired.
///
/// @param chain Pointer to closure chain (will be invalidated)
/// @param ... Zero or more stvar arguments to pass to each closure
/// @return true if all closures returned true, false otherwise or if chain was already invalid
#define cchainCallOnce(chain, ...) _cchainCallOnce(chain, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

/// Transfer a closure chain to a new location
///
/// Safely moves the chain from src to dest, invalidating the source. The transferred chain
/// is inserted before any existing closures in dest. Future attach attempts to src will fail.
///
/// @param dest Destination chain pointer
/// @param src Source chain pointer (will be invalidated)
/// @return true on success, false if dest is invalid
_Success_(return) bool cchainTransfer(_Inout_ptr_opt_ cchain* dest, _Inout_ptr_opt_ cchain* src);

/// Clone a closure chain to a new location
///
/// Creates a copy of all closures in src and adds them to dest. Unlike transfer, the source
/// chain remains valid and usable. Cloned closures are inserted before any existing closures
/// in dest.
///
/// @param dest Destination chain pointer
/// @param src Source chain pointer (remains valid)
/// @return true on success, false if dest is invalid
_Success_(return) bool cchainClone(_Inout_ptr_opt_ cchain* dest, _In_ptr_opt_ cchain* src);

/// Reset a previously invalidated closure chain
///
/// Resets a chain that was invalidated by cchainCallOnce() or cchainTransfer() so it can be
/// reused. Only works on chains that are in the invalid state.
///
/// @param chain Pointer to invalidated closure chain
/// @return true if the chain was reset, false if it was not invalidated
_Success_(return) bool cchainReset(_Inout_ptr_opt_ cchain* chain);

/// Remove all closures from the chain
///
/// Clears all closures from the chain but leaves it in a valid state for future attachments.
/// Unlike cchainDestroy(), the chain can be reused after calling this.
///
/// @param chain Pointer to closure chain
/// @return true on success, false if chain is invalid
_Success_(return) bool cchainClear(_Inout_ptr_opt_ cchain* chain);

/// Destroy a closure chain and all its closures
///
/// Frees all closures and invalidates the chain. The chain cannot be used after this call.
///
/// @param chain Pointer to closure chain
void cchainDestroy(_Inout_ptr_opt_ cchain* chain);

/// @}
// end of closure_chain group
