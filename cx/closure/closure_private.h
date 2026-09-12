#pragma once

#include <cx/thread/atomic.h>
#include <cx/thread/rwlock.h>
#include "closure.h"

typedef struct Closure {
    // Type-erased: a closureFunc for a generic closure, or whatever signature it was created
    // with for a typed one. Only ever called through a cast back to the type in `sig`.
    void (*func)(void);
    // NULL for a generic closure, otherwise the name of the typed signature it was created with.
    // Checked on every call in debug builds, since the call-site cast cannot be.
    const char* sig;
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
