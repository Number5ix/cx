// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuigrid.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"
#include "core/cxuigridstore.h"
#include "core/cxuitreestore.h"
#include <cx/utils/murmur.h>

#include <math.h>

// In DIPs
#define GRID_PAD_X       6    // either side of a cell's text
#define GRID_PAD_Y       2    // above and below a row's text
#define GRID_HEAD_PAD_Y  4    // above and below a header's text
#define GRID_MIN_COL     24   // the narrowest a column gets by default
#define GRID_BAR         12   // scroll bar thickness
#define GRID_HSTEP       30   // a wheel notch or arrow key across
#define GRID_NAT_ROWS    8    // rows in the natural height
#define GRID_NAT_MAX_W   480  // the most of the columns' width the natural width takes
#define GRID_EDGE        3    // how near a header's edge the pointer grabs it
#define GRID_ARROW       8    // the sort arrow's width; it is half as tall
#define GRID_INDENT      16   // a tree's indent for each depth, which holds the expander
#define GRID_CHECK       13   // a check box's size
#define GRID_WHEEL_ROWS  3
#define GRID_EDIT_INSET  3    // from the editor's left edge to its text
#define GRID_EDIT_MIN_W  60   // the narrowest the editor gets, over a narrower column

#define GRID_AUTOSCROLL_MS 30
// How long type-ahead waits for the next key before starting again
#define GRID_TYPE_MS 1000
// The most rows type-ahead looks at for one key
#define GRID_TYPE_SCAN 20000
// Texts the grid remembers fitting, in sets of GRID_FIT_WAYS; a power of two
#define GRID_FIT_CACHE 1024
#define GRID_FIT_WAYS  4

// Toggles a row in the selection when held while clicking
#if defined(_PLATFORM_MACOS)
#define MOD_TOGGLE CXUI_MOD_Meta
#else
#define MOD_TOGGLE CXUI_MOD_Ctrl
#endif

static bool live(void* w)
{
    return _cxuiLive((CxUiWidget*)w);
}

static CxUiGrid* gridOf(void* child)
{
    CxUiWidget* parent = ((CxUiWidget*)child)->parent;
    return parent ? objDynCast(CxUiGrid, parent) : NULL;
}

static CxUiColor pal(void* w, uint32 role)
{
    return cxuiPaletteColor(cxuiWidgetPalette((CxUiWidget*)w), role);
}

static int32 px(CxUiGrid* self, int32 dip)
{
    return _cxuiPx(CxUiWidget(self), dip);
}

static CxUiFont* bodyFont(CxUiGrid* self)
{
    return cxuiWidgetFont(self->_body);
}

static CxUiRect bodyRect(CxUiGrid* self)
{
    return CxUiWidget(self->_body)->_bounds;
}

static void repaint(CxUiGrid* self)
{
    cxuiCanvasInvalidateAll(self->_body);
}

static void repaintHeader(CxUiGrid* self)
{
    if (self->_headerH > 0)
        cxuiCanvasInvalidate(self->_body, 0, 0, bodyRect(self).w, self->_headerH);
}

static CxUiResult raiseEvent(CxUiGrid* self, CxUiEvent* ev)
{
    CxUiWidget* target = objAcquire(CxUiWidget(self));
    CxUiResult r       = _cxuiDispatch(target, ev);
    objRelease(&target);
    return r;
}

static void editEnd(CxUiGrid* self, uint32 stage);
static void editClose(CxUiGrid* self);
static void editMoved(CxUiGrid* self);
static void clickCancel(CxUiGrid* self);

// ---- selection ----

// Selected ranges: how many, and the first and last row of range i
#define SEL_N(self)        (saSize((self)->_sel) / 2)
#define SEL_FIRST(self, i) ((self)->_sel.a[2 * (i)])
#define SEL_LAST(self, i)  ((self)->_sel.a[2 * (i) + 1])

// The first range that ends at or after row
static int32 selSearch(CxUiGrid* self, int64 row)
{
    int32 lo = 0, hi = SEL_N(self);
    while (lo < hi) {
        int32 mid = (lo + hi) / 2;
        if (SEL_LAST(self, mid) < row)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static bool selHas(CxUiGrid* self, int64 row)
{
    int32 i = selSearch(self, row);
    return i < SEL_N(self) && SEL_FIRST(self, i) <= row;
}

// Replaces nrem ranges from range i with the nadd ranges in add
static void selSplice(CxUiGrid* self, int32 i, int32 nrem, const int64* add, int32 nadd)
{
    int32 sz = saSize(self->_sel), at = 2 * i, rem = 2 * nrem, ins = 2 * nadd;
    if (ins > rem)
        saSetSize(&self->_sel, sz + ins - rem);
    memmove(&self->_sel.a[at + ins], &self->_sel.a[at + rem], (size_t)(sz - at - rem) * sizeof(int64));
    if (ins > 0)
        memcpy(&self->_sel.a[at], add, (size_t)ins * sizeof(int64));
    if (ins < rem)
        saSetSize(&self->_sel, sz + ins - rem);
}

static void selAdd(CxUiGrid* self, int64 first, int64 last)
{
    // Ranges it overlaps or touches merge with it.
    int32 i = selSearch(self, first - 1), j = i;
    while (j < SEL_N(self) && SEL_FIRST(self, j) <= last + 1) {
        first = min(first, SEL_FIRST(self, j));
        last  = max(last, SEL_LAST(self, j));
        j++;
    }
    selSplice(self, i, j - i, (int64[]){ first, last }, 1);
}

static void selRemove(CxUiGrid* self, int64 first, int64 last)
{
    int32 i = selSearch(self, first), j = i;
    while (j < SEL_N(self) && SEL_FIRST(self, j) <= last)
        j++;
    if (j == i)
        return;
    int64 keep[4];
    int32 nkeep = 0;
    if (SEL_FIRST(self, i) < first) {
        keep[nkeep++] = SEL_FIRST(self, i);
        keep[nkeep++] = first - 1;
    }
    if (SEL_LAST(self, j - 1) > last) {
        keep[nkeep++] = last + 1;
        keep[nkeep++] = SEL_LAST(self, j - 1);
    }
    selSplice(self, i, j - i, keep, nkeep / 2);
}

static void selClear(CxUiGrid* self)
{
    saClear(&self->_sel);
}

static void selOnly(CxUiGrid* self, int64 first, int64 last)
{
    selClear(self);
    selAdd(self, first, last);
}

static int64 selCount(CxUiGrid* self)
{
    int64 n = 0;
    for (int32 i = 0; i < SEL_N(self); i++)
        n += SEL_LAST(self, i) - SEL_FIRST(self, i) + 1;
    return n;
}

static void selInserted(CxUiGrid* self, int64 at, int64 n)
{
    int32 i = selSearch(self, at);
    if (i < SEL_N(self) && SEL_FIRST(self, i) < at) {
        // New rows in the middle of a range split it.
        int64 split[4] = { SEL_FIRST(self, i), at - 1, at, SEL_LAST(self, i) };
        selSplice(self, i, 1, split, 2);
        i++;
    }
    for (int32 k = 2 * i; k < saSize(self->_sel); k++)
        self->_sel.a[k] += n;
}

static void selRemoved(CxUiGrid* self, int64 at, int64 n)
{
    selRemove(self, at, at + n - 1);
    int32 i = selSearch(self, at);
    for (int32 k = 2 * i; k < saSize(self->_sel); k++)
        self->_sel.a[k] -= n;
    // The ranges either side of the gap may now touch.
    if (i > 0 && i < SEL_N(self) && SEL_LAST(self, i - 1) + 1 == SEL_FIRST(self, i)) {
        int64 joined[2] = { SEL_FIRST(self, i - 1), SEL_LAST(self, i) };
        selSplice(self, i - 1, 2, joined, 1);
    }
}

// Ends a change the user made to the selection, which started as was: raises a change event if
// it differs.
static void selDone(CxUiGrid* self, sa_int64* was)
{
    int32 n   = saSize(self->_sel);
    bool same = saSize(*was) == n && (n == 0 || !memcmp(was->a, self->_sel.a, (size_t)n * sizeof(int64)));
    saDestroy(was);
    if (same)
        return;
    repaint(self);
    CxUiChangeEvent ev = { .base.kind = CXUI_EV_Change };
    raiseEvent(self, &ev.base);
}

static int64 rowAfterInsert(int64 row, int64 at, int64 n)
{
    return row >= at ? row + n : row;
}

// Where a row is after others are removed; one removed goes to the row that takes its place
static int64 rowAfterRemove(int64 row, int64 at, int64 n, int64 rows)
{
    if (row < at)
        return row;
    if (row >= at + n)
        return row - n;
    return rows > 0 ? min(at, rows - 1) : -1;
}

// ---- geometry ----

// Row and header heights and padding for the current scale and font
static void metrics(CxUiGrid* self)
{
    CxUiFontMetrics fm;
    cxuiFontMetrics(bodyFont(self), &fm);
    self->_scale   = cxuiWidgetScale(self);
    self->_padX    = px(self, GRID_PAD_X);
    int32 rowDip   = _cxuiLenDip(CxUiWidget(self), self->_rowHeight, 1);
    self->_rowH    = max(rowDip > 0 ? px(self, rowDip) : fm.height + 2 * px(self, GRID_PAD_Y), 1);
    self->_headerH = (self->_flags & CXUI_GRID_NoHeader) ? 0 : fm.height + 2 * px(self, GRID_HEAD_PAD_Y);
}

static int32 minColWidth(CxUiGrid* self, CxUiGridCol* col)
{
    return col->minWidth.v ? px(self, _cxuiLenDip(CxUiWidget(self), col->minWidth, 0)) : px(self, GRID_MIN_COL);
}

static bool hasBox(CxUiGridCol* col)
{
    return col->kind == CXUI_COL_Check || (col->flags & CXUI_COL_CheckBox);
}

static int32 boxSize(CxUiGrid* self)
{
    return max(min(px(self, GRID_CHECK), self->_rowH - 2), 1);
}

// Where a cell's check box goes, with its content starting lead pixels in
static CxUiRect boxRect(CxUiGrid* self, CxUiGridCol* col, CxUiRect cr, int32 lead)
{
    int32 sz = boxSize(self);
    int32 x  = col->kind == CXUI_COL_Check ? cr.x + (cr.w - sz) / 2 : cr.x + lead;
    return (CxUiRect){ x, cr.y + (cr.h - sz) / 2, sz, sz };
}

// The room a header's title needs, with the sort arrow's
static int32 titleWidth(CxUiGrid* self, CxUiFont* f, CxUiGridCol* col)
{
    int32 w = cxuiFontAdvance(f, col->title) + 2 * self->_padX;
    if (col->flags & CXUI_COL_Sortable)
        w += px(self, GRID_ARROW) + self->_padX / 2;
    return w;
}

// A column's width before stretching, in physical pixels; only during layout
static int32 colWidth(CxUiGrid* self, CxUiFont* f, CxUiGridCol* col)
{
    int32 w = col->width.v ? px(self, _cxuiLenDip(CxUiWidget(self), col->width, 0)) : titleWidth(self, f, col);
    if (!col->width.v && col->kind == CXUI_COL_Check)
        w = max(w, boxSize(self) + 2 * self->_padX);
    return max(w, minColWidth(self, col));
}

// Lays the columns out across avail pixels
static void layoutColumns(CxUiGrid* self, int32 avail)
{
    CxUiFont* f   = bodyFont(self);
    int32 total   = 0;
    int32 stretch = -1;
    for (int32 i = 0; i < self->_ncols; i++) {
        CxUiGridCol* col = &self->_cols[i];
        col->w           = (col->flags & CXUI_COL_Hidden) ? 0 : colWidth(self, f, col);
        if (stretch < 0 && col->w > 0 && (col->flags & CXUI_COL_Stretch))
            stretch = i;
        total += col->w;
    }
    if (stretch >= 0 && total < avail) {
        self->_cols[stretch].w += avail - total;
        total = avail;
    }
    int32 x = 0;
    for (int32 i = 0; i < self->_ncols; i++) {
        self->_cols[i].x = x;
        x += self->_cols[i].w;
    }
    self->_totalW = total;
}

// Which scroll bars a grid of w by h pixels needs
static void barsWanted(CxUiGrid* self, int32 w, int32 h, bool* v, bool* hz)
{
    int32 bar = px(self, GRID_BAR);
    *v = *hz = false;
    // Each bar takes room from the other's axis, so it settles within three rounds.
    for (int i = 0; i < 3; i++) {
        layoutColumns(self, w - (*v ? bar : 0));
        *hz          = self->_totalW > w - (*v ? bar : 0);
        int64 bodyH  = (int64)h - (*hz ? bar : 0) - self->_headerH;
        bool needV   = self->_rows * self->_rowH > bodyH;
        if (needV == *v)
            break;
        *v = needV;
    }
}

// Puts the scroll bars and the scroll position in line with the body's size and the rows
static void sync(CxUiGrid* self)
{
    CxUiRect b = bodyRect(self);
    self->_page = self->_rowH > 0 ? max(b.h - self->_headerH, 0) / self->_rowH : 0;
    self->_top  = clamp(self->_top, 0, max(self->_rows - self->_page, 0));
    self->_scrollX = clamp(self->_scrollX, 0, max(self->_totalW - b.w, 0));
    cxuiDScrollBarSetRange(self->_vbar, self->_rows, self->_page);
    cxuiDScrollBarSetPos(self->_vbar, self->_top);
    cxuiDScrollBarSetStep(self->_vbar, GRID_WHEEL_ROWS);
    cxuiDScrollBarSetRange(self->_hbar, self->_totalW, b.w);
    cxuiDScrollBarSetPos(self->_hbar, self->_scrollX);
    cxuiDScrollBarSetStep(self->_hbar, px(self, GRID_HSTEP));
}

// The rows or the columns' widths changed: lays out again if a scroll bar comes or goes
static void refit(CxUiGrid* self)
{
    if (CxUiWidget(self)->state & CXUI_WS_Placed) {
        CxUiRect gb = CxUiWidget(self)->_bounds;
        bool v, h;
        barsWanted(self, gb.w, gb.h, &v, &h);
        layoutColumns(self, bodyRect(self).w);
        if (v != self->_barV || h != self->_barH)
            _cxuiLayoutDirty(CxUiWidget(self));
    }
    sync(self);
}

// Paints rows first to last again, as far as they are shown
static void repaintRows(CxUiGrid* self, int64 first, int64 last)
{
    first = max(first, self->_top);
    last  = min(last, self->_top + self->_page);
    if (first > last)
        return;
    CxUiRect b = bodyRect(self);
    int32 y    = self->_headerH + (int32)(first - self->_top) * self->_rowH;
    cxuiCanvasInvalidate(self->_body, 0, y, b.w, (int32)(last - first + 1) * self->_rowH);
}

// The row at y in the body, or -1 for none: the headers, past the last row, or outside
static int64 rowAtY(CxUiGrid* self, int32 y)
{
    CxUiRect b = bodyRect(self);
    if (y < self->_headerH || y >= b.h || self->_rowH <= 0)
        return -1;
    int64 row = self->_top + (y - self->_headerH) / self->_rowH;
    return row < self->_rows ? row : -1;
}

// The column at x in the body, or -1 for none
static int32 colAtX(CxUiGrid* self, int32 x)
{
    int32 at = x + self->_scrollX;
    for (int32 i = 0; i < self->_ncols; i++) {
        CxUiGridCol* col = &self->_cols[i];
        if (col->w > 0 && at >= col->x && at < col->x + col->w)
            return i;
    }
    return -1;
}

// The column whose header's right edge is near x, that the user may drag; -1 for none
static int32 edgeAtX(CxUiGrid* self, int32 x)
{
    int32 grab = px(self, GRID_EDGE), best = -1, bestDist = 0;
    for (int32 i = 0; i < self->_ncols; i++) {
        CxUiGridCol* col = &self->_cols[i];
        if (col->w <= 0 || (col->flags & CXUI_COL_FixedWidth))
            continue;
        int32 dist = abs(col->x + col->w - self->_scrollX - x);
        // Ties go to the later column, so a narrow one can still be widened.
        if (dist <= grab && (best < 0 || dist <= bestDist)) {
            best     = i;
            bestDist = dist;
        }
    }
    return best;
}

static void setHot(CxUiGrid* self, int64 row)
{
    if (row == self->_hot)
        return;
    if (self->_hot >= 0)
        repaintRows(self, self->_hot, self->_hot);
    if (row >= 0)
        repaintRows(self, row, row);
    self->_hot = row;
}

// The row under the pointer is the hot one, after a move or scroll
static void hover(CxUiGrid* self)
{
    bool over = self->_mouseIn && self->_mouseX >= 0 && self->_mouseX < bodyRect(self).w;
    setHot(self, over ? rowAtY(self, self->_mouseY) : -1);
}

static void scrollTo(CxUiGrid* self, int64 top, int32 x)
{
    CxUiRect b = bodyRect(self);
    top        = clamp(top, 0, max(self->_rows - self->_page, 0));
    x          = clamp(x, 0, max(self->_totalW - b.w, 0));
    if (top == self->_top && x == self->_scrollX)
        return;
    clickCancel(self);
    // Scrolling keeps what was typed in a cell's editor.
    if (self->_editFrame) {
        editEnd(self, CXUI_EDIT_Commit);
        if (!live(self))
            return;
        top = clamp(top, 0, max(self->_rows - self->_page, 0));
    }
    self->_top     = top;
    self->_scrollX = x;
    cxuiDScrollBarSetPos(self->_vbar, top);
    cxuiDScrollBarSetPos(self->_hbar, x);
    repaint(self);
    hover(self);
}

static void showRow(CxUiGrid* self, int64 row)
{
    if (row < 0 || row >= self->_rows)
        return;
    if (row < self->_top || self->_page <= 0)
        scrollTo(self, row, self->_scrollX);
    else if (row >= self->_top + self->_page)
        scrollTo(self, row - self->_page + 1, self->_scrollX);
}

static CxUiResult barScrolled(stvlist* cvars, CxUiScrollEvent* ev)
{
    unused_noeval(cvars);
    CxUiWidget* bar = ev->base.current;
    CxUiGrid* self  = gridOf(bar);
    if (!self)
        return CXUI_Default;
    if (bar == CxUiWidget(self->_vbar))
        scrollTo(self, ev->pos, self->_scrollX);
    else
        scrollTo(self, self->_top, (int32)ev->pos);
    return CXUI_Handled;
}

// Rows were added: the selection, the focus row and the rows shown stay on the same rows
static void addRows(CxUiGrid* self, int64 at, int64 n)
{
    at = clamp(at, 0, self->_rows);
    self->_rows += n;
    if (at < self->_top)
        self->_top += n;
    selInserted(self, at, n);
    self->_focus  = rowAfterInsert(self->_focus, at, n);
    self->_anchor = rowAfterInsert(self->_anchor, at, n);
    self->_hot    = -1;
    if (self->_editFrame && !self->_tree)
        self->_editItem = (uint64)rowAfterInsert((int64)self->_editItem, at, n);
    clickCancel(self);
    refit(self);
    hover(self);
    repaintRows(self, at, self->_rows - 1);
    editMoved(self);
}

static void removeRows(CxUiGrid* self, int64 at, int64 n)
{
    n = min(n, self->_rows - at);
    self->_rows -= n;
    if (at + n <= self->_top)
        self->_top -= n;
    else if (at < self->_top)
        self->_top = at;
    selRemoved(self, at, n);
    self->_focus  = rowAfterRemove(self->_focus, at, n, self->_rows);
    self->_anchor = rowAfterRemove(self->_anchor, at, n, self->_rows);
    self->_hot    = -1;
    if (self->_editFrame && !self->_tree) {
        int64 r = (int64)self->_editItem;
        if (r >= at && r < at + n)
            editClose(self);
        else
            self->_editItem = (uint64)rowAfterRemove(r, at, n, self->_rows);
    }
    clickCancel(self);
    refit(self);
    hover(self);
    // Rows that move up from past the bottom are painted too.
    repaintRows(self, at, self->_rows + n - 1);
    editMoved(self);
}

// ---- the tree ----

// A node the grid has loaded the children of: one that is or was open, the nodes above it, and
// the top of the tree. Rows are counted as if it were open.
typedef struct TreeRec {
    CxUiNodeId id;
    struct TreeRec* up;   // the parent's record; NULL for the top
    bool expanded;        // always for the top
    bool kept;            // while its parent's children are loaded again
    int64 rows;           // the rows its children take while it is open
    sa_uint64 kids;
    sa_ptr kidRecs;       // each child's record, or NULL
    int32 nrecs;          // how many children have records
    // The rows before each child, and all of them last; only while some child has a record
    sa_int64 cum;
    bool cumDirty;
} TreeRec;

struct CxUiGridTree {
    TreeRec* root;
    hashtable recs;   // CxUiNodeId to TreeRec*, the top excluded
    // The walk through the rows: the record and the child at each depth
    sa_ptr walkRec;
    sa_int32 walkIdx;
};

static TreeRec* recFind(CxUiGrid* self, CxUiNodeId id)
{
    if (id == 0)
        return self->_tree->root;
    TreeRec* rec = NULL;
    htFind(self->_tree->recs, uint64, id, ptr, (void**)&rec);
    return rec;
}

static TreeRec* recNew(CxUiGrid* self, CxUiNodeId id, TreeRec* up)
{
    TreeRec* rec = xaAllocStruct(TreeRec, XA_Zero);
    rec->id      = id;
    rec->up      = up;
    saInit(&rec->kids, uint64, 4);
    saInit(&rec->kidRecs, ptr, 4);
    saInit(&rec->cum, int64, 1);
    if (id)
        htInsert(&self->_tree->recs, uint64, id, ptr, rec);
    return rec;
}

static void recFree(CxUiGrid* self, TreeRec* rec)
{
    for (int32 i = 0; i < saSize(rec->kidRecs); i++) {
        if (rec->kidRecs.a[i])
            recFree(self, rec->kidRecs.a[i]);
    }
    if (rec->id)
        htRemove(&self->_tree->recs, uint64, rec->id);
    saDestroy(&rec->kids);
    saDestroy(&rec->kidRecs);
    saDestroy(&rec->cum);
    xaFree(rec);
}

static int64 kidSize(TreeRec* rec, int32 i)
{
    TreeRec* kr = rec->kidRecs.a[i];
    return 1 + (kr && kr->expanded ? kr->rows : 0);
}

static void recount(TreeRec* rec)
{
    rec->rows = 0;
    for (int32 i = 0; i < saSize(rec->kids); i++)
        rec->rows += kidSize(rec, i);
    rec->cumDirty = true;
}

static int32 kidIndex(TreeRec* rec, CxUiNodeId id)
{
    for (int32 i = 0; i < saSize(rec->kids); i++) {
        if (rec->kids.a[i] == id)
            return i;
    }
    return -1;
}

// Reads a node's children from the source, keeping the records of those still there
static void loadKids(CxUiGrid* self, TreeRec* rec)
{
    for (int32 i = 0; i < saSize(rec->kidRecs); i++) {
        if (rec->kidRecs.a[i])
            ((TreeRec*)rec->kidRecs.a[i])->kept = false;
    }
    sa_uint64 kids;
    sa_ptr recs;
    saInit(&kids, uint64, 4);
    saInit(&recs, ptr, 4);
    rec->nrecs = 0;
    for (CxUiNodeId c = self->_treeSrc->firstChild(self->_source, rec->id); c;
         c = self->_treeSrc->nextSibling(self->_source, c)) {
        TreeRec* kr = recFind(self, c);
        if (kr && kr->up == rec && !kr->kept) {
            kr->kept = true;
            rec->nrecs++;
        } else {
            kr = NULL;
        }
        saPush(&kids, uint64, c);
        saPush(&recs, ptr, kr);
    }
    for (int32 i = 0; i < saSize(rec->kidRecs); i++) {
        TreeRec* old = rec->kidRecs.a[i];
        if (old && !old->kept)
            recFree(self, old);
    }
    saDestroy(&rec->kids);
    saDestroy(&rec->kidRecs);
    rec->kids    = kids;
    rec->kidRecs = recs;
    recount(rec);
}

// The child holding row r of a record's rows, and the row it starts at
static int32 kidAt(TreeRec* rec, int64 r, int64* start)
{
    int32 n = saSize(rec->kids);
    if (rec->nrecs == 0) {
        *start = r;
        return (int32)r;
    }
    if (rec->cumDirty) {
        saSetSize(&rec->cum, n + 1);
        int64 at = 0;
        for (int32 i = 0; i < n; i++) {
            rec->cum.a[i] = at;
            at += kidSize(rec, i);
        }
        rec->cum.a[n]  = at;
        rec->cumDirty = false;
    }
    int32 lo = 0, hi = n - 1;
    while (lo < hi) {
        int32 mid = (lo + hi + 1) / 2;
        if (rec->cum.a[mid] <= r)
            lo = mid;
        else
            hi = mid - 1;
    }
    *start = rec->cum.a[lo];
    return lo;
}

static int64 kidStart(TreeRec* rec, int32 i)
{
    if (rec->nrecs == 0)
        return i;
    int64 start;
    kidAt(rec, 0, &start);   // builds the sums
    return rec->cum.a[i];
}

// The children of rec take delta more rows: counts them up the tree as far as the nodes are
// open. True if that reached the top, so the grid's rows changed.
static bool propagate(TreeRec* rec, int64 delta)
{
    for (;;) {
        rec->rows += delta;
        rec->cumDirty = true;
        if (!rec->up)
            return true;
        if (!rec->expanded)
            return false;
        rec = rec->up;
    }
}

// The row of a child of rec, or -1 if it isn't shown; depth receives its depth
static int64 rowIn(CxUiGrid* self, TreeRec* rec, CxUiNodeId node, int32* depth)
{
    int64 row = 0;
    int32 d   = 0;
    for (;;) {
        if (rec->up && !rec->expanded)
            return -1;
        int32 i = kidIndex(rec, node);
        if (i < 0)
            return -1;
        row += kidStart(rec, i);
        if (!rec->up)
            break;
        row++;
        d++;
        node = rec->id;
        rec  = rec->up;
    }
    if (depth)
        *depth = d;
    return row;
}

static int64 treeRowOf(CxUiGrid* self, CxUiNodeId node, int32* depth)
{
    if (!self->_tree || node == 0)
        return -1;
    TreeRec* rec = recFind(self, self->_treeSrc->parentOf(self->_source, node));
    return rec ? rowIn(self, rec, node, depth) : -1;
}

// ---- walking the rows ----

static void walkPush(CxUiGrid* self, TreeRec* rec, int32 i)
{
    saPush(&self->_tree->walkRec, ptr, rec);
    saPush(&self->_tree->walkIdx, int32, i);
}

// Starts the walk at a row, which must exist
static void walkSeek(CxUiGrid* self, int64 row)
{
    CxUiGridTree* t = self->_tree;
    saClear(&t->walkRec);
    saClear(&t->walkIdx);
    TreeRec* rec = t->root;
    for (;;) {
        int64 start;
        int32 i = kidAt(rec, row, &start);
        walkPush(self, rec, i);
        if (row == start)
            return;
        row = row - start - 1;
        rec = rec->kidRecs.a[i];
    }
}

static int32 walkDepth(CxUiGrid* self)
{
    return saSize(self->_tree->walkIdx) - 1;
}

static TreeRec* walkRecAt(CxUiGrid* self, int32 level)
{
    return self->_tree->walkRec.a[level];
}

static CxUiNodeId walkNode(CxUiGrid* self)
{
    int32 d = walkDepth(self);
    return walkRecAt(self, d)->kids.a[self->_tree->walkIdx.a[d]];
}

// The record of the node the walk is at, or NULL
static TreeRec* walkNodeRec(CxUiGrid* self)
{
    int32 d = walkDepth(self);
    return walkRecAt(self, d)->kidRecs.a[self->_tree->walkIdx.a[d]];
}

// Whether the node the walk passes through at a depth has a sibling after it
static bool walkHasNext(CxUiGrid* self, int32 level)
{
    return self->_tree->walkIdx.a[level] + 1 < saSize(walkRecAt(self, level)->kids);
}

static bool walkNext(CxUiGrid* self)
{
    CxUiGridTree* t = self->_tree;
    TreeRec* kr     = walkNodeRec(self);
    if (kr && kr->expanded && saSize(kr->kids) > 0) {
        walkPush(self, kr, 0);
        return true;
    }
    for (int32 d = walkDepth(self); d >= 0; d--) {
        if (walkHasNext(self, d)) {
            t->walkIdx.a[d]++;
            saSetSize(&t->walkRec, d + 1);
            saSetSize(&t->walkIdx, d + 1);
            return true;
        }
    }
    return false;
}

// ---- opening and closing ----

// Makes records for a node and those above it; NULL if it isn't in the tree
static TreeRec* recEnsure(CxUiGrid* self, CxUiNodeId node)
{
    TreeRec* rec = recFind(self, node);
    if (rec)
        return rec;
    TreeRec* up = recEnsure(self, self->_treeSrc->parentOf(self->_source, node));
    int32 i     = up ? kidIndex(up, node) : -1;
    if (i < 0)
        return NULL;
    rec                 = recNew(self, node, up);
    up->kidRecs.a[i]    = rec;
    up->nrecs++;
    up->cumDirty = true;
    loadKids(self, rec);
    return rec;
}

static void expandNode(CxUiGrid* self, CxUiNodeId node, bool expand);

// Opens the nodes above a node, from the top down
static void openAbove(CxUiGrid* self, CxUiNodeId node)
{
    CxUiNodeId p = self->_treeSrc->parentOf(self->_source, node);
    if (!p)
        return;
    openAbove(self, p);
    expandNode(self, p, true);
}

static void expandNode(CxUiGrid* self, CxUiNodeId node, bool expand)
{
    if (!self->_tree || node == 0)
        return;
    if (expand)
        openAbove(self, node);
    TreeRec* rec = expand ? recEnsure(self, node) : recFind(self, node);
    if (!rec || rec->expanded == expand)
        return;
    // Children added while it was closed are found now.
    if (expand)
        loadKids(self, rec);
    int64 row = rowIn(self, rec->up, node, NULL);
    if (!expand && row >= 0) {
        // The focus row inside goes to the node.
        if (self->_focus > row && self->_focus <= row + rec->rows)
            self->_focus = row;
        if (self->_anchor > row && self->_anchor <= row + rec->rows)
            self->_anchor = row;
    }
    rec->expanded = expand;
    int64 n       = rec->rows;
    if (n > 0 && propagate(rec->up, expand ? n : -n)) {
        if (expand)
            addRows(self, row + 1, n);
        else
            removeRows(self, row + 1, n);
    }
    if (row >= 0)
        repaintRows(self, row, row);
}

// The user opens or closes a node, which the program hears of first
static void userExpand(CxUiGrid* self, CxUiNodeId node, bool expand)
{
    if (expand && !self->_treeSrc->hasChildren(self->_source, node))
        return;
    CxUiExpandEvent ev = { .base.kind = CXUI_EV_Expand, .item = node, .expanded = expand };
    raiseEvent(self, &ev.base);
    if (live(self))
        expandNode(self, node, expand);
}

// Opens or closes every node under rec, without moving rows; counts its rows again
static void setBelow(CxUiGrid* self, TreeRec* rec, bool expand)
{
    for (int32 i = 0; i < saSize(rec->kids); i++) {
        TreeRec* kr = rec->kidRecs.a[i];
        if (!expand) {
            if (kr)
                recFree(self, kr);
            rec->kidRecs.a[i] = NULL;
            continue;
        }
        if (!kr && self->_treeSrc->hasChildren(self->_source, rec->kids.a[i])) {
            kr                 = recNew(self, rec->kids.a[i], rec);
            rec->kidRecs.a[i] = kr;
            loadKids(self, kr);
        }
        if (kr) {
            kr->expanded = true;
            setBelow(self, kr, true);
        }
    }
    rec->nrecs = 0;
    for (int32 i = 0; i < saSize(rec->kidRecs); i++)
        rec->nrecs += rec->kidRecs.a[i] != NULL;
    recount(rec);
}

// Opens or closes a whole subtree at once, then finds the focus and selected nodes again
static void setAll(CxUiGrid* self, CxUiNodeId node, bool expand)
{
    if (!self->_tree)
        return;
    if (node && expand)
        openAbove(self, node);
    TreeRec* rec = node ? (expand ? recEnsure(self, node) : recFind(self, node)) : self->_tree->root;
    if (!rec)
        return;

    CxUiNodeId focus  = CxUiGrid_focusNode(self);
    CxUiNodeId anchor = self->_anchor >= 0 ? CxUiGrid_nodeAt(self, self->_anchor) : 0;
    sa_uint64 sel;
    saInit(&sel, uint64, 16);
    for (int64 r = CxUiGrid_nextSelected(self, -1); r >= 0; r = CxUiGrid_nextSelected(self, r))
        saPush(&sel, uint64, CxUiGrid_nodeAt(self, r));

    int64 before = rec->up ? (rec->expanded ? rec->rows : 0) : 0;
    setBelow(self, rec, expand);
    if (rec->up) {
        rec->expanded = expand;
        int64 after   = expand ? rec->rows : 0;
        if (after != before)
            propagate(rec->up, after - before);
    }

    self->_rows = self->_tree->root->rows;
    selClear(self);
    // A focus node now hidden goes to the nearest node above it still shown.
    int64 frow = -1;
    for (CxUiNodeId n = focus; n && frow < 0; n = self->_treeSrc->parentOf(self->_source, n))
        frow = treeRowOf(self, n, NULL);
    self->_focus  = frow;
    self->_anchor = treeRowOf(self, anchor, NULL);
    for (int32 i = 0; i < saSize(sel); i++) {
        int64 r = treeRowOf(self, sel.a[i], NULL);
        if (r >= 0)
            selAdd(self, r, r);
    }
    saDestroy(&sel);
    self->_hot = -1;
    clickCancel(self);
    refit(self);
    hover(self);
    repaint(self);
    editMoved(self);
}

static void treeCreate(CxUiGrid* self)
{
    CxUiGridTree* t = xaAllocStruct(CxUiGridTree, XA_Zero);
    self->_tree     = t;
    htInit(&t->recs, uint64, ptr, 16);
    saInit(&t->walkRec, ptr, 8);
    saInit(&t->walkIdx, int32, 8);
    t->root           = recNew(self, 0, NULL);
    t->root->expanded = true;
}

static void treeDestroy(CxUiGrid* self)
{
    CxUiGridTree* t = self->_tree;
    if (!t)
        return;
    recFree(self, t->root);
    htDestroy(&t->recs);
    saDestroy(&t->walkRec);
    saDestroy(&t->walkIdx);
    xaDestroy(&self->_tree);
}

// Reads every loaded node's children again, keeping open the nodes still there
static void reloadTree(CxUiGrid* self, TreeRec* rec)
{
    loadKids(self, rec);
    for (int32 i = 0; i < saSize(rec->kidRecs); i++) {
        if (rec->kidRecs.a[i])
            reloadTree(self, rec->kidRecs.a[i]);
    }
    recount(rec);
}

// The column a tree's nodes are drawn in, with their expanders
static int32 treeColumn(CxUiGrid* self)
{
    int32 first = -1;
    for (int32 i = 0; i < self->_ncols; i++) {
        if (self->_cols[i].flags & CXUI_COL_Hidden)
            continue;
        if (self->_cols[i].kind == CXUI_COL_Tree)
            return i;
        if (first < 0)
            first = i;
    }
    return first;
}

// Where a tree cell's text starts, from the cell's left edge
static int32 treeLead(CxUiGrid* self, int32 depth)
{
    return self->_padX / 2 + (depth + 1) * px(self, GRID_INDENT);
}

// The item a row shows: the row in a flat grid, the node in a tree
static uint64 itemAt(CxUiGrid* self, int64 row)
{
    return self->_tree ? CxUiGrid_nodeAt(self, row) : (uint64)row;
}

// ---- cells ----

// Asks the source about an item's cell, into _cell
static bool fetchCell(CxUiGrid* self, uint64 item, int32 col)
{
    CxUiCell* cell = &self->_cell;
    strDestroy(&cell->text);
    cell->fg = cell->bg = (CxUiColor){ 0 };
    cell->image = NULL;
    cell->flags = 0;
    cell->check = CXUI_CHECK_Off;
    if (self->_treeSrc)
        return item && self->_treeSrc->nodeCell(self->_source, cell, item, col);
    return self->_flat && self->_flat->cell(self->_source, cell, (int64)item, col);
}

static const char ellipsis[] = "\xE2\x80\xA6";

// How many bytes of text fit in width pixels with an ellipsis after them, all of them if the text
// fits without one; adv receives the width of what is shown
static int32 fitLength(string* buf, CxUiFont* f, strref text, int32 width, int32* adv)
{
    *adv = cxuiFontAdvance(f, text);
    if (*adv <= width)
        return strLen(text);
    // By halving: lo bytes fit with the ellipsis and hi don't, both at character starts.
    int32 lo = 0, hi = strLen(text);
    while (hi - lo > 1) {
        int32 mid = (lo + hi) / 2;
        while (mid > lo && (strGetChar(text, mid) & 0xC0) == 0x80)
            mid--;
        if (mid == lo) {
            mid = lo + 1;
            while (mid < hi && (strGetChar(text, mid) & 0xC0) == 0x80)
                mid++;
            if (mid >= hi)
                break;
        }
        strSubStr(buf, text, 0, mid);
        strAppend(buf, (strref)ellipsis);
        if (cxuiFontAdvance(f, *buf) <= width)
            lo = mid;
        else
            hi = mid;
    }
    strSubStr(buf, text, 0, lo);
    strAppend(buf, (strref)ellipsis);
    *adv = cxuiFontAdvance(f, *buf);
    return lo;
}

// text, or the longest start of it that fits in width pixels with an ellipsis after it, and its
// width in adv. Each answer is remembered by the text's hash, so a repaint measures nothing it has
// measured before.
static strref fitText(CxUiGrid* self, CxUiFont* f, strref text, int32 width, int32* adv)
{
    if (!self->_fit)
        self->_fit = xaAlloc(GRID_FIT_CACHE * sizeof(CxUiGridFit), XA_Zero);
    int32 len        = strLen(text);
    uint32 hash      = hashMurmur3Str(text);
    CxUiGridFit* set = &self->_fit[((hash ^ (uint32)width * 2654435761u) * GRID_FIT_WAYS) & (GRID_FIT_CACHE - 1)];
    CxUiGridFit* e   = NULL;
    for (int32 i = 0; i < GRID_FIT_WAYS && !e; i++) {
        if (set[i].font == f && set[i].hash == hash && set[i].len == len && set[i].width == width)
            e = &set[i];
    }
    if (!e) {
        // The newest goes first, and the oldest drops off the end.
        memmove(&set[1], &set[0], (GRID_FIT_WAYS - 1) * sizeof(CxUiGridFit));
        e       = &set[0];
        *e      = (CxUiGridFit){ .font = f, .hash = hash, .len = len, .width = width };
        e->keep = fitLength(&self->_shown, f, text, width, &e->adv);
    }
    *adv = e->adv;
    if (e->keep >= len)
        return text;
    strSubStr(&self->_shown, text, 0, e->keep);
    strAppend(&self->_shown, (strref)ellipsis);
    return self->_shown;
}

// Fonts are about to change, and a new one could have an old one's address.
static void forgetFonts(CxUiGrid* self)
{
    objRelease(&self->_bold);
    if (self->_fit)
        memset(self->_fit, 0, GRID_FIT_CACHE * sizeof(CxUiGridFit));
}

// Draws text in a box, fitted to it, aligned across as a column says
static void paintText(CxUiGrid* self, CxUiDraw* d, CxUiFont* f, CxUiRect box, strref text, uint8 align, CxUiColor c)
{
    int32 adv;
    strref shown = fitText(self, f, text, box.w, &adv);
    // Placed here from the remembered width, so the drawing doesn't measure it again
    int32 x = box.x;
    if (align == CXUI_ALIGN_End)
        x = box.x + box.w - adv;
    else if (align == CXUI_ALIGN_Center)
        x = box.x + (box.w - adv) / 2;
    x = max(x, box.x);
    // Fitted text needs no clipping, unless not even the ellipsis fits.
    if (adv > box.w) {
        cxuiDrawTextBox(d, f, box, shown, CXUI_TEXT_Left | CXUI_TEXT_Middle, c);
        return;
    }
    CxUiFontMetrics fm;
    cxuiFontMetrics(f, &fm);
    cxuiDrawText(d, f, x, box.y + (box.h - fm.height) / 2, shown, c);
}

static CxUiFont* boldFont(CxUiGrid* self, CxUiFont* f)
{
    if (!self->_bold)
        self->_bold = cxuiFontCreateFor(self->_body, &(CxUiFontDesc){ .faces = f->_faces, .size = f->_size,
                                                                       .flags = f->_flags | CXUI_FONT_Bold });
    return self->_bold;
}

static CxUiFont* cellFont(CxUiGrid* self, CxUiFont* f)
{
    return (self->_cell.flags & CXUI_CELL_Bold) ? boldFont(self, f) : f;
}

// The width an image in _cell takes before the text, with the gap after it
static int32 imageWidth(CxUiGrid* self, CxUiFont* f, int32 rowH)
{
    CxUiImage* img = self->_cell.image;
    if (!img || img->_rect.h <= 0)
        return 0;
    CxUiFontMetrics fm;
    cxuiFontMetrics(f, &fm);
    int32 ih = min(fm.height, rowH);
    return (int32)((int64)img->_rect.w * ih / img->_rect.h) + self->_padX / 2;
}

// ---- painting ----

static void paintCheck(CxUiGrid* self, CxUiDraw* d, CxUiRect b, uint32 state)
{
    CxUiColor accent = pal(self, cxuiWidgetEffectiveEnabled(self) ? CXUI_PAL_Accent : CXUI_PAL_TextDisabled);
    if (state == CXUI_CHECK_On) {
        cxuiDrawFill(d, b.x, b.y, b.w, b.h, accent);
        int32 lw       = max(px(self, 2), 1);
        CxUiColor mark = pal(self, CXUI_PAL_Content);
        int32 mx = b.x + b.w * 42 / 100, my = b.y + b.h * 72 / 100;
        cxuiDrawLine(d, b.x + b.w * 22 / 100, b.y + b.h * 50 / 100, mx, my, lw, mark);
        cxuiDrawLine(d, mx, my, b.x + b.w * 78 / 100, b.y + b.h * 28 / 100, lw, mark);
        return;
    }
    cxuiDrawFill(d, b.x, b.y, b.w, b.h, pal(self, CXUI_PAL_Input));
    cxuiDrawFrame(d, b.x, b.y, b.w, b.h, 1, pal(self, CXUI_PAL_InputBorder));
    if (state == CXUI_CHECK_Mixed) {
        int32 in = b.w / 4;
        cxuiDrawFill(d, b.x + in, b.y + in, b.w - 2 * in, b.h - 2 * in, accent);
    }
}

// A selected row is drawn in the selection's colors, over the cell's own.
// The cell's text starts lead pixels in.
static void paintCell(CxUiGrid* self, CxUiDraw* d, CxUiFont* f, uint64 item, int32 c, CxUiRect cr, int32 lead,
                      CxUiColor text, bool selected)
{
    if (!fetchCell(self, item, c))
        return;
    CxUiCell* cell = &self->_cell;

    if (cell->bg.a && !selected)
        cxuiDrawFill(d, cr.x, cr.y, cr.w, cr.h, cell->bg);
    CxUiColor fg = selected      ? text
                   : cell->fg.a ? cell->fg
                   : (cell->flags & CXUI_CELL_Dim) ? pal(self, CXUI_PAL_TextDim)
                                                   : text;
    CxUiFont* cf = cellFont(self, f);

    // What comes before the text is placed inside the cell, and the text is fitted to the rest,
    // so only a column too narrow for the box, image or indent needs clipping; most cells are
    // drawn without, which keeps a paint's drawing calls in long batches.
    CxUiGridCol* col = &self->_cols[c];
    int32 iw         = imageWidth(self, f, cr.h);
    int32 fixed      = col->kind == CXUI_COL_Check ? boxSize(self) : lead + (hasBox(col) ? boxSize(self) + self->_padX / 2 : 0) + iw;
    bool clip        = fixed > cr.w;
    if (clip)
        cxuiDrawClipPush(d, cr);
    if (hasBox(col)) {
        // A hidden box keeps its room, so the text lines up with the rows that have one.
        if (!(cell->flags & CXUI_CELL_CheckHidden))
            paintCheck(self, d, boxRect(self, col, cr, lead), cell->check);
        if (col->kind == CXUI_COL_Check) {
            if (clip)
                cxuiDrawClipPop(d);
            return;
        }
        lead += boxSize(self) + self->_padX / 2;
    }
    CxUiRect box = { cr.x + lead, cr.y, cr.w - lead - self->_padX, cr.h };
    if (iw > 0) {
        CxUiFontMetrics fm;
        cxuiFontMetrics(f, &fm);
        int32 ih = min(fm.height, cr.h);
        cxuiDrawImage(d, cell->image, NULL, (CxUiRect){ box.x, cr.y + (cr.h - ih) / 2, iw - self->_padX / 2, ih });
        box.x += iw;
        box.w -= iw;
    }
    if (box.w > 0 && strLen(cell->text) > 0)
        paintText(self, d, cf, box, cell->text, self->_cols[c].align, fg);
    if (clip)
        cxuiDrawClipPop(d);
}

// A triangle pointing up, or down
static void paintArrow(CxUiDraw* d, int32 x, int32 y, int32 w, bool down, CxUiColor c)
{
    int32 h = max(w / 2, 1);
    for (int32 i = 0; i < h; i++) {
        int32 lw = w * (i + 1) / h;
        cxuiDrawFill(d, x + (w - lw) / 2, down ? y + h - 1 - i : y + i, lw, 1, c);
    }
}

// A triangle pointing right, w tall
static void paintArrowRight(CxUiDraw* d, int32 x, int32 y, int32 w, CxUiColor c)
{
    int32 h = max(w / 2, 1);
    for (int32 i = 0; i < h; i++) {
        int32 lh = w * (h - i) / h;
        cxuiDrawFill(d, x + i, y + (w - lh) / 2, 1, lh, c);
    }
}

// A tree node's guide lines and expander, in its cell
static void paintTreeLead(CxUiGrid* self, CxUiDraw* d, CxUiRect cr, int32 depth, CxUiNodeId node, CxUiColor text)
{
    int32 ind  = px(self, GRID_INDENT);
    int32 aw   = px(self, GRID_ARROW);
    int32 x0   = cr.x + self->_padX / 2;
    int32 mid  = cr.y + cr.h / 2;
    bool kids  = self->_treeSrc->hasChildren(self->_source, node);
    if (self->_flags & CXUI_GRID_TreeLines) {
        CxUiColor line = pal(self, CXUI_PAL_GridLine);
        for (int32 l = 0; l < depth; l++) {
            if (walkHasNext(self, l))
                cxuiDrawFill(d, x0 + l * ind + ind / 2, cr.y, 1, cr.h, line);
        }
        // The node's own line stops short of its expander.
        int32 lx  = x0 + depth * ind + ind / 2;
        int32 gap = kids ? aw / 2 + 2 : 0;
        if (depth > 0 || self->_tree->walkIdx.a[0] > 0)
            cxuiDrawFill(d, lx, cr.y, 1, mid - gap - cr.y, line);
        if (walkHasNext(self, depth))
            cxuiDrawFill(d, lx, mid + gap, 1, cr.y + cr.h - mid - gap, line);
        cxuiDrawFill(d, lx + gap, mid, ind - ind / 2 - gap, 1, line);
    }
    if (kids) {
        TreeRec* kr = walkNodeRec(self);
        int32 ax    = x0 + depth * ind + (ind - aw) / 2;
        if (kr && kr->expanded)
            paintArrow(d, ax, mid - aw / 4, aw, true, text);
        else
            paintArrowRight(d, ax + aw / 4, mid - aw / 2, aw, text);
    }
}

static void paintHeader(CxUiGrid* self, CxUiDraw* d, CxUiFont* f, int32 width)
{
    if (self->_headerH <= 0)
        return;
    int32 h = self->_headerH;
    cxuiDrawFill(d, 0, 0, width, h, pal(self, CXUI_PAL_GridHeader));
    CxUiColor text = pal(self, cxuiWidgetEffectiveEnabled(self) ? CXUI_PAL_GridHeaderText : CXUI_PAL_TextDisabled);
    CxUiColor line = pal(self, CXUI_PAL_Border);
    int32 inset    = px(self, GRID_HEAD_PAD_Y);
    int32 arrowW   = px(self, GRID_ARROW);
    for (int32 i = 0; i < self->_ncols; i++) {
        CxUiGridCol* col = &self->_cols[i];
        int32 x          = col->x - self->_scrollX;
        if (col->w <= 0 || x >= width || x + col->w <= 0)
            continue;
        if (i == self->_pressCol)
            cxuiDrawFill(d, x, 0, col->w, h, pal(self, CXUI_PAL_Pressed));
        CxUiRect box = { x + self->_padX, 0, col->w - 2 * self->_padX, h };
        cxuiDrawClipPush(d, (CxUiRect){ x, 0, col->w, h });
        if (i == self->_sortCol) {
            box.w -= arrowW + self->_padX / 2;
            paintArrow(d, x + col->w - self->_padX - arrowW, (h - arrowW / 2) / 2, arrowW, self->_sortDesc, text);
        }
        if (box.w > 0 && strLen(col->title) > 0)
            paintText(self, d, f, box, col->title, col->align, text);
        cxuiDrawClipPop(d);
        cxuiDrawFill(d, x + col->w - 1, inset, 1, max(h - 2 * inset, 0), line);
    }
    cxuiDrawFill(d, 0, h - 1, width, 1, line);
}

static void paint(CxUiGrid* self, CxUiDraw* d)
{
    CxUiSize sz    = cxuiDrawSize(d);
    CxUiRect dirty = cxuiDrawDirty(d);
    CxUiFont* f    = bodyFont(self);
    cxuiDrawFill(d, dirty.x, dirty.y, dirty.w, dirty.h, pal(self, CXUI_PAL_Content));

    bool enabled     = cxuiWidgetEffectiveEnabled(self);
    CxUiColor text   = pal(self, enabled ? CXUI_PAL_Text : CXUI_PAL_TextDisabled);
    CxUiColor selBg  = pal(self, CXUI_PAL_Selection);
    CxUiColor selFg  = pal(self, enabled ? CXUI_PAL_SelectionText : CXUI_PAL_TextDisabled);
    CxUiColor stripe = pal(self, CXUI_PAL_Panel);
    CxUiColor hot    = pal(self, CXUI_PAL_Hover);
    CxUiColor line   = pal(self, CXUI_PAL_GridLine);
    bool lines       = self->_flags & CXUI_GRID_Lines;
    bool focused     = CxUiDrawn(self->_body)->_focused;
    int32 rowsAt     = self->_headerH;
    int32 rowW       = min(sz.w, self->_totalW - self->_scrollX);
    if ((self->_flat || self->_treeSrc) && dirty.y + dirty.h > rowsAt) {
        int64 first = self->_top + max(dirty.y - rowsAt, 0) / self->_rowH;
        int64 last  = min(self->_top + (dirty.y + dirty.h - 1 - rowsAt) / self->_rowH, self->_rows - 1);
        int32 si    = selSearch(self, first);
        int32 tc    = self->_tree ? treeColumn(self) : -1;
        if (self->_tree && first <= last)
            walkSeek(self, first);
        cxuiDrawClipPush(d, (CxUiRect){ 0, rowsAt, sz.w, max(sz.h - rowsAt, 0) });
        for (int64 r = first; r <= last; r++) {
            int32 y = rowsAt + (int32)(r - self->_top) * self->_rowH;
            while (si < SEL_N(self) && SEL_LAST(self, si) < r)
                si++;
            bool selected = si < SEL_N(self) && SEL_FIRST(self, si) <= r;
            if (selected)
                cxuiDrawFill(d, 0, y, sz.w, self->_rowH, selBg);
            else if (r == self->_hot)
                cxuiDrawFill(d, 0, y, sz.w, self->_rowH, hot);
            else if ((self->_flags & CXUI_GRID_Stripes) && (r & 1))
                cxuiDrawFill(d, 0, y, sz.w, self->_rowH, stripe);
            uint64 item = self->_tree ? walkNode(self) : (uint64)r;
            int32 depth = self->_tree ? walkDepth(self) : 0;
            for (int32 c = 0; c < self->_ncols; c++) {
                CxUiGridCol* col = &self->_cols[c];
                int32 x          = col->x - self->_scrollX;
                if (col->w <= 0 || x >= dirty.x + dirty.w || x + col->w <= dirty.x)
                    continue;
                CxUiRect cr   = { x, y, col->w, self->_rowH };
                CxUiColor fg  = selected ? selFg : text;
                int32 lead    = self->_padX;
                if (c == tc) {
                    // The lines and expander fit in the indent, unless the column is narrower.
                    lead      = treeLead(self, depth);
                    bool clip = lead > cr.w;
                    if (clip)
                        cxuiDrawClipPush(d, cr);
                    paintTreeLead(self, d, cr, depth, item, fg);
                    if (clip)
                        cxuiDrawClipPop(d);
                }
                paintCell(self, d, f, item, c, cr, lead, fg, selected);
            }
            if (lines)
                cxuiDrawFill(d, 0, y + self->_rowH - 1, rowW, 1, line);
            // A selected focus row needs no ring where only one row is selected.
            if (focused && r == self->_focus && (!selected || self->_select != CXUI_SELECT_Single))
                cxuiDrawFrame(d, 0, y, max(rowW, 1), self->_rowH, 1, pal(self, CXUI_PAL_Focus));
            if (self->_tree && r < last)
                walkNext(self);
        }
        if (lines && last >= first) {
            int32 bottom = rowsAt + (int32)(last + 1 - self->_top) * self->_rowH;
            for (int32 c = 0; c < self->_ncols; c++) {
                CxUiGridCol* col = &self->_cols[c];
                if (col->w > 0)
                    cxuiDrawFill(d, col->x + col->w - 1 - self->_scrollX, rowsAt, 1, bottom - rowsAt, line);
            }
        }
        cxuiDrawClipPop(d);
    }
    if (dirty.y < rowsAt)
        paintHeader(self, d, f, sz.w);
}

// ---- the user's actions ----

static void activate(CxUiGrid* self, int64 row)
{
    CxUiActivateEvent ev = { .base.kind = CXUI_EV_Activate, .item = itemAt(self, row) };
    raiseEvent(self, &ev.base);
}

static void context(CxUiGrid* self, int64 row, int32 col, bool header, int32 x, int32 y)
{
    CxUiContextEvent ev = { .base.kind = CXUI_EV_Context, .item = row >= 0 ? itemAt(self, row) : 0, .column = col,
                            .onItem = row >= 0, .header = header, .x = x, .y = y };
    raiseEvent(self, &ev.base);
}

static void sortClick(CxUiGrid* self, int32 col)
{
    bool desc = col == self->_sortCol && !self->_sortDesc;
    CxUiGrid_setSort(self, col, desc);
    CxUiSortEvent ev = { .base.kind = CXUI_EV_Sort, .column = col, .descending = desc };
    raiseEvent(self, &ev.base);
}

// Moves the focus row for the user, selecting as the modifiers say. A click toggles with
// MOD_TOGGLE where a key only moves the focus.
static void moveTo(CxUiGrid* self, int64 row, uint32 mods, bool click, bool show)
{
    if (self->_rows <= 0)
        return;
    row          = clamp(row, 0, self->_rows - 1);
    bool shift   = mods & CXUI_MOD_Shift;
    bool toggle  = mods & MOD_TOGGLE;
    sa_int64 was;
    saClone(&was, self->_sel);
    switch (self->_select) {
    case CXUI_SELECT_Multi:
        if (shift) {
            int64 from = self->_anchor >= 0 ? self->_anchor : self->_focus >= 0 ? self->_focus : row;
            self->_anchor = from;
            if (!toggle)
                selClear(self);
            selAdd(self, min(from, row), max(from, row));
        } else if (toggle) {
            if (click) {
                if (selHas(self, row))
                    selRemove(self, row, row);
                else
                    selAdd(self, row, row);
                self->_anchor = row;
            }
        } else {
            selOnly(self, row, row);
            self->_anchor = row;
        }
        break;
    case CXUI_SELECT_Single:
        if (!toggle || click)
            selOnly(self, row, row);
        self->_anchor = row;
        break;
    default:
        self->_anchor = row;
        break;
    }
    if (row != self->_focus) {
        if (self->_focus >= 0)
            repaintRows(self, self->_focus, self->_focus);
        repaintRows(self, row, row);
        self->_focus = row;
    }
    if (show)
        showRow(self, row);
    selDone(self, &was);
}

// Space: selects the focus row, or toggles it with MOD_TOGGLE
static bool spaceKey(CxUiGrid* self, bool toggle)
{
    int64 row = self->_focus;
    if (row < 0 || self->_select == CXUI_SELECT_None)
        return false;
    sa_int64 was;
    saClone(&was, self->_sel);
    if (self->_select == CXUI_SELECT_Single)
        selOnly(self, row, row);
    else if (toggle && selHas(self, row))
        selRemove(self, row, row);
    else
        selAdd(self, row, row);
    self->_anchor = row;
    selDone(self, &was);
    return true;
}

static int32 firstTextColumn(CxUiGrid* self)
{
    for (int32 i = 0; i < self->_ncols; i++) {
        if (!(self->_cols[i].flags & CXUI_COL_Hidden) && self->_cols[i].kind != CXUI_COL_Check)
            return i;
    }
    return -1;
}

// Moves to the next row whose first text column starts with what has been typed
static void typeAhead(CxUiGrid* self, char ch)
{
    int64 now = clockTimer();
    if (now - self->_typedAt > timeMS(GRID_TYPE_MS))
        strClear(&self->_typed);
    self->_typedAt = now;
    char add[2]    = { ch, 0 };
    strAppend(&self->_typed, (strref)add);

    int32 col = firstTextColumn(self);
    if (col < 0 || self->_rows <= 0)
        return;
    // Typing one letter again and again goes through the rows starting with it; a longer prefix
    // may still match the focus row.
    int32 len   = strLen(self->_typed);
    bool repeat = true;
    for (int32 i = 1; i < len && repeat; i++)
        repeat = strGetChar(self->_typed, i) == ch;
    string prefix = 0;
    strSubStr(&prefix, self->_typed, 0, repeat ? 1 : len);
    int64 start = repeat ? self->_focus + 1 : max(self->_focus, 0);
    int64 scan  = min(self->_rows, GRID_TYPE_SCAN);
    for (int64 i = 0; i < scan; i++) {
        int64 row = (start + i) % self->_rows;
        if (fetchCell(self, itemAt(self, row), col) && strBeginsWithi(self->_cell.text, prefix)) {
            moveTo(self, row, 0, false, true);
            break;
        }
    }
    strDestroy(&prefix);
}

static char typedChar(CxUiKey key)
{
    if (key >= CXUI_KEY_A && key <= CXUI_KEY_Z)
        return (char)('a' + (key - CXUI_KEY_A));
    if (key >= CXUI_KEY_0 && key <= CXUI_KEY_9)
        return (char)('0' + (key - CXUI_KEY_0));
    if (key >= CXUI_KEY_Pad0 && key <= CXUI_KEY_Pad9)
        return (char)('0' + (key - CXUI_KEY_Pad0));
    return 0;
}

// The user changes a cell's check box, to state or, if it is negative, the other way. The program
// hears of it first; the grid's own handling of the event then sets it in the source.
static void userCheck(CxUiGrid* self, int64 row, int32 col, int32 state)
{
    uint64 item = itemAt(self, row);
    if (state < 0)
        state = fetchCell(self, item, col) && self->_cell.check == CXUI_CHECK_On ? CXUI_CHECK_Off : CXUI_CHECK_On;
    CxUiCheckEvent ev = { .base.kind = CXUI_EV_Check, .item = item, .column = col, .state = (uint32)state };
    raiseEvent(self, &ev.base);
}

// Whether a row's cell has a check box shown
static bool rowHasBox(CxUiGrid* self, int64 row, int32 col)
{
    return fetchCell(self, itemAt(self, row), col) && !(self->_cell.flags & CXUI_CELL_CheckHidden);
}

// The column whose check box in a row is under x, or -1
static int32 checkAt(CxUiGrid* self, int64 row, int32 x)
{
    int32 tc    = self->_tree ? treeColumn(self) : -1;
    int32 depth = 0;
    if (self->_tree) {
        CxUiGrid_nodeAt(self, row);
        depth = walkDepth(self);
    }
    for (int32 c = 0; c < self->_ncols; c++) {
        CxUiGridCol* col = &self->_cols[c];
        if (col->w <= 0 || !hasBox(col))
            continue;
        CxUiRect cr = { col->x - self->_scrollX, 0, col->w, self->_rowH };
        CxUiRect b  = boxRect(self, col, cr, c == tc ? treeLead(self, depth) : self->_padX);
        // A little either side, and the row's whole height
        int32 slop = px(self, 2);
        if (x >= b.x - slop && x < b.x + b.w + slop)
            return rowHasBox(self, row, c) ? c : -1;
    }
    return -1;
}

// Space changes the focus row's check box, and every selected row's with it if it is selected.
static bool spaceCheck(CxUiGrid* self)
{
    int64 at = self->_focus;
    int32 col = -1;
    for (int32 c = 0; c < self->_ncols && col < 0; c++) {
        if (self->_cols[c].w > 0 && hasBox(&self->_cols[c]))
            col = c;
    }
    if (col < 0 || at < 0 || !rowHasBox(self, at, col))
        return false;
    int32 state = self->_cell.check == CXUI_CHECK_On ? CXUI_CHECK_Off : CXUI_CHECK_On;
    if (!selHas(self, at)) {
        userCheck(self, at, col, state);
        return true;
    }
    // The rows first, since a handler may change the selection.
    sa_int64 rows;
    saInit(&rows, int64, 16);
    for (int64 r = CxUiGrid_nextSelected(self, -1); r >= 0; r = CxUiGrid_nextSelected(self, r))
        saPush(&rows, int64, r);
    for (int32 i = 0; i < saSize(rows) && live(self); i++) {
        if (rows.a[i] < self->_rows && rowHasBox(self, rows.a[i], col))
            userCheck(self, rows.a[i], col, state);
    }
    saDestroy(&rows);
    return true;
}

// Right opens the focus row's node, or goes to its first child; Left closes it, or goes to its
// parent.
static bool treeKey(CxUiGrid* self, int64 at, bool right)
{
    CxUiNodeId node = CxUiGrid_nodeAt(self, at);
    bool open       = CxUiGrid_isExpanded(self, node);
    if (right) {
        if (!open)
            userExpand(self, node, true);
        else if (at + 1 < self->_rows && CxUiGrid_nodeAt(self, at + 1) &&
                 self->_treeSrc->parentOf(self->_source, CxUiGrid_nodeAt(self, at + 1)) == node)
            moveTo(self, at + 1, 0, false, true);
        return true;
    }
    if (open) {
        userExpand(self, node, false);
        return true;
    }
    int64 up = treeRowOf(self, self->_treeSrc->parentOf(self->_source, node), NULL);
    if (up >= 0)
        moveTo(self, up, 0, false, true);
    return true;
}

// ---- editing ----

// Whether a column's cells can be edited at all: shown, and not check boxes alone
static bool editableColumn(CxUiGridCol* col)
{
    return col->w > 0 && col->kind != CXUI_COL_Check;
}

// Whether the user can edit a row's cell: an editable column, and a cell that allows it
static bool userEditable(CxUiGrid* self, int64 row, int32 c)
{
    CxUiGridCol* col = &self->_cols[c];
    if (!editableColumn(col) || !(col->flags & CXUI_COL_Editable))
        return false;
    fetchCell(self, itemAt(self, row), c);
    return !(self->_cell.flags & CXUI_CELL_ReadOnly);
}

// The edited cell's row, or -1 if it isn't shown
static int64 editRow(CxUiGrid* self)
{
    return self->_tree ? treeRowOf(self, self->_editItem, NULL) : (int64)self->_editItem;
}

// Where a row's cell's text starts, from the cell's left edge
static int32 textLead(CxUiGrid* self, int64 row, int32 c)
{
    CxUiGridCol* col = &self->_cols[c];
    int32 lead       = self->_padX;
    if (self->_tree && c == treeColumn(self)) {
        CxUiGrid_nodeAt(self, row);
        lead = treeLead(self, walkDepth(self));
    }
    if (hasBox(col))
        lead += boxSize(self) + self->_padX / 2;
    if (fetchCell(self, itemAt(self, row), c))
        lead += imageWidth(self, bodyFont(self), self->_rowH);
    return lead;
}

// The editor's place over its cell, in the body's physical pixels; empty while the cell isn't
// shown whole
static CxUiRect editCellRect(CxUiGrid* self)
{
    int64 row        = editRow(self);
    CxUiGridCol* col = &self->_cols[self->_editCol];
    if (row < self->_top || row >= self->_top + self->_page || col->w <= 0)
        return (CxUiRect){ 0 };
    int32 left  = col->x - self->_scrollX;
    int32 x     = left + textLead(self, row, self->_editCol) - px(self, GRID_EDIT_INSET);
    int32 right = max(left + col->w - 1, x + px(self, GRID_EDIT_MIN_W));
    x           = max(x, 0);
    right       = min(right, bodyRect(self).w);
    return (CxUiRect){ x, self->_headerH + (int32)(row - self->_top) * self->_rowH, max(right - x, 0), self->_rowH };
}

// Window pixels to layout units, which the layout turns back into the same pixels
static int32 pxToLayout(CxUiGrid* self, int32 v)
{
    return (int32)floor((float64)v * CXUI_FIX / (self->_scale > 0 ? self->_scale : 1) + 0.5);
}

// Puts the editor over its cell, scrolling to keep the cell shown; in the grid's layout pass
static void editArrange(CxUiGrid* self)
{
    int64 row = editRow(self);
    if (row >= 0 && self->_page > 0 && (row < self->_top || row >= self->_top + self->_page)) {
        self->_top = row < self->_top ? row : row - self->_page + 1;
        cxuiDScrollBarSetPos(self->_vbar, self->_top);
    }
    CxUiRect r = editCellRect(self);
    CxUiRect b = bodyRect(self);
    int32 x0 = pxToLayout(self, b.x + r.x), y0 = pxToLayout(self, b.y + r.y);
    _cxuiLayoutPlace(CxUiWidget(self->_editFrame),
                     (CxUiLayoutRect){ x0, y0, pxToLayout(self, b.x + r.x + r.w) - x0,
                                       pxToLayout(self, b.y + r.y + r.h) - y0 });
}

// The rows changed under the editor: it follows its cell, or closes if the cell is gone
static void editMoved(CxUiGrid* self)
{
    if (!self->_editFrame)
        return;
    if (editRow(self) < 0)
        editClose(self);
    else
        _cxuiLayoutDirty(CxUiWidget(self->_editFrame));
}

// Closes the editor without telling the program
static void editClose(CxUiGrid* self)
{
    if (!self->_editFrame)
        return;
    self->_editSerial++;
    CxUiTextFrame* frame = self->_editFrame;
    self->_editFrame     = NULL;
    // Focus goes back to the rows, rather than nowhere, when the field had it.
    CxUiWindow* win = _cxuiWindowOf(CxUiWidget(self));
    if (win && win->_focus == CxUiWidget(self->_editField)) {
        _cxui.focusInPlace = true;
        cxuiFocus(self->_body);
        _cxui.focusInPlace = false;
    }
    objRelease(&self->_editField);
    cxuiDestroy(frame);
    objRelease(&frame);
}

// Closes the editor and tells the program, whose handling of a commit sets the text by default
static void editEnd(CxUiGrid* self, uint32 stage)
{
    if (!self->_editFrame)
        return;
    string text = 0;
    strDup(&text, cxuiTextFieldText(self->_editField));
    CxUiEditEvent ev = { .base.kind = CXUI_EV_Edit, .item = self->_editItem, .column = self->_editCol,
                         .stage = stage };
    editClose(self);
    ev.text = text;
    raiseEvent(self, &ev.base);
    strDestroy(&text);
}

// Scrolls across as little as it takes to show a column, or its start where it is wider
static void showColumn(CxUiGrid* self, int32 c)
{
    CxUiGridCol* col = &self->_cols[c];
    int32 x          = self->_scrollX;
    if (col->x + col->w > x + bodyRect(self).w)
        x = col->x + col->w - bodyRect(self).w;
    if (col->x < x)
        x = col->x;
    scrollTo(self, self->_top, x);
}

// Opens the editor over a row's cell, which the program hears of as it opens and can close
static bool editStart(CxUiGrid* self, int64 row, int32 c)
{
    clickCancel(self);
    if (self->_editFrame) {
        editEnd(self, CXUI_EDIT_Commit);
        if (!live(self))
            return false;
    }
    if (row < 0 || row >= self->_rows || c < 0 || c >= self->_ncols || !editableColumn(&self->_cols[c]))
        return false;
    CxUiWindow* win = _cxuiWindowOf(CxUiWidget(self));
    if (!win || !CxUiWidget(win)->peer || !CxUiWidget(self->_body)->peer)
        return false;
    showRow(self, row);
    showColumn(self, c);

    uint64 item = itemAt(self, row);
    fetchCell(self, item, c);
    CxUiGridEditor* field = CxUiGridEditor_create(self->_cell.text);
    self->_editFrame      = cxuiTextFrameCreate(
        &(CxUiTextFrameDesc){ .field = CxUiTextField(field), .padding = { 1, 1 } });
    self->_editField = CxUiTextField(field);
    self->_editItem  = item;
    self->_editCol   = c;
    self->_editSerial++;
    _cxuiBoxBaseInsert(CxUiBoxBase(self), CxUiWidget(self->_editFrame), 3);
    _cxuiLayoutFlush(win);
    if (!live(self) || !self->_editFrame)
        return false;
    CxUiPeer* fp = CxUiWidget(self->_editFrame)->peer;
    if (!fp || !CxUiWidget(self->_editField)->peer) {
        editClose(self);
        return false;
    }
    if (_cxui.backend->peerRaise) {
        _cxuiNativeEnter();
        _cxui.backend->peerRaise(fp);
        _cxuiNativeLeave();
    }
    cxuiTextFieldSelectAll(self->_editField);
    cxuiFocus(self->_editField);

    uint32 serial = self->_editSerial;
    string text   = 0;
    strDup(&text, cxuiTextFieldText(self->_editField));
    CxUiEditEvent ev = { .base.kind = CXUI_EV_Edit, .item = item, .column = c, .stage = CXUI_EDIT_Start, .text = text };
    CxUiResult r     = raiseEvent(self, &ev.base);
    strDestroy(&text);
    if (!live(self) || self->_editSerial != serial)
        return false;
    if (r == CXUI_Handled) {
        editClose(self);
        return false;
    }
    return true;
}

// Tab keeps what was typed, then edits the next cell the user can, along the row and on down;
// Shift goes back.
static void editTab(CxUiGrid* self, bool back)
{
    int64 row = editRow(self);
    int32 c   = self->_editCol;
    editEnd(self, CXUI_EDIT_Commit);
    // A handler may have opened another editor, or taken the row away.
    if (!live(self) || self->_editFrame || row < 0 || row >= self->_rows)
        return;
    int32 step = back ? -1 : 1;
    for (int64 rows = 0;;) {
        c += step;
        if (c < 0 || c >= self->_ncols) {
            row += step;
            if (row < 0 || row >= self->_rows || ++rows > GRID_TYPE_SCAN)
                return;
            c = back ? self->_ncols - 1 : 0;
        }
        if (userEditable(self, row, c))
            break;
    }
    if (row != self->_focus)
        moveTo(self, row, 0, false, true);
    if (live(self))
        editStart(self, row, c);
}

// How focus leaving the editor for another widget ends the edit
static uint32 blurStage(CxUiGrid* self)
{
    return (self->_flags & CXUI_GRID_CancelOnBlur) ? CXUI_EDIT_Cancel : CXUI_EDIT_Commit;
}

// After the editor's field lost focus: focus that went to another widget in the window ends the
// edit; focus that left the window, as when switching to another program, comes back to it.
static bool editFocusTask(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    CxUiGrid* self = stvlAtObj(cvars, 0, CxUiGrid);
    uint32 serial  = stvlAt(cvars, 1, uint32);
    if (!self || !live(self) || !self->_editFrame || serial != self->_editSerial)
        return true;
    CxUiWindow* win   = _cxuiWindowOf(CxUiWidget(self));
    CxUiWidget* focus = win ? win->_focus : NULL;
    if (focus == CxUiWidget(self->_editField))
        return true;
    if (!focus && cxuiWidgetEffectiveEnabled(self))
        return true;
    objAcquire(self);
    editEnd(self, blurStage(self));
    objRelease(&self);
    return true;
}

static void clickCancel(CxUiGrid* self)
{
    self->_clickRow = -1;
    if (self->_clickTimer)
        cxuiTimerStop(self->_clickTimer);
}

static CxUiResult clickTick(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    ObjInst_WeakRef* wref = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return CXUI_Default;
    CxUiGrid* self = objAcquireFromWeakDyn(CxUiGrid, wref);
    if (!self)
        return CXUI_Default;
    int64 row = self->_clickRow;
    int32 c   = self->_clickCol;
    self->_clickRow = -1;
    if (live(self) && row >= 0 && row < self->_rows && row == self->_focus && selHas(self, row) &&
        userEditable(self, row, c))
        editStart(self, row, c);
    objRelease(&self);
    return CXUI_Default;
}

// A click on the selected focus row edits its cell once the time for a double click has passed,
// unless something else happens first.
static void clickArm(CxUiGrid* self)
{
    int64 time     = timeMS(500);
    int32 distance = 4;
    if (_cxui.backend->doubleClick)
        _cxui.backend->doubleClick(&time, &distance);
    if (!self->_clickTimer) {
        // Holds the grid weakly: the grid holds the timer.
        Weak(CxUiGrid)* wref = objGetWeak(CxUiGrid, self);
        self->_clickTimer    = cxuiTimerCreate(time, CXUI_Timer_OneShot, clickTick, stvar(weakref, objWeakRefBase(wref)));
        objDestroyWeak(&wref);
    }
    self->_clickTimer->period = time;
    cxuiTimerStop(self->_clickTimer);
    cxuiTimerStart(self->_clickTimer);
}

// Edits a row's first cell the user can edit; false if it has none
static bool editFirst(CxUiGrid* self, int64 row)
{
    for (int32 c = 0; c < self->_ncols; c++) {
        if (userEditable(self, row, c)) {
            editStart(self, row, c);
            return true;
        }
    }
    return false;
}

// A key the focused grid handles; false to let it go on to the parent
static bool keyDown(CxUiGrid* self, CxUiKeyEvent* k)
{
    uint32 mods  = k->mods & (CXUI_MOD_Shift | CXUI_MOD_Ctrl | CXUI_MOD_Alt | CXUI_MOD_Meta);
    bool toggle  = mods & MOD_TOGGLE;
    bool other   = mods & ~(CXUI_MOD_Shift | MOD_TOGGLE);
    bool typing  = strLen(self->_typed) > 0 && clockTimer() - self->_typedAt <= timeMS(GRID_TYPE_MS);
    int64 at     = self->_focus;
    int64 page   = max(self->_page, 1);
    int64 target = -1;
    bool move    = true;
    clickCancel(self);

    switch (k->key) {
    case CXUI_KEY_Up:
#if defined(_PLATFORM_MACOS)
        // Command-Up goes to a node's parent on the Mac, as in Finder.
        if (mods == CXUI_MOD_Meta) {
            if (!self->_tree || at < 0)
                return false;
            int64 up = treeRowOf(self, self->_treeSrc->parentOf(self->_source, CxUiGrid_nodeAt(self, at)), NULL);
            if (up >= 0)
                moveTo(self, up, 0, false, true);
            return true;
        }
#endif
        target = at < 0 ? 0 : at - 1;
        break;
    case CXUI_KEY_Down:
#if defined(_PLATFORM_MACOS)
        // Command-Down opens the focus row on the Mac, as in Finder.
        if (mods == CXUI_MOD_Meta) {
            if (at < 0)
                return false;
            activate(self, at);
            return true;
        }
#endif
        target = at < 0 ? 0 : at + 1;
        break;
    case CXUI_KEY_Home:
        target = 0;
        break;
    case CXUI_KEY_End:
        target = self->_rows - 1;
        break;
    // A page key goes to the last row shown that way first, then a page on.
    case CXUI_KEY_PageUp:
        target = at > self->_top ? self->_top : at - (page - 1);
        break;
    case CXUI_KEY_PageDown:
        target = at < self->_top + page - 1 ? self->_top + page - 1 : at + (page - 1);
        break;
    case CXUI_KEY_Left:
    case CXUI_KEY_Right:
        if (!mods && self->_tree && at >= 0)
            return treeKey(self, at, k->key == CXUI_KEY_Right);
        if (mods || self->_totalW <= bodyRect(self).w)
            return false;
        scrollTo(self, self->_top, self->_scrollX + (k->key == CXUI_KEY_Left ? -1 : 1) * px(self, GRID_HSTEP));
        return true;
    case CXUI_KEY_PadAdd:
    case CXUI_KEY_PadSubtract:
        if (mods || !self->_tree || at < 0)
            return false;
        userExpand(self, CxUiGrid_nodeAt(self, at), k->key == CXUI_KEY_PadAdd);
        return true;
    case CXUI_KEY_Enter:
    case CXUI_KEY_PadEnter:
        if (mods || at < 0)
            return false;
#if defined(_PLATFORM_MACOS)
        // Return renames on the Mac, as in Finder, where the row has something to rename.
        if (k->key == CXUI_KEY_Enter && editFirst(self, at))
            return true;
#endif
        activate(self, at);
        return true;
    case CXUI_KEY_F2:
        return !mods && at >= 0 && editFirst(self, at);
    case CXUI_KEY_Space:
        if (!mods && typing) {
            typeAhead(self, ' ');
            return true;
        }
        if (!mods && spaceCheck(self))
            return true;
        return !other && !(mods & CXUI_MOD_Shift) && spaceKey(self, toggle);
    case CXUI_KEY_ContextMenu:
    case CXUI_KEY_F10:
        if (k->key == CXUI_KEY_F10 ? mods != CXUI_MOD_Shift : mods != 0)
            return false;
        showRow(self, at);
        if (at >= self->_top && at < self->_top + page) {
            int32 y = self->_headerH + (int32)(at - self->_top + 1) * self->_rowH;
            context(self, at, firstTextColumn(self), false, self->_padX, y);
        } else {
            context(self, -1, -1, false, self->_padX, self->_headerH);
        }
        return true;
    case CXUI_KEY_A:
        if (mods == MOD_TOGGLE) {
            if (self->_select != CXUI_SELECT_Multi || self->_rows <= 0)
                return false;
            sa_int64 was;
            saClone(&was, self->_sel);
            selOnly(self, 0, self->_rows - 1);
            selDone(self, &was);
            return true;
        }
        move = false;
        break;
    default:
        move = false;
        break;
    }

    if (move) {
        if (other || self->_rows <= 0)
            return false;
        moveTo(self, target, mods, false, true);
        return true;
    }

    char ch = typedChar(k->key);
    if (ch && !(mods & ~CXUI_MOD_Shift)) {
        typeAhead(self, ch);
        return true;
    }
    return false;
}

// ---- the mouse ----

static void stopDrag(CxUiGrid* self)
{
    self->_dragging = false;
    self->_dragDir  = 0;
    if (self->_autoscroll)
        cxuiTimerStop(self->_autoscroll);
}

// Selects to the row at y, or the nearest one shown, while dragging
static void dragTo(CxUiGrid* self, int32 y)
{
    if (self->_rows <= 0 || self->_rowH <= 0)
        return;
    int32 bottom = max(bodyRect(self).h - 1, self->_headerH);
    int32 at     = clamp(y, self->_headerH, bottom);
    int64 row    = min(self->_top + (at - self->_headerH) / self->_rowH, self->_rows - 1);
    if (row != self->_focus)
        moveTo(self, row, self->_select == CXUI_SELECT_Multi ? CXUI_MOD_Shift : 0, true, false);
}

static CxUiResult autoscrollTick(stvlist* cvars, CxUiTimerEvent* ev)
{
    unused_noeval(ev);
    ObjInst_WeakRef* wref = NULL;
    if (!stvlNext(cvars, weakref, &wref))
        return CXUI_Default;
    CxUiGrid* self = objAcquireFromWeakDyn(CxUiGrid, wref);
    if (!self)
        return CXUI_Default;
    if (live(self) && self->_dragging && self->_dragDir) {
        // Faster the farther outside the pointer is
        int32 out = self->_dragDir < 0 ? self->_headerH - self->_mouseY : self->_mouseY - bodyRect(self).h + 1;
        int64 n   = 1 + max(out, 0) / max(self->_rowH, 1);
        scrollTo(self, self->_top + self->_dragDir * n, self->_scrollX);
        dragTo(self, self->_mouseY);
    }
    objRelease(&self);
    return CXUI_Default;
}

static void dragMove(CxUiGrid* self, int32 y)
{
    self->_dragDir = y < self->_headerH ? -1 : y >= bodyRect(self).h ? 1 : 0;
    if (self->_dragDir) {
        if (!self->_autoscroll) {
            // Holds the grid weakly: the grid holds the timer.
            Weak(CxUiGrid)* wref = objGetWeak(CxUiGrid, self);
            self->_autoscroll    = cxuiTimerCreate(timeMS(GRID_AUTOSCROLL_MS), 0, autoscrollTick,
                                                   stvar(weakref, objWeakRefBase(wref)));
            objDestroyWeak(&wref);
        }
        if (!cxuiTimerIsRunning(self->_autoscroll))
            cxuiTimerStart(self->_autoscroll);
    } else if (self->_autoscroll) {
        cxuiTimerStop(self->_autoscroll);
    }
    dragTo(self, y);
}

static void sizeTo(CxUiGrid* self, int32 x)
{
    CxUiGridCol* col = &self->_cols[self->_sizeCol];
    int32 w          = max(self->_sizeW + x - self->_sizeFrom, minColWidth(self, col));
    CxUiLen len      = cxuiLen(_cxuiDip(CxUiWidget(self), w));
    if (len.v == col->width.v && len.unit == col->width.unit)
        return;
    col->width = len;
    refit(self);
    repaint(self);
}

// Whether x in a row is over its node's expander
static bool expanderHit(CxUiGrid* self, int64 row, int32 x)
{
    int32 tc = self->_tree ? treeColumn(self) : -1;
    if (tc < 0)
        return false;
    CxUiNodeId node = CxUiGrid_nodeAt(self, row);
    int32 depth     = walkDepth(self);
    int32 ind       = px(self, GRID_INDENT);
    int32 at        = self->_cols[tc].x - self->_scrollX + self->_padX / 2 + depth * ind;
    return x >= at && x < at + ind && self->_treeSrc->hasChildren(self->_source, node);
}

static void mouseDown(CxUiGrid* self, CxUiMouseEvent* m)
{
    clickCancel(self);
    // A click in the grid outside the editor ends the edit, as focus leaving it does.
    if (self->_editFrame) {
        editEnd(self, blurStage(self));
        if (!live(self))
            return;
    }
    bool dbl      = m->action == CXUI_MOUSE_DoubleClick;
    int32 col     = colAtX(self, m->x);
    uint32 button = m->button;
#if defined(_PLATFORM_MACOS)
    // A Control-click is a right click on the Mac.
    if (button == CXUI_BTN_Left && (m->mods & (CXUI_MOD_Ctrl | CXUI_MOD_Alt | CXUI_MOD_Meta)) == CXUI_MOD_Ctrl)
        button = CXUI_BTN_Right;
#endif
    if (button != CXUI_BTN_Left && button != CXUI_BTN_Right)
        return;

    if (m->y < self->_headerH) {
        if (button == CXUI_BTN_Right) {
            context(self, -1, col, true, m->x, m->y);
            return;
        }
        int32 edge = edgeAtX(self, m->x);
        if (edge >= 0 && dbl) {
            CxUiGrid_fitColumn(self, edge);
        } else if (edge >= 0) {
            self->_sizeCol  = edge;
            self->_sizeFrom = m->x;
            self->_sizeW    = self->_cols[edge].w;
        } else if (col >= 0 && (self->_cols[col].flags & CXUI_COL_Sortable)) {
            self->_pressCol = col;
            repaintHeader(self);
        }
        return;
    }

    int64 row = rowAtY(self, m->y);
    int32 box = row >= 0 && button == CXUI_BTN_Left ? checkAt(self, row, m->x) : -1;
    if (box >= 0) {
        // Changes the box without selecting the row
        userCheck(self, row, box, -1);
        return;
    }
    if (row >= 0 && button == CXUI_BTN_Left && expanderHit(self, row, m->x)) {
        CxUiNodeId node = CxUiGrid_nodeAt(self, row);
        userExpand(self, node, !CxUiGrid_isExpanded(self, node));
        return;
    }
    if (button == CXUI_BTN_Right) {
        // A right click on a row not selected selects it alone.
        if (row >= 0 && !selHas(self, row))
            moveTo(self, row, 0, true, false);
        else if (row >= 0)
            moveTo(self, row, MOD_TOGGLE, false, false);
        if (live(self))
            context(self, row, col, false, m->x, m->y);
        return;
    }
    if (row < 0) {
        // A click below the rows unselects them all.
        if (!(m->mods & (CXUI_MOD_Shift | MOD_TOGGLE))) {
            sa_int64 was;
            saClone(&was, self->_sel);
            selClear(self);
            selDone(self, &was);
        }
        return;
    }
    bool again = !dbl && !(m->mods & (CXUI_MOD_Shift | CXUI_MOD_Ctrl | CXUI_MOD_Alt | CXUI_MOD_Meta)) &&
                 row == self->_focus && selHas(self, row) && col >= 0 && userEditable(self, row, col);
    moveTo(self, row, m->mods, true, true);
    if (!live(self))
        return;
    if (again) {
        self->_clickRow = row;
        self->_clickCol = col;
    }
    if (!(m->mods & MOD_TOGGLE) && self->_select != CXUI_SELECT_None)
        self->_dragging = true;
    if (dbl && !(m->mods & (CXUI_MOD_Shift | MOD_TOGGLE)))
        activate(self, row);
}

static void mouseUp(CxUiGrid* self, CxUiMouseEvent* m)
{
    if (m->button != CXUI_BTN_Left)
        return;
    stopDrag(self);
    if (self->_clickRow >= 0) {
        if (!(m->flags & CXUI_MOUSE_CaptureLost) && rowAtY(self, m->y) == self->_clickRow && self->_focus == self->_clickRow)
            clickArm(self);
        else
            clickCancel(self);
    }
    if (self->_sizeCol >= 0) {
        self->_sizeCol = -1;
        // Its natural size changed too.
        _cxuiLayoutDirty(CxUiWidget(self));
    }
    if (self->_pressCol >= 0) {
        int32 col       = self->_pressCol;
        self->_pressCol = -1;
        repaintHeader(self);
        bool over = m->y >= 0 && m->y < self->_headerH && colAtX(self, m->x) == col;
        if (over && !(m->flags & CXUI_MOUSE_CaptureLost))
            sortClick(self, col);
    }
}

static void mouseMove(CxUiGrid* self, CxUiMouseEvent* m)
{
    if (self->_sizeCol >= 0) {
        sizeTo(self, m->x);
        return;
    }
    if (self->_dragging) {
        dragMove(self, m->y);
        return;
    }
    if (self->_pressCol >= 0)
        return;
    bool edge = m->y >= 0 && m->y < self->_headerH && edgeAtX(self, m->x) >= 0;
    cxuiCanvasSetCursor(self->_body, edge ? CXUI_CURSOR_SizeWE : CXUI_CURSOR_Arrow);
    hover(self);
}

static void mouse(CxUiGrid* self, CxUiMouseEvent* m)
{
    self->_mouseX = m->x;
    self->_mouseY = m->y;
    switch (m->action) {
    case CXUI_MOUSE_Enter:
        self->_mouseIn = true;
        hover(self);
        break;
    case CXUI_MOUSE_Leave:
        self->_mouseIn = false;
        hover(self);
        break;
    case CXUI_MOUSE_Move:
        mouseMove(self, m);
        break;
    case CXUI_MOUSE_Down:
    case CXUI_MOUSE_DoubleClick:
        mouseDown(self, m);
        break;
    case CXUI_MOUSE_Up:
        mouseUp(self, m);
        break;
    }
}

// ---- CxUiGridBody ----

_objfactory_guaranteed CxUiGridBody* CxUiGridBody_create()
{
    CxUiGridBody* self;
    self = objInstCreate(CxUiGridBody);

    objInstInit(self);
    CxUiCanvas(self)->_units  = CXUI_Units_Pixels;
    CxUiDrawn(self)->_canFocus = true;
    return self;
}

extern CxUiResult CxUiDrawn_handleEvent(_In_ CxUiDrawn* self, _Inout_ CxUiEvent* ev);   // parent
#define parent_handleEvent(ev) CxUiDrawn_handleEvent((CxUiDrawn*)(self), ev)
CxUiResult CxUiGridBody_handleEvent(_In_ CxUiGridBody* self, _Inout_ CxUiEvent* ev)
{
    CxUiGrid* grid = gridOf(self);
    if (!grid)
        return parent_handleEvent(ev);
    if (ev->kind == CXUI_EV_Paint) {
        paint(grid, ((CxUiPaintEvent*)ev)->draw);
        return CXUI_Handled;
    }
    if (ev->kind == CXUI_EV_Mouse) {
        // The grid's handlers may destroy it.
        objAcquire(grid);
        mouse(grid, (CxUiMouseEvent*)ev);
        objRelease(&grid);
        return CXUI_Handled;
    }
    return parent_handleEvent(ev);
}

// ---- CxUiGrid ----

// Lets go of the source
static void unwatch(CxUiGrid* self)
{
    CxUiListStore* store = objDynCast(CxUiListStore, self->_source);
    if (store)
        _cxuiListStoreUnwatch(store, self);
    CxUiTreeStore* tstore = objDynCast(CxUiTreeStore, self->_source);
    if (tstore)
        _cxuiTreeStoreUnwatch(tstore, self);
    treeDestroy(self);
    objRelease(&self->_source);
    self->_flat    = NULL;
    self->_treeSrc = NULL;
    self->_edit    = NULL;
}

static void freeColumns(CxUiGrid* self)
{
    for (int32 i = 0; i < self->_ncols; i++)
        strDestroy(&self->_cols[i].title);
    xaDestroy(&self->_cols);
    self->_ncols = 0;
}

static void copyColumns(CxUiGrid* self, const CxUiGridColumn* cols, int32 n)
{
    static const CxUiGridColumn fill = { .flags = CXUI_COL_Stretch };
    if (n <= 0 || !cols) {
        cols = &fill;
        n    = 1;
    }
    self->_cols  = xaAlloc(n * sizeof(CxUiGridCol), XA_Zero);
    self->_ncols = n;
    for (int32 i = 0; i < n; i++) {
        CxUiGridCol* col = &self->_cols[i];
        strDup(&col->title, cols[i].title);
        col->width    = cols[i].width;
        col->minWidth = cols[i].minWidth;
        col->align    = cols[i].align;
        col->kind     = cols[i].kind;
        col->flags    = cols[i].flags;
    }
    self->_sortCol = self->_pressCol = self->_sizeCol = -1;
}

_objfactory_guaranteed CxUiGrid* CxUiGrid_createWith(_In_opt_ const CxUiGridDesc* desc)
{
    CxUiGrid* self;
    self = objInstCreate(CxUiGrid);

    objInstInit(self);
    if (desc) {
        self->_flags          = desc->flags;
        self->_select         = desc->select <= CXUI_SELECT_None ? desc->select : CXUI_SELECT_Single;
        self->_rowHeight      = desc->rowHeight;
        CxUiWidget(self)->_lp = desc->lp;
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    if (!CxUiWidget(self)->_lp.expand)
        CxUiWidget(self)->_lp.expand = CXUI_EXPAND_Both;
    copyColumns(self, desc ? desc->columns : NULL, desc ? desc->ncolumns : 0);
    self->_focus = self->_anchor = self->_hot = self->_clickRow = -1;

    self->_body = CxUiGridBody_create();
    self->_vbar = cxuiDScrollBarCreate(&(CxUiDScrollBarDesc){ .orient = CXUI_ORIENT_V, .thickness = GRID_BAR });
    self->_hbar = cxuiDScrollBarCreate(&(CxUiDScrollBarDesc){ .orient = CXUI_ORIENT_H, .thickness = GRID_BAR });
    cxuiOnScroll(self->_vbar, barScrolled, stvNone);
    cxuiOnScroll(self->_hbar, barScrolled, stvNone);
    _cxuiBoxBaseInsert(CxUiBoxBase(self), CxUiWidget(self->_body), 0);
    _cxuiBoxBaseInsert(CxUiBoxBase(self), CxUiWidget(self->_vbar), 1);
    _cxuiBoxBaseInsert(CxUiBoxBase(self), CxUiWidget(self->_hbar), 2);
    return self;
}

_objinit_guaranteed bool CxUiGrid_init(_In_ CxUiGrid* self)
{
    // Autogen begins -----
    saInit(&self->_sel, int64, 1);
    return true;
    // Autogen ends -------
}

void CxUiGrid_setSource(_In_ CxUiGrid* self, _In_opt_ ObjInst* source)
{
    if (!live(self) || source == self->_source)
        return;
    CxUiTreeSource* tree = source ? objInstIf(source, CxUiTreeSource) : NULL;
    CxUiGridSource* flat = source && !tree ? objInstIf(source, CxUiGridSource) : NULL;
    if (source && !flat && !tree) {
        devFatalError("A grid's source must implement CxUiGridSource or CxUiTreeSource");
        return;
    }
    unwatch(self);
    self->_source  = source ? objAcquire(source) : NULL;
    CxUiListStore* store = objDynCast(CxUiListStore, self->_source);
    if (store)
        _cxuiListStoreWatch(store, self);
    CxUiTreeStore* tstore = objDynCast(CxUiTreeStore, self->_source);
    if (tstore)
        _cxuiTreeStoreWatch(tstore, self);
    self->_flat    = flat;
    self->_treeSrc = tree;
    self->_edit    = source ? objInstIf(source, CxUiGridEditable) : NULL;
    if (tree)
        treeCreate(self);
    self->_top     = 0;
    self->_scrollX = 0;
    CxUiGrid_reset(self);
}

ObjInst* CxUiGrid_source(_In_ CxUiGrid* self)
{
    return live(self) ? self->_source : NULL;
}

void CxUiGrid_setColumnArray(_In_ CxUiGrid* self, _In_opt_ const CxUiGridColumn* cols, int32 n)
{
    if (!live(self))
        return;
    editClose(self);
    clickCancel(self);
    freeColumns(self);
    copyColumns(self, cols, n);
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

int32 CxUiGrid_columnCount(_In_ CxUiGrid* self)
{
    return live(self) ? self->_ncols : 0;
}

void CxUiGrid_setColumnWidth(_In_ CxUiGrid* self, int32 col, CxUiLen width)
{
    if (!live(self) || col < 0 || col >= self->_ncols)
        return;
    self->_cols[col].width = width;
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

int32 CxUiGrid_columnWidth(_In_ CxUiGrid* self, int32 col)
{
    if (!live(self) || col < 0 || col >= self->_ncols || !(CxUiWidget(self)->state & CXUI_WS_Placed))
        return 0;
    return _cxuiDip(CxUiWidget(self), self->_cols[col].w);
}

void CxUiGrid_setColumnHidden(_In_ CxUiGrid* self, int32 col, bool hidden)
{
    if (!live(self) || col < 0 || col >= self->_ncols || hidden == !!(self->_cols[col].flags & CXUI_COL_Hidden))
        return;
    if (hidden)
        self->_cols[col].flags |= CXUI_COL_Hidden;
    else
        self->_cols[col].flags &= ~CXUI_COL_Hidden;
    if (self->_sizeCol == col)
        self->_sizeCol = -1;
    if (self->_pressCol == col)
        self->_pressCol = -1;
    if (hidden && self->_editFrame && self->_editCol == col)
        editClose(self);
    _cxuiLayoutDirty(CxUiWidget(self));
    repaint(self);
}

void CxUiGrid_fitColumn(_In_ CxUiGrid* self, int32 col)
{
    if (!live(self) || col < 0 || col >= self->_ncols)
        return;
    metrics(self);
    CxUiGridCol* c = &self->_cols[col];
    CxUiFont* f    = bodyFont(self);
    int32 w        = titleWidth(self, f, c);
    int64 last     = min(self->_top + self->_page, self->_rows - 1);
    bool tree      = self->_tree && col == treeColumn(self);
    for (int64 r = self->_top; r <= last; r++) {
        uint64 item = itemAt(self, r);
        int32 lead  = tree ? treeLead(self, walkDepth(self)) : self->_padX;
        if (c->flags & CXUI_COL_CheckBox)
            lead += boxSize(self) + self->_padX / 2;
        if (!fetchCell(self, item, col))
            continue;
        int32 cw = cxuiFontAdvance(cellFont(self, f), self->_cell.text) + imageWidth(self, f, self->_rowH);
        w        = max(w, cw + lead + self->_padX);
    }
    if (c->kind == CXUI_COL_Check)
        w = max(titleWidth(self, f, c), boxSize(self) + 2 * self->_padX);
    w = max(w, minColWidth(self, c));
    // Rounded up, so the text still fits once scaled back
    c->width = cxuiLen((int32)ceil(w / (float64)cxuiWidgetScale(self)));
    _cxuiLayoutDirty(CxUiWidget(self));
    refit(self);
    repaint(self);
}

int64 CxUiGrid_rowCount(_In_ CxUiGrid* self)
{
    return live(self) ? self->_rows : 0;
}

int64 CxUiGrid_top(_In_ CxUiGrid* self)
{
    return live(self) ? self->_top : 0;
}

void CxUiGrid_setTop(_In_ CxUiGrid* self, int64 row)
{
    if (live(self))
        scrollTo(self, row, self->_scrollX);
}

void CxUiGrid_showRow(_In_ CxUiGrid* self, int64 row)
{
    if (live(self))
        showRow(self, row);
}

uint32 CxUiGrid_selectMode(_In_ CxUiGrid* self)
{
    return self->_select;
}

void CxUiGrid_setSelectMode(_In_ CxUiGrid* self, uint32 mode)
{
    if (!live(self) || mode > CXUI_SELECT_None || mode == self->_select)
        return;
    self->_select = mode;
    if (mode == CXUI_SELECT_None)
        selClear(self);
    else if (mode == CXUI_SELECT_Single && SEL_N(self) > 0)
        selOnly(self, SEL_FIRST(self, 0), SEL_FIRST(self, 0));
    repaint(self);
}

int64 CxUiGrid_selected(_In_ CxUiGrid* self)
{
    return live(self) && SEL_N(self) > 0 ? SEL_FIRST(self, 0) : -1;
}

bool CxUiGrid_isSelected(_In_ CxUiGrid* self, int64 row)
{
    return live(self) && selHas(self, row);
}

int64 CxUiGrid_selectedCount(_In_ CxUiGrid* self)
{
    return live(self) ? selCount(self) : 0;
}

int64 CxUiGrid_nextSelected(_In_ CxUiGrid* self, int64 after)
{
    if (!live(self))
        return -1;
    after   = max(after, -1);
    int32 i = selSearch(self, after + 1);
    return i < SEL_N(self) ? max(SEL_FIRST(self, i), after + 1) : -1;
}

void CxUiGrid_setSelected(_In_ CxUiGrid* self, int64 row, int64 n, bool selected)
{
    if (!live(self) || n <= 0 || row >= self->_rows || row + n <= 0)
        return;
    int64 last = min(row + n - 1, self->_rows - 1);
    row        = max(row, 0);
    if (!selected)
        selRemove(self, row, last);
    else if (self->_select == CXUI_SELECT_Multi)
        selAdd(self, row, last);
    else if (self->_select == CXUI_SELECT_Single)
        selOnly(self, row, row);
    repaint(self);
}

void CxUiGrid_selectAll(_In_ CxUiGrid* self)
{
    if (!live(self) || self->_select != CXUI_SELECT_Multi || self->_rows <= 0)
        return;
    selOnly(self, 0, self->_rows - 1);
    repaint(self);
}

void CxUiGrid_clearSelection(_In_ CxUiGrid* self)
{
    if (!live(self))
        return;
    selClear(self);
    repaint(self);
}

int64 CxUiGrid_focusRow(_In_ CxUiGrid* self)
{
    return live(self) ? self->_focus : -1;
}

void CxUiGrid_setFocusRow(_In_ CxUiGrid* self, int64 row)
{
    if (!live(self))
        return;
    row           = row < 0 || self->_rows <= 0 ? -1 : min(row, self->_rows - 1);
    self->_focus  = row;
    self->_anchor = row;
    showRow(self, row);
    repaint(self);
}

void CxUiGrid_setSort(_In_ CxUiGrid* self, int32 col, bool descending)
{
    if (!live(self))
        return;
    self->_sortCol  = col >= 0 && col < self->_ncols ? col : -1;
    self->_sortDesc = self->_sortCol >= 0 && descending;
    repaintHeader(self);
}

int32 CxUiGrid_sortColumn(_In_ CxUiGrid* self)
{
    return live(self) ? self->_sortCol : -1;
}

bool CxUiGrid_sortDescending(_In_ CxUiGrid* self)
{
    return live(self) && self->_sortDesc;
}

// Whether the last row is shown, where CXUI_GRID_FollowTail keeps it so; not while a cell is
// edited, which would move the cell from under its editor
static bool atTail(CxUiGrid* self)
{
    return (self->_flags & CXUI_GRID_FollowTail) && !self->_editFrame && self->_top + self->_page >= self->_rows;
}

// Scrolls to show the last row, after rows were added at the tail being followed
static void followTail(CxUiGrid* self)
{
    int64 top = max(self->_rows - self->_page, 0);
    if (top == self->_top)
        return;
    self->_top = top;
    cxuiDScrollBarSetPos(self->_vbar, top);
    repaint(self);
    hover(self);
}

void CxUiGrid_rowsInserted(_In_ CxUiGrid* self, int64 at, int64 n)
{
    if (!live(self) || self->_tree || n <= 0)
        return;
    bool tail = atTail(self);
    addRows(self, at, n);
    if (tail)
        followTail(self);
}

void CxUiGrid_rowsRemoved(_In_ CxUiGrid* self, int64 at, int64 n)
{
    if (live(self) && !self->_tree && at >= 0 && at < self->_rows && n > 0)
        removeRows(self, at, n);
}

void CxUiGrid_rowsChanged(_In_ CxUiGrid* self, int64 at, int64 n)
{
    if (live(self) && n > 0)
        repaintRows(self, at, at + n - 1);
}

CxUiNodeId CxUiGrid_nodeAt(_In_ CxUiGrid* self, int64 row)
{
    if (!live(self) || !self->_tree || row < 0 || row >= self->_rows)
        return 0;
    walkSeek(self, row);
    return walkNode(self);
}

int64 CxUiGrid_rowOf(_In_ CxUiGrid* self, CxUiNodeId node)
{
    return live(self) ? treeRowOf(self, node, NULL) : -1;
}

int32 CxUiGrid_depthOf(_In_ CxUiGrid* self, CxUiNodeId node)
{
    int32 depth = -1;
    return live(self) && treeRowOf(self, node, &depth) >= 0 ? depth : -1;
}

bool CxUiGrid_isExpanded(_In_ CxUiGrid* self, CxUiNodeId node)
{
    TreeRec* rec = live(self) && self->_tree && node ? recFind(self, node) : NULL;
    return rec && rec->expanded;
}

void CxUiGrid_setExpanded(_In_ CxUiGrid* self, CxUiNodeId node, bool expanded)
{
    if (live(self))
        expandNode(self, node, expanded);
}

void CxUiGrid_expandAll(_In_ CxUiGrid* self, CxUiNodeId node)
{
    if (live(self))
        setAll(self, node, true);
}

void CxUiGrid_collapseAll(_In_ CxUiGrid* self, CxUiNodeId node)
{
    if (live(self))
        setAll(self, node, false);
}

void CxUiGrid_showNode(_In_ CxUiGrid* self, CxUiNodeId node)
{
    if (!live(self) || !self->_tree || !node)
        return;
    openAbove(self, node);
    showRow(self, treeRowOf(self, node, NULL));
}

CxUiNodeId CxUiGrid_selectedNode(_In_ CxUiGrid* self)
{
    int64 row = CxUiGrid_selected(self);
    return row >= 0 ? CxUiGrid_nodeAt(self, row) : 0;
}

bool CxUiGrid_isNodeSelected(_In_ CxUiGrid* self, CxUiNodeId node)
{
    int64 row = CxUiGrid_rowOf(self, node);
    return row >= 0 && selHas(self, row);
}

void CxUiGrid_setNodeSelected(_In_ CxUiGrid* self, CxUiNodeId node, bool selected)
{
    if (!live(self) || !self->_tree || !node)
        return;
    if (selected)
        openAbove(self, node);
    int64 row = treeRowOf(self, node, NULL);
    if (row >= 0)
        CxUiGrid_setSelected(self, row, 1, selected);
}

CxUiNodeId CxUiGrid_focusNode(_In_ CxUiGrid* self)
{
    return live(self) && self->_focus >= 0 ? CxUiGrid_nodeAt(self, self->_focus) : 0;
}

void CxUiGrid_setFocusNode(_In_ CxUiGrid* self, CxUiNodeId node)
{
    if (!live(self) || !self->_tree)
        return;
    if (node)
        openAbove(self, node);
    CxUiGrid_setFocusRow(self, node ? treeRowOf(self, node, NULL) : -1);
}

// Paints a node's row again, if it is shown
static void repaintNode(CxUiGrid* self, CxUiNodeId node)
{
    int64 row = treeRowOf(self, node, NULL);
    if (row >= 0)
        repaintRows(self, row, row);
}

void CxUiGrid_nodeInserted(_In_ CxUiGrid* self, CxUiNodeId node)
{
    if (!live(self) || !self->_tree || !node)
        return;
    CxUiNodeId parent = self->_treeSrc->parentOf(self->_source, node);
    TreeRec* rec      = recFind(self, parent);
    // A parent whose children were never read finds them when it opens; it may now show an
    // expander.
    if (!rec || kidIndex(rec, node) >= 0) {
        repaintNode(self, parent);
        return;
    }
    CxUiNodeId next = self->_treeSrc->nextSibling(self->_source, node);
    int32 i         = next ? kidIndex(rec, next) : -1;
    if (i < 0)
        i = saSize(rec->kids);
    saInsert(&rec->kids, i, uint64, node);
    saInsert(&rec->kidRecs, i, ptr, NULL);
    bool tail = atTail(self);
    if (propagate(rec, 1)) {
        addRows(self, rowIn(self, rec, node, NULL), 1);
        if (tail)
            followTail(self);
    }
    repaintNode(self, parent);
}

void CxUiGrid_nodeRemoved(_In_ CxUiGrid* self, CxUiNodeId parent, CxUiNodeId node)
{
    if (!live(self) || !self->_tree || !node)
        return;
    TreeRec* rec = recFind(self, parent);
    int32 i      = rec ? kidIndex(rec, node) : -1;
    if (i < 0) {
        repaintNode(self, parent);
        return;
    }
    int64 n   = kidSize(rec, i);
    int64 row = rowIn(self, rec, node, NULL);
    if (rec->kidRecs.a[i]) {
        recFree(self, rec->kidRecs.a[i]);
        rec->nrecs--;
    }
    saRemove(&rec->kids, i);
    saRemove(&rec->kidRecs, i);
    if (propagate(rec, -n))
        removeRows(self, row, n);
    repaintNode(self, parent);
}

void CxUiGrid_nodeChanged(_In_ CxUiGrid* self, CxUiNodeId node)
{
    if (live(self) && self->_tree)
        repaintNode(self, node);
}

void CxUiGrid_reset(_In_ CxUiGrid* self)
{
    if (!live(self))
        return;
    if (self->_tree) {
        reloadTree(self, self->_tree->root);
        self->_rows = self->_tree->root->rows;
    } else {
        self->_rows = self->_flat ? max(self->_flat->rowCount(self->_source), 0) : 0;
    }
    selClear(self);
    stopDrag(self);
    editClose(self);
    clickCancel(self);
    self->_focus = self->_anchor = self->_hot = -1;
    refit(self);
    hover(self);
    repaint(self);
}

// The grid's parts are its own: nothing can be added, and they leave only when destroyed.
bool CxUiGrid_append(_In_ CxUiGrid* self, _In_ CxUiWidget* child)
{
    return false;
}

bool CxUiGrid_insert(_In_ CxUiGrid* self, _In_ CxUiWidget* child, int32 at)
{
    return false;
}

bool CxUiGrid_detach(_In_ CxUiGrid* self, _In_ CxUiWidget* child)
{
    return !_cxuiLive(child) && _cxuiBoxBaseDetach(CxUiBoxBase(self), child);
}

int32 CxUiGrid_childCount(_In_ CxUiGrid* self)
{
    return saSize(CxUiBoxBase(self)->_children);
}

_Ret_opt_valid_ CxUiWidget* CxUiGrid_child(_In_ CxUiGrid* self, int32 i)
{
    return _cxuiBoxBaseChild(CxUiBoxBase(self), i);
}

extern CxUiSize CxUiWidget_measure(_In_ CxUiWidget* self);   // parent
#define parent_measure() CxUiWidget_measure((CxUiWidget*)(self))
CxUiSize CxUiGrid_measure(_In_ CxUiGrid* self)
{
    // The columns as wide as they ask, up to a limit, and a few rows
    metrics(self);
    CxUiFont* f = bodyFont(self);
    int32 w     = 0;
    for (int32 i = 0; i < self->_ncols; i++) {
        if (!(self->_cols[i].flags & CXUI_COL_Hidden))
            w += colWidth(self, f, &self->_cols[i]);
    }
    int32 h = self->_headerH + GRID_NAT_ROWS * self->_rowH;
    return (CxUiSize){ min(_cxuiDip(CxUiWidget(self), w), GRID_NAT_MAX_W) + GRID_BAR, _cxuiDip(CxUiWidget(self), h) };
}

extern void CxUiWidget_arrange(_In_ CxUiWidget* self);   // parent
#define parent_arrange() CxUiWidget_arrange((CxUiWidget*)(self))
void CxUiGrid_arrange(_In_ CxUiGrid* self)
{
    metrics(self);
    // Fonts and scales may have changed.
    forgetFonts(self);

    CxUiRect gb = CxUiWidget(self)->_bounds;
    barsWanted(self, gb.w, gb.h, &self->_barV, &self->_barH);

    CxUiLayoutRect r = CxUiWidget(self)->_lrect;
    int32 bar        = GRID_BAR * CXUI_FIX;
    int32 vw = self->_barV ? bar : 0, hh = self->_barH ? bar : 0;
    _cxuiLayoutPlace(CxUiWidget(self->_body), (CxUiLayoutRect){ r.x, r.y, r.w - vw, r.h - hh });
    _cxuiLayoutPlace(CxUiWidget(self->_vbar), (CxUiLayoutRect){ r.x + r.w - vw, r.y, vw, self->_barV ? r.h - hh : 0 });
    _cxuiLayoutPlace(CxUiWidget(self->_hbar), (CxUiLayoutRect){ r.x, r.y + r.h - hh, self->_barH ? r.w - vw : 0, hh });

    layoutColumns(self, bodyRect(self).w);
    sync(self);
    if (self->_editFrame)
        editArrange(self);
    repaint(self);
}

extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiGrid_restyle(_In_ CxUiGrid* self)
{
    forgetFonts(self);
    repaint(self);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiGrid_eventMask(_In_ CxUiGrid* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Activate) | (1 << CXUI_EV_Change) | (1 << CXUI_EV_Wheel) |
           (1 << CXUI_EV_Sort) | (1 << CXUI_EV_Context) | (1 << CXUI_EV_Expand) |
           (1 << CXUI_EV_Check) | (1 << CXUI_EV_Edit);
}

extern CxUiResult CxUiWidget_handleEvent(_In_ CxUiWidget* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiWidget_handleEvent((CxUiWidget*)(self), ev)
CxUiResult CxUiGrid_handleEvent(_In_ CxUiGrid* self, _Inout_ CxUiEvent* ev)
{
    // A wheel turned the way the rows can't scroll is passed on, to a scroll box around the grid.
    if (ev->kind == CXUI_EV_Wheel) {
        CxUiWheelEvent* w = (CxUiWheelEvent*)ev;
        int64 rows        = (int64)llround(w->dy * GRID_WHEEL_ROWS);
        int32 across      = (int32)lround(w->dx * px(self, GRID_HSTEP));
        int64 maxTop      = max(self->_rows - self->_page, 0);
        int32 maxX        = max(self->_totalW - bodyRect(self).w, 0);
        bool can          = (w->dy > 0 && self->_top < maxTop) || (w->dy < 0 && self->_top > 0) ||
                   (w->dx > 0 && self->_scrollX < maxX) || (w->dx < 0 && self->_scrollX > 0);
        if (!can)
            return CXUI_Default;
        scrollTo(self, self->_top + (w->dy ? rows : 0), self->_scrollX + (w->dx ? across : 0));
        return CXUI_Handled;
    }
    // Opening a node of a tree is what activating it does, unless the program handles it.
    if (ev->kind == CXUI_EV_Activate && ev->target == CxUiWidget(self) && self->_tree) {
        CxUiNodeId node = (CxUiNodeId)((CxUiActivateEvent*)ev)->item;
        userExpand(self, node, !CxUiGrid_isExpanded(self, node));
        return CXUI_Handled;
    }
    // A check box changed by the user is set in the source, unless the program handles it.
    if (ev->kind == CXUI_EV_Check && ev->target == CxUiWidget(self)) {
        CxUiCheckEvent* ce = (CxUiCheckEvent*)ev;
        if (self->_edit)
            self->_edit->setCellCheck(self->_source, ce->item, ce->column, ce->state);
        return CXUI_Handled;
    }
    // What the user typed into a cell is set in the source if it changed, unless the program
    // handles it.
    if (ev->kind == CXUI_EV_Edit && ev->target == CxUiWidget(self)) {
        CxUiEditEvent* ee = (CxUiEditEvent*)ev;
        if (ee->stage != CXUI_EDIT_Commit || !self->_edit)
            return CXUI_Default;
        fetchCell(self, ee->item, ee->column);
        if (!strEq(self->_cell.text, ee->text))
            self->_edit->setCellText(self->_source, ee->item, ee->column, ee->text);
        return CXUI_Handled;
    }
    // Keys come up from the body, after the program's own handler for them on the grid; those
    // typed into the editor aren't the grid's.
    if (ev->kind == CXUI_EV_Key && ev->target == CxUiWidget(self->_body) && ((CxUiKeyEvent*)ev)->down &&
        keyDown(self, (CxUiKeyEvent*)ev))
        return CXUI_Handled;
    return parent_handleEvent(ev);
}

void CxUiGrid_destroy(_In_ CxUiGrid* self)
{
    unwatch(self);
    freeColumns(self);
    strDestroy(&self->_cell.text);
    xaDestroy(&self->_fit);
    // Autogen begins -----
    objRelease(&self->_source);
    objRelease(&self->_body);
    objRelease(&self->_vbar);
    objRelease(&self->_hbar);
    strDestroy(&self->_shown);
    objRelease(&self->_bold);
    saDestroy(&self->_sel);
    objRelease(&self->_autoscroll);
    strDestroy(&self->_typed);
    objRelease(&self->_editFrame);
    objRelease(&self->_editField);
    objRelease(&self->_clickTimer);
    // Autogen ends -------
}

// ---- CxUiGridEditor ----

_objfactory_guaranteed CxUiGridEditor* CxUiGridEditor_create(_In_ strref text)
{
    CxUiGridEditor* self;
    self = objInstCreate(CxUiGridEditor);
    // The frame gives it its width.
    CxUiTextField(self)->_columns = 1;
    strDup(&CxUiTextField(self)->_text, text);

    objInstInit(self);
    return self;
}

// Enter, Esc and Tab are the grid's, ahead of any handler the program has on the grid.
extern CxUiResult CxUiTextField_handleEvent(_In_ CxUiTextField* self, _Inout_ CxUiEvent* ev);   // parent
#undef parent_handleEvent
#define parent_handleEvent(ev) CxUiTextField_handleEvent((CxUiTextField*)(self), ev)
CxUiResult CxUiGridEditor_handleEvent(_In_ CxUiGridEditor* self, _Inout_ CxUiEvent* ev)
{
    CxUiWidget* frame = CxUiWidget(self)->parent;
    CxUiGrid* grid    = frame ? gridOf(frame) : NULL;
    if (!grid || grid->_editField != CxUiTextField(self))
        return parent_handleEvent(ev);
    if (ev->kind == CXUI_EV_Key && ((CxUiKeyEvent*)ev)->down) {
        // The event's path holds the grid while it closes the editor.
        CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
        uint32 mods     = k->mods & (CXUI_MOD_Shift | CXUI_MOD_Ctrl | CXUI_MOD_Alt | CXUI_MOD_Meta);
        switch (k->key) {
        case CXUI_KEY_Enter:
        case CXUI_KEY_PadEnter:
            if (mods)
                break;
            editEnd(grid, CXUI_EDIT_Commit);
            return CXUI_Handled;
        case CXUI_KEY_Escape:
            if (mods)
                break;
            editEnd(grid, CXUI_EDIT_Cancel);
            return CXUI_Handled;
        case CXUI_KEY_Tab:
            if (mods & ~CXUI_MOD_Shift)
                break;
            editTab(grid, mods != 0);
            return CXUI_Handled;
        default:
            break;
        }
    } else if (ev->kind == CXUI_EV_Focus && !((CxUiFocusEvent*)ev)->gained) {
        // Where focus went is known once the platform has finished moving it.
        cxuiPost(closureCreate(editFocusTask, stvar(object, grid), stvar(uint32, grid->_editSerial)));
    }
    return parent_handleEvent(ev);
}

bool CxUiGrid_edit(_In_ CxUiGrid* self, uint64 item, int32 col)
{
    if (!live(self))
        return false;
    int64 row = -1;
    if (self->_tree && item) {
        openAbove(self, item);
        row = treeRowOf(self, item, NULL);
    } else if (!self->_tree && item < (uint64)max(self->_rows, 0)) {
        row = (int64)item;
    }
    return row >= 0 && editStart(self, row, col);
}

void CxUiGrid_endEdit(_In_ CxUiGrid* self, bool commit)
{
    if (live(self))
        editEnd(self, commit ? CXUI_EDIT_Commit : CXUI_EDIT_Cancel);
}

_Ret_opt_valid_ CxUiTextField* CxUiGrid_editor(_In_ CxUiGrid* self)
{
    return live(self) ? self->_editField : NULL;
}

// Autogen begins -----
// clang-format off
#include "core/cxuigrid.auto.inc"
// clang-format on
// Autogen ends -------
