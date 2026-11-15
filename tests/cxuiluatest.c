#include <cx/string.h>
#include <cxui.h>
#include <cxui/test/cxuitest.h>
#include <cxuilua.h>
#include <lualib.h>

#define TEST_FILE cxuiluatest
#define TEST_FUNCS cxuiluatest_funcs
#include "common.h"

static const CxUiStartDesc nullStart = { .name = _S"Lua UI", .flags = CXUI_Start_Null };

// The last Lua error, for failure messages
static string lastError;

static void keepError(lua_State* L, strref message)
{
    unused_noeval(L);
    strDup(&lastError, message);
}

static lua_State* newState(void)
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    luaL_requiref(L, "cxui", luaopen_cxui, 1);
    lua_pop(L, 1);
    cxluaSetErrorHandler(L, keepError);
    strClear(&lastError);
    return L;
}

static void closeState(lua_State* L)
{
    cxluaClose(L);
    lua_close(L);
}

static bool run(lua_State* L, const char* chunk)
{
    if (luaL_loadstring(L, chunk) != LUA_OK) {
        strCopy(&lastError, (strref)lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return cxluaPCall(L, 0, 0);
}

static CxUiWidget* global(lua_State* L, const char* name)
{
    lua_getglobal(L, name);
    CxUiWidget* w = (CxUiWidget*)cxluaTestObject(L, -1, &CxUiWidget_clsinfo);
    lua_pop(L, 1);
    return w;
}

static int answerBox(lua_State* L)
{
    return lua_pushboolean(L, cxuiTestAnswerMessageBox((int32)luaL_checkinteger(L, 1))), 1;
}

static int answerColor(lua_State* L)
{
    if (lua_isnil(L, 1))
        return lua_pushboolean(L, cxuiTestAnswerColorDialog(NULL)), 1;
    CxUiColor c = cxuiHexA((uint32)luaL_checkinteger(L, 1));
    return lua_pushboolean(L, cxuiTestAnswerColorDialog(&c)), 1;
}

static bool uiTramp(stvlist* cvars, stvlist* args)
{
    unused_noeval(args);
    int (*body)(void) = stvlAtPtr(cvars, 0);
    *(int*)stvlAtPtr(cvars, 1) = body();
    return true;
}

static int onUI(int (*body)(void))
{
    if (!cxuiStart(&nullStart))
        TEST_FAIL(1, _SL("cxuiStart failed"), stvNone);
    int ret = 1;
    cxuiCallSync(closureCreate(uiTramp, stvar(ptr, (void*)body), stvar(ptr, &ret)));
    cxuiShutdown();
    return ret;
}

#define LUA_TEST(name)                       \
    static int name##_body(void);            \
    static int test_cxuilua_##name(void)     \
    {                                        \
        return onUI(name##_body);            \
    }                                        \
    static int name##_body(void)

// Tables build widgets: children by position, the rest as descriptor fields, properties
// (also spelled in lowercase), layout and window settings; the same widget comes back as the same
// value; a positional entry that isn't a widget is an error.
LUA_TEST(build)
{
    lua_State* L = newState();
    bool ran     = run(L, 
                          "field = cxui.textfield{ text = 'path', expand = 'h', readonly = true, name = 'path' }\n"
                          "ok = cxui.button{ title = '&OK', minsize = { w = 80 } }\n"
                          "box = cxui.vbox{ margin = 8, gap = 4, cxui.label{ text = 'Install to:' }, field,\n"
                          "                 cxui.hbox{ cxui.fill{}, ok } }\n"
                          "win = cxui.window{ box, title = 'Settings', size = { w = 300, h = 200 }, defaultenter = ok,\n"
                          "                   closeaction = cxui.CLOSE.destroy }\n"
                          "assert(win:child(0) == box and box:childcount() == 3 and field.parent == box)\n"
                          "assert(field.text == 'path' and field.readOnly and field.readonly and field.name == 'path')\n"
                          "field.readonly = false\n"
                          "ok.align = { 'end', 'center' }\n"
                          "field.text = 'elsewhere'\n"
                          "assert(win.title == 'Settings' and win.closeaction == cxui.CLOSE.destroy)\n"
                          "assert(not pcall(cxui.vbox, { 5 }))\n"
                          "assert(not pcall(cxui.hbox, { ok }), 'ok is in a container already')\n");
    CxUiTextField* field = objDynCast(CxUiTextField, global(L, "field"));
    CxUiWindow* win      = objDynCast(CxUiWindow, global(L, "win"));
    CxUiLayoutParams lp  = { 0 };
    CxUiLayoutParams okl = { 0 };
    CxUiSize size        = { 0 };
    if (field)
        cxuiWidgetLayout(field, &lp);
    if (global(L, "ok"))
        cxuiWidgetLayout(global(L, "ok"), &okl);
    if (win)
        cxuiWindowSize(win, &size);
    bool ok = ran && field && win && strEq(cxuiTextFieldText(field), _S"elsewhere") && !cxuiTextFieldReadOnly(field) &&
              lp.expand == CXUI_EXPAND_H && okl.minSize[0].v == 80 && okl.align[0] == CXUI_ALIGN_End &&
              okl.align[1] == CXUI_ALIGN_Center && size.w == 300 && size.h == 200 &&
              win->closeAction == CXUI_OnClose_Destroy;
    if (win)
        cxuiDestroy(win);
    closeState(L);
    if (!ok)
        TEST_FAIL(1, _SL("ran ${int}; expand ${uint}, minimum ${int}, window ${int}x${int}; ${string}"), stvar(int32, ran),
                  stvar(uint32, lp.expand), stvar(int32, okl.minSize[0].v), stvar(int32, size.w), stvar(int32, size.h), stvar(string, lastError));
    return 0;
}

// Handlers get the widget and the event as a table, and read back from their field; returning
// true handles the event; an erroring handler leaves it unhandled; nil clears the slot.
LUA_TEST(events)
{
    lua_State* L = newState();
    bool built   = run(L, 
                          "presses, keys = 0, {}\n"
                          "function pressed(self, ev) presses = presses + 1 last = ev return true end\n"
                          "button = cxui.button{ title = 'Go', onactivate = pressed }\n"
                          "canvas = cxui.canvas{ onkey = function(self, ev) keys[#keys + 1] = ev return ev.key == cxui.KEY.F3 end }\n"
                          "broken = cxui.button{ onactivate = function() error('on purpose') end }\n"
                          "win = cxui.window{ cxui.vbox{ button, canvas, broken } }\n"
                          "win:show()\n"
                          "assert(button.onactivate == pressed)\n");
    CxUiWidget* button = global(L, "button");
    CxUiWidget* canvas = global(L, "canvas");
    CxUiWidget* broken = global(L, "broken");
    bool clicked       = built && cxuiTestClick(button);
    CxUiResult f3      = built ? cxuiTestKey(canvas, CXUI_KEY_F3, CXUI_MOD_Shift, true) : CXUI_Default;
    CxUiResult f4      = built ? cxuiTestKey(canvas, CXUI_KEY_F4, 0, true) : CXUI_Handled;
    bool brokenRan     = built && cxuiTestClick(broken);
    bool checked       = run(L, "assert(presses == 1 and last.target == button and last.window == win)\n"
                                "assert(#keys == 2 and keys[1].key == cxui.KEY.F3 and keys[1].mods == cxui.MOD.shift and keys[1].down)\n"
                                "button.onactivate = nil\n"
                                "assert(button.onactivate == nil)\n");
    if (checked)
        cxuiTestClick(button);
    bool cleared = run(L, "assert(presses == 1)");
    run(L, "win:destroy()");
    closeState(L);
    if (!built || !clicked || f3 != CXUI_Handled || f4 != CXUI_Default || !brokenRan || !checked || !cleared)
        TEST_FAIL(1, _SL("built ${int}, clicked ${int}, F3 ${int}, F4 ${int}, broken ${int}, checked ${int}, cleared ${int}; ${string}"),
                  stvar(int32, built), stvar(int32, clicked), stvar(int32, f3), stvar(int32, f4), stvar(int32, brokenRan),
                  stvar(int32, checked), stvar(int32, cleared), stvar(string, lastError));
    return 0;
}

// A destroyed widget raises an error when used, and lets go of its handlers.
LUA_TEST(destroyed)
{
    lua_State* L = newState();
    bool ok      = run(L, 
                          "watch = setmetatable({}, { __mode = 'k' })\n"
                          "do\n"
                          "    local b = cxui.button{ title = 'Gone' }\n"
                          "    local fn = function() return b end\n"
                          "    watch[fn] = true\n"
                          "    b.onactivate = fn\n"
                          "    b:destroy()\n"
                          "    local good, err = pcall(function() return b.title end)\n"
                          "    assert(not good and err:find('destroyed'), tostring(err))\n"
                          "end\n"
                          "collectgarbage() collectgarbage()\n"
                          "assert(next(watch) == nil, 'the handler is still held')\n");
    closeState(L);
    if (!ok)
        TEST_FAIL(1, _SL("a destroyed widget wasn't refused, or kept its handler; ${string}"), stvar(string, lastError));
    return 0;
}

// Lists and dropdowns take items in their table and through their methods.
LUA_TEST(lists)
{
    lua_State* L = newState();
    bool ok      = run(L, 
                          "local l = cxui.list{ items = { 'one', 'two' }, value = 1 }\n"
                          "assert(l.count == 2 and l.value == 1 and l:item(0) == 'one')\n"
                          "l:append('three') l:insert(0, 'zero') l:remove(1)\n"
                          "assert(l.count == 3 and l:item(0) == 'zero' and l:item(2) == 'three' and l:item(9) == nil)\n"
                          "local d = cxui.dropdown{ items = { 'a' } }\n"
                          "d:setitems({ 'x', 'y', 'z' }) d.value = 2\n"
                          "assert(d.count == 3 and d.value == 2 and d:item(1) == 'y')\n"
                          "d:clear() assert(d.count == 0)\n"
                          "l:destroy() d:destroy()\n");
    closeState(L);
    if (!ok)
        TEST_FAIL(1, _SL("list or dropdown calls failed; ${string}"), stvar(string, lastError));
    return 0;
}

// Popups from Lua: one that waits, ended by a timer; one that doesn't, ended later; a message box
// with custom buttons.
LUA_TEST(modal)
{
    lua_State* L = newState();
    lua_pushcfunction(L, answerBox);
    lua_setglobal(L, "answerbox");
    bool ok = run(L, 
                     "owner = cxui.window{ title = 'Owner' } owner:show()\n"
                     "dlg = cxui.window{ cxui.label{ text = 'Wait' }, title = 'Popup' }\n"
                     "local t = cxui.timer{ period = 5, oneshot = true, start = true, ontick = function(t) dlg:endpopup(7) end }\n"
                     "assert(dlg:popup(owner) == 7)\n"
                     "local result\n"
                     "assert(dlg:showmodal(owner, function(w, r) result = r end))\n"
                     "assert(result == nil)\n"
                     "dlg:endpopup(4)\n"
                     "assert(result == 4)\n"
                     "local answering = cxui.timer{ period = 5, start = true, ontick = function(t)\n"
                     "    if answerbox(2) then t:stop() end end }\n"
                     "local r = cxui.messagebox{ parent = owner, text = 'Update?', buttons = cxui.MB.custom,\n"
                     "                           custom = { '&Install', '&Later' }, cancel = 2 }\n"
                     "assert(r == 2, 'the box returned ' .. tostring(r))\n"
                     "answering:stop()\n"
                     "dlg:destroy() owner:destroy()\n");
    closeState(L);
    if (!ok)
        TEST_FAIL(1, _SL("a popup, modal show or message box from Lua failed; ${string}"), stvar(string, lastError));
    return 0;
}

// Key names both ways, and colors.
LUA_TEST(keys)
{
    lua_State* L = newState();
    bool ok      = run(L, 
                          "local name = cxui.keyname(cxui.KEY.F3, cxui.MOD.ctrl | cxui.MOD.shift)\n"
                          "local k, m = cxui.parsekey(name)\n"
                          "assert(k == cxui.KEY.F3 and m == cxui.MOD.ctrl | cxui.MOD.shift, name)\n"
                          "assert(cxui.parsekey('Nonsense+Q') == nil)\n"
                          "assert(cxui.rgb(1, 2, 3) == 0x010203ff and cxui.rgba(1, 2, 3, 4) == 0x01020304)\n");
    closeState(L);
    if (!ok)
        TEST_FAIL(1, _SL("key names or colors were wrong; ${string}"), stvar(string, lastError));
    return 0;
}

static int paintNow(lua_State* L)
{
    CxUiCanvas* cv = (CxUiCanvas*)cxluaCheckObject(L, 1, &CxUiCanvas_clsinfo);
    cxuiCanvasInvalidateAll(cv);
    cxuiCanvasPaintNow(cv);
    return 0;
}

// A paint handler draws through its context: fills in both color forms, text in a font, an image
// and a part of it; the context refuses to draw once the handler has returned. Fonts measure, and
// images know their size.
LUA_TEST(drawing)
{
    lua_State* L = newState();
    lua_pushcfunction(L, paintNow);
    lua_setglobal(L, "paintnow");
    bool ok = run(L, "font = cxui.font{ faces = 'ui', size = -12 }\n"
                     "local w, h = font:measure('abc')\n"
                     "assert(w > 0 and h > 0 and font:advance('abc') == w and font:metrics().height > 0)\n"
                     "img = cxui.image{ w = 2, h = 2, pixels = string.rep('\\255\\0\\0\\255', 4) }\n"
                     "local part = img:sub(1, 0, 1, 2)\n"
                     "local iw, ih = part:size()\n"
                     "assert(iw == 1 and ih == 2)\n"
                     "assert(not pcall(cxui.image, { w = 4, h = 4, pixels = 'short' }))\n"
                     "cv = cxui.canvas{ size = { w = 100, h = 50 }, draw = cxui.DRAW.gdi, units = cxui.UNITS.dip,\n"
                     "    onpaint = function(self, dc, dirty)\n"
                     "    kept = dc\n"
                     "    dc:fill(0, 0, dirty.w, dirty.h, 0x102030ff)\n"
                     "    dc:fill(1, 2, 3, 4, { 255, 0, 0 })\n"
                     "    dc:text(font, 5, 6, 'hi', cxui.rgb(0, 0, 255))\n"
                     "    dc:image(part, 7, 8)\n"
                     "end }\n"
                     "win = cxui.window{ cv } win:show()\n"
                     "paintnow(cv)\n"
                     "local good, err = pcall(function() kept:fill(0, 0, 1, 1, 0) end)\n"
                     "assert(not good and err:find('after its paint'), tostring(err))\n"
                     "assert(cv.units == cxui.UNITS.dip and cv.scale > 0)\n"
                     "assert(cv:px(100) == math.floor(100 * cv.scale + 0.5) and cv:dip(cv:px(100)) == 100)\n");
    CxUiCanvas* cv         = objDynCast(CxUiCanvas, global(L, "cv"));
    CxUiTestCanvasInfo ci  = { 0 };
    bool painted           = cv && cxuiTestCanvasInfo(cv, &ci) && ci.nops >= 4;
    int32 fills = 0, texts = 0, images = 0;
    bool red = false;
    for (int32 i = 0; painted && i < ci.nops; i++) {
        const CxUiTestDrawOp* op = &ci.ops[i];
        if (op->kind == CXUI_TESTOP_Fill) {
            fills++;
            red = red || (op->c.r == 255 && op->c.g == 0 && op->c.a == 255 && op->r.x == 1 && op->r.h == 4);
        }
        texts += op->kind == CXUI_TESTOP_Text && strEq(op->text, _S"hi");
        images += op->kind == CXUI_TESTOP_Image && op->src.x == 1 && op->r.x == 7;
    }
    run(L, "win:destroy()");
    closeState(L);
    if (!ok || !painted || fills < 2 || !red || texts != 1 || images != 1)
        TEST_FAIL(1, _SL("ran ${int}, painted ${int} with ${int} operations: ${int} fills (red ${int}), ${int} texts, "
                         "${int} images; ${string}"),
                  stvar(int32, ok), stvar(int32, painted), stvar(int32, ci.nops), stvar(int32, fills), stvar(int32, red),
                  stvar(int32, texts), stvar(int32, images), stvar(string, lastError));
    return 0;
}

// Grids and their stores: list and tree stores through their methods; a grid built from a table
// with columns, flags and a source; its properties and methods; events from a header click and
// editing; a tree's nodes opened, selected and focused; bad columns and sources refused.
LUA_TEST(grids)
{
    lua_State* L = newState();
    bool built   = run(L,
                          "files = cxui.liststore{ columns = 2 }\n"
                          "for i = 0, 9 do files:append('file ' .. i, i .. ' KB') end\n"
                          "assert(files:rowcount() == 10 and files:text(3, 0) == 'file 3')\n"
                          "files:insert(0, 'first', '0 KB') files:remove(1)\n"
                          "files:settext(0, 1, '1 KB') files:setflags(2, cxui.CELL.bold) files:setcheck(2, 0, cxui.CHECK.on)\n"
                          "files:setcolors(3, 0xFF0000FF, { 0, 0, 255 })\n"
                          "assert(files:rowcount() == 10 and files:check(2, 0) == cxui.CHECK.on and files:text(0, 1) == '1 KB')\n"
                          "assert(files:text(0, 0) == 'first' and files:text(99, 0) == nil)\n"
                          "sorts, edits = {}, {}\n"
                          "grid = cxui.grid{ columns = {\n"
                          "        { title = 'Name', flags = cxui.COL.stretch | cxui.COL.sortable | cxui.COL.editable | cxui.COL.checkbox },\n"
                          "        { title = 'Size', width = { cw = 8 }, align = cxui.ALIGN['end'] } },\n"
                          "    flags = cxui.GRID.stripes | cxui.GRID.followtail, select = cxui.SELECT.multi, source = files,\n"
                          "    onsort = function(g, ev) sorts[#sorts + 1] = ev files:sort(ev.column, ev.descending) return true end,\n"
                          "    onedit = function(g, ev) edits[#edits + 1] = ev end }\n"
                          "win = cxui.window{ grid, size = { w = 300, h = 200 } }\n"
                          "win:show()\n"
                          "assert(grid.rowcount == 10 and grid:source() == files and grid.columncount == 2)\n"
                          "assert(grid.selectmode == cxui.SELECT.multi and grid:columnwidth(1) > 0)\n"
                          "grid:setselected(2, 3) assert(grid.selectedcount == 3 and grid:isselected(3) and grid:nextselected(2) == 3)\n"
                          "grid:clearselection() assert(grid.selected == -1)\n"
                          "grid:selectall() assert(grid.selectedcount == 10) grid:clearselection()\n"
                          "grid.focusrow = 4 assert(grid.focusrow == 4)\n"
                          "grid:setsort(1, true) assert(grid.sortcolumn == 1 and grid.sortdescending)\n"
                          "grid:setcolumnwidth(1, 90) grid:setcolumnhidden(1, true) grid:setcolumnhidden(1, false)\n"
                          "assert(grid:edit(1, 0) and grid:editor() ~= nil and edits[1].stage == cxui.EDIT.start)\n"
                          "assert(edits[1].item == 1 and edits[1].column == 0 and edits[1].text == files:text(1, 0))\n"
                          "grid:endedit(false)\n"
                          "assert(edits[2].stage == cxui.EDIT.cancel and grid:editor() == nil)\n"
                          "assert(not pcall(cxui.grid, { source = cxui.label{} }))\n"
                          "assert(not pcall(cxui.grid, { columns = { 5 } }))\n");
    CxUiGrid* grid = built ? objDynCast(CxUiGrid, global(L, "grid")) : NULL;
    if (grid) {
        // A click on Name's header sorts by it, ascending.
        cxuiTestPointer(CxUiCanvas(grid->_body), CXUI_TESTPTR_Press, CXUI_BTN_Left, CXUI_BTN_Left, 0, 20, 5);
        cxuiTestPointer(CxUiCanvas(grid->_body), CXUI_TESTPTR_Release, CXUI_BTN_Left, 0, 0, 20, 5);
    }
    bool sorted = grid && run(L,
                                  "assert(#sorts == 1 and sorts[1].column == 0 and not sorts[1].descending)\n"
                                  "assert(files:text(0, 0) == 'file 1' and grid.sortcolumn == 0)\n"
                                  "grid:setcolumns({ { title = 'Only' } }) assert(grid.columncount == 1)\n"
                                  "grid:setsource(nil) assert(grid:source() == nil and grid.rowcount == 0)\n"
                                  "win:destroy()\n");
    bool tree = sorted && run(L,
                                  "local t = cxui.treestore{ columns = 1 }\n"
                                  "a = t:append(0, 'a') a1 = t:append(a, 'a1') b = t:append(0, 'b')\n"
                                  "a0 = t:insert(a, 0, 'a0')\n"
                                  "assert(t:childcount(a) == 2 and t:childcount() == 2 and t:firstchild(a) == a0)\n"
                                  "assert(t:nextsibling(a0) == a1 and t:parentof(a1) == a and t:text(a1, 0) == 'a1')\n"
                                  "t:setdata(a, 42) t:setdata(b, 'bee') assert(t:data(a) == 42 and t:data(b) == 'bee')\n"
                                  "t:setdata(b, nil) assert(t:data(b) == nil)\n"
                                  "t:setcheck(a, 0, cxui.CHECK.on) t:propagatecheck(a, 0) assert(t:check(a1, 0) == cxui.CHECK.on)\n"
                                  "t:settext(b, 0, 'bee') t:setflags(b, cxui.CELL.dim) t:setcolors(b, 0xFF0000FF, 0)\n"
                                  "tree = cxui.grid{ columns = { { title = 'Name', kind = cxui.COL.tree, flags = cxui.COL.stretch } },\n"
                                  "                  source = t, select = cxui.SELECT.multi }\n"
                                  "win = cxui.window{ tree, size = { w = 300, h = 200 } } win:show()\n"
                                  "assert(tree.rowcount == 2)\n"
                                  "tree:setexpanded(a)\n"
                                  "assert(tree:isexpanded(a) and tree.rowcount == 4 and tree:rowof(a1) == 2 and tree:nodeat(2) == a1)\n"
                                  "assert(tree:depthof(a1) == 1)\n"
                                  "tree:collapseall() assert(tree.rowcount == 2) tree:expandall(a) assert(tree.rowcount == 4)\n"
                                  "tree:setnodeselected(a1) assert(tree:selectednode() == a1 and tree:isnodeselected(a1))\n"
                                  "tree:setfocusnode(b) assert(tree:focusnode() == b) tree:shownode(a1)\n"
                                  "assert(t:remove(a1) and not t:valid(a1) and tree.rowcount == 3)\n"
                                  "tree:reset() assert(tree.rowcount == 3)\n"
                                  "win:destroy()\n");
    closeState(L);
    if (!built || !sorted || !tree)
        TEST_FAIL(1, _SL("built ${int}, sorted ${int}, tree ${int}; ${string}"), stvar(int32, built), stvar(int32, sorted),
                  stvar(int32, tree), stvar(string, lastError));
    return 0;
}

// Menus built from tables: items by position, kinds, hints, submenus, handlers; a window's menu
// bar as a property; items chosen, checked and cleared.
LUA_TEST(menus)
{
    lua_State* L = newState();
    bool ran     = run(L,
                          "chosen = {}\n"
                          "grid = cxui.menuitem{ title = '&Grid', kind = cxui.MENU.check, checked = true,\n"
                          "                      onactivate = function(it) chosen[#chosen + 1] = it.checked end }\n"
                          "view = cxui.menu{ grid, cxui.menuitem{ kind = cxui.MENU.separator },\n"
                          "                  cxui.menuitem{ title = '&Off', kind = cxui.MENU.radio, checked = true },\n"
                          "                  cxui.menuitem{ title = 'O&n', kind = cxui.MENU.radio } }\n"
                          "bar = cxui.menu{ cxui.menuitem{ title = '&View', submenu = view, hint = 'Alt+V' } }\n"
                          "win = cxui.window{ cxui.label{ text = 'x' }, menu = bar }\n"
                          "win:show()\n"
                          "assert(win.menu == bar and bar:childcount() == 1 and view:childcount() == 4)\n"
                          "assert(bar:child(0).submenu == view and bar:child(0).hint == 'Alt+V' and grid.kind == cxui.MENU.check)\n"
                          "grid:choose()\n"
                          "assert(#chosen == 1 and chosen[1] == false and not grid.checked)\n"
                          "view:child(3).checked = true\n"
                          "assert(not view:child(2).checked and view:child(3).checked)\n"
                          "grid.title = '&Grid Lines' assert(grid.title == '&Grid Lines')\n"
                          "view:clear() assert(view:childcount() == 0)\n"
                          "win.menu = nil assert(win.menu == nil)\n"
                          "win:destroy()\n"
                          "assert(not pcall(cxui.menu, { cxui.label{} }))\n");
    closeState(L);
    if (!ran)
        TEST_FAIL(1, _SL("${string}"), stvar(string, lastError));
    return 0;
}

// A list where any number can be selected: selection calls named as a grid's, updates held back,
// the top item as a property; a Ctrl-click by the user reaches the handler.
LUA_TEST(listmulti)
{
    lua_State* L = newState();
    bool ran     = run(L,
                          "changes = 0\n"
                          "list = cxui.list{ items = { 'a', 'b', 'c', 'd' }, select = cxui.SELECT.multi,\n"
                          "                  onchange = function(l) changes = changes + 1 end }\n"
                          "win = cxui.window{ list }\n"
                          "win:show()\n"
                          "assert(list:isselected(0) and list:selectedcount() == 1)\n"
                          "list:setselected(2, 2) list:setselected(0, 1, false)\n"
                          "assert(list:nextselected() == 2 and list:nextselected(2) == 3 and list:nextselected(3) == -1)\n"
                          "list:beginupdate() list:append('e') list:selectall() list:endupdate()\n"
                          "assert(list:selectedcount() == 5 and list.value == 0)\n"
                          "list:clearselection() assert(list:selectedcount() == 0)\n"
                          "list.topItem = 3 assert(list.topItem == 3)\n");
    CxUiList* l = ran ? objDynCast(CxUiList, global(L, "list")) : NULL;
    bool picked = l && l->_select == CXUI_SELECT_Multi && cxuiTestToggle(l, 1) &&
                  run(L, "assert(changes == 1 and list:isselected(1))\n"
                         "win:destroy()\n");
    closeState(L);
    if (!ran || !picked)
        TEST_FAIL(1, _SL("ran ${int}, toggled ${int}: ${string}"), stvar(int32, ran), stvar(int32, picked),
                  stvar(string, lastError));
    return 0;
}

// A render view: its surface table once shown, and the surface event; nil before.
LUA_TEST(renderview)
{
    lua_State* L = newState();
    bool ran     = run(L,
                          "events = {}\n"
                          "view = cxui.renderview{ onsurface = function(v, e) events[#events + 1] = e.available end }\n"
                          "assert(view:surface() == nil)\n"
                          "win = cxui.window{ view, size = { 200, 100 } }\n"
                          "win:show()\n");
    cxuiFlush();
    bool after = ran && run(L, "local s = view:surface()\n"
                               "assert(s and s.kind == cxui.SURFACE.none and s.w > 0 and s.h > 0)\n"
                               "assert(#events == 1 and events[1] == true)\n"
                               "win:destroy()\n"
                               "assert(#events == 2 and events[2] == false)\n");
    closeState(L);
    if (!ran || !after)
        TEST_FAIL(1, _SL("ran ${int}, after showing ${int}: ${string}"), stvar(int32, ran), stvar(int32, after),
                  stvar(string, lastError));
    return 0;
}

// A canvas's text input, key capture and pointer lock from Lua: typed text and the lock's motion
// reach the handlers, and a window goes fullscreen.
LUA_TEST(canvasinput)
{
    lua_State* L = newState();
    bool ran     = run(L,
                          "typed, moved = '', {}\n"
                          "cv = cxui.canvas{ ontext = function(c, e) typed = typed .. e.text return true end,\n"
                          "                  onmouse = function(c, e) if e.action == cxui.MOUSE.move then\n"
                          "                      moved[#moved + 1] = { e.x, e.y, e.dx, e.dy } end return true end }\n"
                          "win = cxui.window{ cv }\n"
                          "win:show()\n"
                          "assert(cv:settextinput(cxui.TEXTINPUT.on))\n"
                          "cv:settextcaret(1, 2, 1, 10)\n"
                          "assert(cv:setkeycapture(cxui.KEYCAPTURE.on))\n"
                          "assert(cv:setpointerlock(cxui.POINTERLOCK.raw))\n"
                          "assert(cv:pointerlockmode() == cxui.POINTERLOCK.raw)\n"
                          "assert(win:setfullscreen(cxui.FULLSCREEN.system))\n"
                          "assert(win:fullscreen() == cxui.FULLSCREEN.system)\n"
                          "local mons = cxui.monitors()\n"
                          "assert(#mons == 1 and mons[1].primary and mons[1].id == 'null-0' and mons[1].refreshhz == 60)\n"
                          "assert(win:setfullscreen(cxui.FULLSCREEN.monitor, mons[1]) and win:monitor() == 1)\n"
                          "local img = cxui.image{ w = 2, h = 2, pixels = string.rep('\\0', 16) }\n"
                          "cv:setcursorimage(cxui.cursor{ image = img, x = 1, y = 1 })\n"
                          "cv:setcursorimage(nil)\n"
                          "assert(win:settopmost(true) and win:topmost() and not win:occluded())\n");
    CxUiCanvas* cv = ran ? objDynCast(CxUiCanvas, global(L, "cv")) : NULL;
    if (cv) {
        cxuiTestText(cv, _S"ok");
        cxuiTestPointerMotion(cv, 3, -4, 0);
    }
    bool after = cv && run(L, "assert(typed == 'ok', typed)\n"
                              "local w, h = cv:size()\n"
                              "assert(#moved == 1 and moved[1][1] == w // 2 and moved[1][2] == h // 2 and moved[1][3] == 3 and\n"
                              "       moved[1][4] == -4)\n"
                              "assert(cv:setpointerlock(cxui.POINTERLOCK.off))\n"
                              "assert(cv:warppointer(3, 4))\n"
                              "win:destroy()\n");
    closeState(L);
    if (!ran || !after)
        TEST_FAIL(1, _SL("ran ${int}, after input ${int}: ${string}"), stvar(int32, ran), stvar(int32, after),
                  stvar(string, lastError));
    return 0;
}

// Native tabs from a table: pages by position with titles in order, a background role, titles read
// and changed, pages added; a pick by the user reaches the handler, one from code doesn't.
LUA_TEST(tabs)
{
    lua_State* L = newState();
    bool ran     = run(L,
                          "picks = {}\n"
                          "tabs = cxui.tabs{ cxui.vbox{}, cxui.vbox{}, titles = { '&One', 'T&wo' }, bgrole = cxui.PAL.panel,\n"
                          "                  onchange = function(t) picks[#picks + 1] = t.active end }\n"
                          "win = cxui.window{ tabs }\n"
                          "win:show()\n"
                          "assert(tabs:title(0) == '&One' and tabs:title(1) == 'T&wo' and tabs:childcount() == 2 and tabs.active == 0)\n"
                          "tabs:add(cxui.label{ text = 'three' }, 'Th&ree')\n"
                          "tabs:settitle(0, 'First')\n"
                          "assert(tabs:title(0) == 'First' and tabs:title(2) == 'Th&ree' and tabs:title(3) == nil)\n"
                          "tabs.active = 2 assert(tabs.active == 2 and #picks == 0)\n"
                          "tabs:settitle(1) assert(tabs:title(1) == nil)\n"
                          "assert(not pcall(tabs.add, tabs, cxui.label{}))\n");
    CxUiTabs* tabs = ran ? objDynCast(CxUiTabs, global(L, "tabs")) : NULL;
    bool picked    = tabs && tabs->_bgRole == CXUI_PAL_Panel && cxuiTestChoose(CxUiWidget(tabs), 1, false) &&
                  run(L, "assert(#picks == 1 and picks[1] == 1)\n"
                         "win:destroy()\n");
    closeState(L);
    if (!ran || !picked)
        TEST_FAIL(1, _SL("ran ${int}, picked ${int}: ${string}"), stvar(int32, ran), stvar(int32, picked),
                  stvar(string, lastError));
    return 0;
}

// Panels, frames and scroll boxes take a palette role or a color of their own for their background.
LUA_TEST(backgrounds)
{
    lua_State* L = newState();
    bool ran     = run(L,
                          "panel = cxui.panel{ bgrole = cxui.PAL.content }\n"
                          "frame = cxui.frame{ bg = 0x102030FF }\n"
                          "scroll = cxui.scrollbox{ bgrole = cxui.PAL.input, bg = { 1, 2, 3 } }\n"
                          "plain = cxui.scrollbox{}\n"
                          "text = cxui.textframe{ cxui.textfield{ readonly = true }, noborder = true, bgrole = cxui.PAL.panel }\n"
                          "assert(cxui.grid{ rowheight = 23 }._rowheight == nil)\n"
                          "assert(not pcall(cxui.panel, { bgrole = 99 }))\n");
    CxUiPanel* panel  = ran ? objDynCast(CxUiPanel, global(L, "panel")) : NULL;
    CxUiPanel* frame  = ran ? objDynCast(CxUiPanel, global(L, "frame")) : NULL;
    CxUiPanel* scroll = ran ? objDynCast(CxUiPanel, global(L, "scroll")) : NULL;
    CxUiPanel* plain  = ran ? objDynCast(CxUiPanel, global(L, "plain")) : NULL;
    bool ok = panel && frame && scroll && plain && panel->_bgRole == CXUI_PAL_Content && !panel->_bg.bits &&
              frame->_bg.bits == cxuiHex(0x102030).bits && scroll->_bgRole == CXUI_PAL_Input &&
              scroll->_bg.bits == cxuiRGB(1, 2, 3).bits && !plain->_bg.bits && plain->_bgRole != CXUI_PAL_Input;
    closeState(L);
    if (!ok)
        TEST_FAIL(1, _SL("ran ${int}; ${string}"), stvar(int32, ran), stvar(string, lastError));
    return 0;
}

// A window's owner as a property, a window moved beside its owner, and a color dialog answered
// and cancelled.
LUA_TEST(owned)
{
    lua_State* L = newState();
    lua_pushcfunction(L, answerColor);
    lua_setglobal(L, "answercolor");
    bool ok = run(L,
                  "local main = cxui.window{ title = 'Main', size = { w = 400, h = 300 }, sizing = cxui.SIZE.start }\n"
                  "local tool = cxui.window{ title = 'Tool', size = { w = 100, h = 100 }, sizing = cxui.SIZE.start }\n"
                  "main:setposition(10, 20) main:show()\n"
                  "tool.owner = main\n"
                  "assert(tool.owner == main)\n"
                  "assert(tool:movenear(nil, cxui.NEAR.right) and tool:realize())\n"
                  "local x, y = tool:position()\n"
                  "assert(x == 410 and y == 120, 'beside its owner at ' .. x .. ',' .. y)\n"
                  "local answering = cxui.timer{ period = 5, start = true, ontick = function(t)\n"
                  "    if answercolor(0x204060ff) then t:stop() end end }\n"
                  "local c = cxui.colordialog{ parent = main, title = 'Tint', color = { 1, 2, 3, 128 } }\n"
                  "assert(c == 0x20406080, 'chose ' .. tostring(c))\n"
                  "answering = cxui.timer{ period = 5, start = true, ontick = function(t)\n"
                  "    if answercolor(nil) then t:stop() end end }\n"
                  "assert(cxui.colordialog{ color = 0xffffffff } == nil)\n"
                  "tool.owner = nil\n"
                  "assert(tool.owner == nil)\n"
                  "tool:destroy() main:destroy()\n");
    closeState(L);
    if (!ok)
        TEST_FAIL(1, _SL("an owned window or color dialog from Lua failed; ${string}"), stvar(string, lastError));
    return 0;
}

static int test_cxuilua_grp_module(void)
{
    TEST_CHAIN(test_cxuilua_build, test_cxuilua_events, test_cxuilua_destroyed, test_cxuilua_lists, test_cxuilua_modal,
               test_cxuilua_keys, test_cxuilua_drawing, test_cxuilua_grids, test_cxuilua_backgrounds, test_cxuilua_menus,
               test_cxuilua_tabs, test_cxuilua_listmulti, test_cxuilua_renderview, test_cxuilua_owned,
               test_cxuilua_canvasinput);
}

testfunc cxuiluatest_funcs[] = {
    { "build", test_cxuilua_build },
    { "events", test_cxuilua_events },
    { "destroyed", test_cxuilua_destroyed },
    { "lists", test_cxuilua_lists },
    { "modal", test_cxuilua_modal },
    { "keys", test_cxuilua_keys },
    { "drawing", test_cxuilua_drawing },
    { "grids", test_cxuilua_grids },
    { "backgrounds", test_cxuilua_backgrounds },
    { "menus", test_cxuilua_menus },
    { "tabs", test_cxuilua_tabs },
    { "listmulti", test_cxuilua_listmulti },
    { "renderview", test_cxuilua_renderview },
    { "owned", test_cxuilua_owned },
    { "canvasinput", test_cxuilua_canvasinput },
    { "grp_module", test_cxuilua_grp_module },
    { 0, 0 }
};
