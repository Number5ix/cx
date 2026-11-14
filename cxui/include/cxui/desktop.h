#pragma once

/// @file desktop.h
/// @brief The clipboard and the monitors

#include <cxui/cxui_shared.h>

CX_C_BEGIN

/// @defgroup cxui_desktop Clipboard and monitors
/// @ingroup cxui
/// @{
/// UI thread only, like the rest of cxui.
///
/// **Clipboard text** is UTF-8, with lines ending in `\n`. On Windows, line ends are changed to
/// `\r\n` on the way out and back to `\n` on the way in, so other programs see the lines.
///
/// **Monitor rectangles** are in the platform's screen units, the same as window placement's
/// (CxUiPlacement): physical pixels on Windows, device-independent pixels on Linux, points on
/// macOS.
///
/// @code
///   cxuiClipboardSetText(_SL("copied\n"));
///
///   sa_CxUiMonitor mons;
///   saInit(&mons, opaque(CxUiMonitor), 4);
///   cxuiMonitors(&mons);
///   for (int32 i = 0; i < saSize(mons); i++) {
///       if (mons.a[i].flags & CXUI_MON_Primary)
///           useArea(mons.a[i].workArea);
///   }
///   saDestroy(&mons);
/// @endcode

/// Puts text on the clipboard, replacing what was there
///
/// On Linux the text stays on the clipboard after the program ends only if the desktop runs a
/// clipboard manager.
///
/// @param text The text
/// @return false if the clipboard couldn't be used
bool cxuiClipboardSetText(_In_opt_ strref text);

/// Gets the text on the clipboard
///
/// On Linux, while another program is asked for the text, this waits with the UI queue and timers
/// still running, as a message box does.
///
/// @param out Receives the text; emptied if there is none
/// @return true if the clipboard held text
bool cxuiClipboardGetText(_Inout_ string* out);

/// Lists the monitors
///
/// @param out Array of CxUiMonitor, which is cleared first; the primary monitor comes first
/// @return How many there are; 0 if the platform can't tell
int32 cxuiMonitors(_Inout_ sa_CxUiMonitor* out);

/// @}  // end of cxui_desktop group

CX_C_END
