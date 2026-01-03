# CX Framework Documentation

CX is a C utility framework focused on programmer convenience and cross-platform development. It provides high-level constructs while maintaining the performance and control of C.

## Core Features

### Strings
String library with copy-on-write semantics and automatic rope conversion for very long strings. Provides comprehensive manipulation functions, searching, comparison operations, and UTF-8 encoding support.

See @ref string

### Containers
Type-safe dynamic arrays (@ref array) and hash tables (@ref hashtable) with automatic cleanup and generic iteration via @ref foreach. Provides efficient memory usage without template bloat through the runtime type system.

See @ref containers

### Runtime Type System
Lightweight runtime type information enabling generic programming without code explosion or template bloat. Supports type-checked argument passing, automatic conversions, and variant types (@ref stvar) for flexible data handling.

See @ref stype

### Object System
Object-oriented programming with classes, interfaces, inheritance, and mixins in pure C. Features automatic reference counting, weak references for breaking cycles, runtime type checking with dynamic casting, and code generation to minimize boilerplate.

See @ref obj

### Threading
Cross-platform threading primitives including thread creation and management, mutexes, read-write locks, events, condition variables, and C11-style atomic operations. Provides consistent API across all platforms, even on compilers without native C11 atomics support.

See @ref thread

### File System
Native and virtual filesystem access with unified APIs across platforms. Provides file I/O operations, directory traversal and manipulation, cross-platform path handling, and filesystem metadata queries.

See @ref fs

### Time
High-resolution timers and time manipulation functions for precise cross-platform timing operations. Supports duration calculations, timestamps, and timer management.

See @ref time

### Semi-Structured Data
Tree-based storage system for hierarchical configuration and data management. Provides path-based access similar to filesystems, thread-safe locking support, and JSON serialization for persistence.

See @ref ssd

### Logging System
Flexible logging framework with multiple output backends including files, in-memory buffers, and deferred logging. Supports configurable severity levels, formatted output, and structured logging patterns.

See @ref log

### Memory Management
Memory allocation system wrapping the high-performance mimalloc allocator. Provides custom alignment support, optional allocations that can fail gracefully, out-of-memory handling policies, and allocation size introspection.

See @ref xalloc

### Closures
Function closures with captured context and automatic lifetime management, enabling advanced callback patterns and functional programming paradigms in pure C without manual context management.

See @ref closure

### Serialization
Binary serialization system with automatic endianness handling for portable data storage and network transmission. Ensures data compatibility across different architectures and platforms.

See @ref serialize

### Platform Abstraction
Cross-platform APIs abstracting OS differences for Windows, Linux, FreeBSD, and WebAssembly on x86-64, x86, ARM64, and WASM architectures.

See @ref platform

### Debug Utilities
Development and debugging aids including runtime assertions, automatic stack trace generation, crash handlers with diagnostics, black box logging for post-mortem analysis, and comprehensive error code management.

See @ref debug

### Utilities
Collection of helper functions and macros for common programming tasks. Includes lazy initialization patterns, generic comparison utilities, callback management, and various convenience functions.

See @ref utils

### Additional Features

- [Random Number Generation](@ref rng) - PCG and LCG implementations
- [Sortable Unique IDs](@ref suid) - Time-based unique identifiers with lexicographic ordering  
- [System Functions](@ref sys) - Host identification and system-level operations

## Design Philosophy

CX is designed to be:
- **Self-contained**: Statically linked with no external runtime dependencies
- **Efficient**: Memory-conscious with lazy allocation and copy-on-write semantics
- **Type-safe**: Runtime type system prevents common errors while maintaining C compatibility
- **Cross-platform**: Write once, compile everywhere with consistent behavior
- **Convenient**: High-level features without sacrificing control or performance

All features work together seamlessly through the runtime type system, making it easy to build complex applications in pure C.
