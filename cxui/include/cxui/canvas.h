#pragma once

/// @file canvas.h
/// @brief Canvases: widgets the program draws itself

#include <cxui/cxui_shared.h>
#include <cxui/draw.h>
#include <cxui/events.h>
#include <cxui/widget.h>

CX_C_BEGIN

/// @defgroup cxui_canvas Canvases
/// @ingroup cxui
/// @{
/// A CxUiCanvas is drawn by its paint handler (cxuiOnPaint()), with the calls in @ref cxui_draw.
///
/// - **cxui keeps what was drawn.** Each canvas has its own image of itself, so the handler is
///   only asked to draw again what cxuiCanvasInvalidate() marked, never just because the window
///   was covered and uncovered.
/// - **Invalidating is cheap and paints later.** Marking many areas in a row leads to one paint,
///   for the smallest rectangle that holds them all.
/// - **Paint all of the dirty area.** The area a paint handler is given may be larger than what
///   was marked, and what was there before isn't kept inside it on every platform, so start by
///   filling it with the background.
/// - **Resizing** the canvas, or moving it to a monitor with another scale, sends a resize event
///   and repaints all of it.
/// - **Units:** a canvas draws in physical pixels by default, so thin lines and character grids
///   fall on exact pixels. Scale the drawing with cxuiCanvasScale(), or set CXUI_Units_Dip to have
///   cxui do it.
/// - **Mouse:** once a button goes down over a canvas, the canvas gets every mouse event until
///   all buttons are up, even outside it, with positions that may be negative or past its size.
///   Enter comes before the first event over the canvas and Leave after the last; while a button
///   is held, Leave waits until it is released. A second press soon after the first, close to it,
///   comes as CXUI_MOUSE_DoubleClick instead of Down.
/// - **Keyboard:** clicking a canvas gives it keyboard focus (cxuiFocus() does too); its key
///   events bubble up to the window. Wheel events go to the canvas under the pointer and bubble
///   the same way; one nobody handles scrolls a scroll box around the canvas. The rest of a
///   gesture, until the wheel pauses, goes where its first turn went: if the first was handled,
///   later ones count as handled even when ignored, and if not, later ones go straight to the
///   scroll box. So return CXUI_Default from a canvas that can't scroll the way the wheel turned,
///   and CXUI_Handled whenever it can.
///
/// @code
///   CxUiCanvas* cv = cxuiCanvasCreate(NULL);
///   cxuiOnPaint(cv, paint, stvar(ptr, app));
///   cxuiContainerAppend(window, cv);
///   objRelease(&cv);
/// @endcode

/// Which drawing engine a canvas uses on Windows; ignored elsewhere
enum CxUiDrawBackends {
    /// Direct2D where it works well; GDI on Windows XP and under Wine
    CXUI_Draw_Auto = 0,
    /// Direct2D, falling back to GDI where it isn't available
    CXUI_Draw_D2D,
    /// GDI: fastest for text-heavy canvases
    CXUI_Draw_GDI,
};

/// Mouse cursors for cxuiCanvasSetCursor()
enum CxUiCursors {
    CXUI_CURSOR_Arrow = 0,
    CXUI_CURSOR_IBeam,        ///< For text
    CXUI_CURSOR_Hand,         ///< For links
    CXUI_CURSOR_Wait,         ///< The arrow on macOS, which has no busy cursor for programs
    CXUI_CURSOR_Cross,
    CXUI_CURSOR_SizeWE,       ///< Resizing left and right
    CXUI_CURSOR_SizeNS,       ///< Resizing up and down
    CXUI_CURSOR_SizeNWSE,     ///< The arrow on macOS before 15
    CXUI_CURSOR_SizeNESW,     ///< The arrow on macOS before 15
    CXUI_CURSOR_Move,
    CXUI_CURSOR_NotAllowed,
    CXUI_CURSOR_None,         ///< Hidden
    CXUI_CURSOR_Count
};

/// Units for a canvas's drawing, mouse positions and invalidated areas
enum CxUiCanvasUnits {
    /// Physical pixels
    CXUI_Units_Pixels = 0,
    /// Device-independent pixels (1/96 inch at 100% scale); cxui scales them, rounding edges to
    /// whole pixels
    CXUI_Units_Dip,
};

/// @}  // end of cxui_canvas group

CX_C_END

#include <cxui/core/cxuicanvas.h>

/// @addtogroup cxui_canvas
/// @{

/// CxUiCanvas* cxuiCanvasCreate(const CxUiCanvasDesc* desc)
///
/// Creates a canvas
///
/// @param ... (desc) Initial settings, or NULL for the defaults
/// @return The canvas; release it with objRelease()
///
/// Example:
/// @code
///   CxUiCanvas* cv = cxuiCanvasCreate(&(CxUiCanvasDesc){ .draw = CXUI_Draw_GDI });
/// @endcode
#define cxuiCanvasCreate(...) CxUiCanvas_createWith(__VA_ARGS__)

/// CxUiRenderView* cxuiRenderViewCreate(const CxUiRenderViewDesc* desc)
///
/// Creates a render view, a surface for a renderer
///
/// @param ... (desc) Initial settings, or NULL for the defaults
/// @return The render view; release it with objRelease()
///
/// Example:
/// @code
///   CxUiRenderView* view = cxuiRenderViewCreate(NULL);
///   cxuiOnSurface(view, surfaceChanged, stvar(ptr, app));   // start or stop the renderer
///   cxuiOnResize(view, viewResized, stvar(ptr, app));       // the size to present at
/// @endcode
#define cxuiRenderViewCreate(...) CxUiRenderView_createWith(__VA_ARGS__)

/// float32 cxuiCanvasScale(CxUiCanvas* cv)
///
/// Gets the scale of the window a canvas is in
///
/// @param cv The canvas
/// @return Physical pixels per device-independent pixel
#define cxuiCanvasScale(cv) cxuiWidgetScale(cv)

/// @}  // end of cxui_canvas group
