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

typedef struct CChainNode CChainNode;
typedef struct CChainNode
{
    CChainNode *prev;
    closureFunc func;
    intptr token;
    atomic(uint32) refcount;
    int nvars;
    stvar cvars[];
} CChainNode;

intptr _closureCompare(_In_ closure cls1, _In_ closure cls2);
