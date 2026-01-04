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
strDup(&str, _S"hello");      // Copy/assign
strAppend(&str, _S" world");  // Modify in place (COW semantics)
strDestroy(&str);             // Cleanup
```

**String literals use `_S` prefix**: `_S"literal"` creates a static string.

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
htInsert(&ht, string, _S"key", int32, 42);
int32 val;

if (htFind(ht, string, _S"key", int32, &val) {
    // found, use val
}
// -- OR --
htelem elem = htFind(ht, string, _S"key", none, NULL);
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

// Release with NULL assignment (preferred pattern)
xaRelease(&data);  // Frees and sets data = NULL
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

### Naming Patterns
- **Interfaces**: `MyInterface` (PascalCase)
- **Classes**: `MyClass` (PascalCase)
- **Functions**: `myFunction` (camelCase)
- **Macros**: `MACRO_NAME` or `macroName` depending on usage
- **Internal functions**: `_myInternalFunc` (leading underscore), should be considered private

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
- Use `///` for all documentation comments (Doxygen-compatible)
- For macros: First line should be a synthetic prototype as if the macro were a C function (shows in IDE tooltips). Follow with an empty line comment for spacing.
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
///   strDup(&s, _S"hello");
/// @endcode
```

### Doxygen-Specific Tags
- **`@page page_id Page Title`** - Create standalone documentation pages for overviews/guides (use sparingly, prefer groups)
- **`@file filename`** - Document the file itself (what's in it)
- **`@brief`** - Short one-line summary
- **`@defgroup group_id Group Title` / `@{` / `@}`** - Create organized module groups that appear in a hierarchical tree in documentation
  - Use `@ingroup parent_group` to nest groups
  - Always close groups with `@}` at the appropriate scope
  - **CRITICAL**: Be very careful with group scope - place `@{` immediately after the defgroup line and `@}` right after the last item that should be in the group to avoid accidentally including unrelated functions
  - Header files should typically define one main group for the module, with logical sub-groups for related functions/types
  - Typically, each major feature will have a @defgroup in one of the main include files under `cx/include/cx/`, while individual header files will have their own @defgroup for the file contents
  that is @ingroup the main module group.

### Organizing Documentation with Groups
Use `@defgroup` to create hierarchical module documentation instead of flat `@page` entries:

**Pattern for file-level groups:**
```c
/// @file mymodule.h
/// @brief Brief description

/// @defgroup mymodule My Module
/// @{
/// Detailed module description here

// Module contents here (types, macros, functions)

/// @}  // end of mymodule group
```

**Pattern for subsections within a file:**
```c
/// @defgroup mymodule_subsection Subsection Title
/// @ingroup mymodule
/// @{
///
/// Description and usage examples
/// @code
///   // example code
/// @endcode

// Only the specific functions/types for this subsection

/// @}  // end of mymodule_subsection group
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

### File-Level Documentation
For major headers, include:
- `@file` tag with filename and brief description
- Consider `@page` for overview documentation that should be prominent in generated docs
- Use `@section` to organize content within overview pages