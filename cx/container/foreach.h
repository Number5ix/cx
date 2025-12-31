#pragma once

#include <cx/utils/macros.h>

/// @file foreach.h
/// @brief Universal iteration macro for containers and iterable objects

/// @defgroup foreach Generic Container Iteration
/// @ingroup containers
/// @{
///
/// Provides a unified `foreach` macro for iterating over various container types and
/// iterable objects. The macro expands to type-specific iteration code while presenting
/// a consistent interface.
///
/// @section foreach_overview Overview
///
/// The `foreach` macro adapts to different container types, automatically handling
/// iterator initialization, validity checking, advancement, and cleanup. The syntax
/// varies by container type to accommodate different iteration patterns.
///
/// @section foreach_variants Supported Container Types
///
/// @subsection foreach_sarray Dynamic Arrays (sarray)
///
/// Iterates over elements in a dynamic array with direct indexed access.
/// Elements are copied by assignment (shallow copy), allowing iteration over
/// value types without pointers.
///
/// **Syntax:**
/// @code
///   foreach(sarray, index, ElementType, element, array)
/// @endcode
///
/// **Parameters:**
/// - `index` - int32 iteration variable (0 to size-1)
/// - `ElementType` - Type of elements stored in the array (can be value or pointer type)
/// - `element` - Variable to receive each element (copied by assignment)
/// - `array` - The sarray to iterate (by value, not pointer)
///
/// **Examples:**
/// @code
///   // Iterating over pointer types
///   sa_ObjIface impls = {0};
///   // ... populate array ...
///   foreach (sarray, i, ObjIface*, impl, impls) {
///       if (!_objCheckIface(impl))
///           return false;
///   }
///
///   // Iterating over value types (no pointer needed)
///   sa_int32 numbers = {0};
///   // ... populate array ...
///   foreach (sarray, i, int32, num, numbers) {
///       total += num;
///   }
/// @endcode
///
/// @subsection foreach_hashtable Hash Tables (hashtable)
///
/// Iterates over all entries in a hash table in insertion order.
/// CX hashtables preserve the order in which entries were added.
///
/// **Syntax:**
/// @code
///   foreach(hashtable, iterator, table)
/// @endcode
///
/// **Parameters:**
/// - `iterator` - htiter variable for the iteration state
/// - `table` - The hashtable to iterate
///
/// **Accessing Values:**
/// Use `htiKey(type, iterator)` and `htiVal(type, iterator)` to copy entries from the table,
/// or use `htiKeyPtr(type, iterator)` and `htiValPtr(type, iterator)` to get direct pointers
/// to the stored keys and values without copying:
///
/// @code
///   // Copying values out of the table
///   hashtable deferred = {0};
///   // ... populate table ...
///   foreach (hashtable, it, deferred) {
///       ComplexTask *task = htiKey(object, it);
///       // process task...
///   }
///
///   // Direct access to stored values (no copy)
///   hashtable settings = {0};
///   // ... populate with string keys and int32 values ...
///   foreach (hashtable, it, settings) {
///       string *key = htiKeyPtr(string, it);
///       int32 *value = htiValPtr(int32, it);
///       // Modify value in place: (*value)++
///   }
/// @endcode
///
/// @subsection foreach_string Strings (string)
///
/// Iterates over chunks (runs) of contiguous bytes in a string. This efficiently handles
/// both simple strings (single chunk) and rope strings (multiple chunks).
///
/// **Syntax:**
/// @code
///   foreach(string, iterator, str)
/// @endcode
///
/// **Parameters:**
/// - `iterator` - striter variable for the iteration state
/// - `str` - The string to iterate
///
/// **Iterator Fields:**
/// - `iterator.bytes` - Pointer to current run of bytes
/// - `iterator.len` - Length of current run in bytes
/// - `iterator.off` - Byte offset from string start
///
/// **Example:**
/// @code
///   foreach (string, it, str) {
///       if (!sbufPWrite(sb, it.bytes, it.len))
///           return false;
///   }
/// @endcode
///
/// @subsection foreach_vfssearch File System Search (vfssearch)
///
/// Iterates over directory entries in a VFS, optionally matching a search pattern.
///
/// **Syntax:**
/// @code
///   foreach(vfssearch, iterator, vfs, path, pattern, typefilter, stat)
/// @endcode
///
/// **Parameters:**
/// - `iterator` - FSSearchIter variable for the iteration state
/// - `vfs` - VFS instance to search within
/// - `path` - Directory path to search
/// - `pattern` - Wildcard pattern (e.g., "*.txt") or NULL for all entries
/// - `typefilter` - Entry type filter (FS_File, FS_Directory, or 0 for all)
/// - `stat` - bool indicating whether to populate stat information
///
/// **Iterator Fields:**
/// - `iterator.name` - Filename (string, valid until next iteration)
/// - `iterator.type` - Entry type (FS_File or FS_Directory)
/// - `iterator.stat` - File metadata (if stat parameter was true)
///
/// **Example:**
/// @code
///   VFS *vfs = vfsCreate(0);
///   vfsMountFS(vfs, _S"/", _S"c:/data");
///   FSSearchIter iter;
///   foreach(vfssearch, iter, vfs, _S"/logs", _S"*.log", FS_File, false) {
///       // Process iter.name (log files only)
///   }
///   objRelease(&vfs);
/// @endcode
///
/// @subsection foreach_ssd SSD Tree Nodes (ssd)
///
/// Iterates over key-value pairs in an SSDTree node with automatic locking.
/// The iteration occurs within a locked transaction for thread safety.
///
/// **Syntax:**
/// @code
///   foreach(ssd, iterator, index, key, value, node)
/// @endcode
///
/// **Parameters:**
/// - `iterator` - SSDIterator* variable (managed internally)
/// - `index` - int32 variable to receive entry index
/// - `key` - strref variable to receive entry key
/// - `value` - stvar* variable to receive entry value
/// - `node` - The SSDNode to iterate
///
/// **Example:**
/// @code
///   SSDNode *btree = ssdSubtreeB(tree, _S"config");
///   foreach(ssd, oiter, idx, name, val, btree) {
///       if (strEq(name, _S"setting1") && stvarIs(val, int32)) {
///           // Process val->data.st_int32
///       }
///   }
/// @endcode
///
/// **Note:** The node is automatically locked for the duration of the iteration.
///
/// @subsection foreach_object Iterable Objects (object)
///
/// Iterates over objects implementing the Iterable interface, which provides an
/// Iterator object for traversal.
///
/// **Syntax:**
/// @code
///   foreach(object, iterator, IteratorType, obj)
/// @endcode
///
/// **Parameters:**
/// - `iterator` - Iterator pointer variable
/// - `IteratorType` - Type of the iterator class
/// - `obj` - Object implementing the Iterable interface
///
/// **Example:**
/// @code
///   MyIterable *collection = ...;
///   foreach(object, iter, MyIterator, collection) {
///       stvar val;
///       if (iter->_->get(iter, &val)) {
///           // Process val
///       }
///   }
/// @endcode
///
/// **Note:** The iterator is automatically released when the loop exits.
///
/// @section foreach_usage Usage Guidelines
///
/// - All iterator variables are automatically initialized and cleaned up
/// - Breaking out of a foreach loop is safe - cleanup code always runs
/// - Iterator variables should not be modified manually
/// - Modifying the container during iteration may cause undefined behavior
/// - For hashtables and strings, use accessor functions to retrieve data
///
/// @}

/// foreach(type, itervar, ...)
///
/// Universal iteration macro for containers and iterable objects
///
/// The `foreach` macro provides a consistent interface for iterating over various
/// container types. The specific parameters required after `itervar` depend on the
/// container type being iterated. See the detailed documentation above for
/// type-specific usage patterns.
///
/// @param type Container type (sarray, hashtable, string, vfssearch, ssd, object)
/// @param itervar Name for the iterator variable
/// @param ... Type-specific initialization parameters (varies by container type)
///
/// Example:
/// @code
///   // Array iteration
///   foreach(sarray, i, MyType*, elem, myArray) { ... }
///
///   // Hash table iteration
///   foreach(hashtable, it, myHashTable) { ... }
///
///   // String chunk iteration
///   foreach(string, it, myString) { ... }
/// @endcode
#define foreach(type, itervar, ...) foreach_##type(type, itervar, __VA_ARGS__)

#define foreach_generic(type, itervar, ...) for (register char _foreach_outer = 1; _foreach_outer; _foreach_outer = 0) \
        for(ForEachIterType_##type itervar = ForEachIterInit_##type; _foreach_outer; _foreach_outer = 0) \
        for(; _foreach_outer; ForEachFinish_##type(itervar), _foreach_outer = 0) \
        for(ForEachInit_##type(itervar, __VA_ARGS__); ForEachValid_##type(itervar); ForEachNext_##type(itervar))

#define foreach_sarray(...) _foreach_array_msvc_workaround((__VA_ARGS__))
#define _foreach_array_msvc_workaround(args) _foreach_sarray args
#define _foreach_sarray(type, itervar, elemtype, elemvar, arrref) if ((arrref)._is_sarray) \
        for (register char _foreach_outer = 1; _foreach_outer; _foreach_outer = 0) \
        for (int32 itervar = 0, _##itervar##_max = saSize(arrref); _foreach_outer; _foreach_outer = 0) \
        for (elemtype elemvar = (elemtype){0}; _foreach_outer; _foreach_outer = 0) \
        for (itervar = 0; itervar < _##itervar##_max && (elemvar = (arrref).a[itervar], 1); ++itervar)

#define foreach_hashtable foreach_generic
#define ForEachIterType_hashtable htiter
#define ForEachIterInit_hashtable {0}
#define ForEachInit_hashtable(itervar, ht) htiInit(&itervar, ht)
#define ForEachValid_hashtable(itervar) htiValid(&itervar)
#define ForEachNext_hashtable(itervar) htiNext(&itervar)
#define ForEachFinish_hashtable(itervar) htiFinish(&itervar)

#define foreach_string foreach_generic
#define ForEachIterType_string striter
#define ForEachIterInit_string {0}
#define ForEachInit_string(itervar, str) striInit(&itervar, str)
#define ForEachValid_string(itervar) striValid(&itervar)
#define ForEachNext_string(itervar) striNext(&itervar)
#define ForEachFinish_string(itervar) striFinish(&itervar)

#define foreach_vfssearch foreach_generic
#define ForEachIterType_vfssearch FSSearchIter
#define ForEachIterInit_vfssearch {0}
#define ForEachInit_vfssearch(itervar, ...) vfsSearchInit(&itervar, __VA_ARGS__)
#define ForEachValid_vfssearch(itervar) vfsSearchValid(&itervar)
#define ForEachNext_vfssearch(itervar) vfsSearchNext(&itervar)
#define ForEachFinish_vfssearch(itervar) vfsSearchFinish(&itervar)

#define foreach_object(...) _foreach_object_msvc_workaround((__VA_ARGS__))
#define _foreach_object_msvc_workaround(args) _foreach_object args
#define _foreach_object(type, itervar, ivartype, obj) for (register char _foreach_outer = 1; _foreach_outer; _foreach_outer = 0) \
        for(ivartype *itervar = (obj) ? (obj)->_->iter(obj) : NULL; _foreach_outer; objRelease(&itervar), _foreach_outer = 0) \
        for(; itervar && itervar->_->valid(itervar); itervar->_->next(itervar))

#define foreach_ssd(...) _foreach_ssd_msvc_workaround((__VA_ARGS__))
#define _foreach_ssd_msvc_workaround(args) _foreach_ssd args
#define _foreach_ssd(type, itervar, idxvar, keyvar, valvar, root) for (register char _foreach_outer = 1; _foreach_outer; _foreach_outer = 0) \
        ssdLockedTransaction(root) \
        for(SSDIterator *itervar = (root) ? (root)->_->_iterLocked(root, _ssdCurrentLockState) : NULL; _foreach_outer; objRelease(&itervar), _foreach_outer = 0) \
        for(int32 idxvar = 0; _foreach_outer; _foreach_outer = 0) \
        for(strref keyvar = 0; _foreach_outer; _foreach_outer = 0) \
        for(stvar *valvar = 0; itervar && itervar->_->iterOut(itervar, &idxvar, &keyvar, &valvar); itervar->_->next(itervar))
