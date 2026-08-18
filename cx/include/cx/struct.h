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
/// - **Automatic Serialization**: Any struct can be written to and read back from JSON, cx's
///   compact binary format, or an SSD tree through the @ref serialize module, with no
///   per-struct code to write. Individual members can be excluded with `[noserialize]`, or
///   renamed on the wire with `[serializeas]`
/// - **Flexible Allocation**: Structs may be stack-allocated, statically allocated,
///   heap-allocated, or stored inline in flat arrays
/// - **SType Integration**: A struct's generated type works as an ordinary stype token, so
///   structs participate fully in CX's generic containers (`sarray`, `hashtable`, ...)
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
///     [serializeas server-name] string hostname;   // written as "server-name"
///     [default 8080] uint16 port;                  // starting value, see below
///     [noserialize] string cachedPassword;         // excluded from serialization
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
/// `[default value]` gives a member a starting value other than zero: `structInit()` and
/// `structCreate()` copy it in after the zero-fill, so `cfg.port` above comes out `8080`
/// rather than `0`. It also feeds serialization — see the Serialization section below.
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
/// structInit(Point, &p);
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
/// | `structInit(Type, ptr)`               | No            | Set metadata pointer; zero-fill members      |
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
/// # SType Descriptors
///
/// A generated struct type works as a stype token exactly like any built-in type: pass the
/// struct's name wherever a type argument is expected, including as a container element type.
///
/// @code
/// sa_Point pts;
/// saInit(&pts, Point, 64);
///
/// Point p = { 0 };
/// structInit(Point, &p);
/// p.x = 1.0;  p.y = 2.0;
/// saPush(&pts, Point, p);
/// @endcode
///
/// Heap-allocated struct *pointers* use a second, separate token: `structp`. It takes no type
/// parameter — every heap struct carries its own `StructInfo*` at offset 0, so `structp` reads
/// the concrete type back from the pointer itself. This makes containers of `structp` values
/// heterogeneous automatically, and whatever a slot holds, the container destroys it (via
/// `structDestroy`) when the slot is overwritten or the container itself is destroyed.
///
/// `htInsert()` copies the struct in, like it does for any type — the pointer you passed is
/// still yours to destroy. To move an existing heap struct into the container without a copy,
/// steal it with `htInsertC()` instead:
///
/// @code
/// hashtable ht;
/// htInit(&ht, string, structp, 16);
///
/// Config *cfg = structCreate(Config);
/// htInsertC(&ht, string, _SL("main"), structp, &cfg);    // moves cfg into ht; cfg is now NULL
///
/// Point *pt = structCreate(Point);
/// htInsertC(&ht, string, _SL("origin"), structp, &pt);   // a different struct type, same table
/// @endcode
///
/// # Slots That Can Hold More Than One Struct Type
///
/// A *struct set* is a named, sorted list of struct types — the declared vocabulary for a
/// slot that may hold any one of them. Declare it with `structset` in a `.cxh` file and type a
/// `structp` member by it:
///
/// @code
/// struct Circle { struct[Point] center; float64 radius; }
/// struct Rect   { struct[Point] topLeft; struct[Point] bottomRight; }
///
/// structset ShapeSet { Circle, Rect }
///
/// struct Drawing {
///     structp[ShapeSet] shape;   // any struct in ShapeSet
/// }
/// @endcode
///
/// `structp[ShapeSet]` behaves like bare `structp` at runtime — one heterogeneous pointer,
/// dispatched through the pointee's own `StructInfo*`. Serialization is where the set matters:
/// writing a struct that isn't in `ShapeSet` fails, and reading a `Drawing` resolves the
/// concrete shape by name against `ShapeSet` with nothing to configure on the reader. Look a
/// type up directly with `structSetFind()`.
///
/// # Serialization
///
/// Structs serialize through the generic @ref serialize module, using the struct's generated
/// schema (`stExt(StructName)`) and type (`stType(StructName)`) — there is no struct-specific
/// serialization code, the same traverser and backends handle every stype-described value.
///
/// @code
/// Config *cfg = structCreate(Config);
/// cfg->port = 8080;
///
/// string json = 0;
/// StreamBuffer *sb = sbufStrCreatePush(&json, 4096);
/// SerWriter *w = serJsonWriterCreate(sb, SER_JSON_Pretty);
/// serWrite(w, Config, *cfg);
/// serWriterFinish(w);
/// serWriterDestroy(&w);
/// sbufRelease(&sb);
///
/// Config *cfg2 = structCreate(Config);
/// sb = sbufCreate(4096);
/// sbufStrPRegisterPull(sb, json);
/// SerReader *r = serJsonReaderCreate(sb, 0);
/// serRead(r, Config, cfg2);
/// serReaderDestroy(&r);
/// sbufRelease(&sb);
///
/// structDestroy(&cfg);
/// structDestroy(&cfg2);
/// strDestroy(&json);
/// @endcode
///
/// The same calls work unchanged against the binary or SSD-tree backend — see @ref serialize
/// for the full read/write API, the available backends, and how object classes serialize.
///
/// A member declared `[default value]` is omitted from the document entirely when it still
/// holds that value, and a document that omits it reads back as that value — `SER_EmitDefaults`
/// writes it anyway. This is automatic once the member is annotated; there is nothing to opt
/// into at the call site.
///
/// `[noserialize]` drops a member from the wire entirely. `[serializeas X]` keeps the member
/// but changes the name it goes out under: the C identifier is unaffected, and `X` is the only
/// spelling a document may use for it.
///
/// # Naming Conventions
///
/// - `MyStruct`             — generated struct type
/// - `MyStruct_structinfo`  — the struct's `StructInfo`, including its member table
/// - `stType(MyStruct)`     — runtime stype descriptor; use directly as a type token
/// - `stExt(MyStruct)`      — schema descriptor, for @ref serialize
/// - `structp`              — runtime stype for any heap-allocated struct pointer

/// @}  // end of struct group

#include <cx/struct/struct.h>
