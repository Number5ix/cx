#include "stvar.h"
#include "cx/container/sarray.h"

// overwrite/init semantics: assumes *stv is uninitialized (does NOT destroy existing)
void _stvarInit(stvar* stv, stype type, stgeneric val)
{
    type = stCanonical(type);   // opaque(T)/struct(T) are Temporary literals
    if (stHasFlag(type, PassPtr)) {
        // oversized value (suid, opaque, struct): give the variant its own heap storage
        // and deep-copy into it so persisting the variant is safe and value-semantic
        void* mem      = xaAlloc(type->size);
        stgeneric dst  = { .st_ptr = mem };
        _stCopy(type, &dst, val, 0);   // honors custom copy op; else memcpy
        stv->data = dst;
        _stvarSetType(stv, type, true);   // owns
    } else {
        _stCopy(type, &stv->data, val, 0);   // string dup / object handling as today
        _stvarSetType(stv, type, false);
    }
}

// full teardown: destroy contents, free owned heap, reset to none
void _stvarClear(stvar* stv, flags_t flags)
{
    stype t = stvarType(stv);
    _stDestroy(t, &stv->data, flags);   // underlying dtor uses data.st_ptr (still clean)
    if (_stvarOwns(stv))
        xaFree(stv->data.st_ptr);   // free AFTER underlying destroy
    _stvarSetType(stv, stType(none), false);
}

// replace semantics: destroy existing contents, then initialize from type + value
void _stvarSet(stvar* stv, stype type, stgeneric val)
{
    _stvarClear(stv, 0);
    _stvarInit(stv, type, val);
}

void stvlInit(stvlist* list, int count, stvar* vars)
{
    list->count  = count;
    list->vars   = vars;
    list->cursor = 0;
}

// Initialize a list from an sarray of stvars
void _stvlInitSA(stvlist* list, stvar* vara)
{
    // slightly ugly here since we want stvar to be usable without including sarray.h
    sarrayref(stvar) vararray = { .a = vara };
    list->count               = saSize(vararray);
    list->vars                = vara;
    list->cursor              = 0;
}

// Get the next variable of the specific type, if it exists
bool _stvlNext(stvlist* list, stype type, stgeneric* out)
{
    for (int i = list->cursor; i < list->count; i++) {
        if (stEq(type, stvarType(&list->vars[i]))) {
            memcpy(stGenPtr(type, *out), stGenPtr(type, list->vars[i].data), stGetSize(type));
            list->cursor = i + 1;
            return true;
        }
    }
    return false;
}

void* _stvlNextPtr(stvlist* list, stype type)
{
    // make sure this is a type that stores a pointer in stvars
    if (!(stEq(type, stType(ptr)) || stHasFlag(type, Object) || stHasFlag(type, PassPtr)))
        return NULL;

    for (int i = list->cursor; i < list->count; i++) {
        if (stEq(type, stvarType(&list->vars[i]))) {
            list->cursor = i + 1;
            return list->vars[i].data.st_ptr;
        }
    }
    return NULL;
}

// Rewind the list
void stvlRewind(stvlist* list)
{
    list->cursor = 0;
}
