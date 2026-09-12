#pragma once

/// @file env.h
/// @brief Environment variables of the running process

/// @defgroup sys_env Environment Variables
/// @ingroup sys
/// @{
///
/// Reads and changes the environment variables of the running process.
///
/// A variable set to an empty string still exists. Removing one completely is a separate
/// call, envUnset(), because "set to empty" and "not set at all" are different states and a
/// program reading the variable can tell them apart.
///
/// @section sys_env_case Name case
///
/// Windows matches variable names without regard to case, so `PATH` and `Path` are the same
/// variable there. Unix matches them exactly, so they are two different variables. cx does not
/// hide this difference, because the programs and libraries that read these variables do not
/// hide it either. envEnum() builds its table to match the platform, so looking a name up in
/// that table gives the same answer the platform itself would give.
///
/// @section sys_env_threads Thread safety
///
/// cx locks its own environment calls, so envGet(), envSet(), envUnset() and envEnum() are
/// safe to call from several threads at once.
///
/// That lock does not cover calls cx does not make. On Unix, changing the environment while
/// another thread reads it with a plain `getenv()` -- from the C library, or from any other
/// library in the process -- can crash, and cx cannot prevent that from outside. Set the
/// variables a program needs during startup, before it starts any threads, and leave the
/// environment alone after that. Windows does not have this problem.

#include <cx/container/hashtable.h>
#include <cx/cx.h>
#include <cx/string.h>

CX_C_BEGIN

/// Reads an environment variable.
///
/// @param out Receives the value, or is set to empty if the variable is not set
/// @param name Variable name
/// @return true if the variable is set, false if it is not
///
/// Example:
/// @code
///   string home = 0;
///   if (envGet(&home, _SL("HOME"))) {
///       // use home
///   }
///   strDestroy(&home);
/// @endcode
bool envGet(_Inout_ string* out, _In_ strref name);

/// Sets an environment variable, creating it or replacing whatever value it had.
///
/// @param name Variable name; must not be empty and must not contain '='
/// @param val Value to set; NULL or empty gives the variable an empty value
/// @return true on success
///
/// Example:
/// @code
///   envSet(_SL("CX_MODE"), _SL("fast"));
/// @endcode
bool envSet(_In_ strref name, _In_opt_ strref val);

/// Removes an environment variable.
///
/// Succeeds whether or not the variable was set to begin with.
///
/// @param name Variable name
/// @return true on success
///
/// Example:
/// @code
///   envUnset(_SL("CX_MODE"));
/// @endcode
bool envUnset(_In_ strref name);

/// Reads the entire environment into a hashtable of name to value.
///
/// This initializes the table itself, so pass an uninitialized hashtable. Destroy it with
/// htDestroy() when finished.
///
/// @param out Receives a new string-to-string table
/// @return true on success
///
/// Example:
/// @code
///   hashtable env;
///   envEnum(&env);
///   foreach (hashtable, it, env) {
///       string name = htiKey(string, it);
///       string val  = htiVal(string, it);
///       // use name and val
///   }
///   htDestroy(&env);
/// @endcode
bool envEnum(_Inout_ hashtable* out);

/// Checks whether an environment variable is set.
///
/// @param name Variable name
/// @return true if the variable is set
///
/// Example:
/// @code
///   if (envExists(_SL("CX_DEBUG"))) {
///       // variable is present
///   }
/// @endcode
_meta_inline bool envExists(_In_ strref name)
{
    string val = 0;
    bool ret   = envGet(&val, name);
    strDestroy(&val);
    return ret;
}

CX_C_END

/// @}
