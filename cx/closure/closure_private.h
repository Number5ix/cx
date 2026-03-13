#pragma once

#include <cx/thread/atomic.h>
#include <cx/thread/rwlock.h>
#include "closure.h"

typedef struct Closure {
    closureFunc func;
    int nvars;
    stvar cvars[];
} Closure;

typedef struct CChainNode CChainNode;
typedef struct CChainNode {
    CChainNode* prev;
    closureFunc func;
    intptr token;
    atomic(uint32) refcount;
    int nvars;
    stvar cvars[];
} CChainNode;

intptr _closureCompare(_In_ closure cls1, _In_ closure cls2);
