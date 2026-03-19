# CX Framework - Agent Instructions

CX (C, eXtended) is a C utility framework for cross-platform development. It provides copy-on-write strings, type-safe generic containers, a runtime type system (`stype`), an object system with interfaces and reference counting, threading primitives, filesystem abstraction, and more — all in pure C with static linking and no external runtime dependencies.

## Build System

### CMake Presets
```bash
cmake --preset msvc-dev       # Windows MSVC (Dev build)
cmake --preset gcc-debug      # Unix GCC (Debug build)
cmake --build build/msvc-dev
```

### Build Types
- `Debug` — Full debug, assertions enabled
- `Dev` — Optimized with debug info (default for development)
- `DevNoOpt` — Debug without optimization
- `Release` — Full optimization

### Build Options
- `CX_LUA` — Lua scripting engine (default ON at top level)
- `CX_TLS` — mbedTLS cryptography (default ON)
- `CX_MIMALLOC` — mimalloc allocator (default ON)
- `CX_XP_COMPAT` — Windows XP compatibility (Windows only)

### Testing
Custom test runner: `test_runner <testname> <subtest>` (e.g., `test_runner objtest iface`).

Tests are individual functions returning `int` (0 = success). Each test file exports a `TEST_FUNCS[]` array. Add tests in `tests/CMakeLists.txt` via `create_test_sourcelist()` and `add_test()`.

### Project Layout
```
cx/              — Core framework source
cx/include/cx/   — Public aggregate headers (users include these)
cx/string/       — String implementation
cx/container/    — Containers (sarray, hashtable)
cx/stype/        — Runtime type system
cx/obj/          — Object system
cx/thread/       — Threading primitives
cx/platform/     — Platform abstraction
cxautogen/       — Code generation tool for .cxh IDL files
cxlua/           — Lua integration (optional)
cxtls/           — TLS/cryptography (optional)
tests/           — Test suite
3rdparty/        — lua, mbedtls, mimalloc, pcre2
cmake/           — Build system helpers
```

## Coding Style

### Formatting
- **4-space indentation**, no tabs
- **Allman braces for function definitions** (opening brace on its own line)
- **K&R braces for control structures** (`if`, `for`, `while`, `switch`, `else`) — opening brace on same line
- One space after keywords (`if`, `for`, `switch`), no space between function name and `(`
- One space around binary operators

```c
static uint32 npow2(uint32 val)
{
    for (uint32 i = 0; i < 32; i++) {
        if (((uint32)1 << i) >= val)
            return 1 << i;
    }
    return 16;
}
```

### Naming Conventions
| Element | Convention | Examples |
|---------|-----------|----------|
| Public functions | Module prefix + camelCase | `strLen()`, `htInsert()`, `saPush()`, `objRelease()` |
| Internal functions | Leading underscore + camelCase | `_strFlatten()`, `_htInit()`, `_objDestroy()` |
| Types/Classes/Interfaces | PascalCase | `ObjInst`, `HashTableHeader`, `Sortable` |
| Opaque handle typedefs | lowercase | `string`, `hashtable`, `closure`, `sarray` |
| Declared array types | `sa_` prefix | `sa_int32`, `sa_string` |
| Macros (constant-like) | SCREAMING_CASE | `HT_SLOTS_PER_CHUNK`, `STR_LEN_MASK` |
| Macros (function-like) | camelCase (matching API) | `stType()`, `saInit()`, `htSize()` |
| Enum values | SCREAMING_CASE with prefix | `STCLASS_OPAQUE`, `SA_Sorted` |
| Flag parameters | `flags_t` (typedef for `uint32`) | All flag arguments use `flags_t` |

### Include Guards
Always `#pragma once` — never use `#ifndef` include guards.

### C++ Compatibility
All headers wrap declarations in `CX_C_BEGIN` / `CX_C_END` (from `cx/platform/cpp.h`).

### Inlining
Use `_meta_inline` instead of `static inline` for cross-platform forced inlining.

### Assertions
Use `devAssert()` / `devAssertMsg()` (debug builds) and `relAssert()` / `relAssertMsg()` (all builds). Never use standard `assert()`.

### SAL Annotations
Function signatures use Microsoft SAL-style annotations for static analysis and documentation. These are required on public API functions and encouraged on internal functions:

**Parameter direction:**
- `_In_` — input (read-only)
- `_Out_` — output (written to)
- `_Inout_` — modified in place
- `_In_opt_` / `_Out_opt_` / `_Inout_opt_` — may be NULL

**Pointer validity:**
- `_Nullable` — pointer may be NULL
- `_Nonnull` — pointer must not be NULL
- `_Pre_notnull_` / `_Post_null_` / `_Post_invalid_` — pre/post conditions

**Return values:**
- `_Ret_valid_` — always returns valid pointer
- `_Ret_maybenull_` — may return NULL
- `_Check_return_` — caller must check return value

**Function properties:**
- `_Pure` — no side effects
- `_Success_(return)` — indicates success condition

**Conditional annotations:**
- `_When_(condition, annotation)` — annotation applies conditionally
- `_Post_equal_to_(value)` — return equals value under condition

**Implementation files:** Use `_Use_decl_annotations_` on function definitions to inherit annotations from the header declaration rather than repeating them.

Example:
```c
// Header:
_When_(s == NULL, _Post_equal_to_(0)) _Pure uint32 strLen(_In_opt_ strref s);

// Implementation:
_Use_decl_annotations_
uint32 strLen(strref s)
{
    ...
}
```

## Memory Management

### CRITICAL: Never use `malloc`/`free` — always use the xalloc system

```c
MyStruct *data = xaAlloc(sizeof(MyStruct));      // Basic allocation
MyStruct *data = xaAllocStruct(MyStruct);         // Convenience macro
uint8 *buffer = xaAlloc(1024, XA_Zero);           // Zero-filled
void *aligned = xaAlloc(4096, XA_Align(6));       // 64-byte aligned
void *data = xaAlloc(huge_size, XA_Opt);          // May return NULL
xaResize(&buffer, new_size);                      // Resize (pointer-to-pointer)
xaFree(data);                                     // Free
xaDestroy(&data);                                 // Free + set to NULL (preferred)
```

### String Lifecycle
- Always initialize to `0`: `string s = 0;`
- Use `strDestroy(&s)` to free (sets to NULL)
- NULL strings are treated as empty throughout the API
- String literals: `_SL("literal")` (with embedded length), `_S"literal"` (prefix form), `STR_CONST(name, "value")` (named constants)
- See the Handle Paradigm section for the `string` / `strref` / `strhandle` type distinctions

### Object and Container Lifecycle
See the Resource Lifecycle Patterns section under API Design Patterns for the full Create/Acquire/Release, Create/Destroy, and Init/Destroy patterns with examples.

Quick reference:

- Objects: `XxxCreate()` → `objAcquire()` / `objRelease(&obj)` — weak refs: `objGetWeak()` / `objAcquireFromWeak()`
- Containers: `saInit(&arr, int32, 16)` / `saDestroy(&arr)`, `htInit(&ht, string, int32, 16)` / `htDestroy(&ht)`

## Documentation Style

This is the most critical section. CX uses Doxygen for API documentation, with specific conventions for IDE tooltip compatibility.

### Comment Format
- **Always use `///`** for documentation comments (Doxygen triple-slash)
- **Never use `/** */`** for inline API documentation
- Use `//` (regular comments) for internal notes and undocumented functions
- Use `@param`, `@return`, `@code`/`@endcode`, `@brief` — never backslash variants (`\param`, `\return`)

### Documenting Macros vs Functions

**For macros** — include a synthetic prototype on the first line, as if the macro were a C function. This shows up in IDE tooltips since the `#define` itself is not informative:

```c
/// void strInit(string *o);
///
/// Initializes a string variable to empty/NULL
///
/// @param o Pointer to string variable to initialize
///
/// Example:
/// @code
///   string s;
///   strInit(&s);
/// @endcode
#define strInit(o) *(o) = NULL
```

**For native C functions** — do NOT include a synthetic prototype. The actual function signature is already visible to the IDE:

```c
/// Duplicates a string using copy-on-write optimization
///
/// Creates a reference to the source string when possible, avoiding copying.
///
/// @param o Pointer to output string variable
/// @param s String to duplicate (NULL creates empty string)
///
/// Example:
/// @code
///   string s1 = 0;
///   strDup(&s1, _SL("hello"));
/// @endcode
void strDup(_Inout_ strhandle o, _In_opt_ strref s);
```

### Documentation Structure for a Function/Macro

1. Brief one-line description (no `@brief` tag needed — Doxygen uses the first line)
2. Blank `///` line
3. Extended description paragraph(s) if needed
4. Blank `///` line
5. `@param` entries for each parameter
6. `@return` description
7. Blank `///` line
8. `Example:` label followed by `@code` / `@endcode` block

### Special Cases for Synthetic Prototypes
- For functions taking `stype` runtime type parameters via macros, omit the stype from the prototype (it's a descriptor processed by the macro, not a user-visible parameter)
- For functions with optional `flags_t` parameters through macros, show as `[flags]` in the prototype

### Internal / Wrapped Functions
The Doxygen configuration uses `HIDE_UNDOC_MEMBERS`. Internal functions or functions that are completely wrapped by public macros should have **no `///` documentation** — use regular `//` comments only. This prevents them from appearing in generated docs.

```c
// Internal function - use htSize() macro instead
_Ret_valid_ _meta_inline HashTableHeader* _htHdr(_In_ hashtable ref)
{
    return HTABLE_HDR(ref);
}
```

### Group Hierarchy with @defgroup

Documentation is organized hierarchically using Doxygen groups. This is the primary organizational structure.

**Pattern for top-level module (in aggregate header under `cx/include/cx/`):**
```c
/// @file string.h
/// @brief Copy-on-write strings with automatic memory management

/// @defgroup string Strings
/// @{
/// High-level string abstraction with automatic memory management...

/// @defgroup string_overview Overview
/// @ingroup string
/// @{
///
/// @section string_types String Types
/// ...detailed narrative with @code examples...
///
/// @}  // end of string_overview group

/// @}  // end of string group
```

**Pattern for implementation header (in module subdirectory):**
```c
/// @file strbase.h
/// @brief Core string types and fundamental operations

/// @defgroup string_base Core String Functions
/// @ingroup string
/// @{

// ... types and function declarations ...

/// @}  // end of core string functions group
```

**Critical rules for groups:**
- Place `@{` immediately after the `@defgroup` line (or after its description)
- Place `@}` right after the last item that belongs in the group
- Failure to close groups promptly causes unrelated functions to be pulled in
- Use `@ingroup parent` to nest groups under a parent
- Each major feature has a `@defgroup` in its aggregate include header, while individual implementation headers have their own `@defgroup` that is `@ingroup` the parent module

### File-Level Documentation
Every header file should have:
```c
/// @file filename.h
/// @brief One-line description of file contents
```

### Narrative Documentation Style
Module overview groups (`*_overview`) contain extensive narrative documentation with:
- `@section` tags for major topics
- Bold text with `**text**` for emphasis
- Bullet lists with `-` prefix
- `@code` / `@endcode` blocks for examples throughout
- Explanations of conceptual models, lifecycle, thread safety, and optimization details

The narrative should explain the "mental model" for the API — e.g., strings behave like immutable values (COW is transparent), containers manage element lifetime through stype operations, objects use reference counting with weak references for cycle breaking.

## API Design Patterns

### Resource Lifecycle Patterns

CX uses three distinct lifecycle patterns. Choosing the right one depends on whether the resource is reference-counted, heap-allocated, or stack-allocated.

#### Create / Acquire / Release — reference-counted objects

Used for all objects derived from `ObjInst` (via the cxautogen class system) and any other reference-counted resource. Full `ObjInst`-based objects are thread-safe through atomic refcount operations. Other reference-counted types such as stream buffers (`StreamBuffer`) use the same Acquire/Release interface but operate on a cooperative single-threaded model — much of their reference management is implicit through provider/consumer registration rather than explicit `Acquire`/`Release` calls.

- **`XxxCreate(...)`** — allocates on the heap, returns with refcount = 1
- **`objAcquire(obj)`** — increments refcount, returns the same pointer
- **`objRelease(&obj)`** — decrements refcount; at zero, destroys and sets the pointer to NULL

```c
Document *doc = documentCreate(_SL("My Document"));  // refcount = 1
Document *ref = objAcquire(doc);                     // refcount = 2
objRelease(&doc);                                    // refcount = 1, doc = NULL
objRelease(&ref);                                    // refcount = 0, destroyed
```

Ownership rules: the caller of `Create` owns one reference and is responsible for `Release`. Passing an object as a function argument does **not** transfer ownership — the callee must `Acquire` if it needs to retain the object.

#### Create / Destroy — non-reference-counted heap objects

Used for heap-allocated objects that do not need sharing (e.g., parser state, one-shot buffers). One owner; no refcounting.

- **`XxxCreate(...)`** — allocates the struct on the heap, returns pointer
- **`XxxDestroy(&obj)`** — frees all internal resources and the struct itself, sets pointer to NULL

```c
Buffer buf = bufCreate(256);
memcpy(buf->data, data, len);
buf->len = len;
bufResize(&buf, 512);     // reallocates; buf pointer may change
bufDestroy(&buf);         // buf = NULL
```

Destroy takes a pointer-to-pointer so it can NULL the caller's handle, preventing accidental use-after-free.

#### Init / Destroy — stack-allocated (or embedded) objects

Used for containers and value-type aggregates whose storage is managed by the caller — typically stack locals or struct members. Init does **not** allocate the struct itself; Destroy does **not** free it. Only internal resources (heap buffers, element storage, etc.) are managed.

- **`xxxInit(&obj, ...)`** — initializes an uninitialized struct; takes capacity hints and type descriptors
- **`xxxDestroy(&obj)`** — releases all internal resources via stype operations; the struct remains in place

```c
sa_string arr;
saInit(&arr, string, 16);       // allocates internal element buffer
saPush(&arr, string, _SL("x"));
saDestroy(&arr);                // frees element buffer; arr struct is still on the stack

hashtable ht;
htInit(&ht, string, int32, 16);
htInsert(&ht, string, _SL("key"), int32, 42);
htDestroy(&ht);
```

Do **not** call Init on a live object without calling Destroy first — this leaks internal resources.

### Handle Paradigm

CX uses a consistent convention to distinguish read-only access from mutation:

- **Pass the handle by value** for operations that do not modify the object.
- **Pass a pointer to the handle (`&handle`)** for operations that may modify, replace, or destroy the object.

```c
uint32 len = strLen(s);          // s passed by value — read-only
strAppend(&s, _SL(" world"));    // &s passed — modifies the string in place
strDestroy(&s);                  // &s passed — destroys and NULLs the handle
```

This matters especially for containers: an append or insert may reallocate internal storage, changing the object's memory address. Always pass `&arr` (not `arr`) when the container can grow.

```c
int32 val = arr.a[0];                // direct member access — read, no function call needed
saPush(&arr, int32, 99);             // &arr — may reallocate
saDestroy(&arr);                     // &arr — cleans up
```

Note: `sarray` is a typed struct rather than an opaque pointer, so elements are accessed directly through the `.a` member of the typed array (e.g., `arr.a[i]`). There is no `saGet` function.

String types make the distinction explicit at the type level:

- `strref` — read-only borrowed reference (analogous to `const char *`); pass by value
- `string` — owning handle; pass by value for reads, `&s` for writes
- `strhandle` (`string *`) — pointer to an owning handle; used as output or in-out parameter

### Avoiding `const`

`const` is used sparingly in CX and is **not** the primary mechanism for expressing read-only intent. The handle paradigm (value vs. `&`) and SAL annotations (`_In_`, `_Inout_`, `_Out_`) carry that information instead.

The main reasons:

1. **COW semantics require write access.** `strDup` and similar operations physically increment a refcount in the string header, so the operation is not truly const even when the visible string value is unchanged. Propagating `const` through such operations would require pervasive casts, defeating the purpose.
2. **The handle model is more expressive.** A `strref` parameter already communicates "read-only borrowed reference" more precisely than `const string` would, and without infecting the type system.
3. **Interop with C strings.** `const char *` is used where necessary to match C stdlib and platform API signatures, and nowhere else.

In practice: if a parameter is `_In_` or `strref`, treat it as read-only regardless of whether `const` appears. If it is `_Inout_` or `_Out_` (or takes `&`), it will be written to.

### Output Parameters and Parameter Ordering

Output parameters come **first** (leftmost). This mirrors assignment syntax — in `someFunc(&x, y)` the destination is on the left, just as in `x = y`. Existing values at an output location are destroyed before the new value is assigned.

```c
void strDup(_Inout_ strhandle o, _In_opt_ strref s);   // dest first, src second
void htInit(hashtable *out, ...);                       // output container first
```

For type-safe generic calls, the **type name immediately precedes the value it describes**:

```c
saPush(&arr, int32, 42);                           // type then value
htInsert(&ht, string, _SL("key"), int32, 42);      // key-type key value-type value
```

### Lazy Initialization
One-time initialization uses the `lazyInit()` pattern:
```c
static LazyInitState initState;
static void doInit(void* unused) { ... }
// In function:
lazyInit(&initState, doInit, NULL);
```

### Macro-Generated Specializations
Performance-critical operations use macro families to generate type-specific code:
```c
#define STCMP_GEN(type) \
    static intptr stCmp_##type(stype st, stgeneric gen1, stgeneric gen2, uint32 flags) { ... }
STCMP_GEN(int8)
STCMP_GEN(int16)
// ...
```

Dispatch tables (256-element arrays indexed by stype ID) route to these specializations at runtime.

## Code Generation System (cxautogen)

`.cxh` files define interfaces and classes in an IDL:
```c
interface Printable {
    void print();
}

class Document implements Printable {
    string title;
    factory create(string title);
}
```

Build integration: `add_cxautogen(myfile.cxh)` in CMakeLists.txt generates:
- `myfile.h` — Public API (**never edit directly**)
- `myfile.c` — Implementation stubs to fill in
- `myfile.auto.inc` — Generated boilerplate

## Common Tasks

### Adding a New Class
1. Create `myclass.cxh` with interface/class definitions
2. Add `add_cxautogen(myclass.cxh)` to the relevant CMakeLists.txt
3. Run cmake to generate files
4. Implement methods in the generated `myclass.c`
5. Include `myclass.h` from consuming code

### Adding Tests
1. Create `mytest.c` with test functions (return `int`, 0 = success)
2. Add to `create_test_sourcelist()` in `tests/CMakeLists.txt`
3. Add test cases: `add_test(NAME "category: Test" COMMAND test_runner mytest testname)`

### Cross-Platform Code
- Platform: `CX_PLATFORM_IS_WINDOWS`, `CX_PLATFORM_IS_UNIX`, `CX_PLATFORM_IS_WASM`
- Compiler: `CX_COMPILER_IS_MSVC`, `CX_COMPILER_IS_CLANG`, `CX_COMPILER_IS_GNU`
- Use abstractions in `cx/platform/` rather than direct OS APIs

### Header Organization
- Include public aggregate headers: `#include <cx/string.h>`, `#include <cx/obj.h>`
- Never include implementation headers (e.g., `cx/string/strbase.h`) from outside the module
- Implementation files use `#include "module_private.h"` for internal access
