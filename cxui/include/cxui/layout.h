#pragma once

/// @file layout.h
/// @brief Boxes, spacing, and how widgets are sized and placed

#include <cxui/cxui_shared.h>
#include <cxui/widget.h>
#include <cxui/core/cxuibox.h>
#include <cxui/core/cxuicontainers.h>

CX_C_BEGIN

/// @defgroup cxui_layout Layout
/// @ingroup cxui
/// @{
/// Widgets are placed by the containers they are in. A program never gives a widget a position;
/// it puts widgets in boxes and says how each should grow.
///
/// - **Every widget has a natural size**: what it needs to show its content. A box's natural size
///   fits its children, its margin and the gaps between them. cxuiWidgetSetSize() replaces a
///   widget's natural size in one or both axes.
/// - **Spare space goes to widgets that expand.** A box shares the space it has beyond its
///   children's natural sizes equally among the children that expand along it
///   (cxuiWidgetSetExpand()). A container expands in every axis any of its children expands in, so
///   a column holding an expanding widget also grows. Across a box, a child that expands or is
///   aligned with CXUI_ALIGN_Fill gets the box's full size.
/// - **Margins and gaps belong to one box** and are never passed on to boxes inside it.
/// - **Hidden widgets take no space**, and the gap beside them goes too.
/// - **Sizes are in device-independent pixels** (DIPs), or in characters of the system's interface
///   font with cxuiChars(). A window's content is laid out again when its scale changes, and every
///   widget's edges are rounded to whole physical pixels without gaps or overlaps between
///   neighbors.
/// - **Layout runs once after a batch of changes.** Changes made by a handler or a queued task are
///   laid out together soon afterwards, before the window is painted. cxuiRefresh() lays out at
///   once, for code that needs the result straight away.
///
/// @code
///   CxUiBox* row = cxuiBoxCreate(&(CxUiBoxDesc){ .gap = 6, .margin = { 8, 8, 8, 8 } });
///   cxuiContainerAppend(row, label);
///   cxuiContainerAppend(row, field);            // set to expand, so it takes the spare width
///   cxuiWidgetSetExpand(field, CXUI_EXPAND_H);
///   cxuiContainerAppend(row, button);
/// @endcode

/// CxUiLen cxuiLen(int32 v)
///
/// A length in device-independent pixels
///
/// @param v Length in DIPs; 0 for none
/// @return The length
#define cxuiLen(v) ((CxUiLen){ (v), CXUI_UNIT_Dip })

/// CxUiLen cxuiChars(int32 v)
///
/// A length in characters of the system's interface font
///
/// Across, a character is the font's average character width; down, it is a line's height.
///
/// @param v Length in characters; 0 for none
/// @return The length
#define cxuiChars(v) ((CxUiLen){ (v), CXUI_UNIT_Chars })

#ifdef __cplusplus
#undef cxuiLen
#undef cxuiChars
_meta_inline CxUiLen cxuiLen(int32 v)
{
    CxUiLen l = { v, CXUI_UNIT_Dip };
    return l;
}
_meta_inline CxUiLen cxuiChars(int32 v)
{
    CxUiLen l = { v, CXUI_UNIT_Chars };
    return l;
}
#endif

/// CxUiBox* cxuiBoxCreate(const CxUiBoxDesc* desc)
///
/// Creates an empty box
///
/// @param ... (desc) Initial settings, or NULL for an empty row
/// @return The box, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiBox* col = cxuiBoxCreate(&(CxUiBoxDesc){ .orient = CXUI_ORIENT_V, .gap = 4 });
/// @endcode
#define cxuiBoxCreate(...) CxUiBox_createWith(__VA_ARGS__)

/// CxUiPanel* cxuiPanelCreate(const CxUiPanelDesc* desc)
///
/// Creates a box with a background color, by default the palette's panel color
///
/// @param ... (desc) Initial settings, or NULL
/// @return The panel, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiPanel* bar = cxuiPanelCreate(&(CxUiPanelDesc){ .gap = 4, .margin = { 4, 2, 4, 2 } });
/// @endcode
#define cxuiPanelCreate(...) CxUiPanel_createWith(__VA_ARGS__)

/// CxUiFrame* cxuiFrameCreate(const CxUiPanelDesc* desc)
///
/// Creates a box with a border, 1 device-independent pixel wide unless desc says otherwise
///
/// @param ... (desc) Initial settings, or NULL
/// @return The frame, with a reference the caller owns
#define cxuiFrameCreate(...) CxUiFrame_createWith(__VA_ARGS__)

/// CxUiScrollBox* cxuiScrollBoxCreate(const CxUiScrollBoxDesc* desc)
///
/// Creates a box whose content scrolls when it is larger than the room the box gets
///
/// Scrolling with the wheel or the scroll bars moves the content without laying it out again, and
/// a widget inside that gets focus is scrolled into view.
///
/// @param ... (desc) Initial settings, or NULL for a row that scrolls down
/// @return The scroll box, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiScrollBox* sb = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){ .orient = CXUI_ORIENT_V, .view = { 0, 300 } });
///   cxuiWidgetSetExpand(sb, CXUI_EXPAND_Both);
///   cxuiContainerAppend(sb, settingsColumn);
/// @endcode
#define cxuiScrollBoxCreate(...) CxUiScrollBox_createWith(__VA_ARGS__)

/// CxUiGridBox* cxuiGridBoxCreate(const CxUiGridBoxDesc* desc)
///
/// Creates an empty grid box
///
/// @param ... (desc) Initial settings, or NULL for one column
/// @return The grid box, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiGridBox* flags = cxuiGridBoxCreate(&(CxUiGridBoxDesc){ .ncols = 3, .colGap = 8, .rowGap = 4 });
/// @endcode
#define cxuiGridBoxCreate(...) CxUiGridBox_createWith(__VA_ARGS__)

/// CxUiZBox* cxuiZBoxCreate(const CxUiZBoxDesc* desc)
///
/// Creates an empty zbox, which shows one child at a time
///
/// @param ... (desc) Initial settings, or NULL
/// @return The zbox, with a reference the caller owns
#define cxuiZBoxCreate(...) CxUiZBox_createWith(__VA_ARGS__)

/// CxUiTabs* cxuiTabsCreate(const CxUiTabsDesc* desc)
///
/// Creates tabs with no pages yet
///
/// The pages take the background of CxUiTabsDesc::bgRole, which the widgets on them show.
///
/// @param ... (desc) Initial settings, or NULL
/// @return The tabs, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiTabs* tabs = cxuiTabsCreate(NULL);
///   cxuiTabsAdd(tabs, generalPage, _S"&General");
///   cxuiTabsAdd(tabs, advancedPage, _S"&Advanced");
///   cxuiOnChange(tabs, onTabPicked, stvar(ptr, app));   // fills a page the first time it is picked
/// @endcode
#define cxuiTabsCreate(...) CxUiTabs_createWith(__VA_ARGS__)

/// CxUiSplit* cxuiSplitCreate(const CxUiSplitDesc* desc)
///
/// Creates a split with no panes yet
///
/// @param ... (desc) Initial settings, or NULL for side-by-side panes sharing the space equally
/// @return The split, with a reference the caller owns
///
/// Example:
/// @code
///   CxUiSplit* split = cxuiSplitCreate(&(CxUiSplitDesc){ .orient = CXUI_ORIENT_V, .value = 0.7f, .minPane = { 80, 60 } });
///   cxuiContainerAppend(split, log);
///   cxuiContainerAppend(split, details);
/// @endcode
#define cxuiSplitCreate(...) CxUiSplit_createWith(__VA_ARGS__)

/// void cxuiWidgetSetLayout(CxUiWidget* w, const CxUiLayoutParams* lp)
///
/// Sets all of a widget's layout parameters
///
/// @param w Any widget
/// @param ... (lp) The parameters
///
/// Example:
/// @code
///   cxuiWidgetSetLayout(button, &(CxUiLayoutParams){ .normalizeGroup = 1 });
/// @endcode
#define cxuiWidgetSetLayout(w, ...) CxUiWidget_setLayoutWith(CxUiWidget(w), __VA_ARGS__)

// Internal: use the macros below
void _cxuiRefresh(_In_opt_ CxUiWidget* w);
CxUiSize _cxuiNaturalSize(_In_opt_ CxUiWidget* w);

/// void cxuiRefresh(CxUiWidget* w)
///
/// Lays out a widget's window now, rather than soon after
///
/// Use it when code needs sizes that depend on changes it has just made, such as growing the
/// window with cxuiWindowFit() after adding content.
///
/// @param w Any widget in the window
#define cxuiRefresh(w) _cxuiRefresh(CxUiWidget(w))

/// CxUiSize cxuiNaturalSize(CxUiWidget* w)
///
/// Gets the size a widget needs to show its content
///
/// Works on widgets that aren't shown yet.
///
/// @param w Any widget
/// @return The natural size in device-independent pixels, after cxuiWidgetSetSize() and
/// cxuiWidgetSetMinSize()
#define cxuiNaturalSize(w) _cxuiNaturalSize(CxUiWidget(w))

/// @}  // end of cxui_layout group

CX_C_END
