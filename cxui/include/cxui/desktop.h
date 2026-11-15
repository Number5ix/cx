#pragma once

/// @file desktop.h
/// @brief The clipboard and the monitors

#include <cx/closure.h>
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
/// macOS. A monitor's **id** finds it again on a later run, when monitors may be listed in
/// another order; save it with the program's settings rather than the monitor's position in
/// the list.
///
/// @code
///   cxuiClipboardSetText(_SL("copied\n"));
///
///   sa_CxUiMonitor mons = { 0 };
///   cxuiMonitors(&mons);
///   for (int32 i = 0; i < saSize(mons); i++) {
///       if (strEq(mons.a[i].id, savedId))
///           cxuiWindowSetFullscreen(win, CXUI_FULLSCREEN_Monitor, &mons.a[i]);
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
/// @param out Array of CxUiMonitor, which is cleared first, and set up if it is `{ 0 }`; the
/// primary monitor comes first
/// @return How many there are; 0 if the platform can't tell
int32 cxuiMonitors(_Inout_ sa_CxUiMonitor* out);

/// Copies a monitor, to keep it apart from the list it came from
///
/// @param out Receives the copy; whatever it held is destroyed first
/// @param m The monitor
void cxuiMonitorCopy(_Inout_ CxUiMonitor* out, _In_ const CxUiMonitor* m);

/// Frees the strings in a monitor kept apart from a list, and clears it
///
/// @param m The monitor
void cxuiMonitorDestroy(_Inout_ CxUiMonitor* m);

/// Called when monitors are connected, disconnected or rearranged, or their settings change
///
/// Raised on the UI thread, with no arguments; call cxuiMonitors() again for the new list. Attach
/// with cchainAttach().
extern cchain cxuiMonitorsChangedNotify;

/// @}  // end of cxui_desktop group

CX_C_END
