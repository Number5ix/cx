# CX Framework - AI Coding Agent Instructions

## Project Overview
CX is a C utility framework for cross-platform development focusing on programmer convenience and high-level constructs in pure C. It provides a runtime type system, object-oriented programming, copy-on-write strings, generic containers, and platform abstraction layers.

**Core Design Principles:**
- Minimize boilerplate through code generation and macros
- Static linking with no external runtime dependencies
- Memory efficiency through reference counting and lazy allocation
- Type-safe generic programming without template bloat

## Architecture & Key Components

### 1. Code Generation System (`cxautogen`)
**Critical**: CX uses a custom IDL (Interface Definition Language) for object-oriented code.

**`.cxh` files** define interfaces and classes:
```c
interface TestIf1 {
    int testfunc();
}

class TestCls1 implements TestIf1 {
    int data;
    factory create();
}
```

**Build Integration**: `add_cxautogen()` in CMakeLists.txt generates `.h`, `.c`, and `.auto.inc` files:
```cmake
add_cxautogen(objtestobj.cxh)
```

Generated files include:
- `objtestobj.h` - Public API
- `objtestobj.c` - Implementation stubs to fill in
- `objtestobj.auto.inc` - Generated boilerplate

**Never edit generated `.h` files directly** - modify the `.cxh` source instead.

### 2. String Library (`cx/string/`)
Copy-on-write strings with automatic rope conversion for long strings.

**Always initialize strings to NULL:**
```c
string s = 0;  // REQUIRED - never use uninitialized strings
```

**Core pattern - strings are opaque handles:**
```c
string str = 0;
strDup(&str, _SL("hello"));      // Copy/assign
strAppend(&str, _SL(" world"));  // Modify in place (COW semantics)
strDestroy(&str);                // Cleanup
```

**String literals**: `_SL("literal")` creates a static string with a compile-time embedded length (GCC/Clang; falls back to runtime `strlen` on MSVC). Use `_S"literal"` for the simpler prefix form. For named constants at file or function scope, use `STR_CONST(name, "value")`.

### 3. Generic Containers

#### SArray (Dynamic Arrays)
Type-safe dynamic arrays declared per-type:

```c
sa_int32 arr;
saInit(&arr, int32, 16);       // Create with initial capacity
saPush(&arr, int32, 42);       // Type-checked push
int32 val = arr.a[0];          // Direct array access
saDestroy(&arr);               // Cleanup
```

**Sorted arrays**: Use `SA_Sorted` flag for O(log n) search with `saFind()`.

#### Hashtable
Generic key-value maps:

```c
hashtable ht;
htInit(&ht, string, int32, 16);
htInsert(&ht, string, _SL("key"), int32, 42);
int32 val;

if (htFind(ht, string, _SL("key"), int32, &val) {
    // found, use val
}
// -- OR --
htelem elem = htFind(ht, string, _SL("key"), none, NULL);
if (elem) {
    val = hteVal(ht, int32, elem);  // can also use hteValPtr to obtain pointer to stored value
}

htDestroy(&ht);
```

**Key pattern**: Functions take type parameters for compile-time checking: `htInsert(&ht, keytype, key, valtype, value)`.

### 4. Object System (`cx/obj/`)
Lightweight OOP with interfaces, inheritance, and mixins.

**Core macros:**
- `objInstIf(obj, Interface)` - Get interface pointer
- `objRelease(&obj)` - Decrement refcount and destroy if zero
- `TestCls(ptr)` - Static cast to class type
- `objDynCast(ptr, TargetClass)` - Runtime type checking, returns NULL if not compatible

**Factory pattern**: Classes typically use `_create()` functions (generated from `factory create();` in `.cxh`).

### 5. Runtime Type System (`cx/stype/`)
`stype` is a 32-bit runtime type descriptor enabling generic programming.

**Common patterns:**
- `stCheckedArg(type, value)` - Type-safe argument passing; implicitly added by macro wrappers
- `stGetSize(stype)` - Get size of type at runtime
- `stvar(type, value)` - Create variant container, stored in the stvar type

### 6. Platform Abstraction (`cx/platform/`, `cx/thread/`)
Cross-platform APIs for:
- Threading: `thrCreate()`, `Mutex`, `RWLock`, `Event`
- Atomics: `atomicStore()`, `atomicFetchAdd()`, etc. (works on MSVC without C11)
- Timers: High-resolution timing APIs
- File system: Path manipulation, VFS

**Windows/Unix/WASM support** through conditional compilation.

## Build System

### CMake Configuration
**Custom build types:**
- `Debug` - Full debug, assertions enabled
- `Dev` - Optimized with debug info (default for development)
- `DevNoOpt` - Debug without optimization
- `Release` - Full optimization

**CMake Presets**: Use `CMakePresets.json` for preconfigured builds:
```bash
cmake --preset msvc-dev       # Windows MSVC
cmake --preset gcc-debug      # Unix GCC
cmake --build build/msvc-dev
```

**Out-of-source builds**: Always build in `build/` or `_win64/` directories.

### Testing
Tests use a custom runner: `test_runner <testname> <subtest>`

Example: `test_runner objtest iface` runs the interface test.

**Test structure** (`tests/common.h`):
- Each test file exports `TEST_FUNCS[]` array
- Sub-tests are individual functions returning 0 on success

## Coding Style

### Formatting
- **4-space indentation**, no tabs
- **Allman braces for function definitions** — opening brace on its own line
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

### Include Guards
Always `#pragma once` — **never** use `#ifndef` include guards.

### C++ Compatibility
All headers wrap declarations in `CX_C_BEGIN` / `CX_C_END` (from `cx/platform/cpp.h`).

### Inlining
Use `_meta_inline` instead of `static inline` for cross-platform forced inlining.

### Assertions
Use `devAssert()` / `devAssertMsg()` (debug builds) and `relAssert()` / `relAssertMsg()` (all builds). **Never use standard `assert()`.**

## Critical Conventions

### Memory Management

**CRITICAL: Never use `malloc`/`free` directly - always use the xalloc system (`xaAlloc`/`xaFree`).**

The xalloc system wraps mimalloc or the system allocator and provides enhanced functionality including alignment, optional allocations, and out-of-memory handling.

#### xalloc Examples:

```c
// Basic allocation
MyStruct *data = xaAlloc(sizeof(MyStruct));

// Convenience macro for struct allocation
MyStruct *data = xaAllocStruct(MyStruct);

// Zero-filled allocation
uint8 *buffer = xaAlloc(1024, XA_Zero);

// Aligned allocation (align to 2^6 = 64 byte boundary)
void *aligned = xaAlloc(4096, XA_Align(6));

// Optional allocation that may fail (returns NULL on OOM)
void *data = xaAlloc(huge_size, XA_Opt);
if (!data) {
    // handle allocation failure
}

// Resize existing allocation
xaResize(&buffer, new_size);  // Takes pointer-to-pointer

// Free memory
xaFree(data);

// Destroy with NULL assignment (preferred pattern)
xaDestroy(&data);  // Frees and sets data = NULL
```

**Optional allocation tiers** (use with `XA_Optional(tier)`):
- `XA_Optional(High)` - Try hard to allocate (default with `XA_Opt`)
- `XA_Optional(Low)` - Don't try particularly hard
- `XA_Optional(None)` - Fail immediately if no memory available

**Other memory rules:**
1. **Strings**: Always initialize to `0`, use `strDestroy()` to free
2. **Containers**: Always initialize using the applicable *Init() functions, use destructor macros
3. **Objects**: Use `objRelease()`, not manual `free()`
4. **Mimalloc**: Default allocator (can override by turning off `CX_MIMALLOC`)

### Type Safety
- **Always pass type parameters**: `saPush(&arr, int32, val)` not just `saPush(&arr, val)`
- **Use `stvar()` for variants**: `htInsert(&ht, string, key, stvar(int32, 42))`
- **Check return values**: Many functions return indices (`-1` for not found) or `NULL`

### SAL Annotations
Function signatures use Microsoft SAL-style annotations for static analysis and documentation. Required on public API functions.

**Parameter direction:** `_In_`, `_Out_`, `_Inout_` (and `_opt_` variants for nullable)

**Pointer validity:** `_Nullable`, `_Nonnull`, `_Pre_notnull_`, `_Post_null_`, `_Post_invalid_`

**Return values:** `_Ret_valid_`, `_Ret_maybenull_`, `_Check_return_`

**Function properties:** `_Pure` (no side effects), `_Success_(return)`

**Conditional:** `_When_(condition, annotation)`, `_Post_equal_to_(value)`

**In implementation files**, use `_Use_decl_annotations_` on definitions to inherit annotations from the header rather than repeating them:

```c
// Header:
_When_(s == NULL, _Post_equal_to_(0)) _Pure uint32 strLen(_In_opt_ strref s);

// Implementation:
_Use_decl_annotations_
uint32 strLen(strref s) { ... }
```

### Handle Paradigm
CX uses a consistent convention to distinguish read-only access from mutation:

- **Pass the handle by value** for operations that do not modify the object.
- **Pass a pointer to the handle (`&handle`)** for operations that may modify, replace, or destroy it.

```c
uint32 len = strLen(s);          // s by value — read-only
strAppend(&s, _SL(" world"));    // &s — modifies in place
strDestroy(&s);                  // &s — destroys and NULLs the handle
```

This matters especially for containers: an append or insert may reallocate, so always pass `&arr` when the container can grow.

String types make the distinction explicit at the type level:
- `strref` — read-only borrowed reference; pass by value
- `string` — owning handle; pass `&s` for writes
- `strhandle` (`string *`) — pointer to owning handle; used as output/in-out parameter

### Avoiding `const`
`const` is used sparingly and is **not** the primary mechanism for expressing read-only intent. The handle paradigm (`value` vs `&`) and SAL annotations (`_In_`, `_Inout_`) carry that information instead. COW string operations physically modify reference counts, so propagating `const` would require pervasive casts. Use `strref` / `_In_` to communicate read-only intent.

### Output Parameter Ordering
Output parameters come **first** (leftmost), mirroring assignment syntax — in `someFunc(&x, y)` the destination is on the left, just like `x = y`. Existing values at an output location are destroyed before the new value is assigned.

```c
void strDup(_Inout_ strhandle o, _In_opt_ strref s);   // dest first, src second
```

For type-safe generic calls, the **type name immediately precedes the value it describes**:

```c
saPush(&arr, int32, 42);
htInsert(&ht, string, _SL("key"), int32, 42);
```

### Naming Patterns
| Element | Convention | Examples |
|---------|-----------|---------|
| Public functions | Module prefix + camelCase | `strLen()`, `htInsert()`, `saPush()` |
| Internal functions | Leading underscore + camelCase | `_strFlatten()`, `_htInit()` |
| Types/Classes/Interfaces | PascalCase | `ObjInst`, `HashTableHeader`, `Sortable` |
| Opaque handle typedefs | lowercase | `string`, `hashtable`, `sarray` |
| Declared array types | `sa_` prefix | `sa_int32`, `sa_string` |
| Macros (constant-like) | SCREAMING_CASE | `HT_SLOTS_PER_CHUNK`, `STR_LEN_MASK` |
| Macros (function-like) | camelCase (matching API) | `stType()`, `saInit()` |
| Enum values | SCREAMING_CASE with prefix | `STCLASS_OPAQUE`, `SA_Sorted` |
| Flag parameters | `flags_t` (typedef for `uint32`) | All flag arguments |

### Header Organization
- `cx/include/cx/*.h` - Public API headers (aggregate includes)
- Module subdirectories contain implementation headers
- **Never include implementation headers directly** - use public includes

## Common Tasks

### Adding a New Class
1. Create `myclass.cxh` with interface/class definitions
2. Add `add_cxautogen(myclass.cxh)` to CMakeLists.txt
3. Run cxautogen to generate files including C stubs
4. Implement methods in `myclass.c`
5. Include generated `myclass.h` in your code

### Adding Tests
1. Create `mytest.c` with test functions
2. Add to `create_test_sourcelist()` in `tests/CMakeLists.txt`
3. Add test cases with `add_test(NAME "category: Test" COMMAND test_runner mytest testname)`
4. Also add to `tests/alltests.c`, which contains a test that runs all other tests in series to ensure that no test state leaks and affects other tests.

### Lazy Initialization
One-time initialization uses the `lazyInit()` pattern:
```c
static LazyInitState initState;
static void doInit(void* unused) { ... }
// In function:
lazyInit(&initState, doInit, NULL);
```

### Cross-Platform Code
- Use `CX_PLATFORM_IS_WINDOWS`, `CX_PLATFORM_IS_UNIX`, `CX_PLATFORM_IS_WASM` defines
- Check compiler with `CX_COMPILER_IS_MSVC`, `CX_COMPILER_IS_CLANG`, `CX_COMPILER_IS_GNU`
- Use platform abstractions in `cx/platform/` rather than direct OS APIs

## Working with Dependencies
Third-party libraries in `3rdparty/`:
- **lua** - Lua scripting (optional, controlled by `CX_LUA` option)
- **mbedtls** - Cryptography
- **mimalloc** - Memory allocator
- **pcre2** - Regular expressions

These are statically linked and managed by the build system.

## Documentation Style Guidelines
When documenting functions and macros in header files, follow these conventions to maintain compatibility with both IDE tooltips and Doxygen documentation generation:

### Comment Style
- **Always use `///`** for documentation comments — **never use `/** */`**
- Use `@param`, `@return`, `@code`/`@endcode` — **never backslash variants** (`\param`, `\return`)
- `@brief` is **not needed** — Doxygen automatically uses the first line as the brief description
- Use `//` (regular comments) for internal notes and undocumented functions
- For macros: First line should be a synthetic prototype as if the macro were a C function (shows in IDE tooltips). Follow with a blank `///` line.
- For native C functions: Do NOT include a synthetic prototype - the actual function signature is already visible

### Structure
After the description, document parameters and return values:
```c
/// @param param1 Description of param1
/// @param param2 Description of param2
/// @return Description of return value
```

For code examples, use Doxygen code blocks:
```c
/// Example:
/// @code
///   string s = 0;
///   strDup(&s, _SL("hello"));
/// @endcode
```

### Doxygen-Specific Tags
- **`@file filename`** - Document the file itself (what's in it)
- **`@defgroup group_id Group Title` / `@{` / `@}`** - Create organized module groups that appear in a hierarchical tree in documentation
  - Use `@ingroup parent_group` to nest groups
  - Always close groups with `@}` at the appropriate scope
  - **CRITICAL**: Be very careful with group scope - place `@{` immediately after the defgroup line and `@}` right after the last item that should be in the group to avoid accidentally including unrelated functions
  - Header files should typically define one main group for the module, with logical sub-groups for related functions/types
  - Typically, each major feature will have a @defgroup in one of the main include files under `cx/include/cx/`, while individual header files will have their own @defgroup for the file contents
  that is @ingroup the main module group.

### Organizing Documentation with Groups
Use `@defgroup` to create hierarchical module documentation instead of flat `@page` entries:

**Pattern for top-level module (in aggregate header under `cx/include/cx/`):**
```c
/// @file mymodule.h
/// @brief Brief description

/// @defgroup mymodule My Module
/// @{
/// Detailed module description here

// Module contents here (types, macros, functions)

/// @}  // end of mymodule group
```

**Pattern for implementation header (in module subdirectory):**
```c
/// @defgroup mymodule_base Core Functions
/// @ingroup mymodule
/// @{

// Types and function declarations...

/// @}  // end of core functions group
```

**Pattern for `*_overview` groups (narrative documentation):**
```c
/// @defgroup mymodule_overview Overview
/// @ingroup mymodule
/// @{
///
/// @section mymodule_types Types
/// Description with **bold** text, bullet lists, and code examples.
/// @code
///   // example code
/// @endcode
///
/// @}  // end of overview group
```

**Key points:**
- Place `@{` right after the defgroup/description to start the group scope
- Place `@}` immediately after the last function/item that belongs in the group
- This prevents subsequent unrelated functions from being included in the group
- Use `@ingroup` to create parent-child relationships between groups
- Groups appear in a tree structure under "Topics" in generated documentation

### Special Cases
- For functions taking runtime type parameters (like `stype`), omit the `stype` type from the synthetic prototype as it's a descriptor processed by the macro system
- For functions with optional `flags_t` parameters through macros, show as `[flags]` in the prototype
- We use HIDE_UNDOC_MEMBERS, so internal functions or functions that are completely wrapped by macros, should have no /// documentation at all (they can still have regular comments). This will prevent them from appearing in the generated docs.

### Narrative Documentation Style
Module overview groups (`*_overview`) contain extensive narrative documentation:
- `@section` tags for major topics
- Bold text with `**text**` for emphasis
- Bullet lists with `-` prefix
- `@code` / `@endcode` blocks for examples throughout
- Explanations of lifecycle, thread safety, optimization, and conceptual models

### File-Level Documentation
Every header file should have:
- `@file` tag with filename and brief description
