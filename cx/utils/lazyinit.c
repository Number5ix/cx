#include "lazyinit.h"
#include "cx/platform/cpu.h"
#include "cx/thread/atomic.h"

// extern inline void lazyInit(LazyInitState *state, LazyInitCallback initfunc, void *userData);

_Use_decl_annotations_
void _lazyInitInternal(LazyInitState* state, LazyInitCallback initfunc, void* userData)
{
    bool concurrent = atomicExchange(bool, &state->initProgress, true, AcqRel);
    if (!concurrent) {
        // we are the first thread to try to initialize
        initfunc(userData);
        atomicStore(bool, &state->init, true, Release);
    } else {
        // another thread is performing the initialization
        // spin until it finishes
        while (!atomicLoad(bool, &state->init, Acquire)) _CPU_PAUSE;
    }
}
