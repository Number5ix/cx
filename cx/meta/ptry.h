#pragma once

/// @file ptry.h
/// @brief Exception handling (advanced feature with semantic differences)

/// @defgroup meta_ptry Exception Handling
/// @ingroup meta
/// @{
///
/// **Advanced Feature:** Exception handling with try/catch/finally semantics.
///
/// Provides exception handling built on protected blocks, allowing errors to propagate up the
/// call stack with automatic cleanup. However, this system has important semantic differences
/// from exception handling in languages like C++ or Java.
///
/// **Performance Warning:**
///
/// Exception handling inherits all the performance costs of protected blocks:
/// - Significant overhead from setjmp/longjmp on every ptTry
/// - Inhibits compiler optimizations throughout protected regions
/// - Substantial stack space consumption
/// - Not suitable for performance-critical or frequently-called code
///
/// **Critical Semantic Differences:**
///
/// 1. **ptCatch blocks ALWAYS execute**, even when no exception occurs:
///    - Check `ptCode` to determine if an exception actually happened
///    - `ptCode == 0` means no exception
///
/// 2. **No automatic exception filtering**:
///    - ptCatch catches ALL exceptions
///    - Must manually check `ptCode` and use `ptRethrow` for selective handling
///
/// 3. **Exception messages MUST be static string literals**:
///    - Cannot use stack-allocated or heap-allocated strings
///    - Messages are stored by pointer, not copied
///
/// 4. **Cannot mix pblocks and exceptions**:
///    - Once using ptTry/ptCatch, must use them consistently
///    - PBLOCK_AFTER labels are skipped during exception processing
///    - Using ptThrow inside a non-ptTry pblock causes incorrect flow
///
/// **When to Use:**
/// - Complex error propagation across multiple function boundaries
/// - When error codes are insufficient for your use case
/// - Non-performance-critical subsystems with complex cleanup needs
///
/// **When NOT to Use:**
/// - Performance-critical paths
/// - Simple error handling (use return codes instead)
/// - Hot loops or frequently-called functions
/// - Most application code (error codes are simpler and faster)
///
/// Example:
/// @code
///   ptTry {
///       resource1 = acquire1();
///       if (!resource1) ptThrow(ERR_RESOURCE, "Failed to acquire resource1");
///       
///       resource2 = acquire2();
///       if (!resource2) ptThrow(ERR_RESOURCE, "Failed to acquire resource2");
///       
///       processData();
///   }
///   ptCatch {
///       // ALWAYS runs, even without exception
///       if (ptCode) {
///           // Exception occurred
///           logError("Exception %d: %s", ptCode, ptMsg);
///           
///           // Selective handling
///           if (ptCode == ERR_FATAL) {
///               ptRethrow;  // Let caller handle fatal errors
///           }
///       }
///       // Cleanup
///       if (resource2) release2(resource2);
///       if (resource1) release1(resource1);
///   }
/// @endcode

#include <cx/meta/pblock.h>

/// Exception information structure.
///
/// Contains details about an exception that is being thrown or is currently active.
/// This is stored in thread-local storage during exception processing.
typedef struct ExceptionInfo {
    int code;                   ///< Application-specific exception code
    const char *msg;            ///< Exception message - MUST be a static string literal
#if DEBUG_LEVEL >= 1
    const char *file;           ///< Source filename where exception was thrown
    int ln;                     ///< Line number where exception was thrown
#endif
} ExceptionInfo;

extern _Thread_local _pblock_jmp_buf_node *_ptry_top;           // stack of jump buffers to rewind to
extern _Thread_local ExceptionInfo _ptry_exc;                   // currently active exception
extern ExceptionInfo _ptry_exc_empty;

/// Handler function for uncaught exceptions.
///
/// @param einfo Information about the uncaught exception
/// @return 0 to abort the program, 1 to resume execution after the exception point
typedef int (*ptUnhandledHandler)(ExceptionInfo *einfo);

// GCC falsely warns that _ptry_top is a dangling pointer, even though we're very careful to clean
// it up when exiting the scope that the local variable it points to is declared in.
#if defined(__GNUC__) && __GNUC__ >= 12
#define ptDisablePtrWarning _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdangling-pointer\"")
#define ptReenableWarnings _Pragma("GCC diagnostic pop")
#else
#define ptDisablePtrWarning
#define ptReenableWarnings
#endif

/// Registers a handler for uncaught exceptions.
///
/// The handler is called when an exception is thrown with no matching ptCatch block up the
/// call stack. This is a last-resort mechanism for error reporting or recovery.
///
/// @param handler Callback function that receives the exception information.
///                Return 0 to abort the program, or 1 to resume execution.
///
/// If multiple handlers are registered, they must ALL return 1 for execution to resume.
/// Execution resumes after the outermost ptFinally block, or immediately after the throw
/// if no ptTry blocks exist.
///
/// Example:
/// @code
///   int myHandler(ExceptionInfo *einfo) {
///       fprintf(stderr, "Uncaught exception %d: %s\n", einfo->code, einfo->msg);
///       return 0;  // Abort
///   }
///   ptRegisterUnhandled(myHandler);
/// @endcode
void ptRegisterUnhandled(ptUnhandledHandler handler);

/// Unregisters a previously registered handler for uncaught exceptions.
///
/// @param handler The handler function to remove
void ptUnregisterUnhandled(ptUnhandledHandler handler);

void _ptry_handle_unhandled(ExceptionInfo *einfo);

_meta_inline void _ptry_clear(void)
{
    _ptry_exc = _ptry_exc_empty;
}

_meta_inline void _ptry_push(_pblock_jmp_buf_node *node)
{
    node->next = _ptry_top;
    ptDisablePtrWarning;
    _ptry_top = node;
    ptReenableWarnings;
}

_meta_inline _pblock_jmp_buf_node *_ptry_pop(void)
{
    _pblock_jmp_buf_node *ret = _ptry_top;
    if (!ret)
        return ret;

    _ptry_top = ret->next;
    ret->next = NULL;
    return ret;
}

_meta_inline void _ptry_pop_until(_pblock_jmp_buf_node *node)
{
    _pblock_jmp_buf_node *last;
    // keep popping until we hit the specified node or run out of jump buffers
    do {
        last = _ptry_pop();
    } while (last && last != node);
}

#if DEBUG_LEVEL >= 1
_meta_inline void _ptry_throw(int code, const char *msg, const char *file, int ln)
#else
_meta_inline void _ptry_throw(int code, const char *msg)
#endif
{
    // populate the thread-local exception info as we go into exception-handling mode
    _ptry_exc.code = code;
    _ptry_exc.msg = msg;
#if DEBUG_LEVEL >= 1
    _ptry_exc.file = file;
    _ptry_exc.ln = ln;
#endif

    // jump to the handler block if we have one, in most cases this function stops executing here
    if (_ptry_top) _pbLongjmp(_ptry_top, -1);

    // otherwise call the unhandled exception handler, this will probably abort and never return
    _ptry_handle_unhandled(&_ptry_exc);

    // if it DOES return, terminate exception processing
    _ptry_clear();
}

/// ptThrow(code, msg)
///
/// Throws an exception to be caught by a ptCatch or ptFinally block up the call stack.
///
/// An exception consists of a numeric code (application-defined) and a message. Control
/// transfers immediately to the nearest ptCatch/ptFinally block, unwinding the stack and
/// executing any cleanup code along the way.
///
/// @param code Application-specific numeric exception code (integer constant)
/// @param msg Exception message - **MUST be a static string literal**
///
/// **Critical Requirements:**
/// - The message MUST be a string literal: `ptThrow(ERR_IO, "File not found")`
/// - Do NOT use stack variables: `ptThrow(ERR_IO, buffer)` is WRONG
/// - Do NOT use heap strings: `ptThrow(ERR_IO, strdup(msg))` is WRONG
/// - The message is stored by pointer, not copied
///
/// Example:
/// @code
///   #define ERR_IO 100
///   #define ERR_MEMORY 101
///   
///   FILE *f = fopen("data.txt", "r");
///   if (!f) ptThrow(ERR_IO, "Failed to open data.txt");  // OK - string literal
///   
///   void *mem = malloc(size);
///   if (!mem) ptThrow(ERR_MEMORY, "Out of memory");  // OK - string literal
/// @endcode
///
/// @note In debug builds, file and line number are automatically captured
/// @see ptCatch, ptFinally, ptRethrow
#if DEBUG_LEVEL >= 1
#define ptThrow(code, msg) _ptry_throw(code, msg, __FILE__, __LINE__)
#else
#define ptThrow(code, msg) _ptry_throw(code, msg)
#endif

/// ptRethrow
///
/// Re-throws the currently active exception to a handler further up the call stack.
///
/// Used inside a ptCatch block to propagate an exception after performing local cleanup
/// or logging. This is the mechanism for selective exception handling - catch specific
/// exception codes locally and rethrow others.
///
/// Example:
/// @code
///   ptTry {
///       doWork();
///   }
///   ptCatch {
///       if (ptCode) {
///           logError("Exception %d: %s", ptCode, ptMsg);
///           
///           // Handle I/O errors locally
///           if (ptCode == ERR_IO) {
///               useDefaultData();
///           } else {
///               // Let caller handle other errors
///               ptRethrow;
///           }
///       }
///   }
/// @endcode
///
/// @note Only valid inside a ptCatch block while ptCode is non-zero
/// @see ptThrow, ptCatch, ptCode
#if DEBUG_LEVEL >= 1
#define ptRethrow _ptry_throw(_ptry_exc.code, _ptry_exc.msg, _ptry_exc.file, _ptry_exc.ln)
#else
#define ptRethrow _ptry_throw(_ptry_exc.code, _ptry_exc.msg)
#endif

/// ptTry { }
///
/// Begins a try block for exception handling.
///
/// If an exception occurs inside the ptTry block, execution transfers to the immediately
/// following ptCatch or ptFinally block. One or the other is **REQUIRED** - a ptTry
/// without a matching ptCatch or ptFinally is a compile error.
///
/// **Key Differences from C++/Java:**
/// - Must be immediately followed by ptCatch or ptFinally (no multiple catch blocks)
/// - No automatic exception type filtering
/// - `return` is not allowed within the try block
///
/// If an exception is not caught anywhere in the call stack, the unhandled exception
/// handler is invoked (default: terminates the program with an error).
///
/// Example:
/// @code
///   ptTry {
///       resource = acquire();
///       if (!resource) ptThrow(ERR_RESOURCE, "Acquisition failed");
///       useResource(resource);
///   }
///   ptFinally {
///       if (resource) release(resource);
///   }
/// @endcode
///
/// @note Inherits all performance costs of protected blocks
/// @warning Cannot mix with plain pblocks in the same call graph
/// @see ptCatch, ptFinally, ptThrow
#define ptTry                                                                                       \
    pblock                                                                                          \
    /* phase 0 is the try, phase 1 is the catch/finally */                                          \
    for (unsigned _ptry_phase = 0; _ptry_phase < 2; ++_ptry_phase)                                  \
        /* try phase */                                                                             \
        if (!_ptry_phase)                                                                           \
            /* push the current pblock jump target to the exception handler stack */                \
            /* this will cause it to be entered through the switch label */                         \
            _blkBefore(_ptry_push(_pblock_unwind_top))                                              \
            do

// common setup for catch/finally
#define _ptry_after_block                                                                           \
    /* this can be reached two ways, either naturally in phase 1 of the _ptry_phase loop (this is   \
     * the 'else'), or if an exception occurs, through the case 1: label through the pblock after   \
     * longjmping into the pblock loop from a lower stack frame. The latter is the same mechanism   \
     * that PBLOCK_AFTER would use, but for try/catch it's unconditional. */                        \
    while(0); else case 1:                                                                          \
    /* critically important to protect the outer block loop variable here! */                       \
    _blkStart                                                                                       \
    /* make sure that this phase isn't executed again (if we got here via longjmp) */               \
    _blkBefore(_ptry_phase = 2)                                                                     \
    /* unwind the handler stack to one level above where we currently are */                        \
    _blkBefore(_ptry_pop_until(_pblock_unwind_top))

/// ptFinally { }
///
/// Cleanup block that executes after a ptTry, whether an exception occurred or not.
///
/// The ptFinally block **MUST immediately follow** a ptTry block. It executes after the
/// ptTry completes or an exception occurs. If an exception occurred, it is automatically
/// re-thrown after the ptFinally block completes, propagating to the caller's handler.
///
/// **Use ptFinally when:**
/// - You need guaranteed cleanup but don't want to handle the exception
/// - The exception should propagate to the caller after cleanup
///
/// Example:
/// @code
///   ptTry {
///       file = fopen("data.txt", "r");
///       if (!file) ptThrow(ERR_IO, "Cannot open file");
///       processFile(file);
///   }
///   ptFinally {
///       // Always runs, exception continues propagating upward
///       if (file) fclose(file);
///   }
/// @endcode
///
/// @note If an exception occurred, it is implicitly re-thrown after this block
/// @see ptTry, ptCatch, ptCode, ptMsg
#define ptFinally                                                                                   \
    _ptry_after_block                                                                               \
    /* finally blocks end in an implicit rethrow if there is an active exception */                 \
    _blkAfter(_ptry_exc.code ? ptRethrow : nop_stmt)

/// ptCatch { }
///
/// Exception handler block that ALWAYS executes and stops exception propagation.
///
/// The ptCatch block **MUST immediately follow** a ptTry block. It is similar to ptFinally
/// but with a critical difference: ptCatch stops exception propagation by default.
///
/// **Critical Semantic Differences from C++/Java:**
///
/// 1. **ptCatch ALWAYS executes**, even when no exception occurred:
///    - Check `ptCode` to determine if an exception happened
///    - `ptCode == 0` means normal execution (no exception)
///    - `ptCode != 0` means an exception occurred
///
/// 2. **No automatic filtering** - catches ALL exceptions:
///    - Must manually check `ptCode` to filter exception types
///    - Use `ptRethrow` to propagate exceptions you don't handle
///
/// 3. **Stops exception propagation by default**:
///    - Exception processing ends after this block unless you `ptRethrow`
///    - Use ptFinally if you want automatic propagation
///
/// Example:
/// @code
///   ptTry {
///       doWork();
///   }
///   ptCatch {
///       // ALWAYS runs, check ptCode to see if exception occurred
///       if (ptCode) {
///           printf("Exception %d: %s\n", ptCode, ptMsg);
///           
///           // Selective handling
///           if (ptCode == ERR_FATAL) {
///               ptRethrow;  // Let caller handle
///           }
///           // Other errors handled locally
///       }
///       // Cleanup runs whether exception occurred or not
///       cleanup();
///   }
/// @endcode
///
/// @note Exception is cleared after this block - use ptRethrow to propagate
/// @see ptTry, ptFinally, ptCode, ptMsg, ptRethrow
#define ptCatch                                                                                     \
    _ptry_after_block                                                                               \
    _blkAfter(_ptry_clear())

/// ptCode
///
/// The exception code of the currently active exception, or 0 if there is none.
///
/// Use inside ptCatch or ptFinally blocks to check if an exception occurred and to
/// determine the exception type.
///
/// Example:
/// @code
///   ptCatch {
///       if (ptCode == 0) {
///           // No exception - normal execution
///       } else if (ptCode == ERR_IO) {
///           // Handle I/O errors
///       } else {
///           // Handle other errors
///       }
///   }
/// @endcode
///
/// @see ptMsg, ptCatch, ptFinally
#define ptCode _ptry_exc.code

/// ptMsg
///
/// The exception message of the currently active exception, or NULL if there is none.
///
/// Points to the static string literal passed to ptThrow(). Use inside ptCatch or
/// ptFinally blocks to access the error message.
///
/// Example:
/// @code
///   ptCatch {
///       if (ptCode) {
///           fprintf(stderr, "Error %d: %s\n", ptCode, ptMsg);
///       }
///   }
/// @endcode
///
/// @warning This is a pointer to the original string literal, not a copy
/// @see ptCode, ptCatch, ptFinally
#define ptMsg _ptry_exc.msg

/// @}  // end of meta_ptry group
