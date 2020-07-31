#pragma once

#include <cx/cx.h>

// Note that this uses a temporary, so the variant's lifetime is equal to the function
// scope that it was created in. It should be used for passing varargs by-value.
#ifndef __cplusplus
#define stvarInit(typen, val) { .data = { .st_##typen = val }, .type = stType(typen) }
#define stvar(typen, val) ((stvar){ .data = stArg(typen, val), .type = stType(typen) })

#define stvNone ((stvar){ .type = stType(none) })
#else
_meta_inline stvar _stvar(stype st, stgeneric val)
{
    stvar ret;
    ret.data = val;
    ret.type = st;
    return ret;
}
#define stvar(typen, val) _stvar(stType(typen), stArg(typen, val))

#define stvNone _stvar(0, stArg(int64, 0))
#endif

// Structure for walking a list of stvars with the convenience functions
typedef struct stvlist {
    int count;
    int cursor;
    stvar *vars;
} stvlist;

// Initialize a list from an array of stvars and a count -- i.e. from a pseudovariadic
// function's argument list
void stvlInit(stvlist *list, int count, stvar *vars);

// Initialize a list from an sarray of stvars
void stvlInitSA(stvlist *list, stvar *vararray);

// Get the next variable of the specific type, if it exists
bool _stvlNext(stvlist *list, stype type, stgeneric *out);
#define stvlNext(list, type, pvar) _stvlNext(list, stCheckedPtr(type, pvar))

// Rewind the list
void stvlRewind(stvlist *list);
