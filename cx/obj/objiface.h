#pragma once

/// @file objiface.h
/// @brief Interface definitions and management for the object system

#include <cx/cx.h>
#include <cx/container/sarray.h>

/// @defgroup obj_interface Interfaces
/// @ingroup obj
/// @{
///
/// Interfaces define contracts that classes implement. An interface is a list of function
/// pointers representing methods that fulfill a specific contract. Interfaces support
/// inheritance, allowing child interfaces to extend parent interfaces.
///
/// The ObjIface structure serves dual purposes depending on context:
///
/// **Template Form** - The publicly declared master copy of an interface:
/// - Contains basic metadata: parent interface and structure size
/// - All method pointers are NULL
/// - The template's memory address uniquely identifies the interface
/// - Used as a key to query classes for interface implementations
/// - When queried, the system returns a populated copy with method pointers filled in
///
/// **Implementation Form** - A statically scoped instance used during class definition:
/// - Contains a pointer to the interface template being implemented
/// - Contains function pointers to the actual implementation methods
/// - Used to register an implementation with a specific class
/// - Multiple classes can provide different implementations of the same interface
///
/// Example from a `.cxh` file:
/// @code
/// interface Printable {
///     void print();
///     void println();
/// }
///
/// class Document implements Printable {
///     string content;
/// }
/// @endcode
///
/// This generates a template `Printable_tmpl` and implementation structures automatically.

typedef struct ObjIface ObjIface;

/// Core interface structure
///
/// This structure has different interpretations based on context. See the @ref obj_interface
/// overview for detailed explanation of template vs implementation forms.
typedef struct ObjIface
{
    ObjIface *_implements;      ///< Implementation only: pointer to interface template being implemented
    ObjIface *_parent;          ///< Template only: parent interface for inheritance (NULL if no parent)
    size_t _size;               ///< Size of the entire interface structure including method pointers

    // Interface method pointers follow this header
    // The specific methods depend on the interface definition
} ObjIface;
saDeclarePtr(ObjIface);

/// Construct interface template name from interface type name
///
/// @param iface Interface type name (e.g., Printable)
/// @return Expands to the template variable name (e.g., Printable_tmpl)
#define objIfTmplName(iface) iface##_tmpl

/// Import an external interface template declaration
///
/// @param iface Interface type name
/// @return Expands to extern declaration for the interface template
#define objIfImport(iface) extern iface objIfTmplName(iface)

/// void objIfCheck(InterfaceType *iface)
///
/// Compile-time check that a pointer is to a valid interface structure
///
/// @param iface Pointer to interface structure to validate
#define objIfCheck(iface) static_assert(((iface)->_implements, (iface)->_size, offsetof(*(iface), _parent) == offsetof(ObjIface, _parent)), "Not an interface")

/// ObjIface *objIfBase(InterfaceType *iface)
///
/// Cast an interface pointer to base ObjIface type
///
/// @param iface Pointer to any interface structure
/// @return Base ObjIface pointer (performs validation through comma operator)
#define objIfBase(iface) ((ObjIface*)((iface)->_implements, (iface)->_parent, (iface)->_size, (iface)))

/// @}  // end of obj_interface group
