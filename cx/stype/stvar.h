#pragma once

/// @file stvar.h
/// @brief Variant type containers and type-safe variadic argument support

#include <cx/cx.h>
#include <cx/stype/stype.h>

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
///           stvar(string, _S"hello"),
///           stvar(float64, 3.14)
///       });
///       // Temporaries destroyed here
///   }
/// @endcode
///
/// For persistent variants, use explicit allocation or embed in structures:
///
/// @code
///   stvar persistent;
///   stvarCopy(&persistent, stvar(int32, 42));
///   // ... use persistent ...
///   stvarDestroy(&persistent);
/// @endcode
///
/// @section stvar_usage Usage Patterns
///
/// **Creating variants:**
/// @code
///   stvar v1 = stvar(int32, 42);
///   stvar v2 = stvar(string, _S"text");
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
///   myFunc(stvar(int32, 10), stvar(string, _S"data"));
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
///       stvarInit(string, _S"first"),
///       stvarInit(int32, 100)
///   };
/// @endcode
#ifndef __cplusplus
#define stvarInit(typen, val) { .data = { .st_##typen = val }, .type = stType(typen) }

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
///       stvar(string, _S"name"),
///       stvar(int32, 100),
///       stvar(float64, 3.14)
///   });
/// @endcode
#define stvar(typen, val) ((stvar) { .data = stArg(typen, val), .type = stType(typen) })

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
#define stvNone ((stvar) { .type = stType(none) })
#else
_meta_inline stvar _stvar(stype st, stgeneric val)
{
    stvar ret;
    ret.data = val;
    ret.type = st;
    return ret;
}
#define stvar(typen, val) _stvar(stType(typen), stArg(typen, val))

#define stvNone _stvar(0, stArg(int64, 0))
#endif

/// @}

/// @defgroup stvar_lifecycle Variant Lifecycle
/// @ingroup stvar
/// @{
///
/// Functions for managing variant lifetime and copying.

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
///   stvarCopy(&v, stvar(string, _S"hello"));
///   // ... use v ...
///   stvarDestroy(&v);  // Releases string reference
/// @endcode
_meta_inline void stvarDestroy(stvar* stv)
{
    _stDestroy(stv->type, NULL, &stv->data, 0);
    stv->type = 0;
}

/// void stvarCopy(stvar *dest, stvar source)
///
/// Deep copy a variant to another variant.
///
/// Copies both the type descriptor and value, performing appropriate operations
/// for the contained type (incrementing reference counts for objects, duplicating
/// strings, etc.). The destination variant should be uninitialized or previously
/// destroyed to avoid leaking resources.
///
/// @param dvar Pointer to destination variant (overwritten)
/// @param svar Source variant to copy (passed by value)
///
/// Example:
/// @code
///   stvar original = stvar(string, _S"text");
///   stvar copy;
///   stvarCopy(&copy, original);
///   // Both variants now reference the string (refcount incremented)
///   stvarDestroy(&copy);
/// @endcode
_meta_inline void stvarCopy(stvar* dvar, stvar svar)
{
    dvar->type = svar.type;
    _stCopy(svar.type, NULL, &dvar->data, svar.data, 0);
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
    return svar && stEq(svar->type, styp);
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
///   stvar v = stvar(string, _S"hello");
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
///   stvar v = stvar(string, _S"hello");
///   string *ps = stvarStringPtr(&v);
///   if (ps) {
///       strAppend(ps, _S" world");
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
///   myFunc(stvar(int32, 123), stvar(string, _S"test"), stvar(object, myObj));
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
///   saPush(&args, stvar, stvar(string, _S"test"));
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
