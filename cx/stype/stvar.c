#include "stvar.h"
#include "cx/container/sarray.h"

// overwrite/init semantics: assumes *stv is uninitialized (does NOT destroy existing)
// does NOT touch the key name -- see _stvarInitK
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

// as _stvarInit, but also carries the key name across. The name is pointer-copied rather
// than duplicated -- stvark() stringizes a token and stvarkn() documents the contract, so
// it always points at storage that outlives the variant.
void _stvarInitK(stvar* stv, stype type, stgeneric val, const char* vk)
{
    _stvarInit(stv, type, val);
    _stvarSetKey(stv, vk);
}

// full teardown: destroy contents, free owned heap, reset to none
void _stvarClear(stvar* stv, flags_t flags)
{
    stype t = stvarType(stv);
    _stDestroy(t, &stv->data, flags);   // underlying dtor uses data.st_ptr (still clean)
    if (_stvarOwns(stv))
        xaFree(stv->data.st_ptr);   // free AFTER underlying destroy
    _stvarSetType(stv, stType(none), false);
    _stvarSetKey(stv, NULL);
}

// replace semantics: destroy existing contents, then initialize from type + value.
// clears any existing key -- the new value is a different value, so a stale key would be
// worse than none. Use stvarSetK/_stvarSetK to replace value and key together.
void _stvarSet(stvar* stv, stype type, stgeneric val)
{
    _stvarClear(stv, 0);
    _stvarInit(stv, type, val);
}

void _stvarSetK(stvar* stv, stype type, stgeneric val, const char* nm)
{
    _stvarClear(stv, 0);
    _stvarInitK(stv, type, val, nm);
}

void* _stvarPrepare(stvar* stv, stype type)
{
    _stvarClear(stv, 0);
    type = stCanonical(type);

    if (stHasFlag(type, PassPtr)) {
        void* mem     = xaAlloc(type->size, XA_Zero);
        stv->data.st_ptr = mem;
        _stvarSetType(stv, type, true);   // owns
        return mem;
    }

    stv->data = (stgeneric) { 0 };
    _stvarSetType(stv, type, false);
    return &stv->data;
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

// A keyed variant is addressed by name only, never positionally. If a positional walk
// could consume keyed arguments, adding a keyed argument to a call would silently shift
// every same-typed positional argument after it -- exactly the fragility keys exist to
// remove. Keeping the two addressing modes disjoint means a caller can add a keyed
// argument to an existing call without touching anything else.
#define stvlSkip(v) (stvarKey(v) != NULL)

// Get the next variable of the specific type, if it exists
bool _stvlNext(stvlist* list, stype type, stgeneric* out)
{
    for (int i = list->cursor; i < list->count; i++) {
        if (!stvlSkip(&list->vars[i]) && stEq(type, stvarType(&list->vars[i]))) {
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
        if (!stvlSkip(&list->vars[i]) && stEq(type, stvarType(&list->vars[i]))) {
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

// Find the index of the variant carrying a key, or -1. Scans the whole list from the
// start and never touches the cursor -- keyed arguments are order-free by design, which
// is the opposite of _stvlNext's find-forward-and-skip contract.
static int findKey(stvlist list, const char* key)
{
    int found = -1;

    if (!key)
        return -1;

    for (int i = 0; i < list.count; i++) {
        if (cstrEq(key, stvarKey(&list.vars[i]))) {
            if (found == -1) {
                found = i;
#if DEBUG_LEVEL < 1 && !defined(DIAGNOSTIC)
                break;   // release: first match wins, stop looking
#endif
            } else {
                // duplicates resolve to the first match; the compiler cannot catch this,
                // so complain loudly in debug builds rather than silently picking one
                devAssertMsg(false, "duplicate key in stvar argument list");
                break;
            }
        }
    }
    return found;
}

bool _stvlFind(stvlist list, const char* key, stype type, stgeneric* out)
{
    int idx = findKey(list, key);
    if (idx == -1)
        return false;

    stvar* var = &list.vars[idx];
    if (!stEq(type, stvarType(var)))
        return false;

    memcpy(stGenPtr(type, *out), stGenPtr(type, var->data), stGetSize(type));
    return true;
}

void* _stvlFindPtr(stvlist list, const char* key, stype type)
{
    // make sure this is a type that stores a pointer in stvars
    if (!(stEq(type, stType(ptr)) || stHasFlag(type, Object) || stHasFlag(type, PassPtr)))
        return NULL;

    int idx = findKey(list, key);
    if (idx == -1)
        return NULL;

    stvar* var = &list.vars[idx];
    if (!stEq(type, stvarType(var)))
        return NULL;

    return var->data.st_ptr;
}

bool _stvlHasKey(stvlist list, const char* key)
{
    return findKey(list, key) != -1;
}
