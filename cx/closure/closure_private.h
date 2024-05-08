#pragma once

#include "closure.h"
#include <cx/thread/atomic.h>
#include <cx/thread/rwlock.h>

typedef struct Closure
{
    closureFunc func;
    int nvars;
    stvar cvars[];
} Closure;

typedef struct ClosureChain ClosureChain;
typedef struct ClosureChain
{
    atomic(ptr) prev;
    closureFunc func;
    intptr token;
    int nvars;
    stvar cvars[];
} ClosureChain;

intptr _closureCompare(_In_ closure cls1, _In_ closure cls2);
intptr _cchainCompare(_In_ cchain cls1, _In_ cchain cls2);
