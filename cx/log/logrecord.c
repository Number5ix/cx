// absolutely NEVER debug locks in this file because lock debugging calls log*
#ifdef CX_LOCK_DEBUG
#undef CX_LOCK_DEBUG
#endif

#include "log_private.h"
#include <cx/format.h>
#include <cx/string.h>

// Rendering a record to flat text.
//
// This is the whole of what a text destination has to do that a structured one does not, and it
// runs on the drain thread. Deferring it is the point: the thread that logged the message paid
// only for copying the arguments, and a record that reaches no text destination at all is never
// formatted.
//
// The rendering is cached per dispatch rather than per destination, because several text
// destinations receiving the same record is the normal case -- a console and a log file, say --
// and formatting the same template twice for them would give back most of what deferral bought.

// How many merged arguments a record can carry before rendering has to reach for the heap. Sized
// so that a call site's own arguments plus a request-scoped context fit without allocating, which
// is every realistic record; the drain thread has the stack for it.
#define LOG_RENDER_INLINE_ARGS 24

// Builds the argument list a template is formatted against: the record's own arguments, followed
// by the keyed fields of its context.
//
// Context fields are visible to the template because a keyed variant is invisible to an unkeyed
// placeholder, so appending them cannot renumber or otherwise disturb the positional arguments
// the call site wrote. A template that names none of them renders exactly as it did before.
//
// Two things are dropped rather than merged. An unkeyed context field would land in the
// positional sequence and renumber the template's own arguments, and a context field whose key an
// argument already uses is the less specific of the two -- the same precedence the NDJSON
// serializer emits them with, and the one the formatter's duplicate-key assertion requires.
static _Ret_valid_ stvar* logRenderArgs(_Out_ int* nout, _Out_writes_(ninl) stvar* inl, int ninl,
                                        _In_ const LogRecord* rec)
{
    int total = rec->nargs;
    for (const LogCtx* c = rec->ctx; c; c = logCtxParent(c))
        total += (int)logCtxNumVars(c);

    stvar* args = (total <= ninl) ? inl : xaAlloc(sizeof(stvar) * (size_t)total);
    int n       = 0;

    // Shallow copies throughout: the record owns its arguments and the context node owns its
    // fields, both outlive the render, and the formatter only reads.
    for (int i = 0; i < rec->nargs; i++)
        args[n++] = rec->args[i];

    for (const LogCtx* c = rec->ctx; c; c = logCtxParent(c)) {
        const stvar* vars = logCtxVars(c);
        uint32 nv         = logCtxNumVars(c);

        for (uint32 i = 0; i < nv; i++) {
            const char* key = stvarKey(&vars[i]);
            if (!key || logCtxShadowed(rec->ctx, c, i, key))
                continue;

            bool shadowed = false;
            for (int j = 0; j < rec->nargs; j++) {
                if (cstrEq(stvarKey(&rec->args[j]), key)) {
                    shadowed = true;
                    break;
                }
            }
            if (!shadowed)
                args[n++] = vars[i];
        }
    }

    *nout = n;
    return args;
}

_Use_decl_annotations_
void logRecordRender(string* out, const LogRecord* rec)
{
    if (rec->_cache && rec->_cache->valid) {
        strDup(out, rec->_cache->str);
        return;
    }

    stvar inl[LOG_RENDER_INLINE_ARGS];
    stvar* args = (stvar*)rec->args;
    int nargs   = rec->nargs;

    // Only a template has anything to substitute. A logStr() message is literal, so one that
    // happens to contain ${...} has to come out the way it went in whether or not a context is in
    // scope -- and with context fields merged in, argument count no longer tells the two apart.
    if (rec->ctx && rec->istmpl)
        args = logRenderArgs(&nargs, inl, LOG_RENDER_INLINE_ARGS, rec);

    if (rec->istmpl)
        _strFormat(out, rec->msgtmpl, nargs, args);
    else
        strDup(out, rec->msgtmpl);   // the template is already the literal message

    if (args != rec->args && args != inl)
        xaFree(args);

    if (rec->_cache) {
        strDup(&rec->_cache->str, *out);
        rec->_cache->valid = true;
    }
}
