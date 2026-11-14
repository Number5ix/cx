#pragma once

/// @file entry.h
/// @brief The entry point of a program that uses cxui

#include <cx/sys/entry.h>

CX_C_BEGIN

/// @addtogroup cxui_runtime
/// @{

// Internal: use CXUI_DEFINE_ENTRY_POINT
int _cxuiEntryRunner(int (*entry)(void));

/// CXUI_DEFINE_ENTRY_POINT
///
/// Defines the program's entry point, as DEFINE_ENTRY_POINT does, for a program that uses cxui
///
/// entryPoint() is called with the command line parsed, and its return value is the exit code. On
/// macOS it runs on a second thread, so code in it can't use other APIs that need the main thread
/// except through cxuiCallSync(). On macOS cxuiStart() fails in a program that doesn't use this.
/// The file that uses it includes this header, which cxui.h leaves out.
///
/// Example:
/// @code
///   #include <cxui/entry.h>
///
///   CXUI_DEFINE_ENTRY_POINT;
///
///   int entryPoint(void)
///   {
///       ...
///   }
/// @endcode
#define CXUI_DEFINE_ENTRY_POINT _CX_DEFINE_ENTRY_POINT_RUNNER(_cxuiEntryRunner)

/// @}  // end of cxui_runtime group

CX_C_END
