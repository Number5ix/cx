#include "cchain.h"
#include "closure_private.h"
#include <cx/thread/aspin.h>

// This module used to use a global lock to handle thread safety. However this became quite
// inefficient once there was a mix of renewable and one-shot chains.

// Now it uses sentinel values with a strict compare-and-exchange regime to implement simple
// spinlocks using the pointers themselves. This is slightly worse for the case with many
// concurrent calls to the same closure chain as they are now serialized, but better for
// the general use case as the locking is specific to a chain rather than global.

// Sentinel value for a chain that's been invalidated for future use.
#define CCNODE_INVALID ((CChainNode*)-1)
// Sentinel value for a chain that is currently busy
#define CCNODE_BUSY    ((CChainNode*)-2)

static _meta_inline void ccnodeAddRef(_Inout_ CChainNode* node)
{
    devAssert(node != CCNODE_INVALID && node != CCNODE_BUSY);

    atomicFetchAdd(uint32, &node->refcount, 1, Relaxed);
}

static void ccnodeDeref(_Inout_ CChainNode* node)
{
    devAssert(node != CCNODE_INVALID && node != CCNODE_BUSY);

    if (!node)
        return;

    if (atomicFetchSub(uint32, &node->refcount, 1, Release) == 1) {
        atomicFence(Acquire);

        CChainNode* prev = node->prev;
        for (int i = node->nvars - 1; i >= 0; --i) {
            stvarDestroy(&node->cvars[i]);
        }
        xaFree(node);

        // recursively deref previous node(s)
        ccnodeDeref(prev);
    }
}

static CChainNode* ccnodeFree(_In_ CChainNode* node)
{
    // Only used in code paths where we have exclusive ownership (cchainCallOnce)
    for (int i = node ? node->nvars - 1 : -1; i >= 0; --i) {
        stvarDestroy(&node->cvars[i]);
    }

    CChainNode* prev = node->prev;
    xaFree(node);
    return prev;
}

// Get the head node of a chain and mark it busy (or invalid)
_Success_(return) static bool
cchainAcquire(_Inout_ cchain* chain, _Out_ CChainNode** onode, bool invalidate)
{
    AdaptiveSpinState astate = { 0 };
    CChainNode* out          = atomicLoad(ptr, (atomic(ptr)*)chain, Relaxed);
    CChainNode* nstate       = invalidate ? CCNODE_INVALID : CCNODE_BUSY;

    for (;;) {
        // another thread beat us to it and invalidated the chain
        if (out == CCNODE_INVALID)
            return false;

        // chain is busy, spinloop
        if (out == CCNODE_BUSY) {
            aspinHandleContention(NULL, &astate);   // abuse contention code for backoff
            out = atomicLoad(ptr, (atomic(ptr)*)chain, Relaxed);
        } else {
            // try to swap it for desired
            if (atomicCompareExchange(ptr,
                                      weak,
                                      (atomic(ptr)*)chain,
                                      (void**)&out,
                                      nstate,
                                      Acquire,
                                      Relaxed))
                break;
            aspinHandleContention(NULL, &astate);
        }
    }
    aspinEndContention(&astate);

    *onode = out;
    return true;
}

// Release a busy chain
static void cchainRelease(_Inout_ cchain* chain, _In_opt_ CChainNode* node)
{
    AdaptiveSpinState astate = { 0 };
    // Write the original value back. This SHOULD be uncontended, but still have to CAS it anyway.

    CChainNode* cur = CCNODE_BUSY;   // only valid starting point for this function
    while (!atomicCompareExchange(ptr,
                                  weak,
                                  (atomic(ptr)*)chain,
                                  (void**)&cur,
                                  node,
                                  Release,
                                  Relaxed)) {
        if (!devVerifyMsg(cur == CCNODE_BUSY, "Incorrect use of cchainRelease"))
            return;
        aspinHandleContention(NULL, &astate);
    }
    aspinEndContention(&astate);
}

_Use_decl_annotations_
bool _cchainAttach(cchain* chain, closureFunc func, intptr token, int n, stvar cvars[])
{
    CChainNode* nchain = xaAlloc(sizeof(CChainNode) + sizeof(stvar) * (n + (token ? 1 : 0)));
    atomicStore(uint32, &nchain->refcount, 1, Relaxed);   // Initial refcount for chain ownership
    nchain->func  = func;
    nchain->nvars = n + (token ? 1 : 0);
    nchain->token = token;

    for (int i = 0; i < n; i++) {
        stvarCopy(&nchain->cvars[i], cvars[i]);
    }
    if (token)
        nchain->cvars[n] = stvar(intptr, token);

    CChainNode* cur;
    if (cchainAcquire(chain, &cur, false)) {
        nchain->prev = cur;
        // nchain now owns reference to cur (transferred from chain ownership)
        cchainRelease(chain, nchain);
        return true;
    } else {
        ccnodeFree(nchain);
        return false;
    }
}

_Use_decl_annotations_
bool cchainDetach(cchain* chain, closureFunc func, intptr token)
{
    CChainNode* top;

    if (cchainAcquire(chain, &top, false)) {
        CChainNode* newchain = NULL;
        CChainNode** newptr  = &newchain;
        CChainNode* cur      = top;
        bool ret             = false;

        // Clone chain excluding the target node
        while (cur) {
            if (cur->func == func && cur->token == token) {
                // Skip this node (first match only)
                ret = true;
                cur = cur->prev;
                continue;
            }

            CChainNode* nnode = xaAlloc(sizeof(CChainNode) + cur->nvars * sizeof(stvar));
            atomicStore(uint32, &nnode->refcount, 1, Relaxed);
            nnode->func  = cur->func;
            nnode->token = cur->token;
            nnode->nvars = cur->nvars;

            for (int i = 0; i < cur->nvars; i++) {
                stvarCopy(&nnode->cvars[i], cur->cvars[i]);
            }

            *newptr = nnode;
            newptr  = &nnode->prev;
            cur     = cur->prev;
        }

        *newptr = NULL;

        // swap in new chain while releasing the lock
        cchainRelease(chain, newchain);

        // decrement our reference to the old chain
        ccnodeDeref(top);

        return ret;
    }

    return false;
}

_Use_decl_annotations_
bool cchainClone(cchain* destchain, cchain* srcchain)
{
    CChainNode *src, *dest;

    if (cchainAcquire(destchain, &dest, false)) {
        CChainNode* origdest = dest;
        if (cchainAcquire(srcchain, &src, false)) {
            CChainNode** dptr = &dest;
            CChainNode* snode = src;

            while (snode) {
                CChainNode* nnode = xaAlloc(sizeof(CChainNode) + snode->nvars * sizeof(stvar));
                atomicStore(uint32, &nnode->refcount, 1, Relaxed);
                nnode->func  = snode->func;
                nnode->token = snode->token;
                nnode->nvars = snode->nvars;

                for (int i = 0; i < snode->nvars; i++) {
                    stvarCopy(&nnode->cvars[i], snode->cvars[i]);
                }

                *dptr = nnode;
                dptr  = &nnode->prev;
                snode = snode->prev;
            }

            *dptr = origdest;   // insert cloned nodes before the original dest

            cchainRelease(srcchain, src);
        }

        cchainRelease(destchain, dest);
        return true;
    } else {
        atomicStore(ptr, (atomic(ptr)*)destchain, NULL, Relaxed);
        return false;
    }
}

_Use_decl_annotations_
bool _cchainCall(cchain* chain, int n, stvar args[])
{
    CChainNode* chainref;
    bool ret = true;

    stvlist stv_cvars;
    stvlist stv_args;

    // Lock briefly to grab a reference to the chain
    if (cchainAcquire(chain, &chainref, false)) {
        if (chainref)
            ccnodeAddRef(chainref);       // make sure it doesn't go away while we're using it
        cchainRelease(chain, chainref);   // release lock immediately

        // now we can safely walk the chain without holding the lock
        CChainNode* cur = chainref;
        while (cur) {
            stvlInit(&stv_cvars, cur->nvars, cur->cvars);
            stvlInit(&stv_args, n, args);
            ret &= cur->func(&stv_cvars, &stv_args);
            cur = cur->prev;
        }

        // decrement our temp reference
        ccnodeDeref(chainref);
        return ret;
    } else {
        return false;
    }
}

_Use_decl_annotations_
bool _cchainCallOnce(cchain* chain, int n, stvar args[])
{
    CChainNode* cur;
    bool ret = true;

    stvlist stv_cvars;
    stvlist stv_args;

    // Atomically invalidate the chain while getting the value, then we can be assured it's owned
    // exclusively and destroy it as we go.

    if (cchainAcquire(chain, &cur, true)) {
        while (cur) {
            stvlInit(&stv_cvars, cur->nvars, cur->cvars);
            stvlInit(&stv_args, n, args);
            ret &= cur->func(&stv_cvars, &stv_args);

            // we should be the sole reference holder here
            devAssert(atomicLoad(uint32, &cur->refcount, Relaxed) == 1);

            cur = ccnodeFree(cur);
        }

        // no need to call cchainRelease because it's invalid now
        return ret;
    } else {
        return false;
    }
}

_Use_decl_annotations_
bool cchainTransfer(cchain* dest, cchain* src)
{
    CChainNode *srcnode, *destnode;
    if (cchainAcquire(dest, &destnode, false)) {
        if (cchainAcquire(src, &srcnode, true)) {
            // Add whatever was in the original dest chain onto the transferred one, so that the
            // transferred chain gets effectively inserted.
            CChainNode** srcptr = &srcnode;
            while (*srcptr) {
                srcptr = &(*srcptr)->prev;
            }
            *srcptr = destnode;
            // Ownership of destnode transfers to the last node in srcnode chain

            cchainRelease(dest, srcnode);
            return true;
        } else {
            cchainRelease(dest, destnode);
            return false;
        }
    } else {
        return false;
    }
}

_Use_decl_annotations_
bool cchainReset(cchain* chain)
{
    CChainNode* cur;

    cur = atomicLoad(ptr, (atomic(ptr)*)chain, Relaxed);

    do {
        if (cur != CCNODE_INVALID)
            return false;
    } while (!atomicCompareExchange(ptr,
                                    weak,
                                    (atomic(ptr)*)chain,
                                    (void**)&cur,
                                    NULL,
                                    Relaxed,
                                    Relaxed));

    return true;
}

_Use_decl_annotations_
bool cchainClear(cchain* chain)
{
    CChainNode* cur;

    if (cchainAcquire(chain, &cur, false)) {
        cchainRelease(chain, NULL);
        ccnodeDeref(cur);   // will cascade if we are the last reference
        return true;
    } else {
        return false;
    }
}

_Use_decl_annotations_
void cchainDestroy(cchain* chain)
{
    CChainNode* cur;

    if (cchainAcquire(chain, &cur, true)) {
        // Decrement reference to chain
        // May not actually destroy immediately, but this is safer if some bonehead
        // calls this while callbacks are executing concurrently.
        ccnodeDeref(cur);
    }

    // intentionally do not release; leave the chain in an invalidated state
}
