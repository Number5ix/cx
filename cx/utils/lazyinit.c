#include "lazyinit.h"
#include "cx/thread/atomic.h"
#include "cx/platform/cpu.h"

// extern inline void lazyInit(LazyInitState *state, LazyInitCallback initfunc, void *userData);

void _lazyInitInternal(bool *init, bool *initProgress, LazyInitCallback initfunc, void *userData)
{
    bool concurrent = atomic_exchange_bool((atomic_bool_t*)initProgress, true, ATOMIC_ACQ_REL);
    if (!concurrent) {
        // we are the first thread to try to initialize
        initfunc(userData);
        atomic_store_bool((atomic_bool_t*)init, true, ATOMIC_RELAXED);
    } else {
        // another thread is performing the initialization
        // spin until it finishes
        while (!atomic_load_bool((atomic_bool_t*)init, ATOMIC_RELAXED))
            _CPU_PAUSE;
    }
}
