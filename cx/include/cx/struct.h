#pragma once

/// @file struct.h
/// @brief CX Struct System - Introspectable, serializable POD structures in C

// clang-format off
/// @defgroup struct Struct System
/// @{
///
/// The CX struct system provides plain-old-data (POD) C structures with runtime
/// type introspection, automatic serialization, and lifecycle management. Unlike
/// objects in the CX object system, structs are not reference counted, are not
/// polymorphic, and carry no vtable overhead — they are simply annotated C structs
/// with metadata automatically generated from `.cxh` definitions.
///
/// # Key Features
///
/// - **IDL Definition**: Structs are declared in `.cxh` files alongside classes and interfaces
/// - **Runtime Introspection**: Every struct carries a pointer to its `StructInfo` metadata,
///   accessible at runtime for generic code
/// - **Automatic Serialization**: Structs can be serialized to/from JSON or a compact binary
///   format; individual members can be excluded with the `[transient]` annotation
/// - **Flexible Allocation**: Structs may be stack-allocated, statically allocated,
///   heap-allocated, or stored inline in flat arrays
/// - **SType Integration**: Structs have their own `stype` descriptor so they participate fully
///   in CX generic containers and the object system
/// - **No Thread Safety**: Structs provide no synchronization; callers are responsible for
///   any concurrent access
///
/// # Defining Structs in .cxh Files
///
/// Structs are declared in `.cxh` files using the `struct` keyword:
///
/// @code
/// struct Point {
///     float64 x;
///     float64 y;
/// }
///
/// struct Config {
///     string hostname;
///     uint16 port;
///     [transient] string cachedPassword;   // excluded from serialization
/// }
/// @endcode
///
/// The build system (via `add_cxautogen()` in CMakeLists.txt) generates:
/// - `mystructs.h`        - Public API (type definitions, function declarations)
/// - `mystructs.c`        - Static `StructInfo` / `StructMemberDesc` tables
/// - `mystructs.auto.inc` - Internal boilerplate
///
/// **Never edit generated `.h` files directly** — modify the `.cxh` source instead.
///
/// # Memory Layout
///
/// Every generated struct type begins with a `StructBase` union as its first member:
///
/// @code
/// typedef struct Point {
///     union {
///         StructInfo* structinfo;
///         void*       _is_Struct;   // type-check marker
///     };
///     float64 x;
///     float64 y;
/// } Point;
/// @endcode
///
/// This overlay means a `Point*` is castable to `StructBase*` for generic handling,
/// and the `structinfo` pointer is always available at a known, zero offset.
///
/// # Allocation Strategies
///
/// Structs are not restricted to the heap:
///
/// @code
/// // Stack allocation — zero-init manually or via structInit()
/// Point p = { 0 };
/// structInit(&p, Point);
/// p.x = 1.0;
/// p.y = 2.0;
/// structDestroyMembers(&p);          // clean up members, struct itself is on the stack
///
/// // Heap allocation
/// Config *cfg = structCreate(Config);
/// cfg->port = 8080;
/// structDestroy(&cfg);               // destroys members and xaFree()s the pointer
///
/// // Flat array (inline storage, not pointer-to-struct) — manual memory management
/// Config *arr = xaAlloc(32 * sizeof(Config));
/// structInitMany(Config, &arr[0], 32);
/// // ... use arr ...
/// structDestroyMembersMany(&arr[0], 32);
/// xaFree(arr);
/// @endcode
///
/// # Lifecycle Functions
///
/// | Function                              | Frees memory? | Description |
/// |---------------------------------------|:-------------:|----------------------------------------------|
/// | `structInit(ptr, Type)`               | No            | Set metadata pointer; zero-fill members      |
/// | `structDestroyMembers(ptr)`           | No            | Destroy members via stype dtors              |
/// | `structDestroyMembersMany(ptr, n)`    | No            | Same, for a flat array of `n` structs        |
/// | `structDestroy(&ptr)`                 | Yes           | `structDestroyMembers` + `xaFree`; sets NULL |
///
/// Custom init and destroy hooks can be declared in the `.cxh` file (analogous to
/// object system `init()`/`destroy()`):
///
/// @code
/// struct Config {
///     string hostname;
///     uint16 port;
///     init();     // called after zero-fill and structinfo setup
///     destroy();  // called before automatic member cleanup
/// }
/// @endcode
///
/// # Serialization
///
/// Structs support round-trip serialization out of the box:
///
/// @code
/// Config *cfg = structCreate(Config);
/// cfg->hostname = strDup(NULL, _SL("localhost"));
/// cfg->port = 8080;
///
/// // JSON
/// string json = 0;
/// structToJson(&json, cfg);
/// structFromJson(cfg2, json);    // cfg2 must already be initialized
///
/// // Binary
/// Buffer buf = structToBytes(cfg);
/// structFromBytes(cfg3, buf);
/// @endcode
///
/// Members annotated `[transient]` in the `.cxh` file are silently skipped during
/// both serialization and deserialization.
///
/// # SType Descriptors
///
/// Structs integrate with the CX stype system through two complementary descriptors:
///
/// **`structptr`** — pointer-to-struct descriptor:
/// - The stored value is a heap-allocated `StructBase*`
/// - Because every heap-allocated struct carries its own `StructInfo*` at offset 0,
///   the stype requires **no type parameter** — the runtime inspects the pointer to
///   identify the concrete type. Heterogeneous containers are therefore supported
///   automatically: different struct types can coexist in the same container.
/// - The stype system manages the pointer (destructor calls `structDestroy`)
/// - Use this when storing struct pointers in containers or object members
///
/// **`struct`** — inline/embedded struct descriptor:
/// - Analogous to `opaque`: the value occupies its full storage footprint inline
/// - Must be paired with the concrete type to obtain size information
/// - Suitable for `sa_<Type>` flat arrays where structs live contiguously in memory
/// - Destructor calls `structDestroyMembers` (no free)
///
/// @code
/// // Pointer-to-struct in a hashtable (structptr stype, no type parameter)
/// hashtable ht;
/// htInit(&ht, string, structptr, 16);
/// Config *cfg = structCreate(Config);
/// htInsert(&ht, string, _SL("main"), structptr, cfg);
///
/// // Heterogeneous structs in the same hashtable -- works because StructInfo
/// // is read directly from the stored pointer
/// Point *pt = structCreate(Point);
/// htInsert(&ht, string, _SL("origin"), structptr, pt);
///
/// // Inline array of structs (struct stype, type parameter required)
/// sa_Point pts;
/// saInit(&pts, struct(Point), 64);
/// Point p = { 0 };
/// p.x = 1.0;  p.y = 2.0;
/// saPush(&pts, struct, p);
/// @endcode
///
/// # Naming Conventions
///
/// - `MyStruct`                    — generated struct type
/// - `MyStruct_structinfo`         — static `StructInfo` instance
/// - `MyStruct_members[]`          — static `StructMemberDesc` array
/// - `structptr`                   — runtime stype for any heap-allocated struct pointer
/// - `struct(MyStruct)`            — runtime stype for inline MyStruct storage

/// @defgroup struct_impl Implementation Notes
/// @ingroup struct
/// @{
///
/// @note These notes are intended for the initial implementation phase and should be
/// relocated or removed once the implementation is complete.
///
/// @section struct_impl_codegen Code Generation (.cxh → .c/.h)
///
/// The cxautogen tool needs a new top-level production for `struct`:
///
/// @code
/// struct Point {
///     float64 x;
///     float64 y;
///     [transient] string debugLabel;
///     init();
///     destroy();
/// }
/// @endcode
///
/// **Generated `.h`** should emit:
/// - The concrete struct typedef (with `StructBase` union as first member)
/// - `extern StructInfo Point_structinfo;`
/// - Declarations for `pointInit(Point*)`, `pointDestroyMembers(Point*)`, etc.
/// - A `stvar`-compatible helper macro: `structSType(Point)` → the stype for `Point*`
///
/// **Generated `.c`** should emit:
/// - Static `StructMemberDesc Point_members[]` — one entry per non-`[transient]` member
///   (transient members MUST also be listed but flagged so generic code can skip them while
///   still computing correct offsets for size checking)
/// - Static `StructInfo Point_structinfo = { sizeof(Point), ARRAY_SIZE(Point_members),
/// Point_members, ... }`
/// - Stub implementations of `Point_init` and `Point_destroy` if declared in `.cxh`
///
/// **`StructMemberDesc.flags`** usage (to be defined in `struct.h`):
/// - Bit 0: `STRUCT_MEMBER_TRANSIENT` — skip during serialization/deserialization
/// - Additional bits reserved for future use (e.g., `READONLY`, `DEPRECATED`)
///
/// @section struct_impl_stype SType Integration
///
/// Two new type IDs are required (add to `STYPE_ID` enum in `stype.h`):
/// - `STypeId_structptr` — pointer-to-StructBase (object-like; `STypeFlag_Object` set)
/// - `STypeId_struct`    — inline struct (`STypeFlag_PassPtr` set; size comes from `StructInfo`)
///
/// `structptr` (no macro parameter):
/// - Expands to a fixed `stype` constant: id=`STypeId_structptr`, flags=`STypeFlag_Object`,
///   size=`sizeof(void*)` (pointer width; identical for all struct types).
/// - Runtime operations (dtor, copy, cmp, hash) read `StructInfo*` from offset 0 of the
///   stored pointer, so no type parameter is needed and heterogeneous containers work
///   without any special handling.
/// - The associated dtor calls `structDestroy`.
///
/// `struct(T)` macro:
/// - Variable element size means the standard 32-bit encoding is insufficient on its own.
///   Preferred approach: `struct(T)` is a **compound expression** that builds a custom
///   `stype` on the fly using `_stype_mkcustom` with `sizeof(T)` baked in at the call
///   site — avoids global mutable state and lets the compiler constant-fold it.
/// - Returns a `stype` with id=`STypeId_struct`, flags=`STypeFlag_PassPtr`,
///   size=`sizeof(T)` encoded in bits 16–31.
/// - The associated dtor calls `structDestroyMembers` (no free).
///
/// The ops dispatch table (`STypeOps`) needs entries for both descriptors:
/// - **dtor**: as above
/// - **copy**: for `structptr`, duplicate the heap struct (deep copy via member stype copy ops);
///   for `struct`, deep-copy inline (destination must already be allocated).
/// - **cmp**: fallback to memcmp for POD-only structs; otherwise field-by-field via stype cmp.
/// - **hash**: field-by-field hash accumulation using stype hash ops.
/// - **convert**: not supported initially (return error).
///
/// @section struct_impl_lifecycle Lifecycle Implementation
///
/// `structInit(ptr, Type)`:
/// - Sets `ptr->structinfo = &Type_structinfo`
/// - Calls the custom `init` hook if present (after zero-fill; generated alloc functions
///   zero-fill via `xaAlloc(..., XA_Zero)`)
///
/// `structDestroyMembers(ptr)`:
/// - Calls the custom `destroy` hook if present (before member cleanup)
/// - Iterates `structinfo->members[i]` and calls stype dtor on each member by offset
/// - Resets the `structinfo` pointer to NULL as a use-after-destroy guard
///
/// `structDestroyMembersMany(ptr, Type, n)`:
/// - Walks the flat array by `sizeof(Type)` strides, calling `structDestroyMembers` on each
///
/// `structDestroy(&ptr)`:
/// - `structDestroyMembers(*ptr)` then `xaDestroy(ptr)`
///
/// @section struct_impl_serialization Serialization Architecture
///
/// Serialization lives in a new module (e.g., `cx/struct/structser.c`) and operates
/// entirely via the `StructInfo`/`StructMemberDesc` metadata — no generated code required.
///
/// **JSON** (using the existing CX JSON/format infrastructure):
/// - `structToJson(string *out, void *s)`: walk members, format each with its stype name
///   and value; skip `STRUCT_MEMBER_TRANSIENT` members
/// - `structFromJson(void *s, strref json)`: parse JSON object, match keys to member names,
///   convert JSON values using stype convert ops
///
/// **Binary** format (length-prefixed, field-ID-tagged for forward compatibility):
/// - Each non-transient member is written as: [uint16 member_index][uint16 size][data]
/// - Unknown member indices on read are skipped (forward compatibility)
/// - `structToBytes(void *s)` → `Buffer`
/// - `structFromBytes(void *s, Buffer buf)` → `bool`
///
/// @section struct_impl_cxautogen_changes cxautogen Parser Changes
///
/// 1. Add `TOK_STRUCT` keyword to the lexer.
/// 2. Add `parseStruct()` production (similar to `parseClass()` but simpler — no vtable,
///    no interface list, no `extends`).
/// 3. Annotations to handle: `[transient]`, `[noinit]`, custom `init()`, `destroy()`.
/// 4. Emit calls into existing `header.c` / `impl.c` / `stub.c` infrastructure, adding
///    struct-specific emitters.
///
/// @section struct_impl_dynamic_structsets Dynamic Struct Sets
///
/// A *struct set* is a named registry that maps string names to `StructInfo*` pointers.
/// It enables generic deserialization code to reconstruct the correct concrete struct type
/// from a `"type"` attribute embedded in the JSON or binary stream, without the caller
/// needing to hard-code a switch on type names.
///
/// **Proposed `.cxh` syntax** — new `structset` top-level keyword:
///
/// @code
/// // shapes.cxh
/// struct Point   { float64 x; float64 y; }
/// struct Circle  { struct:Point center; float64 radius; }
/// struct Rect    { struct:Point topLeft; struct:Point bottomRight; }
///
/// structset ShapeSet {
///     Point,
///     Circle,
///     Rect
/// }
/// @endcode
///
/// **Generated code** (in `shapes.h` / `shapes.c`):
///
/// @code
/// // shapes.h
/// typedef struct StructSetEntry {
///     strref        name;       // interned string, no allocation
///     StructInfo   *info;
/// } StructSetEntry;
///
/// typedef struct StructSet {
///     int            nentries;
///     StructSetEntry entries[];  // sorted by name for binary search
/// } StructSet;
///
/// extern StructSet ShapeSet_structset;
/// StructInfo *structSetFind(StructSet *ss, strref name);
/// @endcode
///
/// **Serialization integration**:
///   When serializing a `structptr` stype value through a struct set context, a `"type"` key
///   is prepended to the JSON object (or written as a leading field in binary) containing
///   the registered name.
/// - `structFromJsonAny(StructSet *ss, strref json)` → allocates and returns the correct
///   concrete struct type by looking up the `"type"` field first, then deserializing into it.
/// - The struct set is optional; plain `structFromJson` still works when the target type
///   is known statically.
///
/// **Open design questions for struct sets**:
/// - Name mapping: use the bare struct name (`"Point"`) or a user-overridable alias via
///   `[name "point"]` annotation on the struct set entry?
/// - Should `structset` entries be allowed to span multiple `.cxh` files (i.e., forward
///   declarations in a set, then defined elsewhere)?
///
/// @section struct_impl_open_questions Open Questions
///
/// - Should `struct` sarrays automatically call `structInit` on push (to set `structinfo`)?
///   Most likely yes — the stype copy op should handle this, but needs design confirmation.
/// - Nested structs default to heap-allocated pointers using `struct:Name` member syntax in
///   `.cxh` (generates a `Name*` field, automatically allocated/freed by lifecycle functions).
///   An `[embedded]` annotation could indicate inline storage, but this complicates parent
///   struct init/destroy and is deferred for now.
/// - Should structs support a `clone()` helper analogous to `objAcquire` for the pointer case?
///
/// @}  // end of struct_impl group

/// @}  // end of struct group

#include <cx/struct/struct.h>
