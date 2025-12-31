#pragma once

#include <cx/container/foreach.h>
#include <cx/container/hashtable.h>
#include <cx/container/sarray.h>

/// @file container.h
/// @brief Generic type-safe containers with runtime type system integration

/// @defgroup containers Containers
/// @{
/// Type-safe generic containers with compile-time checking and runtime flexibility

/// @defgroup containers_overview Overview
/// @ingroup containers
/// @{
///
/// @section containers_intro Introduction
///
/// The CX container system provides type-safe generic data structures that integrate
/// deeply with the runtime type system. Unlike C++ templates which generate separate
/// code for each type instantiation, CX containers use a single implementation that
/// operates on runtime type descriptors, avoiding code bloat while maintaining
/// compile-time type safety through macro-based checking.
///
/// @section containers_philosophy Design Philosophy
///
/// **Type Safety Without Templates**: Containers enforce type safety at compile-time
/// through macro expansion that verifies type names and structure compatibility. The
/// actual container implementation is type-agnostic, operating on stype descriptors
/// and function pointers for type-specific operations.
///
/// **Runtime Type Integration**: Every container stores the stype descriptor of its
/// elements, enabling:
/// - Runtime type checking and validation
/// - Generic algorithms that work across container types
/// - Serialization and reflection capabilities
/// - Heterogeneous containers (hashtables with variant values)
///
/// **Memory Efficiency**: Containers use:
/// - Reference counting for object types (via stype ops)
/// - Copy-on-write semantics where applicable
/// - Lazy allocation and automatic shrinking
/// - Chunked allocation for hashtables to reduce fragmentation
///
/// **Consistent API**: All containers follow similar patterns:
/// - Initialization with type parameters: `xxxInit(&container, type, ...)`
/// - Type-checked operations: `xxxPush(&arr, type, value)`
/// - Automatic cleanup: `xxxDestroy(&container)`
///
/// @section containers_available Available Containers
///
/// **Dynamic Arrays (sarray)**: Contiguous arrays with:
/// - O(1) indexed access and amortized O(1) append
/// - Optional sorted mode with O(log n) binary search
/// - Configurable growth strategies
/// - Support for both value and reference semantics
///
/// **Hash Tables (hashtable)**: Open-addressed hash tables with:
/// - Type-safe key-value storage
/// - Configurable probing strategies
/// - Optional case-insensitive string keys
/// - Chunked storage allocation for efficiency
/// - Insertion-order preservation
///
/// **Generic Iteration (foreach)**: Unified iteration interface across:
/// - Arrays, hashtables, strings
/// - File system searches (VFS)
/// - SSD tree nodes
/// - Any object implementing the Iterable interface
///
/// @section containers_stype_integration Runtime Type System Integration
///
/// Containers use the stype system for element management. When you write:
/// @code
///   saInit(&arr, string, 16);
///   saPush(&arr, string, _S"hello");
/// @endcode
///
/// The macros expand to:
/// 1. Type descriptor creation: `stFullType(string)` → runtime stype value
/// 2. Type checking: Verify the value matches the declared type at compile-time
/// 3. Operation dispatch: Container uses stype to call correct copy/destroy functions
///
/// **Type Operations**: The stype system provides function pointers for:
/// - `dtor` - Destructor (called when elements are removed)
/// - `copy` - Copy constructor (for inserting elements)
/// - `cmp` - Comparison (for sorted arrays, equality checks)
/// - `hash` - Hash function (for hashtable keys)
/// - `convert` - Type conversion (for heterogeneous operations)
///
/// **Default Implementations**: Built-in types have automatic defaults:
/// - Integers/floats: bitwise copy, numeric comparison
/// - Strings: reference-counted copy, lexical comparison
/// - Objects: reference-counted, polymorphic operations
/// - Pointers: shallow copy (or reference-only with SA_Ref/HT_Ref flags)
///
/// **Custom Types**: Use `opaque(MyType)` for structures:
/// @code
///   typedef struct MyData {
///       string name;
///       int32 value;
///   } MyData;
///
///   // Array of structures (container manages memory)
///   sa_MyData arr = {0};
///   saInit(&arr, opaque(MyData), 16);
///
///   // Must provide custom destroy function if struct contains managed types
///   void myDataDestroy(stype st, stgeneric *gen, flags_t flags) {
///       MyData *data = gen->st_ptr;
///       strDestroy(&data->name);
///   }
///
///   STypeOps myops = { .dtor = myDataDestroy };
///   saInit(&arr, custom(opaque(MyData), myops), 16);
/// @endcode
///
/// @section containers_memory Memory Management
///
/// **Initialization**: Containers must be initialized before use:
/// @code
///   sa_int32 arr;
///   saInit(&arr, int32, 16);      // Initialize with initial capacity
///   // ... use array ...
///   saDestroy(&arr);              // Required cleanup
/// @endcode
///
/// Note: The Init functions fully initialize the container from indeterminate
/// state, so explicit zero-initialization is not required.
///
/// **Element Ownership**: By default, containers take ownership of elements:
/// - **Value types** (int32, float64, etc.): Copied bitwise
/// - **Strings**: Reference count incremented on insert, decremented on remove
/// - **Objects**: Reference count managed automatically
/// - **Pointers**: Copied as-is (NOT deep copied unless custom ops provided)
///
/// **Reference-Only Mode**: Use SA_Ref/HT_Ref flags to store without ownership:
/// @code
///   sa_object observers;
///   saInit(&observers, object, 0, SA_Ref);  // Don't increment refcounts
///   saPush(&observers, object, someObject);  // Just stores pointer
///   saDestroy(&observers);                   // Won't release objects
/// @endcode
///
/// **Destruction**: Always call the container's destroy function:
/// - Elements are properly destructed (unless in reference-only mode)
/// - Memory is freed
/// - Handle is set to NULL for safety
///
/// @section containers_type_safety Type Safety Guarantees
///
/// **Compile-Time Checking**: The macro system verifies:
/// - Type names are recognized (typos cause compilation errors)
/// - Values match the declared type (through union member access)
/// - Function signatures are correct
///
/// Example of compile-time safety:
/// @code
///   sa_int32 arr;
///   saInit(&arr, int32, 16);
///   saPush(&arr, int32, 42);        // OK: correct type
///   saPush(&arr, int32, _S"text");  // ERROR: incompatible value type detected at compile-time
/// @endcode
///
/// **Runtime Checking**: In debug and development builds, assertions verify:
/// - Element types match the container's declared type (e.g., no int64 in int32 array)
/// - Type conversions are valid
/// - Hash/comparison operations are type-compatible
///
/// Example that asserts at runtime (debug/dev builds):
/// @code
///   sa_int32 arr;
///   saInit(&arr, int32, 16);
///   saPush(&arr, int64, 42);        // Runtime assertion failure: type mismatch
/// @endcode
///
/// @section containers_patterns Common Usage Patterns
///
/// **Building Collections**:
/// @code
///   // Array of integers
///   sa_int32 numbers;
///   saInit(&numbers, int32, 0);
///   saPush(&numbers, int32, 10);
///   saPush(&numbers, int32, 20);
///   saPush(&numbers, int32, 30);
///
///   // String to integer map
///   hashtable scores;
///   htInit(&scores, string, int32, 16);
///   htInsert(&scores, string, _S"Alice", int32, 95);
///   htInsert(&scores, string, _S"Bob", int32, 87);
/// @endcode
///
/// **Iteration**:
/// @code
///   // Array iteration
///   foreach(sarray, i, int32, num, numbers) {
///       printf("%d\n", num);
///   }
///
///   // Hash table iteration
///   foreach(hashtable, it, scores) {
///       string name = htiKey(string, it);
///       int32 score = htiVal(int32, it);
///       printf("%s: %d\n", strC(name), score);
///   }
/// @endcode
///
/// **Search and Lookup**:
/// @code
///   // Array search (linear)
///   int32 idx = saFind(numbers, int32, 20);
///   if (idx >= 0) {
///       // found at index idx
///   }
///
///   // Hash table lookup
///   int32 score;
///   if (htFind(scores, string, _S"Alice", int32, &score)) {
///       // found, score contains value
///   }
/// @endcode
///
/// **Sorted Arrays**:
/// @code
///   sa_int32 sorted;
///   saInit(&sorted, int32, 0, SA_Sorted);
///   saPush(&sorted, int32, 30);  // Inserted in sorted position
///   saPush(&sorted, int32, 10);
///   saPush(&sorted, int32, 20);
///   // Array is now [10, 20, 30]
///   int32 idx = saFind(sorted, int32, 20);  // O(log n) binary search
/// @endcode
///
/// **Heterogeneous Collections with Variants**:
/// @code
///   hashtable config = 0;
///   htInit(&config, string, stvar, 16);
///   htInsert(&config, string, _S"port", stvar, stvar(int32, 8080));
///   htInsert(&config, string, _S"host", stvar, stvar(string, _S"localhost"));
///   htInsert(&config, string, _S"debug", stvar, stvar(bool, true));
///
///   stvar val;
///   if (htFind(config, string, _S"port", stvar, &val)) {
///       if (val.type == stType(int32)) {
///           int32 port = val.data.st_int32;
///       }
///   }
/// @endcode
///
/// @section containers_performance Performance Characteristics
///
/// **Dynamic Arrays (sarray)**:
/// - Access: O(1) by index
/// - Append: O(1) amortized (may trigger reallocation)
/// - Insert: O(n) - must shift elements
/// - Search: O(n) linear, O(log n) for sorted arrays
/// - Remove: O(n) - must shift elements
///
/// **Hash Tables (hashtable)**:
/// - Insert: O(1) average, O(n) worst case
/// - Lookup: O(1) average, O(n) worst case
/// - Remove: O(1) average, O(n) worst case
/// - Iteration: O(n) in insertion order
///
/// **Growth Strategies**: Both containers support configurable growth:
/// - Arrays: SA_Grow(Normal), SA_Grow(Aggressive), SA_Grow(Slow), etc.
/// - Hashtables: HT_Grow(MaxSpeed), HT_Grow(MinSize)
///
/// @section containers_thread_safety Thread Safety
///
/// Containers are NOT thread-safe by default:
/// - Multiple readers on the same container: UNSAFE (due to COW and refcounting)
/// - Concurrent modifications: UNSAFE
/// - Independent containers in different threads: SAFE
///
/// For thread-safe access, use external synchronization (Mutexes, RWLocks).
/// The underlying stype operations (for strings, objects) use atomic reference
/// counting, but the container structures themselves are not protected.
///
/// @section containers_best_practices Best Practices
///
/// 1. **Always call *Init() functions before use**: Containers must be initialized
///    - There is a small exception in that NULL sarrays can be lazily initialized by pushing to
///    them, but you are unable to set initialization flags in that case; they become plain unsorted
///    arrays
///
/// 2. **Always call destroy**: Memory leaks occur if you forget cleanup
///
/// 3. **Use type names consistently**: `int32` not `int`, `string` not `char*`
///
/// 4. **Choose the right container**:
///    - Fixed/dynamic size, indexed access → sarray
///    - Key-value lookup, unique keys → hashtable
///    - Sorted access, binary search → sarray with SA_Sorted
///
/// 5. **Consider growth strategies**: Pre-allocate capacity if final size is known
///
/// 6. **Use reference mode carefully**: SA_Ref/HT_Ref means you manage lifetimes
///
/// 7. **Leverage type system**: Custom types can have custom comparison, hashing, etc.
///
/// 8. **Use foreach for iteration**: More concise and handles cleanup automatically
///
/// @}  // end of containers_overview defgroup
/// @}  // end of containers defgroup
