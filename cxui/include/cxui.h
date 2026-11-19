#pragma once

#include <cx/closure.h>
#include <cx/taskqueue.h>

#include <cxui/canvas.h>
#include <cxui/cxui_shared.h>
#include <cxui/desktop.h>
#include <cxui/dialog.h>
#include <cxui/draw.h>
#include <cxui/drawn.h>
#include <cxui/font.h>
#include <cxui/grid.h>
#include <cxui/image.h>
#include <cxui/events.h>
#include <cxui/keys.h>
#include <cxui/layout.h>
#include <cxui/menu.h>
#include <cxui/runtime.h>
#include <cxui/text.h>
#include <cxui/controls.h>
#include <cxui/theme.h>
#include <cxui/timer.h>
#include <cxui/widget.h>
#include <cxui/window.h>

/// @file cxui.h
/// @brief cxui: native user interfaces for cx programs

/// @defgroup cxui User Interface
/// @{
/// Windows, native controls and drawn widgets on Win32, GTK4 and AppKit, with one C API.
///
/// @defgroup cxui_overview Overview
/// @ingroup cxui
/// @{
///
/// @section cxui_thread The UI thread
///
/// cxui runs all user interface work on **its own UI thread**. cxuiStart() creates it, and
/// cxuiShutdown() stops it. A program never picks the thread and never runs an event loop itself.
///
/// Other threads talk to the UI by posting work to the UI queue: cxuiPost() queues a closure,
/// cxuiCallSync() runs one and waits for it.
///
/// - **Callbacks always run on the UI thread**: posted work, event handlers and timers.
/// - **Almost every cxui call must be made on the UI thread.** The exceptions are listed in
///   @ref cxui_runtime.
/// - **The UI queue keeps running inside modal loops**, such as a message box or a native file
///   dialog, so posted work and timers are never held up by one.
/// - **Start and shutdown can repeat** in one process, and calls are counted: a library that
///   calls cxuiStart() while the UI is already running shares it, and the UI thread stops when
///   every cxuiStart() has been matched by a cxuiShutdown().
/// - **A program that uses cxui defines its entry point with CXUI_DEFINE_ENTRY_POINT**
///   (cxui/entry.h). macOS needs the user interface on the process's main thread, so there the
///   program's entryPoint() runs on another thread.
/// - **An idle UI costs nothing**: while nothing happens, nothing wakes the UI thread.
///
/// @section cxui_first A first window
///
/// @code
///   #include <cxui.h>
///   #include <cxui/entry.h>
///
///   CXUI_DEFINE_ENTRY_POINT;
///
///   typedef struct App {
///       Event quit;
///       CxUiWindow* win;
///       CxUiTextField* name;
///   } App;
///
///   static CxUiResult onGreet(stvlist* cvars, CxUiActivateEvent* ev)
///   {
///       App* app    = stvlNextPtr(cvars);
///       string text = 0;
///       strNConcat(&text, _S"Hello, ", cxuiTextFieldText(app->name));
///       cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent = app->win, .text = text });
///       strDestroy(&text);
///       return CXUI_Handled;
///   }
///
///   static CxUiResult onClose(stvlist* cvars, CxUiCloseEvent* ev)
///   {
///       eventSignal(&((App*)stvlNextPtr(cvars))->quit);
///       return CXUI_Default;   // the window goes
///   }
///
///   static bool build(stvlist* cvars, stvlist* args)
///   {
///       App* app     = stvlNextPtr(cvars);
///       app->win     = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Hello" });
///       CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 6, .margin = { 8, 8, 8, 8 } });
///       CxUiLabel* l = cxuiLabelCreate(&(CxUiLabelDesc){ .text = _S"&Name:" });
///       app->name    = cxuiTextFieldCreate(NULL);
///       CxUiButton* greet = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&Greet" });
///       cxuiWidgetSetExpand(app->name, CXUI_EXPAND_H);
///       cxuiOnActivate(greet, onGreet, stvar(ptr, app));
///       cxuiOnClose(app->win, onClose, stvar(ptr, app));
///       cxuiContainerAppend(row, l);
///       cxuiContainerAppend(row, app->name);
///       cxuiContainerAppend(row, greet);
///       cxuiContainerAppend(app->win, row);
///       cxuiWindowSetDefaultEnter(app->win, greet);
///       cxuiWindowShow(app->win, CXUI_SHOW_Normal);
///       objRelease(&l);
///       objRelease(&greet);
///       objRelease(&row);
///       return true;
///   }
///
///   int entryPoint(void)
///   {
///       App app = { 0 };
///       if (!cxuiStart(NULL))
///           return 1;
///       eventInit(&app.quit);
///       cxuiCallSync(closureCreate(build, stvar(ptr, &app)));
///       eventWait(&app.quit);       // this thread is free to do other work meanwhile
///       cxuiShutdown();
///       objRelease(&app.name);
///       objRelease(&app.win);
///       eventDestroy(&app.quit);
///       return 0;
///   }
/// @endcode
///
/// @section cxui_parts What is where
///
/// - Starting, stopping, and getting work onto the UI thread: @ref cxui_runtime.
/// - What every widget has, containers, focus, and enabled and visible states: @ref cxui_widget.
/// - Boxes, grid boxes, stacks, tabs, splits, panels, frames and scroll boxes, which place
///   everything inside them: @ref cxui_layout.
/// - How handlers are set and how events travel: @ref cxui_events.
/// - Windows, popups, placement and owned windows: @ref cxui_window.
/// - The platform's own labels, buttons, check boxes, lists, sliders, progress bars and text
///   fields: @ref cxui_controls and @ref cxui_text.
/// - Widgets cxui draws itself, which look the same everywhere: @ref cxui_drawn.
/// - Canvases the program draws on, and render views a renderer presents to: @ref cxui_canvas,
///   @ref cxui_draw, @ref cxui_font and @ref cxui_image.
/// - Lists, tables and trees of any size: @ref cxui_grid.
/// - Menu bars, message boxes, and file and color dialogs: @ref cxui_menu and @ref cxui_dialog.
/// - Light and dark windows, and the palettes drawn widgets take their colors from:
///   @ref cxui_theme.
/// - Timers, key names and shortcuts, the clipboard and the monitors: @ref cxui_timer,
///   @ref cxui_keys and @ref cxui_desktop.
/// - The same from Lua: @ref cxuilua.
///
/// @section cxui_conventions What holds throughout
///
/// - **Widgets are objects.** A factory returns a reference the caller owns; a container holds its
///   own reference to each child, so a widget is usually created, added and released.
///   cxuiDestroy() takes a widget off the screen for good, and leaves a **dead** object that
///   ignores every call, so stale references are harmless.
/// - **Settings come in a descriptor**, usually a compound literal, where every field left at 0
///   takes its default; NULL takes them all. Most can be changed later through the widget's
///   functions, before or after it is on screen.
/// - **Setting a value from code raises no event.** Change events report what the user did.
/// - **One handler per event** on each widget (cxuiOn*), with captured values. Key and wheel
///   events bubble up to the window until a handler returns CXUI_Handled.
/// - **Sizes are device-independent pixels** (DIPs), which the platform's scale turns into
///   physical pixels; canvases draw in physical pixels unless told otherwise.
/// - **Layout runs after a batch of changes**, once, before the window is painted.
/// - **Text is UTF-8**, in cx strings. An `&` in a title marks its mnemonic.
///
/// @section cxui_platforms Platforms
///
/// - **Windows** from XP: Win32 controls with the system's visual styles, a dark look drawn by
///   cxui where Windows has none, and canvases drawn with Direct2D (GDI on XP and under Wine).
/// - **Linux**: GTK 4, on Wayland or X11. Programs can't place windows there, so positions and
///   moving a window beside another do nothing; sizes and the maximized state still work.
///   cxui leaves the C locale as the program set it, so GTK's own text, such as the file
///   dialog's buttons, is translated only if the program calls `setlocale(LC_ALL, "")`.
/// - **macOS** from 14.4, on Apple silicon and Intel: AppKit. The menu bar at the top of the
///   screen shows the key window's menus. There are no mnemonics, so the `&` is left out of
///   titles, and Command is the shortcut key where the others use Ctrl
///   (CXUI_MOD_Primary stands for either).
///
/// @}  // end of cxui_overview group

/// @}  // end of cxui group
