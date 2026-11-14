#pragma once

/// @file menu.h
/// @brief Menu bars and their menus

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuimenu.h>

CX_C_BEGIN

/// @defgroup cxui_menu Menus
/// @ingroup cxui
/// @{
/// A window's menu bar is a CxUiMenu given to cxuiWindowSetMenu(). A menu holds CxUiMenuItem
/// widgets, and an item opens a menu of its own when it is given one as its submenu.
///
/// - **Choosing an item raises an activate event** (cxuiOnActivate()) on it, and so does
///   cxuiMenuItemChoose(). A check item has been turned on or off, and a radio item checked, by
///   the time the handler runs. Items that open a menu, separators and disabled items aren't
///   chosen.
/// - **Radio items form groups**: those next to each other, up to the first item of another kind
///   each way. Checking one, by choosing it or with cxuiMenuItemSetChecked(), clears the others.
/// - **Disabling an item** (cxuiWidgetSetEnabled()) grays it; an item that opens a menu grays its
///   title, and the menu can't be opened (on Linux it opens, with everything in it grayed).
/// - **A menu bar's items open menus.** Linux shows only those in a bar.
/// - **Menus can change at any time**, while shown too: titles, hints, check marks and enabled
///   states, and items added or destroyed, even the item whose handler is running.
/// - **Mnemonics.** An `&` before a letter in a title underlines it, and Alt with that letter
///   opens the menu or chooses the item on Windows and Linux. `&&` shows one `&`.
/// - **Shortcuts are only shown.** An item's hint, such as "Ctrl+S", appears beside its title;
///   the window's key handler does the work (cxuiOnKey()).
/// - A menu holds a reference to each of its items, an item to its submenu, and a window to its
///   menu bar; each is destroyed with its holder.
///
/// @code
///   CxUiMenu* file = cxuiMenuCreate();
///   CxUiMenuItem* open = cxuiMenuAppendItem(file, .title = _S"&Open...", .hint = _S"Ctrl+O");
///   cxuiOnActivate(open, onOpen, stvar(ptr, app));
///   cxuiMenuAppendSeparator(file);
///   cxuiOnActivate(cxuiMenuAppendItem(file, .title = _S"E&xit"), onExit, stvar(ptr, app));
///
///   CxUiMenu* bar = cxuiMenuCreate();
///   cxuiMenuAppendItem(bar, .title = _S"&File", .submenu = file);
///   objRelease(&file);
///   cxuiWindowSetMenu(win, bar);
///   objRelease(&bar);
/// @endcode

/// CxUiMenuItem* cxuiMenuItemCreate(const CxUiMenuItemDesc* desc)
///
/// Creates a menu item, to add to a menu with cxuiContainerAppend() or cxuiContainerInsert()
///
/// @param ... (desc) Initial settings, or NULL for a command with no title
/// @return The item, with a reference the caller owns
#define cxuiMenuItemCreate(...) CxUiMenuItem_createWith(__VA_ARGS__)

/// CxUiMenuItem* cxuiMenuAppendItem(CxUiMenu* menu, ...)
///
/// Adds an item at the end of a menu
///
/// The arguments after the menu are the fields of a CxUiMenuItemDesc, as designated
/// initializers.
///
/// @param menu The menu
/// @return The item, borrowed: the menu holds it
///
/// Example:
/// @code
///   CxUiMenuItem* grid = cxuiMenuAppendItem(view, .title = _S"&Grid", .kind = CXUI_MENU_Check, .checked = true);
/// @endcode
#define cxuiMenuAppendItem(menu, ...) CxUiMenu_appendItemWith(menu, &(CxUiMenuItemDesc){ __VA_ARGS__ })

/// @}  // end of cxui_menu group

CX_C_END
