#pragma once

/// @file objprop.h
/// @brief Run-time access to class properties by name

#include <cx/obj/objclass.h>
#include <cx/stype/stvar.h>

/// @defgroup obj_prop Properties
/// @ingroup obj
/// @{
///
/// A property pairs a class's typed getter and setter methods with a name, so that code which
/// only knows the name at run time (a script binding, an inspector, a debug dump) can read and
/// write it. Properties are declared in the `.cxh` file with the `[prop]` annotation:
///
/// @code
///   class Slider {
///       [prop value] float64 value();
///       [prop value] void setValue(float64 v);
///       [prop dragging ro] bool dragging();
///       [prop orient enum] int32 orient();
///       [prop orient enum] void setOrient(int32 o);
///       factory create();
///   }
/// @endcode
///
/// - `[prop NAME]` marks a getter (no parameters, returns the value) or a setter (one parameter
///   of the same type, returns `void` or `bool`). Every property needs both, unless the getter is
///   marked `ro` (read-only). A setter that returns `bool` can refuse a value by returning false.
/// - `enum` marks an `int32` property that holds an enum value. It goes on both methods.
/// - Allowed types are `bool`, the integer types, `size_t`, `float32`, `float64`, `string` or
///   `strref`, and pointers to a class. Methods from an interface the class implements, or from a
///   mixin it uses, can be properties too.
/// - Properties are inherited. A class can't declare a property its parent already has; override
///   the parent's methods instead.
///
/// Reading and writing go through the same methods C code calls, so overrides are honored:
///
/// @code
///   stvar v;
///   if (objPropGet(slider, _SL("value"), &v)) {
///       // v holds a float64
///       stvarDestroy(&v);
///   }
///   objPropSet(slider, _SL("value"), stvar(int32, 1));   // converted to float64
/// @endcode
///
/// Values are converted to the property's type the same way stConvert() converts them, with range
/// checks. The exceptions: a `bool` property accepts only `bool` and integer values (nonzero is
/// true), and an object property accepts only an instance of its class (or NULL, if the setter's
/// parameter is `[opt]`).
///
/// Properties add no locking: they are as thread-safe as the methods they call.

CX_C_BEGIN

/// Property descriptor flags
enum ObjPropFlagsEnum {
    OBJPROP_ReadOnly = 0x0001,   ///< No setter
    OBJPROP_Enum     = 0x0002,   ///< An int32 that holds an enum value
    OBJPROP_Iface    = 0x0004,   ///< Declared by an interface the class implements
};

// Getter: initializes *out with an owned copy of the value. Returns false if it refused.
typedef bool (*ObjPropGetFn)(_In_ ObjInst* self, _Out_ stvar* out);

// Setter: val already has the descriptor's type. Returns false if it refused the value.
typedef bool (*ObjPropSetFn)(_Inout_ ObjInst* self, stvar val);

/// Describes one property of a class
///
/// cxautogen generates these; find them with objPropFind() or objPropEnum().
typedef struct ObjPropDesc {
    strref name;        ///< Property name
    stype type;         ///< Type of the value; `object` for a class pointer
    ObjPropGetFn get;   ///< Reads the value
    ObjPropSetFn set;   ///< Writes the value; NULL for a read-only property
    uint32 flags;       ///< Combination of ObjPropFlagsEnum values
} ObjPropDesc;

/// Finds a property by name on a class or any of its parents
///
/// @param cls Class to search, usually objClsInfo(obj)
/// @param name Property name (case-sensitive)
/// @return The descriptor, or NULL if the class has no such property
///
/// Example:
/// @code
///   const ObjPropDesc *pd = objPropFind(objClsInfo(slider), _SL("value"));
/// @endcode
_Ret_maybenull_ const ObjPropDesc* objPropFind(_In_ ObjClassInfo* cls, _In_opt_ strref name);

/// Callback for objPropEnum(); return false to stop
typedef bool (*ObjPropEnumFn)(_In_ const ObjPropDesc* pd, _In_opt_ void* ctx);

/// Calls a function for every property of a class, including inherited ones
///
/// The class's own properties come first, each in name order, then its parent's, and so on.
///
/// @param cls Class to list
/// @param fn Called once per property; returning false stops the listing
/// @param ctx Passed to fn
/// @return false if fn stopped the listing early
///
/// Example:
/// @code
///   static bool printProp(const ObjPropDesc *pd, void *ctx)
///   {
///       logFmt(Info, _S"${string}", stvar(strref, pd->name));
///       return true;
///   }
///
///   objPropEnum(objClsInfo(obj), printProp, NULL);
/// @endcode
bool objPropEnum(_In_ ObjClassInfo* cls, _In_ ObjPropEnumFn fn, _In_opt_ void* ctx);

_Success_(return) bool _objPropGetD(_In_ ObjInst* obj, _In_ const ObjPropDesc* pd, _Out_ stvar* out);
_Success_(return) bool _objPropSetD(_Inout_ ObjInst* obj, _In_ const ObjPropDesc* pd, stvar val);
_Success_(return) bool _objPropGet(_In_ ObjInst* obj, _In_opt_ strref name, _Out_ stvar* out);
_Success_(return) bool _objPropSet(_Inout_ ObjInst* obj, _In_opt_ strref name, stvar val);

/// bool objPropGet(obj, strref name, stvar *out)
///
/// Reads a property by name
///
/// @param obj Object to read from
/// @param name Property name
/// @param out Receives the value, which the caller must destroy with stvarDestroy(). It is
/// overwritten without being destroyed first. Set to stvNone on failure.
/// @return true on success, false if the property doesn't exist or the getter refused
///
/// Example:
/// @code
///   stvar v;
///   if (objPropGet(label, _SL("text"), &v)) {
///       strref text = v.data.st_string;
///       stvarDestroy(&v);
///   }
/// @endcode
#define objPropGet(obj, name, out) _objPropGet(objInstBase(obj), name, out)

/// bool objPropSet(obj, strref name, stvar val)
///
/// Writes a property by name, converting the value to the property's type
///
/// @param obj Object to write to
/// @param name Property name
/// @param val New value; it is not consumed
/// @return true on success, false if the property doesn't exist, is read-only, the value can't
/// be converted, or the setter refused it
///
/// Example:
/// @code
///   objPropSet(label, _SL("text"), stvar(strref, _SL("Hello")));
/// @endcode
#define objPropSet(obj, name, val) _objPropSet(objInstBase(obj), name, val)

/// bool objPropGetD(obj, const ObjPropDesc *pd, stvar *out)
///
/// Reads a property through a descriptor from objPropFind()
///
/// Same as objPropGet() without the name lookup, for callers that cache descriptors. pd must
/// belong to the object's class or one of its parents.
///
/// @param obj Object to read from
/// @param pd Property descriptor
/// @param out Receives the value; see objPropGet()
/// @return true on success, false if the getter refused
#define objPropGetD(obj, pd, out) _objPropGetD(objInstBase(obj), pd, out)

/// bool objPropSetD(obj, const ObjPropDesc *pd, stvar val)
///
/// Writes a property through a descriptor from objPropFind()
///
/// Same as objPropSet() without the name lookup, for callers that cache descriptors. pd must
/// belong to the object's class or one of its parents.
///
/// @param obj Object to write to
/// @param pd Property descriptor
/// @param val New value; it is not consumed
/// @return true on success, false if the property is read-only, the value can't be converted, or
/// the setter refused it
#define objPropSetD(obj, pd, val) _objPropSetD(objInstBase(obj), pd, val)

CX_C_END

/// @}  // end of obj_prop group
