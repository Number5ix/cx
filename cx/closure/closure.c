#include "closure_private.h"
#include <cx/debug/assert.h>
#include <cx/stype/stype_stvar.h>

static closure closureAlloc(void (*func)(void), const char* sig, int n, stvar cvars[])
{
    Closure* c = xaAlloc(sizeof(Closure) + n * sizeof(stvar));
    c->func    = func;
    c->sig     = sig;
    c->nvars   = n;
    for (int i = 0; i < n; i++) {
        stvarCopy(&c->cvars[i], cvars[i]);
    }
    return (closure)c;
}

_Use_decl_annotations_
closure _closureCreate(closureFunc func, int n, stvar cvars[])
{
    return closureAlloc((void (*)(void))func, NULL, n, cvars);
}

_Use_decl_annotations_
closure _closureCreateAs(void (*func)(void), const char* sig, int n, stvar cvars[])
{
    return closureAlloc(func, sig, n, cvars);
}

_Use_decl_annotations_
closure closureClone(closure cls)
{
    if (!cls)
        return NULL;

    Closure* src = (Closure*)cls;
    return closureAlloc(src->func, src->sig, src->nvars, src->cvars);
}

_Use_decl_annotations_
bool _closureCall(closure cls, int n, stvar args[])
{
    Closure* c = (Closure*)cls;
    devAssertMsg(!c->sig, "closureCall() on a typed closure; call it with closureCallAs()");

    stvlist stv_cvars;
    stvlist stv_args;
    stvlInit(&stv_cvars, c->nvars, c->cvars);
    stvlInit(&stv_args, n, args);
    bool ret = ((closureFunc)c->func)(&stv_cvars, &stv_args);

    return ret;
}

_Use_decl_annotations_
void (*_closureFuncAs(closure cls, const char* sig))(void)
{
    Closure* c = (Closure*)cls;
    devAssertMsg(c->sig && (c->sig == sig || cstrEq(c->sig, sig)),
                 "closureCallAs() with a different signature than the closure was created with");
    return c->func;
}

_Use_decl_annotations_
stvlist* _closureCvars(closure cls, stvlist* storage)
{
    Closure* c = (Closure*)cls;
    stvlInit(storage, c->nvars, c->cvars);
    return storage;
}

_Use_decl_annotations_
intptr _closureCompare(_In_ closure cls1, _In_ closure cls2)
{
    Closure* c1 = (Closure*)cls1;
    Closure* c2 = (Closure*)cls2;

    if (c1->func != c2->func)
        return (intptr)c1->func - (intptr)c2->func;

    if (c1->nvars != c2->nvars)
        return c1->nvars - c2->nvars;

    for (int i = 0; i < c1->nvars; i++) {
        intptr res = stCmp(stvar, c1->cvars[i], c2->cvars[i]);
        if (res != 0)
            return res;
    }

    return 0;
}

_Use_decl_annotations_
void closureDestroy(closure* cls)
{
    // Tolerating an unset closure is what lets cxautogen emit an unconditional call to this in
    // the destructor of any class with a closure member, without every such class having to
    // guarantee the field was ever assigned.
    if (!cls || !*cls)
        return;

    Closure* c = (Closure*)(*cls);

    for (int i = c->nvars - 1; i >= 0; --i) {
        stvarDestroy(&c->cvars[i]);
    }

    xaFree(*cls);
    *cls = NULL;
}
