#pragma once
/// @file log/logctx.h
/// @brief Per-thread log context: fields every record on this thread inherits

/// @defgroup log_ctx Log Context
/// @ingroup log
/// @{
///
/// Correlation fields attached to everything logged on a thread, without threading a context
/// object through every function that might log:
///
/// @code
///   withLogCtx (stvark(reqid, suid, id), stvark(tenant, string, tenant)) {
///       handleRequest(req);   // every record logged in here carries reqid and tenant
///   }
/// @endcode
///
/// Contexts are **immutable and shared**. Entering one allocates a single node pointing at the
/// enclosing context; a record snapshots the whole chain by taking one reference. Nothing is
/// copied per record, and a record that outlives the block it was logged in still renders the
/// fields that were in scope.
///
/// Fields are keyed variants, the same ones logFmt() takes (see stvark()). Structured
/// destinations emit all of them; text destinations render a configured subset, or none, so a
/// console does not fill up with correlation ids.
///
/// **A message template can name them too**, under the same keys, so a correlation id can appear
/// in the sentence rather than only in the annotation a destination appends:
///
/// @code
///   withLogCtx (stvark(reqid, string, id)) {
///       logFmt(Info, _SL("handling ${string:reqid}"), stvNone);
///   }
/// @endcode
///
/// This costs the call site nothing it was not already paying: a keyed variant is invisible to an
/// unkeyed placeholder, so context fields cannot renumber or otherwise disturb the arguments the
/// call site wrote. An argument sharing a key with a context field wins, being the more specific
/// of the two. A key that is in no context in scope leaves the placeholder unmatched, which fails
/// the format like any other unmatched placeholder unless it carries a `;default`.
///
/// Only logFmt() messages are templates. A logStr() message is literal, so one containing
/// `${...}` is delivered unchanged whether or not a context is in scope.
///
/// **Nesting shadows rather than merges.** An inner context that reuses a key wins; the outer
/// value is still there but is not emitted twice.
///
/// **Work that hops threads keeps its context.** A task inherits the context of whoever
/// submitted it, restored for the duration of its run and undone afterwards. Without that,
/// correlation is lost the moment work is handed to a queue, which in an async server is
/// immediately.

#include <cx/log/log.h>
#include <cx/meta/block.h>
#include <cx/utils/macros.h>

CX_C_BEGIN

/// void withLogCtx(...)
///
/// Runs the following block with additional fields attached to everything logged on this thread
///
/// The context is popped when the block exits, including via `break` or `continue`. `return` is
/// not allowed inside the block, as with every block-wrapping macro in cx.
///
/// @param ... Keyed variants, e.g. stvark(reqid, suid, id)
/// @code
///   withLogCtx (stvark(reqid, suid, id)) {
///       logStr(Info, _SL("started"));   // carries reqid
///   }
/// @endcode
#define withLogCtx(...)                                                   \
    blkWrap (_logCtxPush(count_macro_args(__VA_ARGS__),                   \
                         (stvar[]) { __VA_ARGS__ }),                      \
             logCtxPop())

// Internal; use withLogCtx()
void _logCtxPush(int n, _In_ stvar* vars);

/// Leave the innermost context on this thread
///
/// Only needed when the enter/leave pair cannot be a block; prefer withLogCtx().
void logCtxPop(void);

/// The calling thread's current context, or NULL if it has none
///
/// Borrowed: valid until the thread leaves the context. Acquire it to keep it.
///
/// @return Current context, or NULL
_Ret_opt_valid_ LogCtx* logCtxCurrent(void);

/// Take a reference to a context
///
/// @param ctx Context to acquire, or NULL
/// @return The same context, or NULL
_Ret_opt_valid_ LogCtx* logCtxAcquire(_In_opt_ LogCtx* ctx);

/// Release a reference to a context
///
/// @param ctx Context to release; set to NULL
void logCtxRelease(_Inout_ LogCtx** ctx);

/// Replace this thread's context wholesale
///
/// For carrying a context across a thread boundary: capture logCtxCurrent() where the work is
/// submitted, then swap it in where the work runs. Ownership of the **returned** context
/// transfers to the caller, who must hand it to logCtxRestore().
///
/// @param ctx Context to install, or NULL for none
/// @return The context that was installed before, or NULL
/// @code
///   LogCtx *prev = logCtxSwap(task->logctx);
///   runTask(task);
///   logCtxRestore(prev);
/// @endcode
_Ret_opt_valid_ LogCtx* logCtxSwap(_In_opt_ LogCtx* ctx);

/// Put back a context taken by logCtxSwap()
///
/// Consumes the reference logCtxSwap() handed out.
///
/// @param ctx Context returned by logCtxSwap(), or NULL
void logCtxRestore(_In_opt_ LogCtx* ctx);

/// The context this one was entered from, or NULL at the outermost
///
/// @param ctx Context to inspect
/// @return Enclosing context, or NULL
_Ret_opt_valid_ const LogCtx* logCtxParent(_In_ const LogCtx* ctx);

/// How many fields this context node adds
///
/// Does not include fields inherited from logCtxParent(); walk the chain for the full set.
///
/// @param ctx Context to inspect
/// @return Number of fields
uint32 logCtxNumVars(_In_ const LogCtx* ctx);

/// This context node's fields
///
/// @param ctx Context to inspect
/// @return Array of logCtxNumVars() keyed variants
/// @code
///   for (const LogCtx *c = rec->ctx; c; c = logCtxParent(c)) {
///       const stvar *vars = logCtxVars(c);
///       for (uint32 i = 0; i < logCtxNumVars(c); i++)
///           emitField(stvarKey(&vars[i]), &vars[i]);
///   }
/// @endcode
_Ret_valid_ const stvar* logCtxVars(_In_ const LogCtx* ctx);

/// Has this key already been seen by a walk in progress?
///
/// Nesting shadows: an inner context that reuses a key wins, and the outer one should not be
/// emitted a second time. A destination walking the chain from `top` calls this for each
/// candidate field to find out whether a more deeply nested context already supplied it.
///
/// @param top Context the walk started at (the record's context)
/// @param node Context node the candidate field belongs to
/// @param idx Index of the candidate field within that node
/// @param key The candidate field's key
/// @return true if the key already appeared, so this field should be skipped
/// @code
///   for (const LogCtx *c = rec->ctx; c; c = logCtxParent(c)) {
///       const stvar *vars = logCtxVars(c);
///       for (uint32 i = 0; i < logCtxNumVars(c); i++) {
///           const char *key = stvarKey(&vars[i]);
///           if (!key || logCtxShadowed(rec->ctx, c, i, key))
///               continue;
///           emitField(key, &vars[i]);
///       }
///   }
/// @endcode
bool logCtxShadowed(_In_ const LogCtx* top, _In_ const LogCtx* node, uint32 idx,
                    _In_opt_z_ const char* key);

/// @}  // end of log_ctx group

CX_C_END
