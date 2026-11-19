#include <cxuilua.h>

#include <cx/string.h>
#include <cxui.h>

// Widgets are held with a reference, like other objects; a destroyed one is refused by the alive
// hook. Event handlers are closures around the Lua function, which stays in a table of the
// widget's handlers so its `on` field reads back.

static char handlersKey;

// ---- reading tables ----

// The value of t[key] pushed; false, with nothing pushed, if it is nil
static bool optField(lua_State* L, int t, const char* key)
{
    if (lua_getfield(L, t, key) != LUA_TNIL)
        return true;
    lua_pop(L, 1);
    return false;
}

static void optString(lua_State* L, int t, const char* key, string* out)
{
    if (!optField(L, t, key))
        return;
    size_t len    = 0;
    const char* s = luaL_checklstring(L, -1, &len);
    strFromBytes(out, s, (uint32)len);
    lua_pop(L, 1);
}

static lua_Integer optInt(lua_State* L, int t, const char* key, lua_Integer def)
{
    if (!optField(L, t, key))
        return def;
    lua_Integer v = luaL_checkinteger(L, -1);
    lua_pop(L, 1);
    return v;
}

static lua_Number optNum(lua_State* L, int t, const char* key, lua_Number def)
{
    if (!optField(L, t, key))
        return def;
    lua_Number v = luaL_checknumber(L, -1);
    lua_pop(L, 1);
    return v;
}

static bool optBool(lua_State* L, int t, const char* key, bool def)
{
    if (!optField(L, t, key))
        return def;
    bool v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

// A size table at idx: { w, h } or { cw, ch }, each axis 0 when left out
static void toLens(lua_State* L, int idx, CxUiLen out[2])
{
    luaL_checktype(L, idx, LUA_TTABLE);
    static const char* const dips[]  = { "w", "h" };
    static const char* const chars[] = { "cw", "ch" };
    idx = lua_absindex(L, idx);
    for (int a = 0; a < 2; a++) {
        lua_Integer v = optInt(L, idx, chars[a], 0);
        out[a]        = v ? cxuiChars((int32)v) : cxuiLen((int32)optInt(L, idx, dips[a], 0));
    }
}

static CxUiSize optSize(lua_State* L, int t, const char* key)
{
    CxUiSize s = { 0 };
    if (!optField(L, t, key))
        return s;
    luaL_checktype(L, -1, LUA_TTABLE);
    s.w = (int32)optInt(L, lua_gettop(L), "w", 0);
    s.h = (int32)optInt(L, lua_gettop(L), "h", 0);
    lua_pop(L, 1);
    return s;
}

// A number for every side, or { left, top, right, bottom }
static CxUiEdges optEdges(lua_State* L, int t, const char* key)
{
    CxUiEdges e = { 0 };
    if (!optField(L, t, key))
        return e;
    if (lua_isnumber(L, -1)) {
        int32 v = (int32)lua_tointeger(L, -1);
        e       = (CxUiEdges){ v, v, v, v };
    } else {
        luaL_checktype(L, -1, LUA_TTABLE);
        int32* sides[] = { &e.left, &e.top, &e.right, &e.bottom };
        for (int i = 0; i < 4; i++) {
            lua_rawgeti(L, -1, i + 1);
            *sides[i] = (int32)luaL_optinteger(L, -1, 0);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return e;
}

static uint32 toExpand(lua_State* L, int idx)
{
    if (lua_isnumber(L, idx))
        return (uint32)lua_tointeger(L, idx);
    static const char* const names[] = { "none", "h", "v", "both", NULL };
    return (uint32)luaL_checkoption(L, idx, NULL, names);
}

static uint32 toAlign(lua_State* L, int idx)
{
    if (lua_isnumber(L, idx))
        return (uint32)lua_tointeger(L, idx);
    static const char* const names[] = { "default", "start", "center", "end", "fill", NULL };
    return (uint32)luaL_checkoption(L, idx, NULL, names);
}

// align = "center", or { horizontal, vertical }
static void toAligns(lua_State* L, int idx, uint8 out[2])
{
    if (!lua_istable(L, idx)) {
        out[0] = out[1] = (uint8)toAlign(L, idx);
        return;
    }
    idx = lua_absindex(L, idx);
    for (int a = 0; a < 2; a++) {
        lua_rawgeti(L, idx, a + 1);
        out[a] = lua_isnil(L, -1) ? CXUI_ALIGN_Default : (uint8)toAlign(L, -1);
        lua_pop(L, 1);
    }
}

// Applies a layout key's value at idx; false if key isn't one
static bool layoutKey(lua_State* L, const char* key, int idx, CxUiLayoutParams* lp)
{
    if (!strcmp(key, "expand"))
        lp->expand = (uint8)toExpand(L, idx);
    else if (!strcmp(key, "align"))
        toAligns(L, idx, lp->align);
    else if (!strcmp(key, "size"))
        toLens(L, idx, lp->size);
    else if (!strcmp(key, "minsize"))
        toLens(L, idx, lp->minSize);
    else if (!strcmp(key, "normalize"))
        lp->normalizeGroup = (uint8)luaL_checkinteger(L, idx);
    else if (!strcmp(key, "eager"))
        lp->flags = (uint8)((lp->flags & ~CXUI_LP_RealizeEager) | (lua_toboolean(L, idx) ? CXUI_LP_RealizeEager : 0));
    else
        return false;
    return true;
}

static const char* const layoutKeys[] = { "expand", "align", "size", "minsize", "normalize", "eager", "name", NULL };

static void readLayout(lua_State* L, int t, CxUiLayoutParams* lp)
{
    *lp = (CxUiLayoutParams){ 0 };
    for (int i = 0; layoutKeys[i]; i++) {
        if (optField(L, t, layoutKeys[i])) {
            layoutKey(L, layoutKeys[i], lua_gettop(L), lp);
            lua_pop(L, 1);
        }
    }
}

static bool listed(const char* const* keys, const char* key)
{
    for (; keys && *keys; keys++) {
        if (!strcmp(*keys, key))
            return true;
    }
    return false;
}

// ---- events ----

static CxUiResult runHandler(stvlist* cvars, CxUiEvent* ev);

#define EV_TRAMP(Kind)                                                          \
    static CxUiResult tramp##Kind(stvlist* cvars, CxUi##Kind##Event* ev)        \
    {                                                                           \
        return runHandler(cvars, &ev->base);                                    \
    }
EV_TRAMP(Activate)
EV_TRAMP(Change)
EV_TRAMP(Commit)
EV_TRAMP(Key)
EV_TRAMP(Mouse)
EV_TRAMP(Wheel)
EV_TRAMP(Focus)
EV_TRAMP(Resize)
EV_TRAMP(Close)
EV_TRAMP(ShowState)
EV_TRAMP(Move)
EV_TRAMP(DpiChanged)
EV_TRAMP(ThemeChanged)
EV_TRAMP(Scroll)
EV_TRAMP(Sort)
EV_TRAMP(Context)
EV_TRAMP(Expand)
EV_TRAMP(Check)
EV_TRAMP(Edit)
EV_TRAMP(Surface)
EV_TRAMP(Text)

static CxUiResult trampPaint(stvlist* cvars, CxUiPaintEvent* ev);

typedef struct EvSlot {
    const char* field;
    CxUiEventKind kind;
} EvSlot;

static const EvSlot evSlots[] = {
    { "onactivate", CXUI_EV_Activate },         { "onchange", CXUI_EV_Change },
    { "oncommit", CXUI_EV_Commit },             { "onkey", CXUI_EV_Key },
    { "onmouse", CXUI_EV_Mouse },               { "onwheel", CXUI_EV_Wheel },
    { "onfocus", CXUI_EV_Focus },               { "onresize", CXUI_EV_Resize },
    { "onclose", CXUI_EV_Close },               { "onshowstate", CXUI_EV_ShowState },
    { "onmove", CXUI_EV_Move },                 { "ondpichanged", CXUI_EV_DpiChanged },
    { "onthemechanged", CXUI_EV_ThemeChanged }, { "onscroll", CXUI_EV_Scroll },
    { "onpaint", CXUI_EV_Paint },               { "onsort", CXUI_EV_Sort },
    { "oncontext", CXUI_EV_Context },           { "onexpand", CXUI_EV_Expand },
    { "oncheck", CXUI_EV_Check },
    { "onedit", CXUI_EV_Edit },
    { "onsurface", CXUI_EV_Surface },
    { "ontext", CXUI_EV_Text },
};

static const EvSlot* slotFor(const char* key)
{
    for (size_t i = 0; i < sizeof(evSlots) / sizeof(evSlots[0]); i++) {
        if (!strcmp(evSlots[i].field, key))
            return &evSlots[i];
    }
    return NULL;
}

static void setInt(lua_State* L, const char* key, lua_Integer v)
{
    lua_pushinteger(L, v);
    lua_setfield(L, -2, key);
}

static void setNum(lua_State* L, const char* key, lua_Number v)
{
    lua_pushnumber(L, v);
    lua_setfield(L, -2, key);
}

static void setBool(lua_State* L, const char* key, bool v)
{
    lua_pushboolean(L, v);
    lua_setfield(L, -2, key);
}

// The event as a table; it can be kept, unlike the event itself
static void pushEvent(lua_State* L, CxUiEvent* ev)
{
    lua_newtable(L);
    cxluaPushObject(L, objInstBase(ev->target));
    lua_setfield(L, -2, "target");
    cxluaPushObject(L, ev->window ? objInstBase(ev->window) : NULL);
    lua_setfield(L, -2, "window");
    switch (ev->kind) {
    case CXUI_EV_Activate:
        setInt(L, "item", (lua_Integer)((CxUiActivateEvent*)ev)->item);
        break;
    case CXUI_EV_Key: {
        CxUiKeyEvent* k = (CxUiKeyEvent*)ev;
        setInt(L, "key", k->key);
        setInt(L, "mods", k->mods);
        setInt(L, "scancode", k->scancode);
        setBool(L, "down", k->down);
        setBool(L, "repeat", k->repeat);
        break;
    }
    case CXUI_EV_Mouse: {
        CxUiMouseEvent* m = (CxUiMouseEvent*)ev;
        setInt(L, "action", m->action);
        setInt(L, "button", m->button);
        setInt(L, "buttons", m->buttons);
        setInt(L, "mods", m->mods);
        setInt(L, "flags", m->flags);
        setInt(L, "x", m->x);
        setInt(L, "y", m->y);
        setNum(L, "dx", m->dx);
        setNum(L, "dy", m->dy);
        break;
    }
    case CXUI_EV_Wheel: {
        CxUiWheelEvent* w = (CxUiWheelEvent*)ev;
        setNum(L, "dx", w->dx);
        setNum(L, "dy", w->dy);
        setInt(L, "mods", w->mods);
        setInt(L, "x", w->x);
        setInt(L, "y", w->y);
        break;
    }
    case CXUI_EV_Focus:
        setBool(L, "gained", ((CxUiFocusEvent*)ev)->gained);
        setInt(L, "flags", ((CxUiFocusEvent*)ev)->flags);
        break;
    case CXUI_EV_Resize:
        setInt(L, "w", ((CxUiResizeEvent*)ev)->size.w);
        setInt(L, "h", ((CxUiResizeEvent*)ev)->size.h);
        break;
    case CXUI_EV_Close:
        setInt(L, "reason", ((CxUiCloseEvent*)ev)->reason);
        break;
    case CXUI_EV_ShowState:
        setInt(L, "state", ((CxUiShowStateEvent*)ev)->state);
        setBool(L, "occluded", ((CxUiShowStateEvent*)ev)->occluded);
        break;
    case CXUI_EV_Move:
        setInt(L, "x", ((CxUiMoveEvent*)ev)->pos.x);
        setInt(L, "y", ((CxUiMoveEvent*)ev)->pos.y);
        break;
    case CXUI_EV_DpiChanged:
        setNum(L, "scale", ((CxUiDpiChangedEvent*)ev)->scale);
        setNum(L, "oldscale", ((CxUiDpiChangedEvent*)ev)->oldScale);
        break;
    case CXUI_EV_ThemeChanged:
        setInt(L, "theme", ((CxUiThemeChangedEvent*)ev)->theme);
        break;
    case CXUI_EV_Scroll:
        setInt(L, "pos", ((CxUiScrollEvent*)ev)->pos);
        setInt(L, "reason", ((CxUiScrollEvent*)ev)->reason);
        break;
    case CXUI_EV_Sort:
        setInt(L, "column", ((CxUiSortEvent*)ev)->column);
        setBool(L, "descending", ((CxUiSortEvent*)ev)->descending);
        break;
    case CXUI_EV_Context: {
        CxUiContextEvent* c = (CxUiContextEvent*)ev;
        setInt(L, "item", (lua_Integer)c->item);
        setInt(L, "column", c->column);
        setBool(L, "onitem", c->onItem);
        setBool(L, "header", c->header);
        setInt(L, "x", c->x);
        setInt(L, "y", c->y);
        break;
    }
    case CXUI_EV_Expand:
        setInt(L, "item", (lua_Integer)((CxUiExpandEvent*)ev)->item);
        setBool(L, "expanded", ((CxUiExpandEvent*)ev)->expanded);
        break;
    case CXUI_EV_Check:
        setInt(L, "item", (lua_Integer)((CxUiCheckEvent*)ev)->item);
        setInt(L, "column", ((CxUiCheckEvent*)ev)->column);
        setInt(L, "state", ((CxUiCheckEvent*)ev)->state);
        break;
    case CXUI_EV_Edit: {
        CxUiEditEvent* e = (CxUiEditEvent*)ev;
        setInt(L, "item", (lua_Integer)e->item);
        setInt(L, "column", e->column);
        setInt(L, "stage", e->stage);
        lua_pushlstring(L, strC(e->text), strLen(e->text));
        lua_setfield(L, -2, "text");
        break;
    }
    case CXUI_EV_Surface:
        setBool(L, "available", ((CxUiSurfaceEvent*)ev)->available);
        break;
    case CXUI_EV_Text: {
        strref t = ((CxUiTextEvent*)ev)->text;
        lua_pushlstring(L, strC(t), strLen(t));
        lua_setfield(L, -2, "text");
        break;
    }
    default:
        break;
    }
}

static CxUiResult runHandler(stvlist* cvars, CxUiEvent* ev)
{
    CxLuaRef* fn = stvlAtPtr(cvars, 0);
    lua_State* L = cxluaRefPush(fn);
    if (!L)
        return CXUI_Default;
    cxluaPushObject(L, objInstBase(ev->current));
    pushEvent(L, ev);
    if (!cxluaPCall(L, 2, 1))
        return CXUI_Default;
    CxUiResult r = lua_toboolean(L, -1) ? CXUI_Handled : CXUI_Default;
    lua_pop(L, 1);
    return r;
}

static void handlerGone(stvlist* cvars)
{
    CxLuaRef* fn = stvlAtPtr(cvars, 0);
    cxluaRefDestroy(&fn);
}

static closure handlerFor(lua_State* L, CxUiEventKind kind, int fn)
{
    stvar ref = stvar(ptr, cxluaRefCreate(L, fn));
    closure cls;
    switch (kind) {
    case CXUI_EV_Activate:     cls = closureCreateAs(CxUiActivateFn, trampActivate, ref); break;
    case CXUI_EV_Change:       cls = closureCreateAs(CxUiChangeFn, trampChange, ref); break;
    case CXUI_EV_Commit:       cls = closureCreateAs(CxUiCommitFn, trampCommit, ref); break;
    case CXUI_EV_Key:          cls = closureCreateAs(CxUiKeyFn, trampKey, ref); break;
    case CXUI_EV_Mouse:        cls = closureCreateAs(CxUiMouseFn, trampMouse, ref); break;
    case CXUI_EV_Wheel:        cls = closureCreateAs(CxUiWheelFn, trampWheel, ref); break;
    case CXUI_EV_Focus:        cls = closureCreateAs(CxUiFocusFn, trampFocus, ref); break;
    case CXUI_EV_Resize:       cls = closureCreateAs(CxUiResizeFn, trampResize, ref); break;
    case CXUI_EV_Close:        cls = closureCreateAs(CxUiCloseFn, trampClose, ref); break;
    case CXUI_EV_ShowState:    cls = closureCreateAs(CxUiShowStateFn, trampShowState, ref); break;
    case CXUI_EV_Move:         cls = closureCreateAs(CxUiMoveFn, trampMove, ref); break;
    case CXUI_EV_DpiChanged:   cls = closureCreateAs(CxUiDpiChangedFn, trampDpiChanged, ref); break;
    case CXUI_EV_ThemeChanged: cls = closureCreateAs(CxUiThemeChangedFn, trampThemeChanged, ref); break;
    case CXUI_EV_Paint:        cls = closureCreateAs(CxUiPaintFn, trampPaint, ref); break;
    case CXUI_EV_Sort:         cls = closureCreateAs(CxUiSortFn, trampSort, ref); break;
    case CXUI_EV_Context:      cls = closureCreateAs(CxUiContextFn, trampContext, ref); break;
    case CXUI_EV_Expand:       cls = closureCreateAs(CxUiExpandFn, trampExpand, ref); break;
    case CXUI_EV_Check:        cls = closureCreateAs(CxUiCheckFn, trampCheck, ref); break;
    case CXUI_EV_Edit:         cls = closureCreateAs(CxUiEditFn, trampEdit, ref); break;
    case CXUI_EV_Surface:      cls = closureCreateAs(CxUiSurfaceFn, trampSurface, ref); break;
    case CXUI_EV_Text:         cls = closureCreateAs(CxUiTextFn, trampText, ref); break;
    default:                   cls = closureCreateAs(CxUiScrollFn, trampScroll, ref); break;
    }
    closureSetDestroy(cls, handlerGone);
    return cls;
}

// The widget's table of handler functions, pushed; created when asked to
static bool pushHandlers(lua_State* L, ObjInst* obj, bool create)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &handlersKey);
    cxluaPushObject(L, obj);
    if (lua_rawget(L, -2) == LUA_TTABLE || !create) {
        lua_remove(L, -2);
        if (lua_istable(L, -1))
            return true;
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);
    lua_newtable(L);
    cxluaPushObject(L, obj);
    lua_pushvalue(L, -2);
    lua_rawset(L, -4);
    lua_remove(L, -2);
    return true;
}

// ---- widget fields ----

static bool widgetAlive(ObjInst* obj)
{
    return !(((CxUiWidget*)obj)->state & CXUI_WS_Dead);
}

static CxUiWidget* checkWidget(lua_State* L, int idx)
{
    return (CxUiWidget*)cxluaCheckObject(L, idx, &CxUiWidget_clsinfo);
}

static CxUiWindow* checkWindow(lua_State* L, int idx)
{
    return (CxUiWindow*)cxluaCheckObject(L, idx, &CxUiWindow_clsinfo);
}

static CxUiWindow* optWindow(lua_State* L, int idx)
{
    return lua_isnoneornil(L, idx) ? NULL : checkWindow(L, idx);
}

static bool widgetGet(lua_State* L, ObjInst* obj, const char* key)
{
    CxUiWidget* w = (CxUiWidget*)obj;
    if (slotFor(key)) {
        if (pushHandlers(L, obj, false)) {
            lua_getfield(L, -1, key);
            lua_remove(L, -2);
        } else {
            lua_pushnil(L);
        }
        return true;
    }
    if (!strcmp(key, "name")) {
        lua_pushlstring(L, strC(w->name), strLen(w->name));
        return true;
    }
    if (!strcmp(key, "parent")) {
        cxluaPushObject(L, w->parent ? objInstBase(w->parent) : NULL);
        return true;
    }
    if (!strcmp(key, "scale")) {
        lua_pushnumber(L, cxuiWidgetScale(w));
        return true;
    }
    if (!strcmp(key, "window")) {
        CxUiWidget* top = w;
        while (top && !objDynCast(CxUiWindow, top))
            top = top->parent;
        cxluaPushObject(L, top ? objInstBase(top) : NULL);
        return true;
    }
    CxUiWindow* win = objDynCast(CxUiWindow, w);
    if (win && !strcmp(key, "closeaction")) {
        lua_pushinteger(L, win->closeAction);
        return true;
    }
    return false;
}

static bool widgetSet(lua_State* L, ObjInst* obj, const char* key, int val)
{
    CxUiWidget* w        = (CxUiWidget*)obj;
    const EvSlot* slot   = slotFor(key);
    if (slot) {
        if (!lua_isnil(L, val))
            luaL_checktype(L, val, LUA_TFUNCTION);
        cxuiSetHandler(w, slot->kind, lua_isnil(L, val) ? NULL : handlerFor(L, slot->kind, val));
        pushHandlers(L, obj, true);
        lua_pushvalue(L, val);
        lua_setfield(L, -2, key);
        lua_pop(L, 1);
        return true;
    }
    CxUiLayoutParams lp;
    cxuiWidgetLayout(w, &lp);
    if (layoutKey(L, key, val, &lp)) {
        cxuiWidgetSetLayout(w, &lp);
        return true;
    }
    if (!strcmp(key, "name")) {
        size_t len    = 0;
        const char* s = luaL_checklstring(L, val, &len);
        strFromBytes(&w->name, s, (uint32)len);
        return true;
    }
    CxUiWindow* win = objDynCast(CxUiWindow, w);
    if (win) {
        if (!strcmp(key, "closeaction")) {
            win->closeAction = (uint32)luaL_checkinteger(L, val);
            return true;
        }
        CxUiWidget* target = lua_isnil(L, val) ? NULL : checkWidget(L, val);
        if (!strcmp(key, "defaultenter"))
            cxuiWindowSetDefaultEnter(win, target);
        else if (!strcmp(key, "defaultesc"))
            cxuiWindowSetDefaultEsc(win, target);
        else if (!strcmp(key, "startfocus"))
            cxuiWindowSetStartFocus(win, target);
        else
            return false;
        return true;
    }
    CxUiTextField* tf = objDynCast(CxUiTextField, w);
    if (tf && !strcmp(key, "cue")) {
        string cue = 0;
        size_t len = 0;
        const char* s = lua_isnil(L, val) ? NULL : luaL_checklstring(L, val, &len);
        strFromBytes(&cue, s, (uint32)len);
        cxuiTextFieldSetCue(tf, cue);
        strDestroy(&cue);
        return true;
    }
    if (tf && !strcmp(key, "maxlength")) {
        cxuiTextFieldSetMaxLength(tf, (int32)luaL_checkinteger(L, val));
        return true;
    }
    return false;
}

static const CxLuaClassHooks widgetHooks = { .alive = widgetAlive, .getField = widgetGet, .setField = widgetSet };

// ---- building ----

// Adds the table's positional entries as children, then sets each named entry not in skip.
static void applyTable(lua_State* L, int t, int ud, const char* const* skip)
{
    CxUiWidget* w = checkWidget(L, ud);
    lua_Integer n = (lua_Integer)lua_rawlen(L, t);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, t, i);
        CxUiWidget* child = checkWidget(L, -1);
        if (!cxuiContainerAppend(w, child))
            luaL_error(L, "entry %d can't be added: it is in a container already, or this isn't one", (int)i);
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    while (lua_next(L, t)) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const char* key = lua_tostring(L, -2);
            if (!listed(layoutKeys, key) && !listed(skip, key)) {
                lua_pushvalue(L, -2);
                lua_pushvalue(L, -2);
                lua_settable(L, ud);
            }
        }
        lua_pop(L, 1);
    }
}

typedef CxUiWidget* (*MakeFn)(lua_State* L, int t, const CxUiLayoutParams* lp, strref name);

static int construct(lua_State* L, MakeFn make, const char* const* consumed)
{
    if (lua_isnoneornil(L, 1)) {
        lua_settop(L, 0);
        lua_newtable(L);
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    CxUiLayoutParams lp;
    readLayout(L, 1, &lp);
    string name = 0;
    optString(L, 1, "name", &name);
    CxUiWidget* w = make(L, 1, &lp, name);
    strDestroy(&name);
    cxluaPushObject(L, objInstBase(w));
    objRelease(&w);
    applyTable(L, 1, lua_gettop(L), consumed);
    return 1;
}

#define CONSTRUCTOR(fn, make, ...)                                \
    static int fn(lua_State* L)                                   \
    {                                                             \
        static const char* const keys[] = { __VA_ARGS__, NULL };  \
        return construct(L, make, keys);                          \
    }

static CxUiBoxDesc boxDesc(lua_State* L, int t, uint32 orient, const CxUiLayoutParams* lp, strref name)
{
    CxUiBoxDesc d = { .orient = (uint32)optInt(L, t, "orient", orient), .margin = optEdges(L, t, "margin"),
                      .gap = (int32)optInt(L, t, "gap", 0), .normalize = (uint8)optInt(L, t, "normalizechildren", 0),
                      .lp = *lp, .name = name };
    if (optField(L, t, "childalign")) {
        toAligns(L, -1, d.align);
        lua_pop(L, 1);
    }
    return d;
}

static CxUiWidget* makeVBox(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    CxUiBoxDesc d = boxDesc(L, t, CXUI_ORIENT_V, lp, name);
    return CxUiWidget(cxuiBoxCreate(&d));
}

static CxUiWidget* makeHBox(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    CxUiBoxDesc d = boxDesc(L, t, CXUI_ORIENT_H, lp, name);
    return CxUiWidget(cxuiBoxCreate(&d));
}

#define BOX_KEYS "orient", "margin", "gap", "normalizechildren", "childalign"
CONSTRUCTOR(newVBox, makeVBox, BOX_KEYS)
CONSTRUCTOR(newHBox, makeHBox, BOX_KEYS)

static CxUiWidget* makeRadioGroup(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    CxUiBoxDesc d = boxDesc(L, t, CXUI_ORIENT_V, lp, name);
    return CxUiWidget(cxuiRadioGroupCreate(&d));
}
CONSTRUCTOR(newRadioGroup, makeRadioGroup, BOX_KEYS)

static CxUiColor checkColor(lua_State* L, int idx);

// t.bg, or all 0 for the palette's
static CxUiColor optColor(lua_State* L, int t, const char* key)
{
    if (!optField(L, t, key))
        return (CxUiColor){ 0 };
    CxUiColor c = checkColor(L, -1);
    lua_pop(L, 1);
    return c;
}

static uint32 optBgRole(lua_State* L, int t)
{
    lua_Integer role = optInt(L, t, "bgrole", 0);
    if (role < 0 || role >= CXUI_PAL_Count)
        luaL_error(L, "bgrole is not one of cxui.PAL");
    return (uint32)role;
}

static CxUiPanelDesc panelDesc(lua_State* L, int t, const CxUiLayoutParams* lp, strref name, string* title)
{
    CxUiBoxDesc b = boxDesc(L, t, CXUI_ORIENT_V, lp, name);
    optString(L, t, "title", title);
    return (CxUiPanelDesc){ .orient = b.orient, .margin = b.margin, .gap = b.gap, .align = { b.align[0], b.align[1] },
                            .normalize = b.normalize, .border = (int32)optInt(L, t, "border", 0), .title = *title,
                            .bgRole = optBgRole(L, t), .bg = optColor(L, t, "bg"), .lp = *lp, .name = name };
}

static CxUiWidget* makePanel(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string title    = 0;
    CxUiPanelDesc d = panelDesc(L, t, lp, name, &title);
    CxUiWidget* w   = CxUiWidget(cxuiPanelCreate(&d));
    strDestroy(&title);
    return w;
}

static CxUiWidget* makeFrame(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string title    = 0;
    CxUiPanelDesc d = panelDesc(L, t, lp, name, &title);
    CxUiWidget* w   = CxUiWidget(cxuiFrameCreate(&d));
    strDestroy(&title);
    return w;
}
CONSTRUCTOR(newPanel, makePanel, BOX_KEYS, "title", "border", "bgrole", "bg")
CONSTRUCTOR(newFrame, makeFrame, BOX_KEYS, "title", "border", "bgrole", "bg")

static CxUiWidget* makeScrollBox(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    CxUiBoxDesc b = boxDesc(L, t, CXUI_ORIENT_V, lp, name);
    uint32 scroll = CXUI_EXPAND_V;
    if (optField(L, t, "scroll")) {
        scroll = toExpand(L, -1);
        lua_pop(L, 1);
    }
    CxUiScrollBox* sb = cxuiScrollBoxCreate(&(CxUiScrollBoxDesc){
        .orient = b.orient, .scroll = (uint8)scroll, .view = optSize(L, t, "view"), .margin = b.margin, .gap = b.gap,
        .align = { b.align[0], b.align[1] }, .bgRole = optBgRole(L, t), .lp = *lp, .name = name });
    CxUiColor bg = optColor(L, t, "bg");
    if (bg.bits)
        cxuiPanelSetBg(sb, bg);
    return CxUiWidget(sb);
}
CONSTRUCTOR(newScrollBox, makeScrollBox, BOX_KEYS, "scroll", "view", "bgrole", "bg")

static CxUiWidget* makeZBox(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    flags_t flags = optBool(L, t, "sizetoactive", false) ? CXUI_ZBOX_SizeToActive : 0;
    return CxUiWidget(cxuiZBoxCreate(&(CxUiZBoxDesc){ .flags = flags, .lp = *lp, .name = name }));
}
CONSTRUCTOR(newZBox, makeZBox, "sizetoactive")

static CxUiWidget* makeTabs(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    flags_t flags  = (optBool(L, t, "sizetoactive", false) ? CXUI_ZBOX_SizeToActive : 0) |
                    (optBool(L, t, "multiline", false) ? CXUI_TABS_Multiline : 0);
    CxUiTabs* tabs = cxuiTabsCreate(&(CxUiTabsDesc){ .flags = flags, .lp = *lp, .name = name });
    if (optField(L, t, "bgrole")) {
        lua_pop(L, 1);
        cxuiTabsSetBgRole(tabs, optBgRole(L, t));
    }
    return CxUiWidget(tabs);
}

// The pages are the table's entries, as in any container; titles names their tabs in order.
static int newTabs(lua_State* L)
{
    static const char* const keys[] = { "sizetoactive", "multiline", "bgrole", "titles", NULL };
    construct(L, makeTabs, keys);
    CxUiTabs* tabs = (CxUiTabs*)cxluaCheckObject(L, -1, &CxUiTabs_clsinfo);
    if (optField(L, 1, "titles")) {
        luaL_checktype(L, -1, LUA_TTABLE);
        lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
        string title  = 0;
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            size_t len    = 0;
            const char* s = luaL_checklstring(L, -1, &len);
            strFromBytes(&title, s, (uint32)len);
            cxuiTabsSetTitle(tabs, (int32)(i - 1), title);
            lua_pop(L, 1);
        }
        strDestroy(&title);
        lua_pop(L, 1);
    }
    return 1;
}

static int mTabsAdd(lua_State* L)
{
    CxUiTabs* tabs   = (CxUiTabs*)cxluaCheckObject(L, 1, &CxUiTabs_clsinfo);
    CxUiWidget* page = checkWidget(L, 2);
    size_t len       = 0;
    const char* s    = luaL_checklstring(L, 3, &len);
    string title     = 0;
    strFromBytes(&title, s, (uint32)len);
    bool ok = cxuiTabsAdd(tabs, page, title);
    strDestroy(&title);
    if (!ok)
        return luaL_error(L, "the page can't be added: it is in a container already");
    return 0;
}

static int mTabsTitle(lua_State* L)
{
    CxUiTabs* tabs = (CxUiTabs*)cxluaCheckObject(L, 1, &CxUiTabs_clsinfo);
    strref title   = cxuiTabsTitle(tabs, (int32)luaL_checkinteger(L, 2));
    if (title)
        lua_pushlstring(L, strC(title), strLen(title));
    else
        lua_pushnil(L);
    return 1;
}

static int mTabsSetTitle(lua_State* L)
{
    CxUiTabs* tabs = (CxUiTabs*)cxluaCheckObject(L, 1, &CxUiTabs_clsinfo);
    int32 index    = (int32)luaL_checkinteger(L, 2);
    size_t len     = 0;
    const char* s  = luaL_optlstring(L, 3, NULL, &len);
    string title   = 0;
    if (s)
        strFromBytes(&title, s, (uint32)len);
    cxuiTabsSetTitle(tabs, index, title);
    strDestroy(&title);
    return 0;
}

static const luaL_Reg tabsMethods[] = {
    { "add", mTabsAdd },
    { "title", mTabsTitle },
    { "settitle", mTabsSetTitle },
    { NULL, NULL },
};

static CxUiWidget* makeSplit(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    return CxUiWidget(cxuiSplitCreate(&(CxUiSplitDesc){ .orient = (uint32)optInt(L, t, "orient", CXUI_ORIENT_H),
                                                        .value  = (float32)optNum(L, t, "value", 0),
                                                        .lp     = *lp,
                                                        .name   = name }));
}
CONSTRUCTOR(newSplit, makeSplit, "orient", "value")

static CxUiWidget* makeFill(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    unused_noeval(L);
    unused_noeval(t);
    unused_noeval(name);
    CxUiWidget* w = CxUiWidget(cxuiFillCreate());
    cxuiWidgetSetLayout(w, lp);
    return w;
}
CONSTRUCTOR(newFill, makeFill, "")

static CxUiWidget* makeSpace(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    unused_noeval(lp);
    unused_noeval(name);
    return CxUiWidget(cxuiSpaceCreate(cxuiLen((int32)optInt(L, t, "w", 0)), cxuiLen((int32)optInt(L, t, "h", 0))));
}
CONSTRUCTOR(newSpace, makeSpace, "w", "h")

static CxUiWidget* makeLabel(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string text = 0;
    optString(L, t, "text", &text);
    CxUiWidget* w = CxUiWidget(cxuiLabelCreate(&(CxUiLabelDesc){
        .text = text, .align = (flags_t)optInt(L, t, "textalign", 0), .ellipsis = (uint32)optInt(L, t, "ellipsis", 0),
        .wrap = optBool(L, t, "wrap", false), .columns = (int32)optInt(L, t, "columns", 0),
        .color = { .bits = (uint32)optInt(L, t, "color", 0) }, .lp = *lp, .name = name }));
    strDestroy(&text);
    return w;
}
CONSTRUCTOR(newLabel, makeLabel, "text", "textalign", "ellipsis", "wrap", "columns", "color")

static CxUiWidget* makeButton(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string title = 0;
    optString(L, t, "title", &title);
    CxUiWidget* w = CxUiWidget(
        cxuiButtonCreate(&(CxUiButtonDesc){ .title = title, .padding = optSize(L, t, "padding"), .lp = *lp, .name = name }));
    strDestroy(&title);
    return w;
}
CONSTRUCTOR(newButton, makeButton, "title", "padding")

static CxUiWidget* makeMenu(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    unused_noeval(L);
    unused_noeval(t);
    unused_noeval(lp);
    CxUiMenu* m = cxuiMenuCreate();
    strDup(&CxUiWidget(m)->name, name);
    return CxUiWidget(m);
}
CONSTRUCTOR(newMenu, makeMenu, "")

static CxUiWidget* makeMenuItem(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    unused_noeval(lp);
    string title = 0, hint = 0;
    optString(L, t, "title", &title);
    optString(L, t, "hint", &hint);
    CxUiMenu* sub = NULL;
    if (optField(L, t, "submenu")) {
        sub = (CxUiMenu*)cxluaCheckObject(L, -1, &CxUiMenu_clsinfo);
        lua_pop(L, 1);
    }
    CxUiMenuItem* item = cxuiMenuItemCreate(&(CxUiMenuItemDesc){ .title   = title,
                                                                 .hint    = hint,
                                                                 .kind    = (uint32)optInt(L, t, "kind", 0),
                                                                 .checked = optBool(L, t, "checked", false),
                                                                 .submenu = sub,
                                                                 .name    = name });
    strDestroy(&title);
    strDestroy(&hint);
    return CxUiWidget(item);
}
CONSTRUCTOR(newMenuItem, makeMenuItem, "title", "hint", "kind", "checked", "submenu")

static int mMenuClear(lua_State* L)
{
    cxuiMenuClear((CxUiMenu*)cxluaCheckObject(L, 1, &CxUiMenu_clsinfo));
    return 0;
}

static const luaL_Reg menuMethods[] = {
    { "clear", mMenuClear },
    { NULL, NULL },
};

static int mMenuChoose(lua_State* L)
{
    cxuiMenuItemChoose((CxUiMenuItem*)cxluaCheckObject(L, 1, &CxUiMenuItem_clsinfo));
    return 0;
}

static const luaL_Reg menuItemMethods[] = {
    { "choose", mMenuChoose },
    { NULL, NULL },
};

static CxUiWidget* makeCheck(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string title = 0;
    optString(L, t, "title", &title);
    uint32 state  = optBool(L, t, "value", false) ? CXUI_CHECK_On : (uint32)optInt(L, t, "state", CXUI_CHECK_Off);
    CxUiWidget* w = CxUiWidget(cxuiCheckCreate(&(CxUiCheckDesc){
        .title = title, .state = state, .threeState = optBool(L, t, "threestate", false), .lp = *lp, .name = name }));
    strDestroy(&title);
    return w;
}
CONSTRUCTOR(newCheck, makeCheck, "title", "value", "state", "threestate")

static CxUiWidget* makeRadio(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string title = 0;
    optString(L, t, "title", &title);
    CxUiWidget* w = CxUiWidget(cxuiRadioCreate(&(CxUiRadioDesc){ .title = title, .lp = *lp, .name = name }));
    strDestroy(&title);
    return w;
}
CONSTRUCTOR(newRadio, makeRadio, "title")

static CxUiWidget* makeTextField(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string text = 0, cue = 0;
    optString(L, t, "text", &text);
    optString(L, t, "cue", &cue);
    CxUiWidget* w = CxUiWidget(cxuiTextFieldCreate(&(CxUiTextFieldDesc){
        .text = text, .cue = cue, .columns = (int32)optInt(L, t, "columns", 0),
        .maxLength = (int32)optInt(L, t, "maxlength", 0), .readOnly = optBool(L, t, "readonly", false), .lp = *lp,
        .name = name }));
    strDestroy(&text);
    strDestroy(&cue);
    return w;
}
CONSTRUCTOR(newTextField, makeTextField, "text", "cue", "columns", "maxlength", "readonly")

static CxUiWidget* makeTextArea(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string text = 0;
    optString(L, t, "text", &text);
    CxUiWidget* w = CxUiWidget(cxuiTextAreaCreate(&(CxUiTextAreaDesc){
        .text = text, .columns = (int32)optInt(L, t, "columns", 0), .lines = (int32)optInt(L, t, "lines", 0),
        .wrap = optBool(L, t, "wrap", false), .maxLength = (int32)optInt(L, t, "maxlength", 0),
        .readOnly = optBool(L, t, "readonly", false), .lp = *lp, .name = name }));
    strDestroy(&text);
    return w;
}
CONSTRUCTOR(newTextArea, makeTextArea, "text", "columns", "lines", "wrap", "maxlength", "readonly")

static CxUiWidget* makeTextFrame(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    lua_rawgeti(L, t, 1);
    CxUiTextField* field = (CxUiTextField*)cxluaCheckObject(L, -1, &CxUiTextField_clsinfo);
    lua_pop(L, 1);
    // The field is the frame's own child, not a positional entry to add.
    lua_pushnil(L);
    lua_rawseti(L, t, 1);
    CxUiTextFrame* fr = cxuiTextFrameCreate(&(CxUiTextFrameDesc){ .field    = field,
                                                                  .padding  = optSize(L, t, "padding"),
                                                                  .border   = (int32)optInt(L, t, "border", 0),
                                                                  .noBorder = optBool(L, t, "noborder", false),
                                                                  .lp       = *lp,
                                                                  .name     = name });
    if (optField(L, t, "bgrole")) {
        lua_pop(L, 1);
        cxuiPanelSetBgRole(fr, optBgRole(L, t));
    }
    CxUiColor bg = optColor(L, t, "bg");
    if (bg.bits)
        cxuiPanelSetBg(fr, bg);
    return CxUiWidget(fr);
}
CONSTRUCTOR(newTextFrame, makeTextFrame, "padding", "border", "noborder", "bgrole", "bg")

// The table's `items` as strings kept in out; returns a NULL-ended array of them to free with
// xaFree(), or NULL for none
static strref* optItems(lua_State* L, int t, sa_string* out)
{
    saInit(out, string, 8);
    if (!optField(L, t, "items"))
        return NULL;
    luaL_checktype(L, -1, LUA_TTABLE);
    lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        size_t len    = 0;
        const char* s = luaL_checklstring(L, -1, &len);
        string item   = 0;
        strFromBytes(&item, s, (uint32)len);
        saPushC(out, string, &item);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    strref* refs = xaAlloc((saSize(*out) + 1) * sizeof(strref), XA_Zero);
    for (int32 i = 0; i < saSize(*out); i++)
        refs[i] = out->a[i];
    return refs;
}

static CxUiWidget* makeList(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    sa_string items;
    strref* refs  = optItems(L, t, &items);
    CxUiWidget* w = CxUiWidget(cxuiListCreate(&(CxUiListDesc){
        .items = refs, .value = (int32)optInt(L, t, "value", 0),
        .visibleItems = (int32)optInt(L, t, "visibleitems", 0), .columns = (int32)optInt(L, t, "columns", 0),
        .select = (uint8)optInt(L, t, "select", 0), .lp = *lp, .name = name }));
    xaFree((void*)refs);
    saDestroy(&items);
    return w;
}
CONSTRUCTOR(newList, makeList, "items", "value", "visibleitems", "columns", "select")

static CxUiWidget* makeDropdown(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    sa_string items;
    strref* refs  = optItems(L, t, &items);
    CxUiWidget* w = CxUiWidget(cxuiDropdownCreate(&(CxUiDropdownDesc){
        .items = refs, .value = (int32)optInt(L, t, "value", 0),
        .columns = (int32)optInt(L, t, "columns", 0), .lp = *lp, .name = name }));
    xaFree((void*)refs);
    saDestroy(&items);
    return w;
}
CONSTRUCTOR(newDropdown, makeDropdown, "items", "value", "columns")

static CxUiWidget* makeSlider(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    return CxUiWidget(cxuiSliderCreate(&(CxUiSliderDesc){
        .min = optNum(L, t, "min", 0), .max = optNum(L, t, "max", 0), .value = optNum(L, t, "value", 0),
        .step = optNum(L, t, "step", 0), .pageStep = optNum(L, t, "pagestep", 0), .ticks = (int32)optInt(L, t, "ticks", 0),
        .orient = (uint32)optInt(L, t, "orient", CXUI_ORIENT_H), .length = (int32)optInt(L, t, "length", 0), .lp = *lp,
        .name = name }));
}
CONSTRUCTOR(newSlider, makeSlider, "min", "max", "value", "step", "pagestep", "ticks", "orient", "length")

static CxUiWidget* makeProgress(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    return CxUiWidget(cxuiProgressCreate(&(CxUiProgressDesc){
        .value = optNum(L, t, "value", 0), .max = optNum(L, t, "max", 0),
        .indeterminate = optBool(L, t, "indeterminate", false), .length = (int32)optInt(L, t, "length", 0), .lp = *lp,
        .name = name }));
}
CONSTRUCTOR(newProgress, makeProgress, "value", "max", "indeterminate", "length")

static CxUiWidget* makeCanvas(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    CxUiWidget* w = CxUiWidget(cxuiCanvasCreate(&(CxUiCanvasDesc){ .draw  = (uint32)optInt(L, t, "draw", 0),
                                                                   .units = (uint32)optInt(L, t, "units", 0),
                                                                   .name  = name }));
    cxuiWidgetSetLayout(w, lp);
    return w;
}
CONSTRUCTOR(newCanvas, makeCanvas, "draw", "units")

static CxUiWidget* makeRenderView(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    CxUiWidget* w = CxUiWidget(cxuiRenderViewCreate(
        &(CxUiRenderViewDesc){ .units = (uint32)optInt(L, t, "units", 0), .name = name }));
    cxuiWidgetSetLayout(w, lp);
    return w;
}
CONSTRUCTOR(newRenderView, makeRenderView, "units")

static CxUiWidget* makeDLabel(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string text = 0;
    optString(L, t, "text", &text);
    CxUiWidget* w = CxUiWidget(cxuiDLabelCreate(&(CxUiDLabelDesc){
        .text = text, .align = (flags_t)optInt(L, t, "textalign", 0), .ellipsis = optBool(L, t, "ellipsis", false),
        .padding = optSize(L, t, "padding"), .lp = *lp, .name = name }));
    strDestroy(&text);
    return w;
}
CONSTRUCTOR(newDLabel, makeDLabel, "text", "textalign", "ellipsis", "padding")

static CxUiWidget* makeDButton(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string title = 0;
    optString(L, t, "title", &title);
    CxUiWidget* w = CxUiWidget(cxuiDButtonCreate(&(CxUiDButtonDesc){
        .title = title, .toggle = optBool(L, t, "toggle", false), .value = optBool(L, t, "value", false),
        .padding = optSize(L, t, "padding"), .canFocus = optBool(L, t, "canfocus", false), .lp = *lp, .name = name }));
    strDestroy(&title);
    return w;
}
CONSTRUCTOR(newDButton, makeDButton, "title", "toggle", "value", "padding", "canfocus")

static CxUiWidget* makeDTabs(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    CxUiZBox* zbox = NULL;
    if (optField(L, t, "zbox")) {
        zbox = (CxUiZBox*)cxluaCheckObject(L, -1, &CxUiZBox_clsinfo);
        lua_pop(L, 1);
    }
    CxUiDTabs* tabs = cxuiDTabsCreate(&(CxUiDTabsDesc){ .zbox = zbox, .padding = optSize(L, t, "padding"),
                                                        .gap = (int32)optInt(L, t, "gap", 0), .lp = *lp, .name = name });
    if (optField(L, t, "tabs")) {
        luaL_checktype(L, -1, LUA_TTABLE);
        lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
        string title  = 0;
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            size_t len    = 0;
            const char* s = luaL_checklstring(L, -1, &len);
            strFromBytes(&title, s, (uint32)len);
            cxuiDTabsAdd(tabs, title);
            lua_pop(L, 1);
        }
        strDestroy(&title);
        lua_pop(L, 1);
    }
    return CxUiWidget(tabs);
}
CONSTRUCTOR(newDTabs, makeDTabs, "zbox", "padding", "gap", "tabs")

static CxUiWidget* makeLink(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string text = 0, url = 0;
    optString(L, t, "text", &text);
    optString(L, t, "url", &url);
    CxUiWidget* w = CxUiWidget(cxuiLinkCreate(&(CxUiLinkDesc){
        .text = text, .url = url, .underline = (uint32)optInt(L, t, "underline", 0),
        .noFocus = optBool(L, t, "nofocus", false), .lp = *lp, .name = name }));
    strDestroy(&text);
    strDestroy(&url);
    return w;
}
CONSTRUCTOR(newLink, makeLink, "text", "url", "underline", "nofocus")

// A window's size and minsize are the area inside its frame, in device-independent pixels.
static CxUiWidget* makeWindow(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    string title = 0;
    optString(L, t, "title", &title);
    if (lp->size[0].unit == CXUI_UNIT_Chars || lp->size[1].unit == CXUI_UNIT_Chars ||
        lp->minSize[0].unit == CXUI_UNIT_Chars || lp->minSize[1].unit == CXUI_UNIT_Chars)
        luaL_error(L, "a window's size is in device-independent pixels: w and h, not cw and ch");
    CxUiWindowDesc d = { .title = title, .size = { lp->size[0].v, lp->size[1].v }, .sizing = (uint32)optInt(L, t, "sizing", 0),
                         .minSize = { lp->minSize[0].v, lp->minSize[1].v }, .flags = (flags_t)optInt(L, t, "flags", 0),
                         .closeAction = (uint32)optInt(L, t, "closeaction", 0), .theme = (uint32)optInt(L, t, "theme", 0),
                         .name = name };
    CxUiWidget* w = CxUiWidget(cxuiWindowCreate(&d));
    strDestroy(&title);
    return w;
}
CONSTRUCTOR(newWindow, makeWindow, "title", "sizing", "flags", "closeaction", "theme")

static CxUiWidget* makeProgressDialog(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    unused_noeval(lp);
    string title = 0, text = 0, cancelTitle = 0;
    optString(L, t, "title", &title);
    optString(L, t, "text", &text);
    optString(L, t, "canceltitle", &cancelTitle);
    CxUiWidget* w = CxUiWidget(cxuiProgressDialogCreate(&(CxUiProgressDialogDesc){
        .title = title, .text = text, .total = (int64)optInt(L, t, "total", 0), .cancel = optBool(L, t, "cancel", false),
        .cancelTitle = cancelTitle, .name = name }));
    strDestroy(&title);
    strDestroy(&text);
    strDestroy(&cancelTitle);
    return w;
}
CONSTRUCTOR(newProgressDialog, makeProgressDialog, "title", "text", "total", "cancel", "canceltitle")

// ---- methods ----

static strref checkStr(lua_State* L, int idx, string* keep)
{
    size_t len    = 0;
    const char* s = luaL_checklstring(L, idx, &len);
    strFromBytes(keep, s, (uint32)len);
    return *keep;
}

static int mDestroy(lua_State* L)
{
    cxuiDestroy(checkWidget(L, 1));
    return 0;
}

static int mFocus(lua_State* L)
{
    cxuiFocus(checkWidget(L, 1));
    return 0;
}

static int mRefresh(lua_State* L)
{
    cxuiRefresh(checkWidget(L, 1));
    return 0;
}

static int mAppend(lua_State* L)
{
    return lua_pushboolean(L, cxuiContainerAppend(checkWidget(L, 1), checkWidget(L, 2))), 1;
}

static int mInsert(lua_State* L)
{
    return lua_pushboolean(L, cxuiContainerInsert(checkWidget(L, 1), checkWidget(L, 2), (int32)luaL_checkinteger(L, 3))), 1;
}

static int mDetach(lua_State* L)
{
    return lua_pushboolean(L, cxuiContainerDetach(checkWidget(L, 1), checkWidget(L, 2))), 1;
}

static int mChild(lua_State* L)
{
    CxUiWidget* c = cxuiContainerChild(checkWidget(L, 1), (int32)luaL_checkinteger(L, 2));
    cxluaPushObject(L, c ? objInstBase(c) : NULL);
    return 1;
}

static int mChildCount(lua_State* L)
{
    return lua_pushinteger(L, cxuiContainerChildCount(checkWidget(L, 1))), 1;
}

static int mPx(lua_State* L)
{
    return lua_pushinteger(L, cxuiPx(checkWidget(L, 1), (int32)luaL_checkinteger(L, 2))), 1;
}

static int mDip(lua_State* L)
{
    return lua_pushinteger(L, cxuiDip(checkWidget(L, 1), (int32)luaL_checkinteger(L, 2))), 1;
}

static const luaL_Reg widgetMethods[] = {
    { "destroy", mDestroy }, { "focus", mFocus },   { "refresh", mRefresh },   { "append", mAppend },
    { "insert", mInsert },   { "detach", mDetach }, { "child", mChild },       { "childcount", mChildCount },
    { "px", mPx },           { "dip", mDip },       { NULL, NULL },
};

static int mShow(lua_State* L)
{
    cxuiWindowShow(checkWindow(L, 1), (uint32)luaL_optinteger(L, 2, CXUI_SHOW_Normal));
    return 0;
}

static int mHide(lua_State* L)
{
    cxuiWindowHide(checkWindow(L, 1));
    return 0;
}

static int mRealize(lua_State* L)
{
    return lua_pushboolean(L, cxuiWindowRealize(checkWindow(L, 1))), 1;
}

static int mClose(lua_State* L)
{
    cxuiWindowClose(checkWindow(L, 1));
    return 0;
}

static int mPopup(lua_State* L)
{
    CxUiWindow* w = checkWindow(L, 1);
    return lua_pushinteger(L, cxuiWindowPopup(w, optWindow(L, 2))), 1;
}

static int mShowModal(lua_State* L)
{
    CxUiWindow* w      = checkWindow(L, 1);
    CxUiWindow* parent = optWindow(L, 2);
    closure done       = lua_isnoneornil(L, 3) ? NULL : cxluaToClosure(L, 3);
    return lua_pushboolean(L, cxuiWindowShowModal(w, parent, done)), 1;
}

static int mEndPopup(lua_State* L)
{
    cxuiWindowEndPopup(checkWindow(L, 1), (int32)luaL_optinteger(L, 2, 0));
    return 0;
}

static int mBeginDrag(lua_State* L)
{
    return lua_pushboolean(L, cxuiWindowBeginDrag(checkWindow(L, 1))), 1;
}

static int mFit(lua_State* L)
{
    cxuiWindowFit(checkWindow(L, 1), (flags_t)luaL_optinteger(L, 2, 0));
    return 0;
}

static int mSetSize(lua_State* L)
{
    cxuiWindowSetSize(checkWindow(L, 1), (int32)luaL_checkinteger(L, 2), (int32)luaL_checkinteger(L, 3));
    return 0;
}

static int mSize(lua_State* L)
{
    CxUiSize s;
    if (!cxuiWindowSize(checkWindow(L, 1), &s))
        return 0;
    lua_pushinteger(L, s.w);
    lua_pushinteger(L, s.h);
    return 2;
}

static int mSetPosition(lua_State* L)
{
    return lua_pushboolean(L, cxuiWindowSetPosition(checkWindow(L, 1), (int32)luaL_checkinteger(L, 2),
                                                    (int32)luaL_checkinteger(L, 3))),
           1;
}

static int mPosition(lua_State* L)
{
    CxUiPoint p;
    if (!cxuiWindowPosition(checkWindow(L, 1), &p))
        return 0;
    lua_pushinteger(L, p.x);
    lua_pushinteger(L, p.y);
    return 2;
}

static int mMoveNear(lua_State* L)
{
    CxUiWindow* w = checkWindow(L, 1);
    return lua_pushboolean(L, cxuiWindowMoveNear(w, optWindow(L, 2), (uint32)luaL_optinteger(L, 3, CXUI_NEAR_Center))), 1;
}

static int mFocused(lua_State* L)
{
    CxUiWidget* f = cxuiFocused(checkWindow(L, 1));
    cxluaPushObject(L, f ? objInstBase(f) : NULL);
    return 1;
}

// w:setfullscreen(mode [, monitor]): the monitor is a table from cxui.monitors()
static int mSetFullscreen(lua_State* L)
{
    CxUiWindow* w   = checkWindow(L, 1);
    uint32 mode     = (uint32)luaL_checkinteger(L, 2);
    CxUiMonitor mon = { 0 };
    bool given      = !lua_isnoneornil(L, 3);
    if (given) {
        luaL_checktype(L, 3, LUA_TTABLE);
        mon.bounds = (CxUiRect){ (int32)optInt(L, 3, "x", 0), (int32)optInt(L, 3, "y", 0), (int32)optInt(L, 3, "w", 0),
                                 (int32)optInt(L, 3, "h", 0) };
        optString(L, 3, "id", &mon.id);
    }
    bool ok = cxuiWindowSetFullscreen(w, mode, given ? &mon : NULL);
    cxuiMonitorDestroy(&mon);
    return lua_pushboolean(L, ok), 1;
}

static int mSetTopmost(lua_State* L)
{
    return lua_pushboolean(L, cxuiWindowSetTopmost(checkWindow(L, 1), lua_toboolean(L, 2))), 1;
}

static int mTopmost(lua_State* L)
{
    return lua_pushboolean(L, cxuiWindowTopmost(checkWindow(L, 1))), 1;
}

static int mOccluded(lua_State* L)
{
    return lua_pushboolean(L, cxuiWindowOccluded(checkWindow(L, 1))), 1;
}

// The index into cxui.monitors(), from 1, or nil
static int mWindowMonitor(lua_State* L)
{
    int32 i = cxuiWindowMonitor(checkWindow(L, 1));
    if (i < 0)
        return lua_pushnil(L), 1;
    return lua_pushinteger(L, i + 1), 1;
}

static int mFullscreen(lua_State* L)
{
    return lua_pushinteger(L, cxuiWindowFullscreen(checkWindow(L, 1))), 1;
}

static const luaL_Reg windowMethods[] = {
    { "show", mShow },
    { "hide", mHide },
    { "realize", mRealize },
    { "close", mClose },
    { "popup", mPopup },
    { "showmodal", mShowModal },
    { "endpopup", mEndPopup },
    { "begindrag", mBeginDrag },
    { "fit", mFit },
    { "setsize", mSetSize },
    { "size", mSize },
    { "setposition", mSetPosition },
    { "position", mPosition },
    { "movenear", mMoveNear },
    { "focused", mFocused },
    { "setfullscreen", mSetFullscreen },
    { "fullscreen", mFullscreen },
    { "monitor", mWindowMonitor },
    { "settopmost", mSetTopmost },
    { "topmost", mTopmost },
    { "occluded", mOccluded },
    { NULL, NULL },
};

static CxUiCanvas* checkCanvas(lua_State* L, int idx)
{
    return (CxUiCanvas*)cxluaCheckObject(L, idx, &CxUiCanvas_clsinfo);
}

static int mInvalidate(lua_State* L)
{
    CxUiCanvas* cv = checkCanvas(L, 1);
    if (lua_isnoneornil(L, 2))
        cxuiCanvasInvalidateAll(cv);
    else
        cxuiCanvasInvalidate(cv, (int32)luaL_checkinteger(L, 2), (int32)luaL_checkinteger(L, 3),
                             (int32)luaL_checkinteger(L, 4), (int32)luaL_checkinteger(L, 5));
    return 0;
}

static int mCanvasSize(lua_State* L)
{
    CxUiSize s;
    cxuiCanvasSize(checkCanvas(L, 1), &s);
    lua_pushinteger(L, s.w);
    lua_pushinteger(L, s.h);
    return 2;
}

static int mSetCursor(lua_State* L)
{
    cxuiCanvasSetCursor(checkCanvas(L, 1), (uint32)luaL_checkinteger(L, 2));
    return 0;
}

// cv:setcursorimage(cursor or nil)
static int mSetCursorImage(lua_State* L)
{
    CxUiCursor* cur = lua_isnoneornil(L, 2) ? NULL : (CxUiCursor*)cxluaCheckObject(L, 2, &CxUiCursor_clsinfo);
    cxuiCanvasSetCursorImage(checkCanvas(L, 1), cur);
    return 0;
}

static int mWarpPointer(lua_State* L)
{
    return lua_pushboolean(L, cxuiCanvasWarpPointer(checkCanvas(L, 1), (int32)luaL_checkinteger(L, 2),
                                                    (int32)luaL_checkinteger(L, 3))),
           1;
}

static int mSetTextInput(lua_State* L)
{
    return lua_pushboolean(L, cxuiCanvasSetTextInput(checkCanvas(L, 1), (flags_t)luaL_checkinteger(L, 2))), 1;
}

static int mSetTextCaret(lua_State* L)
{
    cxuiCanvasSetTextCaret(checkCanvas(L, 1), (int32)luaL_checkinteger(L, 2), (int32)luaL_checkinteger(L, 3),
                           (int32)luaL_checkinteger(L, 4), (int32)luaL_checkinteger(L, 5));
    return 0;
}

static int mSetKeyCapture(lua_State* L)
{
    return lua_pushboolean(L, cxuiCanvasSetKeyCapture(checkCanvas(L, 1), (flags_t)luaL_checkinteger(L, 2))), 1;
}

static int mSetPointerLock(lua_State* L)
{
    return lua_pushboolean(L, cxuiCanvasSetPointerLock(checkCanvas(L, 1), (uint32)luaL_checkinteger(L, 2))), 1;
}

static int mPointerLockMode(lua_State* L)
{
    return lua_pushinteger(L, cxuiCanvasPointerLockMode(checkCanvas(L, 1))), 1;
}

// Shared by canvases and render views
#define CANVAS_INPUT_METHODS                                                                   \
    { "settextinput", mSetTextInput }, { "settextcaret", mSetTextCaret },                    \
        { "setkeycapture", mSetKeyCapture }, { "setpointerlock", mSetPointerLock },          \
        { "pointerlockmode", mPointerLockMode }, { "setcursorimage", mSetCursorImage },        \
        { "warppointer", mWarpPointer }

static const luaL_Reg canvasMethods[] = {
    { "invalidate", mInvalidate },
    { "size", mCanvasSize },
    { "setcursor", mSetCursor },
    CANVAS_INPUT_METHODS,
    { NULL, NULL },
};

// The native surface as a table: kind, native and connection as light userdata, w and h; nil
// while there is none
static int mRenderViewSurface(lua_State* L)
{
    CxUiNativeSurface s;
    if (!cxuiRenderViewSurface((CxUiRenderView*)cxluaCheckObject(L, 1, &CxUiRenderView_clsinfo), &s))
        return lua_pushnil(L), 1;
    lua_newtable(L);
    setInt(L, "kind", s.kind);
    lua_pushlightuserdata(L, s.native);
    lua_setfield(L, -2, "native");
    lua_pushlightuserdata(L, s.connection);
    lua_setfield(L, -2, "connection");
    setInt(L, "w", s.w);
    setInt(L, "h", s.h);
    return 1;
}

static const luaL_Reg renderViewMethods[] = {
    { "invalidate", mInvalidate },
    { "size", mCanvasSize },
    { "setcursor", mSetCursor },
    CANVAS_INPUT_METHODS,
    { "surface", mRenderViewSurface },
    { NULL, NULL },
};

// Lists and dropdowns share their item calls; the class check picks which.
static bool isList(lua_State* L, int idx, CxUiList** list, CxUiDropdown** dd)
{
    *list = (CxUiList*)cxluaTestObject(L, idx, &CxUiList_clsinfo);
    *dd   = *list ? NULL : (CxUiDropdown*)cxluaCheckObject(L, idx, &CxUiDropdown_clsinfo);
    return *list != NULL;
}

static int mItemsAppend(lua_State* L)
{
    CxUiList* l;
    CxUiDropdown* d;
    string text = 0;
    if (isList(L, 1, &l, &d))
        cxuiListAppend(l, checkStr(L, 2, &text));
    else
        cxuiDropdownAppend(d, checkStr(L, 2, &text));
    strDestroy(&text);
    return 0;
}

static int mItemsInsert(lua_State* L)
{
    CxUiList* l;
    CxUiDropdown* d;
    string text = 0;
    int32 at    = (int32)luaL_checkinteger(L, 2);
    if (isList(L, 1, &l, &d))
        cxuiListInsert(l, at, checkStr(L, 3, &text));
    else
        cxuiDropdownInsert(d, at, checkStr(L, 3, &text));
    strDestroy(&text);
    return 0;
}

static int mItemsRemove(lua_State* L)
{
    CxUiList* l;
    CxUiDropdown* d;
    int32 at = (int32)luaL_checkinteger(L, 2);
    if (isList(L, 1, &l, &d))
        cxuiListRemove(l, at);
    else
        cxuiDropdownRemove(d, at);
    return 0;
}

static int mItemsClear(lua_State* L)
{
    CxUiList* l;
    CxUiDropdown* d;
    if (isList(L, 1, &l, &d))
        cxuiListClear(l);
    else
        cxuiDropdownClear(d);
    return 0;
}

static int mItem(lua_State* L)
{
    CxUiList* l;
    CxUiDropdown* d;
    int32 at   = (int32)luaL_checkinteger(L, 2);
    strref s   = isList(L, 1, &l, &d) ? cxuiListItem(l, at) : cxuiDropdownItem(d, at);
    if (!s)
        return 0;
    lua_pushlstring(L, strC(s), strLen(s));
    return 1;
}

static int mSetItems(lua_State* L)
{
    CxUiList* l;
    CxUiDropdown* d;
    bool list = isList(L, 1, &l, &d);
    luaL_checktype(L, 2, LUA_TTABLE);
    sa_string items;
    saInit(&items, string, 8);
    lua_Integer n = (lua_Integer)lua_rawlen(L, 2);
    for (lua_Integer i = 1; i <= n; i++) {
        lua_rawgeti(L, 2, i);
        string item = 0;
        checkStr(L, -1, &item);
        saPushC(&items, string, &item);
        lua_pop(L, 1);
    }
    if (list)
        cxuiListSetItems(l, items);
    else
        cxuiDropdownSetItems(d, items);
    saDestroy(&items);
    return 0;
}

static const luaL_Reg itemsMethods[] = {
    { "append", mItemsAppend }, { "insert", mItemsInsert }, { "remove", mItemsRemove },
    { "clear", mItemsClear },   { "item", mItem },          { "setitems", mSetItems },
    { NULL, NULL },
};

static CxUiList* checkList(lua_State* L, int idx)
{
    return (CxUiList*)cxluaCheckObject(L, idx, &CxUiList_clsinfo);
}

static int mListIsSelected(lua_State* L)
{
    return lua_pushboolean(L, cxuiListIsSelected(checkList(L, 1), (int32)luaL_checkinteger(L, 2))), 1;
}

static int mListSelectedCount(lua_State* L)
{
    return lua_pushinteger(L, cxuiListSelectedCount(checkList(L, 1))), 1;
}

static int mListNextSelected(lua_State* L)
{
    return lua_pushinteger(L, cxuiListNextSelected(checkList(L, 1), (int32)luaL_optinteger(L, 2, -1))), 1;
}

static int mListSetSelected(lua_State* L)
{
    cxuiListSetSelected(checkList(L, 1), (int32)luaL_checkinteger(L, 2), (int32)luaL_optinteger(L, 3, 1),
                        lua_isnoneornil(L, 4) || lua_toboolean(L, 4));
    return 0;
}

static int mListSelectAll(lua_State* L)
{
    cxuiListSelectAll(checkList(L, 1));
    return 0;
}

static int mListClearSelection(lua_State* L)
{
    cxuiListClearSelection(checkList(L, 1));
    return 0;
}

static int mListBeginUpdate(lua_State* L)
{
    cxuiListBeginUpdate(checkList(L, 1));
    return 0;
}

static int mListEndUpdate(lua_State* L)
{
    cxuiListEndUpdate(checkList(L, 1));
    return 0;
}

static const luaL_Reg listMethods[] = {
    { "append", mItemsAppend },
    { "insert", mItemsInsert },
    { "remove", mItemsRemove },
    { "clear", mItemsClear },
    { "item", mItem },
    { "setitems", mSetItems },
    { "isselected", mListIsSelected },
    { "selectedcount", mListSelectedCount },
    { "nextselected", mListNextSelected },
    { "setselected", mListSetSelected },
    { "selectall", mListSelectAll },
    { "clearselection", mListClearSelection },
    { "beginupdate", mListBeginUpdate },
    { "endupdate", mListEndUpdate },
    { NULL, NULL },
};

static CxUiTextField* checkTextField(lua_State* L, int idx)
{
    return (CxUiTextField*)cxluaCheckObject(L, idx, &CxUiTextField_clsinfo);
}

static int mSelectAll(lua_State* L)
{
    cxuiTextFieldSelectAll(checkTextField(L, 1));
    return 0;
}

static int mSelect(lua_State* L)
{
    cxuiTextFieldSelect(checkTextField(L, 1), (int32)luaL_checkinteger(L, 2), (int32)luaL_checkinteger(L, 3));
    return 0;
}

static int mSelection(lua_State* L)
{
    int32 start = 0, end = 0;
    cxuiTextFieldSelection(checkTextField(L, 1), &start, &end);
    lua_pushinteger(L, start);
    lua_pushinteger(L, end);
    return 2;
}

static const luaL_Reg textMethods[] = {
    { "selectall", mSelectAll },
    { "select", mSelect },
    { "selection", mSelection },
    { NULL, NULL },
};

static CxUiProgressDialog* checkProgress(lua_State* L, int idx)
{
    return (CxUiProgressDialog*)cxluaCheckObject(L, idx, &CxUiProgressDialog_clsinfo);
}

static int mSetText(lua_State* L)
{
    string text = 0;
    cxuiProgressDialogSetText(checkProgress(L, 1), checkStr(L, 2, &text));
    strDestroy(&text);
    return 0;
}

static int mSetCount(lua_State* L)
{
    cxuiProgressDialogSetCount(checkProgress(L, 1), (int64)luaL_checkinteger(L, 2));
    return 0;
}

static int mSetTotal(lua_State* L)
{
    cxuiProgressDialogSetTotal(checkProgress(L, 1), (int64)luaL_checkinteger(L, 2));
    return 0;
}

static const luaL_Reg progressMethods[] = {
    { "settext", mSetText },
    { "setcount", mSetCount },
    { "settotal", mSetTotal },
    { NULL, NULL },
};

// ---- timers ----

static CxUiResult timerTick(stvlist* cvars, CxUiTimerEvent* ev)
{
    CxLuaRef* fn = stvlAtPtr(cvars, 0);
    lua_State* L = cxluaRefPush(fn);
    if (!L)
        return CXUI_Default;
    cxluaPushObject(L, objInstBase(ev->timer));
    cxluaPCall(L, 1, 0);
    return CXUI_Default;
}

// cxui.timer{ period = ms, oneshot = true, ontick = function(t) ... end }: a timer, not started
static int newTimer(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_Integer period = optInt(L, 1, "period", 0);
    bool oneshot       = optBool(L, 1, "oneshot", false);
    if (lua_getfield(L, 1, "ontick") != LUA_TFUNCTION)
        return luaL_error(L, "a timer needs an ontick function");
    closure tick = closureCreateAs(CxUiTimerFn, timerTick, stvar(ptr, cxluaRefCreate(L, -1)));
    closureSetDestroy(tick, handlerGone);
    lua_pop(L, 1);
    CxUiTimer* t = CxUiTimer_createWith(timeMS(period), oneshot ? CXUI_Timer_OneShot : 0, tick);
    cxluaPushObject(L, objInstBase(t));
    objRelease(&t);
    if (optBool(L, 1, "start", false))
        cxuiTimerStart((CxUiTimer*)cxluaCheckObject(L, -1, &CxUiTimer_clsinfo));
    return 1;
}

static CxUiTimer* checkTimer(lua_State* L, int idx)
{
    return (CxUiTimer*)cxluaCheckObject(L, idx, &CxUiTimer_clsinfo);
}

static int mTimerStart(lua_State* L)
{
    return lua_pushboolean(L, cxuiTimerStart(checkTimer(L, 1))), 1;
}

static int mTimerStop(lua_State* L)
{
    cxuiTimerStop(checkTimer(L, 1));
    return 0;
}

static int mTimerRunning(lua_State* L)
{
    return lua_pushboolean(L, cxuiTimerIsRunning(checkTimer(L, 1))), 1;
}

static const luaL_Reg timerMethods[] = {
    { "start", mTimerStart },
    { "stop", mTimerStop },
    { "running", mTimerRunning },
    { NULL, NULL },
};

// ---- drawing ----

// A paint handler's drawing context: usable only while the handler runs, which keeps draw set.
typedef struct LuaDraw {
    CxUiDraw* draw;
} LuaDraw;

static const char drawMeta[] = "cxui.draw";

static CxUiDraw* checkDraw(lua_State* L)
{
    LuaDraw* d = luaL_checkudata(L, 1, drawMeta);
    if (!d->draw)
        luaL_error(L, "a drawing context was used after its paint handler returned");
    return d->draw;
}

// 0xRRGGBBAA, or { r, g, b, a } with a defaulting to 255
static CxUiColor checkColor(lua_State* L, int idx)
{
    if (!lua_istable(L, idx))
        return cxuiHexA((uint32)luaL_checkinteger(L, idx));
    uint8 c[4] = { 0, 0, 0, 255 };
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, idx, i + 1);
        c[i] = (uint8)luaL_optinteger(L, -1, c[i]);
        lua_pop(L, 1);
    }
    return cxuiRGBA(c[0], c[1], c[2], c[3]);
}

static int32 checkI32(lua_State* L, int idx)
{
    return (int32)luaL_checkinteger(L, idx);
}

static CxUiFont* checkFont(lua_State* L, int idx)
{
    return (CxUiFont*)cxluaCheckObject(L, idx, &CxUiFont_clsinfo);
}

static CxUiImage* checkImage(lua_State* L, int idx)
{
    return (CxUiImage*)cxluaCheckObject(L, idx, &CxUiImage_clsinfo);
}

// Lua's own string, borrowed while it stays on the stack
static strref borrowStr(lua_State* L, int idx, string* keep)
{
    return checkStr(L, idx, keep);
}

static int dFill(lua_State* L)
{
    cxuiDrawFill(checkDraw(L), checkI32(L, 2), checkI32(L, 3), checkI32(L, 4), checkI32(L, 5), checkColor(L, 6));
    return 0;
}

static int dFrame(lua_State* L)
{
    cxuiDrawFrame(checkDraw(L), checkI32(L, 2), checkI32(L, 3), checkI32(L, 4), checkI32(L, 5), checkI32(L, 6),
                  checkColor(L, 7));
    return 0;
}

static int dLine(lua_State* L)
{
    cxuiDrawLine(checkDraw(L), checkI32(L, 2), checkI32(L, 3), checkI32(L, 4), checkI32(L, 5), checkI32(L, 6),
                 checkColor(L, 7));
    return 0;
}

// dc:text(font, x, y, text, color) -> the width drawn
static int dText(lua_State* L)
{
    CxUiDraw* d = checkDraw(L);
    string s    = 0;
    int32 adv   = cxuiDrawText(d, checkFont(L, 2), checkI32(L, 3), checkI32(L, 4), borrowStr(L, 5, &s), checkColor(L, 6));
    strDestroy(&s);
    return lua_pushinteger(L, adv), 1;
}

// dc:textbox(font, x, y, w, h, text, align, color)
static int dTextBox(lua_State* L)
{
    CxUiDraw* d = checkDraw(L);
    string s    = 0;
    cxuiDrawTextBox(d, checkFont(L, 2), (CxUiRect){ checkI32(L, 3), checkI32(L, 4), checkI32(L, 5), checkI32(L, 6) },
                    borrowStr(L, 7, &s), (flags_t)luaL_checkinteger(L, 8), checkColor(L, 9));
    strDestroy(&s);
    return 0;
}

// dc:cells(font, x, y, text, cellw, color) -> the width drawn
static int dCells(lua_State* L)
{
    CxUiDraw* d = checkDraw(L);
    string s    = 0;
    int32 w     = cxuiDrawTextCells(d, checkFont(L, 2), checkI32(L, 3), checkI32(L, 4), borrowStr(L, 5, &s),
                                    checkI32(L, 6), checkColor(L, 7));
    strDestroy(&s);
    return lua_pushinteger(L, w), 1;
}

// dc:image(img, x, y [, w, h [, sx, sy, sw, sh]])
static int dImage(lua_State* L)
{
    CxUiDraw* d    = checkDraw(L);
    CxUiImage* img = checkImage(L, 2);
    CxUiSize is    = cxuiImageSize(img);
    CxUiRect dst   = { checkI32(L, 3), checkI32(L, 4), (int32)luaL_optinteger(L, 5, is.w), (int32)luaL_optinteger(L, 6, is.h) };
    if (lua_isnoneornil(L, 7)) {
        cxuiDrawImage(d, img, NULL, dst);
    } else {
        CxUiRect src = { checkI32(L, 7), checkI32(L, 8), checkI32(L, 9), checkI32(L, 10) };
        cxuiDrawImage(d, img, &src, dst);
    }
    return 0;
}

static int dClip(lua_State* L)
{
    cxuiDrawClipPush(checkDraw(L), (CxUiRect){ checkI32(L, 2), checkI32(L, 3), checkI32(L, 4), checkI32(L, 5) });
    return 0;
}

static int dUnclip(lua_State* L)
{
    cxuiDrawClipPop(checkDraw(L));
    return 0;
}

static int dSize(lua_State* L)
{
    CxUiSize s = cxuiDrawSize(checkDraw(L));
    lua_pushinteger(L, s.w);
    lua_pushinteger(L, s.h);
    return 2;
}

static int dScale(lua_State* L)
{
    return lua_pushnumber(L, cxuiDrawScale(checkDraw(L))), 1;
}

static const luaL_Reg drawMethods[] = {
    { "fill", dFill },   { "frame", dFrame }, { "line", dLine },     { "text", dText },   { "textbox", dTextBox },
    { "cells", dCells }, { "image", dImage }, { "clip", dClip },     { "unclip", dUnclip }, { "size", dSize },
    { "scale", dScale }, { NULL, NULL },
};

// onpaint(canvas, dc, dirty), with dirty { x, y, w, h } in the canvas's units
static CxUiResult trampPaint(stvlist* cvars, CxUiPaintEvent* ev)
{
    CxLuaRef* fn = stvlAtPtr(cvars, 0);
    lua_State* L = cxluaRefPush(fn);
    if (!L)
        return CXUI_Default;
    cxluaPushObject(L, objInstBase(ev->base.current));
    // A render view's paint has nothing to draw with.
    int dc = LUA_NOREF;
    if (ev->draw) {
        LuaDraw* d = lua_newuserdatauv(L, sizeof(LuaDraw), 0);
        d->draw    = ev->draw;
        luaL_setmetatable(L, drawMeta);
        lua_pushvalue(L, -1);
        dc = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_pushnil(L);
    }
    lua_newtable(L);
    setInt(L, "x", ev->dirty.x);
    setInt(L, "y", ev->dirty.y);
    setInt(L, "w", ev->dirty.w);
    setInt(L, "h", ev->dirty.h);
    bool ok = cxluaPCall(L, 3, 1);
    CxUiResult r = ok && lua_toboolean(L, -1) ? CXUI_Handled : CXUI_Default;
    if (ok)
        lua_pop(L, 1);
    if (dc != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, dc);
        ((LuaDraw*)lua_touserdata(L, -1))->draw = NULL;
        lua_pop(L, 1);
        luaL_unref(L, LUA_REGISTRYINDEX, dc);
    }
    return r;
}

// cxui.font{ faces = "Segoe UI;ui", size = 9, bold = true, italic, underline, scale = 1.5 } or
// with for = widget instead of scale, at that widget's scale
static int newFont(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    string faces = 0;
    optString(L, 1, "faces", &faces);
    uint32 flags = (optBool(L, 1, "bold", false) ? CXUI_FONT_Bold : 0) | (optBool(L, 1, "italic", false) ? CXUI_FONT_Italic : 0) |
                   (optBool(L, 1, "underline", false) ? CXUI_FONT_Underline : 0);
    CxUiFontDesc desc = { .faces = faces, .size = (float32)optNum(L, 1, "size", 0), .flags = flags };
    CxUiFont* f;
    if (optField(L, 1, "for")) {
        f = cxuiFontCreateFor(checkWidget(L, -1), &desc);
        lua_pop(L, 1);
    } else {
        f = cxuiFontCreate(&desc, (float32)optNum(L, 1, "scale", 1));
    }
    strDestroy(&faces);
    cxluaPushObject(L, objInstBase(f));
    objRelease(&f);
    return 1;
}

static int mFontMeasure(lua_State* L)
{
    string s   = 0;
    CxUiSize m = cxuiFontMeasure(checkFont(L, 1), borrowStr(L, 2, &s));
    strDestroy(&s);
    lua_pushinteger(L, m.w);
    lua_pushinteger(L, m.h);
    return 2;
}

static int mFontAdvance(lua_State* L)
{
    string s  = 0;
    int32 adv = cxuiFontAdvance(checkFont(L, 1), borrowStr(L, 2, &s));
    strDestroy(&s);
    return lua_pushinteger(L, adv), 1;
}

static int mFontMetrics(lua_State* L)
{
    CxUiFontMetrics m;
    cxuiFontMetrics(checkFont(L, 1), &m);
    lua_newtable(L);
    setInt(L, "ascent", m.ascent);
    setInt(L, "descent", m.descent);
    setInt(L, "height", m.height);
    setInt(L, "avgchar", m.avgCharW);
    setInt(L, "cellw", m.cellW);
    setInt(L, "cellh", m.cellH);
    return 1;
}

static int mFontScale(lua_State* L)
{
    return lua_pushnumber(L, cxuiFontScale(checkFont(L, 1))), 1;
}

static const luaL_Reg fontMethods[] = {
    { "measure", mFontMeasure },
    { "advance", mFontAdvance },
    { "metrics", mFontMetrics },
    { "scale", mFontScale },
    { NULL, NULL },
};

// cxui.image{ w = 16, h = 16, pixels = s, rgb = false, stride }: from straight RGBA (or RGB)
// bytes in a string
static int newImage(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int32 w = (int32)optInt(L, 1, "w", 0), h = (int32)optInt(L, 1, "h", 0);
    bool rgb     = optBool(L, 1, "rgb", false);
    int32 stride = (int32)optInt(L, 1, "stride", w * (rgb ? 3 : 4));
    if (lua_getfield(L, 1, "pixels") != LUA_TSTRING)
        return luaL_error(L, "an image needs its pixels as a string");
    size_t len       = 0;
    const uint8* px  = (const uint8*)lua_tolstring(L, -1, &len);
    if (w <= 0 || h <= 0 || stride < w * (rgb ? 3 : 4) || len < (size_t)stride * (size_t)h)
        return luaL_error(L, "the pixels don't fill a %dx%d image", (int)w, (int)h);
    CxUiImage* img = rgb ? cxuiImageCreateRGB(w, h, stride, px) : cxuiImageCreateRGBA(w, h, stride, px);
    lua_pop(L, 1);
    cxluaPushObject(L, img ? objInstBase(img) : NULL);
    objRelease(&img);
    return 1;
}

// cxui.cursor{ image = img, x = 0, y = 0 }: a mouse cursor from an image, clicking at x, y
static int newCursor(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "image");
    CxUiImage* img = checkImage(L, -1);
    CxUiCursor* c  = cxuiCursorCreate(img, (int32)optInt(L, 1, "x", 0), (int32)optInt(L, 1, "y", 0));
    lua_pop(L, 1);
    cxluaPushObject(L, objInstBase(c));
    objRelease(&c);
    return 1;
}

// img:sub(x, y, w, h): a part of the image, sharing its pixels
static int mImageSub(lua_State* L)
{
    CxUiImage* sub = cxuiImageCreateSub(checkImage(L, 1), (CxUiRect){ checkI32(L, 2), checkI32(L, 3), checkI32(L, 4), checkI32(L, 5) });
    cxluaPushObject(L, sub ? objInstBase(sub) : NULL);
    objRelease(&sub);
    return 1;
}

static int mImageSize(lua_State* L)
{
    CxUiSize s = cxuiImageSize(checkImage(L, 1));
    lua_pushinteger(L, s.w);
    lua_pushinteger(L, s.h);
    return 2;
}

static const luaL_Reg imageMethods[] = {
    { "sub", mImageSub },
    { "size", mImageSize },
    { NULL, NULL },
};

// ---- functions ----

// cxui.monitors(): a list of tables, the primary first: x, y, w, h, workarea (x, y, w, h), scale,
// primary, id, name, refreshhz and safearea (left, top, right, bottom)
static int fMonitors(lua_State* L)
{
    sa_CxUiMonitor mons = { 0 };
    int32 n             = cxuiMonitors(&mons);
    lua_createtable(L, n, 0);
    for (int32 i = 0; i < n; i++) {
        CxUiMonitor* m = &mons.a[i];
        lua_newtable(L);
        setInt(L, "x", m->bounds.x);
        setInt(L, "y", m->bounds.y);
        setInt(L, "w", m->bounds.w);
        setInt(L, "h", m->bounds.h);
        lua_newtable(L);
        setInt(L, "x", m->workArea.x);
        setInt(L, "y", m->workArea.y);
        setInt(L, "w", m->workArea.w);
        setInt(L, "h", m->workArea.h);
        lua_setfield(L, -2, "workarea");
        lua_newtable(L);
        setInt(L, "left", m->safeArea.left);
        setInt(L, "top", m->safeArea.top);
        setInt(L, "right", m->safeArea.right);
        setInt(L, "bottom", m->safeArea.bottom);
        lua_setfield(L, -2, "safearea");
        setNum(L, "scale", m->scale);
        setNum(L, "refreshhz", m->refreshHz);
        setBool(L, "primary", (m->flags & CXUI_MON_Primary) != 0);
        lua_pushlstring(L, strC(m->id), strLen(m->id));
        lua_setfield(L, -2, "id");
        lua_pushlstring(L, strC(m->name), strLen(m->name));
        lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, i + 1);
    }
    saDestroy(&mons);
    return 1;
}

// cxui.messagebox{ parent, title, text, kind, buttons, custom = { ... }, default, cancel }
static int fMessageBox(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    CxUiWindow* parent = NULL;
    if (optField(L, 1, "parent")) {
        parent = checkWindow(L, -1);
        lua_pop(L, 1);
    }
    string title = 0, text = 0, custom[3] = { 0 };
    optString(L, 1, "title", &title);
    optString(L, 1, "text", &text);
    if (optField(L, 1, "custom")) {
        luaL_checktype(L, -1, LUA_TTABLE);
        for (int i = 0; i < 3; i++) {
            if (lua_rawgeti(L, -1, i + 1) != LUA_TNIL)
                checkStr(L, -1, &custom[i]);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    int32 r = cxuiMessageBox(&(CxUiMessageBoxDesc){ .parent        = parent,
                                                    .title         = title,
                                                    .text          = text,
                                                    .kind          = (uint32)optInt(L, 1, "kind", 0),
                                                    .buttons       = (uint32)optInt(L, 1, "buttons", 0),
                                                    .custom        = { custom[0], custom[1], custom[2] },
                                                    .defaultButton = (int32)optInt(L, 1, "default", 0),
                                                    .cancelButton  = (int32)optInt(L, 1, "cancel", 0) });
    strDestroy(&title);
    strDestroy(&text);
    for (int i = 0; i < 3; i++)
        strDestroy(&custom[i]);
    lua_pushinteger(L, r);
    return 1;
}

// cxui.colordialog{ parent, title, color } -> the color chosen as 0xRRGGBBAA, or nil
static int fColorDialog(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    CxUiWindow* parent = NULL;
    if (optField(L, 1, "parent")) {
        parent = checkWindow(L, -1);
        lua_pop(L, 1);
    }
    string title = 0;
    optString(L, 1, "title", &title);
    CxUiColor c = cxuiRGB(255, 255, 255);
    if (optField(L, 1, "color")) {
        c = checkColor(L, -1);
        lua_pop(L, 1);
    }
    bool chosen = cxuiColorDialog(&(CxUiColorDialogDesc){ .parent = parent, .title = title }, &c);
    strDestroy(&title);
    if (!chosen)
        return 0;
    lua_pushinteger(L, ((lua_Integer)c.r << 24) | ((lua_Integer)c.g << 16) | ((lua_Integer)c.b << 8) | c.a);
    return 1;
}

// cxui.filedialog{ parent, title, mode, filters = { { "Images", "*.png" } }, filterindex,
// defaultext, initialdir, initialfile, nooverwriteprompt } -> path and status, or nil and status
static int fFileDialog(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    CxUiWindow* parent = NULL;
    if (optField(L, 1, "parent")) {
        parent = checkWindow(L, -1);
        lua_pop(L, 1);
    }
    string title = 0, ext = 0, dir = 0, file = 0, path = 0;
    optString(L, 1, "title", &title);
    optString(L, 1, "defaultext", &ext);
    optString(L, 1, "initialdir", &dir);
    optString(L, 1, "initialfile", &file);
    sa_string parts;
    saInit(&parts, string, 8);
    CxUiFileFilter filters[16];
    int32 n = 0;
    if (optField(L, 1, "filters")) {
        luaL_checktype(L, -1, LUA_TTABLE);
        for (lua_Integer i = 1; i <= (lua_Integer)lua_rawlen(L, -1) && n < 16; i++) {
            lua_rawgeti(L, -1, i);
            luaL_checktype(L, -1, LUA_TTABLE);
            for (int j = 1; j <= 2; j++) {
                lua_rawgeti(L, -1, j);
                string s = 0;
                checkStr(L, -1, &s);
                saPushC(&parts, string, &s);
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            n++;
        }
        lua_pop(L, 1);
    }
    for (int32 i = 0; i < n; i++)
        filters[i] = (CxUiFileFilter){ parts.a[i * 2], parts.a[i * 2 + 1] };
    uint32 status = cxuiFileDialog(
        &(CxUiFileDialogDesc){ .parent = parent, .title = title, .mode = (uint32)optInt(L, 1, "mode", 0),
                               .filters = n ? filters : NULL, .nfilters = n,
                               .filterIndex = (int32)optInt(L, 1, "filterindex", 0), .defaultExt = ext,
                               .initialDir = dir, .initialFile = file,
                               .flags = optBool(L, 1, "nooverwriteprompt", false) ? CXUI_FILE_NoOverwritePrompt : 0 },
        &path);
    if (status == CXUI_FILE_Cancelled)
        lua_pushnil(L);
    else
        lua_pushlstring(L, strC(path), strLen(path));
    lua_pushinteger(L, status);
    strDestroy(&title);
    strDestroy(&ext);
    strDestroy(&dir);
    strDestroy(&file);
    strDestroy(&path);
    saDestroy(&parts);
    return 2;
}

static int fKeyName(lua_State* L)
{
    string s = 0;
    if (!cxuiKeyFormat(&s, (CxUiKey)luaL_checkinteger(L, 1), (uint32)luaL_optinteger(L, 2, 0)))
        return 0;
    lua_pushlstring(L, strC(s), strLen(s));
    strDestroy(&s);
    return 1;
}

static int fKeyLabel(lua_State* L)
{
    string s = 0;
    if (!cxuiKeyLabel(&s, (CxUiKey)luaL_checkinteger(L, 1), (uint32)luaL_optinteger(L, 2, 0)))
        return 0;
    lua_pushlstring(L, strC(s), strLen(s));
    strDestroy(&s);
    return 1;
}

static int fParseKey(lua_State* L)
{
    string s   = 0;
    CxUiKey key;
    uint32 mods;
    bool ok = cxuiKeyParse(&key, &mods, checkStr(L, 1, &s));
    strDestroy(&s);
    if (!ok)
        return 0;
    lua_pushinteger(L, key);
    lua_pushinteger(L, mods);
    return 2;
}

// cxui.keylocks(): the cxui.LOCK flags on
static int fKeyLocks(lua_State* L)
{
    lua_pushinteger(L, cxuiKeyLocks());
    return 1;
}

static int fRgba(lua_State* L)
{
    uint32 r = (uint32)luaL_checkinteger(L, 1) & 0xff, g = (uint32)luaL_checkinteger(L, 2) & 0xff,
           b = (uint32)luaL_checkinteger(L, 3) & 0xff, a = (uint32)luaL_optinteger(L, 4, 255) & 0xff;
    lua_pushinteger(L, (lua_Integer)((r << 24) | (g << 16) | (b << 8) | a));
    return 1;
}

static int fFlush(lua_State* L)
{
    unused_noeval(L);
    cxuiFlush();
    return 0;
}

static int fClipboard(lua_State* L)
{
    string s = 0;
    if (!cxuiClipboardGetText(&s))
        return 0;
    lua_pushlstring(L, strC(s), strLen(s));
    strDestroy(&s);
    return 1;
}

static int fSetClipboard(lua_State* L)
{
    string s = 0;
    bool ok  = cxuiClipboardSetText(checkStr(L, 1, &s));
    strDestroy(&s);
    return lua_pushboolean(L, ok), 1;
}

// ---- grids ----

static CxUiGrid* checkGrid(lua_State* L, int idx)
{
    return (CxUiGrid*)cxluaCheckObject(L, idx, &CxUiGrid_clsinfo);
}

static CxUiListStore* checkListStore(lua_State* L, int idx)
{
    return (CxUiListStore*)cxluaCheckObject(L, idx, &CxUiListStore_clsinfo);
}

static CxUiTreeStore* checkTreeStore(lua_State* L, int idx)
{
    return (CxUiTreeStore*)cxluaCheckObject(L, idx, &CxUiTreeStore_clsinfo);
}

static CxUiNodeId checkNode(lua_State* L, int idx)
{
    return (CxUiNodeId)luaL_checkinteger(L, idx);
}

static CxUiNodeId optNode(lua_State* L, int idx)
{
    return (CxUiNodeId)luaL_optinteger(L, idx, 0);
}

// A list store or tree store, or nil for none
static ObjInst* optSource(lua_State* L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return NULL;
    ObjInst* src = cxluaTestObject(L, idx, &CxUiListStore_clsinfo);
    if (!src)
        src = cxluaTestObject(L, idx, &CxUiTreeStore_clsinfo);
    if (!src)
        luaL_argerror(L, idx, "a list store or tree store");
    return src;
}

// A width in DIPs, or { cw = n } in characters
static CxUiLen toLen(lua_State* L, int idx)
{
    if (lua_istable(L, idx))
        return cxuiChars((int32)optInt(L, lua_absindex(L, idx), "cw", 0));
    return cxuiLen((int32)luaL_optinteger(L, idx, 0));
}

static CxUiLen optLen(lua_State* L, int t, const char* key)
{
    lua_getfield(L, t, key);
    CxUiLen len = toLen(L, -1);
    lua_pop(L, 1);
    return len;
}

// The columns in a sequence of { title, width, minwidth, align, kind, flags } tables; titles
// keeps their text. Free the result with xaFree().
static CxUiGridColumn* readColumns(lua_State* L, int idx, sa_string* titles, int32* n)
{
    idx = lua_absindex(L, idx);
    luaL_checktype(L, idx, LUA_TTABLE);
    *n = (int32)lua_rawlen(L, idx);
    // Checked before anything is allocated, so an error leaks nothing
    for (int32 i = 1; i <= *n; i++) {
        if (lua_rawgeti(L, idx, i) != LUA_TTABLE)
            luaL_error(L, "column %d isn't a table", (int)i);
        lua_pop(L, 1);
    }
    saInit(titles, string, 4);
    CxUiGridColumn* cols = xaAlloc((size_t)max(*n, 1) * sizeof(CxUiGridColumn), XA_Zero);
    for (int32 i = 0; i < *n; i++) {
        lua_rawgeti(L, idx, i + 1);
        int c        = lua_gettop(L);
        string title = 0;
        if (lua_getfield(L, c, "title") == LUA_TSTRING) {
            size_t len    = 0;
            const char* t = lua_tolstring(L, -1, &len);
            strFromBytes(&title, t, (uint32)len);
        }
        lua_pop(L, 1);
        saPushC(titles, string, &title);
        cols[i].width    = optLen(L, c, "width");
        cols[i].minWidth = optLen(L, c, "minwidth");
        cols[i].align    = (uint8)optInt(L, c, "align", 0);
        cols[i].kind     = (uint8)optInt(L, c, "kind", 0);
        cols[i].flags    = (flags_t)optInt(L, c, "flags", 0);
        lua_pop(L, 1);
    }
    for (int32 i = 0; i < *n; i++)
        cols[i].title = titles->a[i];
    return cols;
}

static CxUiWidget* makeGrid(lua_State* L, int t, const CxUiLayoutParams* lp, strref name)
{
    lua_getfield(L, t, "source");
    ObjInst* src = optSource(L, -1);
    lua_pop(L, 1);
    sa_string titles = { 0 };
    int32 n          = 0;
    CxUiGridColumn* cols = NULL;
    if (lua_getfield(L, t, "columns") != LUA_TNIL)
        cols = readColumns(L, -1, &titles, &n);
    lua_pop(L, 1);
    CxUiGrid* g = cxuiGridCreate(&(CxUiGridDesc){ .columns = cols, .ncolumns = n, .flags = (flags_t)optInt(L, t, "flags", 0),
                                                  .select = (uint8)optInt(L, t, "select", 0),
                                                  .rowHeight = optLen(L, t, "rowheight"), .lp = *lp, .name = name });
    xaFree(cols);
    saDestroy(&titles);
    CxUiGrid_setSource(g, src);
    return CxUiWidget(g);
}
CONSTRUCTOR(newGrid, makeGrid, "columns", "flags", "select", "source", "rowheight")

static int pushNode(lua_State* L, CxUiNodeId node)
{
    return lua_pushinteger(L, (lua_Integer)node), 1;
}

static int mGridSetSource(lua_State* L)
{
    CxUiGrid_setSource(checkGrid(L, 1), optSource(L, 2));
    return 0;
}

static int mGridSource(lua_State* L)
{
    return cxluaPushObject(L, cxuiGridSource(checkGrid(L, 1))), 1;
}

static int mGridSetColumns(lua_State* L)
{
    CxUiGrid* g = checkGrid(L, 1);
    sa_string titles;
    int32 n;
    CxUiGridColumn* cols = readColumns(L, 2, &titles, &n);
    cxuiGridSetColumnArray(g, cols, n);
    xaFree(cols);
    saDestroy(&titles);
    return 0;
}

static int mGridColumnWidth(lua_State* L)
{
    return lua_pushinteger(L, cxuiGridColumnWidth(checkGrid(L, 1), checkI32(L, 2))), 1;
}

static int mGridSetColumnWidth(lua_State* L)
{
    cxuiGridSetColumnWidth(checkGrid(L, 1), checkI32(L, 2), toLen(L, 3));
    return 0;
}

static int mGridSetColumnHidden(lua_State* L)
{
    cxuiGridSetColumnHidden(checkGrid(L, 1), checkI32(L, 2), lua_toboolean(L, 3));
    return 0;
}

static int mGridFitColumn(lua_State* L)
{
    cxuiGridFitColumn(checkGrid(L, 1), checkI32(L, 2));
    return 0;
}

static int mGridShowRow(lua_State* L)
{
    cxuiGridShowRow(checkGrid(L, 1), luaL_checkinteger(L, 2));
    return 0;
}

static int mGridIsSelected(lua_State* L)
{
    return lua_pushboolean(L, cxuiGridIsSelected(checkGrid(L, 1), luaL_checkinteger(L, 2))), 1;
}

static int mGridNextSelected(lua_State* L)
{
    return lua_pushinteger(L, cxuiGridNextSelected(checkGrid(L, 1), luaL_optinteger(L, 2, -1))), 1;
}

static int mGridSetSelected(lua_State* L)
{
    cxuiGridSetSelected(checkGrid(L, 1), luaL_checkinteger(L, 2), luaL_optinteger(L, 3, 1),
                        lua_isnoneornil(L, 4) || lua_toboolean(L, 4));
    return 0;
}

static int mGridSelectAll(lua_State* L)
{
    cxuiGridSelectAll(checkGrid(L, 1));
    return 0;
}

static int mGridClearSelection(lua_State* L)
{
    cxuiGridClearSelection(checkGrid(L, 1));
    return 0;
}

static int mGridSetSort(lua_State* L)
{
    cxuiGridSetSort(checkGrid(L, 1), checkI32(L, 2), lua_toboolean(L, 3));
    return 0;
}

static int mGridNodeAt(lua_State* L)
{
    return pushNode(L, cxuiGridNodeAt(checkGrid(L, 1), luaL_checkinteger(L, 2)));
}

static int mGridRowOf(lua_State* L)
{
    return lua_pushinteger(L, cxuiGridRowOf(checkGrid(L, 1), checkNode(L, 2))), 1;
}

static int mGridDepthOf(lua_State* L)
{
    return lua_pushinteger(L, cxuiGridDepthOf(checkGrid(L, 1), checkNode(L, 2))), 1;
}

static int mGridIsExpanded(lua_State* L)
{
    return lua_pushboolean(L, cxuiGridIsExpanded(checkGrid(L, 1), checkNode(L, 2))), 1;
}

static int mGridSetExpanded(lua_State* L)
{
    cxuiGridSetExpanded(checkGrid(L, 1), checkNode(L, 2), lua_isnoneornil(L, 3) || lua_toboolean(L, 3));
    return 0;
}

static int mGridExpandAll(lua_State* L)
{
    cxuiGridExpandAll(checkGrid(L, 1), optNode(L, 2));
    return 0;
}

static int mGridCollapseAll(lua_State* L)
{
    cxuiGridCollapseAll(checkGrid(L, 1), optNode(L, 2));
    return 0;
}

static int mGridShowNode(lua_State* L)
{
    cxuiGridShowNode(checkGrid(L, 1), checkNode(L, 2));
    return 0;
}

static int mGridSelectedNode(lua_State* L)
{
    return pushNode(L, cxuiGridSelectedNode(checkGrid(L, 1)));
}

static int mGridIsNodeSelected(lua_State* L)
{
    return lua_pushboolean(L, cxuiGridIsNodeSelected(checkGrid(L, 1), checkNode(L, 2))), 1;
}

static int mGridSetNodeSelected(lua_State* L)
{
    cxuiGridSetNodeSelected(checkGrid(L, 1), checkNode(L, 2), lua_isnoneornil(L, 3) || lua_toboolean(L, 3));
    return 0;
}

static int mGridFocusNode(lua_State* L)
{
    return pushNode(L, cxuiGridFocusNode(checkGrid(L, 1)));
}

static int mGridSetFocusNode(lua_State* L)
{
    cxuiGridSetFocusNode(checkGrid(L, 1), optNode(L, 2));
    return 0;
}

static int mGridEdit(lua_State* L)
{
    return lua_pushboolean(L, cxuiGridEdit(checkGrid(L, 1), (uint64)luaL_checkinteger(L, 2), checkI32(L, 3))), 1;
}

static int mGridEndEdit(lua_State* L)
{
    cxuiGridEndEdit(checkGrid(L, 1), lua_toboolean(L, 2));
    return 0;
}

static int mGridEditor(lua_State* L)
{
    CxUiTextField* ed = cxuiGridEditor(checkGrid(L, 1));
    return cxluaPushObject(L, ed ? objInstBase(ed) : NULL), 1;
}

static int mGridReset(lua_State* L)
{
    cxuiGridReset(checkGrid(L, 1));
    return 0;
}

static const luaL_Reg gridMethods[] = {
    { "setsource", mGridSetSource },
    { "source", mGridSource },
    { "setcolumns", mGridSetColumns },
    { "columnwidth", mGridColumnWidth },
    { "setcolumnwidth", mGridSetColumnWidth },
    { "setcolumnhidden", mGridSetColumnHidden },
    { "fitcolumn", mGridFitColumn },
    { "showrow", mGridShowRow },
    { "isselected", mGridIsSelected },
    { "nextselected", mGridNextSelected },
    { "setselected", mGridSetSelected },
    { "selectall", mGridSelectAll },
    { "clearselection", mGridClearSelection },
    { "setsort", mGridSetSort },
    { "nodeat", mGridNodeAt },
    { "rowof", mGridRowOf },
    { "depthof", mGridDepthOf },
    { "isexpanded", mGridIsExpanded },
    { "setexpanded", mGridSetExpanded },
    { "expandall", mGridExpandAll },
    { "collapseall", mGridCollapseAll },
    { "shownode", mGridShowNode },
    { "selectednode", mGridSelectedNode },
    { "isnodeselected", mGridIsNodeSelected },
    { "setnodeselected", mGridSetNodeSelected },
    { "focusnode", mGridFocusNode },
    { "setfocusnode", mGridSetFocusNode },
    { "edit", mGridEdit },
    { "endedit", mGridEndEdit },
    { "editor", mGridEditor },
    { "reset", mGridReset },
    { NULL, NULL },
};

// The cells' text from idx to the top of the stack; keep holds it. Free the result with xaFree().
static strref* argCells(lua_State* L, int idx, sa_string* keep, int32* n)
{
    int top = lua_gettop(L);
    *n      = max(top - idx + 1, 0);
    for (int i = idx; i <= top; i++)
        luaL_checkstring(L, i);
    saInit(keep, string, max(*n, 1));
    strref* refs = xaAlloc((size_t)max(*n, 1) * sizeof(strref));
    for (int32 i = 0; i < *n; i++) {
        size_t len    = 0;
        const char* c = lua_tolstring(L, idx + i, &len);
        string cell   = 0;
        strFromBytes(&cell, c, (uint32)len);
        saPushC(keep, string, &cell);
    }
    for (int32 i = 0; i < *n; i++)
        refs[i] = keep->a[i];
    return refs;
}

// cxui.liststore{ columns = n }: rows of n cells of text
static int newListStore(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    CxUiListStore* s = cxuiListStoreCreate((int32)optInt(L, 1, "columns", 1));
    cxluaPushObject(L, objInstBase(s));
    objRelease(&s);
    return 1;
}

static int mListRowCount(lua_State* L)
{
    CxUiListStore* s = checkListStore(L, 1);
    return lua_pushinteger(L, cxuiListStoreRowCount(s)), 1;
}

static int mListAppend(lua_State* L)
{
    CxUiListStore* s = checkListStore(L, 1);
    sa_string keep;
    int32 n;
    strref* cells = argCells(L, 2, &keep, &n);
    int64 row     = cxuiListStoreAppendCells(s, cells, n);
    xaFree((void*)cells);
    saDestroy(&keep);
    return lua_pushinteger(L, row), 1;
}

static int mListInsert(lua_State* L)
{
    CxUiListStore* s = checkListStore(L, 1);
    int64 at         = luaL_checkinteger(L, 2);
    sa_string keep;
    int32 n;
    strref* cells = argCells(L, 3, &keep, &n);
    int64 row     = cxuiListStoreInsertCells(s, at, cells, n);
    xaFree((void*)cells);
    saDestroy(&keep);
    return lua_pushinteger(L, row), 1;
}

static int mListRemove(lua_State* L)
{
    cxuiListStoreRemove(checkListStore(L, 1), luaL_checkinteger(L, 2), luaL_optinteger(L, 3, 1));
    return 0;
}

static int mListClear(lua_State* L)
{
    cxuiListStoreClear(checkListStore(L, 1));
    return 0;
}

static int mListText(lua_State* L)
{
    strref t = cxuiListStoreText(checkListStore(L, 1), luaL_checkinteger(L, 2), checkI32(L, 3));
    if (!t)
        return 0;
    return lua_pushlstring(L, strC(t), strLen(t)), 1;
}

static int mListSetText(lua_State* L)
{
    CxUiListStore* s = checkListStore(L, 1);
    string text      = 0;
    cxuiListStoreSetText(s, luaL_checkinteger(L, 2), checkI32(L, 3), checkStr(L, 4, &text));
    strDestroy(&text);
    return 0;
}

static int mListSetFlags(lua_State* L)
{
    cxuiListStoreSetFlags(checkListStore(L, 1), luaL_checkinteger(L, 2), (flags_t)luaL_checkinteger(L, 3));
    return 0;
}

static int mListSetColors(lua_State* L)
{
    CxUiListStore* s = checkListStore(L, 1);
    cxuiListStoreSetColors(s, luaL_checkinteger(L, 2), checkColor(L, 3), checkColor(L, 4));
    return 0;
}

static int mListSetCheck(lua_State* L)
{
    cxuiListStoreSetCheck(checkListStore(L, 1), luaL_checkinteger(L, 2), checkI32(L, 3), (uint32)luaL_checkinteger(L, 4));
    return 0;
}

static int mListCheck(lua_State* L)
{
    return lua_pushinteger(L, cxuiListStoreCheck(checkListStore(L, 1), luaL_checkinteger(L, 2), checkI32(L, 3))), 1;
}

static int mListSort(lua_State* L)
{
    cxuiListStoreSort(checkListStore(L, 1), checkI32(L, 2), lua_toboolean(L, 3));
    return 0;
}

static const luaL_Reg listStoreMethods[] = {
    { "rowcount", mListRowCount }, { "append", mListAppend },       { "insert", mListInsert },
    { "remove", mListRemove },     { "clear", mListClear },         { "text", mListText },
    { "settext", mListSetText },   { "setflags", mListSetFlags },   { "setcolors", mListSetColors },
    { "setcheck", mListSetCheck }, { "check", mListCheck },         { "sort", mListSort },
    { NULL, NULL },
};

// cxui.treestore{ columns = n }: nodes of n cells of text
static int newTreeStore(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    CxUiTreeStore* s = cxuiTreeStoreCreate((int32)optInt(L, 1, "columns", 1));
    cxluaPushObject(L, objInstBase(s));
    objRelease(&s);
    return 1;
}

static int mTreeAppend(lua_State* L)
{
    CxUiTreeStore* s  = checkTreeStore(L, 1);
    CxUiNodeId parent = optNode(L, 2);
    sa_string keep;
    int32 n;
    strref* cells   = argCells(L, 3, &keep, &n);
    CxUiNodeId node = cxuiTreeStoreAppendCells(s, parent, cells, n);
    xaFree((void*)cells);
    saDestroy(&keep);
    return pushNode(L, node);
}

static int mTreeInsert(lua_State* L)
{
    CxUiTreeStore* s  = checkTreeStore(L, 1);
    CxUiNodeId parent = optNode(L, 2), after = optNode(L, 3);
    sa_string keep;
    int32 n;
    strref* cells   = argCells(L, 4, &keep, &n);
    CxUiNodeId node = cxuiTreeStoreInsertCells(s, parent, after, cells, n);
    xaFree((void*)cells);
    saDestroy(&keep);
    return pushNode(L, node);
}

static int mTreeRemove(lua_State* L)
{
    return lua_pushboolean(L, cxuiTreeStoreRemove(checkTreeStore(L, 1), checkNode(L, 2))), 1;
}

static int mTreeClear(lua_State* L)
{
    cxuiTreeStoreClear(checkTreeStore(L, 1));
    return 0;
}

static int mTreeValid(lua_State* L)
{
    return lua_pushboolean(L, cxuiTreeStoreIsValid(checkTreeStore(L, 1), checkNode(L, 2))), 1;
}

static int mTreeChildCount(lua_State* L)
{
    return lua_pushinteger(L, cxuiTreeStoreChildCount(checkTreeStore(L, 1), optNode(L, 2))), 1;
}

static int mTreeFirstChild(lua_State* L)
{
    return pushNode(L, cxuiTreeStoreFirstChild(checkTreeStore(L, 1), optNode(L, 2)));
}

static int mTreeNextSibling(lua_State* L)
{
    return pushNode(L, cxuiTreeStoreNextSibling(checkTreeStore(L, 1), checkNode(L, 2)));
}

static int mTreeParentOf(lua_State* L)
{
    return pushNode(L, cxuiTreeStoreParentOf(checkTreeStore(L, 1), checkNode(L, 2)));
}

static int mTreeText(lua_State* L)
{
    strref t = cxuiTreeStoreText(checkTreeStore(L, 1), checkNode(L, 2), checkI32(L, 3));
    if (!t)
        return 0;
    return lua_pushlstring(L, strC(t), strLen(t)), 1;
}

static int mTreeSetText(lua_State* L)
{
    CxUiTreeStore* s = checkTreeStore(L, 1);
    string text      = 0;
    cxuiTreeStoreSetText(s, checkNode(L, 2), checkI32(L, 3), checkStr(L, 4, &text));
    strDestroy(&text);
    return 0;
}

static int mTreeSetFlags(lua_State* L)
{
    cxuiTreeStoreSetFlags(checkTreeStore(L, 1), checkNode(L, 2), (flags_t)luaL_checkinteger(L, 3));
    return 0;
}

static int mTreeSetColors(lua_State* L)
{
    CxUiTreeStore* s = checkTreeStore(L, 1);
    cxuiTreeStoreSetColors(s, checkNode(L, 2), checkColor(L, 3), checkColor(L, 4));
    return 0;
}

static int mTreeSetCheck(lua_State* L)
{
    cxuiTreeStoreSetCheck(checkTreeStore(L, 1), checkNode(L, 2), checkI32(L, 3), (uint32)luaL_checkinteger(L, 4));
    return 0;
}

static int mTreeCheck(lua_State* L)
{
    return lua_pushinteger(L, cxuiTreeStoreCheck(checkTreeStore(L, 1), checkNode(L, 2), checkI32(L, 3))), 1;
}

static int mTreePropagateCheck(lua_State* L)
{
    cxuiTreeStorePropagateCheck(checkTreeStore(L, 1), checkNode(L, 2), checkI32(L, 3));
    return 0;
}

// A value kept with a node: anything with a cx value, such as a number, string or object
static int mTreeSetData(lua_State* L)
{
    CxUiTreeStore* s = checkTreeStore(L, 1);
    CxUiNodeId node  = checkNode(L, 2);
    stvar v          = stvNone;
    if (!lua_isnoneornil(L, 3) && !cxluaToStvar(L, 3, &v))
        return luaL_argerror(L, 3, "a number, string, boolean or object");
    cxuiTreeStoreSetData(s, node, v);
    stvarDestroy(&v);
    return 0;
}

static int mTreeData(lua_State* L)
{
    stvar v = cxuiTreeStoreData(checkTreeStore(L, 1), checkNode(L, 2));
    if (stvarIs(&v, none))
        return 0;
    cxluaPushStvar(L, v);
    return 1;
}

static const luaL_Reg treeStoreMethods[] = {
    { "append", mTreeAppend },
    { "insert", mTreeInsert },
    { "remove", mTreeRemove },
    { "clear", mTreeClear },
    { "valid", mTreeValid },
    { "childcount", mTreeChildCount },
    { "firstchild", mTreeFirstChild },
    { "nextsibling", mTreeNextSibling },
    { "parentof", mTreeParentOf },
    { "text", mTreeText },
    { "settext", mTreeSetText },
    { "setflags", mTreeSetFlags },
    { "setcolors", mTreeSetColors },
    { "setcheck", mTreeSetCheck },
    { "check", mTreeCheck },
    { "propagatecheck", mTreePropagateCheck },
    { "setdata", mTreeSetData },
    { "data", mTreeData },
    { NULL, NULL },
};

static const luaL_Reg functions[] = {
    { "window", newWindow },
    { "vbox", newVBox },
    { "hbox", newHBox },
    { "panel", newPanel },
    { "menu", newMenu },
    { "menuitem", newMenuItem },
    { "frame", newFrame },
    { "scrollbox", newScrollBox },
    { "zbox", newZBox },
    { "split", newSplit },
    { "fill", newFill },
    { "space", newSpace },
    { "label", newLabel },
    { "button", newButton },
    { "check", newCheck },
    { "radio", newRadio },
    { "radiogroup", newRadioGroup },
    { "textfield", newTextField },
    { "textarea", newTextArea },
    { "textframe", newTextFrame },
    { "list", newList },
    { "dropdown", newDropdown },
    { "slider", newSlider },
    { "progress", newProgress },
    { "canvas", newCanvas },
    { "renderview", newRenderView },
    { "dlabel", newDLabel },
    { "dbutton", newDButton },
    { "dtabs", newDTabs },
    { "tabs", newTabs },
    { "link", newLink },
    { "progressdialog", newProgressDialog },
    { "grid", newGrid },
    { "liststore", newListStore },
    { "treestore", newTreeStore },
    { "timer", newTimer },
    { "font", newFont },
    { "image", newImage },
    { "cursor", newCursor },
    { "monitors", fMonitors },
    { "messagebox", fMessageBox },
    { "filedialog", fFileDialog },
    { "colordialog", fColorDialog },
    { "keyname", fKeyName },
    { "keylabel", fKeyLabel },
    { "parsekey", fParseKey },
    { "keylocks", fKeyLocks },
    { "rgba", fRgba },
    { "rgb", fRgba },
    { "flush", fFlush },
    { "clipboard", fClipboard },
    { "setclipboard", fSetClipboard },
    { NULL, NULL },
};

// ---- constants ----

typedef struct Const {
    const char* name;
    lua_Integer value;
} Const;

static void constTable(lua_State* L, const char* name, const Const* c)
{
    lua_newtable(L);
    for (; c->name; c++) {
        lua_pushinteger(L, c->value);
        lua_setfield(L, -2, c->name);
    }
    lua_setfield(L, -2, name);
}

// cxui.KEY by the keys' own names, such as cxui.KEY.F3 and cxui.KEY.Tab
static void keyTable(lua_State* L)
{
    lua_newtable(L);
    string name = 0;
    for (int32 k = CXUI_KEY_None + 1; k < CXUI_KEY_Count; k++) {
        if (!cxuiKeyFormat(&name, (CxUiKey)k, 0))
            continue;
        lua_pushinteger(L, k);
        lua_setfield(L, -2, strC(name));
    }
    strDestroy(&name);
    lua_setfield(L, -2, "KEY");
}

static void constants(lua_State* L)
{
    keyTable(L);
    constTable(L, "MOD", (const Const[]){ { "shift", CXUI_MOD_Shift }, { "ctrl", CXUI_MOD_Ctrl }, { "alt", CXUI_MOD_Alt },
                                          { "meta", CXUI_MOD_Meta }, { "primary", CXUI_MOD_Primary }, { NULL, 0 } });
    constTable(L, "LOCK", (const Const[]){ { "caps", CXUI_LOCK_Caps }, { "num", CXUI_LOCK_Num },
                                           { "scroll", CXUI_LOCK_Scroll }, { NULL, 0 } });
    constTable(L, "BUTTON", (const Const[]){ { "left", CXUI_BTN_Left }, { "middle", CXUI_BTN_Middle },
                                             { "right", CXUI_BTN_Right }, { "x1", CXUI_BTN_X1 }, { "x2", CXUI_BTN_X2 },
                                             { NULL, 0 } });
    constTable(L, "MOUSE", (const Const[]){ { "down", CXUI_MOUSE_Down }, { "up", CXUI_MOUSE_Up },
                                            { "doubleclick", CXUI_MOUSE_DoubleClick }, { "move", CXUI_MOUSE_Move },
                                            { "enter", CXUI_MOUSE_Enter }, { "leave", CXUI_MOUSE_Leave },
                                            { "capturelost", CXUI_MOUSE_CaptureLost }, { NULL, 0 } });
    constTable(L, "FOCUS", (const Const[]){ { "pointerlock", CXUI_FOCUS_PointerLock }, { NULL, 0 } });
    constTable(L, "TEXTINPUT", (const Const[]){ { "on", CXUI_TEXTINPUT_On }, { NULL, 0 } });
    constTable(L, "KEYCAPTURE", (const Const[]){ { "on", CXUI_KEYCAPTURE_On }, { "systemkey", CXUI_KEYCAPTURE_SystemKey },
                                                 { NULL, 0 } });
    constTable(L, "POINTERLOCK", (const Const[]){ { "off", CXUI_POINTERLOCK_Off }, { "warp", CXUI_POINTERLOCK_Warp },
                                                  { "raw", CXUI_POINTERLOCK_Raw }, { NULL, 0 } });
    constTable(L, "FULLSCREEN", (const Const[]){ { "none", CXUI_FULLSCREEN_None }, { "system", CXUI_FULLSCREEN_System },
                                                 { "monitor", CXUI_FULLSCREEN_Monitor }, { NULL, 0 } });
    constTable(L, "ALIGN", (const Const[]){ { "default", CXUI_ALIGN_Default }, { "start", CXUI_ALIGN_Start },
                                            { "center", CXUI_ALIGN_Center }, { "end", CXUI_ALIGN_End },
                                            { "fill", CXUI_ALIGN_Fill }, { NULL, 0 } });
    constTable(L, "TEXT", (const Const[]){ { "left", CXUI_TEXT_Left }, { "center", CXUI_TEXT_Center },
                                           { "right", CXUI_TEXT_Right }, { "top", CXUI_TEXT_Top },
                                           { "middle", CXUI_TEXT_Middle }, { "bottom", CXUI_TEXT_Bottom }, { NULL, 0 } });
    constTable(L, "EXPAND", (const Const[]){ { "none", CXUI_EXPAND_None }, { "h", CXUI_EXPAND_H }, { "v", CXUI_EXPAND_V },
                                             { "both", CXUI_EXPAND_Both }, { "stoph", CXUI_EXPAND_StopH },
                                             { "stopv", CXUI_EXPAND_StopV }, { NULL, 0 } });
    constTable(L, "ORIENT", (const Const[]){ { "h", CXUI_ORIENT_H }, { "v", CXUI_ORIENT_V }, { NULL, 0 } });
    constTable(L, "SHOW", (const Const[]){ { "normal", CXUI_SHOW_Normal }, { "noactivate", CXUI_SHOW_NoActivate },
                                           { "minimized", CXUI_SHOW_Minimized }, { "maximized", CXUI_SHOW_Maximized },
                                           { NULL, 0 } });
    constTable(L, "NEAR", (const Const[]){ { "center", CXUI_NEAR_Center }, { "right", CXUI_NEAR_Right },
                                           { "left", CXUI_NEAR_Left }, { NULL, 0 } });
    constTable(L, "CLOSE", (const Const[]){ { "hide", CXUI_OnClose_Hide }, { "destroy", CXUI_OnClose_Destroy },
                                            { "none", CXUI_OnClose_None }, { "user", CXUI_CLOSE_User },
                                            { "system", CXUI_CLOSE_System }, { "app", CXUI_CLOSE_App }, { NULL, 0 } });
    constTable(L, "SIZE", (const Const[]){ { "natural", CXUI_SIZE_Natural }, { "fixed", CXUI_SIZE_Fixed },
                                           { "start", CXUI_SIZE_Start }, { NULL, 0 } });
    constTable(L, "WIN", (const Const[]){ { "shrink", CXUI_WINDOW_Shrink }, { "borderless", CXUI_WINDOW_Borderless },
                                          { "normal", CXUI_WINDOW_Normal }, { "minimized", CXUI_WINDOW_Minimized },
                                          { "maximized", CXUI_WINDOW_Maximized },
                                          { "fullscreen", CXUI_WINDOW_Fullscreen }, { NULL, 0 } });
    constTable(L, "THEME", (const Const[]){ { "system", CXUI_Theme_System }, { "light", CXUI_Theme_Light },
                                            { "dark", CXUI_Theme_Dark }, { NULL, 0 } });
    constTable(L, "CHECK", (const Const[]){ { "off", CXUI_CHECK_Off }, { "on", CXUI_CHECK_On }, { "mixed", CXUI_CHECK_Mixed },
                                            { NULL, 0 } });
    constTable(L, "MB", (const Const[]){ { "info", CXUI_MB_Info }, { "warning", CXUI_MB_Warning }, { "error", CXUI_MB_Error },
                                         { "question", CXUI_MB_Question }, { "ok", CXUI_MB_Ok },
                                         { "okcancel", CXUI_MB_OkCancel }, { "yesno", CXUI_MB_YesNo },
                                         { "yesnocancel", CXUI_MB_YesNoCancel }, { "custom", CXUI_MB_Custom },
                                         { NULL, 0 } });
    constTable(L, "FILE", (const Const[]){ { "open", CXUI_FILE_Open }, { "save", CXUI_FILE_Save },
                                           { "folder", CXUI_FILE_Folder }, { "cancelled", CXUI_FILE_Cancelled },
                                           { "existing", CXUI_FILE_Existing }, { "new", CXUI_FILE_New }, { NULL, 0 } });
    constTable(L, "CURSOR", (const Const[]){ { "arrow", CXUI_CURSOR_Arrow }, { "ibeam", CXUI_CURSOR_IBeam },
                                             { "hand", CXUI_CURSOR_Hand }, { "wait", CXUI_CURSOR_Wait },
                                             { "cross", CXUI_CURSOR_Cross }, { "sizewe", CXUI_CURSOR_SizeWE },
                                             { "sizens", CXUI_CURSOR_SizeNS }, { "sizenwse", CXUI_CURSOR_SizeNWSE },
                                             { "sizenesw", CXUI_CURSOR_SizeNESW }, { "move", CXUI_CURSOR_Move },
                                             { "notallowed", CXUI_CURSOR_NotAllowed }, { "none", CXUI_CURSOR_None },
                                             { NULL, 0 } });
    constTable(L, "DRAW", (const Const[]){ { "auto", CXUI_Draw_Auto }, { "d2d", CXUI_Draw_D2D }, { "gdi", CXUI_Draw_GDI },
                                           { NULL, 0 } });
    constTable(L, "UNITS", (const Const[]){ { "pixels", CXUI_Units_Pixels }, { "dip", CXUI_Units_Dip }, { NULL, 0 } });
    constTable(L, "COL", (const Const[]){ { "text", CXUI_COL_Text }, { "tree", CXUI_COL_Tree }, { "check", CXUI_COL_Check },
                                          { "stretch", CXUI_COL_Stretch }, { "hidden", CXUI_COL_Hidden },
                                          { "sortable", CXUI_COL_Sortable }, { "fixedwidth", CXUI_COL_FixedWidth },
                                          { "checkbox", CXUI_COL_CheckBox }, { "editable", CXUI_COL_Editable },
                                          { NULL, 0 } });
    constTable(L, "GRID", (const Const[]){ { "noheader", CXUI_GRID_NoHeader }, { "stripes", CXUI_GRID_Stripes },
                                           { "lines", CXUI_GRID_Lines }, { "treelines", CXUI_GRID_TreeLines },
                                           { "cancelonblur", CXUI_GRID_CancelOnBlur },
                                           { "followtail", CXUI_GRID_FollowTail }, { NULL, 0 } });
    constTable(L, "SURFACE", (const Const[]){ { "none", CXUI_SURFACE_None }, { "win32", CXUI_SURFACE_Win32 },
                                              { "x11", CXUI_SURFACE_X11 }, { "wayland", CXUI_SURFACE_Wayland },
                                              { "metal", CXUI_SURFACE_Metal }, { NULL, 0 } });
    constTable(L, "SELECT", (const Const[]){ { "single", CXUI_SELECT_Single }, { "multi", CXUI_SELECT_Multi },
                                             { "none", CXUI_SELECT_None }, { NULL, 0 } });
    constTable(L, "CELL", (const Const[]){ { "bold", CXUI_CELL_Bold }, { "dim", CXUI_CELL_Dim },
                                           { "checkhidden", CXUI_CELL_CheckHidden }, { "readonly", CXUI_CELL_ReadOnly },
                                           { NULL, 0 } });
    constTable(L, "EDIT", (const Const[]){ { "start", CXUI_EDIT_Start }, { "commit", CXUI_EDIT_Commit },
                                           { "cancel", CXUI_EDIT_Cancel }, { NULL, 0 } });
    constTable(L, "PAL", (const Const[]){ { "panel", CXUI_PAL_Panel }, { "content", CXUI_PAL_Content },
                                          { "border", CXUI_PAL_Border }, { "text", CXUI_PAL_Text },
                                          { "textdim", CXUI_PAL_TextDim }, { "textdisabled", CXUI_PAL_TextDisabled },
                                          { "accent", CXUI_PAL_Accent }, { "selection", CXUI_PAL_Selection },
                                          { "selectiontext", CXUI_PAL_SelectionText }, { "hover", CXUI_PAL_Hover },
                                          { "pressed", CXUI_PAL_Pressed }, { "input", CXUI_PAL_Input },
                                          { "inputtext", CXUI_PAL_InputText }, { "inputborder", CXUI_PAL_InputBorder },
                                          { "inputbad", CXUI_PAL_InputBad }, { "inputcue", CXUI_PAL_InputCue },
                                          { "focus", CXUI_PAL_Focus }, { "scrolltrack", CXUI_PAL_ScrollTrack },
                                          { "scrollthumb", CXUI_PAL_ScrollThumb },
                                          { "scrollthumbhot", CXUI_PAL_ScrollThumbHot },
                                          { "tooltipbg", CXUI_PAL_TooltipBg }, { "tooltiptext", CXUI_PAL_TooltipText },
                                          { "link", CXUI_PAL_Link }, { "linkhover", CXUI_PAL_LinkHover },
                                          { "gridline", CXUI_PAL_GridLine }, { "gridheader", CXUI_PAL_GridHeader },
                                          { "gridheadertext", CXUI_PAL_GridHeaderText }, { NULL, 0 } });
    constTable(L, "MENU", (const Const[]){ { "command", CXUI_MENU_Command }, { "check", CXUI_MENU_Check },
                                           { "radio", CXUI_MENU_Radio }, { "separator", CXUI_MENU_Separator },
                                           { NULL, 0 } });
    constTable(L, "SCROLL", (const Const[]){ { "thumb", CXUI_SCROLL_Thumb }, { "page", CXUI_SCROLL_Page },
                                             { "step", CXUI_SCROLL_Step }, { "wheel", CXUI_SCROLL_Wheel },
                                             { "grabbed", CXUI_SCROLL_ThumbGrabbed },
                                             { "released", CXUI_SCROLL_ThumbReleased }, { NULL, 0 } });
}

_Use_decl_annotations_
int luaopen_cxui(lua_State* L)
{
    cxluaOpen(L);
    cxluaRegisterClass(L, &CxUiWidget_clsinfo, &widgetHooks, widgetMethods);
    cxluaRegisterClass(L, &CxUiWindow_clsinfo, NULL, windowMethods);
    cxluaRegisterClass(L, &CxUiCanvas_clsinfo, NULL, canvasMethods);
    cxluaRegisterClass(L, &CxUiRenderView_clsinfo, NULL, renderViewMethods);
    cxluaRegisterClass(L, &CxUiList_clsinfo, NULL, listMethods);
    cxluaRegisterClass(L, &CxUiDropdown_clsinfo, NULL, itemsMethods);
    cxluaRegisterClass(L, &CxUiTextField_clsinfo, NULL, textMethods);
    cxluaRegisterClass(L, &CxUiProgressDialog_clsinfo, NULL, progressMethods);
    cxluaRegisterClass(L, &CxUiTimer_clsinfo, NULL, timerMethods);
    cxluaRegisterClass(L, &CxUiFont_clsinfo, NULL, fontMethods);
    cxluaRegisterClass(L, &CxUiImage_clsinfo, NULL, imageMethods);
    cxluaRegisterClass(L, &CxUiCursor_clsinfo, NULL, NULL);
    cxluaRegisterClass(L, &CxUiGrid_clsinfo, NULL, gridMethods);
    cxluaRegisterClass(L, &CxUiListStore_clsinfo, NULL, listStoreMethods);
    cxluaRegisterClass(L, &CxUiTreeStore_clsinfo, NULL, treeStoreMethods);
    cxluaRegisterClass(L, &CxUiMenu_clsinfo, NULL, menuMethods);
    cxluaRegisterClass(L, &CxUiMenuItem_clsinfo, NULL, menuItemMethods);
    cxluaRegisterClass(L, &CxUiTabs_clsinfo, NULL, tabsMethods);
    if (luaL_newmetatable(L, drawMeta)) {
        luaL_newlib(L, drawMethods);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);

    if (lua_rawgetp(L, LUA_REGISTRYINDEX, &handlersKey) != LUA_TTABLE) {
        lua_newtable(L);
        lua_newtable(L);
        lua_pushliteral(L, "k");
        lua_setfield(L, -2, "__mode");
        lua_setmetatable(L, -2);
        lua_rawsetp(L, LUA_REGISTRYINDEX, &handlersKey);
    }
    lua_pop(L, 1);

    luaL_newlib(L, functions);
    constants(L);
    return 1;
}
