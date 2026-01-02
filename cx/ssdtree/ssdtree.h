/// @file ssdtree.h
/// @brief Semi-Structured Data (SSD) tree public API

#pragma once

#include <cx/container/sarray.h>
#include <cx/ssdtree/node/ssdnode.h>
#include <cx/string/strbase.h>

/// @defgroup ssd_create Tree Creation
/// @ingroup ssd
/// @{
///
/// Functions for creating new SSD trees with different root node types.

_Ret_valid_ SSDNode* _ssdCreateRoot(int crtype, _In_opt_ SSDTree* tree, uint32 flags);

/// SSDNode *ssdCreateHashtable([flags])
///
/// Creates a new semi-structured tree with a hashtable as its root.
///
/// Hashtable nodes store key-value pairs where keys are strings and values are stvars.
/// This is the most common root type, analogous to a JSON object.
///
/// @param ... (flags) Optional flags (SSD_CaseInsensitive for case-insensitive keys)
/// @return A new SSDNode with a hashtable root (must be released with objRelease)
///
/// Example:
/// @code
///   SSDNode *root = ssdCreateHashtable();
///   ssdSet(root, _S"name", true, stvar(string, _S"value"));
///   objRelease(&root);
/// @endcode
#define ssdCreateHashtable(...) _ssdCreateRoot(SSD_Create_Hashtable, NULL, opt_flags(__VA_ARGS__))

/// SSDNode *ssdCreateArray([flags])
///
/// Creates a new semi-structured tree with an array as its root.
///
/// Array nodes store indexed values accessible by integer indices, analogous to a JSON array.
///
/// @param ... (flags) Optional flags for the tree
/// @return A new SSDNode with an array root (must be released with objRelease)
///
/// Example:
/// @code
///   SSDNode *root = ssdCreateArray();
///   ssdAppend(root, NULL, true, stvar(int32, 42));
///   objRelease(&root);
/// @endcode
#define ssdCreateArray(...) _ssdCreateRoot(SSD_Create_Array, NULL, opt_flags(__VA_ARGS__))

/// SSDNode *ssdCreateSingle([flags])
///
/// Creates a new semi-structured tree with a single value as its root.
///
/// Single-value nodes hold exactly one stvar value. Supported types include integers,
/// floats, strings, booleans, and objects.
///
/// @param ... (flags) Optional flags for the tree
/// @return A new SSDNode with a single-value root (must be released with objRelease)
///
/// Example:
/// @code
///   SSDNode *root = ssdCreateSingle();
///   ssdSet(root, NULL, true, stvar(int32, 42));
///   objRelease(&root);
/// @endcode
#define ssdCreateSingle(...) _ssdCreateRoot(SSD_Create_Single, NULL, opt_flags(__VA_ARGS__))

/// SSDNode *ssdCreateCustom(crtype, tree)
///
/// Creates a tree with a custom node type using factory functions from the provided tree.
///
/// This is an advanced function for creating trees with custom derived node classes.
///
/// @param crtype The SSDCreateType indicating which factory to use
/// @param tree The SSDTree object containing custom factory functions
/// @return A new SSDNode using custom node types
#define ssdCreateCustom(crtype, tree) _ssdCreateRoot(crtype, tree, 0)

/// @}  // end of ssd_create

/// @defgroup ssd_subtree Subtree Operations
/// @ingroup ssd
/// @{
///
/// Functions for cloning trees and accessing/creating subtrees.

_Ret_opt_valid_ SSDNode* _ssdClone(_In_ SSDNode* root, _In_opt_ SSDTree* desttree,
                                   _Inout_opt_ SSDLockState* lstate);

/// SSDNode *ssdClone(SSDNode *root, SSDTree *desttree)
///
/// Creates a deep clone of the entire tree rooted at the given node.
///
/// If desttree is provided, the cloned nodes are associated with that tree, creating an
/// isolated branch that can later be grafted onto the destination tree. If desttree is NULL,
/// a new independent tree is created.
///
/// @param root The node to clone
/// @param desttree Optional destination tree to associate cloned nodes with
/// @return A new SSDNode that is a deep copy of root (must be released with objRelease)
#define ssdClone(root, desttree) _ssdClone(root, desttree, (SSDLockState*)_ssdCurrentLockState)

_Ret_opt_valid_ SSDNode* _ssdSubtree(_In_ SSDNode* root, _In_opt_ strref path, SSDCreateType create,
                                     _Inout_opt_ SSDLockState* lstate);

/// SSDNode *ssdSubtree(SSDNode *root, strref path, SSDCreateType create)
///
/// Returns a node representing a subtree at the given path, optionally creating it.
///
/// This function traverses the path and returns the node at that location. If the node
/// does not exist and create is not SSD_Create_None, a new node of the specified type
/// is created at that path.
///
/// @param root The root node to search from
/// @param path The path to the subtree (e.g., "bucket/paints[2]")
/// @param create Node type to create if path doesn't exist (SSD_Create_None to disable creation)
/// @return The subtree node (caller must release with objRelease), or NULL if not found
///
/// Example:
/// @code
///   SSDNode *subtree = ssdSubtree(root, _S"config/display", SSD_Create_Hashtable);
///   ssdSet(subtree, _S"width", true, stvar(int32, 1920));
///   objRelease(&subtree);
/// @endcode
#define ssdSubtree(root, path, create) \
    _ssdSubtree(root, path, create, (SSDLockState*)_ssdCurrentLockState)

_Ret_opt_valid_ SSDNode* _ssdSubtreeB(_In_ SSDNode* root, _In_opt_ strref path,
                                      _Inout_opt_ SSDLockState* lstate);

/// SSDNode *ssdSubtreeB(SSDNode *root, strref path)
///
/// Borrowed version of ssdSubtree that does not acquire an object reference.
///
/// **IMPORTANT:** This function must only be used within a locked transaction via
/// ssdLockedTransaction(). The returned pointer is only valid while the lock is held.
/// This version cannot create nodes and is intended for read-only access.
///
/// @param root The root node to search from
/// @param path The path to the subtree
/// @return Borrowed pointer to the subtree node (do not release), or NULL if not found
#define ssdSubtreeB(root, path) \
    _ssdSubtreeB(root, path, (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState)

/// @}  // end of ssd_subtree

/// @defgroup ssd_path Path Syntax
/// @ingroup ssd
/// @{
///
/// **Path Syntax for Tree Traversal**
///
/// Many SSD functions accept a path string for navigating the tree structure. Paths consist
/// of names separated by '/' and array indices enclosed in brackets.
///
/// **Examples:**
///
/// Path: `bucket/paints[2]/yellow`
///
/// Represents: A value with key 'yellow' in the third element (index 2) of an array named
/// 'paints', which is inside a hashtable named 'bucket' at the root.
///
/// JSON equivalent:
/// @code
/// {
///     "bucket": {
///         "paints": [
///             {},
///             {},
///             { "yellow": "THIS VALUE HERE" }
///         ]
///     }
/// }
/// @endcode
///
/// Path: `[1]/mango`
///
/// Represents: A value with key 'mango' in the second element of an array at the root.
///
/// JSON equivalent:
/// @code
/// [
///     {},
///     { "mango": "THIS VALUE HERE" }
/// ]
/// @endcode
///
/// **Note:** If automatic path traversal is not desired, the SSDNode interface methods
/// can be used directly without paths.
///
/// @}  // end of ssd_path

/// @defgroup ssd_access Value Access
/// @ingroup ssd
/// @{
///
/// Functions for reading values from the tree.

_Success_(return) bool _ssdGet(_In_ SSDNode* root, _In_opt_ strref path, _Out_ stvar* out,
                        _Inout_opt_ SSDLockState* lstate);

/// bool ssdGet(SSDNode *root, strref path, stvar *out)
///
/// Retrieves a value from the tree as an stvar.
///
/// **IMPORTANT:** The output stvar must be destroyed with stDestroy() to avoid memory leaks
/// when the value is a string, object, or a subtree (hashtable/array). For simpler access
/// without memory management, use ssdPtr() or the convenience functions like ssdVal().
///
/// @param root The root node to search from
/// @param path Path to the value (NULL for root node)
/// @param out Output stvar (caller must destroy with stDestroy)
/// @return true if the value was found, false otherwise
#define ssdGet(root, path, out) _ssdGet(root, path, out, (SSDLockState*)_ssdCurrentLockState)

_Success_(return) _meta_inline bool _ssdGetD(_In_ SSDNode* root, _In_opt_ strref path, _Out_ stvar* out,
                                      stvar def, _Inout_opt_ SSDLockState* lstate)
{
    if (_ssdGet(root, path, out, lstate))
        return true;

    stvarCopy(out, def);
    return false;
}

/// bool ssdGetD(SSDNode *root, strref path, stvar *out, stvar def)
///
/// Variant of ssdGet that provides a default value if the path doesn't exist.
///
/// If the value is not found, the default is copied to out and the function returns false.
///
/// @param root The root node to search from
/// @param path Path to the value
/// @param out Output stvar (caller must destroy with stDestroy)
/// @param def Default value to use if path doesn't exist
/// @return true if value was found, false if default was used
#define ssdGetD(root, path, out, def) \
    _ssdGetD(root, path, out, def, (SSDLockState*)_ssdCurrentLockState)

_Success_(return) bool _ssdCopyOut(_In_ SSDNode* root, _In_opt_ strref path, stype valtype,
                            _stCopyDest_Anno_(valtype) stgeneric* val,
                            _Inout_opt_ SSDLockState* lstate);

/// bool ssdCopyOut(SSDNode *root, strref path, vtype, val_copy_out)
///
/// Retrieves a value from the tree and copies it to an arbitrary destination, with type conversion.
///
/// This function is useful when you need the value in a specific type and want automatic
/// conversion from the stored representation.
///
/// @param root The root node to search from
/// @param path Path to the value
/// @param vtype The expected/desired type for the output
/// @param val_copy_out Pointer to destination variable
/// @return true if value was found and converted successfully
///
/// Example:
/// @code
///   int32 count;
///   if (ssdCopyOut(root, _S"stats/count", int32, &count)) {
///       // use count
///   }
/// @endcode
#define ssdCopyOut(root, path, vtype, val_copy_out)   \
    _ssdCopyOut(root,                                 \
                path,                                 \
                stCheckedPtrArg(vtype, val_copy_out), \
                (SSDLockState*)_ssdCurrentLockState)

_Success_(return) bool _ssdCopyOutD(_In_ SSDNode* root, _In_opt_ strref path, stype valtype,
                             _stCopyDest_Anno_(valtype) stgeneric* val, stgeneric def,
                             _Inout_opt_ SSDLockState* lstate);

/// bool ssdCopyOutD(SSDNode *root, strref path, vtype, val_copy_out, def)
///
/// Variant of ssdCopyOut that provides a default value if conversion fails or path doesn't exist.
///
/// @param root The root node to search from
/// @param path Path to the value
/// @param vtype The expected/desired type for the output
/// @param val_copy_out Pointer to destination variable
/// @param def Default value to use on failure
/// @return true if value was found and converted, false if default was used
#define ssdCopyOutD(root, path, vtype, val_copy_out, def) \
    _ssdCopyOutD(root,                                    \
                 path,                                    \
                 stCheckedPtrArg(vtype, val_copy_out),    \
                 stArg(vtype, def),                       \
                 (SSDLockState*)_ssdCurrentLockState)

/// @cond IGNORE
#define ssdval_spec(type)                                                                \
    _meta_inline stTypeDef(type)                                                         \
        _ssdVal_##type(_In_ SSDNode* root,                                               \
                       _In_opt_ strref path,                                             \
                       stTypeDef(type) def,                                              \
                       _Inout_opt_ SSDLockState* lstate)                                 \
    {                                                                                    \
        stTypeDef(type) out;                                                             \
        _ssdCopyOutD(root, path, stCheckedPtrArg(type, &out), stArg(type, def), lstate); \
        return out;                                                                      \
    }

// clang-format off
ssdval_spec(bool)
ssdval_spec(int8)
ssdval_spec(int16)
ssdval_spec(int32)
ssdval_spec(int64)
ssdval_spec(uint8)
ssdval_spec(uint16)
ssdval_spec(uint32)
ssdval_spec(uint64)
ssdval_spec(float32)
ssdval_spec(float64)
// clang-format on
/// @endcond

/// type ssdVal(type, SSDNode *root, strref path, type def)
///
/// Type-specific convenience function for getting primitive values with a default.
///
/// This function automatically converts and returns the value as the specified type.
/// If the value doesn't exist or can't be converted, the default is returned.
///
/// Supported types: bool, int8, int16, int32, int64, uint8, uint16, uint32, uint64,
/// float32, float64
///
/// @param type The desired return type
/// @param root The root node
/// @param path Path to the value
/// @param def Default value if not found or conversion fails
/// @return The value or default
///
/// Example:
/// @code
///   int32 port = ssdVal(int32, root, _S"server/port", 8080);
///   bool enabled = ssdVal(bool, root, _S"server/enabled", false);
/// @endcode
#define ssdVal(type, root, path, def) \
    _ssdVal_##type(root, path, def, (SSDLockState*)_ssdCurrentLockState)

            _meta_inline bool _ssdStringOut(_In_ SSDNode* root, _In_opt_ strref path,
                                            _Inout_ string* out, _Inout_opt_ SSDLockState* lstate)
{
    strDestroy(out);
    return _ssdCopyOut(root, path, stCheckedPtrArg(string, out), lstate);
}

/// bool ssdStringOut(SSDNode *root, strref path, string *out)
///
/// Retrieves a string value, copying it into the provided string variable.
///
/// The output string is automatically destroyed before being populated with the new value.
///
/// @param root The root node
/// @param path Path to the string value
/// @param out Pointer to string variable (will be destroyed and replaced)
/// @return true if the value was found and copied, false otherwise
#define ssdStringOut(root, path, out) \
    _ssdStringOut(root, path, out, (SSDLockState*)_ssdCurrentLockState)

_meta_inline bool _ssdStringOutD(_In_ SSDNode* root, _In_opt_ strref path, _Inout_ string* out,
                                 _In_opt_ strref def, _Inout_opt_ SSDLockState* lstate)
{
    strDestroy(out);
    return _ssdCopyOutD(root, path, stCheckedPtrArg(string, out), stArg(strref, def), lstate);
}

/// bool ssdStringOutD(SSDNode *root, strref path, string *out, strref def)
///
/// Retrieves a string value with a default if not found.
///
/// The output string is automatically destroyed before being populated.
///
/// @param root The root node
/// @param path Path to the string value
/// @param out Pointer to string variable (will be destroyed and replaced)
/// @param def Default string to use if value not found
/// @return true if value was found, false if default was used
#define ssdStringOutD(root, path, out, def) \
    _ssdStringOutD(root, path, out, def, (SSDLockState*)_ssdCurrentLockState)

/// @}  // end of ssd_access

/// @defgroup ssd_modify Value Modification
/// @ingroup ssd
/// @{
///
/// Functions for setting, removing, and modifying values in the tree.

bool _ssdSet(_Inout_ SSDNode* root, _In_opt_ strref path, bool createpath, stvar val,
             _Inout_opt_ SSDLockState* lstate);

/// bool ssdSet(SSDNode *root, strref path, bool createpath, stvar val)
///
/// Sets a value at the specified path in the tree.
///
/// If createpath is true, any intermediate nodes along the path are automatically created
/// as hashtables. If false and the path doesn't exist, the operation fails.
///
/// @param root The root node
/// @param path Path where to set the value (NULL for root node)
/// @param createpath If true, create intermediate nodes as needed
/// @param val The value to set (copied into the tree)
/// @return true on success, false on failure
///
/// Example:
/// @code
///   ssdSet(root, _S"config/port", true, stvar(int32, 8080));
/// @endcode
#define ssdSet(root, path, createpath, val) \
    _ssdSet(root, path, createpath, val, (SSDLockState*)_ssdCurrentLockState)

bool _ssdSetC(_Inout_ SSDNode* root, _In_opt_ strref path, bool createpath,
              _Pre_notnull_ _Post_invalid_ stvar* val, _Inout_opt_ SSDLockState* lstate);

/// bool ssdSetC(SSDNode *root, strref path, bool createpath, stvar *val)
///
/// Sets a value at the specified path, consuming the provided stvar.
///
/// This function takes ownership of the value and will consume it even on failure.
/// This is more efficient than ssdSet() when you don't need to keep the original value.
///
/// @param root The root node
/// @param path Path where to set the value
/// @param createpath If true, create intermediate nodes as needed
/// @param val Pointer to the value (consumed and invalidated)
/// @return true on success, false on failure
#define ssdSetC(root, path, createpath, val) \
    _ssdSetC(root, path, createpath, val, (SSDLockState*)_ssdCurrentLockState)

bool _ssdRemove(_Inout_ SSDNode* root, _In_opt_ strref path, _Inout_opt_ SSDLockState* lstate);

/// bool ssdRemove(SSDNode *root, strref path)
///
/// Removes a value at the specified path from the tree.
///
/// @param root The root node
/// @param path Path to the value to remove
/// @return true if the value was removed, false if not found
#define ssdRemove(root, path) _ssdRemove(root, path, (SSDLockState*)_ssdCurrentLockState)

/// @}  // end of ssd_modify

/// @defgroup ssd_pointer Pointer Access (Lock Required)
/// @ingroup ssd
/// @{
///
/// Functions that return pointers to internal storage. These require explicit locking
/// via ssdLockedTransaction() and are only valid while the lock is held.

stvar* _ssdPtr(_In_ SSDNode* root, _In_opt_ strref path, _Inout_ SSDLockState* lstate);

/// stvar *ssdPtr(SSDNode *root, strref path)
///
/// Returns a pointer to the internal stvar storage at the specified path.
///
/// **CRITICAL:** This function MUST be used within a ssdLockedTransaction() or manual lock.
/// The pointer is only valid while the lock is held. Accessing it after the lock is released
/// results in undefined behavior.
///
/// @param root The root node
/// @param path Path to the value
/// @return Pointer to internal stvar, or NULL if not found
///
/// Example:
/// @code
///   ssdLockedTransaction(root) {
///       stvar *val = ssdPtr(root, _S"config/timeout");
///       if (val && stvarIs(val, int32)) {
///           // use val->data.st_int32
///       }
///   }
/// @endcode
#define ssdPtr(root, path) \
    _ssdPtr(root, path, (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState)

_Ret_opt_valid_ _meta_inline strref
_ssdStrRef(_In_ SSDNode* root, _In_opt_ strref path, _Inout_ SSDLockState* lstate)
{
    stvar* temp = _ssdPtr(root, path, lstate);
    return stvarIs(temp, string) ? temp->data.st_string : NULL;
}

/// strref ssdStrRef(SSDNode *root, strref path)
///
/// Returns a string reference directly from internal storage.
///
/// **CRITICAL:** This function MUST be used within a ssdLockedTransaction(). The string
/// reference is only valid while the lock is held.
///
/// @param root The root node
/// @param path Path to the string value
/// @return String reference, or NULL if not found or wrong type
#define ssdStrRef(root, path) \
    _ssdStrRef(root, path, (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState)

_Must_inspect_result_ _Ret_opt_valid_ _meta_inline ObjInst*
_ssdObjInstPtr(_In_ SSDNode* root, _In_opt_ strref path, _Inout_ SSDLockState* lstate)
{
    stvar* temp = _ssdPtr(root, path, lstate);
    return stvarIs(temp, object) ? temp->data.st_object : NULL;
}

/// ObjInst *ssdObjInstPtr(SSDNode *root, strref path)
///
/// Returns an object instance pointer directly from internal storage.
///
/// **CRITICAL:** This function MUST be used within a ssdLockedTransaction(). The pointer
/// is only valid while the lock is held.
///
/// @param root The root node
/// @param path Path to the object value
/// @return Object instance pointer, or NULL if not found or wrong type
#define ssdObjInstPtr(root, path) \
    _ssdObjInstPtr(root, path, (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState)

/// ClassName *ssdObjPtr(SSDNode *root, strref path, ClassName)
///
/// Returns a typed object pointer with runtime type checking.
///
/// This is a convenience wrapper around ssdObjInstPtr() that performs a dynamic cast
/// to the specified class type.
///
/// @param root The root node
/// @param path Path to the object value
/// @param clsname The class name to cast to
/// @return Typed object pointer, or NULL if not found, wrong type, or cast fails
#define ssdObjPtr(root, path, clsname) objDynCast(clsname, ssdObjInstPtr(root, path))

/// @}  // end of ssd_pointer

/// @defgroup ssd_array Array Operations
/// @ingroup ssd
/// @{
///
/// Functions for importing/exporting arrays and working with array nodes.

bool _ssdExportArray(_In_ SSDNode* root, _In_opt_ strref path, _Inout_ sa_stvar* out,
                     _Inout_opt_ SSDLockState* lstate);

/// bool ssdExportArray(SSDNode *root, strref path, sa_stvar *out)
///
/// Exports an array node to a dynamic array of stvars.
///
/// The output array must be either initialized or point to NULL. The function will populate
/// it with copies of all values in the SSD array node.
///
/// @param root The root node
/// @param path Path to the array node
/// @param out Pointer to sa_stvar (initialized or NULL)
/// @return true on success, false if path doesn't point to an array
#define ssdExportArray(root, path, out) \
    _ssdExportArray(root, path, out, (SSDLockState*)_ssdCurrentLockState)

bool _ssdImportArray(_Inout_ SSDNode* root, _In_opt_ strref path, _In_ sa_stvar arr,
                     _Inout_opt_ SSDLockState* lstate);

/// bool ssdImportArray(SSDNode *root, strref path, sa_stvar arr)
///
/// Imports a dynamic array of stvars into an array node.
///
/// Creates an array node at the specified path and populates it with the values.
///
/// @param root The root node
/// @param path Path where to create/replace the array
/// @param arr The source array of stvars
/// @return true on success, false on failure
#define ssdImportArray(root, path, arr) \
    _ssdImportArray(root, path, arr, (SSDLockState*)_ssdCurrentLockState)

bool _ssdExportTypedArray(_In_ SSDNode* root, _In_opt_ strref path, stype elemtype,
                          _Inout_ sahandle out, bool strict, _Inout_opt_ SSDLockState* lstate);
bool _ssdImportTypedArray(_Inout_ SSDNode* root, _In_opt_ strref path, stype elemtype,
                          _In_ sa_ref arr, _Inout_opt_ SSDLockState* lstate);

/// bool ssdExportTypedArray(SSDNode *root, strref path, type, sahandle out, bool strict)
///
/// Exports an array node to a typed dynamic array.
///
/// The output array must be either initialized or point to NULL. Values are converted
/// to the specified element type during export.
///
/// @param root The root node
/// @param path Path to the array node
/// @param type The desired element type
/// @param out Pointer to typed sarray (initialized or NULL)
/// @param strict If true, fails on type mismatch; if false, skips incompatible items
/// @return true on success, false on failure
///
/// Example:
/// @code
///   sa_int32 values;
///   saInit(&values, int32, 0);
///   if (ssdExportTypedArray(root, _S"scores", int32, &values, false)) {
///       // use values.a[]
///   }
///   saDestroy(&values);
/// @endcode
#define ssdExportTypedArray(root, path, type, out, strict) \
    _ssdExportTypedArray(root,                             \
                         path,                             \
                         stType(type),                     \
                         SAHANDLE(out),                    \
                         strict,                           \
                         (SSDLockState*)_ssdCurrentLockState)

/// bool ssdImportTypedArray(SSDNode *root, strref path, type, sa_ref arr)
///
/// Imports a typed dynamic array into an array node.
///
/// @param root The root node
/// @param path Path where to create/replace the array
/// @param type The element type of the source array
/// @param arr Reference to the source array
/// @return true on success, false on failure
#define ssdImportTypedArray(root, path, type, arr) \
    _ssdImportTypedArray(root, path, stType(type), SAREF(arr), (SSDLockState*)_ssdCurrentLockState)

int32 _ssdCount(_In_ SSDNode* root, _In_opt_ strref path, bool arrayonly,
                _Inout_opt_ SSDLockState* lstate);

/// int32 ssdCount(SSDNode *root, strref path, bool arrayonly)
///
/// Returns the number of children/elements at the given path.
///
/// @param root The root node
/// @param path Path to the node to count
/// @param arrayonly If true, returns 0 for hashtable nodes
/// @return Number of elements, or 0 if not found or empty
#define ssdCount(root, path, arrayonly) \
    _ssdCount(root, path, arrayonly, (SSDLockState*)_ssdCurrentLockState)

_Ret_opt_valid_ stvar* _ssdIndex(_In_ SSDNode* root, _In_opt_ strref path, int32 idx,
                                 _Inout_ SSDLockState* lstate);

/// stvar *ssdIndex(SSDNode *root, strref path, int32 idx)
///
/// Gets a pointer to a specific array element by index.
///
/// **CRITICAL:** Like ssdPtr(), this requires a lock via ssdLockedTransaction().
/// The pointer is only valid while the lock is held.
///
/// @param root The root node
/// @param path Path to the array node
/// @param idx Array index to access
/// @return Pointer to the element's stvar, or NULL if not found
#define ssdIndex(root, path, idx) \
    _ssdIndex(root, path, idx, (SSDLockState*)&_ssdCurrentLockState->_is_SSDLockState)

bool _ssdAppend(_Inout_ SSDNode* root, _In_opt_ strref path, bool createpath, stvar val,
                _Inout_opt_ SSDLockState* lstate);

/// bool ssdAppend(SSDNode *root, strref path, bool createpath, stvar val)
///
/// Appends a value to an array at the given path.
///
/// If the array doesn't exist and createpath is true, it will be created automatically.
///
/// @param root The root node
/// @param path Path to the array node (NULL to append to root if it's an array)
/// @param createpath If true, create the array if it doesn't exist
/// @param val Value to append (copied)
/// @return true on success, false if the node exists but isn't an array
///
/// Example:
/// @code
///   ssdAppend(root, _S"items", true, stvar(string, _S"apple"));
///   ssdAppend(root, _S"items", true, stvar(string, _S"banana"));
/// @endcode
#define ssdAppend(root, path, createpath, val) \
    _ssdAppend(root, path, createpath, val, (SSDLockState*)_ssdCurrentLockState)

/// @}  // end of ssd_array

/// @defgroup ssd_advanced Advanced Operations
/// @ingroup ssd
/// @{
///
/// Advanced tree manipulation functions like grafting subtrees.

bool _ssdGraft(_Inout_ SSDNode* dest, _In_opt_ strref destpath,
               _Inout_opt_ SSDLockState* dest_lstate, _In_ SSDNode* src, _In_opt_ strref srcpath,
               _Inout_opt_ SSDLockState* src_lstate);

/// bool ssdGraft(SSDNode *dest, strref destpath, SSDNode *src, strref srcpath)
///
/// Grafts a subtree from the source tree onto the destination tree.
///
/// This performs a deep copy of the source subtree and attaches it to the destination
/// at the specified path. If the destination tree uses custom node classes, those will
/// be used for the copied nodes.
///
/// **IMPORTANT:** If used within a locked transaction, it must be for the DESTINATION
/// tree. The source tree is automatically locked in read-only mode with a transient lock
/// and must NOT already be locked by the caller.
///
/// @param dest The destination tree root
/// @param destpath Path in destination where to attach the subtree
/// @param src The source tree root
/// @param srcpath Path in source to the subtree to copy
/// @return true on success, false on failure
///
/// Example:
/// @code
///   // Copy config/database from one tree to another
///   ssdGraft(destTree, _S"config/database", srcTree, _S"config/database");
/// @endcode
#define ssdGraft(dest, destpath, src, srcpath) \
    _ssdGraft(dest, destpath, (SSDLockState*)_ssdCurrentLockState, src, srcpath, NULL)

/// @}  // end of ssd_advanced
