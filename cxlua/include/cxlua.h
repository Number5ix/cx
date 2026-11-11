#pragma once
/// @file cxlua.h
/// @brief Using cx objects and values from Lua

#include <cx/closure.h>
#include <cx/obj.h>
#include <cx/stype/stvar.h>
#include <lauxlib.h>
#include <lua.h>

CX_C_BEGIN

/// @defgroup cxlua Lua bridge
/// @{
/// cxlua lets Lua use cx objects: their properties as fields, methods a module registers for
/// their class, and Lua functions as cx closures. It adds itself to a lua_State the program
/// already has, and never creates one of its own.
///
/// @section cxlua_objects Objects
///
/// Each object has one userdata per state, so the same object compares equal to itself in Lua. A
/// field that is neither a property nor a method is stored on the userdata, for Lua's own use.
/// A class registered as weak (cxluaRegisterClass()) is held weakly: Lua doesn't keep it alive,
/// and using it after it is gone, or after its alive hook says so, raises a Lua error. Other
/// objects are held with a reference, which Lua's garbage collector releases.
///
/// Property names are the class's own, such as `readOnly`; their all-lowercase spelling works
/// too. Methods come first when a name is both. Setting a property that holds an object to nil
/// gives it none.
///
/// @section cxlua_threads Threads
///
/// A lua_State is used by one thread at a time, and cxlua adds no locking. Only releasing a value
/// kept for C (cxluaRefDestroy(), or destroying a closure made by cxluaToClosure()) may happen on
/// any thread; the state lets go of it the next time cxlua runs on its own thread.
///
/// @code
///   lua_State* L = luaL_newstate();
///   luaL_openlibs(L);
///   cxluaOpen(L);
///   cxluaPushObject(L, objInstBase(doc));
///   lua_setglobal(L, "doc");
///   if (luaL_loadstring(L, "doc.title = 'Notes'") == LUA_OK)
///       cxluaPCall(L, 0, 0);
///   cxluaClose(L);
///   lua_close(L);
/// @endcode

/// What cxlua needs to know about a class beyond its properties
typedef struct CxLuaClassHooks {
    /// Held weakly, for objects something else owns
    bool weak;
    /// Whether the object can still be used; NULL for always
    bool (*alive)(_In_ ObjInst* obj);
    /// Reads a field that is neither a property nor a method: pushes its value and returns true,
    /// or returns false if the class has no such field. May be NULL.
    bool (*getField)(_Inout_ lua_State* L, _In_ ObjInst* obj, _In_z_ const char* key);
    /// Sets such a field from the value at index val: returns true if the class has the field. A
    /// value it can't take raises a Lua error. May be NULL.
    bool (*setField)(_Inout_ lua_State* L, _In_ ObjInst* obj, _In_z_ const char* key, int val);
} CxLuaClassHooks;

/// Adds cxlua to a Lua state
///
/// Calling it again on the same state does nothing.
///
/// @param L The state
void cxluaOpen(_Inout_ lua_State* L);

/// Takes cxlua out of a Lua state that is about to close
///
/// Values kept for C stop working, and closures made from Lua functions do nothing from then on.
///
/// @param L The state
void cxluaClose(_Inout_ lua_State* L);

/// Describes a class to Lua
///
/// The hooks apply to the class and every class that extends it, unless one of those registers
/// hooks of its own. Methods are inherited, a subclass's replacing a parent's of the same name.
/// Register a class before its objects first reach Lua.
///
/// @param L The state
/// @param cls The class, such as `&CxUiWidget_clsinfo`
/// @param hooks How objects of the class are held and their extra fields; NULL for a strong hold
/// and no extra fields. Kept, so it must outlive the state.
/// @param methods Lua functions to call as methods (`obj:name(...)`), ending with a NULL entry;
/// may be NULL. The object is argument 1.
void cxluaRegisterClass(_Inout_ lua_State* L, _In_ ObjClassInfo* cls, _In_opt_ const CxLuaClassHooks* hooks,
                        _In_opt_ const luaL_Reg* methods);

/// Pushes an object's userdata
/// @param L The state
/// @param obj The object, or NULL to push nil
void cxluaPushObject(_Inout_ lua_State* L, _In_opt_ ObjInst* obj);

/// Gets the object at a stack index
///
/// The object stays usable until the Lua function that is running returns.
///
/// @param L The state
/// @param idx Stack index
/// @param cls The class it must be, or extend
/// @return The object; raises a Lua error if it is something else, or is gone
_Ret_valid_ ObjInst* cxluaCheckObject(_Inout_ lua_State* L, int idx, _In_ ObjClassInfo* cls);

/// Gets the object at a stack index, if there is one of that class
/// @param L The state
/// @param idx Stack index
/// @param cls The class it must be, or extend
/// @return The object, usable as for cxluaCheckObject(); NULL if the value is anything else, or
/// the object is gone
_Ret_maybenull_ ObjInst* cxluaTestObject(_Inout_ lua_State* L, int idx, _In_ ObjClassInfo* cls);

/// Pushes a value
///
/// Numbers, booleans and strings become Lua's own, objects their userdata, and empty values nil;
/// a pointer becomes a light userdata. Any other type pushes nil.
///
/// @param L The state
/// @param v The value
void cxluaPushStvar(_Inout_ lua_State* L, stvar v);

/// Reads a value from a stack index
///
/// An integer becomes int64, any other number float64, and nil an empty value.
///
/// @param L The state
/// @param idx Stack index
/// @param out Receives the value, which holds its own copy of a string and its own reference to an
/// object; free it with stvarDestroy()
/// @return false for a value with no cx equivalent, such as a table or function
_Success_(return) bool cxluaToStvar(_Inout_ lua_State* L, int idx, _Out_ stvar* out);

/// A Lua value kept for C
typedef struct CxLuaRef CxLuaRef;

/// Keeps the value at a stack index for later
/// @param L The state
/// @param idx Stack index
/// @return The kept value; release it with cxluaRefDestroy()
_Ret_valid_ CxLuaRef* cxluaRefCreate(_Inout_ lua_State* L, int idx);

/// Pushes a kept value onto its state
///
/// Call it only on the thread using the state.
///
/// @param ref The kept value
/// @return The state it was pushed onto; NULL, with nothing pushed, if the state has closed
_Ret_maybenull_ lua_State* cxluaRefPush(_In_ CxLuaRef* ref);

/// Lets go of a kept value; any thread
/// @param ref The kept value; set to NULL
void cxluaRefDestroy(_Inout_ CxLuaRef** ref);

/// Makes a cx closure from the Lua function at a stack index
///
/// Calling the closure calls the function with the call's arguments, as cxluaPushStvar() pushes
/// them; it returns whether the function returned a true value. An error is handled as by
/// cxluaPCall(), and returns false. The closure may be destroyed on any thread.
///
/// @param L The state
/// @param idx Stack index of the function
/// @return The closure; free it with closureDestroy()
_Ret_valid_ closure cxluaToClosure(_Inout_ lua_State* L, int idx);

/// What cxluaSetErrorHandler() calls with an error's message and traceback
typedef void (*CxLuaErrorFn)(_Inout_ lua_State* L, _In_ strref message);

/// Calls a function as lua_pcall() does, handling an error
///
/// An error's message, with a traceback, goes to the log at Error level, or to the handler set
/// with cxluaSetErrorHandler(), and the results are left off the stack.
///
/// @param L The state
/// @param nargs Arguments above the function on the stack
/// @param nresults Results to keep, or LUA_MULTRET
/// @return false if the function raised an error
bool cxluaPCall(_Inout_ lua_State* L, int nargs, int nresults);

/// Sends errors from cxluaPCall() somewhere other than the log
/// @param L The state
/// @param fn The handler, or NULL for the log
void cxluaSetErrorHandler(_Inout_ lua_State* L, _In_opt_ CxLuaErrorFn fn);

/// @}  // end of cxlua group

CX_C_END
