#pragma once
/// @file cxuilua.h
/// @brief cxui for Lua

#include <cxlua.h>

CX_C_BEGIN

/// @defgroup cxuilua cxui for Lua
/// @{
/// The `cxui` Lua module builds windows and widgets from Lua and handles their events there.
/// Everything it does runs on the UI thread: run the Lua state there, as tasks on cxuiQueue().
///
/// @section cxuilua_build Building
///
/// Each kind of widget has a constructor taking a table: its positional entries are children, and
/// its named entries set up the widget. Named entries are those of the widget's
/// descriptor in C (`title`, `gap`, `margin`...), its properties, layout (`expand`, `align`,
/// `size`, `minsize`, `normalize`), event handlers (`onactivate`...) and, for a window,
/// `defaultenter`, `defaultesc`, `startfocus` and `closeaction`. Each can also be set on the widget
/// afterwards.
///
/// @code
///   local cxui = require "cxui"
///   local ok = cxui.button{ title = "&OK", onactivate = function(self) dlg:endpopup(1) end }
///   dlg = cxui.window{
///       cxui.vbox{ margin = 8, gap = 4,
///           cxui.label{ text = "Install path:" },
///           cxui.textfield{ text = path, expand = "h" },
///           cxui.hbox{ cxui.fill{}, ok },
///       },
///       title = "Settings", defaultenter = ok,
///   }
///   local r = dlg:popup(mainwin)
/// @endcode
///
/// Every widget also has `name`, and read-only `parent`, `window` (the window it is in) and `scale`
/// (its window's physical pixels per device-independent pixel; the main monitor's until the
/// window is shown). w:px(dips) and w:dip(px) convert between the two, rounding as layout does.
///
/// Sizes in tables are `{ w = 300, h = 200 }` in device-independent pixels, or `cw` and `ch` in
/// characters of the widget's font. Positions, indexes and values count from 0, as in C. Colors are
/// `0xRRGGBBAA` or `{ r, g, b, a }`, with `a` 255 if left out.
///
/// Panels, frames, scroll boxes and text frames take `bgrole`, one of `cxui.PAL` (`cxui.PAL.content`
/// for the white behind lists), or `bg`, a color of their own, to set their background.
///
/// @section cxuilua_grids Grids
///
/// `cxui.liststore{ columns = n }` and `cxui.treestore{ columns = n }` hold the text a grid shows;
/// `cxui.grid{ columns = { ... }, source = store }` shows it, each column a table of `title`,
/// `width` (in DIPs, or `{ cw = n }`), `minwidth`, `align`, `kind` (`cxui.COL.tree`...) and `flags`
/// (`cxui.COL.stretch | cxui.COL.sortable`...); `rowheight`, in DIPs, sets every row's height.
/// The stores and the grid have the C calls as methods in lowercase, without their prefix:
/// `store:append(...)` returns the new row, or with the parent first in a tree store the new node,
/// `grid:setselected(row, n)`, `grid:setexpanded(node)`. Tree nodes are integers.
///
/// @code
///   local files = cxui.liststore{ columns = 2 }
///   files:append("readme.txt", "2 KB")
///   local grid = cxui.grid{ source = files, select = cxui.SELECT.multi,
///       columns = { { title = "Name", flags = cxui.COL.stretch | cxui.COL.sortable },
///                   { title = "Size", width = { cw = 10 }, align = cxui.ALIGN["end"] } },
///       onsort = function(g, ev) files:sort(ev.column, ev.descending) return true end }
/// @endcode
///
/// @section cxuilua_life Lifetime
///
/// Lua holds a reference to each widget it has. A destroyed widget, with w:destroy() or by its
/// window closing with closeaction `cxui.CLOSE.destroy`, raises an error when used; destroying
/// it also lets go of its handlers, and with them anything they refer to.
///
/// @section cxuilua_events Events
///
/// A handler is a function in one of a widget's `on` fields, such as `onactivate`, called with the
/// widget and a table describing the event. Returning true handles the event, so it goes no
/// further; anything else lets cxui do what it would. An error in a handler goes to the log, and
/// is taken as not handling the event.

/// Opens the `cxui` module, leaving its table on the stack
///
/// Use it with luaL_requiref() or `package.preload`. It opens cxlua on the state too. UI thread
/// only, with cxui running.
///
/// @param L The state
/// @return 1
int luaopen_cxui(_Inout_ lua_State* L);

/// @}  // end of cxuilua group

CX_C_END
