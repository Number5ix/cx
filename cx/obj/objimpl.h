#pragma once

/// @file objimpl.h
/// @brief Implementation helpers for class and interface authors

#include <cx/obj/objiface.h>
#include <cx/obj/objclass.h>
#include <cx/utils/macros/salieri.h>

/// @defgroup obj_impl Class Implementation
/// @ingroup obj
/// @{
///
/// Functions and macros for implementing classes and interfaces. These should only be
/// used within class implementation files, not by code that merely uses objects.
///
/// **IMPORTANT**: Do not call these functions from outside class factory functions and
/// implementation code. Regular object users should use the public API (objAcquire,
/// objRelease, etc.).

_Ret_notnull_ ObjInst *_objInstCreate(_In_ ObjClassInfo *cls);
/// ClassName *objInstCreate(ClassName)
///
/// Allocate and initialize an object instance
///
/// Allocates memory for an object instance and sets up the base ObjInst fields. The
/// instance is returned with a reference count of 1. This function does NOT call the
/// class's init() callback - that must be done separately via objInstInit().
///
/// **Only call from factory functions!**
///
/// Example usage in a factory function:
/// @code
/// Document *documentCreate(string title) {
///     Document *ret = objInstCreate(Document);
///     ret->title = 0;
///     strDup(&ret->title, title);
///     if (!objInstInit(ret))
///         goto error;
///     return ret;
/// error:
///     objRelease(&ret);
///     return NULL;
/// }
/// @endcode
///
/// @param clsname Class name
/// @return Newly allocated object instance (never NULL)
#define objInstCreate(clsname) (clsname*)_objInstCreate(&objClassInfoName(clsname))

bool _objInstInit(_Inout_ ObjInst *inst, _In_ ObjClassInfo *cls);
/// bool objInstInit(ClassType *inst)
///
/// Call initialization callbacks for the object's class hierarchy
///
/// Recursively calls init() callbacks starting from the root parent class down to the
/// object's actual class. Must be called at the end of factory functions, after setting
/// up per-instance data but before returning to the caller.
///
/// If any init() returns false, construction must abort and the factory should clean up
/// and return NULL.
///
/// @param inst Object instance to initialize
/// @return true if all init callbacks succeeded, false if any failed
#define objInstInit(inst) _objInstInit(objInstBase(inst), (inst)->_clsinfo)

/// Default comparison function for Sortable interface
///
/// Provides a generic comparison by comparing the raw bytes of object instances after
/// the ObjInst header. First compares instance sizes, then performs memcmp on the data.
///
/// Use this as a fallback when a class implements Sortable but doesn't need custom
/// comparison logic. For classes with pointers or complex data, implement a custom
/// comparison function instead.
///
/// @param self First object to compare
/// @param other Second object to compare
/// @param flags Comparison flags (currently unused)
/// @return Negative if self < other, 0 if equal, positive if self > other
intptr objDefaultCmp(_In_ void *self, _In_ void *other, uint32 flags);

/// Default hash function for Hashable interface
///
/// Provides a generic hash by running MurmurHash3 over the raw bytes of the object
/// instance after the ObjInst header.
///
/// Use this as a fallback when a class implements Hashable but doesn't need custom
/// hash logic. For classes with pointers or complex data, implement a custom hash
/// function instead.
///
/// @param self Object to hash
/// @param flags Hash flags (currently unused)
/// @return 32-bit hash value
uint32 objDefaultHash(_In_ void *self, uint32 flags);

/// SAL annotation indicating init callback always succeeds
///
/// Use in class definitions when the init callback is guaranteed to return true.
#define _objinit_guaranteed _Post_equal_to_(true)

/// SAL annotation indicating factory function always succeeds
///
/// Use for factory functions that are guaranteed to return a valid object (never NULL).
#define _objfactory_guaranteed _Ret_valid_

/// SAL annotation indicating factory function may fail
///
/// Use for factory functions that can fail and return NULL. Callers must check the
/// return value.
#define _objfactory_check _Ret_opt_valid_ _Check_return_

/// @}  // end of obj_impl group
