#pragma once

/// @file cxui_shared.h
/// @brief Types shared across the cxui API

#include <cx/container/sarray.h>
#include <cx/cx.h>
#include <cx/string.h>

CX_C_BEGIN

/// @defgroup cxui_shared Shared Types
/// @ingroup cxui
/// @{

typedef struct CxUiWidget CxUiWidget;
typedef struct CxUiWindow CxUiWindow;
typedef struct CxUiCanvas CxUiCanvas;
typedef struct CxUiFont CxUiFont;
typedef struct CxUiImage CxUiImage;
typedef struct CxUiCursor CxUiCursor;
typedef struct CxUiDraw CxUiDraw;
typedef struct CxUiDGroup CxUiDGroup;
typedef struct CxUiZBox CxUiZBox;
typedef struct CxUiRenderView CxUiRenderView;
typedef struct CxUiTabs CxUiTabs;
typedef struct CxUiTextField CxUiTextField;
typedef struct CxUiTextArea CxUiTextArea;
typedef struct CxUiLabel CxUiLabel;
typedef struct CxUiButton CxUiButton;
typedef struct CxUiCheck CxUiCheck;
typedef struct CxUiRadio CxUiRadio;
typedef struct CxUiRadioGroup CxUiRadioGroup;
typedef struct CxUiList CxUiList;
typedef struct CxUiDropdown CxUiDropdown;
typedef struct CxUiSlider CxUiSlider;
typedef struct CxUiProgress CxUiProgress;
typedef struct CxUiScrollBox CxUiScrollBox;
typedef struct CxUiLink CxUiLink;
typedef struct CxUiGrid CxUiGrid;
typedef struct CxUiListStore CxUiListStore;
typedef struct CxUiTreeStore CxUiTreeStore;
typedef struct CxUiMenu CxUiMenu;
typedef struct CxUiMenuItem CxUiMenuItem;

// Internal: a widget's native counterpart, owned by the backend
typedef struct CxUiPeer CxUiPeer;
// Internal: one of a widget's occupied event slots
typedef struct CxUiHandlerSlot CxUiHandlerSlot;
saDeclare(CxUiHandlerSlot);

/// A point, in physical pixels unless documented otherwise
typedef struct CxUiPoint {
    int32 x, y;
} CxUiPoint;

/// A size, in physical pixels unless documented otherwise
typedef struct CxUiSize {
    int32 w, h;
} CxUiSize;

/// A rectangle: top-left corner and size, in physical pixels unless documented otherwise
typedef struct CxUiRect {
    int32 x, y, w, h;
} CxUiRect;

/// What a handler did with an event
typedef enum CxUiResult {
    /// Not handled: the event bubbles on, and the platform's own handling runs
    CXUI_Default = 0,
    /// Handled: the event stops here, and the platform's own handling is suppressed
    CXUI_Handled = 1,
} CxUiResult;

/// Kinds of events, one per handler slot
typedef enum CxUiEventKind {
    CXUI_EV_Activate,     ///< A button, link or menu item was used, or a list item opened
    CXUI_EV_Change,       ///< A value the user can edit changed; read it from the widget
    CXUI_EV_Commit,       ///< A text field was confirmed with Enter, or lost focus after an edit
    CXUI_EV_Key,          ///< A key went down or up in the focused widget (bubbles)
    CXUI_EV_Mouse,        ///< The mouse moved, or a button went down or up, over a canvas
    CXUI_EV_Wheel,        ///< The mouse wheel turned over a widget (bubbles)
    CXUI_EV_Focus,        ///< A widget gained or lost focus, or a window became active or inactive
    CXUI_EV_Paint,        ///< A canvas needs part of itself drawn
    CXUI_EV_Resize,       ///< A canvas, render view or window changed size
    CXUI_EV_Close,        ///< Something asked to close a window
    CXUI_EV_ShowState,    ///< A window was minimized, maximized, made fullscreen or restored
    CXUI_EV_Move,         ///< A window moved
    CXUI_EV_DpiChanged,   ///< A window's scale changed, for example on moving to another monitor
    CXUI_EV_ThemeChanged, ///< A window changed between light and dark
    CXUI_EV_Scroll,       ///< The user moved a scroll bar
    CXUI_EV_Sort,         ///< The user clicked a grid column's header to sort by it
    CXUI_EV_Context,      ///< The user asked for a context menu: a right click, the Menu key or Shift+F10
    CXUI_EV_Expand,       ///< The user is opening or closing a node of a tree
    CXUI_EV_Check,        ///< The user is checking or clearing a grid cell's check box
    CXUI_EV_Edit,         ///< A grid cell's editor opened, or closed with or without a change
    CXUI_EV_Surface,      ///< A render view's native surface came, or is about to go
    CXUI_EV_Text,         ///< Text was typed into a canvas that takes text (bubbles)
    CXUI_EV_Count
} CxUiEventKind;

/// The start of every event struct
typedef struct CxUiEvent {
    CxUiEventKind kind;
    CxUiWidget* target;    ///< Where the event started
    CxUiWidget* current;   ///< Whose handler is running; differs from target while bubbling
    CxUiWindow* window;    ///< The window the target is in, or NULL
} CxUiEvent;

/// A color, with straight (not premultiplied) alpha
///
/// Make one with cxuiRGB(), cxuiRGBA(), cxuiHex() or cxuiHexA(), or with CXUI_HEX() or
/// CXUI_HEXA() in a static table. Compare two with `a.bits == b.bits`.
typedef union CxUiColor {
    struct {
        uint8 r, g, b;
        uint8 a;   ///< 255 is opaque, 0 is invisible
    };
    /// All four bytes as one value, for comparing and hashing only; it is not a hex color
    uint32 bits;
} CxUiColor;

/// What each color in a CxUiPalette is for
typedef enum CxUiPaletteRole {
    CXUI_PAL_Panel,            ///< Windows, panels, tool bars and tab strips
    CXUI_PAL_Content,          ///< Behind grids, lists and canvases
    CXUI_PAL_Border,           ///< Frames, separators and sashes
    CXUI_PAL_Text,             ///< Normal text
    CXUI_PAL_TextDim,          ///< Secondary text
    CXUI_PAL_TextDisabled,     ///< Text in a disabled drawn widget
    CXUI_PAL_Accent,           ///< Focus accents, the selected tab, checked toggles
    CXUI_PAL_Selection,        ///< Selected rows and text
    CXUI_PAL_SelectionText,    ///< Text on CXUI_PAL_Selection
    CXUI_PAL_Hover,            ///< A drawn button under the mouse
    CXUI_PAL_Pressed,          ///< A drawn button held down
    CXUI_PAL_Input,            ///< Background of a text field
    CXUI_PAL_InputText,        ///< Text in a text field
    CXUI_PAL_InputBorder,      ///< Border of a text field
    CXUI_PAL_InputBad,         ///< Background of a text field whose contents are wrong
    CXUI_PAL_InputCue,         ///< Placeholder text in an empty text field
    CXUI_PAL_Focus,            ///< Focus ring
    CXUI_PAL_ScrollTrack,      ///< Drawn scroll bar background
    CXUI_PAL_ScrollThumb,      ///< Drawn scroll bar thumb
    CXUI_PAL_ScrollThumbHot,   ///< The thumb under the mouse or being dragged
    CXUI_PAL_TooltipBg,        ///< Drawn tooltip background
    CXUI_PAL_TooltipText,      ///< Drawn tooltip text
    CXUI_PAL_Link,             ///< A drawn link
    CXUI_PAL_LinkHover,        ///< A drawn link under the mouse
    CXUI_PAL_GridLine,         ///< Lines between grid cells
    CXUI_PAL_GridHeader,       ///< Grid column header background
    CXUI_PAL_GridHeaderText,   ///< Grid column header text
    CXUI_PAL_Count
} CxUiPaletteRole;

// Internal: the colors of a palette, by CxUiPaletteRole
typedef struct CxUiPaletteColors {
    CxUiColor c[CXUI_PAL_Count];
} CxUiPaletteColors;

/// Space around the inside of a container, in device-independent pixels
typedef struct CxUiEdges {
    int32 left, top, right, bottom;
} CxUiEdges;

/// What a CxUiLen counts
typedef enum CxUiUnit {
    /// Device-independent pixels
    CXUI_UNIT_Dip = 0,
    /// Average character widths across, line heights down, of the system's interface font
    CXUI_UNIT_Chars,
} CxUiUnit;

/// A length for layout: a number and its unit; see cxuiLen() and cxuiChars()
typedef struct CxUiLen {
    /// 0 for none: the natural size, or no minimum
    int32 v;
    /// CxUiUnit
    uint8 unit;
} CxUiLen;

/// Axes a widget expands in, or a box normalizes
///
/// A container also expands in every axis any of its children expands in, unless it has a stop
/// bit for that axis.
typedef enum CxUiExpand {
    CXUI_EXPAND_None = 0,
    CXUI_EXPAND_H    = 1,
    CXUI_EXPAND_V    = 2,
    CXUI_EXPAND_Both = 3,
    /// A container that doesn't expand across just because a child does; its children still
    /// expand inside it
    CXUI_EXPAND_StopH = 4,
    /// The same, down
    CXUI_EXPAND_StopV = 8,
    /// The same, in both axes
    CXUI_EXPAND_Stop = 12,
} CxUiExpand;

/// Where a widget sits in a slot larger than it
typedef enum CxUiAlign {
    /// Whatever the container uses: its own alignment, or the start
    CXUI_ALIGN_Default = 0,
    CXUI_ALIGN_Start,
    CXUI_ALIGN_Center,
    CXUI_ALIGN_End,
    /// Stretched to fill the slot
    CXUI_ALIGN_Fill,
} CxUiAlign;

/// Flags for CxUiLayoutParams
enum CxUiLayoutFlags {
    /// Left out of layout; the widget keeps the bounds given to cxuiWidgetSetBounds()
    CXUI_LP_Floating = 0x01,
    /// Created along with its window even on a page that isn't shown yet, such as an inactive
    /// zbox page, rather than when first shown; applies to everything inside it
    CXUI_LP_RealizeEager = 0x02,
};

/// How a widget asks to be laid out; every field left at 0 takes the default
typedef struct CxUiLayoutParams {
    /// Size per axis (width, height), replacing the natural size; 0 for the natural size
    CxUiLen size[2];
    /// Smallest size per axis, even when shrunk; 0 for none
    CxUiLen minSize[2];
    /// CxUiExpand: axes in which the widget takes a share of its container's spare space
    uint8 expand;
    /// CxUiAlign per axis (horizontal, vertical)
    uint8 align[2];
    /// Widgets in one window with the same group number share the largest natural size; 0 for
    /// none
    uint8 normalizeGroup;
    /// CxUiLayoutFlags
    uint8 flags;
} CxUiLayoutParams;

/// Direction a box lays its children out in
typedef enum CxUiOrient {
    CXUI_ORIENT_H = 0,
    CXUI_ORIENT_V,
} CxUiOrient;

/// Initial settings for cxuiBoxCreate(); a field left at 0 takes the default
typedef struct CxUiBoxDesc {
    /// CxUiOrient (default CXUI_ORIENT_H)
    uint32 orient;
    /// Space inside the box's edges, in device-independent pixels
    CxUiEdges margin;
    /// Space between children, in device-independent pixels
    int32 gap;
    /// CxUiAlign per axis for children that don't set their own; along the box, it places the
    /// children when none expands
    uint8 align[2];
    /// CxUiExpand: axes in which every child gets the size of the largest
    uint8 normalize;
    CxUiLayoutParams lp;
    strref name;
} CxUiBoxDesc;

/// Initial settings for cxuiPanelCreate() and cxuiFrameCreate(); a field left at 0 takes the
/// default
typedef struct CxUiPanelDesc {
    /// CxUiOrient (default CXUI_ORIENT_H)
    uint32 orient;
    /// Space inside the edges (and inside a frame's border), in device-independent pixels
    CxUiEdges margin;
    /// Space between children, in device-independent pixels
    int32 gap;
    /// As for CxUiBoxDesc
    uint8 align[2];
    /// As for CxUiBoxDesc
    uint8 normalize;
    /// CxUiPaletteRole of the background (default CXUI_PAL_Panel); a frame without one shows the
    /// background around it
    uint32 bgRole;
    /// A background color in place of the palette's; leave it at 0 to use bgRole
    CxUiColor bg;
    /// Width of the border, in device-independent pixels (a frame's default is 1)
    int32 border;
    /// A frame's title, shown in the gap it leaves in the top of its border
    strref title;
    CxUiLayoutParams lp;
    strref name;
} CxUiPanelDesc;

/// Initial settings for cxuiScrollBoxCreate(); a field left at 0 takes the default
typedef struct CxUiScrollBoxDesc {
    /// CxUiOrient of the children inside (default CXUI_ORIENT_H)
    uint32 orient;
    /// CxUiExpand: the axes the content scrolls in (default CXUI_EXPAND_V)
    uint8 scroll;
    /// Its natural size along the axes that scroll, in device-independent pixels; 0 for the
    /// content's, so it scrolls only when its container gives it less
    CxUiSize view;
    /// Space inside the edges, around the content, in device-independent pixels
    CxUiEdges margin;
    /// Space between children, in device-independent pixels
    int32 gap;
    /// As for CxUiBoxDesc
    uint8 align[2];
    /// CxUiPaletteRole of the background; the default shows the background around it, and
    /// cxuiPanelSetBgRole() can pick CXUI_PAL_Panel
    uint32 bgRole;
    CxUiLayoutParams lp;
    strref name;
} CxUiScrollBoxDesc;

/// Initial settings for cxuiGridBoxCreate(); a field left at 0 takes the default
typedef struct CxUiGridBoxDesc {
    /// Columns (default 1); children fill the rows from left to right
    int32 ncols;
    /// Space between columns and between rows, in device-independent pixels
    int32 colGap;
    int32 rowGap;
    /// Space inside the edges, in device-independent pixels
    CxUiEdges margin;
    CxUiLayoutParams lp;
    strref name;
} CxUiGridBoxDesc;

/// Flags for CxUiZBoxDesc
enum CxUiZBoxFlags {
    /// The natural size is the shown child's, rather than the largest child's
    CXUI_ZBOX_SizeToActive = 0x01,
};

/// Initial settings for cxuiZBoxCreate(); a field left at 0 takes the default
typedef struct CxUiZBoxDesc {
    /// CxUiZBoxFlags
    flags_t flags;
    CxUiLayoutParams lp;
    strref name;
} CxUiZBoxDesc;

/// Flags for CxUiTabsDesc, beside CxUiZBoxFlags
enum CxUiTabsFlags {
    /// Tabs that don't fit the width go onto more lines, where the platform can do that (Windows
    /// and macOS), rather than scrolling; the pages then get less height as the tabs get narrower
    CXUI_TABS_Multiline = 0x0100,
};

/// Initial settings for cxuiTabsCreate(); a field left at 0 takes the default
typedef struct CxUiTabsDesc {
    /// CxUiZBoxFlags and CxUiTabsFlags
    flags_t flags;
    /// CxUiPaletteRole of the pages' background (default CXUI_PAL_Content); cxuiTabsSetBgRole()
    /// can pick CXUI_PAL_Panel
    uint32 bgRole;
    CxUiLayoutParams lp;
    strref name;
} CxUiTabsDesc;

/// Initial settings for cxuiSplitCreate(); a field left at 0 takes the default
typedef struct CxUiSplitDesc {
    /// CxUiOrient: CXUI_ORIENT_H puts the panes side by side (the default)
    uint32 orient;
    /// Share of the space the first pane gets, from 0 to 1 (0 for half)
    float32 value;
    /// Smallest and largest size of each pane, in device-independent pixels; 0 for no limit
    int32 minPane[2];
    int32 maxPane[2];
    /// Thickness of the sash between the panes, in device-independent pixels (default 5)
    int32 sash;
    /// Shows a grip in the middle of the sash
    bool grip;
    CxUiLayoutParams lp;
    strref name;
} CxUiSplitDesc;

/// Initial settings for cxuiDLabelCreate(); a field left at 0 takes the default
typedef struct CxUiDLabelDesc {
    strref text;
    /// CxUiTextAlign (default left and top)
    flags_t align;
    /// Shortens text that doesn't fit with an ellipsis
    bool ellipsis;
    /// Space around the text, in device-independent pixels, across and down
    CxUiSize padding;
    CxUiLayoutParams lp;
    strref name;
} CxUiDLabelDesc;

/// When a link is underlined
typedef enum CxUiLinkUnderline {
    CXUI_LINK_Underline,    ///< Always
    CXUI_LINK_HoverOnly,    ///< While the mouse is over it, or it has focus
    CXUI_LINK_NoUnderline,  ///< Never
} CxUiLinkUnderline;

/// Initial settings for cxuiLinkCreate(); a field left at 0 takes the default
typedef struct CxUiLinkDesc {
    /// The text
    strref text;
    /// Where it leads, kept for the program to open; cxui never opens it
    strref url;
    /// CxUiLinkUnderline
    uint32 underline;
    /// Its color, and while the mouse is over it; alpha 0 (the default) for CXUI_PAL_Link and
    /// CXUI_PAL_LinkHover
    CxUiColor color, hoverColor;
    /// Takes no keyboard focus; by default Tab stops at it, and Space or Enter opens it
    bool noFocus;
    CxUiLayoutParams lp;
    strref name;
} CxUiLinkDesc;

/// Flags for CxUiCell
enum CxUiCellFlags {
    CXUI_CELL_Bold = 0x0001,   ///< Bold text
    CXUI_CELL_Dim  = 0x0002,   ///< Text in CXUI_PAL_TextDim, unless the cell has its own color
    /// No check box, in a column that has them
    CXUI_CELL_CheckHidden = 0x0004,
    /// The user can't edit it, in a column with CXUI_COL_Editable
    CXUI_CELL_ReadOnly = 0x0008,
};

/// What a grid's source says about one cell (see CxUiGridSource)
///
/// The grid clears it before each call. Copying in a string the source keeps, with strDup(), costs
/// nothing.
typedef struct CxUiCell {
    /// The text
    string text;
    /// Colors of its own for the text and the background; alpha 0 for the palette's
    CxUiColor fg, bg;
    /// Drawn before the text, as tall as the text; borrowed for the call. NULL for none.
    CxUiImage* image;
    /// CxUiCellFlags
    flags_t flags;
    /// CxUiCheckState of its check box, in a column that has them
    uint8 check;
} CxUiCell;

/// A node of a tree (CxUiTreeSource) that stays the same while the node exists; 0 for none, or
/// for the top of the tree
typedef uint64 CxUiNodeId;

/// What a grid column shows
typedef enum CxUiGridColumnKind {
    CXUI_COL_Text = 0,   ///< Text, with an optional image before it
    /// Text indented by depth after the node's expander, in a tree; without one, the first column
    CXUI_COL_Tree,
    /// A check box alone, centered
    CXUI_COL_Check,
} CxUiGridColumnKind;

/// Flags for CxUiGridColumn
enum CxUiGridColumnFlags {
    /// Takes the width the other columns leave; only the first such column does
    CXUI_COL_Stretch = 0x0001,
    /// Not shown
    CXUI_COL_Hidden = 0x0002,
    /// A click on the header raises CXUI_EV_Sort and shows an arrow
    CXUI_COL_Sortable = 0x0004,
    /// The user can't drag the header's edge to change the width
    CXUI_COL_FixedWidth = 0x0008,
    /// A check box before the text
    CXUI_COL_CheckBox = 0x0010,
    /// The user can edit the text in place, with F2 or a click on the selected row
    CXUI_COL_Editable = 0x0020,
};

/// A column of a grid (cxuiGridSetColumns())
typedef struct CxUiGridColumn {
    /// The header's text
    strref title;
    /// Width; 0 to fit the title
    CxUiLen width;
    /// The narrowest it gets; 0 for the default
    CxUiLen minWidth;
    /// CxUiAlign: where the text sits, from CXUI_ALIGN_Start (the default) to CXUI_ALIGN_End
    uint8 align;
    /// CxUiGridColumnKind
    uint8 kind;
    /// CxUiGridColumnFlags
    flags_t flags;
} CxUiGridColumn;

/// Flags for CxUiGridDesc
enum CxUiGridFlags {
    CXUI_GRID_NoHeader = 0x0001,   ///< No column headers
    CXUI_GRID_Stripes  = 0x0002,   ///< Every other row in CXUI_PAL_Panel
    CXUI_GRID_Lines    = 0x0004,   ///< Lines between cells, in CXUI_PAL_GridLine
    CXUI_GRID_TreeLines = 0x0008,  ///< Lines joining a tree's nodes to their parents, in CXUI_PAL_GridLine
    /// Leaving a cell's editor for another widget drops what was typed, as Esc does, rather than
    /// keeping it
    CXUI_GRID_CancelOnBlur = 0x0010,
    /// While the last row is shown, rows the source adds keep it shown, as a log follows its end;
    /// scrolling away, or editing a cell, stops it until the last row is shown again
    CXUI_GRID_FollowTail = 0x0020,
};

/// How many rows of a grid, or items of a list, the user can select
typedef enum CxUiSelectMode {
    CXUI_SELECT_Single = 0,   ///< One at a time
    CXUI_SELECT_Multi,        ///< Any number, with Ctrl and Shift
    CXUI_SELECT_None,         ///< None; the arrow keys move only the focus row
} CxUiSelectMode;

/// Initial settings for cxuiGridCreate(); a field left at 0 takes the default
typedef struct CxUiGridDesc {
    /// The columns, copied; none for one column without a title that fills the width
    const CxUiGridColumn* columns;
    int32 ncolumns;
    /// CxUiGridFlags
    flags_t flags;
    /// CxUiSelectMode
    uint8 select;
    /// Height of every row, as cxuiLen() or cxuiChars(); 0 for the font's height and a little room
    CxUiLen rowHeight;
    CxUiLayoutParams lp;
    strref name;
} CxUiGridDesc;

// Internal: a grid's view of a tree, kept in the grid's code
typedef struct CxUiGridTree CxUiGridTree;

// Internal: a node of a CxUiTreeStore, or a free slot
typedef struct CxUiTreeSlot {
    // Bumped when the slot is freed, so old ids stop matching
    uint32 gen;
    bool used;
    // The next free slot, while free
    int32 nextFree;
    CxUiNodeId parent, first, last, prev, next;
    // _ncols of them, and their CxUiCheckState
    string* cells;
    uint8* checks;
    flags_t flags;
    uint32 fg, bg;
    stvar data;
} CxUiTreeSlot;

// Internal: how much of a text fits a width in a font, as a grid remembers it
typedef struct CxUiGridFit {
    CxUiFont* font;   // not a reference; NULL for an empty entry
    uint32 hash;
    int32 len, width;
    // Bytes kept before the ellipsis, or len when it all fits, and the width of what is shown
    int32 keep, adv;
} CxUiGridFit;

// Internal: a grid column as the grid keeps it
typedef struct CxUiGridCol {
    string title;
    CxUiLen width, minWidth;
    uint8 align, kind;
    flags_t flags;
    // As laid out, in physical pixels: the left edge from the first column's, and the width
    int32 x, w;
} CxUiGridCol;

/// Colors of a drawn button that can be set apart from its palette's
typedef enum CxUiDButtonColor {
    CXUI_DBC_Bg,              ///< Background (default: the background around it)
    CXUI_DBC_Text,            ///< Text (CXUI_PAL_Text)
    CXUI_DBC_Hover,           ///< Background under the mouse (CXUI_PAL_Hover)
    CXUI_DBC_Pressed,         ///< Background held down (CXUI_PAL_Pressed)
    CXUI_DBC_Checked,         ///< Background of a toggle that is on (CXUI_PAL_Selection)
    CXUI_DBC_Border,          ///< Border (CXUI_PAL_Border)
    CXUI_DBC_BorderHover,     ///< Border under the mouse (CXUI_PAL_Accent)
    CXUI_DBC_BorderPressed,   ///< Border held down, or of a toggle that is on (CXUI_PAL_Accent)
    CXUI_DBC_TextHover,       ///< Text under the mouse (CXUI_PAL_Text)
    CXUI_DBC_TextPressed,     ///< Text held down, or of a toggle that is on (CXUI_PAL_SelectionText)
    CXUI_DBC_Count
} CxUiDButtonColor;

// Internal: a drawn button's own colors, by CxUiDButtonColor
typedef struct CxUiDButtonColors {
    CxUiColor c[CXUI_DBC_Count];
} CxUiDButtonColors;

/// Initial settings for cxuiDButtonCreate(); a field left at 0 takes the default
typedef struct CxUiDButtonDesc {
    /// The title; `&` marks a mnemonic and `&&` is a literal ampersand
    strref title;
    /// An icon, drawn before the title
    CxUiImage* image;
    /// Stays down when clicked, until clicked again
    bool toggle;
    /// For a toggle: starts down
    bool value;
    /// For a toggle: only one toggle in the group is down at a time
    CxUiDGroup* group;
    /// Space around the title, in device-independent pixels, across and down (default 8 by 4)
    CxUiSize padding;
    /// Takes keyboard focus when clicked, shows a focus ring, and is pressed with Space or Enter
    bool canFocus;
    CxUiLayoutParams lp;
    strref name;
} CxUiDButtonDesc;

/// Initial settings for cxuiDTabsCreate(); a field left at 0 takes the default
typedef struct CxUiDTabsDesc {
    /// The zbox whose pages the tabs show; tab n shows its child n
    CxUiZBox* zbox;
    /// Space around each title, in device-independent pixels, across and down (default 10 by 3)
    CxUiSize padding;
    /// Space between tabs, in device-independent pixels (default 3)
    int32 gap;
    CxUiLayoutParams lp;
    strref name;
} CxUiDTabsDesc;

/// Initial settings for cxuiDScrollBarCreate(); a field left at 0 takes the default
typedef struct CxUiDScrollBarDesc {
    /// CxUiOrient: CXUI_ORIENT_V for a bar that scrolls up and down
    uint32 orient;
    /// Units of the whole content: rows, lines, pixels, whatever the program scrolls by
    int64 range;
    /// Units visible at once
    int64 page;
    /// The first unit visible
    int64 pos;
    /// Units per wheel notch or arrow key (default 1)
    int64 step;
    /// Hides the bar while everything is visible
    bool autoHide;
    /// Thickness, in device-independent pixels (default 12)
    int32 thickness;
    CxUiLayoutParams lp;
    strref name;
} CxUiDScrollBarDesc;

/// Initial settings for cxuiTextFieldCreate(); a field left at 0 takes the default
typedef struct CxUiTextFieldDesc {
    strref text;
    /// Shown in gray while the field is empty and doesn't have focus
    strref cue;
    /// Characters the field shows for its natural width (default 20)
    int32 columns;
    /// Most characters the user can type; 0 for no limit
    int32 maxLength;
    /// The user can select and copy the text, but not change it
    bool readOnly;
    CxUiLayoutParams lp;
    strref name;
} CxUiTextFieldDesc;

/// Initial settings for cxuiTextAreaCreate(); a field left at 0 takes the default
typedef struct CxUiTextAreaDesc {
    /// The text, with `\n` between lines
    strref text;
    /// Characters across its natural width (default 40)
    int32 columns;
    /// Lines down its natural height (default 4)
    int32 lines;
    /// Breaks lines at words to fit the width, rather than scrolling across
    bool wrap;
    /// Most characters the user can type; 0 for no limit
    int32 maxLength;
    /// The user can select and copy the text, but not change it
    bool readOnly;
    CxUiLayoutParams lp;
    strref name;
} CxUiTextAreaDesc;

/// Initial settings for cxuiTextFrameCreate(); a field left at 0 takes the default
typedef struct CxUiTextFrameDesc {
    /// The field inside the frame; the frame holds a reference to it
    CxUiTextField* field;
    /// Space between the border and the text, in device-independent pixels, across and down
    /// (default 3 by 2)
    CxUiSize padding;
    /// Width of the border, in device-independent pixels (default 1)
    int32 border;
    /// No border; the background shows to the edges
    bool noBorder;
    CxUiLayoutParams lp;
    strref name;
} CxUiTextFrameDesc;

/// How a label shortens text that doesn't fit
typedef enum CxUiEllipsis {
    /// Cut off at the edge
    CXUI_ELLIPSIS_None = 0,
    /// "…" at the end
    CXUI_ELLIPSIS_End,
    /// "…" in the middle, keeping the start and the end
    CXUI_ELLIPSIS_Middle,
    /// "…" in place of folders in the middle of a path, keeping its last part (the middle on Linux
    /// and macOS)
    CXUI_ELLIPSIS_Path,
} CxUiEllipsis;

/// Initial settings for cxuiLabelCreate(); a field left at 0 takes the default
typedef struct CxUiLabelDesc {
    /// The text, with `\n` between lines; `&` before a letter makes it the mnemonic, `&&` is one
    /// `&`
    strref text;
    /// CXUI_TEXT_Left, CXUI_TEXT_Center or CXUI_TEXT_Right, for text narrower than the label
    flags_t align;
    /// CxUiEllipsis, for text wider than the label; not with wrap
    uint32 ellipsis;
    /// Breaks the text into lines to fit the width it gets, growing down
    bool wrap;
    /// With wrap, the most average characters across its natural width (default 40); wider when
    /// its container gives it more
    int32 columns;
    /// The text's color; alpha 0 (the default) for the palette's text color
    CxUiColor color;
    CxUiLayoutParams lp;
    strref name;
} CxUiLabelDesc;

/// What a menu item is
typedef enum CxUiMenuItemKind {
    CXUI_MENU_Command = 0,   ///< Chosen to run a command, or to open its submenu
    CXUI_MENU_Check,         ///< Turned on or off each time it is chosen
    CXUI_MENU_Radio,         ///< One of a group, which runs to the next item of another kind
    CXUI_MENU_Separator,     ///< A line between items
} CxUiMenuItemKind;

/// Initial settings for cxuiMenuItemCreate(); a field left at 0 takes the default
typedef struct CxUiMenuItemDesc {
    /// The title; `&` before a letter makes it the mnemonic, `&&` is one `&`
    strref title;
    /// A shortcut shown beside the title, such as "Ctrl+S"; it is only shown
    strref hint;
    /// CxUiMenuItemKind
    uint32 kind;
    /// A check or radio item starts checked
    bool checked;
    /// The menu the item opens; the item holds it from then on
    CxUiMenu* submenu;
    strref name;
} CxUiMenuItemDesc;

/// Initial settings for cxuiButtonCreate(); a field left at 0 takes the default
typedef struct CxUiButtonDesc {
    /// The title; `&` before a letter makes it the mnemonic, `&&` is one `&`
    strref title;
    /// Space added to the platform's size for the title, in device-independent pixels, on each
    /// side across and down
    CxUiSize padding;
    CxUiLayoutParams lp;
    strref name;
} CxUiButtonDesc;

/// What a check box shows
typedef enum CxUiCheckState {
    CXUI_CHECK_Off,     ///< Not checked
    CXUI_CHECK_On,      ///< Checked
    CXUI_CHECK_Mixed,   ///< Neither, as for a setting that differs between the things it covers
} CxUiCheckState;

/// Initial settings for cxuiCheckCreate(); a field left at 0 takes the default
typedef struct CxUiCheckDesc {
    /// The title; `&` before a letter makes it the mnemonic, `&&` is one `&`
    strref title;
    /// CxUiCheckState to start in
    uint32 state;
    /// Clicks go from checked to mixed before unchecked; otherwise only code sets mixed
    bool threeState;
    CxUiLayoutParams lp;
    strref name;
} CxUiCheckDesc;

/// Initial settings for cxuiRadioCreate(); a field left at 0 takes the default
typedef struct CxUiRadioDesc {
    /// The title; `&` before a letter makes it the mnemonic, `&&` is one `&`
    strref title;
    CxUiLayoutParams lp;
    strref name;
} CxUiRadioDesc;

/// Initial settings for cxuiListCreate(); a field left at 0 takes the default
typedef struct CxUiListDesc {
    /// The items, ending with NULL; or NULL for none
    const strref* items;
    /// The item selected to begin with; 0 selects the first, -1 none
    int32 value;
    /// Its natural height, in items (default 5)
    int32 visibleItems;
    /// Its natural width, in average characters; 0 to fit the widest item
    int32 columns;
    /// CxUiSelectMode: CXUI_SELECT_Single (the default) or CXUI_SELECT_Multi, where the user
    /// selects any number with Ctrl and Shift
    uint8 select;
    CxUiLayoutParams lp;
    strref name;
} CxUiListDesc;

/// Initial settings for cxuiDropdownCreate(); a field left at 0 takes the default
typedef struct CxUiDropdownDesc {
    /// The items, ending with NULL; or NULL for none
    const strref* items;
    /// The item selected to begin with; 0 selects the first, -1 none
    int32 value;
    /// Its natural width, in average characters; 0 to fit the widest item
    int32 columns;
    CxUiLayoutParams lp;
    strref name;
} CxUiDropdownDesc;

/// Initial settings for cxuiSliderCreate(); a field left at 0 takes the default
typedef struct CxUiSliderDesc {
    /// The lowest and highest values; both 0 for 0 to 100
    float64 min, max;
    /// The value to begin with, snapped to a step
    float64 value;
    /// The difference between values the slider can take (default a hundredth of the range)
    float64 step;
    /// How far Page Up and Page Down move it (default ten steps)
    float64 pageStep;
    /// Tick marks along it, at even spaces from end to end; 0 for none
    int32 ticks;
    /// CxUiOrient: CXUI_ORIENT_V for one that runs up, its lowest value at the bottom
    uint32 orient;
    /// Its natural length, in device-independent pixels (default 160)
    int32 length;
    CxUiLayoutParams lp;
    strref name;
} CxUiSliderDesc;

/// Initial settings for cxuiProgressCreate(); a field left at 0 takes the default
typedef struct CxUiProgressDesc {
    /// How much is done, from 0 to max
    float64 value;
    /// The value at which it is full (default 100)
    float64 max;
    /// Shows activity without saying how much is done
    bool indeterminate;
    /// Its natural length, in device-independent pixels (default 160)
    int32 length;
    CxUiLayoutParams lp;
    strref name;
} CxUiProgressDesc;

/// Initial settings for cxuiProgressDialogCreate(); a field left at 0 takes the default
typedef struct CxUiProgressDialogDesc {
    strref title;
    /// A line describing the work
    strref text;
    /// The amount of work; 0 shows activity without saying how much is done
    int64 total;
    /// Has a Cancel button
    bool cancel;
    /// The Cancel button's title (default "Cancel")
    strref cancelTitle;
    strref name;
} CxUiProgressDialogDesc;

// Internal: a rectangle in 1/256 device-independent pixels, relative to the window
typedef struct CxUiLayoutRect {
    int32 x, y, w, h;
} CxUiLayoutRect;

/// Light or dark appearance
typedef enum CxUiThemeMode {
    /// Follow the system's setting
    CXUI_Theme_System = 0,
    CXUI_Theme_Light,
    CXUI_Theme_Dark,
} CxUiThemeMode;

/// How a window's size is chosen
typedef enum CxUiWindowSizing {
    /// Fits its content when first shown, and again whenever the content's natural size changes,
    /// until the user or the program resizes it. A size given as well is the smallest it starts
    /// at.
    CXUI_SIZE_Natural = 0,
    /// The size given; the user can't resize it
    CXUI_SIZE_Fixed,
    /// Starts at the size given (or the natural size, without one); the user can resize it
    CXUI_SIZE_Start,
} CxUiWindowSizing;

/// Flags for CxUiWindowDesc
enum CxUiWindowFlags {
    /// The window may be made smaller than its content's natural size; its content then gives up
    /// space, starting with the widgets that expand
    CXUI_WINDOW_Shrink = 0x0001,
    /// No frame or title bar, and the user can't resize it. The window keeps its taskbar button,
    /// with the system menu and minimize there; cxuiWindowBeginDrag() lets the user move it.
    CXUI_WINDOW_Borderless = 0x0002,
};

/// Initial settings for cxuiWindowCreate(); a field left at 0 takes the default
typedef struct CxUiWindowDesc {
    strref title;
    /// Size of the area inside the frame, in device-independent pixels; see sizing
    CxUiSize size;
    /// CxUiWindowSizing (default CXUI_SIZE_Natural)
    uint32 sizing;
    /// Smallest size of the area inside the frame, in device-independent pixels. Without
    /// CXUI_WINDOW_Shrink, the window is never smaller than its content's natural size either.
    CxUiSize minSize;
    /// CxUiWindowFlags
    flags_t flags;
    /// CxUiCloseActions (default CXUI_OnClose_Hide)
    uint32 closeAction;
    /// CxUiThemeMode (default CXUI_Theme_System)
    uint32 theme;
    strref name;
} CxUiWindowDesc;

/// What cxuiWindowPlacement() can read and cxuiWindowSetPlacement() can apply on a platform; see
/// cxuiPlacementCaps()
enum CxUiPlacementCaps {
    /// Where the window is (not on Linux)
    CXUI_PLACE_Position = 0x0001,
    /// How big the window is
    CXUI_PLACE_Size = 0x0002,
    /// Whether the window is minimized or maximized
    CXUI_PLACE_State = 0x0004,
};

/// Where a window is and how big, for saving and restoring between runs
///
/// The rectangle is in the platform's screen units, and only means something on the platform that
/// produced it: on Windows the frame in physical pixels, on Linux the area inside the frame in
/// device-independent pixels, on macOS the frame in points.
typedef struct CxUiPlacement {
    /// The window's rectangle when it is neither minimized nor maximized
    CxUiRect normal;
    /// CxUiWindowStates
    uint32 state;
    /// CxUiPlacementCaps: the parts that hold or are to be applied
    flags_t valid;
} CxUiPlacement;

/// Flags for CxUiMonitor
enum CxUiMonitorFlags {
    /// The system's main monitor
    CXUI_MON_Primary = 0x0001,
    /// workArea leaves out the taskbar or panels; otherwise it is the whole monitor
    CXUI_MON_WorkAreaKnown = 0x0002,
    /// The rectangles say where the monitor is among the others; otherwise only their sizes mean
    /// anything (Wayland)
    CXUI_MON_PositionKnown = 0x0004,
};

/// A monitor, as cxuiMonitors() reports it
///
/// The rectangles are in the platform's screen units, the same as CxUiPlacement's. It holds
/// strings: an array of monitors frees them with the array, and one kept elsewhere is copied with
/// cxuiMonitorCopy() and freed with cxuiMonitorDestroy().
typedef struct CxUiMonitor {
    /// The whole monitor
    CxUiRect bounds;
    /// The part windows normally go in, without the taskbar or dock
    CxUiRect workArea;
    /// Physical pixels per device-independent pixel
    float32 scale;
    /// CxUiMonitorFlags
    flags_t flags;
    /// Names the same monitor from one run to the next, even when monitors are numbered
    /// differently; empty where the platform can't tell
    string id;
    /// The monitor's name to show people, such as its model; empty where unknown
    string name;
    /// How many times a second it shows a new picture; 0 where unknown
    float32 refreshHz;
    /// How far in from each edge of bounds content is safe from being hidden, such as under a
    /// camera housing; all 0 for none
    CxUiEdges safeArea;
} CxUiMonitor;

stDeclare(CxUiMonitor);
saDeclare(CxUiMonitor);
#define SType_CxUiMonitor                         CxUiMonitor*
#define STStorageType_CxUiMonitor                 CxUiMonitor
#define STypeArg_CxUiMonitor(type, val)           stgeneric(opaque, &(val))
#define STypeArgPtr_CxUiMonitor(type, val)        &stgeneric(opaque, (val))
#define STypeCheckedArg_CxUiMonitor(type, val)    stType(type), stArg(type, val)
#define STypeCheckedPtrArg_CxUiMonitor(type, val) stType(type), stArgPtr(type, val)

/// Initial settings for cxuiCanvasCreate(); a field left at 0 takes the default
typedef struct CxUiCanvasDesc {
    /// CxUiDrawBackends (default CXUI_Draw_Auto)
    uint32 draw;
    /// CxUiCanvasUnits (default CXUI_Units_Pixels)
    uint32 units;
    strref name;
} CxUiCanvasDesc;

/// Initial settings for cxuiRenderViewCreate(); a field left at 0 takes the default
typedef struct CxUiRenderViewDesc {
    /// CxUiCanvasUnits for its size and mouse positions (default CXUI_Units_Pixels)
    uint32 units;
    strref name;
} CxUiRenderViewDesc;

/// What a render view's native surface is
typedef enum CxUiSurfaceKind {
    CXUI_SURFACE_None = 0,   ///< None, as on the null backend
    CXUI_SURFACE_Win32,      ///< native is the HWND, connection the HINSTANCE
    CXUI_SURFACE_X11,        ///< native is the Window, connection the Display*
    CXUI_SURFACE_Wayland,    ///< native is the wl_surface*, connection the wl_display*
    CXUI_SURFACE_Metal,      ///< native is the CAMetalLayer*
} CxUiSurfaceKind;

/// A render view's native surface, for a renderer to present to
typedef struct CxUiNativeSurface {
    /// CxUiSurfaceKind
    uint32 kind;
    void* native;
    void* connection;
    /// Its size in physical pixels
    int32 w, h;
} CxUiNativeSurface;

/// What a font asks for; see cxuiFontCreate()
typedef struct CxUiFontDesc {
    /// Typefaces to try in order, separated by semicolons, such as "Segoe UI;Tahoma;ui". Each
    /// can be a generic family: ui (the system's interface font), sans, serif or monospace. Empty
    /// for "ui".
    strref faces;
    /// Size in points, scaled with the display; negative for a size in pixels, which isn't scaled;
    /// 0 for the system's interface font size
    float32 size;
    /// CxUiFontFlags
    uint32 flags;
} CxUiFontDesc;

/// Measurements of a font, in physical pixels
typedef struct CxUiFontMetrics {
    int32 ascent;     ///< From the top of a line to the baseline
    int32 descent;    ///< From the baseline to the bottom of a line
    int32 height;     ///< Distance from one line to the next
    int32 avgCharW;   ///< Average character width
    int32 cellW;      ///< Width of a cell for cxuiDrawTextCells(): the width of '0'
    int32 cellH;      ///< Height of a cell: the line height
} CxUiFontMetrics;

// Internal: the part of a canvas waiting to be painted. Past CXUI_DIRTY_MAX rectangles, they are
// merged into one.
#define CXUI_DIRTY_MAX 8
typedef struct CxUiDirtyRegion {
    CxUiRect r[CXUI_DIRTY_MAX];
    int32 n;
} CxUiDirtyRegion;

/// Flags for CxUiStartDesc
enum CxUiStartFlags {
    /// Run without any operating system UI. Windows and controls exist and behave normally, but
    /// nothing is shown. Also selected by setting the environment variable `CXUI_BACKEND=null`.
    CXUI_Start_Null = 0x0001,

    /// Fall back to running without UI, as with CXUI_Start_Null, when the display can't be
    /// reached, rather than failing.
    CXUI_Start_AllowHeadless = 0x0002,
};

/// Options for cxuiStart()
typedef struct CxUiStartDesc {
    /// Name of the UI thread, as it appears in debuggers and logs (empty for "UI")
    strref name;

    /// CxUiStartFlags
    flags_t flags;

    /// How long the UI thread may spend running queued work before it lets the platform handle
    /// input and painting, in microseconds (0 for the default of 8 ms)
    int64 tickBudget;
} CxUiStartDesc;

/// @}  // end of cxui_shared group

CX_C_END
