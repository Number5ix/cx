/// @file closure.h
/// @brief Basic closure functionality
/// @defgroup closure_basic Closures
/// @ingroup closure
/// @{
///
/// Function closures that capture environment variables for deferred execution.
///
/// A closure packages a function pointer with a list of captured variables (cvars) that
/// act as the function's environment. When the closure is called, these captured variables
/// are passed to the function along with any call-time arguments.
///
/// This is useful for:
/// - Passing context to callbacks without global state
/// - Implementing event handlers with associated data
/// - Deferred execution with captured parameters
///
/// Basic usage:
/// @code
///   bool myCallback(stvlist *cvars, stvlist *args) {
///       int captured = stvlNextInt(cvars);
///       string arg = stvlNextPtr(string, args);
///       // use captured value and argument...
///       return true;
///   }
///
///   closure cls = closureCreate(myCallback, stvar(int32, 42));
///   closureCall(cls, stvar(string, myString));
///   closureDestroy(&cls);
/// @endcode

#pragma once

#include <cx/cx.h>
#include <cx/stype/stvar.h>
#include <cx/utils/macros/args.h>

// Opaque closure reference type (internal)
typedef struct closure_ref
{
    void* _is_closure;
} closure_ref;

/// Opaque handle to a closure
typedef struct closure_ref *closure;

/// Closure function signature
///
/// Closure functions receive two argument lists:
/// - cvars: Captured variables provided when the closure was created
/// - args: Arguments provided when the closure is called
///
/// Both are accessed using stvlist iteration functions (stvlNext*, stvlNextPtr, etc.)
///
/// @param cvars List of captured variables from closureCreate()
/// @param args List of arguments from closureCall()
/// @return true on success, false on failure
typedef bool (*closureFunc)(stvlist *cvars, stvlist *args);

_Ret_valid_ closure _closureCreate(_In_ closureFunc func, int n, stvar cvars[]);

/// closure closureCreate(closureFunc func, ...)
///
/// Create a new closure with captured variables.
///
/// Captures the provided variables (cvars) which will be available to the function when
/// the closure is called. The variables are copied, so the originals can be safely destroyed.
///
/// @param func Closure function to call
/// @param ... Zero or more stvar arguments to capture as the closure's environment
/// @return New closure object (must be freed with closureDestroy())
#define closureCreate(func, ...) _closureCreate(func, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

bool _closureCall(_In_ closure cls, int n, stvar args[]);

/// bool closureCall(closure cls, ...)
///
/// Call a closure with the given arguments.
///
/// Invokes the closure's function, passing both the captured variables (cvars) and the
/// provided call-time arguments.
///
/// @param cls Closure to call
/// @param ... Zero or more stvar arguments to pass to the closure function
/// @return Return value from the closure function
#define closureCall(cls, ...) _closureCall(cls, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

/// Create a copy of a closure
///
/// Creates a new closure with the same function and captured variables. The captured
/// variables are deep-copied, so modifications to the original won't affect the clone.
///
/// @param cls Closure to clone
/// @return New independent closure (must be freed with closureDestroy())
_Ret_valid_ closure closureClone(_In_ closure cls);

/// Destroy a closure and release its resources
///
/// Frees the closure and all captured variables. Sets the closure pointer to NULL.
///
/// @param cls Pointer to closure to destroy
void closureDestroy(_Inout_ptr_uninit_ closure *cls);

/// @}
// end of closure_basic group
