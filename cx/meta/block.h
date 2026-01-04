#pragma once

/// @file block.h
/// @brief Block wrapping macros for automatic resource management

/// @defgroup meta_block Block Wrapping
/// @ingroup meta
/// @{
///
/// Lightweight block wrapping macros for automatic cleanup and resource management.
///
/// The primary facility provided by this module is blkWrap(), which wraps a code block
/// with "before" and "after" statements. The after statement is guaranteed to execute
/// even if the block exits early with `break` or `continue`, making it ideal for
/// resource management patterns similar to RAII in C++.
///
/// **Common Use Cases:**
///
/// @code
///   // Mutex protection - automatic unlock even on early exit
///   blkWrap(mutexAcquire(&mtx), mutexRelease(&mtx)) {
///       // critical section
///       if (error) break;  // mutex still released
///   }
///
///   // Resource management - automatic cleanup
///   blkWrap(file = fopen("data.txt", "r"), fclose(file)) {
///       if (!file) break;
///       // use file
///   }
///
///   // Reference counting - automatic release
///   blkWrap(objAcquire(&obj), objRelease(&obj)) {
///       // work with object
///   }
/// @endcode
///
/// The framework uses these macros throughout for creating higher-level wrappers:
/// - `withMutex(m)` - Wraps mutex acquire/release
/// - `withReadLock(l)` and `withWriteLock(l)` - Wraps RWLock operations
/// - Many container iteration macros
///
/// **Performance Characteristics:**
///
/// Unlike protected blocks or exception handling, blkWrap() is very lightweight:
/// - Negligible runtime overhead
/// - Does not inhibit compiler optimizations
/// - Minimal stack space usage
/// - Suitable for performance-critical code
///
/// **Compile-Time Feature Inhibition:**
///
/// The module also provides compile-time checks to prevent misuse of certain language
/// features within blocks. For example, `return` is inhibited in many block contexts
/// to prevent resource leaks. In debug builds, using `return` where it's disallowed
/// results in a compilation error.

// Block wrapping using complex for statements
// Inspired by P99 preprocessor macros, modified (in some cases heavily)
// to fit with CX design philosophy.
// https://gustedt.gitlabpages.inria.fr/p99/p99-html/

#include <cx/stype/stype.h>
#include <cx/platform/base.h>
#include <cx/utils/macros/tokens.h>

// -------------------- Compile-Time Feature Inhibition --------------------

/// @defgroup meta_inhibit Feature Inhibition
/// @ingroup meta_block
/// @{
///
/// Compile-time checks to prevent use of specific language features within certain blocks.
///
/// This facility allows library code to enforce correct usage patterns by disallowing certain
/// operations (like `return`) within blocks where they would cause problems such as resource
/// leaks or incorrect control flow.
///
/// In debug builds, attempting to use an inhibited feature results in a compile error.
/// In release builds, the checks are disabled for performance.

#define _inhibit_name(name) _inhibit_##name

/// inhibitDeclare(name)
///
/// Declares a feature that can be inhibited at compile time.
///
/// @param name Feature identifier to declare
#define inhibitDeclare(name) enum { _inhibit_name(name) = 0 }

/// inhibitCheck(name)
///
/// Results in a compile error if the specified feature is inhibited in the current block.
///
/// This is typically used by redefining language keywords to check themselves. For example,
/// the `return` keyword is redefined in debug builds to check if RETURN is inhibited.
///
/// @param name Feature identifier to check
///
/// @note Disabled on MSVC versions prior to VS2022 due to a compiler bug
#if !defined(_MSC_VER) || _MSC_VER > 1930
#define inhibitCheck(name) switch(tokstring(_inhibit_name(name))[_inhibit_name(name)]) default:
#else
#define inhibitCheck(name)
#endif

#define _inhibitDisallow(name) _blkCond(const int * const _inhibit_name(name) = 0, !_inhibit_name(name))

/// inhibitDisallow(name)
///
/// Inhibits the use of a feature inside the given block.
///
/// Any attempt to use the inhibited feature within the block will result in a compile error
/// in debug builds. This is typically used by library code to prevent incorrect usage patterns.
///
/// @param name Feature identifier to inhibit
///
/// Example:
/// @code
///   inhibitDisallow(RETURN) {
///       // return statement not allowed here
///       // return 0;  // Would cause compile error in debug builds
///   }
/// @endcode
#define inhibitDisallow(name) _blkStart _inhibitDisallow(name)

#define _inhibitAllow(name) _blkCond(const int _inhibit_name(name) = 0, !_inhibit_name(name))

/// inhibitAllow(name)
///
/// Allows the use of a previously inhibited feature inside the given block.
///
/// This creates an exception to an outer inhibitDisallow() block, permitting the feature
/// to be used within this nested scope.
///
/// @param name Feature identifier to allow
#define inhibitAllow(name) _blkStart _inhibitAllow(name)

/// @}  // end of meta_inhibit group

// The "RETURN" feature is used internally for correctness checks

// If this is being compiled in debug/dev mode, redefine "return" to check it for correctness
#if DEBUG_LEVEL >= 1 && !defined(_PREFAST_)
inhibitDeclare(RETURN);
#define _inhibitReturn _inhibitDisallow(RETURN)
#define _allowReturn _inhibitAllow(RETURN)
#define return inhibitCheck(RETURN) return
#else
#define _inhibitReturn _blkStart
#define _allowReturn _blkStart
#endif

// -------------------- Block Wrapping Macros --------------------

#define _BLK_VAR _block_done

// Internal building blocks (no pun intended)

#define _blkDef(before) for (tokeval(before); _BLK_VAR; _BLK_VAR = 0)
#define _blkCond(before, cond) for (tokeval(before); (cond) && _BLK_VAR; _BLK_VAR = 0)
#define _blkFull(before, cond, ...) for (tokeval(before); (cond) && _BLK_VAR; (__VA_ARGS__), _BLK_VAR = 0)

// _blkStart should be the first token used when building a structure that uses blocks.
// It declares the marker variable that is used to ensure the various for loop abuse only executes once.
#define _blkStart _blkDef(bool _BLK_VAR = 1)
#define _blkBefore(...) for (tokeval(__VA_ARGS__); _BLK_VAR; _BLK_VAR = 0)
#define _blkBeforeAfter(before, ...) _inhibitReturn _blkFull(tokeval(before), true, __VA_ARGS__)
#define _blkAfter(...) _blkBeforeAfter(, (__VA_ARGS__))
// _blkEnd is used as an inner loop around a user-provided block to swallow 'break' so it
// doesn't interrupt control flow.
#define _blkEnd _blkBefore()

// Special helper for declaring a scoped variable that can refer to a variable in the outer scope with
// the same name. It does this by using a temporary variable with a different name.
#define _blkDefRecursive(type, name, ...)                                                                       \
    _blkDef(type tokcat2(_block_decl_, name) = tokeval(__VA_ARGS__))                                            \
    _blkCond(type name = tokcat2(_block_decl_, name), ((void)name, true))

/// blkWrap(before, after) { }
///
/// Wraps a code block with "before" and "after" statements, guaranteeing cleanup.
///
/// This is the primary facility for automatic resource management in CX. The before statement
/// executes once when entering the block, and the after statement executes when leaving the
/// block - either normally or via early exit with `break` or `continue`.
///
/// **Key Features:**
/// - Lightweight with negligible runtime overhead
/// - Does not inhibit compiler optimizations
/// - Minimal stack space usage
/// - `break` or `continue` at the base level will exit the block but still run cleanup
/// - `return` is not allowed within the block (compile error in debug builds)
///
/// @param before Statement to execute before the block
/// @param ... (after) Statement(s) to execute after the block (can use comma operator for multiple)
///
/// **Common Patterns:**
///
/// @code
///   // Mutex protection
///   blkWrap(mutexAcquire(&mtx), mutexRelease(&mtx)) {
///       // critical section
///   }
///
///   // Multiple cleanup operations
///   blkWrap(init(), (cleanup1(), cleanup2())) {
///       // work
///   }
///
///   // Early exit still runs cleanup
///   blkWrap(acquire(), release()) {
///       if (error) break;  // release() still called
///       // more work
///   }
/// @endcode
///
/// **Used Throughout CX:**
/// - `withMutex(m)` - Mutex acquire/release
/// - `withReadLock(l)`, `withWriteLock(l)` - RWLock operations  
/// - `foreach` and container iteration macros
/// - Many other resource management patterns
///
/// @note For complex unwinding scenarios involving nested blocks, see @ref meta_pblock
#define blkWrap(before, ...) _blkStart _blkBeforeAfter(tokeval(before), __VA_ARGS__) _blkEnd

/// @}  // end of meta_block group
