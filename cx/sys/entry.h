#pragma once

// Platform-independent entry point

#include <cx/container/sarray.h>
#include <cx/cx.h>
#include <cx/platform/base.h>

// Command-line arguments (UTF-8)
extern string cmdProgram;
extern sa_string cmdArgs;

// DEFINE_ENTRY_POINT
// Put this macro in a translation unit somewhere to define the platform
// dependent entry point, which will parse the command-line arguments and
// call entryPoint()

CX_C_BEGIN

// User-supplied function that is called by DEFINE_ENTRY_POINT
int entryPoint(void);

// Internal: runs the program's entry function and returns the program's exit code. Only cx's own
// libraries supply one, through _CX_DEFINE_ENTRY_POINT_RUNNER in their entry point macro, when
// they must keep the main thread and call entry on another. Arguments are already parsed.
typedef int (*EntryRunner)(int (*entry)(void));

// Internal support functions, do not call these directly
void _entryParseArgs(int argc, const char** argv);
void _entryParseArgsU16(int argc, const uint16** argv);
int _entryRunDirect(int (*entry)(void));

#if defined(_PLATFORM_WIN)
#include "cx/platform/win/win_sys_entry.h"
#elif defined(_PLATFORM_UNIX)
#include "cx/platform/unix/unix_sys_entry.h"
#elif defined(_PLATFORM_WASM)
#include "cx/platform/wasm/wasm_sys_entry.h"
#endif

#define DEFINE_ENTRY_POINT _CX_DEFINE_ENTRY_POINT_RUNNER(_entryRunDirect)

CX_C_END