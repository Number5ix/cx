#pragma once

/// @file window.h
/// @brief Top-level windows

#include <cxui/cxui_shared.h>
#include <cxui/events.h>
#include <cxui/widget.h>

CX_C_BEGIN

/// @defgroup cxui_window Windows
/// @ingroup cxui
/// @{
/// A CxUiWindow is a top-level window holding one widget, its content. Windows are created hidden;
/// the native window is created when one is first shown or realized.
///
/// - **A shown window keeps itself alive.** From the time it is first shown or realized until it
///   is destroyed or cxui shuts down, cxui holds a reference to it, so the program can release its
///   own as soon as the window is set up.
/// - **Closing is an event.** The close box, Alt+F4 and cxuiWindowClose() all call the window's
///   close handler (cxuiOnClose()). A handler that returns CXUI_Handled keeps the window open;
///   otherwise the window's `closeAction` runs. Hiding or destroying the last window doesn't stop
///   cxui.
/// - **Popups and message boxes are modal.** cxuiWindowPopup() and cxuiMessageBox() wait for an
///   answer while the UI queue and timers keep running, and the window they belong to can't be
///   used or closed meanwhile. They can nest; each returns once everything opened after it has
///   ended.
/// - **Settings work before or after realization.** Title, size, position and theme can be set at
///   any time; settings made before the window is shown are applied before it first appears.
/// - **Positions** are in the platform's screen units: physical pixels on Windows, points on
///   macOS. Linux doesn't let programs place windows, so cxuiWindowSetPosition() fails there and
///   the system picks where windows appear.
/// - **Placement** (cxuiWindowPlacement(), cxuiWindowSetPlacement()) saves and restores a window's
///   rectangle and whether it is maximized, for programs that reopen windows where the user left
///   them. cxuiPlacementCaps() says which parts the platform supports.
/// - **Owned windows** (cxuiWindowSetOwner()) stay in front of the window they belong to, for tool
///   windows beside a main window; cxuiWindowMoveNear() puts one beside its owner. A popup, message
///   box or dialog over a window makes its owner and the other windows that owner owns wait too.
///
/// @code
///   static CxUiResult onClose(stvlist* cvars, CxUiCloseEvent* ev)
///   {
///       App* app = stvlNextPtr(cvars);
///       if (appHasUnsavedWork(app))
///           return CXUI_Handled;   // stay open
///       eventSignal(&app->quit);
///       return CXUI_Default;
///   }
///
///   CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Settings", .size = { 480, 320 } });
///   cxuiOnClose(w, onClose, stvar(ptr, app));
///   cxuiWindowShow(w, CXUI_SHOW_Normal);
///   objRelease(&w);
/// @endcode

/// How cxuiWindowShow() shows a window
enum CxUiShowModes {
    /// Restored, and made the active window
    CXUI_SHOW_Normal,
    /// Restored, without taking focus from the active window. Only a hint on Linux, where the
    /// system decides.
    CXUI_SHOW_NoActivate,
    /// Minimized, without taking focus
    CXUI_SHOW_Minimized,
    /// Maximized, and made the active window
    CXUI_SHOW_Maximized,
};

/// What a window does when asked to close, if its close handler doesn't stop it
enum CxUiCloseActions {
    /// Hide it; it can be shown again
    CXUI_OnClose_Hide = 0,
    /// Destroy it, as cxuiDestroy() does; one still open when cxui shuts down is destroyed then
    CXUI_OnClose_Destroy,
    /// Nothing; the close handler does whatever is needed
    CXUI_OnClose_None,
};

/// Flags for cxuiWindowFit()
enum CxUiFitFlags {
    /// Make the window smaller too, where its content needs less room than it has
    CXUI_FIT_Shrink = 0x0001,
};

/// Where cxuiWindowMoveNear() puts a window
enum CxUiNearPlaces {
    /// Centered on the other window
    CXUI_NEAR_Center,
    /// Beside the other window's right edge, centered on it top to bottom; beside its left edge
    /// where the screen has no room on the right
    CXUI_NEAR_Right,
    /// Beside the other window's left edge; beside its right edge where the screen has no room on
    /// the left
    CXUI_NEAR_Left,
};

/// Which parts of a window's placement this platform can read and apply
/// @return CxUiPlacementCaps; 0 if cxui isn't running
flags_t cxuiPlacementCaps(void);

/// @}  // end of cxui_window group

CX_C_END

#include <cxui/core/cxuiwindow.h>

/// @addtogroup cxui_window
/// @{

/// CxUiWindow* cxuiWindowCreate(const CxUiWindowDesc* desc)
///
/// Creates a window, hidden
///
/// @param ... (desc) Initial settings, or NULL for the defaults
/// @return The window; release it with objRelease()
///
/// Example:
/// @code
///   CxUiWindow* w = cxuiWindowCreate(&(CxUiWindowDesc){ .title = _S"Log", .theme = CXUI_Theme_Dark });
/// @endcode
#define cxuiWindowCreate(...) CxUiWindow_createWith(__VA_ARGS__)

/// @}  // end of cxui_window group
