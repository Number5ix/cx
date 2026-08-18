// Lifetime, error and resolver plumbing shared by every backend and both directions of the
// traverser. The format-specific work lives in the backends; the traversal lives in sertraverse.c.

#include "cx/serialize/serreader.h"
#include "cx/serialize/serwriter.h"

#include "cx/string.h"
#include "cx/xalloc/xalloc.h"

// ---------------------------------------------------------------------------------------
// Errors and the path stack
// ---------------------------------------------------------------------------------------

_Use_decl_annotations_
void serErrorDestroy(SerError* err)
{
    if (!err)
        return;
    strDestroy(&err->msg);
    strDestroy(&err->path);
    err->code = SER_Err_None;
}

_Use_decl_annotations_
void serPathString(string* out, const SerTraverseState* ws)
{
    strClear(out);

    int32 n = saSize(ws->path);
    if (n == 0) {
        strDup(out, _S"/");
        return;
    }

    string tmp = 0;
    for (int32 i = 0; i < n; i++) {
        const SerPathFrame* f = &ws->path.a[i];
        if (f->name) {
            strAppend(out, _S"/");
            strAppend(out, f->name);
        } else {
            strFromInt32(&tmp, f->idx, 10);
            strAppend(out, _S"[");
            strAppend(out, tmp);
            strAppend(out, _S"]");
        }
    }
    strDestroy(&tmp);
}

_Use_decl_annotations_
void _serTraverseStateDestroy(SerTraverseState* ws)
{
    // SerError is carried in the collected array as an opaque POD, so its strings are not
    // reached by the array's element destructor -- there isn't one.
    for (int32 i = 0; i < saSize(ws->collected); i++)
        serErrorDestroy(&ws->collected.a[i]);

    saDestroy(&ws->collected);
    saDestroy(&ws->path);
}

// Record an error. Only the first one is kept in `err`; under SER_Collect every one is also
// appended to the collected list. The path is materialized here and nowhere else, which is
// what keeps the success path free of string building.
static bool serRaise(_Inout_ SerError* err, _Inout_ SerTraverseState* ws, flags_t flags, int32 code,
                     _In_opt_ strref msg)
{
    SerError e = { .code = code };
    strDup(&e.msg, msg);
    serPathString(&e.path, ws);

    if (flags & SER_Collect)
        saPush(&ws->collected, opaque, e);

    if (err->code == SER_Err_None) {
        err->code = code;
        strDup(&err->msg, e.msg);
        strDup(&err->path, e.path);
    }

    if (!(flags & SER_Collect))
        serErrorDestroy(&e);

    return false;
}

_Use_decl_annotations_
bool serWriterFail(SerWriter* w, int32 code, strref msg)
{
    return serRaise(&w->err, &w->traverse, w->flags, code, msg);
}

_Use_decl_annotations_
bool serReaderFail(SerReader* r, int32 code, strref msg)
{
    return serRaise(&r->err, &r->traverse, r->flags, code, msg);
}

// ---------------------------------------------------------------------------------------
// Writer lifetime
// ---------------------------------------------------------------------------------------

_Use_decl_annotations_
SerWriter* _serWriterAlloc(size_t size, const SerWriterOps* ops, flags_t caps, flags_t flags)
{
    devAssert(size >= sizeof(SerWriter));

    SerWriter* w = xaAlloc(size, XA_Zero);
    w->ops       = ops;
    w->caps      = caps;
    w->flags     = flags;
    saInit(&w->traverse.path, opaque(SerPathFrame), 8);
    saInit(&w->inprogress, ptr, 8);

    return w;
}

_Use_decl_annotations_
bool serWriterFinish(SerWriter* w)
{
    if (!w)
        return false;
    if (w->finished)
        return w->err.code == SER_Err_None;

    w->finished = true;

    if (w->ops->finish && !w->ops->finish(w))
        return false;

    return w->err.code == SER_Err_None;
}

_Use_decl_annotations_
void serWriterDestroy(SerWriter** w)
{
    if (!w || !*w)
        return;

    SerWriter* wr = *w;
    serErrorDestroy(&wr->err);
    _serTraverseStateDestroy(&wr->traverse);
    saDestroy(&wr->inprogress);
    htDestroy(&wr->refids);

    // The backend frees the struct itself, so it stays free to allocate from somewhere other
    // than xalloc.
    wr->ops->destroy(wr);
    *w = NULL;
}

// ---------------------------------------------------------------------------------------
// Reader lifetime and type resolution
// ---------------------------------------------------------------------------------------

_Use_decl_annotations_
SerReader* _serReaderAlloc(size_t size, const SerReaderOps* ops, flags_t caps, flags_t flags)
{
    devAssert(size >= sizeof(SerReader));

    SerReader* r = xaAlloc(size, XA_Zero);
    r->ops       = ops;
    r->caps      = caps;
    r->flags     = flags;
    saInit(&r->traverse.path, opaque(SerPathFrame), 8);

    return r;
}

_Use_decl_annotations_
void serReaderDestroy(SerReader** r)
{
    if (!r || !*r)
        return;

    SerReader* rd = *r;
    serErrorDestroy(&rd->err);
    _serTraverseStateDestroy(&rd->traverse);
    saDestroy(&rd->resolvers);
    htDestroy(&rd->refs);

    rd->ops->destroy(rd);
    *r = NULL;
}

_Use_decl_annotations_
void serReaderAddResolver(SerReader* r, SerTypeResolver fn, void* user)
{
    if (!r || !fn)
        return;

    if (!r->resolvers.a)
        saInit(&r->resolvers, opaque(SerResolverEnt), 4);

    saPush(&r->resolvers, opaque, ((SerResolverEnt) { .fn = fn, .user = user }));
}

_Use_decl_annotations_
bool serReaderResolve(SerResolved* out, SerReader* r, strref name)
{
    memset(out, 0, sizeof(SerResolved));

    for (int32 i = 0; i < saSize(r->resolvers); i++) {
        if (r->resolvers.a[i].fn(out, name, r->resolvers.a[i].user))
            return true;
    }

    // cx's own structural names run last, so an application resolver can shadow one if it
    // really means to.
    return _serResolveBuiltin(out, name);
}
