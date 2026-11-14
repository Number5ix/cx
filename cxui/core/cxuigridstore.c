// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuigridstore.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

#include <ctype.h>

// Opens n zeroed elements at `at` in any array
#define openGap(arr, at, n)                                                                        \
    do {                                                                                           \
        int32 sz_ = saSize(arr);                                                                   \
        saSetSize(&(arr), sz_ + (n));                                                              \
        memmove(&(arr).a[(at) + (n)], &(arr).a[at], (size_t)(sz_ - (at)) * sizeof((arr).a[0]));   \
        memset(&(arr).a[at], 0, (size_t)(n) * sizeof((arr).a[0]));                                 \
    } while (0)

// Closes n elements at `at`, whose contents are already destroyed or need none
#define closeGap(arr, at, n)                                                                       \
    do {                                                                                           \
        int32 sz_ = saSize(arr);                                                                   \
        memmove(&(arr).a[at], &(arr).a[(at) + (n)], (size_t)(sz_ - (at) - (n)) * sizeof((arr).a[0])); \
        memset(&(arr).a[sz_ - (n)], 0, (size_t)(n) * sizeof((arr).a[0]));                         \
        saSetSize(&(arr), sz_ - (n));                                                              \
    } while (0)

static int64 rows(CxUiListStore* self)
{
    return saSize(self->_flags);
}

static bool validRow(CxUiListStore* self, int64 row)
{
    return row >= 0 && row < rows(self);
}

static string* cellAt(CxUiListStore* self, int64 row, int32 col)
{
    return &self->_cells.a[row * self->_ncols + col];
}

_objfactory_guaranteed CxUiListStore* CxUiListStore_create(int32 ncols)
{
    CxUiListStore* self;
    self         = objInstCreate(CxUiListStore);
    self->_ncols = max(ncols, 1);

    objInstInit(self);
    return self;
}

_objinit_guaranteed bool CxUiListStore_init(_In_ CxUiListStore* self)
{
    // Autogen begins -----
    saInit(&self->_cells, string, 1);
    saInit(&self->_flags, uint32, 1);
    saInit(&self->_fg, uint32, 1);
    saInit(&self->_bg, uint32, 1);
    saInit(&self->_checks, uint8, 1);
    saInit(&self->_grids, ptr, 1);
    return true;
    // Autogen ends -------
}

_Use_decl_annotations_
void _cxuiListStoreWatch(CxUiListStore* self, CxUiGrid* g)
{
    if (saFind(self->_grids, ptr, g) < 0)
        saPush(&self->_grids, ptr, g);
}

_Use_decl_annotations_
void _cxuiListStoreUnwatch(CxUiListStore* self, CxUiGrid* g)
{
    saFindRemove(&self->_grids, ptr, g);
}

int32 CxUiListStore_columns(_In_ CxUiListStore* self)
{
    return self->_ncols;
}

int64 CxUiListStore_appendCells(_In_ CxUiListStore* self, _In_opt_ strref* cells, int32 n)
{
    return CxUiListStore_insertCells(self, rows(self), cells, n);
}

int64 CxUiListStore_insertCells(_In_ CxUiListStore* self, int64 at, _In_opt_ strref* cells, int32 n)
{
    at = clamp(at, 0, rows(self));
    relAssertMsg((rows(self) + 1) * self->_ncols < INT32_MAX, "too many cells in a list store");
    int32 first = (int32)at * self->_ncols;
    openGap(self->_cells, first, self->_ncols);
    openGap(self->_checks, first, self->_ncols);
    openGap(self->_flags, (int32)at, 1);
    openGap(self->_fg, (int32)at, 1);
    openGap(self->_bg, (int32)at, 1);
    for (int32 i = 0; i < min(n, self->_ncols); i++)
        strDup(&self->_cells.a[first + i], cells[i]);
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridRowsInserted((CxUiGrid*)self->_grids.a[i], at, 1);
    return at;
}

void CxUiListStore_remove(_In_ CxUiListStore* self, int64 at, int64 n)
{
    if (!validRow(self, at) || n <= 0)
        return;
    n           = min(n, rows(self) - at);
    int32 first = (int32)at * self->_ncols, ncells = (int32)n * self->_ncols;
    for (int32 i = 0; i < ncells; i++)
        strDestroy(&self->_cells.a[first + i]);
    closeGap(self->_cells, first, ncells);
    closeGap(self->_checks, first, ncells);
    closeGap(self->_flags, (int32)at, (int32)n);
    closeGap(self->_fg, (int32)at, (int32)n);
    closeGap(self->_bg, (int32)at, (int32)n);
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridRowsRemoved((CxUiGrid*)self->_grids.a[i], at, n);
}

void CxUiListStore_clear(_In_ CxUiListStore* self)
{
    saClear(&self->_cells);
    saClear(&self->_checks);
    saClear(&self->_flags);
    saClear(&self->_fg);
    saClear(&self->_bg);
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridReset((CxUiGrid*)self->_grids.a[i]);
}

static void changed(CxUiListStore* self, int64 row)
{
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridRowsChanged((CxUiGrid*)self->_grids.a[i], row, 1);
}

void CxUiListStore_setText(_In_ CxUiListStore* self, int64 row, int32 col, _In_ strref text)
{
    if (!validRow(self, row) || col < 0 || col >= self->_ncols || strEq(*cellAt(self, row, col), text))
        return;
    strDup(cellAt(self, row, col), text);
    changed(self, row);
}

strref CxUiListStore_text(_In_ CxUiListStore* self, int64 row, int32 col)
{
    if (!validRow(self, row) || col < 0 || col >= self->_ncols)
        return NULL;
    return *cellAt(self, row, col);
}

void CxUiListStore_setFlags(_In_ CxUiListStore* self, int64 row, flags_t flags)
{
    if (!validRow(self, row) || self->_flags.a[row] == flags)
        return;
    self->_flags.a[row] = flags;
    changed(self, row);
}

void CxUiListStore_setColors(_In_ CxUiListStore* self, int64 row, CxUiColor fg, CxUiColor bg)
{
    if (!validRow(self, row) || (self->_fg.a[row] == fg.bits && self->_bg.a[row] == bg.bits))
        return;
    self->_fg.a[row] = fg.bits;
    self->_bg.a[row] = bg.bits;
    changed(self, row);
}

// Compares text ignoring case, with runs of digits compared as numbers
static intptr naturalCmp(const char* a, const char* b)
{
    while (*a && *b) {
        if (isdigit((uint8)*a) && isdigit((uint8)*b)) {
            while (*a == '0')
                a++;
            while (*b == '0')
                b++;
            const char *ea = a, *eb = b;
            while (isdigit((uint8)*ea))
                ea++;
            while (isdigit((uint8)*eb))
                eb++;
            // More digits, once leading zeros are gone, is a larger number.
            if (ea - a != eb - b)
                return (ea - a) - (eb - b);
            for (; a < ea; a++, b++) {
                if (*a != *b)
                    return *a - *b;
            }
            continue;
        }
        int ca = tolower((uint8)*a), cb = tolower((uint8)*b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (intptr)(*a != 0) - (intptr)(*b != 0);
}

typedef struct SortBy {
    CxUiListStore* store;
    int32 col;
    bool descending;
} SortBy;

static intptr rowCmp(stype st, stgeneric g1, stgeneric g2, flags_t flags, void* ctx)
{
    SortBy* by = ctx;
    int32 r1 = g1.st_int32, r2 = g2.st_int32;
    intptr c = naturalCmp(strC(*cellAt(by->store, r1, by->col)), strC(*cellAt(by->store, r2, by->col)));
    if (by->descending)
        c = -c;
    // Ties go by the old order, which keeps the sort stable.
    return c ? c : (intptr)r1 - (intptr)r2;
}

// Puts each row's elem bytes in the order given
static void permute(void* data, int32 elem, const int32* order, int32 n)
{
    uint8* old = xaAlloc((size_t)elem * n);
    memcpy(old, data, (size_t)elem * n);
    for (int32 i = 0; i < n; i++)
        memcpy((uint8*)data + (size_t)i * elem, old + (size_t)order[i] * elem, elem);
    xaFree(old);
}

void CxUiListStore_sort(_In_ CxUiListStore* self, int32 col, bool descending)
{
    int32 n = (int32)rows(self);
    if (col < 0 || col >= self->_ncols || n <= 0)
        return;
    sa_int32 order;
    saInit(&order, int32, n);
    for (int32 i = 0; i < n; i++)
        saPush(&order, int32, i);
    SortBy by = { self, col, descending };
    saSortCustom(&order, rowCmp, &by);
    // The strings move by handle; none is copied or freed.
    permute(self->_cells.a, self->_ncols * (int32)sizeof(string), order.a, n);
    permute(self->_checks.a, self->_ncols, order.a, n);
    permute(self->_flags.a, sizeof(uint32), order.a, n);
    permute(self->_fg.a, sizeof(uint32), order.a, n);
    permute(self->_bg.a, sizeof(uint32), order.a, n);
    saDestroy(&order);
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridReset((CxUiGrid*)self->_grids.a[i]);
}

int64 CxUiListStore_rowCount(_In_ CxUiListStore* self)
{
    return rows(self);
}

bool CxUiListStore_cell(_In_ CxUiListStore* self, _Out_ CxUiCell* out, int64 row, int32 col)
{
    if (!validRow(self, row) || col < 0 || col >= self->_ncols)
        return false;
    strDup(&out->text, *cellAt(self, row, col));
    out->flags   = self->_flags.a[row];
    out->fg.bits = self->_fg.a[row];
    out->bg.bits = self->_bg.a[row];
    out->check   = self->_checks.a[row * self->_ncols + col];
    return true;
}

void CxUiListStore_destroy(_In_ CxUiListStore* self)
{
    // Autogen begins -----
    saDestroy(&self->_cells);
    saDestroy(&self->_flags);
    saDestroy(&self->_fg);
    saDestroy(&self->_bg);
    saDestroy(&self->_checks);
    saDestroy(&self->_grids);
    // Autogen ends -------
}

void CxUiListStore_setCheck(_In_ CxUiListStore* self, int64 row, int32 col, uint32 state)
{
    if (!validRow(self, row) || col < 0 || col >= self->_ncols || state > CXUI_CHECK_Mixed)
        return;
    uint8* at = &self->_checks.a[row * self->_ncols + col];
    if (*at == state)
        return;
    *at = (uint8)state;
    changed(self, row);
}

uint32 CxUiListStore_check(_In_ CxUiListStore* self, int64 row, int32 col)
{
    if (!validRow(self, row) || col < 0 || col >= self->_ncols)
        return CXUI_CHECK_Off;
    return self->_checks.a[row * self->_ncols + col];
}

bool CxUiListStore_setCellText(_In_ CxUiListStore* self, uint64 item, int32 col, _In_ strref text)
{
    if (!validRow(self, (int64)item) || col < 0 || col >= self->_ncols)
        return false;
    CxUiListStore_setText(self, (int64)item, col, text);
    return true;
}

bool CxUiListStore_setCellCheck(_In_ CxUiListStore* self, uint64 item, int32 col, uint32 state)
{
    if (!validRow(self, (int64)item) || col < 0 || col >= self->_ncols || state > CXUI_CHECK_Mixed)
        return false;
    CxUiListStore_setCheck(self, (int64)item, col, state);
    return true;
}

// Autogen begins -----
// clang-format off
#include "core/cxuigridstore.auto.inc"
// clang-format on
// Autogen ends -------
