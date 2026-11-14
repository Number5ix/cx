// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "core/cxuimenu.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "cxui_private.h"

bool CxUiMenu_append(_In_ CxUiMenu* self, _In_ CxUiWidget* child);
bool CxUiMenu_insert(_In_ CxUiMenu* self, _In_ CxUiWidget* child, int32 at);

static bool live(void* w)
{
    return _cxuiLive((CxUiWidget*)w);
}

static CxUiPeer* menuPeer(CxUiMenu* m)
{
    return m ? CxUiWidget(m)->peer : NULL;
}

static CxUiMenu* parentMenu(CxUiMenuItem* item)
{
    CxUiWidget* parent = CxUiWidget(item)->parent;
    return parent ? objDynCast(CxUiMenu, parent) : NULL;
}

static int32 indexIn(CxUiMenu* m, CxUiMenuItem* item)
{
    return m ? saFind(m->_items, object, item) : -1;
}

// Tells the platform an item changed: its title, hint, check mark, enabled state or submenu
static void itemChanged(CxUiMenuItem* item)
{
    CxUiMenu* m  = parentMenu(item);
    CxUiPeer* p  = menuPeer(m);
    int32 at     = indexIn(m, item);
    if (p && at >= 0 && _cxui.backend->menuUpdate)
        _cxui.backend->menuUpdate(p, at, item);
}

// ---- realizing ----

_Use_decl_annotations_
void _cxuiMenuRealize(CxUiMenu* m, bool bar)
{
    if (!live(m) || menuPeer(m) || !_cxui.backend->menuCreate)
        return;
    CxUiPeer* p = _cxui.backend->menuCreate(m, bar);
    if (!p)
        return;
    _cxuiPeerAttach(CxUiWidget(m), p);
    for (int32 i = 0; i < saSize(m->_items); i++) {
        CxUiMenuItem* item = (CxUiMenuItem*)m->_items.a[i];
        if (item->_submenu)
            _cxuiMenuRealize(item->_submenu, false);
        _cxui.backend->menuInsert(p, i, item);
    }
}

// The menu first, so a platform that destroys submenus along with their menu has let go of them
_Use_decl_annotations_
void _cxuiMenuUnrealize(CxUiMenu* m)
{
    _cxuiUnrealize(CxUiWidget(m));
    for (int32 i = 0; i < saSize(m->_items); i++) {
        CxUiMenuItem* item = (CxUiMenuItem*)m->_items.a[i];
        if (item->_submenu)
            _cxuiMenuUnrealize(item->_submenu);
    }
}

_Use_decl_annotations_
void _cxuiMenuChosen(CxUiMenuItem* item)
{
    _cxuiNativeEnter();
    if (live(item))
        CxUiMenuItem_choose(item);
    _cxuiNativeLeave();
}

// ---- CxUiMenu ----

_objfactory_guaranteed CxUiMenu* CxUiMenu_create()
{
    CxUiMenu* self;
    self = objInstCreate(CxUiMenu);
    objInstInit(self);
    return self;
}

_objinit_guaranteed bool CxUiMenu_init(_In_ CxUiMenu* self)
{
    saInit(&self->_items, object, 8);
    // Autogen begins -----
    return true;
    // Autogen ends -------
}

CxUiMenuItem* CxUiMenu_appendItemWith(_In_ CxUiMenu* self, _In_ const CxUiMenuItemDesc* desc)
{
    if (!live(self))
        return NULL;
    CxUiMenuItem* item = CxUiMenuItem_createWith(desc);
    bool added         = CxUiMenu_append(self, CxUiWidget(item));
    CxUiMenuItem* ret  = added ? item : NULL;
    objRelease(&item);
    return ret;
}

void CxUiMenu_appendSeparator(_In_ CxUiMenu* self)
{
    CxUiMenu_appendItemWith(self, &(CxUiMenuItemDesc){ .kind = CXUI_MENU_Separator });
}

void CxUiMenu_clear(_In_ CxUiMenu* self)
{
    for (int32 i = live(self) ? saSize(self->_items) - 1 : -1; i >= 0; i--)
        _cxuiDestroy(self->_items.a[i]);
}

bool CxUiMenu_append(_In_ CxUiMenu* self, _In_ CxUiWidget* child)
{
    return CxUiMenu_insert(self, child, live(self) ? saSize(self->_items) : 0);
}

bool CxUiMenu_insert(_In_ CxUiMenu* self, _In_ CxUiWidget* child, int32 at)
{
    CxUiMenuItem* item = child ? objDynCast(CxUiMenuItem, child) : NULL;
    if (!live(self) || !item || !live(item) || child->parent)
        return false;
    at            = clamp(at, 0, saSize(self->_items));
    child->parent = CxUiWidget(self);
    // Before it is in the list, so the item doesn't tell the platform about itself yet
    _cxuiLayoutEnabledChanged(child);
    saInsert(&self->_items, at, object, child);
    CxUiPeer* p = menuPeer(self);
    if (p) {
        if (item->_submenu)
            _cxuiMenuRealize(item->_submenu, false);
        _cxui.backend->menuInsert(p, at, item);
    }
    return true;
}

bool CxUiMenu_detach(_In_ CxUiMenu* self, _In_ CxUiWidget* child)
{
    int32 at = saFind(self->_items, object, child);
    if (at < 0)
        return false;
    CxUiMenuItem* item = (CxUiMenuItem*)child;
    // The list's reference keeps the item alive until it is unlinked.
    objAcquire(child);
    CxUiPeer* p = menuPeer(self);
    if (p)
        _cxui.backend->menuRemove(p, at);
    saRemove(&self->_items, at);
    child->parent = NULL;
    if (item->_submenu)
        _cxuiMenuUnrealize(item->_submenu);
    _cxuiLayoutEnabledChanged(child);
    objRelease(&child);
    return true;
}

int32 CxUiMenu_childCount(_In_ CxUiMenu* self)
{
    return saSize(self->_items);
}

_Ret_opt_valid_ CxUiWidget* CxUiMenu_child(_In_ CxUiMenu* self, int32 i)
{
    return i >= 0 && i < saSize(self->_items) ? self->_items.a[i] : NULL;
}

void CxUiMenu_destroy(_In_ CxUiMenu* self)
{
    // Autogen begins -----
    saDestroy(&self->_items);
    // Autogen ends -------
}

// ---- CxUiMenuItem ----

_objfactory_guaranteed CxUiMenuItem* CxUiMenuItem_createWith(_In_opt_ const CxUiMenuItemDesc* desc)
{
    CxUiMenuItem* self;
    self = objInstCreate(CxUiMenuItem);
    if (desc) {
        strDup(&self->_title, desc->title);
        if (!strEmpty(desc->hint))
            strDup(&self->_hint, desc->hint);
        self->_kind    = desc->kind <= CXUI_MENU_Separator ? desc->kind : CXUI_MENU_Command;
        self->_checked = desc->checked && (self->_kind == CXUI_MENU_Check || self->_kind == CXUI_MENU_Radio);
        strDup(&CxUiWidget(self)->name, desc->name);
    }
    objInstInit(self);
    if (desc && desc->submenu && self->_kind == CXUI_MENU_Command)
        CxUiMenuItem_setSubmenu(self, desc->submenu);
    return self;
}

strref CxUiMenuItem_title(_In_ CxUiMenuItem* self)
{
    return live(self) ? self->_title : NULL;
}

void CxUiMenuItem_setTitle(_In_ CxUiMenuItem* self, _In_ strref title)
{
    if (!live(self) || strEq(title, self->_title))
        return;
    strDup(&self->_title, title);
    itemChanged(self);
}

strref CxUiMenuItem_hint(_In_ CxUiMenuItem* self)
{
    return live(self) ? self->_hint : NULL;
}

void CxUiMenuItem_setHint(_In_ CxUiMenuItem* self, _In_opt_ strref hint)
{
    if (!live(self) || strEq(hint, self->_hint))
        return;
    if (strEmpty(hint))
        strDestroy(&self->_hint);
    else
        strDup(&self->_hint, hint);
    itemChanged(self);
}

uint32 CxUiMenuItem_kind(_In_ CxUiMenuItem* self)
{
    return self->_kind;
}

bool CxUiMenuItem_checked(_In_ CxUiMenuItem* self)
{
    return live(self) && self->_checked;
}

// Clears the other radio items of item's group: the radio items next to it, up to the first item
// of another kind each way
static void radioSiblingsClear(CxUiMenuItem* item)
{
    CxUiMenu* m = parentMenu(item);
    int32 at    = indexIn(m, item);
    if (at < 0)
        return;
    for (int32 dir = -1; dir <= 1; dir += 2) {
        for (int32 i = at + dir; i >= 0 && i < saSize(m->_items); i += dir) {
            CxUiMenuItem* other = (CxUiMenuItem*)m->_items.a[i];
            if (other->_kind != CXUI_MENU_Radio)
                break;
            if (other->_checked) {
                other->_checked = false;
                itemChanged(other);
            }
        }
    }
}

void CxUiMenuItem_setChecked(_In_ CxUiMenuItem* self, bool checked)
{
    if (!live(self) || (self->_kind != CXUI_MENU_Check && self->_kind != CXUI_MENU_Radio))
        return;
    if (checked && self->_kind == CXUI_MENU_Radio)
        radioSiblingsClear(self);
    if (checked == self->_checked)
        return;
    self->_checked = checked;
    itemChanged(self);
}

CxUiMenu* CxUiMenuItem_submenu(_In_ CxUiMenuItem* self)
{
    return live(self) ? self->_submenu : NULL;
}

void CxUiMenuItem_setSubmenu(_In_ CxUiMenuItem* self, _In_opt_ CxUiMenu* menu)
{
    if (!live(self) || self->_kind != CXUI_MENU_Command || menu == self->_submenu ||
        (menu && (!live(menu) || CxUiWidget(menu)->parent)))
        return;
    CxUiMenu* old  = self->_submenu;
    self->_submenu = menu ? objAcquire(menu) : NULL;
    if (menu) {
        CxUiWidget(menu)->parent = CxUiWidget(self);
        _cxuiLayoutEnabledChanged(CxUiWidget(menu));
        if (menuPeer(parentMenu(self)))
            _cxuiMenuRealize(menu, false);
    }
    // The platform takes the new menu, or lets go of the old one, before the old one goes.
    itemChanged(self);
    if (old) {
        CxUiWidget(old)->parent = NULL;
        _cxuiMenuUnrealize(old);
        _cxuiLayoutEnabledChanged(CxUiWidget(old));
        objRelease(&old);
    }
}

void CxUiMenuItem_choose(_In_ CxUiMenuItem* self)
{
    if (!live(self) || (CxUiWidget(self)->state & CXUI_WS_Inactive) || self->_kind == CXUI_MENU_Separator ||
        self->_submenu)
        return;
    objAcquire(self);
    if (self->_kind == CXUI_MENU_Check)
        CxUiMenuItem_setChecked(self, !self->_checked);
    else if (self->_kind == CXUI_MENU_Radio)
        CxUiMenuItem_setChecked(self, true);
    CxUiActivateEvent ev = { .base.kind = CXUI_EV_Activate };
    _cxuiDispatch(CxUiWidget(self), &ev.base);
    objRelease(&self);
}

extern uint32 CxUiWidget_eventMask(_In_ CxUiWidget* self);   // parent
#define parent_eventMask() CxUiWidget_eventMask((CxUiWidget*)(self))
uint32 CxUiMenuItem_eventMask(_In_ CxUiMenuItem* self)
{
    return parent_eventMask() | (1 << CXUI_EV_Activate);
}

// The enabled state changed, as restyling follows it.
extern void CxUiWidget_restyle(_In_ CxUiWidget* self);   // parent
#define parent_restyle() CxUiWidget_restyle((CxUiWidget*)(self))
void CxUiMenuItem_restyle(_In_ CxUiMenuItem* self)
{
    itemChanged(self);
}

bool CxUiMenuItem_append(_In_ CxUiMenuItem* self, _In_ CxUiWidget* child)
{
    CxUiMenu* m = child ? objDynCast(CxUiMenu, child) : NULL;
    if (!m || self->_submenu)
        return false;
    CxUiMenuItem_setSubmenu(self, m);
    return self->_submenu == m;
}

bool CxUiMenuItem_insert(_In_ CxUiMenuItem* self, _In_ CxUiWidget* child, int32 at)
{
    unused_noeval(at);
    return CxUiMenuItem_append(self, child);
}

bool CxUiMenuItem_detach(_In_ CxUiMenuItem* self, _In_ CxUiWidget* child)
{
    if (!child || child != CxUiWidget(self->_submenu))
        return false;
    // A dead item is detaching its submenu as both are destroyed; there is no platform item left.
    CxUiMenu* old  = self->_submenu;
    self->_submenu = NULL;
    if (live(self))
        itemChanged(self);
    CxUiWidget(old)->parent = NULL;
    _cxuiMenuUnrealize(old);
    _cxuiLayoutEnabledChanged(CxUiWidget(old));
    objRelease(&old);
    return true;
}

int32 CxUiMenuItem_childCount(_In_ CxUiMenuItem* self)
{
    return self->_submenu ? 1 : 0;
}

_Ret_opt_valid_ CxUiWidget* CxUiMenuItem_child(_In_ CxUiMenuItem* self, int32 i)
{
    return i == 0 && self->_submenu ? CxUiWidget(self->_submenu) : NULL;
}

void CxUiMenuItem_destroy(_In_ CxUiMenuItem* self)
{
    // Autogen begins -----
    strDestroy(&self->_title);
    strDestroy(&self->_hint);
    objRelease(&self->_submenu);
    // Autogen ends -------
}

// Autogen begins -----
// clang-format off
#include "core/cxuimenu.auto.inc"
// clang-format on
// Autogen ends -------
