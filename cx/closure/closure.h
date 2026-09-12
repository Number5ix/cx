/// @file closure.h
/// @brief Basic closure functionality
/// @defgroup closure_basic Closures
/// @ingroup closure
/// @{
///
/// Function closures that capture environment variables for deferred execution.
///
/// A closure packages a function pointer with a list of captured variables (cvars) that
/// act as the function's environment. The captured variables are copied into the closure
/// when it is created and destroyed along with it, so anything a callback needs -- strings,
/// object references, plain values -- lives exactly as long as the callback does, with
/// nothing to free by hand.
///
/// Use a closure when a callback needs state that must stay alive for as long as the callback
/// is registered and be cleaned up with it. A plain function pointer and context pointer is
/// still the better fit for a callback that only runs during the call that receives it (a sort
/// comparator, a parse visitor), or for a table of several callbacks sharing one context.
///
/// @section closure_generic Generic closures
///
/// A generic closure has the signature `bool fn(stvlist *cvars, stvlist *args)`: captured
/// variables and call-time arguments both arrive as variant lists. This suits callbacks where
/// the caller and callee share no function type, and callbacks that run rarely enough that
/// packing the arguments into variants does not matter.
///
/// Captures are easiest to read back by name. Tag them with `stvark()` and fetch them with
/// `stvlFindVal()`, so their order does not matter:
/// @code
///   static bool onDone(stvlist *cvars, stvlist *args) {
///       strref path  = stvlFindVal(cvars, path, strref);
///       int32 tries  = stvlFindVal(cvars, tries, int32);
///       // ...
///       return true;
///   }
///
///   closure cls = closureCreate(onDone, stvark(path, string, filename), stvark(tries, int32, 3));
///   closureCall(cls, stvNone);
///   closureDestroy(&cls);
/// @endcode
///
/// A closure that captures nothing, or a call that passes nothing, must say so with `stvNone`
/// rather than leaving the argument list empty.
///
/// @section closure_typed Typed closures
///
/// A typed closure is called through an ordinary C function type instead, so it can return any
/// type and take raw pointers and sizes as arguments, with no variant packing on the call. Use
/// one for a callback that runs often, or that needs to return something other than `bool`.
///
/// Its function type takes the captured variables first, followed by at least one argument of
/// its own:
/// @code
///   typedef size_t (*MyReadFunc)(stvlist *cvars, uint8 *buf, size_t sz);
///
///   static size_t readFile(stvlist *cvars, uint8 *buf, size_t sz) {
///       File *file = stvlAtObj(cvars, 0, File);
///       size_t didread = 0;
///       fileRead(file, buf, sz, &didread);
///       return didread;
///   }
///
///   closure cls = closureCreateAs(MyReadFunc, readFile, stvar(object, file));
///   size_t n    = closureCallAs(MyReadFunc, cls, buf, sizeof(buf));
///   closureDestroy(&cls);
/// @endcode
///
/// The compiler checks that the function matches the type named in closureCreateAs(). The type
/// named in closureCallAs() must be the same one; debug builds check this on every call. Call a
/// typed closure only with closureCallAs(), and a generic one only with closureCall().
///
/// Captures can be read back by position with `stvlAt()`, `stvlAtPtr()` and `stvlAtObj()`,
/// which is cheap enough for a callback that runs constantly, or by name with `stvlFindVal()`,
/// `stvlFindPtr()` and `stvlFindObj()`.
///
/// @section closure_destroy State that is not a capture
///
/// Captures are copied into the closure and read back as values, so they cannot hold state the
/// callback changes as it runs, and copying a large value just to capture it can be wasteful. For
/// that, allocate the state yourself, capture a pointer to it, and give the closure a destroy
/// function to free it. The destroy function runs once when the closure is destroyed, with the
/// captures still readable, much like a class destructor:
/// @code
///   static void readerDestroy(stvlist *cvars) {
///       ReaderState *st = stvlAtPtr(cvars, 0);
///       bufDestroy(&st->buf);
///       xaFree(st);
///   }
///
///   ReaderState *st = xaAllocStruct(ReaderState, XA_Zero);
///   closure cls     = closureCreateAs(MyReadFunc, readChunk, stvar(ptr, st));
///   closureSetDestroy(cls, readerDestroy);
/// @endcode
///
/// A closure with a destroy function cannot be copied with closureClone().

#pragma once

#include <cx/cx.h>
#include <cx/stype/stvar.h>
#include <cx/utils/macros/args.h>

CX_C_BEGIN

// Opaque closure reference type (internal)
typedef struct closure_ref {
    void* _is_closure;
} closure_ref;

/// Opaque handle to a closure
typedef struct closure_ref* closure;

/// Generic closure function signature
///
/// Generic closure functions receive two argument lists:
/// - cvars: Captured variables provided when the closure was created
/// - args: Arguments provided when the closure is called
///
/// @param cvars List of captured variables from closureCreate()
/// @param args List of arguments from closureCall()
/// @return true on success, false on failure
typedef bool (*closureFunc)(stvlist* cvars, stvlist* args);

_Ret_valid_ closure _closureCreate(_In_ closureFunc func, int n, stvar cvars[]);

/// closure closureCreate(closureFunc func, ...)
///
/// Create a new generic closure with captured variables.
///
/// The variables are copied, so the originals can be safely destroyed.
///
/// @param func Closure function to call
/// @param ... One or more stvar arguments to capture (`stvNone` for none)
/// @return New closure object (must be freed with closureDestroy())
#define closureCreate(func, ...) \
    _closureCreate(func, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

bool _closureCall(_In_ closure cls, int n, stvar args[]);

/// bool closureCall(closure cls, ...)
///
/// Call a generic closure with the given arguments.
///
/// @param cls Closure to call
/// @param ... One or more stvar arguments to pass (`stvNone` for none)
/// @return Return value from the closure function
#define closureCall(cls, ...) \
    _closureCall(cls, count_macro_args(__VA_ARGS__), (stvar[]) { __VA_ARGS__ })

_Ret_valid_ closure _closureCreateAs(_In_ void (*func)(void), _In_z_ const char* sig, int n,
                                     stvar cvars[]);

/// closure closureCreateAs(sigtype, func, ...)
///
/// Create a new typed closure with captured variables.
///
/// @param sigtype Function pointer type the closure is called through; its first parameter
///                must be `stvlist *cvars`
/// @param func Function to call; must match `sigtype` exactly
/// @param ... One or more stvar arguments to capture (`stvNone` for none)
/// @return New closure object (must be freed with closureDestroy())
///
/// Example:
/// @code
///   typedef void (*ProgressFunc)(stvlist *cvars, int64 done, int64 total);
///   closure cls = closureCreateAs(ProgressFunc, onProgress, stvar(object, window));
/// @endcode
#define closureCreateAs(sigtype, func, ...)                                                   \
    _closureCreateAs((void (*)(void))(1 ? (func) : (sigtype)0),                                \
                     #sigtype,                                                                 \
                     count_macro_args(__VA_ARGS__),                                            \
                     (stvar[]) { __VA_ARGS__ })

// Internal: back closureCallAs(). They are usable directly by code that needs to call a typed
// closure without the macro, but closureCallAs() is the supported way.
void (*_closureFuncAs(_In_ closure cls, _In_z_ const char* sig))(void);
_Ret_valid_ stvlist* _closureCvars(_In_ closure cls, _Out_ stvlist* storage);

/// rettype closureCallAs(sigtype, closure cls, ...)
///
/// Call a typed closure.
///
/// `cls` is evaluated more than once, so pass a plain variable or field rather than an
/// expression with side effects.
///
/// @param sigtype The same function pointer type the closure was created with
/// @param cls Closure to call
/// @param ... Arguments after `cvars`, as declared by `sigtype` (at least one)
/// @return Whatever the closure function returns
///
/// Example:
/// @code
///   closureCallAs(ProgressFunc, cls, done, total);
/// @endcode
#define closureCallAs(sigtype, cls, ...) \
    ((sigtype)_closureFuncAs((cls), #sigtype))(_closureCvars((cls), &(stvlist) { 0 }), __VA_ARGS__)

/// Destroy function signature
///
/// @param cvars Captured variables of the closure being destroyed
typedef void (*closureDestroyFunc)(stvlist* cvars);

/// Sets a function to run when the closure is destroyed
///
/// Use it to free state the closure owns that is not one of its captures. It runs once, from
/// closureDestroy(), before the captured variables are destroyed.
///
/// @param cls Closure to attach the function to
/// @param destroy Function to run, or NULL for none
void closureSetDestroy(_Inout_ closure cls, _In_opt_ closureDestroyFunc destroy);

/// Create a copy of a closure
///
/// Creates a new closure with the same function and captured variables. The captured
/// variables are deep-copied, so modifications to the original won't affect the clone.
///
/// @param cls Closure to clone (NULL returns NULL)
/// @return New independent closure (must be freed with closureDestroy()), or NULL for a closure
///         with a destroy function, which cannot be copied
_Ret_maybenull_ closure closureClone(_In_opt_ closure cls);

/// Destroy a closure and release its resources
///
/// Frees the closure and all captured variables. Sets the closure pointer to NULL. Does nothing
/// if the closure was never created.
///
/// @param cls Pointer to closure to destroy (may be NULL or point to NULL)
void closureDestroy(_Inout_opt_ closure* _Nullable cls);

/// @}
// end of closure_basic group

CX_C_END
