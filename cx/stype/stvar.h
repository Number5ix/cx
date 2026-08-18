#pragma once

/// @file stvar.h
/// @brief Variant type containers and type-safe variadic argument support

#include <cx/cx.h>
#include <cx/stype/stype.h>

CX_C_BEGIN

/// @defgroup stvar Variant Types
/// @ingroup stype
/// @{
///
/// Type-tagged variant containers providing runtime polymorphism and type-safe
/// variadic function arguments without exposing raw `va_list` semantics.
///
/// @section stvar_concept Concept
///
/// The `stvar` structure combines an `stype` descriptor with a typed value container
/// (`stgeneric`), enabling a single variable to hold values of different types while
/// preserving type information at runtime. This is the foundation for:
///
/// - **Type-safe variadic functions**: Replace C's unsafe `va_list` with arrays of `stvar`
/// - **Heterogeneous containers**: Store mixed types in a single array or collection
/// - **Dynamic dispatch**: Route operations based on runtime type information
/// - **Generic algorithms**: Process values without compile-time type knowledge
///
/// @section stvar_lifetime Lifetime and Scope
///
/// Variants created with `stvar()` use C99 compound literals to create stack-allocated
/// temporaries. These temporaries have automatic storage duration limited to the
/// enclosing block scope:
///
/// @code
///   void processVariants(int count, stvar *vars);
///
///   void example() {
///       // Temporary variants valid until end of function
///       processVariants(3, (stvar[]){
///           stvar(int32, 42),
///           stvar(string, _SL("hello")),
///           stvar(float64, 3.14)
///       });
///       // Temporaries destroyed here
///   }
/// @endcode
///
/// For persistent variants, use explicit allocation or embed in structures. Duplicate a
/// temporary into a persistent variant with `stvarCopy()` (which assumes an uninitialized
/// destination), or replace the contents of an already-initialized variant with
/// `stvarSet()` (which destroys any existing value first). Both deep-copy oversized
/// (`suid`, `opaque`, `struct`) values into storage the variant owns, so the persistent
/// copy stays valid after the temporary goes out of scope:
///
/// @code
///   stvar persistent;
///   stvarCopy(&persistent, stvar(int32, 42));   // destination was uninitialized
///   // ... use persistent ...
///   stvarSet(&persistent, string, _SL("now a string"));   // replaces the int32
///   // ... use persistent ...
///   stvarDestroy(&persistent);
/// @endcode
///
/// @section stvar_usage Usage Patterns
///
/// **Creating variants:**
/// @code
///   stvar v1 = stvar(int32, 42);
///   stvar v2 = stvar(string, _SL("text"));
///   stvar empty = stvNone;
/// @endcode
///
/// **Type checking and access:**
/// @code
///   if (stvarIs(&v1, int32)) {
///       int32 val = v1.data.st_int32;
///   }
///
///   string s = stvarString(&v2);  // Returns NULL if not a string
/// @endcode
///
/// **Variadic function pattern:**
/// @code
///   // Internal function taking count and variant array
///   void _myFunc(int count, stvar *args) {
///       stvlist list;
///       stvlInit(&list, count, args);
///
///       int32 num;
///       string str;
///       if (stvlNext(&list, int32, &num) && stvlNext(&list, string, &str)) {
///           // Process typed arguments
///       }
///   }
///
///   // Macro wrapper to automatically fill count and create array
///   #define myFunc(...) _myFunc(count_macro_args(__VA_ARGS__), (stvar[]){__VA_ARGS__})
///
///   // Usage - caller just passes typed arguments directly
///   myFunc(stvar(int32, 10), stvar(string, _SL("data")));
/// @endcode
///
/// @}

/// @defgroup stvar_create Variant Creation
/// @ingroup stvar
/// @{
///
/// Macros for creating and initializing variant containers.

/// stvar stvarInit(type, value)
///
/// Static initializer for variant structures (C only).
///
/// Creates a compile-time initializer suitable for static/automatic variable
/// initialization. This is primarily used when declaring persistent variant
/// variables, not for temporary expressions.
///
/// @param typen Type name (e.g., int32, string)
/// @param val Value of the specified type
/// @return Initializer expression for stvar structure
///
/// Example:
/// @code
///   stvar persistent = stvarInit(int32, 42);
///   stvar array[] = {
///       stvarInit(string, _SL("first")),
///       stvarInit(int32, 100)
///   };
/// @endcode
#ifndef __cplusplus
#define stvarInit(typen, val) { .data = { .st_##typen = val }, ._type = stType(typen) }

/// stvar stvar(type, value)
///
/// Create a temporary variant containing a typed value.
///
/// Uses C99 compound literals to create a stack-allocated temporary with
/// automatic storage duration. The temporary is valid until the end of the
/// enclosing block scope. This is the primary mechanism for passing typed
/// arguments to variadic functions.
///
/// **IMPORTANT**: The variant's lifetime is limited to the current function scope.
/// Do not return these from functions or store pointers to them beyond the
/// current scope.
///
/// @param typen Type name (e.g., int32, string, object)
/// @param val Value of the specified type
/// @return Temporary stvar with automatic storage duration
///
/// Example:
/// @code
///   processValue(stvar(int32, 42));
///
///   myFunc(3, (stvar[]){
///       stvar(string, _SL("name")),
///       stvar(int32, 100),
///       stvar(float64, 3.14)
///   });
/// @endcode
#define stvar(typen, val) ((stvar) { .data = stArg(typen, val), ._type = stType(typen) })

/// stvar stvNone
///
/// Empty variant constant representing no value.
///
/// Used to represent the absence of a value or as a sentinel/terminator in
/// variant arrays. The type field is set to `stType(none)`.
///
/// Example:
/// @code
///   stvar result = stvNone;
///   if (conditionMet) {
///       result = stvar(int32, 42);
///   }
/// @endcode
#define stvNone ((stvar) { ._type = stType(none) })

/// stvar stvark(key, type, value)
///
/// Create a temporary variant tagged with a key name.
///
/// Identical to `stvar()` except that the variant also carries a name, letting the
/// receiver locate it with `stvlFind()` by key rather than by type and position. Keyed
/// and unkeyed arguments mix freely in the same call.
///
/// The key is written as a bare token and stringized, so it costs nothing at runtime on
/// any compiler and **cannot be handed a pointer that dangles**. Any comma-free token
/// sequence works, including dotted names (`stvark(http.status, int32, code)`).
///
/// The name is metadata: preserved by copy, ignored by compare and hash.
///
/// @param key Key name as a bare token (not a string literal)
/// @param typen Type name (e.g., int32, string, object)
/// @param val Value of the specified type
/// @return Temporary stvar with automatic storage duration
///
/// Example:
/// @code
///   strFormat(&s, _SL("${string:host} took ${int:ms}ms"),
///             stvark(host, string, hostname), stvark(ms, int32, elapsed));
/// @endcode
#define stvark(key, typen, val) \
    ((stvar) { .data = stArg(typen, val), ._type = stType(typen), ._key = #key })

/// stvar stvarkn(name, type, value)
///
/// Create a temporary keyed variant from a runtime name pointer.
///
/// The escape hatch for argument lists built at runtime rather than at a call site --
/// deserialization, script bindings, forwarded log records. Prefer `stvark()` everywhere
/// else: it stringizes a token and so enforces the lifetime rule structurally.
///
/// **The name is pointer-copied, never duplicated.** It must remain valid for as long as
/// any copy of the variant does, which in practice means program lifetime. Passing a
/// stack buffer, or a heap buffer that is later freed, leaves a dangling pointer that
/// will not surface until something formats or serializes the variant.
///
/// @param name `const char*` with program lifetime (or NULL)
/// @param typen Type name (e.g., int32, string, object)
/// @param val Value of the specified type
/// @return Temporary stvar with automatic storage duration
///
/// Example:
/// @code
///   // field names interned for the life of the process
///   stvar v = stvarkn(internedName, string, value);
/// @endcode
#define stvarkn(name, typen, val) \
    ((stvar) { .data = stArg(typen, val), ._type = stType(typen), ._key = (name) })
#else
_meta_inline stvar _stvar(stype st, stgeneric val)
{
    stvar ret;
    ret.data  = val;
    ret._type = st;
    ret._key  = NULL;
    return ret;
}
#define stvar(typen, val) _stvar(stType(typen), stArg(typen, val))

_meta_inline stvar _stvark(const char* nm, stype st, stgeneric val)
{
    stvar ret = _stvar(st, val);
    ret._key  = nm;
    return ret;
}
#define stvark(key, typen, val)   _stvark(#key, stType(typen), stArg(typen, val))
#define stvarkn(name, typen, val) _stvark((name), stType(typen), stArg(typen, val))

#define stvNone _stvar(stType(none), _cxStGenZero())
#endif

/// @}

/// @defgroup stvar_lifecycle Variant Lifecycle
/// @ingroup stvar
/// @{
///
/// Functions for managing variant lifetime and copying.

// Core lifecycle helpers. These manage storage in a standardized manner so that oversized (PassPtr)
// values - suid, opaque, struct — are deep-copied into a heap allocation the variant owns, rather
// than left as dangling pointers to a caller's temporary.

// Initialize *stv (assumed uninitialized) from a type + value. Overwrite/init semantics:
// does NOT destroy any prior contents. For PassPtr types, allocates and deep-copies.
// Does not touch the key name.
void _stvarInit(stvar* stv, stype type, stgeneric val);

// As _stvarInit, but also sets the key name (pointer-copied, never duplicated).
void _stvarInitK(stvar* stv, stype type, stgeneric val, const char* vk);

// Destroy the contents of *stv, free any owned heap allocation, clear any key name, and
// reset to none.
void _stvarClear(stvar* stv, flags_t flags);

// Replace semantics: destroy existing contents, then initialize from type + value.
// Clears any existing key name.
void _stvarSet(stvar* stv, stype type, stgeneric val);

// Replace semantics preserving/replacing the key name.
void _stvarSetK(stvar* stv, stype type, stgeneric val, const char* vk);

// Prepare *stv -- which may hold a live value -- to receive a value of `type` written
// directly into the returned storage. For PassPtr types the variant allocates and owns the
// block; otherwise the storage is the variant's own inline `data`. Either way the variant is
// left holding a zero-filled value of that type, so _stvarClear can always undo it.
_Ret_notnull_ void* _stvarPrepare(stvar* stv, stype type);

/// void stvarSet(stvar *stv, type, value)
///
/// Replace the contents of a variant with a new typed value.
///
/// Unlike `stvarCopy` (which assumes an uninitialized destination), `stvarSet`
/// destroys any existing value first, then stores the new one. The variant must
/// already be initialized (e.g. `stvNone` or a prior value). Oversized values
/// (suid, opaque, struct) are deep-copied into storage the variant owns.
///
/// @param stv Pointer to variant to modify (must be initialized)
/// @param type Type name (e.g. int32, string, suid)
/// @param val Value of the specified type
///
/// Example:
/// @code
///   stvar v = stvNone;
///   stvarSet(&v, int32, 42);
///   stvarSet(&v, string, _SL("hello"));   // previous int32 replaced safely
///   stvarDestroy(&v);
/// @endcode
#define stvarSet(stv, type, val) _stvarSet(stv, stCheckedArg(type, val))

/// void stvarSetK(stvar *stv, key, type, value)
///
/// Replace the contents of a variant with a new typed value and attach a key name.
///
/// As `stvarSet`, but also tags the variant with a key, written as a bare token and
/// stringized exactly as in `stvark()`. Plain `stvarSet` *clears* any existing key,
/// on the grounds that a stale key on a new value is worse than no key at all -- so
/// use this form when replacing the value of a keyed variant.
///
/// @param stv Pointer to variant to modify (must be initialized)
/// @param key Key name as a bare token (not a string literal)
/// @param type Type name (e.g. int32, string, suid)
/// @param val Value of the specified type
///
/// Example:
/// @code
///   stvar v = stvNone;
///   stvarSetK(&v, host, string, _SL("web01"));
///   stvarDestroy(&v);
/// @endcode
#define stvarSetK(stv, key, type, val) _stvarSetK(stv, stCheckedArg(type, val), #key)

/// void stvarDestroy(stvar *stv)
///
/// Destroy a variant and release its resources.
///
/// Invokes the type-appropriate destructor on the contained value (e.g.,
/// decrements reference counts for objects, frees strings) and resets the
/// type to `none`. After destruction, the variant is in a valid but empty
/// state and can be safely destroyed again or reassigned.
///
/// @param stv Pointer to variant to destroy
///
/// Example:
/// @code
///   stvar v;
///   stvarCopy(&v, stvar(string, _SL("hello")));
///   // ... use v ...
///   stvarDestroy(&v);  // Releases string reference
/// @endcode
_meta_inline void stvarDestroy(stvar* stv)
{
    _stvarClear(stv, 0);
}

/// void stvarCopy(stvar *dest, stvar source)
///
/// Copy a variant to another variant.
///
/// Copies the type descriptor, the value, and any key name, performing appropriate
/// operations for the contained type (incrementing reference counts for objects,
/// duplicating strings, etc.). The destination variant should be uninitialized or
/// previously destroyed to avoid leaking resources.
///
/// The key name is pointer-copied, not duplicated -- which is why keys are required to
/// have program lifetime. A copied variant may outlive the call that created it.
///
/// @param dvar Pointer to destination variant (overwritten)
/// @param svar Source variant to copy (passed by value)
///
/// Example:
/// @code
///   stvar original = stvar(string, _SL("text"));
///   stvar copy;
///   stvarCopy(&copy, original);
///   // Both variants now reference the string (refcount incremented)
///   stvarDestroy(&copy);
/// @endcode
_meta_inline void stvarCopy(stvar* dvar, stvar svar)
{
    _stvarInitK(dvar, stvarType(&svar), svar.data, svar._key);
}

/// @}

/// @defgroup stvar_access Variant Type Checking and Access
/// @ingroup stvar
/// @{
///
/// Functions and macros for checking variant types and extracting values.

/// bool stvarIs(stvar *svar, type)
///
/// Check if a variant contains a value of the specified type.
///
/// Compares the variant's runtime type descriptor against the specified
/// compile-time type. Returns false for NULL pointers.
///
/// @param svar Pointer to variant to check
/// @param type Type name to check for (e.g., int32, string)
/// @return true if variant contains the specified type, false otherwise
///
/// Example:
/// @code
///   stvar v = stvar(int32, 42);
///   if (stvarIs(&v, int32)) {
///       int32 val = v.data.st_int32;
///   }
/// @endcode
#define stvarIs(svar, type) _stvarIs(svar, stType(type))
_meta_inline bool _stvarIs(stvar* svar, stype styp)
{
    return svar && stEq(stvarType(svar), styp);
}

/// string stvarString(stvar *svar)
///
/// Extract string value from variant if it contains a string.
///
/// Convenience accessor that checks the type and returns the string value
/// in one operation. Returns NULL if the variant does not contain a string
/// or if the pointer is NULL.
///
/// @param svar Pointer to variant
/// @return String value if variant contains string, NULL otherwise
///
/// Example:
/// @code
///   stvar v = stvar(string, _SL("hello"));
///   string s = stvarString(&v);
///   if (s) {
///       // Use string
///   }
/// @endcode
_meta_inline string stvarString(stvar* svar)
{
    if (stvarIs(svar, string))
        return svar->data.st_string;
    return NULL;
}

/// string* stvarStringPtr(stvar *svar)
///
/// Get pointer to string value within variant.
///
/// Returns a pointer to the string field inside the variant's data union,
/// allowing modification of the stored string. Returns NULL if the variant
/// does not contain a string.
///
/// @param svar Pointer to variant
/// @return Pointer to string field, or NULL if not a string variant
///
/// Example:
/// @code
///   stvar v = stvar(string, _SL("hello"));
///   string *ps = stvarStringPtr(&v);
///   if (ps) {
///       strAppend(ps, _SL(" world"));
///   }
/// @endcode
_meta_inline string* stvarStringPtr(stvar* svar)
{
    if (stvarIs(svar, string))
        return &svar->data.st_string;
    return NULL;
}

/// ObjInst* stvarObjInst(stvar *svar)
///
/// Extract object instance from variant if it contains an object.
///
/// Returns the untyped object pointer if the variant contains an object.
/// For typed access, use `stvarObj()` macro instead.
///
/// @param svar Pointer to variant
/// @return Object instance pointer if variant contains object, NULL otherwise
///
/// Example:
/// @code
///   stvar v = stvar(object, myObj);
///   ObjInst *obj = stvarObjInst(&v);
///   if (obj) {
///       // Use untyped object
///   }
/// @endcode
_meta_inline ObjInst* stvarObjInst(stvar* svar)
{
    if (stvarIs(svar, object))
        return svar->data.st_object;
    return NULL;
}

/// ObjInst** stvarObjInstPtr(stvar *svar)
///
/// Get pointer to object instance field within variant.
///
/// Returns a pointer to the object field inside the variant's data union.
/// Returns NULL if the variant does not contain an object.
///
/// @param svar Pointer to variant
/// @return Pointer to object field, or NULL if not an object variant
_meta_inline ObjInst** stvarObjInstPtr(stvar* svar)
{
    if (stvarIs(svar, object))
        return &svar->data.st_object;
    return NULL;
}

/// ClassName* stvarObj(ClassName, stvar *svar)
///
/// Extract typed object from variant with runtime type checking.
///
/// Retrieves the object from the variant and performs a dynamic cast to the
/// specified class type. Returns NULL if the variant does not contain an
/// object or if the object is not compatible with the target class.
///
/// @param class Target class name (e.g., MyClass)
/// @param svar Pointer to variant
/// @return Typed object pointer, or NULL if not compatible
///
/// Example:
/// @code
///   stvar v = stvar(object, myTestObj);
///   TestClass *tc = stvarObj(TestClass, &v);
///   if (tc) {
///       // Use typed object
///   }
/// @endcode
#define stvarObj(class, svar) (objDynCast(class, stvarObjInst(svar)))

/// @}

/// @defgroup stvar_list Variant List Walking
/// @ingroup stvar
/// @{
///
/// Iterator pattern for processing arrays of variants with type-safe extraction.
///
/// The variant list walker provides a cursor-based interface for sequentially
/// extracting typed values from an array of variants, commonly used for
/// implementing type-safe variadic functions.
///
/// Example usage pattern:
/// @code
///   // Internal implementation function
///   void _myFunc(int count, stvar *args) {
///       stvlist list;
///       stvlInit(&list, count, args);
///
///       int32 id;
///       string name;
///       MyClass *obj;
///
///       // Extract arguments in order by type
///       if (stvlNext(&list, int32, &id) &&
///           stvlNext(&list, string, &name) &&
///           (obj = stvlNextObj(&list, MyClass))) {
///           // Process typed arguments
///       }
///   }
///
///   // Macro wrapper for convenient calling
///   #define myFunc(...) _myFunc(count_macro_args(__VA_ARGS__), (stvar[]){__VA_ARGS__})
///
///   // Usage
///   myFunc(stvar(int32, 123), stvar(string, _SL("test")), stvar(object, myObj));
/// @endcode

/// Variant list walker structure.
///
/// Maintains a cursor position for iterating through an array of variants.
/// Initialized with `stvlInit()` or `stvlInitSA()`, then accessed with the
/// various `stvlNext*()` functions.
typedef struct stvlist {
    int count;     ///< Total number of variants in array
    int cursor;    ///< Current position (next variant to examine)
    stvar* vars;   ///< Pointer to variant array
} stvlist;

/// void stvlInit(stvlist *list, int count, stvar *vars)
///
/// Initialize variant list walker from array and count.
///
/// Sets up the list structure to iterate over a raw array of variants,
/// typically from a variadic function's argument list. Resets the cursor
/// to the beginning.
///
/// @param list Pointer to list structure to initialize
/// @param count Number of variants in array
/// @param vars Pointer to variant array
///
/// Example:
/// @code
///   void processVars(int count, stvar *args) {
///       stvlist list;
///       stvlInit(&list, count, args);
///       // Use stvlNext() to walk the list
///   }
/// @endcode
void stvlInit(stvlist* list, int count, stvar* vars);

/// void stvlInitSA(stvlist *list, sa_stvar vararray)
///
/// Initialize variant list walker from an sarray of variants.
///
/// Sets up the list structure to iterate over a dynamic array (sarray) of
/// variants. The count is extracted automatically from the array metadata.
///
/// @param list Pointer to list structure to initialize
/// @param vararray Dynamic array of variants (sa_stvar or similar)
///
/// Example:
/// @code
///   sa_stvar args;
///   saInit(&args, stvar, 8);
///   saPush(&args, stvar, stvar(int32, 42));
///   saPush(&args, stvar, stvar(string, _SL("test")));
///
///   stvlist list;
///   stvlInitSA(&list, args);
///   // Walk the list
///   saDestroy(&args);
/// @endcode
#define stvlInitSA(list, vararray) _stvlInitSA(list, (vararray).a)
void _stvlInitSA(stvlist* list, stvar* vara);

/// bool stvlNext(stvlist *list, type, type *pvar)
///
/// Extract next variant of specified type from list.
///
/// Searches forward from the current cursor position for the next variant
/// matching the specified type. If found, copies the value to the output
/// parameter, advances the cursor past that variant, and returns true.
/// If no matching variant is found, returns false and leaves the cursor
/// unchanged.
///
/// This allows flexible argument ordering in variadic functions where
/// arguments can be provided in any order.
///
/// @param list Pointer to list walker
/// @param type Type name to search for (e.g., int32, string)
/// @param pvar Pointer to variable to receive the value
/// @return true if matching variant found and extracted, false otherwise
///
/// Example:
/// @code
///   stvlist list;
///   stvlInit(&list, count, args);
///
///   int32 num;
///   string str;
///   if (stvlNext(&list, int32, &num)) {
///       // Found int32, num now contains value
///   }
///   if (stvlNext(&list, string, &str)) {
///       // Found string, str now contains value
///   }
/// @endcode
#define stvlNext(list, type, pvar) _stvlNext(list, stCheckedPtrArg(type, pvar))
bool _stvlNext(stvlist* list, stype type, stgeneric* out);

/// void* stvlNextPtr(stvlist *list)
///
/// Extract next pointer-type variant from list.
///
/// Searches for the next variant containing a generic pointer (`ptr` type),
/// advances the cursor, and returns the pointer value. Returns NULL if no
/// pointer variant is found.
///
/// @param list Pointer to list walker
/// @return Pointer value, or NULL if not found
///
/// Example:
/// @code
///   void *data = stvlNextPtr(&list);
///   if (data) {
///       // Use generic pointer
///   }
/// @endcode
#define stvlNextPtr(list) _stvlNextPtr(list, stType(ptr))
void* _stvlNextPtr(stvlist* list, stype type);

/// ClassName* stvlNextObj(stvlist *list, ClassName)
///
/// Extract next object variant from list with runtime type checking.
///
/// Searches for the next variant containing an object, performs a dynamic
/// cast to the specified class type, advances the cursor, and returns the
/// typed object pointer. Returns NULL if no compatible object is found.
///
/// @param list Pointer to list walker
/// @param class Target class name for dynamic cast
/// @return Typed object pointer, or NULL if not found or incompatible
///
/// Example:
/// @code
///   TestClass *obj = stvlNextObj(&list, TestClass);
///   if (obj) {
///       // Use typed object
///   }
/// @endcode
#define stvlNextObj(list, class) objDynCast(class, (ObjInst*)_stvlNextPtr(list, stType(object)))

/// void stvlRewind(stvlist *list)
///
/// Reset list walker cursor to beginning.
///
/// Resets the cursor to position 0, allowing the same variant array to be
/// walked multiple times or re-scanned for different argument combinations.
///
/// @param list Pointer to list walker to rewind
///
/// Example:
/// @code
///   stvlist list;
///   stvlInit(&list, count, args);
///
///   // First pass: extract required args
///   stvlNext(&list, int32, &required);
///
///   // Second pass: scan for optional args
///   stvlRewind(&list);
///   stvlNext(&list, string, &optional);
/// @endcode
void stvlRewind(stvlist* list);

/// @}

/// @defgroup stvar_list_keyed Keyed Variant Lookup
/// @ingroup stvar_list
/// @{
///
/// Lookup by key name rather than by type and position.
///
/// @section stvar_keyed_contract How this differs from stvlNext
///
/// `stvlNext()` scans **forward from the cursor** for the next variant of a type, advances
/// past the match, and discards everything it skipped. That is the right contract for
/// positional arguments, which arrive in a known order.
///
/// Keys exist precisely so that order does not matter, so `stvlFind()` does the opposite:
/// it scans the **whole list from the start** and **mutates nothing**. That is why it takes
/// the `stvlist` **by value** rather than by pointer -- per the handle paradigm, passing by
/// value at the call site is the visible signal that the walker's cursor is untouched.
///
/// The two address the same argument list without interfering, in either order:
///
/// @code
///   void _myFunc(int count, stvar *args)
///   {
///       stvlist list;
///       stvlInit(&list, count, args);
///
///       int32 timeout = 5000;                          // optional, keyed
///       stvlFind(list, timeout, int32, &timeout);
///
///       string required;                               // required, positional
///       if (stvlNext(&list, string, &required)) { ... }
///   }
///
///   myFunc(stvar(string, _SL("path")), stvark(timeout, int32, 250));
/// @endcode
///
/// **The two modes are disjoint: `stvlNext()` skips keyed variants entirely.** A keyed
/// argument is reachable only through `stvlFind()`. This is deliberate -- if positional
/// walking could consume keyed arguments, adding one to an existing call would silently
/// shift every same-typed positional argument after it, which is exactly the fragility
/// keys exist to remove. It also means a caller can add a keyed argument to a call
/// without renumbering or reordering anything.
///
/// @section stvar_keyed_dupes Duplicate keys
///
/// Duplicate keys in one argument list resolve to the **first** match. This cannot be
/// caught at compile time; debug builds assert on it, release builds take the first. Do
/// not rely on the behaviour.

/// bool stvlFind(stvlist list, key, type, type *pvar)
///
/// Find a variant by key name and type, without disturbing the walker.
///
/// Scans the entire list from the beginning for a variant whose key matches `key` and
/// whose type matches `type`, and copies its value to `pvar`. The cursor is not moved and
/// the list is not modified -- the walker is taken by value.
///
/// The key is written as a bare token and stringized, matching `stvark()`.
///
/// @param list Variant list walker (by value; not modified)
/// @param key Key name as a bare token (not a string literal)
/// @param type Expected type name
/// @param pvar Pointer to storage receiving the value
/// @return true if a matching keyed variant was found
///
/// Example:
/// @code
///   int32 ms;
///   if (stvlFind(list, timeout, int32, &ms)) { ... }
/// @endcode
#define stvlFind(list, key, type, pvar) _stvlFind(list, #key, stCheckedPtrArg(type, pvar))
bool _stvlFind(stvlist list, const char* key, stype type, stgeneric* out);

/// void* stvlFindPtr(stvlist list, key)
///
/// Find a pointer-typed variant by key name, without disturbing the walker.
///
/// As `stvlFind()` for pointer-like types (`ptr`, objects, and PassPtr types). Returns
/// the stored pointer directly.
///
/// @param list Variant list walker (by value; not modified)
/// @param key Key name as a bare token (not a string literal)
/// @return Stored pointer, or NULL if no matching keyed variant exists
///
/// Example:
/// @code
///   void *ctx = stvlFindPtr(list, context);
/// @endcode
#define stvlFindPtr(list, key) _stvlFindPtr(list, #key, stType(ptr))
void* _stvlFindPtr(stvlist list, const char* key, stype type);

/// ClassName* stvlFindObj(stvlist list, key, ClassName)
///
/// Find an object variant by key name and dynamic-cast it, without disturbing the walker.
///
/// As `stvlFindPtr()`, but restricted to object variants and passed through `objDynCast`,
/// so the result is NULL unless the object is compatible with the named class.
///
/// @param list Variant list walker (by value; not modified)
/// @param key Key name as a bare token (not a string literal)
/// @param class Target class name for dynamic cast
/// @return Typed object pointer, or NULL if not found or incompatible
///
/// Example:
/// @code
///   Document *doc = stvlFindObj(list, source, Document);
/// @endcode
#define stvlFindObj(list, key, class) \
    objDynCast(class, (ObjInst*)_stvlFindPtr(list, #key, stType(object)))

/// bool stvlHasKey(stvlist list, key)
///
/// Test whether a keyed variant exists, regardless of its type.
///
/// @param list Variant list walker (by value; not modified)
/// @param key Key name as a bare token (not a string literal)
/// @return true if any variant in the list carries that key
///
/// Example:
/// @code
///   if (stvlHasKey(list, verbose)) { ... }
/// @endcode
#define stvlHasKey(list, key) _stvlHasKey(list, #key)
bool _stvlHasKey(stvlist list, const char* key);

/// @}

CX_C_END
