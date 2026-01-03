#pragma once

/// @file obj.h
/// @brief CX Object System - Object-oriented programming in C

/// @defgroup obj Object System
/// @{
///
/// The CX object system brings object-oriented programming to C with interfaces, inheritance,
/// reference counting, and runtime type information. It uses a custom Interface Definition
/// Language (IDL) to minimize boilerplate while maintaining type safety and C compatibility.
///
/// # Key Features
///
/// - **Interfaces**: Abstract contracts that classes implement, supporting multiple inheritance
/// - **Classes**: Concrete types with data and methods, supporting single implementation
/// inheritance
/// - **Reference Counting**: Automatic memory management through acquire/release
/// - **Runtime Type System**: Dynamic casting and interface queries
/// - **Weak References**: Non-owning pointers that safely detect object destruction
/// - **Code Generation**: `.cxh` files generate boilerplate C code automatically
///
/// # Working with .cxh Files
///
/// Instead of writing verbose C code, define interfaces and classes in `.cxh` files:
///
/// @code
/// // myclass.cxh
/// interface Printable {
///     void print();
/// }
///
/// class Document implements Printable {
///     string title;
///     string content;
///     factory create(string title);
/// }
/// @endcode
///
/// The build system (via `add_cxautogen()` in CMakeLists.txt) generates:
/// - `myclass.h` - Public API declarations
/// - `myclass.c` - Implementation stubs to fill in
/// - `myclass.auto.inc` - Internal boilerplate
///
/// **Never edit generated .h files directly** - modify the `.cxh` source instead.
///
/// ## Advanced .cxh Features
///
/// **Abstract classes** cannot be instantiated directly:
/// @code
/// abstract class Iterator {
///     bool valid();
///     bool next();
/// }
/// @endcode
///
/// **Mixins** are abstract classes that can be included in multiple classes using `uses`:
/// @code
/// mixin class Logging {
///     void log(string message);
/// }
///
/// class Document uses Logging {
///     // Document gets all Logging methods and members
/// }
/// @endcode
///
/// **Method qualifiers** control how methods are bound and called:
/// - `override` - Mark method as overriding parent implementation (just `override methodname;`, no need to repeat parameters)
/// - `unbound` - Method does not dynamically resolve to child class implementations (analogous to Java's `final` or C++'s non-virtual methods)
/// - `standalone` - Function not bound to class and does NOT take self parameter (like static)
///
/// **Member and method annotations** in `[brackets]`:
/// - `[methodprefix xyz]` - Override default method prefix (class name in lowercase)
/// - `[noinit]` - Member is not initialized by generated code, factory must handle it
/// - `[sal ...]` - SAL annotations for static analysis
/// - `[opt]` - Function can return NULL/optional result
/// - `[out]`, `[inout]` - Parameter direction hints
///
/// Example:
/// @code
/// [methodprefix doc]
/// class Document {
///     [noinit] string title;       // Factory must initialize manually
///     override print;              // Just the method name, prototype inherited
///     unbound void validate();     // Not polymorphic, always calls Document's version
///     standalone Document *fromFile(string path);  // No implicit self parameter
/// }
/// @endcode
///
/// **Note**: Method implementations receive the object instance as the `self` parameter (not `this`).
///
/// # Using Objects
///
/// Objects use reference counting for memory management:
///
/// @code
/// // Create object using factory method (usually 'create', but can vary)
/// Document *doc = documentCreate(_S"My Document");
///
/// // Call methods using wrapper macros (most common)
/// documentPrint(doc);
///
/// // Release when done (decrements refcount, destroys at 0)
/// objRelease(&doc);  // doc is set to NULL
/// @endcode
///
/// If declaring an object pointer without immediately creating it, initialize to NULL:
///
/// @code
/// Document *doc = 0;  // NULL until assigned
/// if (condition) {
///     doc = documentCreate(_S"Title");
/// }
/// @endcode
///
/// # Object Lifecycle: Initializers and Destructors
///
/// Classes can define optional init and destroy callbacks for custom initialization and cleanup.
/// Declare them in the `.cxh` file and implement in the `.c` file:
///
/// @code
/// // document.cxh
/// class Document {
///     string title;
///     string content;
///     init();      // Optional: validate/initialize after construction
///     destroy();   // Optional: clean up resources before deallocation
/// }
///
/// // document.c - implement the callbacks
/// bool Document_init(Document *self) {
///     // Called after factory sets up members, before returning to caller
///     // Return false to abort construction
///     return validateDocument(self);
/// }
///
/// void Document_destroy(Document *self) {
///     // Called just before memory deallocation
///     // Clean up owned resources
///     strDestroy(&self->title);
///     strDestroy(&self->content);
/// }
/// @endcode
///
/// **Initialization order**: Parent class init() → Child class init()
///
/// **Destruction order**: Child class destroy() → Parent class destroy()
///
/// Factory functions must call `objInstInit()` after setting up instance data. If init
/// returns false, the factory should clean up and return NULL. The framework automatically
/// handles calling init/destroy for the entire class hierarchy.
///
/// # Reference Counting Rules
///
/// - Factory methods (usually `create()`, but can have other names) return objects with refcount = 1
/// - Generated wrapper macros use pseudo-camelCase: `documentCreate()` wraps the underlying factory
/// - `objAcquire(obj)` increments the reference count
/// - `objRelease(&obj)` decrements and destroys when count reaches 0
/// - Never use `xaFree()` directly on objects
/// - Passing objects as function parameters does NOT transfer ownership (caller still responsible)
///
/// # Interfaces
///
/// Classes can implement multiple interfaces. While you can call methods directly using
/// class wrapper macros (e.g., `documentPrint()`), you can also query for interfaces when
/// working with interface types or need to access interface methods explicitly.
///
/// Classes that declare their own methods implicitly implement a "class interface" containing
/// those methods, accessible through the class's interface table.
///
/// @code
/// // Most common: use class wrapper macros
/// Document *doc = documentCreate(_S"Title");
/// documentPrint(doc);  // Calls Printable->print implementation
///
/// // Query interface when you only have an interface pointer
/// // Best practice: check for NULL to ensure object implements the interface
/// ObjInst *obj = getSomeObject();
/// Printable *printIf = objInstIf(obj, Printable);
/// if (printIf) {
///     printIf->print(obj);
/// }
///
/// // Get interface from class (without instance)
/// Printable *classIf = objClassIf(Document, Printable);
/// @endcode
///
/// # Class Hierarchy
///
/// Classes support single inheritance. Child classes inherit and can override parent methods.
/// Method wrappers use the full class name in lowercase:
///
/// @code
/// class SpecialDocument extends Document {
///     int priority;
///     factory create(string title, int priority);
/// }
///
/// // Usage - class name is fully lowercase in wrapper
/// SpecialDocument *special = specialdocumentCreate(_S"Important", 10);
///
/// // Optionally override the prefix with methodprefix annotation:
/// [methodprefix sdoc]
/// class SpecialDocument extends Document {
///     // ...
/// }
/// // Now methods use: sdocCreate()
/// @endcode
///
/// # Dynamic Casting
///
/// Use `objDynCast()` to safely cast within the class hierarchy:
///
/// @code
/// ObjInst *baseObj = getObject();
/// Document *doc = objDynCast(Document, baseObj);
/// if (doc) {
///     // Safe to use as Document
/// }
/// @endcode
///
/// # Weak References
///
/// Weak references allow observing objects without ownership:
///
/// @code
/// Document *doc = documentCreate(_S"Title");
/// Weak(Document) *weak = objGetWeak(Document, doc);
///
/// // Later, try to acquire (returns NULL if object was destroyed)
/// Document *doc2 = objAcquireFromWeak(Document, weak);
/// if (doc2) {
///     // Object still exists
///     objRelease(&doc2);
/// }
///
/// objDestroyWeak(&weak);
/// objRelease(&doc);
/// @endcode
///
/// # Naming Conventions
///
/// **Interfaces:**
/// - `MyInterface` - Interface type name
/// - `MyInterface_tmpl` - Interface template object
///
/// **Classes:**
/// - `MyClass` - Class instance type name
/// - `MyClass_clsinfo` - Class metadata structure
/// - `myClassCreate()` - Typical factory wrapper (from `factory create();` in .cxh)
/// - Factory methods can have other names besides `create` as defined in .cxh file
///
/// **Interface Implementations:**
/// - `MyClass_MyInterface` - Function table for this class's implementation
///
/// PascalCase is preferred for classes and interfaces, but not enforced.

#include <cx/obj/objclass.h>
#include <cx/obj/objiface.h>
#include <cx/obj/objimpl.h>

/// @}  // end of obj group
