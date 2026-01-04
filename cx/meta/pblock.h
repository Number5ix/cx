#pragma once

/// @file pblock.h
/// @brief Protected blocks with stack unwinding (advanced feature)

/// @defgroup meta_pblock Protected Blocks
/// @ingroup meta
/// @{
///
/// **Advanced Feature:** Protected blocks with multi-level stack unwinding capabilities.
///
/// Protected blocks provide stack unwinding through setjmp/longjmp, allowing early exit from
/// nested blocks with guaranteed cleanup. They are the foundation for the exception handling
/// system but can be used independently.
///
/// **Performance Warning:**
///
/// Protected blocks are heavyweight operations with significant costs:
/// - Measurable execution overhead from setjmp setup
/// - Inhibits compiler optimizations throughout the protected region
/// - Consumes substantial stack space (each nested block adds ~200+ bytes)
/// - Should only be used for complex operations that are not performance critical
///
/// **When to Use:**
/// - Complex error handling requiring multi-level unwinding
/// - As a base for exception handling systems
/// - When blkWrap() is insufficient for cleanup requirements
///
/// **When NOT to Use:**
/// - Performance-critical code
/// - Simple resource management (use blkWrap() instead)
/// - Frequently-called functions
/// - Hot loops
///
/// For most resource management needs, use blkWrap() from @ref meta_block instead.
///
/// Example:
/// @code
///   pblock {
///       // Complex operations with multiple potential exit points
///       if (error1) pblockUnwind(1);  // Exit this block
///       
///       pblock {
///           // Nested protected block
///           if (error2) pblockUnwind(2);  // Exit both blocks
///       PBLOCK_AFTER:
///           // Cleanup for inner block - always runs
///       }
///       
///   PBLOCK_AFTER:
///       // Cleanup for outer block - always runs
///   }
/// @endcode

#include <cx/meta/block.h>
#include <cx/utils/macros/unused.h>
#include <setjmp.h>

enum _pblock_unwind_const {
    _pblock_unwind_top = 0
};

/// Internal jump buffer node for protected block unwinding chain.
/// @private
typedef struct _pblock_jmp_buf_node _pblock_jmp_buf_node;
typedef struct _pblock_jmp_buf_node {
    _pblock_jmp_buf_node *next;  ///< Link to outer block's jump buffer
    jmp_buf buf;                 ///< longjmp destination for unwinding
    volatile int target;         ///< How many levels of nesting to unwind
} _pblock_jmp_buf_node;

typedef _pblock_jmp_buf_node _pblock_jmp_buf[1];
#define BLOCK_JMPBUF_INIT {[0] = {.target = 0}}

_meta_inline /*noreturn*/ void _pbLongjmp(_pblock_jmp_buf_node *node, int val)
{
    node->target = val;
    longjmp(node->buf, 1);
}

/// pblock { }
///
/// **Advanced Feature:** Declares a protected block with stack unwinding capability.
///
/// A protected block sets up a setjmp buffer for stack unwinding that can be jumped to using
/// pblockUnwind(). Protected blocks can be nested, and an arbitrary number of layers can be
/// unwound in a single operation.
///
/// **Cleanup with PBLOCK_AFTER:**
///
/// A protected block can optionally end with a `PBLOCK_AFTER:` pseudo-label. Code after this
/// label executes when exiting the block normally OR when unwinding through it, providing
/// guaranteed cleanup similar to a finally clause.
///
/// **Performance Implications:**
/// - Setjmp adds measurable overhead on every entry
/// - Inhibits compiler optimizations (inlining, register allocation, etc.)
/// - Consumes significant stack space (~200+ bytes per nested level)
/// - Use only when simpler alternatives are insufficient
///
/// @note `return` statements are not allowed within protected blocks (compile error in debug builds)
/// @note This is the foundation for ptTry/ptCatch, but can be used independently
///
/// Example:
/// @code
///   pblock {
///       resource1 = acquire1();
///       if (!resource1) pblockUnwind(1);
///       
///       resource2 = acquire2();
///       if (!resource2) pblockUnwind(1);
///       
///       // Use resources
///       
///   PBLOCK_AFTER:
///       // Cleanup runs even if we unwind early
///       if (resource2) release2(resource2);
///       if (resource1) release1(resource1);
///   }
/// @endcode
///
/// @warning Prefer blkWrap() for simple resource management - it's much lighter weight
/// @see pblockUnwind(), PBLOCK_AFTER, blkWrap()
#define pblock                                                                                                  \
    _blkStart                                                                                                   \
    _inhibitReturn                                                                                              \
    /* set to true during the unwinding process */                                                              \
    _blkDef(volatile bool _pblock_unwind = false)                                                               \
    _blkFull(/* BEFORE */ volatile int _pblock_target = 0, true,                                                \
        /* After we exit the block through whatever means, if we are unwinding, try to unwind the next level.   \
         * Since this loop is above the one that defines _pblock_unwind_top, the jump buffer here points to the \
         * one set by the block that encloses this one and we'll jump back into it, giving it the ability to    \
         * run any PBLOCK_AFTER cleanup and continue the chain.                                                 \
         * In the outermost block, _pblock_unwind_top will instead be 0 from the global enum, and pblockUnwind  \
         * will do nothing.                                                                                     \
         * If _pblock_target reaches 0, this will also exit the loop and continue execution as normal. */       \
        /* AFTER */ (_pblock_unwind ?                                                                           \
            pblockUnwind(_pblock_target <= 0 ? _pblock_target : _pblock_target - 1) : nop_stmt))                \
    /* allocate a jump buffer on the stack for this level of recursion */                                       \
    _blkDef(_pblock_jmp_buf _pblock_unwind_top = tokeval(BLOCK_JMPBUF_INIT))                                    \
    /* set the longjump target to be within the inntermost loop and coerce the return value to 1 or 0 */        \
    switch(setjmp(_pblock_unwind_top[0].buf))                                                                   \
        /* case 2 doesn't actually exist, it serves as a place to hang an if-else construct so we can define    \
           two case labels here instead of only one - case 0 for the user block to execute, and a default       \
           case label that acts as a fallback for case 1 not being defined, which can happen if the user does   \
           not include a PBLOCK_AFTER label. */                                                                 \
        case 2: if (0) {                                                                                        \
            default:                                                                                            \
                /* if there isn't a PBLOCK_AFTER but we got here as a longjmp target, we need to take care of   \
                 * setting up the variable for unwinding, otherwise the AFTER blocks will fail to jump to the   \
                 * next target in the chain and we'll stop unwinding early. */                                  \
                _pblock_target = _pblock_unwind_top[0].target;                                                  \
                _pblock_unwind = !!_pblock_target;                                                              \
        } else                                                                                                  \
        /* This is the case for when we get here the first time, right after calling setjmp. The user-provided  \
         * block slots in directly after this label. */                                                         \
        case 0:


_meta_inline void _pblockUnwind(void *top, int cond)
{
    if (cond && top) _pbLongjmp(top, cond);
}

/// pblockUnwind(num)
///
/// Exits the current protected block early via stack unwinding.
///
/// Triggers unwinding of one or more nested protected blocks, jumping to the appropriate
/// `PBLOCK_AFTER` cleanup section if one exists, then continuing to outer blocks if needed.
///
/// @param num Number of nested blocks to exit:
///   - `1` - Exit only the current protected block
///   - `2` - Exit current and one outer block
///   - `-1` - Exit all protected blocks, returning to the outermost level
///
/// Example:
/// @code
///   pblock {  // Outer
///       pblock {  // Inner
///           if (error) pblockUnwind(1);   // Exit inner only
///           if (fatal) pblockUnwind(2);   // Exit both
///           if (abort) pblockUnwind(-1);  // Exit all
///       PBLOCK_AFTER:
///           // Cleanup inner
///       }
///   PBLOCK_AFTER:
///       // Cleanup outer
///   }
/// @endcode
///
/// @note All PBLOCK_AFTER sections are executed during unwinding
/// @see pblock, PBLOCK_AFTER
#define pblockUnwind(num) _pblockUnwind((void*)_pblock_unwind_top, (num))

/// PBLOCK_AFTER:
///
/// Pseudo-label marking the cleanup section of a protected block.
///
/// Code after this label is guaranteed to execute before control flow leaves the protected
/// block, similar to a finally clause in exception handling. This occurs both during normal
/// exit and during unwinding.
///
/// The cleanup code runs:
/// - When the block completes normally
/// - When pblockUnwind() is called from within the block
/// - When unwinding through this block from a nested pblockUnwind()
/// - When an exception propagates through this block (if using ptTry)
///
/// Example:
/// @code
///   pblock {
///       file = fopen("data.txt", "r");
///       if (!file) pblockUnwind(1);
///       
///       processFile(file);
///       
///   PBLOCK_AFTER:
///       // This ALWAYS runs, even if we unwind early
///       if (file) fclose(file);
///   }
/// @endcode
///
/// @note Not affected by abnormal control flow from outside (longjmp, signals, etc.)
/// @see pblock, pblockUnwind()
#define PBLOCK_AFTER                                                                                            \
    if (0) {                                                                                                    \
        /* Even though this case 1 label is placed into the user-defined block, it technically exists within    \
         * the switch statement at the end of the pblock macro.                                                 \
         * When the user provides a PBLOCK_AFTER target, it defines this case 1 which overrides the default     \
         * case label (the one that's hidden in the if(0) after the case 2:) and comes here instead. */         \
        case 1:                                                                                                 \
            _pblock_target = _pblock_unwind_top[0].target;                                                      \
            _pblock_unwind = !!_pblock_target;                                                                  \
            /* Execution falls through to the statement below. */                                               \
    }                                                                                                           \
    /* This odd looking construct exists to swallow the ':' after the fake PBLOCK_AFTER label. */               \
    switch(0) case 0

/// @}  // end of meta_pblock group
