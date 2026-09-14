#pragma once

/// @file dynlib.h
/// @brief Loading shared libraries at runtime

/// @defgroup sys_dynlib Dynamic Libraries
/// @ingroup sys
/// @{

#include <cx/cx.h>
#include <cx/sys/dynlibobj.h>

CX_C_BEGIN

/// @defgroup sys_dynlib_overview Overview
/// @ingroup sys_dynlib
/// @{
///
/// Loads shared libraries (`.dll` on Windows, `.so` on Unix) while the program is running, and
/// finds the functions and variables they export.
///
/// @code
///   string fname = 0;
///   dynlibFilename(&fname, _SL("myplugin"), 0);   // "myplugin.dll" or "libmyplugin.so"
///
///   DynLib* lib = dynlibOpen(fname, DYNLIB_ExeDirFirst);
///   if (lib) {
///       PluginEntryFn entry = dynlibFunc(lib, PluginEntryFn, _SL("pluginEntry"));
///       if (entry)
///           entry();
///       dynlibRelease(&lib);
///   }
///   strDestroy(&fname);
/// @endcode
///
/// @section sys_dynlib_cx Libraries that link cx
///
/// **Do not pass cx data between the program and a library that has its own copy of cx.**
///
/// cx is always statically linked, so a shared library built with cx contains a complete,
/// separate copy of it: its own memory allocator, its own type and class registries, its own
/// logging system, and its own `cxerr`. The two copies know nothing about each other.
///
/// Strings, objects, containers, closures, and anything else allocated by one copy must never be
/// freed, released, or resized by the other. Doing so corrupts memory. Design the interface
/// between the program and the library around plain C types — integers, plain structs, C
/// strings, and function pointers — and have each side free only what it allocated.
///
/// A library that runs its own copy of cx may also have started background threads, so it
/// should normally stay loaded until the program exits.
///
/// @section sys_dynlib_open Finding the library
///
/// A path containing a directory, such as `plugins/foo.so` or `c:/app/foo.dll`, loads exactly
/// that file. A relative path is relative to the current directory.
///
/// A bare filename like `foo.dll` is found using the operating system's search rules:
/// - **Windows** searches the executable's directory, then `System32`. The current directory is
///   never searched.
/// - **Unix** searches `LD_LIBRARY_PATH`, the executable's run path, and the system library
///   directories. The executable's own directory is not searched.
///
/// Pass DYNLIB_ExeDirFirst to look next to the executable first on every platform.
///
/// @section sys_dynlib_handles Handles
///
/// Each successful dynlibOpen() returns a new DynLib holding its own reference to the library.
/// Opening the same library twice loads it only once: both handles find the same functions and
/// share the same global variables. The library is unloaded when the last handle is released.
///
/// **Releasing the last handle while anything still points into the library is a crash waiting
/// to happen.** That includes function pointers from dynlibFunc(), pointers to its data, and
/// callbacks it registered with other code.
///
/// @section sys_dynlib_errors Errors
///
/// Failed calls set `cxerr`:
/// - `CX_FileNotFound` — the file does not exist. When loading by bare filename, this is also
///   reported if the library was found but something it needs was not, since the operating
///   system does not tell the two apart.
/// - `CX_InvalidImage` — the file exists but could not be loaded: it is not a library, is built
///   for a different CPU, or needs another library that is missing.
/// - `CX_AccessDenied` — the file could not be read.
/// - `CX_SymbolNotFound` — the library does not export the requested name.
/// - `CX_NotSupported` — the platform cannot load libraries (WebAssembly).
///
/// The operating system's own description of the most recent failure is available from
/// dynlibLastError(). Failures are also logged to the `cx/sys/dynlib` channel at `Diag` level.
///
/// @section sys_dynlib_threads Threads
///
/// All functions may be called from any thread. The operating system runs a library's startup
/// code (`DllMain`, constructors) while holding a process-wide loader lock. Loading a library
/// from inside that startup code, or having it wait on another thread that is loading a
/// library, can deadlock.
///
/// @section sys_dynlib_export Exporting from a library
///
/// Mark each function or variable a library exports with CX_EXPORT. Code that links against the
/// library at build time, rather than through dynlibOpen(), declares them with CX_IMPORT.
///
/// @code
///   CX_EXPORT int pluginEntry(void)
///   {
///       return 1;
///   }
/// @endcode
///
/// @}  // end of sys_dynlib_overview group

/// Flags for dynlibOpen() and dynlibFilename()
enum DYNLIB_FLAGS {
    /// Make the library's symbols available to libraries loaded later (Unix only)
    DYNLIB_Global = 0x0001,

    /// Resolve the library's function references on first call instead of at load time (Unix
    /// only). By default a library that references a missing function fails to load.
    DYNLIB_Lazy = 0x0002,

    /// Never use the operating system search rules; a bare filename is relative to the current
    /// directory
    DYNLIB_NoSearch = 0x0004,

    /// Look for a bare filename in the executable's directory before the operating system search
    DYNLIB_ExeDirFirst = 0x0008,

    /// dynlibFilename() only: do not add the `lib` prefix on Unix
    DYNLIB_NoPrefix = 0x0010,
};

/// Pointer to an exported function, before being cast to its real type
typedef void (*DynLibFunc)(void);

/// Loads a shared library.
///
/// @param path Path or bare filename of the library
/// @param flags Any of DYNLIB_Global, DYNLIB_Lazy, DYNLIB_NoSearch, DYNLIB_ExeDirFirst
/// @return A new handle, or NULL on failure with cxerr set
///
/// Example:
/// @code
///   DynLib* lib = dynlibOpen(_SL("plugins/libfoo.so"), 0);
///   if (!lib) {
///       string msg = 0;
///       dynlibLastError(&msg);
///       logFmt(Warn, _SL("Plugin failed to load: ${string}"), stvar(string, msg));
///       strDestroy(&msg);
///   }
/// @endcode
_Ret_opt_valid_ DynLib* dynlibOpen(_In_opt_ strref path, flags_t flags);

/// Gets a handle to the running executable.
///
/// Use this to look up symbols the executable exports. On Unix, an executable only exports
/// symbols if it was linked with `-rdynamic`.
///
/// @return A new handle, or NULL on failure with cxerr set
///
/// Example:
/// @code
///   DynLib* self = dynlibSelf();
///   dynlibRelease(&self);
/// @endcode
_Ret_opt_valid_ DynLib* dynlibSelf(void);

/// void dynlibRelease(DynLib **plib);
///
/// Releases a library handle.
///
/// The library is unloaded when its last handle is released. The pointer is set to NULL.
///
/// @param plib Pointer to the handle to release; may point to NULL
///
/// Example:
/// @code
///   dynlibRelease(&lib);
/// @endcode
#define dynlibRelease(plib) objRelease(plib)

/// Looks up an exported variable or other data symbol.
///
/// For functions, use dynlibFunc() instead.
///
/// @param lib Library to search
/// @param name Exported name
/// @return Address of the symbol, or NULL with cxerr set to CX_SymbolNotFound
///
/// Example:
/// @code
///   int* counter = dynlibSymbol(lib, _SL("pluginCounter"));
/// @endcode
_Ret_opt_valid_ void* dynlibSymbol(_In_ DynLib* lib, _In_opt_ strref name);

/// Looks up an exported function.
///
/// Usually called through dynlibFunc(), which casts the result to the right type.
///
/// @param lib Library to search
/// @param name Exported name
/// @return The function, or NULL with cxerr set to CX_SymbolNotFound
_Ret_opt_valid_ DynLibFunc dynlibSymbolFn(_In_ DynLib* lib, _In_opt_ strref name);

/// fntype dynlibFunc(DynLib *lib, fntype, strref name);
///
/// Looks up an exported function and casts it to a function pointer type.
///
/// @param lib Library to search
/// @param fntype Function pointer type to cast to
/// @param name Exported name
/// @return The function, or NULL with cxerr set to CX_SymbolNotFound
///
/// Example:
/// @code
///   typedef int (*AddFn)(int a, int b);
///   AddFn add = dynlibFunc(lib, AddFn, _SL("pluginAdd"));
///   if (add)
///       add(1, 2);
/// @endcode
#define dynlibFunc(lib, fntype, name) ((fntype)dynlibSymbolFn(lib, name))

/// Gets the full path of a loaded library.
///
/// @param out String to receive the path
/// @param lib Library to query
/// @return true on success
///
/// Example:
/// @code
///   string path = 0;
///   dynlibPath(&path, lib);
/// @endcode
bool dynlibPath(_Inout_ string* out, _In_ DynLib* lib);

/// Builds the platform's filename for a library.
///
/// Adds `.dll` on Windows. On Unix adds `.so`, and a `lib` prefix unless DYNLIB_NoPrefix is set.
/// Any directory in `basename` is kept.
///
/// @param out String to receive the filename
/// @param basename Library name without prefix or extension
/// @param flags DYNLIB_NoPrefix, or 0
///
/// Example:
/// @code
///   string fname = 0;
///   dynlibFilename(&fname, _SL("plugins/render"), 0);   // "plugins/librender.so" on Linux
/// @endcode
void dynlibFilename(_Inout_ string* out, _In_opt_ strref basename, flags_t flags);

/// Gets the operating system's description of the most recent failure.
///
/// Covers failures of dynlibOpen(), dynlibSelf(), dynlibSymbol(), and dynlibFunc() on the
/// calling thread. Successful calls do not clear it.
///
/// @param out String to receive the message; empty if nothing has failed on this thread
///
/// Example:
/// @code
///   string msg = 0;
///   dynlibLastError(&msg);
/// @endcode
void dynlibLastError(_Inout_ string* out);

CX_C_END

/// @}  // end of sys_dynlib group
