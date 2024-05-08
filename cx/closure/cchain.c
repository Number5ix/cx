#include "cchain.h"
#include "closure_private.h"
#include <cx/utils/lazyinit.h>

// global lock because it's the least complicated way to be able to safely destroy a
// closure chain. The lock cannot be part of the chain itself because there will always be
// a gap between retrieving the pointer and accessing the lock.

// Despite it being global, it's relatively low-contention because write locks are only
// taken when destroying a chain.

// It's not exactly being used as a read-write lock as we do modify state while holding the
// read lock (which is why atomics are used for the pointers). The write lock is used only
// as an exclusive mode for deallocating memory.

static LazyInitState cchainLock_state;
static RWLock cchainLock;

static void cchainLock_init(void *dummy)
{
    rwlockInit(&cchainLock);
}

_Use_decl_annotations_
void _cchainAttach(cchain *chain, closureFunc func, intptr token, int n, stvar cvars[])
{
    lazyInit(&cchainLock_state, cchainLock_init, NULL);

    atomic(ptr) *curptr = (atomic(ptr) *)chain;

    // ensure tokens are unique
    if(token)
        cchainDetach(chain, func, token);

    ClosureChain *nchain = xaAlloc(sizeof(ClosureChain) + sizeof(stvar) * (n + (token ? 1 : 0)));
    nchain->func = func;
    nchain->nvars = n + (token ? 1 : 0);
    nchain->token = token;

    for(int i = 0; i < n; i++) {
        stvarCopy(&nchain->cvars[i], cvars[i]);
    }
    if(token)
        nchain->cvars[n] = stvar(intptr, token);

    withReadLock(&cchainLock)
    {
        ClosureChain *cur = atomicLoad(ptr, curptr, Relaxed);
        do {
            atomicStore(ptr, &nchain->prev, cur, Relaxed);
        } while(!atomicCompareExchange(ptr, weak, curptr, &cur, nchain, AcqRel, Relaxed));
    }
}

_Use_decl_annotations_
bool cchainDetach(cchain *chain, closureFunc func, intptr token)
{
    lazyInit(&cchainLock_state, cchainLock_init, NULL);

    atomic(ptr) *curptr = (atomic(ptr) *)chain;
    ClosureChain *cur;
    bool ret = true;

    withReadLock(&cchainLock)
    {
        do {
            // walk up the list to find the callback to remove
            cur = atomicLoad(ptr, curptr, Relaxed);
            while(cur) {
                if(cur->func == func &&
                   cur->token == token)
                    break;
                curptr = &cur->prev;
                cur = atomicLoad(ptr, curptr, Relaxed);
            }

            if(!cur) {
                ret = false;
                break;
            }
        } while(!atomicCompareExchange(ptr, strong, curptr, &cur, atomicLoad(ptr, &cur->prev, Relaxed), AcqRel, Relaxed));
    }

    if(ret) {
        atomicStore(ptr, &cur->prev, NULL, Relaxed);        // ensure we destroy only the removed closure
        cchainDestroy((cchain*)&cur);
    }

    return ret;
}

_Use_decl_annotations_
void cchainClone(cchain *destchain, cchain *srcchain)
{
    lazyInit(&cchainLock_state, cchainLock_init, NULL);

    atomic(ptr) *dptr = (atomic(ptr) *)destchain;
    ClosureChain *src;
    ClosureChain *dest = NULL;

    withReadLock(&cchainLock)
    {
        src = atomicLoad(ptr, (atomic(ptr) *)srcchain, Relaxed);
        while(src) {
            dest = xaAlloc(sizeof(ClosureChain) + src->nvars * sizeof(stvar));
            dest->func = src->func;
            dest->token = src->token;
            dest->nvars = src->nvars;

            for(int i = 0; i < src->nvars; i++) {
                stvarCopy(&dest->cvars[i], src->cvars[i]);
            }

            atomicStore(ptr, dptr, dest, Relaxed);
            dptr = &dest->prev;
            src = atomicLoad(ptr, &src->prev, Relaxed);
        }

        atomicStore(ptr, dptr, NULL, Relaxed);
    }
}

_Use_decl_annotations_
bool _cchainCall(cchain *chain, int n, stvar args[])
{
    lazyInit(&cchainLock_state, cchainLock_init, NULL);

    atomic(ptr) *curptr = (atomic(ptr) *)chain;
    ClosureChain *cur;
    bool ret = true;

    stvlist stv_cvars;
    stvlist stv_args;

    withReadLock(&cchainLock)
    {
        cur = atomicLoad(ptr, curptr, Relaxed);
        while(cur) {
            stvlInit(&stv_cvars, cur->nvars, cur->cvars);
            stvlInit(&stv_args, n, args);
            ret &= cur->func(&stv_cvars, &stv_args);

            cur = atomicLoad(ptr, &cur->prev, Relaxed);
        }
    }

    return ret;
}

_Use_decl_annotations_
void cchainTransfer(cchain *dest, cchain *src)
{
    lazyInit(&cchainLock_state, cchainLock_init, NULL);

    atomic(ptr) *destptr = (atomic(ptr) *)dest;
    atomic(ptr) *srcptr = (atomic(ptr) *)src;

    ClosureChain *srcchain = atomicLoad(ptr, srcptr, Relaxed);
    withReadLock(&cchainLock)
    {
        do {
            atomicStore(ptr, destptr, srcchain, Relaxed);
        } while(!atomicCompareExchange(ptr, weak, srcptr, &srcchain, NULL, Relaxed, Relaxed));
    }
    *src = NULL;        // redundant, but keeps compiler happy
}

_Use_decl_annotations_
void cchainDestroy(cchain *chain)
{
    lazyInit(&cchainLock_state, cchainLock_init, NULL);

    ClosureChain *cur, *prev;

    // early out before locking
    if(!(chain && *chain))
        return;

    // get the write lock to ensure we cannot destroy any closure that is in the middle
    // of executing
    withWriteLock(&cchainLock)
    {
        cur = atomicLoad(ptr, (atomic(ptr) *)chain, Relaxed);     // atomic access isn't strictly necessary with lock held, but good form
        atomicStore(ptr, (atomic(ptr) *)chain, NULL, Release);

        while(cur) {
            for(int i = cur ? cur->nvars - 1 : -1; i >= 0; --i) {
                stvarDestroy(&cur->cvars[i]);
            }

            prev = atomicLoad(ptr, &cur->prev, Relaxed);
            xaFree(cur);
            cur = prev;
        }
    }

    *chain = NULL;          // redundant, but keeps compiler happy
}
