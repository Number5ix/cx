#pragma once

/// @file widget.h
/// @brief The widget base class and its lifecycle

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuiwidget.h>

CX_C_BEGIN

/// @defgroup cxui_widget Widgets
/// @ingroup cxui
/// @{
/// Widgets are objects: a factory returns one with a reference the caller owns, and objRelease()
/// drops it. A container holds its own reference to each child, so the usual pattern is to create a
/// widget, add it to its container, and release it.
///
/// **Destroying a widget is separate from releasing it.** cxuiDestroy() takes a widget and
/// everything inside it off the screen for good, removes it from its container, clears its event
/// handlers and calls its `destroyNotify` chain. The object itself lives on until its last
/// reference is gone, as a **dead** widget: every cxui call on it does nothing and returns the
/// failure value, so code that still holds one, such as a task queued before a window closed, can
/// use it without checking first.
///
/// @code
///   CxUiButton* ok = cxuiButtonCreate(&(CxUiButtonDesc){ .title = _S"&OK" });
///   cxuiContainerAppend(box, ok);   // the box takes its own reference
///   objRelease(&ok);
/// @endcode

/// Flags in CxUiWidget::state
enum CxUiWidgetStates {
    /// Destroyed with cxuiDestroy(); every call on the widget does nothing
    CXUI_WS_Dead = 0x0001,
};

// Internal: use cxuiDestroy()
void _cxuiDestroy(_In_opt_ CxUiWidget* w);

/// void cxuiDestroy(CxUiWidget* w)
///
/// Destroys a widget and everything inside it
///
/// Children are destroyed first, then the widget is removed from its container. Each destroyed
/// widget's `destroyNotify` chain is called once. A handler may destroy the widget its event is for.
/// Destroying a dead widget does nothing.
///
/// @param w Any widget
#define cxuiDestroy(w) _cxuiDestroy(CxUiWidget(w))

// Internal: use the cxuiContainer* macros
bool _cxuiContainerInsert(_In_opt_ CxUiWidget* c, _In_opt_ CxUiWidget* child, int32 at);
bool _cxuiContainerDetach(_In_opt_ CxUiWidget* c, _In_opt_ CxUiWidget* child);
int32 _cxuiContainerChildCount(_In_opt_ CxUiWidget* c);
_Ret_maybenull_ CxUiWidget* _cxuiContainerChild(_In_opt_ CxUiWidget* c, int32 i);

/// bool cxuiContainerAppend(CxUiWidget* c, CxUiWidget* child)
///
/// Adds a widget at the end of a container
///
/// The container takes its own reference to the child.
///
/// @param c A widget that is a container
/// @param child A widget that isn't in a container yet
/// @return true if it was added; false if c isn't a live container or child is already in one
#define cxuiContainerAppend(c, child) _cxuiContainerInsert(CxUiWidget(c), CxUiWidget(child), -1)

/// bool cxuiContainerInsert(CxUiWidget* c, CxUiWidget* child, int32 at)
///
/// Adds a widget to a container before a position
///
/// @param c A widget that is a container
/// @param child A widget that isn't in a container yet
/// @param at Position to insert before; -1 or anything past the end appends
/// @return true if it was added
#define cxuiContainerInsert(c, child, at) _cxuiContainerInsert(CxUiWidget(c), CxUiWidget(child), at)

/// bool cxuiContainerDetach(CxUiWidget* c, CxUiWidget* child)
///
/// Removes a child from a container, without destroying it
///
/// The container drops its reference, so hold one first to keep the child.
///
/// @param c The container
/// @param child One of its children
/// @return true if it was removed
#define cxuiContainerDetach(c, child) _cxuiContainerDetach(CxUiWidget(c), CxUiWidget(child))

/// int32 cxuiContainerChildCount(CxUiWidget* c)
///
/// Counts a container's children
///
/// @param c The container
/// @return How many children it has; 0 if it isn't a live container
#define cxuiContainerChildCount(c) _cxuiContainerChildCount(CxUiWidget(c))

/// CxUiWidget* cxuiContainerChild(CxUiWidget* c, int32 i)
///
/// Gets one of a container's children
///
/// @param c The container
/// @param i Position, from 0
/// @return The child, borrowed, or NULL
#define cxuiContainerChild(c, i) _cxuiContainerChild(CxUiWidget(c), i)

// Internal: use cxuiFocus()
void _cxuiFocus(_In_opt_ CxUiWidget* w);

/// void cxuiFocus(CxUiWidget* w)
///
/// Gives a widget keyboard focus
///
/// Canvases, text fields, grids and drawn widgets made with `canFocus` take focus; a disabled or
/// hidden widget doesn't. Any other container gives focus to the first widget in it that takes it,
/// in the order Tab visits them. The widget's window must be shown for this to have any effect;
/// the widget gets a focus event once it has focus.
///
/// @param w The widget
#define cxuiFocus(w) _cxuiFocus(CxUiWidget(w))

/// Gets the widget with keyboard focus in a window
/// @param win The window
/// @return The focused widget, borrowed, or NULL
_Ret_maybenull_ CxUiWidget* cxuiFocused(_In_opt_ CxUiWindow* win);

// Internal: use cxuiIsTextInput()
bool _cxuiIsTextInput(_In_opt_ CxUiWidget* w);

/// bool cxuiIsTextInput(CxUiWidget* w)
///
/// Whether a widget is one the user types text into: a text field or text area, including the
/// editor over a grid cell
///
/// For sending keys such as Ctrl+Z to the widget's own handling, rather than the program's.
///
/// @param w The widget, or NULL
/// @return true if it is
///
/// Example:
/// @code
///   if (cxuiKeyMatches(CXUI_KEY_Z, CXUI_MOD_Primary, ev->key, ev->mods) && !cxuiIsTextInput(ev->base.target))
///       undoLast(doc);
/// @endcode
#define cxuiIsTextInput(w) _cxuiIsTextInput(CxUiWidget(w))

// Internal: use the macros below
float32 _cxuiWidgetScale(_In_opt_ CxUiWidget* w);
int32 _cxuiDip(_In_opt_ CxUiWidget* w, int32 px);
int32 _cxuiPx(_In_opt_ CxUiWidget* w, int32 dip);

/// float32 cxuiWidgetScale(CxUiWidget* w)
///
/// Gets the scale of the window a widget is in
///
/// Each window has one scale, which changes when it moves to a monitor with another one (see
/// cxuiOnDpiChanged()). A widget that isn't in a shown window uses the main monitor's.
///
/// @param w Any widget
/// @return Physical pixels per device-independent pixel: 1.0 at 96 DPI, 1.5 at 150%, and so on
#define cxuiWidgetScale(w) _cxuiWidgetScale(CxUiWidget(w))

/// int32 cxuiDip(CxUiWidget* w, int32 px)
///
/// Converts physical pixels to device-independent pixels, at a widget's scale
///
/// @param w Any widget
/// @param px Physical pixels
/// @return Device-independent pixels, rounded
#define cxuiDip(w, px) _cxuiDip(CxUiWidget(w), px)

/// int32 cxuiPx(CxUiWidget* w, int32 dip)
///
/// Converts device-independent pixels to physical pixels, at a widget's scale
///
/// @param w Any widget
/// @param dip Device-independent pixels
/// @return Physical pixels, rounded
#define cxuiPx(w, dip) _cxuiPx(CxUiWidget(w), dip)

/// @}  // end of cxui_widget group

CX_C_END
