/// @file cchain.h
/// @brief Thread-safe closure chains for event callbacks
/// @defgroup closure_chain Closure Chains
/// @ingroup closure
/// @{
///
/// A closure chain is a list of closures that are called together, for events that can have more
/// than one listener. Any thread can attach, detach or call at any time.
///
/// - A `cchain` set to NULL is an empty chain, ready to use.
/// - Closures run **newest first**: the most recently attached closure is called first.
/// - Every closure is called, even after one returns false.
/// - A call runs the closures that were attached when it started. Attaching or detaching during a
///   call, from another thread or from one of its closures, takes effect from the next call. So a
///   closure can still run once after cchainDetach() returns, if another thread was already calling
///   the chain; its captured variables stay valid until then.
/// - Closures run on the calling thread with no lock held, so they may attach, detach or call the
///   same chain.
/// - A token identifies one attached closure for cchainDetach(). It is passed to the closure as the
///   last captured variable, after the ones given to cchainAttachToken().
///
/// cchainCallOnce(), cchainDestroy(), and cchainTransfer() (on its source) **consume** a chain.
/// After that, attaching fails and calling returns false, so a closure can't be attached after a
/// one-time event has already fired. cchainReset() makes a consumed chain usable again.
///
/// Basic usage:
/// @code
///   cchain eventChain = NULL;
///
///   bool handler1(stvlist *cvars, stvlist *args) {
///       strref event = stvlAt(args, 0, strref);
///       // handle event...
///       return true;
///   }
///
///   bool handler2(stvlist *cvars, stvlist *args) {
///       int32 captured = stvlAt(cvars, 0, int32);
///       // handle event with captured data...
///       return true;
///   }
///
///   // Attach handlers
///   cchainAttach(&eventChain, handler1, stvNone);
///   cchainAttach(&eventChain, handler2, stvar(int32, 42));
///
///   // Fire event - calls handler2, then handler1
///   cchainCall(&eventChain, stvar(strref, _SL("myEvent")));
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

CX_C_BEGIN

typedef struct cchain_ref {
    void* _is_closure_chain;
} cchain_ref;

_Success_(return) bool _cchainAttach(_Inout_ptr_opt_ cchain* chain, _In_ closureFunc func, intptr token,
                              int n, stvar cvars[]);

/// bool cchainAttach(cchain *chain, closureFunc func, ...)
///
/// Attach a closure to the chain without a token.
///
/// @param chain Pointer to closure chain
/// @param func Closure function to attach
/// @param ... One or more stvar arguments to capture (`stvNone` for none)
/// @return true on success, false if the chain has been consumed
#define cchainAttach(chain, func, ...) \
    (_stvarArgsGuard(__VA_ARGS__),   \
     _cchainAttach(chain, func, 0, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ }))

/// bool cchainAttachToken(cchain *chain, closureFunc func, intptr token, ...)
///
/// Attach a closure to the chain with an identifying token.
///
/// @param chain Pointer to closure chain
/// @param func Closure function to attach
/// @param token Nonzero identifier for this closure instance, used by cchainDetach()
/// @param ... One or more stvar arguments to capture (`stvNone` for none)
/// @return true on success, false if the chain has been consumed
#define cchainAttachToken(chain, func, token, ...) \
    (_stvarArgsGuard(__VA_ARGS__),   \
     _cchainAttach(chain, func, token, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ }))

/// Detach a closure from the chain
///
/// The function and token must both match the ones used when attaching (0 if there was no token).
/// If that pair was attached more than once, only the most recently attached one is removed.
///
/// @param chain Pointer to closure chain
/// @param func Closure function to detach
/// @param token Token that was used when attaching (0 if none)
/// @return true if the closure was found and removed, false otherwise
_Success_(return) bool cchainDetach(_Inout_ptr_opt_ cchain* chain, _In_ closureFunc func, intptr token);

_Success_(return) bool _cchainCall(_In_ptr_opt_ cchain* chain, int n, stvar args[]);

/// bool cchainCall(cchain *chain, ...)
///
/// Call all closures in the chain with the given arguments.
///
/// Unlike closureCall(), this takes a pointer to the chain.
///
/// @param chain Pointer to closure chain
/// @param ... One or more stvar arguments to pass to each closure (`stvNone` for none)
/// @return true if every closure returned true (or the chain is empty), false if any returned
/// false or the chain has been consumed
#define cchainCall(chain, ...) \
    (_stvarArgsGuard(__VA_ARGS__),   \
     _cchainCall(chain, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ }))

_Success_(return) bool _cchainCallOnce(_Inout_ptr_opt_ cchain* chain, int n, stvar args[]);

/// bool cchainCallOnce(cchain *chain, ...)
///
/// Consume the chain and call all of its closures.
///
/// The chain is consumed before the first closure runs, so any later cchainAttach() fails instead
/// of attaching a closure that would never be called. Use this for one-time events.
///
/// @param chain Pointer to closure chain
/// @param ... One or more stvar arguments to pass to each closure (`stvNone` for none)
/// @return true if every closure returned true (or the chain is empty), false if any returned
/// false or the chain was already consumed
#define cchainCallOnce(chain, ...) \
    (_stvarArgsGuard(__VA_ARGS__),   \
     _cchainCallOnce(chain, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ }))

/// Transfer a closure chain to a new location
///
/// Moves every closure from src to dest and consumes src. The moved closures run before the ones
/// already in dest.
///
/// @param dest Destination chain pointer
/// @param src Source chain pointer
/// @return true on success, false if either chain has been consumed
_Success_(return) bool cchainTransfer(_Inout_ptr_opt_ cchain* dest, _Inout_ptr_opt_ cchain* src);

/// Clone a closure chain to a new location
///
/// Copies every closure from src into dest. The copies run before the closures already in dest.
/// src is unchanged.
///
/// @param dest Destination chain pointer
/// @param src Source chain pointer
/// @return true on success, false if dest has been consumed
_Success_(return) bool cchainClone(_Inout_ptr_opt_ cchain* dest, _In_ptr_opt_ cchain* src);

/// Make a consumed closure chain usable again
///
/// The chain is left empty.
///
/// @param chain Pointer to closure chain
/// @return true if the chain was reset, false if it had not been consumed
_Success_(return) bool cchainReset(_Inout_ptr_opt_ cchain* chain);

/// Remove all closures from the chain
///
/// Unlike cchainDestroy(), the chain stays usable.
///
/// @param chain Pointer to closure chain
/// @return true on success, false if the chain has been consumed
_Success_(return) bool cchainClear(_Inout_ptr_opt_ cchain* chain);

/// Destroy a closure chain and all its closures
///
/// The chain is left consumed. Destroying a chain that is already consumed does nothing.
///
/// @param chain Pointer to closure chain
void cchainDestroy(_Inout_ptr_opt_ cchain* chain);

/// @}
// end of closure_chain group

CX_C_END