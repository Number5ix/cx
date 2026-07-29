// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "net/filter.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include "net_private.h"

void NetFlowFilter_notify(_In_ NetFlowFilter* self, NetFilterNotify note)
{
    // Record the notification for the data-plane driver to deliver (as a NET_FilterNotify event)
    // after this filter returns. The framework never sets this on a filter's behalf; a filter calls
    // netflowfilterNotify() itself when it decides the application should hear about a milestone
    // (NFN_Secured, for example).
    self->pendingNotify = note;
}

_objinit_guaranteed bool NetStreamFilter_init(_In_ NetStreamFilter* self)
{
    // The two boundary rings are [noinit] so codegen does not try to size them; allocate them here
    // for every concrete stream filter (init runs parent -> child). The generated destructor tears
    // them back down.
    bufringInit(&self->encOut, NET_FILTER_RING_SEGSZ);
    bufringInit(&self->decOut, NET_FILTER_RING_SEGSZ);

    // Autogen begins -----
    return true;
    // Autogen ends -------
}

void NetStreamFilter_destroy(_In_ NetStreamFilter* self)
{
    // Autogen begins -----
    bufringDestroy(&self->encOut);
    bufringDestroy(&self->decOut);
    // Autogen ends -------
}

void NetDatagramFilter_destroy(_In_ NetDatagramFilter* self)
{
    // Nobody will drain these two queues once this stage is torn down, so any messages still
    // sitting in them have to be released here rather than dispatched. This is the case the stage's
    // own pool reference exists for: a chain dropped mid-handshake is the ordinary way a filter dies
    // with output still staged, and destroying those buffers instead of returning them would take
    // that much capacity away from every socket sharing the pool, for good.
    netpoolFreeMsgQueue(self->pool, &self->encOut);
    netpoolFreeMsgQueue(self->pool, &self->decOut);
    // Autogen begins -----
    objRelease(&self->pool);
    // Autogen ends -------
}

// ---------------------------------------------------------------------------------------------
// Chain construction
//
// A flow's chain is never assembled by the application: the socket owns an ordered list of
// NetFilter factories, and every flow it owns gets the parallel list of NetFlowFilter stages that
// those factories produce. Building it before the flow becomes reachable is what guarantees no
// byte can ever reach a handler unfiltered.
// ---------------------------------------------------------------------------------------------

// The staging buffer the encode side consumes from. Allocated with the chain (stream flows only;
// a datagram chain stages whole messages in flow->encInMsgs instead, which needs no allocation).
static void flowInitStaging(NetSocket* sock, NetFlow* flow)
{
    if (sock->type != NST_Stream || flow->encIn || saSize(flow->filters) == 0)
        return;

    flow->encIn = xaAlloc(sizeof(BufRing), XA_Zero);
    bufringInit(flow->encIn, NET_FILTER_RING_SEGSZ);
}

_Use_decl_annotations_
void NetFlow__addFilter(NetFlow* self, NetSocket* sock, NetFilter* filter)
{
    // The factory gets the final say per flow: returning NULL leaves this stage out of the chain
    // entirely rather than installing a pass-through nobody asked for.
    NetFlowFilter* stage = netfilterCreateFlow(filter, sock->type);
    if (!stage)
        return;

    // A datagram stage cannot work without the pool -- it is where its output comes from and where
    // its input goes back to -- and it is wired up here rather than in createFlow() so that no
    // concrete filter can forget to do it. The reference belongs to the stage from this point on.
    NetDatagramFilter* dgram = objDynCast(NetDatagramFilter, stage);
    if (dgram)
        dgram->pool = objAcquire(self->pool);

    withMutex (&self->filterLock) {
        // Consume the createFlow() reference: the flow's array is the sole owner of a stage.
        saPushC(&self->filters, object, &stage);
        flowInitStaging(sock, self);
    }
}

_Use_decl_annotations_
void NetFlow__buildFilters(NetFlow* self, NetSocket* sock)
{
    for (int32 i = 0; i < saSize(sock->filters); i++) {
        NetFilter* f = sock->filters.a[i];
        if (f)
            netflow_addFilter(self, sock, f);
    }
}

_Use_decl_annotations_
void NetFlow__clearFilters(NetFlow* self)
{
    withMutex (&self->filterLock) {
        // Whatever the stages still held buffered goes with them -- there is no wire left to flush
        // it to by the time a chain is being dropped.
        saDestroy(&self->filters);
        netpoolFreeMsgQueue(self->pool, &self->encInMsgs);

        if (self->encIn) {
            bufringDestroy(self->encIn);
            xaFree(self->encIn);
            self->encIn = NULL;
        }
    }
}

_Use_decl_annotations_
void NetFlow__filterShutdown(NetFlow* self)
{
    withMutex (&self->filterLock) {
        // App -> wire order, so an outer stage's close record can still be wrapped by the stages
        // below it when the driver runs one last encode pass afterwards.
        for (int32 i = 0; i < saSize(self->filters); i++) netflowfilterShutdown(self->filters.a[i]);
    }
}

_Use_decl_annotations_
void NetFlow__filterNotify(NetFlow* self, NetQueue* q, NetSocket* sock, bool onWorker)
{
    // Never called with the filter lock held: an application handler is free to call netsocketSend()
    // straight back into the encode path, which takes it. Each stage is sampled and cleared under
    // the lock, then delivered outside it.
    for (int32 i = 0; i < saSize(self->filters); i++) {
        NetFilterNotify note = NFN_None;

        withMutex (&self->filterLock) {
            // The chain can be dropped by netsocketRemoveFilters() between iterations, so the index
            // is re-checked rather than trusted.
            if (i < saSize(self->filters)) {
                NetFlowFilter* f = self->filters.a[i];
                note             = f->pendingNotify;
                f->pendingNotify = NFN_None;
            }
        }

        if (note == NFN_None)
            continue;

        if (onWorker) {
            // Already inside the flow's dispatch batch: deliver directly, which is what puts the
            // notification ahead of any application data produced by the same pass.
            NetEvent ev      = { .event = NET_FilterNotify };
            ev.filter.notify = note;
            netqueue_deliver(q, sock, self, &ev);
        } else if (q) {
            // Raised from a send on some application thread. Events are only ever delivered on a
            // worker, so it rides the flow's inbox like a packet and lands in order.
            NetMessage* msg = netpoolAllocHeader(self->pool);
            msg->kind       = NMSG_FilterNotify;
            msg->bytes      = (size_t)note;
            netqueue_submit(q, self, msg);
        }
    }
}

// Autogen begins -----
// clang-format off
#include "net/filter.auto.inc"
// clang-format on
// Autogen ends -------
