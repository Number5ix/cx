// The same kinds of widgets built by a Lua script through the cxui module: a form whose button
// writes to a label, a canvas painted from Lua that drags a borderless window, a popup ended by a
// timer, and a message box with custom buttons.

#include "gallery.h"

#include <cxuilua.h>
#include <lualib.h>

static lua_State* L;

static const char script[] =
    "local cxui = cxui\n"
    "local say = say\n"
    "local name = cxui.textfield{ cue = 'Your name', expand = 'h' }\n"
    "local hello = cxui.label{ text = 'Nobody greeted yet', expand = 'h' }\n"
    "local greet = cxui.button{ title = '&Greet', onactivate = function(self)\n"
    "    hello.text = 'Hello, ' .. (name.text ~= '' and name.text or 'nobody') .. '!'\n"
    "    say('greeted ' .. name.text)\n"
    "end }\n"
    "local font = cxui.font{ faces = 'ui', size = 11, bold = true, ['for'] = page }\n"
    "local hits = 0\n"
    "local board = cxui.canvas{ size = { w = 360, h = 90 }, expand = 'h',\n"
    "    onpaint = function(self, dc, dirty)\n"
    "        local w, h = dc:size()\n"
    "        dc:fill(0, 0, w, h, 0x203040ff)\n"
    "        dc:frame(0, 0, w, h, 1, { 120, 160, 200 })\n"
    "        dc:line(0, h - 1, w, 0, 1, 0x406080ff)\n"
    "        dc:textbox(font, 0, 0, w, h, 'Painted from Lua: clicked ' .. hits .. ' times',\n"
    "                   cxui.TEXT.center | cxui.TEXT.middle, 0xe0e8f0ff)\n"
    "    end,\n"
    "    onmouse = function(self, ev)\n"
    "        if ev.action == cxui.MOUSE.down then hits = hits + 1 self:invalidate() end\n"
    "    end }\n"
    "local function popup()\n"
    "    local dlg = cxui.window{ cxui.vbox{ margin = 16, cxui.label{ text = 'A Lua popup; it ends itself in 3 seconds.' } },\n"
    "                             title = 'cxui gallery: Lua popup' }\n"
    "    local t = cxui.timer{ period = 3000, oneshot = true, start = true, ontick = function() dlg:endpopup(5) end }\n"
    "    say('the popup returned ' .. dlg:popup(page.window))\n"
    "    t:stop()\n"
    "    dlg:destroy()\n"
    "end\n"
    "local function ask()\n"
    "    local r = cxui.messagebox{ parent = page.window, title = 'cxui gallery', text = 'Asked from Lua.',\n"
    "                               buttons = cxui.MB.custom, custom = { '&Fine', '&Not now' }, cancel = 2 }\n"
    "    say('the message box returned ' .. r)\n"
    "end\n"
    "local function borderless()\n"
    "    local strip = cxui.canvas{ size = { w = 300, h = 28 }, expand = 'h',\n"
    "        onpaint = function(self, dc) local w, h = dc:size()\n"
    "            dc:fill(0, 0, w, h, 0x305070ff)\n"
    "            dc:textbox(font, 8, 0, w - 16, h, 'Drag here (Lua)', cxui.TEXT.middle, 0xffffffff) end,\n"
    "        onmouse = function(self, ev)\n"
    "            if ev.action == cxui.MOUSE.down and ev.button == cxui.BUTTON.left then ev.window:begindrag() return true end\n"
    "        end }\n"
    "    local w\n"
    "    w = cxui.window{ cxui.vbox{ strip, cxui.vbox{ margin = 12, gap = 8,\n"
    "        cxui.label{ text = 'A borderless window built from Lua.' },\n"
    "        cxui.button{ title = '&Close', onactivate = function() w:close() end } } },\n"
    "        flags = cxui.WIN.borderless, closeaction = cxui.CLOSE.destroy }\n"
    "    w:show()\n"
    "end\n"
    "local planets = cxui.liststore{ columns = 2 }\n"
    "for i, p in ipairs({ { 'Mercury', '0.39' }, { 'Venus', '0.72' }, { 'Earth', '1.00' }, { 'Mars', '1.52' },\n"
    "                     { 'Jupiter', '5.20' }, { 'Saturn', '9.54' } }) do planets:append(p[1], p[2]) end\n"
    "local grid = cxui.grid{ source = planets, size = { h = 120 }, expand = 'h', flags = cxui.GRID.stripes,\n"
    "    columns = { { title = 'Planet', flags = cxui.COL.stretch | cxui.COL.sortable | cxui.COL.editable },\n"
    "                { title = 'AU', width = { cw = 8 }, align = cxui.ALIGN['end'], flags = cxui.COL.sortable } },\n"
    "    onsort = function(self, ev) planets:sort(ev.column, ev.descending) return true end,\n"
    "    onactivate = function(self, ev) say('opened ' .. planets:text(ev.item, 0)) end,\n"
    "    onedit = function(self, ev) if ev.stage == cxui.EDIT.commit then say('renamed to ' .. ev.text) end end }\n"
    "page:append(cxui.vbox{ gap = 8,\n"
    "    cxui.hbox{ gap = 6, name, greet }, hello, board, grid,\n"
    "    cxui.hbox{ gap = 6, cxui.button{ title = '&Popup', onactivate = popup },\n"
    "                        cxui.button{ title = '&Message box', onactivate = ask },\n"
    "                        cxui.button{ title = '&Borderless window', onactivate = borderless } } })\n";

static void luaError(lua_State* state, strref message)
{
    unused_noeval(state);
    galleryStatus(_S"lua", _S"a Lua error; see the console");
    say(_SL("lua: ${string}\n"), stvar(strref, message));
}

static int luaSay(lua_State* state)
{
    size_t len    = 0;
    const char* s = luaL_checklstring(state, 1, &len);
    string text   = 0;
    strFromBytes(&text, s, (uint32)len);
    galleryStatus(_S"lua", text);
    strDestroy(&text);
    return 0;
}

static bool pageGone(stvlist* cvars, stvlist* args)
{
    unused_noeval(cvars);
    unused_noeval(args);
    cxluaClose(L);
    lua_close(L);
    L = NULL;
    return true;
}

static void build(CxUiBox* box)
{
    galleryAdd(box, cxuiLabelCreate(&(CxUiLabelDesc){
                        .text = _S"Everything below is built by a Lua script with the cxui module, and handled in Lua.",
                        .wrap = true }));
    L = luaL_newstate();
    luaL_openlibs(L);
    luaL_requiref(L, "cxui", luaopen_cxui, 1);
    lua_pop(L, 1);
    cxluaSetErrorHandler(L, luaError);
    lua_pushcfunction(L, luaSay);
    lua_setglobal(L, "say");
    cxluaPushObject(L, objInstBase(box));
    lua_setglobal(L, "page");
    cchainAttach(&CxUiWidget(box)->destroyNotify, pageGone, stvNone);
    if (luaL_loadstring(L, script) == LUA_OK)
        cxluaPCall(L, 0, 0);
    else
        luaError(L, (strref)lua_tostring(L, -1));
}

const GalleryPage galleryLua = {
    .name  = _S"lua",
    .title = _S"Lua",
    .build = build,
};
