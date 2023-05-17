#pragma once

// Block wrapping using complex for statements
// Inspired by P99 preprocessor macros, modified (in some cases heavily)
// to fit with CX design philosophy.
// https://gustedt.gitlabpages.inria.fr/p99/p99-html/

#include <cx/core/stype.h>
#include <cx/platform/base.h>
#include <cx/utils/macros/tokens.h>
#include <cx/utils/macros/unused.h>
#include <setjmp.h>

// Compile-time checks for inhibited features
// This are declared here rather than a separate include file because they are co-dependent
// on the block macros, which in turn requires these to check for incorrect usage of "return"

#define _inhibit_name(name) _inhibit_##name

// Declare a feature that can be inhibited at compile time
#define inhibitDeclare(name) enum { _inhibit_name(name) = 0 }

// Results in a compile error if the specific feature is inhibited in the current block
#define inhibitCheck(name) switch(tokstring(_inhibit_name(name))[_inhibit_name(name)]) default:

// The internal versions are for contexts when we know we're guaranteed to already have a var and not
// need another _blockStart
#define _inhibitDisallow(name) _blkCond(const int * const _inhibit_name(name) = 0, !_inhibit_name(name))
// Inhibits the use of a feature inside the given block
#define inhibitDisallow(name) _blkStart _inhibitDisallow(name)
//for (const int *_inhibit_name(name) = 0; !_inhibit_name(name) && _BLK_VAR; _BLK_VAR = 0)

#define _inhibitAllow(name) _blkCond(const int _inhibit_name(name) = 0, !_inhibit_name(name))
// Allows the use of a feature inside the given block
#define inhibitAllow(name) _blkStart _inhibitAllow(name)
// for (const int _inhibit_name(name) = 0; !_inhibit_name(name) && _BLK_VAR; _BLK_VAR = 0)

// The "RETURN" feature is used internally for correctness checks

// If this is being compiled in debug/dev mode, redefine "return" to check it for correctness
#if DEBUG_LEVEL >= 1
inhibitDeclare(RETURN);
#define _inhibitReturn _inhibitDisallow(RETURN)
#define _allowReturn _inhibitAllow(RETURN)
#define return inhibitCheck(RETURN) return
#else
#define _inhibitReturn _blkStart
#define _allowReturn _blkStart
#endif

// ---------- Block Macros ----------

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

// blkWrap(before, after) {}
// Wraps the given block with "before" and "after" statements. Early exit from the block is possible
// with a break or a continue statement at the base level; but the "after" statement is executed
// even if the block is exited early.
// This version does not support fancy unwinding but is quite lightweight and suitable for use in
// most situations.
#define blkWrap(before, ...) _blkStart _blkBeforeAfter(tokeval(before), __VA_ARGS__) _blkEnd

// ---------- Unwind ----------

enum _block_unwind_const {
    _block_unwind_top = 0,
    _block_unwind_prev = 0,
};

typedef struct _block_jmp_buf_node _block_jmp_buf_node;
typedef struct _block_jmp_buf_node {
    _block_jmp_buf_node *next;                  // used for link list in try/catch code
    jmp_buf buf;                                // longjmp destination to exit the current block
    volatile int target;                        // how many levels of nesting are we unwinding
    volatile bool returning;                    // we are in the process of returning to the caller
} _block_jmp_buf_node;

typedef _block_jmp_buf_node _block_jmp_buf[1];
#define BLOCK_JMPBUF_INIT {[0] = {.returning = 0}}

typedef struct _block_inhibitor _block_inhibitor;
typedef struct _block_inhibitor {
    int dummy;
} _block_inhibitor;

_meta_inline /*noreturn*/ void _blkLongjmp(_block_jmp_buf_node *node, int val)
{
    node->target = val;
    longjmp(node->buf, 1);
}

// P99 defines a new unwind_return in every UNWIND_PROTECT block because it has no way to determine
// if it's nested inside another UNWIND_PROTECT. This wastes a lot of stack space and time initializing
// the rather large jump buffer for each block, when only the outermost one actually gets used.
// Since we only really need one, instead this implementation uses a static per-thread jump buffer
// that is reused by every blkProt in the thread.
//
// This also eliminates the need for the unwind_bottom pointer, which used the recursive macro and
// added two extra loops around every block, as well as the inhibitor to protect unwind_return.
extern _Thread_local _block_jmp_buf_node _block_unwind_return;

// blkProt {}
// Declares a protected block. It sets up a setjmp buffer for stack unwinding that can be jumped
// directly to using blkUnwind. Protected blocks can also be nested and an arbitrary number of
// layers can be broken out of.
//
// A protected block can optionally end in a section with a BLKPROT_AFTER pseudo-label. The
// instructions after this label are executed when exiting the block normally, as well as when
// the block is exited early via unwinding or a return (blkReturn must be used instead of return).
//
// This is the basic building block on which try/catch is implemented, but can be used separately
// from it.
//
// Note that protected blocks are quite heavyweight. They add a measurable amount of execution time
// both from the setjmp setup as well as the fact that the use of setjmp/longjmp inhibits numerous
// compiler optimizations from being performed on the block. Additionally, each nested block consumes
// a fairly large amount of stack space. They should be used only around complex operations that are
// not performance critical.
#define blkProt                                                                                                 \
    _blkStart                                                                                                   \
    _inhibitReturn                                                                                              \
    _blkFull(/* BEFORE */ _block_unwind_return.returning = 0, true,                                             \
        /* returning will be true in the AFTER if blkReturn was used */                                         \
        /* AFTER */ _block_unwind_return.returning ? _blkLongjmp(&_block_unwind_return, 1) : nop_stmt)          \
    /* set to true during the unwinding process */                                                              \
    _blkDef(volatile bool _block_unwind = false)                                                                \
    _blkFull(/* BEFORE */ volatile int _block_target = 0, true,                                                 \
        /* After we exit the block through whatever means, if we are unwinding, try to unwind the next level.   \
         * Since this loop is above the one that defines _block_unwind_top, the jump buffer here points to the  \
         * one set by the block that encloses this one and we'll jump back into it, giving it the ability to    \
         * run any BLKPROT_AFTER cleanup and continue the chain.                                                \
         * In the outermost block, _block_unwind_top will instead be 0 from the global enum, and blkUnwind      \
         * will do nothing.                                                                                     \
         * If _block_target reaches 0, this will also exit the loop and continue execution as normal. */        \
        /* AFTER */ (_block_unwind ?                                                                            \
            blkUnwind(_block_target <= 0 ? _block_target : _block_target - 1) : nop_stmt))                      \
    /* unwind_prev is not actually used here, but is for integration with the try/catch exceptions */           \
    _blkDef(_block_jmp_buf_node *_block_unwind_prev = _block_unwind_top)                                        \
    /* allocate a jump buffer on the stack for this level of recursion */                                       \
    _blkDef(_block_jmp_buf _block_unwind_top = tokeval(BLOCK_JMPBUF_INIT))                                      \
    /* set the longjump target to be within the inntermost loop and coerce the return value to 1 or 0 */        \
    switch(setjmp(_block_unwind_top[0].buf) ? 0 : 1)                                                            \
        /* case 2 doesn't actually exist, it's guarding a default case label that acts as a fallback for        \
         * if case 0 isn't defined, which can happen if the user does not include a BLKPROT_AFTER label. */     \
        case 2: if (0) {                                                                                        \
            default:                                                                                            \
                /* if there isn't a BLKPROT_AFTER but we got here as a longjmp target, we need to take care of  \
                 * setting up the variable for unwinding, otherwise the AFTER blocks will fail to jump to the   \
                 * next target in the chain and we'll stop unwinding early. */                                  \
                _block_target = _block_unwind_top[0].target;                                                    \
                _block_unwind = !!_block_target;                                                                \
        } else                                                                                                  \
        /* This is the case for when we get here the first time, right after calling setjmp. The user-provided  \
         * block slots in directly after this label. */                                                         \
        case 1:
    

_meta_inline void _blkUnwind(void *top, int cond)
{
    if (cond && top) _blkLongjmp(top, cond);
}

// blkUnwind(num)
// Exits the current protected block early. num may be the number of nested blocks to break out of,
// starting at 1 for the current block only. It may also be -1 which breaks out of any number of protected
// blocks and returns to the instruction immediately after the outermost layer.
#define blkUnwind(num) _blkUnwind(_block_unwind_top, (num))

// BLKPROT_AFTER:
// This pseudo-label is similar to a finally clause in exception handling. It is guaranteed to be
// executed before control flow leaves the protected block that it is contained within, regardless
// of how that occurs (except for abnormal control flow like longjmp).
#define BLKPROT_AFTER                                                                                           \
    if (0) {                                                                                                    \
        /* Even though this case 0 label is placed into the user-defined block, it technically exists within    \
         * the switch statement at the end of the blkProt macro.                                                \
         * When the user provides a BLKPROT_AFTER target, it defines this case 0 which overrides the default    \
         * case label (the one that's hidden in the if(0) after the case 2:) and comes here instead. */         \
        case 0:                                                                                                 \
            _block_target = _block_unwind_top[0].target;                                                        \
            _block_unwind = !!_block_target;                                                                    \
            /* Execution falls through to the statement below. */                                               \
    }                                                                                                           \
    /* This odd looking construct exists to swallow the ':' after the fake BLKPROT_AFTER label. */              \
    switch(0) case 0

// blkReturn expr
// Acts as a substitute for the 'return' keyword within a protected block.
// blkReturn triggers a return to the calling function, but correctly unwinds all protected blocks
// first and allows their BLKPROT sections to perform any needed cleanup.
#define blkReturn                                                                                               \
    /* Set up the thread-local unwind return buffer to jump back here after unwinding all of the                \
     * protected blocks */                                                                                      \
    if (!setjmp(_block_unwind_return.buf)) {                                                                    \
        /* Flag the blkProt loop to jump to the return buffer at the end of its AFTER phase */                  \
        _block_unwind_return.returning = 1;                                                                     \
        /* Unwind everything. */                                                                                \
        blkUnwind(-1);                                                                                          \
    }                                                                                                           \
    /* Set the returning flag back to 0 right before doing the actual return, since the return buffer is shared \
     * between all blocks in the thread and this function might have been called from within a protected block. \
     * Finally, after we've longjumped back here, return the expression that the user placed after the          \
     * blkReturn statement.                                                                                     \
     * It's important to note that the value is NOT captured before unwinding, so it may need to be volatile    \
     * if it depends on anything that changes during the unwinding process. */                                  \
    else inhibitAllow(RETURN) _blkDef(_block_unwind_return.returning = 0) return
