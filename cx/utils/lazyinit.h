#pragma once

#include <cx/cx.h>

_EXTERN_C_BEGIN

typedef struct LazyInitState {
    bool init;
    bool initProgress;
} LazyInitState;

typedef void(*LazyInitCallback)(void *userData);

void _lazyInitInternal(bool *init, bool *initProgress, LazyInitCallback initfunc, void *userData);

// Calls the function initfunc() exactly once in a thread-safe manner.
// All simultaneous threads are guarnteed to not pass this point until
// after initfunc() has been completely executed by one of them.
// This operation becomes very cheap once initialization is complete
// and all caches have synchronized.
_meta_inline void lazyInit(LazyInitState *state, LazyInitCallback initfunc, void *userData)
{
    if (!state->init)
        _lazyInitInternal(&state->init, &state->initProgress, initfunc, userData);
}

_EXTERN_C_END
