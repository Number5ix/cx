#pragma once

/// @file objclass.h
/// @brief Class definitions and instance management for the object system

#include <cx/obj/objiface.h>
#include <cx/container/hashtable.h>
#include <cx/thread/atomic.h>
#include <cx/utils/macros/unused.h>
#include <cx/thread/rwlock.h>

/// @defgroup obj_class Classes
/// @ingroup obj
/// @{
///
/// Classes are the concrete types in the object system that store per-instance data and
/// implement interfaces. Each class has associated runtime metadata (ObjClassInfo) that
/// describes its structure, inheritance relationships, and interface implementations.
///
/// Classes support:
/// - Single implementation inheritance (one parent class)
/// - Multiple interface implementation
/// - Reference-counted memory management
/// - Runtime type information and dynamic casting
/// - Optional initialization and destruction callbacks
///
/// Classes are defined in `.cxh` files and the build system generates the boilerplate code.

CX_C_BEGIN

typedef struct ObjClassInfo ObjClassInfo;
typedef struct ObjInst ObjInst;
typedef struct ObjClassInfo ObjClassInfo;
typedef struct ObjInst ObjInst;

/// Runtime metadata for a class
///
/// This structure contains all the information needed to manage a class at runtime,
/// including its size, inheritance relationships, interface implementations, and
/// lifecycle callbacks. One instance exists per class for the lifetime of the program.
///
/// The structure is divided into two sections:
/// - **Static fields**: Set by the class implementer during declaration
/// - **Runtime fields**: Populated automatically during first instantiation
typedef struct ObjClassInfo {
    // ----- Statically defined in class info instance by class implementer -----
    
    size_t instsize;            ///< Size in bytes of a class instance
    ObjClassInfo *parent;       ///< Parent class (NULL if no parent)
    ObjIface *classif;          ///< Class interface (methods specific to this class, optional)
    ObjIface **ifimpl;          ///< NULL-terminated array of interface implementations

    /// Optional initialization callback
    ///
    /// Called at the end of factory functions, after members are initialized but before
    /// returning to caller. If init returns false, construction must abort and the
    /// factory should return NULL. Useful for validation or common initialization that
    /// applies regardless of which factory is used.
    bool(*init)(void *_self);

    /// Optional destruction callback
    ///
    /// Called just before the object's memory is deallocated. Use this to clean up
    /// resources, release owned objects, destroy strings, etc. The framework automatically
    /// calls parent class destructors after the child destructor.
    void(*destroy)(void *_self);

    bool _abstract;             ///< True if this is an abstract class (cannot be instantiated)

    // ----- Runtime use only, do not set manually! -----
    
    /// Cached function pointer for Sortable.cmp interface (NULL if not implemented)
    intptr (*_cmp)(void *self, void *other, uint32 flags);
    
    /// Cached function pointer for Hashable.hash interface (NULL if not implemented)
    uint32 (*_hash)(void *self, uint32 flags);
    
    sa_ObjIface _impl;          ///< Storage for hydrated interface implementations
    hashtable _tmpl;            ///< Maps interface templates to populated implementations
} ObjClassInfo;

/// Construct class info variable name from class name
///
/// @param cname Class name
/// @return Expands to the class info variable name (e.g., Document_clsinfo)
#define objClassInfoName(cname) cname##_clsinfo

/// Base structure for all object instances
///
/// ObjInst is the implicit base class for all objects in the CX object system. Every
/// class instance begins with these fields, allowing generic handling of objects regardless
/// of their specific class type.
///
/// The structure uses a union trick with `_is_ObjInst` to enable compile-time type checking
/// in macros while maintaining C compatibility.
typedef struct ObjInst {
    union {
        ObjIface *_classif;         ///< Class interface (vtable) - called _ in generated class code
        void *_is_ObjInst;          ///< Type marker for compile-time validation
    };
    ObjClassInfo *_clsinfo;         ///< Pointer to class metadata
    atomic(uintptr) _ref;           ///< Reference count for memory management
    atomic(ptr) _weakref;           ///< Associated weak reference object (NULL if none exist)

    // Class-specific data members follow in derived classes
} ObjInst;

/// Weak reference structure
///
/// Allows observing an object without owning it. When the object is destroyed, the weak
/// reference's `_inst` pointer is set to NULL, allowing safe detection of destruction.
/// Multiple weak references can point to the same object.
typedef struct ObjInst_WeakRef
{
    union
    {
        ObjInst *_inst;             ///< Pointer to the referenced object (NULL if destroyed)
        void *_is_ObjInst_WeakRef;  ///< Type marker for compile-time validation
    };
    atomic(uintptr) _ref;           ///< Reference count for the weak reference itself
    RWLock _lock;                   ///< Protects access during object destruction
} ObjInst_WeakRef;

/// Construct weak reference type name from class name
///
/// @param clsname Class name
/// @return Expands to the weak reference type (e.g., Document_WeakRef)
#define Weak(clsname) clsname##_WeakRef

/// ObjInst *ObjInst(ClassType *inst)
///
/// Cast a class instance pointer to base ObjInst type
///
/// @param inst Pointer to any class instance
/// @return Base ObjInst pointer (with compile-time type validation)
#define ObjInst(inst) ((ObjInst*)(unused_noeval(&((inst)->_is_ObjInst)), (inst)))

/// ObjInst_WeakRef *ObjInst_WeakRef(WeakRefType *ref)
///
/// Cast a weak reference pointer to base ObjInst_WeakRef type
///
/// @param ref Pointer to any weak reference
/// @return Base ObjInst_WeakRef pointer (with compile-time type validation)
#define ObjInst_WeakRef(ref) ((ObjInst_WeakRef*)(unused_noeval(&((ref)->_is_ObjInst_WeakRef)), (ref)))

/// ObjInst *objInstBase(ClassType *inst)
///
/// Alias for ObjInst() cast
///
/// @param inst Pointer to any class instance
/// @return Base ObjInst pointer
#define objInstBase(inst) ObjInst(inst)

/// ObjInst_WeakRef *objWeakRefBase(WeakRefType *ref)
///
/// Alias for ObjInst_WeakRef() cast
///
/// @param ref Pointer to any weak reference
/// @return Base ObjInst_WeakRef pointer
#define objWeakRefBase(ref) ObjInst_WeakRef(ref)

/// ObjClassInfo *objClsInfo(ClassType *inst)
///
/// Get the class metadata for an object instance
///
/// @param inst Pointer to any class instance
/// @return Pointer to the class's ObjClassInfo structure
#define objClsInfo(inst) (inst->_clsinfo)

/// Internal object destruction function - DO NOT CALL DIRECTLY!
///
/// This function is called internally when an object's reference count reaches zero.
/// Always use objRelease() instead, which properly manages reference counting.
void _objDestroy(_Pre_notnull_ _Post_invalid_ ObjInst *inst);

_meta_inline void _objAcquire(_In_opt_ ObjInst *inst)
{
    if (inst)
        atomicFetchAdd(uintptr, &inst->_ref, 1, Relaxed);
}

/// ClassType *objAcquire(ClassType *inst)
///
/// Increment the reference count of an object
///
/// Use this when storing an additional reference to an object. Each objAcquire() must
/// be paired with a corresponding objRelease(). Does nothing if inst is NULL.
///
/// Example:
/// @code
/// Document *doc = documentCreate(_S"Title");  // refcount = 1
/// Document *doc2 = objAcquire(doc);           // refcount = 2
/// objRelease(&doc);                           // refcount = 1
/// objRelease(&doc2);                          // refcount = 0, object destroyed
/// @endcode
///
/// @param inst Pointer to object instance (may be NULL)
/// @return The same pointer passed in (for convenience)
#define objAcquire(inst) (_objAcquire(objInstBase(inst)), (inst))

_At_(*instp, _Pre_maybenull_ _Post_null_)
void _objRelease(_Inout_ ObjInst **instp);

/// void objRelease(ClassType **pinst)
///
/// Decrement reference count and destroy object if it reaches zero
///
/// This is the primary way to release ownership of an object. The pointer is automatically
/// set to NULL after the call. If the reference count reaches zero, the object's destroy()
/// callback (if any) is called, followed by memory deallocation. Does nothing if *pinst is NULL.
///
/// **CRITICAL**: Pass a pointer-to-pointer, not a pointer. The object pointer will be set to NULL.
///
/// Example:
/// @code
/// Document *doc = documentCreate(_S"Title");
/// // Use doc...
/// objRelease(&doc);  // Note: &doc, not doc
/// // doc is now NULL
/// @endcode
///
/// @param pinst Pointer to object pointer (automatically set to NULL)
#define objRelease(pinst) (unused_noeval(&((*(pinst))->_is_ObjInst)), _objRelease((ObjInst**)(pinst)))

_Ret_maybenull_ ObjIface *_objClassIf(_In_ ObjClassInfo *cls, _In_ ObjIface *iftmpl);
/// InterfaceType *objClassIf(ClassName, InterfaceName)
///
/// Get interface implementation from a class (without instance)
///
/// Queries whether a class implements a specific interface and returns the populated
/// interface structure if it does. Useful for accessing interface methods when you
/// don't have an instance.
///
/// @param clsname Class name (e.g., Document)
/// @param ifname Interface name (e.g., Printable)
/// @return Pointer to populated interface, or NULL if class doesn't implement it
#define objClassIf(clsname, ifname) ((ifname*)_objClassIf(&objClassInfoName(clsname), objIfBase(&objIfTmplName(ifname))))

_Ret_maybenull_ ObjIface *_objInstIf(_In_opt_ ObjInst *inst, _In_ ObjIface *iftmpl);
/// InterfaceType *objInstIf(ClassType *inst, InterfaceName)
///
/// Query an object for a specific interface implementation
///
/// Returns the populated interface if the object implements it, NULL otherwise. Always
/// check the return value before using the interface to ensure the object supports it.
///
/// Example:
/// @code
/// ObjInst *obj = getSomeObject();
/// Printable *printIf = objInstIf(obj, Printable);
/// if (printIf) {
///     printIf->print(obj);
/// }
/// @endcode
///
/// @param inst Object instance (may be NULL)
/// @param ifname Interface name
/// @return Pointer to populated interface, or NULL if not implemented
#define objInstIf(inst, ifname) ((ifname*)_objInstIf(objInstBase(inst), objIfBase(&objIfTmplName(ifname))))

_Ret_valid_ ObjInst_WeakRef *_objGetWeak(_In_ ObjInst *inst);
/// Weak(ClassName) *objGetWeak(ClassName, ClassType *inst)
///
/// Get a weak reference to an object
///
/// Creates a weak reference if one doesn't exist, or returns the existing weak reference.
/// Weak references allow observing an object without owning it. The returned weak reference
/// has its own reference count and must be destroyed with objDestroyWeak().
///
/// @param clsname Class name for type casting
/// @param inst Object instance
/// @return Weak reference to the object (never NULL)
#define objGetWeak(clsname, inst) ((Weak(clsname)*)_objGetWeak((ObjInst*)clsname(inst)))

_Ret_maybenull_ ObjInst_WeakRef *_objCloneWeak(_In_opt_ ObjInst_WeakRef *ref);
/// WeakRefType *objCloneWeak(WeakRefType *ref)
///
/// Increment the reference count of a weak reference
///
/// Creates an additional reference to the same weak reference object. Each objCloneWeak()
/// must be paired with objDestroyWeak().
///
/// @param ref Weak reference to clone (may be NULL)
/// @return The same weak reference with incremented refcount
#define objCloneWeak(ref) (_objCloneWeak(objWeakRefBase(ref)))

void _objDestroyWeak(_Inout_ ObjInst_WeakRef **refp);
/// void objDestroyWeak(WeakRefType **pref)
///
/// Release a weak reference
///
/// Decrements the weak reference's own reference count. When the count reaches zero,
/// the weak reference object itself is destroyed. The pointer is set to NULL.
///
/// Example:
/// @code
/// Weak(Document) *weak = objGetWeak(Document, doc);
/// // Use weak reference...
/// objDestroyWeak(&weak);  // weak is now NULL
/// @endcode
///
/// @param pref Pointer to weak reference pointer (automatically set to NULL)
#define objDestroyWeak(pref) (unused_noeval(&((*(pref))->_is_ObjInst_WeakRef)), _objDestroyWeak((ObjInst_WeakRef**)(pref)))

_Ret_maybenull_ ObjInst *_objAcquireFromWeak(_In_opt_ ObjInst_WeakRef *ref);
/// ClassName *objAcquireFromWeak(ClassName, Weak(ClassName) *ref)
///
/// Attempt to acquire the object referenced by a weak reference
///
/// If the object still exists, increments its reference count and returns it. If the
/// object has been destroyed, returns NULL. The caller is responsible for calling
/// objRelease() on the returned object.
///
/// Example:
/// @code
/// Weak(Document) *weak = objGetWeak(Document, doc);
/// // ... later, possibly after doc was released ...
/// Document *doc2 = objAcquireFromWeak(Document, weak);
/// if (doc2) {
///     // Object still exists
///     objRelease(&doc2);
/// }
/// @endcode
///
/// @param clsname Class name for type casting
/// @param ref Weak reference (may be NULL)
/// @return Object pointer with incremented refcount, or NULL if object was destroyed
#define objAcquireFromWeak(clsname, ref) ((clsname*)_objAcquireFromWeak((ObjInst_WeakRef*)Weak(clsname)(ref)))

_Ret_maybenull_ ObjInst *_objDynCast(_In_opt_ ObjInst *inst, _In_ ObjClassInfo *cls);
/// ClassName *objDynCast(ClassName, ClassType *inst)
///
/// Safely cast an object to a different class type with runtime checking
///
/// Checks if the object is an instance of the specified class or any of its subclasses.
/// Returns the object cast to the requested type if compatible, NULL otherwise.
///
/// Example:
/// @code
/// ObjInst *baseObj = getSomeObject();
/// Document *doc = objDynCast(Document, baseObj);
/// if (doc) {
///     // Safe to use as Document
/// }
/// @endcode
///
/// @param clsname Class name to cast to
/// @param inst Object instance to cast (may be NULL)
/// @return Object cast to requested type, or NULL if incompatible
#define objDynCast(clsname, inst) ((clsname*)_objDynCast(objInstBase(inst), &objClassInfoName(clsname)))

_Ret_maybenull_ ObjInst *_objAcquireFromWeakDyn(_In_opt_ ObjInst_WeakRef *ref, _In_ ObjClassInfo *cls);
/// ClassName *objAcquireFromWeakDyn(ClassName, Weak(BaseClass) *ref)
///
/// Acquire from weak reference with runtime type checking
///
/// Combines objAcquireFromWeak() and objDynCast(). Attempts to acquire the object and
/// verify it's compatible with the requested class type. Returns NULL if the object was
/// destroyed or is not compatible with the target class.
///
/// @param clsname Class name to cast to
/// @param ref Weak reference (may be NULL)
/// @return Object cast to requested type with incremented refcount, or NULL
#define objAcquireFromWeakDyn(clsname, ref) ((clsname*)_objAcquireFromWeakDyn(objWeakRefBase(ref), &objClassInfoName(clsname)))

/// @}  // end of obj_class group

CX_C_END
