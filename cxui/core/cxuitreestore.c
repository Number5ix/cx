// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuitreestore.h"
// clang-format on
// ==================== Auto-generated section ends ======================
#include "cxui_private.h"

// A node id is its slot's generation above its slot's index plus one, so 0 is never an id and an
// id from a freed slot no longer matches.
static CxUiNodeId makeId(int32 slot, uint32 gen)
{
    return ((uint64)gen << 32) | (uint32)(slot + 1);
}

// The node's slot, or NULL if the id names nothing
static CxUiTreeSlot* slotOf(CxUiTreeStore* self, CxUiNodeId id)
{
    int64 i = (int64)(uint32)id - 1;
    if (i < 0 || i >= self->_nslots)
        return NULL;
    CxUiTreeSlot* s = &self->_slots[i];
    return s->used && s->gen == (uint32)(id >> 32) ? s : NULL;
}

// The first and last child of a parent; 0 is the top of the tree
static bool childList(CxUiTreeStore* self, CxUiNodeId parent, CxUiNodeId** first, CxUiNodeId** last)
{
    if (parent == 0) {
        *first = &self->_first;
        *last  = &self->_last;
        return true;
    }
    CxUiTreeSlot* p = slotOf(self, parent);
    if (!p)
        return false;
    *first = &p->first;
    *last  = &p->last;
    return true;
}

static void notifyInserted(CxUiTreeStore* self, CxUiNodeId node)
{
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridNodeInserted((CxUiGrid*)self->_grids.a[i], node);
}

static void notifyChanged(CxUiTreeStore* self, CxUiNodeId node)
{
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridNodeChanged((CxUiGrid*)self->_grids.a[i], node);
}

// A slot for a new node, which may move the others
static int32 allocSlot(CxUiTreeStore* self)
{
    int32 i;
    if (self->_free >= 0) {
        i           = self->_free;
        self->_free = self->_slots[i].nextFree;
    } else {
        if (self->_nslots == self->_cap) {
            self->_cap = self->_cap ? self->_cap * 2 : 16;
            xaResize(&self->_slots, (size_t)self->_cap * sizeof(CxUiTreeSlot));
        }
        i = self->_nslots++;
        memset(&self->_slots[i], 0, sizeof(CxUiTreeSlot));
        self->_slots[i].gen = 1;
    }
    CxUiTreeSlot* s = &self->_slots[i];
    s->used         = true;
    s->cells        = xaAlloc((size_t)self->_ncols * sizeof(string), XA_Zero);
    s->checks       = xaAlloc((size_t)self->_ncols, XA_Zero);
    s->data         = stvNone;
    return i;
}

static void freeSlot(CxUiTreeStore* self, int32 i)
{
    CxUiTreeSlot* s = &self->_slots[i];
    for (int32 c = 0; c < self->_ncols; c++)
        strDestroy(&s->cells[c]);
    xaDestroy(&s->cells);
    xaDestroy(&s->checks);
    stvarDestroy(&s->data);
    uint32 gen = s->gen + 1;
    memset(s, 0, sizeof(*s));
    // Generation 0 is skipped, so a slot's ids stay apart from the ones before it wrapped.
    s->gen      = gen ? gen : 1;
    s->nextFree = self->_free;
    self->_free = i;
    self->_count--;
}

_objfactory_guaranteed CxUiTreeStore* CxUiTreeStore_create(int32 ncols)
{
    CxUiTreeStore* self;
    self = objInstCreate(CxUiTreeStore);
    self->_ncols = max(ncols, 1);
    self->_free  = -1;

    objInstInit(self);
    return self;
}

_objinit_guaranteed bool CxUiTreeStore_init(_In_ CxUiTreeStore* self)
{
    // Autogen begins -----
    saInit(&self->_grids, ptr, 1);
    return true;
    // Autogen ends -------
}

_Use_decl_annotations_
void _cxuiTreeStoreWatch(CxUiTreeStore* self, CxUiGrid* g)
{
    if (saFind(self->_grids, ptr, g) < 0)
        saPush(&self->_grids, ptr, g);
}

_Use_decl_annotations_
void _cxuiTreeStoreUnwatch(CxUiTreeStore* self, CxUiGrid* g)
{
    saFindRemove(&self->_grids, ptr, g);
}

int32 CxUiTreeStore_columns(_In_ CxUiTreeStore* self)
{
    return self->_ncols;
}

int64 CxUiTreeStore_nodeCount(_In_ CxUiTreeStore* self)
{
    return self->_count;
}

CxUiNodeId CxUiTreeStore_appendCells(_In_ CxUiTreeStore* self, CxUiNodeId parent, _In_opt_ strref* cells, int32 n)
{
    CxUiNodeId *first, *last;
    if (!childList(self, parent, &first, &last))
        return 0;
    return CxUiTreeStore_insertCells(self, parent, *last, cells, n);
}

CxUiNodeId CxUiTreeStore_insertCells(_In_ CxUiTreeStore* self, CxUiNodeId parent, CxUiNodeId after, _In_opt_ strref* cells,
                                     int32 n)
{
    CxUiNodeId *first, *last;
    if (!childList(self, parent, &first, &last))
        return 0;
    if (after) {
        CxUiTreeSlot* a = slotOf(self, after);
        if (!a || a->parent != parent)
            return 0;
    }
    relAssertMsg(self->_nslots < INT32_MAX - 1, "too many nodes in a tree store");

    int32 i         = allocSlot(self);
    CxUiTreeSlot* s = &self->_slots[i];
    CxUiNodeId id   = makeId(i, s->gen);
    for (int32 c = 0; c < min(n, self->_ncols); c++)
        strDup(&s->cells[c], cells[c]);
    s->parent = parent;
    // The slots may have moved, so the parent's list is found again; it was there above.
    if (!childList(self, parent, &first, &last))
        return 0;
    CxUiNodeId next = after ? slotOf(self, after)->next : *first;
    s->prev         = after;
    s->next         = next;
    if (after)
        slotOf(self, after)->next = id;
    else
        *first = id;
    if (next)
        slotOf(self, next)->prev = id;
    else
        *last = id;
    self->_count++;
    notifyInserted(self, id);
    return id;
}

bool CxUiTreeStore_remove(_In_ CxUiTreeStore* self, CxUiNodeId node)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s)
        return false;
    CxUiNodeId parent = s->parent, prev = s->prev, next = s->next;
    CxUiNodeId *first, *last;
    if (!childList(self, parent, &first, &last))
        return false;
    if (prev)
        slotOf(self, prev)->next = next;
    else
        *first = next;
    if (next)
        slotOf(self, next)->prev = prev;
    else
        *last = prev;

    // The subtree, depth first without recursion: each node's children are freed before it.
    CxUiNodeId at = node;
    while (at) {
        CxUiTreeSlot* cur = slotOf(self, at);
        if (cur->first) {
            CxUiNodeId child = cur->first;
            cur->first       = slotOf(self, child)->next;
            // Detached from its siblings, the child is visited next.
            slotOf(self, child)->next = 0;
            at                        = child;
            continue;
        }
        CxUiNodeId up = at == node ? 0 : cur->parent;
        freeSlot(self, (int32)(uint32)at - 1);
        at = up;
    }

    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridNodeRemoved((CxUiGrid*)self->_grids.a[i], parent, node);
    return true;
}

void CxUiTreeStore_clear(_In_ CxUiTreeStore* self)
{
    for (int32 i = 0; i < self->_nslots; i++) {
        if (self->_slots[i].used)
            freeSlot(self, i);
    }
    self->_first = self->_last = 0;
    self->_count = 0;
    for (int32 i = 0; i < saSize(self->_grids); i++)
        cxuiGridReset((CxUiGrid*)self->_grids.a[i]);
}

bool CxUiTreeStore_isValid(_In_ CxUiTreeStore* self, CxUiNodeId node)
{
    return slotOf(self, node) != NULL;
}

int64 CxUiTreeStore_childCount(_In_ CxUiTreeStore* self, CxUiNodeId parent)
{
    CxUiNodeId *first, *last;
    int64 n = 0;
    if (childList(self, parent, &first, &last)) {
        for (CxUiNodeId c = *first; c; c = slotOf(self, c)->next)
            n++;
    }
    return n;
}

void CxUiTreeStore_setText(_In_ CxUiTreeStore* self, CxUiNodeId node, int32 col, _In_ strref text)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s || col < 0 || col >= self->_ncols || strEq(s->cells[col], text))
        return;
    strDup(&s->cells[col], text);
    notifyChanged(self, node);
}

strref CxUiTreeStore_text(_In_ CxUiTreeStore* self, CxUiNodeId node, int32 col)
{
    CxUiTreeSlot* s = slotOf(self, node);
    return s && col >= 0 && col < self->_ncols ? s->cells[col] : NULL;
}

void CxUiTreeStore_setFlags(_In_ CxUiTreeStore* self, CxUiNodeId node, flags_t flags)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s || s->flags == flags)
        return;
    s->flags = flags;
    notifyChanged(self, node);
}

void CxUiTreeStore_setColors(_In_ CxUiTreeStore* self, CxUiNodeId node, CxUiColor fg, CxUiColor bg)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s || (s->fg == fg.bits && s->bg == bg.bits))
        return;
    s->fg = fg.bits;
    s->bg = bg.bits;
    notifyChanged(self, node);
}

void CxUiTreeStore_setData(_In_ CxUiTreeStore* self, CxUiNodeId node, stvar data)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s)
        return;
    stvarDestroy(&s->data);
    stvarCopy(&s->data, data);
}

stvar CxUiTreeStore_data(_In_ CxUiTreeStore* self, CxUiNodeId node)
{
    CxUiTreeSlot* s = slotOf(self, node);
    return s ? s->data : stvNone;
}

CxUiNodeId CxUiTreeStore_firstChild(_In_ CxUiTreeStore* self, CxUiNodeId parent)
{
    CxUiNodeId *first, *last;
    return childList(self, parent, &first, &last) ? *first : 0;
}

CxUiNodeId CxUiTreeStore_nextSibling(_In_ CxUiTreeStore* self, CxUiNodeId node)
{
    CxUiTreeSlot* s = slotOf(self, node);
    return s ? s->next : 0;
}

CxUiNodeId CxUiTreeStore_parentOf(_In_ CxUiTreeStore* self, CxUiNodeId node)
{
    CxUiTreeSlot* s = slotOf(self, node);
    return s ? s->parent : 0;
}

bool CxUiTreeStore_hasChildren(_In_ CxUiTreeStore* self, CxUiNodeId node)
{
    CxUiTreeSlot* s = slotOf(self, node);
    return s && s->first;
}

bool CxUiTreeStore_nodeCell(_In_ CxUiTreeStore* self, _Out_ CxUiCell* out, CxUiNodeId node, int32 col)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s || col < 0 || col >= self->_ncols)
        return false;
    strDup(&out->text, s->cells[col]);
    out->flags   = s->flags;
    out->fg.bits = s->fg;
    out->bg.bits = s->bg;
    out->check   = s->checks[col];
    return true;
}

void CxUiTreeStore_destroy(_In_ CxUiTreeStore* self)
{
    for (int32 i = 0; i < self->_nslots; i++) {
        if (self->_slots[i].used)
            freeSlot(self, i);
    }
    xaDestroy(&self->_slots);
    // Autogen begins -----
    saDestroy(&self->_grids);
    // Autogen ends -------
}

void CxUiTreeStore_setCheck(_In_ CxUiTreeStore* self, CxUiNodeId node, int32 col, uint32 state)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s || col < 0 || col >= self->_ncols || state > CXUI_CHECK_Mixed || s->checks[col] == state)
        return;
    s->checks[col] = (uint8)state;
    notifyChanged(self, node);
}

uint32 CxUiTreeStore_check(_In_ CxUiTreeStore* self, CxUiNodeId node, int32 col)
{
    CxUiTreeSlot* s = slotOf(self, node);
    return s && col >= 0 && col < self->_ncols ? s->checks[col] : CXUI_CHECK_Off;
}

void CxUiTreeStore_propagateCheck(_In_ CxUiTreeStore* self, CxUiNodeId node, int32 col)
{
    CxUiTreeSlot* s = slotOf(self, node);
    if (!s || col < 0 || col >= self->_ncols)
        return;
    uint32 state = s->checks[col];

    // The nodes under it, depth first
    CxUiNodeId at = state != CXUI_CHECK_Mixed ? s->first : 0;
    while (at) {
        CxUiTreeStore_setCheck(self, at, col, state);
        CxUiTreeSlot* cur = slotOf(self, at);
        if (cur->first) {
            at = cur->first;
            continue;
        }
        // On to the next sibling of the nearest node with one, short of where it started
        while (at != node && !slotOf(self, at)->next)
            at = slotOf(self, at)->parent;
        at = at == node ? 0 : slotOf(self, at)->next;
    }

    // The nodes above it, from what their shown children say
    for (CxUiNodeId p = s->parent; p; p = slotOf(self, p)->parent) {
        bool on = false, off = false, mixed = false;
        for (CxUiNodeId c = slotOf(self, p)->first; c; c = slotOf(self, c)->next) {
            CxUiTreeSlot* cs = slotOf(self, c);
            if (cs->flags & CXUI_CELL_CheckHidden)
                continue;
            on |= cs->checks[col] == CXUI_CHECK_On;
            off |= cs->checks[col] == CXUI_CHECK_Off;
            mixed |= cs->checks[col] == CXUI_CHECK_Mixed;
        }
        if (!on && !off && !mixed)
            break;
        CxUiTreeStore_setCheck(self, p, col, mixed || (on && off) ? CXUI_CHECK_Mixed : on ? CXUI_CHECK_On : CXUI_CHECK_Off);
    }
}

bool CxUiTreeStore_setCellText(_In_ CxUiTreeStore* self, uint64 item, int32 col, _In_ strref text)
{
    if (!slotOf(self, item) || col < 0 || col >= self->_ncols)
        return false;
    CxUiTreeStore_setText(self, item, col, text);
    return true;
}

bool CxUiTreeStore_setCellCheck(_In_ CxUiTreeStore* self, uint64 item, int32 col, uint32 state)
{
    if (!slotOf(self, item) || col < 0 || col >= self->_ncols || state > CXUI_CHECK_Mixed)
        return false;
    CxUiTreeStore_setCheck(self, item, col, state);
    return true;
}

// Autogen begins -----
// clang-format off
#include "core/cxuitreestore.auto.inc"
// clang-format on
// Autogen ends -------
