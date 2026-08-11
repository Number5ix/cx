// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/string.h>

// Per-thread log context.
//
// A context is an immutable node holding its own fields and a reference to the one it was
// entered from, so entering costs a single allocation and a record snapshots the entire chain by
// taking one reference. Nothing is copied per record, which is what makes it affordable to
// attach correlation fields to everything a request touches.
//
// The refcount is atomic because a record outlives the block that logged it and is released on
// the drain thread, and because a context handed to a task queue is released by whichever
// worker ran the task. The node contents never change after construction, so nothing else needs
// synchronizing.

typedef struct LogCtx {
    struct LogCtx* parent;   // reference owned by this node
    atomic(uint32) refs;
    uint32 nvars;
    stvar vars[];            // nvars entries, allocated with the header
} LogCtx;

static _Thread_local LogCtx* _log_ctx;

_Use_decl_annotations_
LogCtx* logCtxAcquire(LogCtx* ctx)
{
    if (ctx)
        atomicFetchAdd(uint32, &ctx->refs, 1, Relaxed);
    return ctx;
}

_Use_decl_annotations_
void logCtxRelease(LogCtx** ctx)
{
    LogCtx* c = *ctx;
    *ctx      = NULL;

    // iterative rather than recursive: a deep context chain released all at once would otherwise
    // put its depth on the stack, and depth is chosen by whoever nests withLogCtx blocks
    while (c) {
        if (atomicFetchSub(uint32, &c->refs, 1, AcqRel) != 1)
            return;

        LogCtx* parent = c->parent;
        for (uint32 i = 0; i < c->nvars; i++)
            stvarDestroy(&c->vars[i]);
        xaFree(c);
        c = parent;
    }
}

_Use_decl_annotations_
void _logCtxPush(int n, stvar* vars)
{
    if (n < 0)
        n = 0;

    LogCtx* ctx = xaAlloc(sizeof(LogCtx) + sizeof(stvar) * (size_t)n, XA_Zero);
    ctx->parent = _log_ctx;   // adopts the reference the thread was holding
    ctx->nvars  = (uint32)n;
    atomicStore(uint32, &ctx->refs, 1, Relaxed);

    // The variants are deep-copied for the same reason a record's arguments are: the caller's
    // stvar array is a compound literal that dies at the end of the statement, and the context
    // has to outlive the whole block.
    for (int i = 0; i < n; i++)
        stvarCopy(&ctx->vars[i], vars[i]);

    _log_ctx = ctx;
}

void logCtxPop(void)
{
    LogCtx* top = _log_ctx;
    if (!top)
        return;

    // the popped node owns the reference to its parent, so the thread has to take its own before
    // letting the node go
    _log_ctx = logCtxAcquire(top->parent);
    logCtxRelease(&top);
}

_Use_decl_annotations_
LogCtx* logCtxCurrent(void)
{
    return _log_ctx;
}

_Use_decl_annotations_
LogCtx* logCtxSwap(LogCtx* ctx)
{
    LogCtx* prev = _log_ctx;
    _log_ctx     = logCtxAcquire(ctx);
    return prev;   // reference transfers to the caller
}

_Use_decl_annotations_
void logCtxRestore(LogCtx* ctx)
{
    LogCtx* cur = _log_ctx;
    _log_ctx    = ctx;
    logCtxRelease(&cur);
}

_Use_decl_annotations_
const LogCtx* logCtxParent(const LogCtx* ctx)
{
    return ctx->parent;
}

_Use_decl_annotations_
uint32 logCtxNumVars(const LogCtx* ctx)
{
    return ctx->nvars;
}

_Use_decl_annotations_
const stvar* logCtxVars(const LogCtx* ctx)
{
    return ctx->vars;
}

_Use_decl_annotations_
bool logCtxShadowed(const LogCtx* top, const LogCtx* node, uint32 idx, const char* key)
{
    // Walks from the innermost node down to the candidate's own position, asking whether this key
    // already appeared. Quadratic, but contexts hold a handful of fields and this avoids carrying
    // a scratch set around; the alternative costs an allocation per record rendered.
    for (const LogCtx* c = top; c; c = c->parent) {
        uint32 n = (c == node) ? idx : c->nvars;
        for (uint32 i = 0; i < n; i++) {
            if (cstrEq(stvarKey(&c->vars[i]), key))
                return true;
        }
        if (c == node)
            break;
    }
    return false;
}
