#include "gtk4_ui.h"

// A CxUiMenu is a GMenu, rebuilt whenever its items change, with a section for each run of items
// between separators. Every item that can be chosen has an action in one group that each window
// with a menu bar holds: stateless for a command, a boolean for a check item, and a string for a
// radio item, which GTK draws as one when the state equals the item's target. GTK can't gray an
// item that opens a menu; the items inside it are grayed instead. A menu bar can only hold items
// that open menus, so the others are left out of it.

#define MENU_PREFIX "cxuim"

typedef struct Gtk4MenuEntry {
    // Borrowed: the core takes the item out before it goes
    CxUiMenuItem* item;
    // NULL for a separator or an item that opens a menu
    GSimpleAction* action;
} Gtk4MenuEntry;

typedef struct Gtk4Menu {
    CxUiPeer base;
    GMenu* menu;
    bool bar;
    Gtk4MenuEntry* entries;
    int32 n;
} Gtk4Menu;

static GSimpleActionGroup* menuActions;
static uint64 actionSerial;

// ---- titles and hints ----

// & marks the mnemonic in cxui and _ in GTK; && is one &, and a _ of the title's own is doubled
static void gtkLabel(string* out, strref title)
{
    strClear(out);
    uint32 len = strLen(title);
    for (uint32 i = 0; i < len; i++) {
        char c = (char)strGetChar(title, (int32)i);
        if (c == '&') {
            if (i + 1 < len && strGetChar(title, (int32)i + 1) == '&') {
                strAppend(out, _S"&");
                i++;
            } else {
                strAppend(out, _S"_");
            }
        } else if (c == '_') {
            strAppend(out, _S"__");
        } else {
            char one[2] = { c, 0 };
            strAppend(out, (strref)one);
        }
    }
}

static void cxuiTitle(string* out, const char* label)
{
    strClear(out);
    for (const char* c = label; *c; c++) {
        if (*c == '_') {
            if (c[1] == '_') {
                strAppend(out, _S"_");
                c++;
            } else {
                strAppend(out, _S"&");
            }
        } else if (*c == '&') {
            strAppend(out, _S"&&");
        } else {
            char one[2] = { *c, 0 };
            strAppend(out, (strref)one);
        }
    }
}

static guint keyval(CxUiKey key)
{
    if (key >= CXUI_KEY_A && key <= CXUI_KEY_Z)
        return GDK_KEY_a + (key - CXUI_KEY_A);
    if (key >= CXUI_KEY_0 && key <= CXUI_KEY_9)
        return GDK_KEY_0 + (key - CXUI_KEY_0);
    if (key >= CXUI_KEY_F1 && key <= CXUI_KEY_F20)
        return GDK_KEY_F1 + (key - CXUI_KEY_F1);
    switch (key) {
    case CXUI_KEY_Tab:       return GDK_KEY_Tab;
    case CXUI_KEY_Enter:     return GDK_KEY_Return;
    case CXUI_KEY_Escape:    return GDK_KEY_Escape;
    case CXUI_KEY_Space:     return GDK_KEY_space;
    case CXUI_KEY_Backspace: return GDK_KEY_BackSpace;
    case CXUI_KEY_Insert:    return GDK_KEY_Insert;
    case CXUI_KEY_Delete:    return GDK_KEY_Delete;
    case CXUI_KEY_Home:      return GDK_KEY_Home;
    case CXUI_KEY_End:       return GDK_KEY_End;
    case CXUI_KEY_PageUp:    return GDK_KEY_Page_Up;
    case CXUI_KEY_PageDown:  return GDK_KEY_Page_Down;
    case CXUI_KEY_Left:      return GDK_KEY_Left;
    case CXUI_KEY_Right:     return GDK_KEY_Right;
    case CXUI_KEY_Up:        return GDK_KEY_Up;
    case CXUI_KEY_Down:      return GDK_KEY_Down;
    default:                 return 0;
    }
}

static CxUiKey keyOf(guint kv)
{
    for (int32 k = CXUI_KEY_None + 1; k < CXUI_KEY_Count; k++) {
        if (keyval((CxUiKey)k) == gdk_keyval_to_lower(kv))
            return (CxUiKey)k;
    }
    return CXUI_KEY_None;
}

// GTK shows a hint only as an accelerator, so one cxui can't read as a key shows nothing.
static char* gtkAccel(strref hint)
{
    CxUiKey key;
    uint32 mods;
    if (strEmpty(hint) || !cxuiKeyParse(&key, &mods, hint) || !keyval(key))
        return NULL;
    GdkModifierType gm = ((mods & (CXUI_MOD_Ctrl | CXUI_MOD_Primary)) ? GDK_CONTROL_MASK : 0) |
                         ((mods & CXUI_MOD_Shift) ? GDK_SHIFT_MASK : 0) | ((mods & CXUI_MOD_Alt) ? GDK_ALT_MASK : 0) |
                         ((mods & CXUI_MOD_Meta) ? GDK_META_MASK : 0);
    return gtk_accelerator_name(keyval(key), gm);
}

static void cxuiHint(string* out, const char* accel)
{
    guint kv           = 0;
    GdkModifierType gm = 0;
    strClear(out);
    if (!accel || !gtk_accelerator_parse(accel, &kv, &gm))
        return;
    uint32 mods = ((gm & GDK_CONTROL_MASK) ? CXUI_MOD_Ctrl : 0) | ((gm & GDK_SHIFT_MASK) ? CXUI_MOD_Shift : 0) |
                  ((gm & GDK_ALT_MASK) ? CXUI_MOD_Alt : 0) | ((gm & GDK_META_MASK) ? CXUI_MOD_Meta : 0);
    cxuiKeyFormat(out, keyOf(kv), mods);
}

// ---- actions ----

static void onActivate(GSimpleAction* action, GVariant* param, gpointer ud)
{
    unused_noeval(action);
    unused_noeval(param);
    _cxuiMenuChosen((CxUiMenuItem*)ud);
}

static void actionDrop(Gtk4MenuEntry* e)
{
    if (!e->action)
        return;
    g_signal_handlers_disconnect_by_data(e->action, e->item);
    g_action_map_remove_action(G_ACTION_MAP(menuActions), g_action_get_name(G_ACTION(e->action)));
    g_object_unref(e->action);
    e->action = NULL;
}

// The entry's action as the item now is: made for an item that can be chosen, its state and
// enabled state set
static void actionSet(Gtk4MenuEntry* e)
{
    CxUiMenuItem* item = e->item;
    bool choosable     = item->_kind != CXUI_MENU_Separator && !item->_submenu;
    const GVariantType* want = item->_kind == CXUI_MENU_Radio ? G_VARIANT_TYPE_STRING : NULL;
    bool stateful            = item->_kind == CXUI_MENU_Check || item->_kind == CXUI_MENU_Radio;
    // Kept only while it is the right sort
    if (e->action && (!choosable || (g_action_get_state_type(G_ACTION(e->action)) != NULL) != stateful ||
                      (g_action_get_parameter_type(G_ACTION(e->action)) != NULL) != (want != NULL)))
        actionDrop(e);
    if (!choosable)
        return;
    if (!e->action) {
        char name[24];
        snprintf(name, sizeof(name), "i%llu", (unsigned long long)++actionSerial);
        if (item->_kind == CXUI_MENU_Check)
            e->action = g_simple_action_new_stateful(name, NULL, g_variant_new_boolean(item->_checked));
        else if (item->_kind == CXUI_MENU_Radio)
            e->action = g_simple_action_new_stateful(name, G_VARIANT_TYPE_STRING,
                                                     g_variant_new_string(item->_checked ? "on" : "off"));
        else
            e->action = g_simple_action_new(name, NULL);
        g_signal_connect(e->action, "activate", G_CALLBACK(onActivate), item);
        g_action_map_add_action(G_ACTION_MAP(menuActions), G_ACTION(e->action));
    } else if (item->_kind == CXUI_MENU_Check) {
        g_simple_action_set_state(e->action, g_variant_new_boolean(item->_checked));
    } else if (item->_kind == CXUI_MENU_Radio) {
        g_simple_action_set_state(e->action, g_variant_new_string(item->_checked ? "on" : "off"));
    }
    g_simple_action_set_enabled(e->action, !(CxUiWidget(item)->state & CXUI_WS_Inactive));
}

// ---- the model ----

static void rebuild(Gtk4Menu* m)
{
    g_menu_remove_all(m->menu);
    GMenu* section = g_menu_new();
    string label   = 0;
    for (int32 i = 0; i <= m->n; i++) {
        Gtk4MenuEntry* e = i < m->n ? &m->entries[i] : NULL;
        // A menu bar shows only items that open menus.
        if (e && m->bar && !e->item->_submenu)
            continue;
        if (!e || e->item->_kind == CXUI_MENU_Separator) {
            // Every section goes in, empty ones too, so each separator is a section boundary.
            g_menu_append_section(m->menu, NULL, G_MENU_MODEL(section));
            g_object_unref(section);
            section = e ? g_menu_new() : NULL;
            continue;
        }
        gtkLabel(&label, e->item->_title);
        GMenuItem* gi = g_menu_item_new(strC(label), NULL);
        CxUiPeer* sub = e->item->_submenu ? CxUiWidget(e->item->_submenu)->peer : NULL;
        if (sub) {
            g_menu_item_set_submenu(gi, G_MENU_MODEL(((Gtk4Menu*)sub)->menu));
        } else if (e->action) {
            char detailed[40];
            snprintf(detailed, sizeof(detailed), MENU_PREFIX ".%s", g_action_get_name(G_ACTION(e->action)));
            if (e->item->_kind == CXUI_MENU_Radio)
                g_menu_item_set_action_and_target_value(gi, detailed, g_variant_new_string("on"));
            else
                g_menu_item_set_action_and_target_value(gi, detailed, NULL);
        }
        char* accel = gtkAccel(e->item->_hint);
        if (accel) {
            g_menu_item_set_attribute(gi, "accel", "s", accel);
            g_free(accel);
        }
        g_menu_append_item(section, gi);
        g_object_unref(gi);
    }
    strDestroy(&label);
}

CxUiPeer* _cxuiGtk4MenuCreate(CxUiMenu* m, bool bar)
{
    unused_noeval(m);
    if (!menuActions)
        menuActions = g_simple_action_group_new();
    Gtk4Menu* p  = xaAllocStruct(Gtk4Menu, XA_Zero);
    p->base.kind = CXUI_PEER_Menu;
    p->menu      = g_menu_new();
    p->bar       = bar;
    rebuild(p);
    return &p->base;
}

void _cxuiGtk4MenuInsert(CxUiPeer* peer, int32 at, CxUiMenuItem* item)
{
    Gtk4Menu* p = (Gtk4Menu*)peer;
    xaResize(&p->entries, sizeof(Gtk4MenuEntry) * (p->n + 1));
    memmove(&p->entries[at + 1], &p->entries[at], sizeof(Gtk4MenuEntry) * (p->n - at));
    p->entries[at] = (Gtk4MenuEntry){ .item = item };
    p->n++;
    actionSet(&p->entries[at]);
    rebuild(p);
}

void _cxuiGtk4MenuRemove(CxUiPeer* peer, int32 at)
{
    Gtk4Menu* p = (Gtk4Menu*)peer;
    actionDrop(&p->entries[at]);
    memmove(&p->entries[at], &p->entries[at + 1], sizeof(Gtk4MenuEntry) * (p->n - at - 1));
    p->n--;
    rebuild(p);
}

void _cxuiGtk4MenuUpdate(CxUiPeer* peer, int32 at, CxUiMenuItem* item)
{
    Gtk4Menu* p = (Gtk4Menu*)peer;
    p->entries[at].item = item;
    actionSet(&p->entries[at]);
    rebuild(p);
}

// The model is reference counted: a menu that still shows this one as a submenu keeps it.
void _cxuiGtk4MenuDestroy(CxUiPeer* peer)
{
    Gtk4Menu* p = (Gtk4Menu*)peer;
    for (int32 i = 0; i < p->n; i++)
        actionDrop(&p->entries[i]);
    g_object_unref(p->menu);
    xaFree(p->entries);
    xaFree(p);
}

_Use_decl_annotations_
GMenuModel* _cxuiGtk4MenuModel(CxUiPeer* menu)
{
    return G_MENU_MODEL(((Gtk4Menu*)menu)->menu);
}

_Use_decl_annotations_
void _cxuiGtk4MenuActionsTo(GtkWidget* win)
{
    if (menuActions)
        gtk_widget_insert_action_group(win, MENU_PREFIX, G_ACTION_GROUP(menuActions));
}

// ---- tests ----

// The section and place in it of position at; false for a separator, with *sec the section after,
// or for an item a menu bar leaves out
static bool locate(Gtk4Menu* p, int32 at, int32* sec, int32* idx)
{
    *sec = 0;
    *idx = 0;
    if (p->bar) {
        for (int32 i = 0; i < at; i++)
            *idx += p->entries[i].item->_submenu ? 1 : 0;
        return p->entries[at].item->_submenu != NULL;
    }
    for (int32 i = 0; i < at; i++) {
        if (p->entries[i].item->_kind == CXUI_MENU_Separator) {
            (*sec)++;
            *idx = 0;
        } else {
            (*idx)++;
        }
    }
    return p->entries[at].item->_kind != CXUI_MENU_Separator;
}

// Items read from the model itself: sections stand for separators
int32 _cxuiGtk4TestMenuCount(CxUiPeer* peer)
{
    GMenuModel* m = G_MENU_MODEL(((Gtk4Menu*)peer)->menu);
    int32 n       = g_menu_model_get_n_items(m);
    int32 count   = max(n - 1, 0);
    for (int32 s = 0; s < n; s++) {
        GMenuModel* sec = g_menu_model_get_item_link(m, s, G_MENU_LINK_SECTION);
        if (sec) {
            count += g_menu_model_get_n_items(sec);
            g_object_unref(sec);
        }
    }
    return count;
}

static GAction* actionOf(GMenuModel* sec, int32 idx)
{
    char* detailed = NULL;
    if (!g_menu_model_get_item_attribute(sec, idx, G_MENU_ATTRIBUTE_ACTION, "s", &detailed))
        return NULL;
    const char* dot = strchr(detailed, '.');
    GAction* a      = dot ? g_action_map_lookup_action(G_ACTION_MAP(menuActions), dot + 1) : NULL;
    g_free(detailed);
    return a;
}

bool _cxuiGtk4TestMenuItem(CxUiPeer* peer, int32 at, CxUiTestMenuItemInfo* out)
{
    Gtk4Menu* p = (Gtk4Menu*)peer;
    int32 s, idx;
    if (at < 0 || at >= p->n)
        return false;
    if (!locate(p, at, &s, &idx)) {
        if (p->bar)
            return false;
        out->kind    = CXUI_MENU_Separator;
        out->enabled = true;
        return true;
    }
    GMenuModel* sec = g_menu_model_get_item_link(G_MENU_MODEL(p->menu), s, G_MENU_LINK_SECTION);
    if (!sec)
        return false;
    char* label = NULL;
    char* accel = NULL;
    if (g_menu_model_get_item_attribute(sec, idx, G_MENU_ATTRIBUTE_LABEL, "s", &label))
        cxuiTitle(&out->title, label);
    if (g_menu_model_get_item_attribute(sec, idx, "accel", "s", &accel))
        cxuiHint(&out->hint, accel);
    g_free(label);
    g_free(accel);
    GMenuModel* sub = g_menu_model_get_item_link(sec, idx, G_MENU_LINK_SUBMENU);
    out->submenu    = sub != NULL;
    if (sub)
        g_object_unref(sub);
    GAction* a   = actionOf(sec, idx);
    out->enabled = !a || g_action_get_enabled(a);
    GVariant* st = a ? g_action_get_state(a) : NULL;
    out->kind    = !st ? CXUI_MENU_Command
                   : g_variant_is_of_type(st, G_VARIANT_TYPE_BOOLEAN) ? CXUI_MENU_Check
                                                                       : CXUI_MENU_Radio;
    out->checked = st && (g_variant_is_of_type(st, G_VARIANT_TYPE_BOOLEAN) ? g_variant_get_boolean(st)
                                                                            : strcmp(g_variant_get_string(st, NULL), "on") == 0);
    if (st)
        g_variant_unref(st);
    g_object_unref(sec);
    return true;
}

// Activates the item's action, as clicking it does; GTK leaves a disabled action alone.
bool _cxuiGtk4TestMenuChoose(CxUiPeer* peer, int32 at)
{
    Gtk4Menu* p = (Gtk4Menu*)peer;
    int32 s, idx;
    if (at < 0 || at >= p->n)
        return false;
    if (!locate(p, at, &s, &idx))
        return true;
    GMenuModel* sec = g_menu_model_get_item_link(G_MENU_MODEL(p->menu), s, G_MENU_LINK_SECTION);
    GAction* a      = sec ? actionOf(sec, idx) : NULL;
    if (a && g_action_get_enabled(a)) {
        GVariant* target = g_menu_model_get_item_attribute_value(sec, idx, G_MENU_ATTRIBUTE_TARGET, NULL);
        g_action_activate(a, target);
        if (target)
            g_variant_unref(target);
    }
    if (sec)
        g_object_unref(sec);
    return true;
}
