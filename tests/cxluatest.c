#include "objtestobj.h"
#include <cx/string.h>
#include <cx/thread.h>
#include <cxlua.h>
#include <lualib.h>

#define TEST_FILE cxluatest
#define TEST_FUNCS cxluatest_funcs
#include "common.h"

static lua_State* newState(void)
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    cxluaOpen(L);
    return L;
}

static void closeState(lua_State* L)
{
    cxluaClose(L);
    lua_close(L);
}

// Runs a chunk; its error, if any, goes to the log.
static bool run(lua_State* L, const char* chunk)
{
    if (luaL_loadstring(L, chunk) != LUA_OK) {
        logFmt(Error, _SL("${string}"), stvar(strref, (strref)lua_tostring(L, -1)));
        lua_pop(L, 1);
        return false;
    }
    return cxluaPCall(L, 0, 0);
}

static int doubled(lua_State* L)
{
    PropBase* o = (PropBase*)cxluaCheckObject(L, 1, &PropBase_clsinfo);
    lua_pushinteger(L, (lua_Integer)o->vnum * 2);
    return 1;
}

static int childOnly(lua_State* L)
{
    lua_pushstring(L, "child");
    return 1;
}

static bool hookGet(lua_State* L, ObjInst* obj, const char* key)
{
    unused_noeval(obj);
    if (strcmp(key, "answer") != 0)
        return false;
    lua_pushinteger(L, 42);
    return true;
}

static bool hookSet(lua_State* L, ObjInst* obj, const char* key, int val)
{
    if (strcmp(key, "answer") != 0)
        return false;
    ((PropBase*)obj)->vbig = luaL_checkinteger(L, val);
    return true;
}

static const CxLuaClassHooks fieldHooks = { .getField = hookGet, .setField = hookSet };

// Properties read and set by name, refused values raising errors; methods registered for a class
// reach its subclasses, with a subclass's own added or replacing them; a hook's fields, also on a
// subclass registered without hooks; other fields kept on the userdata; one userdata per object.
static int test_cxlua_objects(void)
{
    lua_State* L = newState();
    static const luaL_Reg baseMethods[]  = { { "doubled", doubled }, { "label2", doubled }, { NULL, NULL } };
    static const luaL_Reg childMethods[] = { { "childOnly", childOnly }, { "label2", childOnly }, { NULL, NULL } };
    cxluaRegisterClass(L, &PropBase_clsinfo, &fieldHooks, baseMethods);
    cxluaRegisterClass(L, &PropChild_clsinfo, NULL, childMethods);

    PropBase* base   = propbaseCreate();
    PropChild* child = propchildCreate();
    cxluaPushObject(L, objInstBase(base));
    lua_setglobal(L, "base");
    cxluaPushObject(L, objInstBase(child));
    lua_setglobal(L, "child");
    cxluaPushObject(L, objInstBase(base));
    lua_getglobal(L, "base");
    bool same = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);

    bool ran = run(L, "base.num = 7\n"
                      "base.label = 'seven'\n"
                      "base.ratio = 2\n"
                      "assert(base.num == 7 and base.label == 'seven' and base.ratio == 2.0)\n"
                      "assert(base:doubled() == 14)\n"
                      "assert(not pcall(function() base.limit = 500 end))\n"
                      "assert(not pcall(function() base.version = 1 end))\n"
                      "base.mine = { 3 }\n"
                      "assert(base.mine[1] == 3 and child.mine == nil)\n"
                      "assert(base.answer == 42)\n"
                      "base.answer = 5\n"
                      "child.num = 4\n"
                      "assert(child.num == 8, 'the override doubles it')\n"
                      "assert(child:doubled() == 16 and child:childOnly() == 'child')\n"
                      "assert(child:label2() == 'child' and base:label2() == 14, 'a subclass method of the same name wins')\n"
                      "assert(base.childOnly == nil)\n"
                      "assert(child.answer == 42, 'the child takes its parent\\'s hooks')\n");
    if (!same || !ran || base->vnum != 7 || !strEq(base->vlabel, _S"seven") || base->vbig != 5)
        TEST_FAIL(1, _SL("same userdata ${int}, ran ${int}; num ${int}, label ${string}, big ${int}"), stvar(int32, same),
                  stvar(int32, ran), stvar(int32, base->vnum), stvar(string, base->vlabel), stvar(int64, base->vbig));

    closeState(L);
    objRelease(&base);
    objRelease(&child);
    return 0;
}

static bool aliveUnlessTagged(ObjInst* obj)
{
    return ((PropBase*)obj)->vtag != 99;
}

static const CxLuaClassHooks weakHooks = { .weak = true, .alive = aliveUnlessTagged };

// A weakly held object doesn't stay alive for Lua, and using it once it's gone, or once its alive
// hook says so, is an error rather than a crash.
static int test_cxlua_weak(void)
{
    lua_State* L = newState();
    cxluaRegisterClass(L, &PropBase_clsinfo, &weakHooks, NULL);
    PropBase* a = propbaseCreate();
    PropBase* b = propbaseCreate();
    Weak(PropBase)* wa = objGetWeak(PropBase, a);
    cxluaPushObject(L, objInstBase(a));
    lua_setglobal(L, "a");
    cxluaPushObject(L, objInstBase(b));
    lua_setglobal(L, "b");

    bool used = run(L, "a.num = 3");
    objRelease(&a);
    PropBase* still = objAcquireFromWeak(PropBase, wa);
    bool freed = still == NULL;
    objRelease(&still);
    propbaseSetTag(b, 99);
    bool errs = run(L, "local ok1, e1 = pcall(function() return a.num end)\n"
                       "local ok2, e2 = pcall(function() b.num = 1 end)\n"
                       "assert(not ok1 and e1:find('destroyed'), tostring(e1))\n"
                       "assert(not ok2 and e2:find('destroyed'), tostring(e2))\n"
                       "a = nil b = nil collectgarbage()");
    if (!used || !freed || !errs)
        TEST_FAIL(1, _SL("used ${int}, freed while Lua held it ${int}, errors after ${int}"), stvar(int32, used),
                  stvar(int32, freed), stvar(int32, errs));

    objDestroyWeak(&wa);
    closeState(L);
    objRelease(&b);
    return 0;
}

static int destroyOnThread(Thread* self)
{
    closure* cls = stvlNextPtr(&self->args);
    closureDestroy(cls);
    return 0;
}

// A Lua function as a closure: called with arguments, its result returned; destroyed on another
// thread, the state lets the function go the next time cxlua runs; after the state closes,
// calling it does nothing.
static int test_cxlua_closures(void)
{
    lua_State* L = newState();
    run(L, "watch = setmetatable({}, { __mode = 'k' })\n"
           "function make() local f = function(a, b) return a + b == 5 end watch[f] = true return f end");
    lua_getglobal(L, "make");
    cxluaPCall(L, 0, 1);
    closure cls = cxluaToClosure(L, -1);
    lua_pop(L, 1);

    bool yes = closureCall(cls, stvar(int32, 2), stvar(int32, 3));
    bool no  = closureCall(cls, stvar(int32, 2), stvar(int32, 4));
    Thread* t = thrCreate(destroyOnThread, _S"Destroy", stvar(ptr, &cls));
    thrWait(t, timeS(5));
    thrRelease(&t);
    bool released = run(L, "collectgarbage() collectgarbage() assert(next(watch) == nil, 'still referenced')");

    lua_getglobal(L, "make");
    cxluaPCall(L, 0, 1);
    closure late = cxluaToClosure(L, -1);
    lua_pop(L, 1);
    closeState(L);
    bool afterClose = closureCall(late, stvar(int32, 2), stvar(int32, 3));
    closureDestroy(&late);

    if (!yes || no || !released || afterClose)
        TEST_FAIL(1, _SL("true result ${int}, false result ${int}, released ${int}, called after close ${int}"),
                  stvar(int32, yes), stvar(int32, no), stvar(int32, released), stvar(int32, afterClose));
    return 0;
}

static string caught;

static void catchError(lua_State* L, strref message)
{
    unused_noeval(L);
    strDup(&caught, message);
}

// An error in a call goes to the handler with a traceback and leaves the stack as it was; an
// erroring closure returns false.
static int test_cxlua_errors(void)
{
    lua_State* L = newState();
    cxluaSetErrorHandler(L, catchError);
    int top  = lua_gettop(L);
    bool ran = run(L, "local function inner() error('broken') end inner()");
    bool tb  = strFind(caught, 0, _S"broken") >= 0 && strFind(caught, 0, _S"stack traceback") >= 0;
    strClear(&caught);
    luaL_loadstring(L, "return function() error('in a closure') end");
    cxluaPCall(L, 0, 1);
    closure cls  = cxluaToClosure(L, -1);
    lua_pop(L, 1);
    bool called  = closureCall(cls, stvNone);
    bool clsErr  = strFind(caught, 0, _S"in a closure") >= 0;
    closureDestroy(&cls);
    int after = lua_gettop(L);
    closeState(L);
    strDestroy(&caught);
    if (ran || !tb || called || !clsErr || after != top)
        TEST_FAIL(1, _SL("ran ${int}, traceback ${int}, closure returned ${int}, its error seen ${int}, stack ${int} -> ${int}"),
                  stvar(int32, ran), stvar(int32, tb), stvar(int32, called), stvar(int32, clsErr), stvar(int32, top),
                  stvar(int32, after));
    return 0;
}

// Values both ways: numbers, booleans, strings with a zero byte, objects, and nil.
static int test_cxlua_values(void)
{
    lua_State* L   = newState();
    PropBase* obj  = propbaseCreate();
    string zeroed  = 0;
    strFromBytes(&zeroed, "a\0b", 3);
    stvar in[]     = { stvar(int32, -5), stvar(uint64, 9), stvar(float64, 0.5), stvar(bool, true), stvar(string, zeroed),
                       stvar(object, obj), stvNone };
    int32 n        = sizeof(in) / sizeof(in[0]);
    for (int32 i = 0; i < n; i++)
        cxluaPushStvar(L, in[i]);
    bool ok = lua_tointeger(L, 1) == -5 && lua_tointeger(L, 2) == 9 && lua_tonumber(L, 3) == 0.5 && lua_toboolean(L, 4) &&
              lua_rawlen(L, 5) == 3 && lua_isuserdata(L, 6) && lua_isnil(L, 7);

    stvar out[7];
    bool read = true;
    for (int32 i = 0; i < n; i++)
        read = read && cxluaToStvar(L, i + 1, &out[i]);
    bool back = read && out[0].data.st_int64 == -5 && out[2].data.st_float64 == 0.5 && out[3].data.st_bool &&
                strEq(out[4].data.st_string, zeroed) && out[5].data.st_object == objInstBase(obj) &&
                stvarTypeId(&out[6]) == stTypeId(none);
    for (int32 i = 0; read && i < n; i++)
        stvarDestroy(&out[i]);
    lua_newtable(L);
    stvar table;
    bool refused = !cxluaToStvar(L, -1, &table);

    closeState(L);
    objRelease(&obj);
    strDestroy(&zeroed);
    if (!ok || !back || !refused)
        TEST_FAIL(1, _SL("pushed ${int}, read back ${int}, a table refused ${int}"), stvar(int32, ok), stvar(int32, back),
                  stvar(int32, refused));
    return 0;
}

static int test_cxlua_grp_bridge(void)
{
    TEST_CHAIN(test_cxlua_objects, test_cxlua_weak, test_cxlua_closures, test_cxlua_errors, test_cxlua_values);
}

testfunc cxluatest_funcs[] = {
    { "objects", test_cxlua_objects },
    { "weak", test_cxlua_weak },
    { "closures", test_cxlua_closures },
    { "errors", test_cxlua_errors },
    { "values", test_cxlua_values },
    { "grp_bridge", test_cxlua_grp_bridge },
    { 0, 0 }
};
