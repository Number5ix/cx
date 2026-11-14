#pragma once

/// @file grid.h
/// @brief Grids: tables of rows and columns that cxui draws itself

#include <cxui/cxui_shared.h>
#include <cxui/core/cxuigridstore.h>
#include <cxui/core/cxuitreestore.h>

CX_C_BEGIN

/// @defgroup cxui_grid Grids
/// @ingroup cxui
/// @{
/// A CxUiGrid shows rows and columns from a **source**: an object implementing CxUiGridSource,
/// such as a CxUiListStore. It draws only the rows on screen, so a source can have millions.
///
/// - **The grid keeps no copy of the data.** It asks the source for each cell it paints, and
///   remembers only its own state: column widths and the scroll position.
/// - **Tell the grid what changed.** A CxUiListStore does it itself. A source of the program's own
///   calls cxuiGridRowsInserted(), cxuiGridRowsRemoved(), cxuiGridRowsChanged(), or cxuiGridReset()
///   after sorting or filtering. A source can be shown by more than one grid.
/// - **Everything happens on the UI thread**, stores included. A source fed by other threads
///   posts its changes to the UI thread (cxuiQueue()).
/// - **Selection** follows CxUiGridDesc.select: one row, any number with Ctrl and Shift, or none.
///   Selected rows stay selected as rows are added or removed around them; cxuiGridReset()
///   unselects them all. Changing the selection from code raises no event.
/// - **Events**, raised on the grid: CXUI_EV_Change when the user changes the selection,
///   CXUI_EV_Activate (a double click, or Enter) with the row as `item`, CXUI_EV_Sort for a click
///   on a CXUI_COL_Sortable column's header, and CXUI_EV_Context for a right click, the Menu key or
///   Shift+F10. Its key handler sees keys before the grid does.
/// - **The keyboard:** the arrows, Page Up and Down, Home and End move the focus row, and Shift and
///   Ctrl select as they do with the mouse; Space selects the focus row, Ctrl+A every row; typing
///   letters and digits goes to the next row whose first text column starts with them. On the Mac,
///   Command stands for Ctrl; Return edits the focus row's first editable cell where it has one,
///   as in Finder, Command-Down activates the row, Command-Up goes to a node's parent, and a
///   Control-click is a right click. The grid's key handler sees every key first, so a program
///   that wants a key for itself handles it there.
/// - **The wheel** scrolls the rows. Turned where they can't scroll any further, as in a grid whose
///   rows all fit, it scrolls a scroll box around the grid instead, unless the same turn of the
///   wheel already scrolled the rows.
/// - **Trees:** given a CxUiTreeSource, such as a CxUiTreeStore, the grid shows a tree: each node
///   a row, indented under its parent, with an expander when it has children. Nodes open and close
///   by clicking the expander, by double-clicking or pressing Enter (unless the activate handler
///   handles it), with Right and Left, which also go to a node's first child and its parent, and
///   with the keypad's + and -. CXUI_EV_Expand comes before a node opens, so its children can be
///   added then. Events carry the node as `item`. Selection works by row as in a flat grid, and
///   stays on the same nodes as others open, close, come and go; cxuiGridNodeAt() and
///   cxuiGridRowOf() go between rows and nodes.
/// - **Check boxes:** a CXUI_COL_Check column shows a box alone, and CXUI_COL_CheckBox puts one
///   before a column's text, as for a tree's nodes. Each cell gives its box's state in
///   CxUiCell.check, or hides it with CXUI_CELL_CheckHidden. A click on a box, or Space, raises
///   CXUI_EV_Check, then sets the box through the source's CxUiGridEditable unless the handler
///   handles the event; Space changes every selected row's box when the focus row is selected.
/// - **Editing:** a CXUI_COL_Editable column's text can be edited in place, in a text field over
///   the cell, unless the cell has CXUI_CELL_ReadOnly. F2 (or Return on the Mac) edits the focus
///   row's first such cell, and so does a click on the selected focus row's cell once the time for
///   a double click has passed; cxuiGridEdit() edits any cell from code. Enter keeps the text and Esc drops it; Tab
///   keeps it and goes on to the next cell the user can edit, Shift+Tab back. Clicking elsewhere
///   in the window keeps it too, or drops it with CXUI_GRID_CancelOnBlur, and so does scrolling.
///   CXUI_EV_Edit comes as the editor opens and after it closes; after a commit the grid sets a
///   changed text through the source's CxUiGridEditable, unless the handler handles the event.
///   The editor closes, saying nothing, when the program removes or hides its cell.
/// - Rows all have the same height, from the grid's font. Colors come from the grid's palette:
///   CXUI_PAL_Content behind the rows, CXUI_PAL_GridHeader and CXUI_PAL_GridHeaderText for the
///   headers, CXUI_PAL_GridLine between cells, CXUI_PAL_Selection and CXUI_PAL_SelectionText for
///   selected rows, CXUI_PAL_Hover for the row under the mouse; a cell can have colors of its own,
///   except in a selected row.
///
/// @code
///   static CxUiResult sortFiles(stvlist* cvars, CxUiSortEvent* ev)
///   {
///       cxuiListStoreSort(stvlNextPtr(cvars), ev->column, ev->descending);
///       return CXUI_Handled;
///   }
///
///   CxUiListStore* files = cxuiListStoreCreate(2);
///   cxuiListStoreAppend(files, _S"readme.txt", _S"2 KB");
///
///   CxUiGrid* grid = cxuiGridCreate(&(CxUiGridDesc){
///       .columns  = (CxUiGridColumn[]){ { .title = _S"Name", .flags = CXUI_COL_Stretch | CXUI_COL_Sortable },
///                                       { .title = _S"Size", .width = cxuiChars(10), .align = CXUI_ALIGN_End } },
///       .ncolumns = 2,
///       .select   = CXUI_SELECT_Multi });
///   cxuiGridSetSource(grid, files);
///   cxuiOnSort(grid, sortFiles, stvar(ptr, files));
///   cxuiContainerAppend(window, grid);
/// @endcode

/// CxUiGrid* cxuiGridCreate(const CxUiGridDesc* desc)
///
/// Creates a grid, with no source
///
/// @param ... (desc) Initial settings, or NULL for one column that fills the width
/// @return The grid, with a reference the caller owns
#define cxuiGridCreate(...) CxUiGrid_createWith(__VA_ARGS__)

/// void cxuiGridSetColumns(CxUiGrid* g, CxUiGridColumn col, ...)
///
/// Replaces a grid's columns
///
/// @param g The grid
/// @param ... The columns, each in braces
///
/// Example:
/// @code
///   cxuiGridSetColumns(grid, { .title = _S"Name", .flags = CXUI_COL_Stretch }, { .title = _S"Size" });
/// @endcode
#define cxuiGridSetColumns(g, ...)                                                                \
    CxUiGrid_setColumnArray(CxUiGrid(g), (CxUiGridColumn[]){ __VA_ARGS__ },                      \
                            sizeof((CxUiGridColumn[]){ __VA_ARGS__ }) / sizeof(CxUiGridColumn))

/// int64 cxuiListStoreAppend(CxUiListStore* store, strref cell, ...)
///
/// Adds a row at the end of a list store
///
/// @param store The store
/// @param ... The cells' text, one per column
/// @return The new row
///
/// Example:
/// @code
///   cxuiListStoreAppend(files, _S"readme.txt", _S"2 KB");
/// @endcode
#define cxuiListStoreAppend(store, ...) \
    CxUiListStore_appendCells(CxUiListStore(store), (strref[]){ __VA_ARGS__ }, sizeof((strref[]){ __VA_ARGS__ }) / sizeof(strref))

/// int64 cxuiListStoreInsert(CxUiListStore* store, int64 at, strref cell, ...)
///
/// Adds a row to a list store
///
/// @param store The store
/// @param at Where it goes; past the end appends
/// @param ... The cells' text, one per column
/// @return The new row
#define cxuiListStoreInsert(store, at, ...)                                                       \
    CxUiListStore_insertCells(CxUiListStore(store), at, (strref[]){ __VA_ARGS__ },                \
                              sizeof((strref[]){ __VA_ARGS__ }) / sizeof(strref))

/// CxUiNodeId cxuiTreeStoreAppend(CxUiTreeStore* store, CxUiNodeId parent, strref cell, ...)
///
/// Adds a node to a tree store, after its parent's other children
///
/// @param store The store
/// @param parent The parent, or 0 for the top of the tree
/// @param ... The cells' text, one per column
/// @return The new node, or 0 if the parent doesn't exist
///
/// Example:
/// @code
///   CxUiNodeId docs = cxuiTreeStoreAppend(tree, 0, _S"docs");
///   cxuiTreeStoreAppend(tree, docs, _S"readme.txt");
/// @endcode
#define cxuiTreeStoreAppend(store, parent, ...)                                                   \
    CxUiTreeStore_appendCells(CxUiTreeStore(store), parent, (strref[]){ __VA_ARGS__ },            \
                              sizeof((strref[]){ __VA_ARGS__ }) / sizeof(strref))

/// CxUiNodeId cxuiTreeStoreInsert(CxUiTreeStore* store, CxUiNodeId parent, CxUiNodeId after, strref cell, ...)
///
/// Adds a node to a tree store among its parent's children
///
/// @param store The store
/// @param parent The parent, or 0 for the top of the tree
/// @param after The child it goes after, or 0 to go first
/// @param ... The cells' text, one per column
/// @return The new node, or 0 if the parent doesn't exist or after isn't its child
#define cxuiTreeStoreInsert(store, parent, after, ...)                                            \
    CxUiTreeStore_insertCells(CxUiTreeStore(store), parent, after, (strref[]){ __VA_ARGS__ },     \
                              sizeof((strref[]){ __VA_ARGS__ }) / sizeof(strref))

/// @}  // end of cxui_grid group

CX_C_END
