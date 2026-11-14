#pragma once

/// @file drawn.h
/// @brief Widgets cxui draws itself: labels, buttons, tabs, scroll bars and links

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuidrawn.h>

CX_C_BEGIN

/// @defgroup cxui_drawn Drawn widgets
/// @ingroup cxui
/// @{
/// Drawn widgets look the same on every platform and take every color from their palette
/// (cxuiWidgetPalette()), including when disabled, when their text uses CXUI_PAL_TextDisabled
/// rather than the system's gray. Those with a native widget of the same kind are named with a D
/// after the prefix.
///
/// - **They draw on the background around them**: the nearest panel's, or the window's.
/// - **They take no keyboard focus** unless made with `canFocus`.
/// - **Setting a value from code raises no event**; only the user's actions do.
/// - **A program can take over** a drawn widget's painting or mouse handling by setting a handler
///   for the event that returns CXUI_Handled.
///
/// @code
///   CxUiDGroup* levels = cxuiDGroupCreate();
///   CxUiDButton* warn  = cxuiDButtonCreate(&(CxUiDButtonDesc){ .title = _S"&Warnings", .toggle = true,
///                                                              .group = levels });
///   cxuiOnChange(warn, onLevel, stvar(ptr, app));
/// @endcode

/// CxUiLink* cxuiLinkCreate(const CxUiLinkDesc* desc)
///
/// Creates a link
///
/// @param ... (desc) Initial settings, or NULL for an empty one
/// @return The link, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiLink* site = cxuiLinkCreate(&(CxUiLinkDesc){ .text = _S"Release notes", .url = _S"https://example.com/notes" });
///   cxuiOnActivate(site, openUrl, stvNone);   // reads cxuiLinkUrl() and opens it
/// @endcode
#define cxuiLinkCreate(...) CxUiLink_createWith(__VA_ARGS__)

/// CxUiDLabel* cxuiDLabelCreate(const CxUiDLabelDesc* desc)
///
/// Creates a drawn label
///
/// @param ... (desc) Initial settings, or NULL for an empty label
/// @return The label, with a reference the caller owns
#define cxuiDLabelCreate(...) CxUiDLabel_createWith(__VA_ARGS__)

/// CxUiDButton* cxuiDButtonCreate(const CxUiDButtonDesc* desc)
///
/// Creates a drawn button or toggle
///
/// A click raises an activate event; a toggle raises a change event first.
///
/// @param ... (desc) Initial settings, or NULL
/// @return The button, with a reference the caller owns
#define cxuiDButtonCreate(...) CxUiDButton_createWith(__VA_ARGS__)

/// CxUiDTabs* cxuiDTabsCreate(const CxUiDTabsDesc* desc)
///
/// Creates a row of drawn tabs, each showing a page of a zbox
///
/// @param ... (desc) Initial settings, or NULL; give it the zbox, then add a tab per page
/// @return The tabs, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiDTabs* tabs = cxuiDTabsCreate(&(CxUiDTabsDesc){ .zbox = pages });
///   cxuiDTabsAdd(tabs, _S"Loading");
///   cxuiDTabsAdd(tabs, _S"Commands");
/// @endcode
#define cxuiDTabsCreate(...) CxUiDTabs_createWith(__VA_ARGS__)

/// CxUiDScrollBar* cxuiDScrollBarCreate(const CxUiDScrollBarDesc* desc)
///
/// Creates a drawn scroll bar
///
/// It raises scroll events as the user moves it (cxuiOnScroll()).
///
/// @param ... (desc) Initial settings, or NULL
/// @return The scroll bar, with a reference the caller owns
#define cxuiDScrollBarCreate(...) CxUiDScrollBar_createWith(__VA_ARGS__)

/// @}  // end of cxui_drawn group

CX_C_END
