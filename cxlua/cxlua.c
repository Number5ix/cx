#include <cxlua.h>

#include <cx/container.h>
#include <cx/log.h>
#include <cx/string.h>
#include <cx/thread.h>

// The state's registry holds, under keys that are addresses here:
// - ctxKey: the CxLuaCtx, as a light userdata
// - objectsKey: a weak-valued table from each object (light userdata) to its userdata
// - classesKey: a table from each registered class (light userdata) to { hooks, methods }
// - metasKey: a table from each class pushed so far to its metatable
static char ctxKey, objectsKey, classesKey, metasKey;

// cxlua's state for one lua_State. Closures and kept values hold references to it, so it outlives
// the state when they do, with L cleared.
typedef struct CxLuaCtx {
    atomic(int32) refs;
    lua_State* L;
    // Registry references let go of off the state's thread, freed the next time cxlua runs on it;
    // pending counts them, so the lock is only taken when there are some
    Mutex lock;
    sa_int32 unrefs;
    atomic(int32) pending;
    // Objects held for the Lua function that is running
    sa_ptr holds;
    CxLuaErrorFn onError;
} CxLuaCtx;

// An object's userdata
typedef struct LuaObj {
    ObjInst* strong;
    Weak(ObjInst)* weak;
    const CxLuaClassHooks* hooks;
} LuaObj;

struct CxLuaRef {
    CxLuaCtx* ctx;
    int ref;
};

static void ctxRelease(CxLuaCtx* ctx)
{
    if (atomicFetchSub(int32, &ctx->refs, 1, AcqRel) != 1)
        return;
    mutexDestroy(&ctx->lock);
    saDestroy(&ctx->unrefs);
    saDestroy(&ctx->holds);
    xaFree(ctx);
}

static CxLuaCtx* ctxOf(lua_State* L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &ctxKey);
    CxLuaCtx* ctx = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!ctx)
        luaL_error(L, "cxlua isn't open on this state");
    return ctx;
}

// Frees the references let go of elsewhere
static void drainUnrefs(CxLuaCtx* ctx)
{
    if (atomicLoad(int32, &ctx->pending, Acquire) == 0)
        return;
    mutexAcquire(&ctx->lock);
    for (int32 i = 0; i < saSize(ctx->unrefs); i++)
        luaL_unref(ctx->L, LUA_REGISTRYINDEX, ctx->unrefs.a[i]);
    saClear(&ctx->unrefs);
    atomicStore(int32, &ctx->pending, 0, Release);
    mutexRelease(&ctx->lock);
}

static void releaseHolds(CxLuaCtx* ctx, int32 mark)
{
    while (saSize(ctx->holds) > mark) {
        ObjInst* obj = ctx->holds.a[saSize(ctx->holds) - 1];
        saRemove(&ctx->holds, saSize(ctx->holds) - 1);
        objRelease(&obj);
    }
}

// ---- opening and closing ----

_Use_decl_annotations_
void cxluaOpen(lua_State* L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &ctxKey);
    bool open = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (open)
        return;

    CxLuaCtx* ctx = xaAllocStruct(CxLuaCtx, XA_Zero);
    atomicStore(int32, &ctx->refs, 1, Release);
    ctx->L = L;
    mutexInit(&ctx->lock);
    saInit(&ctx->unrefs, int32, 8);
    saInit(&ctx->holds, ptr, 8);
    lua_pushlightuserdata(L, ctx);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &ctxKey);

    lua_newtable(L);
    lua_newtable(L);
    lua_pushliteral(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &objectsKey);
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &classesKey);
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &metasKey);
}

_Use_decl_annotations_
void cxluaClose(lua_State* L)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &ctxKey);
    CxLuaCtx* ctx = lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!ctx)
        return;
    drainUnrefs(ctx);
    releaseHolds(ctx, 0);
    mutexAcquire(&ctx->lock);
    ctx->L = NULL;
    mutexRelease(&ctx->lock);
    lua_pushnil(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &ctxKey);
    ctxRelease(ctx);
}

_Use_decl_annotations_
void cxluaSetErrorHandler(lua_State* L, CxLuaErrorFn fn)
{
    ctxOf(L)->onError = fn;
}

// ---- calls ----

static int traceback(lua_State* L)
{
    const char* msg = lua_tostring(L, 1);
    if (!msg)
        msg = luaL_tolstring(L, 1, NULL);
    luaL_traceback(L, L, msg, 1);
    return 1;
}

_Use_decl_annotations_
bool cxluaPCall(lua_State* L, int nargs, int nresults)
{
    CxLuaCtx* ctx = ctxOf(L);
    drainUnrefs(ctx);
    int32 mark = saSize(ctx->holds);
    int base   = lua_gettop(L) - nargs;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    int status = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    releaseHolds(ctx, mark);
    if (status == LUA_OK)
        return true;

    size_t len      = 0;
    const char* msg = lua_tolstring(L, -1, &len);
    string text     = 0;
    strFromBytes(&text, msg, msg ? (uint32)len : 0);
    if (ctx->onError)
        ctx->onError(L, text);
    else
        logFmt(Error, _SL("Lua error: ${string}"), stvar(string, text));
    strDestroy(&text);
    lua_pop(L, 1);
    return false;
}

// Runs a method with the objects it takes held until it returns, even by an error.
static int methodTrampoline(lua_State* L)
{
    CxLuaCtx* ctx = ctxOf(L);
    drainUnrefs(ctx);
    int32 mark    = saSize(ctx->holds);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    int status = lua_pcall(L, lua_gettop(L) - 1, LUA_MULTRET, 0);
    releaseHolds(ctx, mark);
    // What the method let go of, such as a destroyed widget's handlers
    drainUnrefs(ctx);
    if (status != LUA_OK)
        return lua_error(L);
    return lua_gettop(L);
}

// ---- kept values and closures ----

_Use_decl_annotations_
CxLuaRef* cxluaRefCreate(lua_State* L, int idx)
{
    CxLuaCtx* ctx = ctxOf(L);
    drainUnrefs(ctx);
    CxLuaRef* ref = xaAllocStruct(CxLuaRef);
    lua_pushvalue(L, idx);
    ref->ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ref->ctx = ctx;
    atomicFetchAdd(int32, &ctx->refs, 1, Relaxed);
    return ref;
}

_Use_decl_annotations_
lua_State* cxluaRefPush(CxLuaRef* ref)
{
    lua_State* L = ref->ctx->L;
    if (!L)
        return NULL;
    drainUnrefs(ref->ctx);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref->ref);
    return L;
}

_Use_decl_annotations_
void cxluaRefDestroy(CxLuaRef** pref)
{
    CxLuaRef* ref = *pref;
    if (!ref)
        return;
    *pref         = NULL;
    CxLuaCtx* ctx = ref->ctx;
    mutexAcquire(&ctx->lock);
    if (ctx->L) {
        saPush(&ctx->unrefs, int32, ref->ref);
        atomicFetchAdd(int32, &ctx->pending, 1, Release);
    }
    mutexRelease(&ctx->lock);
    ctxRelease(ctx);
    xaFree(ref);
}

static bool closureCalled(stvlist* cvars, stvlist* args)
{
    CxLuaRef* ref = stvlAtPtr(cvars, 0);
    lua_State* L  = cxluaRefPush(ref);
    if (!L) {
        logFmt(Debug, _SL("a closure made from a Lua function was called after its state closed"), stvNone);
        return false;
    }
    for (int32 i = 0; i < args->count; i++)
        cxluaPushStvar(L, args->vars[i]);
    if (!cxluaPCall(L, args->count, 1))
        return false;
    bool r = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return r;
}

static void closureGone(stvlist* cvars)
{
    CxLuaRef* ref = stvlAtPtr(cvars, 0);
    cxluaRefDestroy(&ref);
}

_Use_decl_annotations_
closure cxluaToClosure(lua_State* L, int idx)
{
    luaL_checktype(L, idx, LUA_TFUNCTION);
    closure cls = closureCreate(closureCalled, stvar(ptr, cxluaRefCreate(L, idx)));
    closureSetDestroy(cls, closureGone);
    return cls;
}

// ---- objects ----

// The hooks registered for cls or its nearest ancestor that has some
static const CxLuaClassHooks* hooksFor(lua_State* L, ObjClassInfo* cls)
{
    const CxLuaClassHooks* hooks = NULL;
    lua_rawgetp(L, LUA_REGISTRYINDEX, &classesKey);
    for (; cls && !hooks; cls = cls->parent) {
        if (lua_rawgetp(L, -1, cls) == LUA_TTABLE) {
            lua_getfield(L, -1, "hooks");
            hooks = lua_touserdata(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return hooks;
}

_Use_decl_annotations_
void cxluaRegisterClass(lua_State* L, ObjClassInfo* cls, const CxLuaClassHooks* hooks, const luaL_Reg* methods)
{
    ctxOf(L);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &classesKey);
    lua_newtable(L);
    if (hooks) {
        lua_pushlightuserdata(L, (void*)hooks);
        lua_setfield(L, -2, "hooks");
    }
    lua_newtable(L);
    for (; methods && methods->name; methods++) {
        lua_pushcfunction(L, methods->func);
        lua_pushcclosure(L, methodTrampoline, 1);
        lua_setfield(L, -2, methods->name);
    }
    lua_setfield(L, -2, "methods");
    lua_rawsetp(L, -2, cls);
    lua_pop(L, 1);
    // Metatables made so far may miss what this adds; objects already in Lua keep theirs.
    lua_newtable(L);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &metasKey);
}

// The object a userdata stands for, held until the running function returns; NULL if it's gone
static ObjInst* resolve(lua_State* L, LuaObj* lo)
{
    if (lo->strong)
        return lo->hooks && lo->hooks->alive && !lo->hooks->alive(lo->strong) ? NULL : lo->strong;
    ObjInst* obj = objAcquireFromWeak(ObjInst, lo->weak);
    if (!obj)
        return NULL;
    saPush(&ctxOf(L)->holds, ptr, obj);
    return lo->hooks && lo->hooks->alive && !lo->hooks->alive(obj) ? NULL : obj;
}

static LuaObj* toLuaObj(lua_State* L, int idx)
{
    if (lua_type(L, idx) != LUA_TUSERDATA || !lua_getmetatable(L, idx))
        return NULL;
    lua_getfield(L, -1, "__cxlua");
    bool ours = lua_toboolean(L, -1);
    lua_pop(L, 2);
    return ours ? lua_touserdata(L, idx) : NULL;
}

static ObjInst* checkSelf(lua_State* L, int idx)
{
    LuaObj* lo   = toLuaObj(L, idx);
    ObjInst* obj = lo ? resolve(L, lo) : NULL;
    if (!obj)
        luaL_error(L, lo ? "attempt to use a destroyed object" : "attempt to use something that isn't a cx object");
    return obj;
}

_Use_decl_annotations_
ObjInst* cxluaTestObject(lua_State* L, int idx, ObjClassInfo* cls)
{
    LuaObj* lo   = toLuaObj(L, idx);
    ObjInst* obj = lo ? resolve(L, lo) : NULL;
    return obj && _objDynCast(obj, cls) ? obj : NULL;
}

_Use_decl_annotations_
ObjInst* cxluaCheckObject(lua_State* L, int idx, ObjClassInfo* cls)
{
    ObjInst* obj = checkSelf(L, idx);
    if (!_objDynCast(obj, cls))
        luaL_error(L, "argument %d is the wrong kind of object", idx);
    return obj;
}

// A property by its own name, or spelled all in lowercase
typedef struct PropSearch {
    const char* key;
    const ObjPropDesc* found;
} PropSearch;

static bool propLower(const ObjPropDesc* pd, void* ctx)
{
    PropSearch* ps = ctx;
    if (strEqi(pd->name, (strref)ps->key)) {
        ps->found = pd;
        return false;
    }
    return true;
}

static const ObjPropDesc* findProp(ObjInst* obj, const char* key)
{
    const ObjPropDesc* pd = objPropFind(objClsInfo(obj), (strref)key);
    if (pd)
        return pd;
    for (const char* c = key; *c; c++) {
        if (*c >= 'A' && *c <= 'Z')
            return NULL;
    }
    PropSearch ps = { key, NULL };
    objPropEnum(objClsInfo(obj), propLower, &ps);
    return ps.found;
}

static int objIndex(lua_State* L)
{
    drainUnrefs(ctxOf(L));
    ObjInst* obj    = checkSelf(L, 1);
    LuaObj* lo      = lua_touserdata(L, 1);
    const char* key = lua_tostring(L, 2);
    if (lua_type(L, 2) == LUA_TSTRING) {
        lua_getmetatable(L, 1);
        lua_getfield(L, -1, "__methods");
        if (lua_getfield(L, -1, key) != LUA_TNIL)
            return 1;
        lua_pop(L, 3);

        const ObjPropDesc* pd = findProp(obj, key);
        if (pd) {
            stvar v = { 0 };
            if (!objPropGetD(obj, pd, &v))
                return luaL_error(L, "can't read property '%s'", key);
            cxluaPushStvar(L, v);
            stvarDestroy(&v);
            return 1;
        }
        if (lo->hooks && lo->hooks->getField && lo->hooks->getField(L, obj, key))
            return 1;
    }
    if (lua_getiuservalue(L, 1, 1) != LUA_TTABLE)
        return 1;   // nil
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}

static int objNewIndex(lua_State* L)
{
    drainUnrefs(ctxOf(L));
    ObjInst* obj    = checkSelf(L, 1);
    LuaObj* lo      = lua_touserdata(L, 1);
    const char* key = lua_tostring(L, 2);
    if (lua_type(L, 2) == LUA_TSTRING) {
        const ObjPropDesc* pd = findProp(obj, key);
        if (pd) {
            stvar v = { 0 };
            // nil is no object, for a property that takes one
            if (lua_isnil(L, 3) && stEq(pd->type, stType(object)))
                v = stvar(object, (ObjInst*)NULL);
            else if (!cxluaToStvar(L, 3, &v))
                return luaL_error(L, "property '%s' can't take a %s", key, luaL_typename(L, 3));
            bool ok = objPropSetD(obj, pd, v);
            stvarDestroy(&v);
            if (!ok)
                return luaL_error(L, "property '%s' refused the value", key);
            return 0;
        }
        if (lo->hooks && lo->hooks->setField && lo->hooks->setField(L, obj, key, 3))
            return 0;
    }
    if (lua_getiuservalue(L, 1, 1) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setiuservalue(L, 1, 1);
    }
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);
    return 0;
}

static int objGc(lua_State* L)
{
    LuaObj* lo = lua_touserdata(L, 1);
    objRelease(&lo->strong);
    objDestroyWeak(&lo->weak);
    return 0;
}

// Userdata are one per object, but one whose object was freed may still be waiting for the
// collector while a new object reuses the address.
static int objEq(lua_State* L)
{
    LuaObj* a = toLuaObj(L, 1);
    LuaObj* b = toLuaObj(L, 2);
    return lua_pushboolean(L, a && b && a == b), 1;
}

static int objToString(lua_State* L)
{
    LuaObj* lo   = lua_touserdata(L, 1);
    ObjInst* obj = resolve(L, lo);
    lua_pushfstring(L, obj ? "cx object: %p" : "destroyed cx object", (void*)obj);
    return 1;
}

// The metatable for a class: methods merged from its registered ancestors, nearest first
static void pushMeta(lua_State* L, ObjClassInfo* cls)
{
    lua_rawgetp(L, LUA_REGISTRYINDEX, &metasKey);
    if (lua_rawgetp(L, -1, cls) == LUA_TTABLE) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushboolean(L, 1);
    lua_setfield(L, -2, "__cxlua");
    lua_pushcfunction(L, objIndex);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, objNewIndex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, objGc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, objEq);
    lua_setfield(L, -2, "__eq");
    lua_pushcfunction(L, objToString);
    lua_setfield(L, -2, "__tostring");

    lua_newtable(L);
    int merged = lua_gettop(L);
    lua_rawgetp(L, LUA_REGISTRYINDEX, &classesKey);
    int classes = lua_gettop(L);
    for (ObjClassInfo* c = cls; c; c = c->parent) {
        if (lua_rawgetp(L, classes, c) != LUA_TTABLE) {
            lua_pop(L, 1);
            continue;
        }
        lua_getfield(L, -1, "methods");
        int methods = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, methods)) {
            // A nearer class's method of the same name stays.
            lua_pushvalue(L, -2);
            if (lua_rawget(L, merged) == LUA_TNIL) {
                lua_pushvalue(L, -3);
                lua_pushvalue(L, -3);
                lua_rawset(L, merged);
            }
            lua_pop(L, 2);
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
    lua_setfield(L, -2, "__methods");

    lua_pushvalue(L, -1);
    lua_rawsetp(L, -3, cls);
    lua_remove(L, -2);
}

_Use_decl_annotations_
void cxluaPushObject(lua_State* L, ObjInst* obj)
{
    if (!obj) {
        lua_pushnil(L);
        return;
    }
    lua_rawgetp(L, LUA_REGISTRYINDEX, &objectsKey);
    if (lua_rawgetp(L, -1, obj) == LUA_TUSERDATA) {
        LuaObj* lo   = lua_touserdata(L, -1);
        ObjInst* was = lo->strong ? lo->strong : objAcquireFromWeak(ObjInst, lo->weak);
        bool same    = was == obj;
        if (!lo->strong)
            objRelease(&was);
        if (same) {
            lua_remove(L, -2);
            return;
        }
    }
    lua_pop(L, 1);

    const CxLuaClassHooks* hooks = hooksFor(L, objClsInfo(obj));
    LuaObj* lo                   = lua_newuserdatauv(L, sizeof(LuaObj), 1);
    *lo                          = (LuaObj){ .hooks = hooks };
    if (hooks && hooks->weak)
        lo->weak = objGetWeak(ObjInst, obj);
    else
        lo->strong = objAcquire(obj);
    pushMeta(L, objClsInfo(obj));
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    lua_rawsetp(L, -3, obj);
    lua_remove(L, -2);
}

// ---- values ----

_Use_decl_annotations_
void cxluaPushStvar(lua_State* L, stvar v)
{
    if (!v._type) {
        lua_pushnil(L);
        return;
    }
    switch (stvarTypeId(&v)) {
    case stTypeId(bool):    lua_pushboolean(L, v.data.st_bool); break;
    case stTypeId(int8):    lua_pushinteger(L, v.data.st_int8); break;
    case stTypeId(int16):   lua_pushinteger(L, v.data.st_int16); break;
    case stTypeId(int32):   lua_pushinteger(L, v.data.st_int32); break;
    case stTypeId(int64):   lua_pushinteger(L, v.data.st_int64); break;
    case stTypeId(uint8):   lua_pushinteger(L, v.data.st_uint8); break;
    case stTypeId(uint16):  lua_pushinteger(L, v.data.st_uint16); break;
    case stTypeId(uint32):  lua_pushinteger(L, v.data.st_uint32); break;
    case stTypeId(uint64):  lua_pushinteger(L, (lua_Integer)v.data.st_uint64); break;
    case stTypeId(float32): lua_pushnumber(L, v.data.st_float32); break;
    case stTypeId(float64): lua_pushnumber(L, v.data.st_float64); break;
    case stTypeId(string):
        lua_pushlstring(L, strC(v.data.st_string), strLen(v.data.st_string));
        break;
    case stTypeId(object):  cxluaPushObject(L, v.data.st_object); break;
    case stTypeId(ptr):     lua_pushlightuserdata(L, v.data.st_ptr); break;
    case stTypeId(stvar):   cxluaPushStvar(L, *v.data.st_stvar); break;
    default:                lua_pushnil(L); break;
    }
}

_Use_decl_annotations_
bool cxluaToStvar(lua_State* L, int idx, stvar* out)
{
    *out = (stvar){ 0 };
    switch (lua_type(L, idx)) {
    case LUA_TNIL:
    case LUA_TNONE:
        *out = stvNone;
        return true;
    case LUA_TBOOLEAN:
        *out = stvar(bool, lua_toboolean(L, idx));
        return true;
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx))
            *out = stvar(int64, lua_tointeger(L, idx));
        else
            *out = stvar(float64, lua_tonumber(L, idx));
        return true;
    case LUA_TSTRING: {
        size_t len    = 0;
        const char* s = lua_tolstring(L, idx, &len);
        string copy   = 0;
        strFromBytes(&copy, s, (uint32)len);
        stvarCopy(out, stvar(string, copy));
        strDestroy(&copy);
        return true;
    }
    case LUA_TLIGHTUSERDATA:
        *out = stvar(ptr, lua_touserdata(L, idx));
        return true;
    case LUA_TUSERDATA: {
        LuaObj* lo   = toLuaObj(L, idx);
        ObjInst* obj = lo ? resolve(L, lo) : NULL;
        if (!obj)
            return false;
        stvarCopy(out, stvar(object, obj));
        return true;
    }
    }
    return false;
}
