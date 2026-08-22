// ==================== Auto-generated section begins ====================
// clang-format off
// Do not modify the contents of this section; any changes will be lost!
#include <cx/obj.h>
#include <cx/debug/assert.h>
#include <cx/obj/objstdif.h>
#include <cx/container.h>
#include <cx/string.h>
#include "netfilterobj.h"
// clang-format on
// ==================== Auto-generated section ends ======================

#include <cx/net/net_private.h>

// Scratch large enough for every payload these tests move through a stage in one pass.
#define FILTER_SCRATCH 4096

_objfactory_guaranteed NetPassStreamFilter* NetPassStreamFilter_create()
{
    NetPassStreamFilter* self;
    self = objInstCreate(NetPassStreamFilter);

    objInstInit(self);
    return self;
}

void NetPassStreamFilter_shutdown(_In_ NetPassStreamFilter* self)
{
    unused_noeval(self);   // nothing to negotiate, so nothing to close down
}

intptr NetPassStreamFilter_encode(_In_ NetPassStreamFilter* self, BufRing* src)
{
    // Move everything available straight through to the wire-facing ring. A priming pass arrives
    // here with src empty and correctly produces nothing: this filter initiates nothing.
    uint8 tmp[FILTER_SCRATCH];
    intptr moved = 0;
    size_t n;
    while ((n = bufringRead(src, tmp, sizeof(tmp))) > 0) {
        bufringWrite(&self->encOut, tmp, n);
        moved += (intptr)n;
    }
    return moved;
}

intptr NetPassStreamFilter_decode(_In_ NetPassStreamFilter* self, BufRing* src)
{
    // Stand in for a handshake completing on the first inbound data: raise NFN_Secured once, on the
    // transition, the way a real session filter would. The framework never infers this -- the filter
    // asks for it explicitly, and a filter that never asks (see NetFrameStreamFilter) never gets it.
    if (!self->secured) {
        self->secured = true;
        netpassstreamfilterNotify(self, NFN_Secured);
    }

    uint8 tmp[FILTER_SCRATCH];
    intptr moved = 0;
    size_t n;
    while ((n = bufringRead(src, tmp, sizeof(tmp))) > 0) {
        bufringWrite(&self->decOut, tmp, n);
        moved += (intptr)n;
    }
    return moved;
}

_objfactory_guaranteed NetFrameStreamFilter* NetFrameStreamFilter_create()
{
    NetFrameStreamFilter* self;
    self = objInstCreate(NetFrameStreamFilter);

    objInstInit(self);
    return self;
}

void NetFrameStreamFilter_shutdown(_In_ NetFrameStreamFilter* self)
{
    unused_noeval(self);
}

intptr NetFrameStreamFilter_encode(_In_ NetFrameStreamFilter* self, BufRing* src)
{
    // Frame whatever is available as one length-prefixed unit (2-byte big-endian length + payload).
    // Cap each frame at the scratch size so the decoder's fixed buffer is always sufficient.
    size_t avail = src->total;
    if (avail == 0)
        return 0;
    if (avail > FILTER_SCRATCH)
        avail = FILTER_SCRATCH;

    uint8 tmp[FILTER_SCRATCH];
    size_t n     = bufringRead(src, tmp, avail);
    uint8 hdr[2] = { (uint8)((n >> 8) & 0xff), (uint8)(n & 0xff) };
    bufringWrite(&self->encOut, hdr, 2);
    bufringWrite(&self->encOut, tmp, n);
    return (intptr)(n + 2);
}

intptr NetFrameStreamFilter_decode(_In_ NetFrameStreamFilter* self, BufRing* src)
{
    // Emit a payload only once its whole length-prefixed frame has arrived; a partial frame stays in
    // the input ring and resumes on the next wire read. This is the rate mismatch that exercises the
    // boundary rings and the fixpoint driver.
    intptr produced = 0;
    for (;;) {
        if (src->total < 2)
            break;

        uint8 hdr[2];
        bufringPeek(src, hdr, 0, 2);
        size_t framelen = ((size_t)hdr[0] << 8) | hdr[1];

        if (src->total < 2 + framelen)
            break;   // the whole frame is not here yet

        bufringSkip(src, 2);
        uint8 tmp[FILTER_SCRATCH];
        size_t got = bufringRead(src, tmp, framelen);
        bufringWrite(&self->decOut, tmp, got);
        produced += (intptr)got;
    }
    return produced;
}

_objfactory_guaranteed NetPassDgramFilter* NetPassDgramFilter_create()
{
    NetPassDgramFilter* self;
    self = objInstCreate(NetPassDgramFilter);

    objInstInit(self);
    return self;
}

void NetPassDgramFilter_shutdown(_In_ NetPassDgramFilter* self)
{
    unused_noeval(self);
}

intptr NetPassDgramFilter_encodeMsg(_In_ NetPassDgramFilter* self, NetMsgQueue* src)
{
    // Identity: hand every staged message straight down toward the wire. Priming finds src empty
    // and produces nothing, which is what a transform with no negotiation of its own should do.
    intptr moved = 0;
    NetMessage* m;
    while ((m = netMsgQueuePop(src))) {
        netMsgQueuePush(&self->encOut, m);
        moved++;
    }
    return moved;
}

intptr NetPassDgramFilter_decodeMsg(_In_ NetPassDgramFilter* self, NetMsgQueue* src)
{
    // Copy into a message allocated from the pool rather than passing the wire packet through, so
    // the test covers the allocate-and-emit path a stage that reframes or reassembles would take --
    // including the pooled buffer finding its way back to the pool once the event is retired.
    intptr produced = 0;
    NetMessage* m;

    while ((m = netMsgQueuePop(src))) {
        NetMessage* out = netpoolAllocMsg(self->pool);
        if (!out) {
            // Pool exhausted: drop the packet rather than allocate around the ceiling the pool
            // exists to impose, exactly as a real stage should.
            netpoolFreeMsg(self->pool, &m);
            continue;
        }

        size_t len = m->buf ? m->buf->len : 0;
        if (len > out->buf->sz)
            len = out->buf->sz;
        if (len > 0)
            memcpy(out->buf->data, m->buf->data, len);
        out->buf->len = len;
        out->addr     = m->addr;

        netMsgQueuePush(&self->decOut, out);
        produced++;

        netpoolFreeMsg(self->pool, &m);
    }

    return produced;
}

_objfactory_guaranteed NetPassStreamFactory* NetPassStreamFactory_create()
{
    NetPassStreamFactory* self;
    self = objInstCreate(NetPassStreamFactory);

    objInstInit(self);
    return self;
}

NetFlowFilter* NetPassStreamFactory_createFlow(_In_ NetPassStreamFactory* self, NetSocketType type)
{
    unused_noeval(self);
    unused_noeval(type);
    return NetFlowFilter(netpassstreamfilterCreate());
}

bool NetPassStreamFactory_canFilter(_In_ NetPassStreamFactory* self, NetSocketType type)
{
    unused_noeval(self);
    return type == NST_Stream;
}

_objfactory_guaranteed NetFrameStreamFactory* NetFrameStreamFactory_create()
{
    NetFrameStreamFactory* self;
    self = objInstCreate(NetFrameStreamFactory);

    objInstInit(self);
    return self;
}

NetFlowFilter* NetFrameStreamFactory_createFlow(_In_ NetFrameStreamFactory* self, NetSocketType type)
{
    unused_noeval(self);
    unused_noeval(type);
    return NetFlowFilter(netframestreamfilterCreate());
}

bool NetFrameStreamFactory_canFilter(_In_ NetFrameStreamFactory* self, NetSocketType type)
{
    unused_noeval(self);
    return type == NST_Stream;
}

_objfactory_guaranteed NetPassDgramFactory* NetPassDgramFactory_create()
{
    NetPassDgramFactory* self;
    self = objInstCreate(NetPassDgramFactory);

    objInstInit(self);
    return self;
}

NetFlowFilter* NetPassDgramFactory_createFlow(_In_ NetPassDgramFactory* self, NetSocketType type)
{
    unused_noeval(self);
    unused_noeval(type);
    return NetFlowFilter(netpassdgramfilterCreate());
}

bool NetPassDgramFactory_canFilter(_In_ NetPassDgramFactory* self, NetSocketType type)
{
    unused_noeval(self);
    return type == NST_Datagram;
}

_objfactory_guaranteed NetTapStreamFilter* NetTapStreamFilter_create(BufRing* sink)
{
    NetTapStreamFilter* self;
    self = objInstCreate(NetTapStreamFilter);

    self->sink = sink;

    objInstInit(self);
    return self;
}

void NetTapStreamFilter_shutdown(_In_ NetTapStreamFilter* self)
{
    unused_noeval(self);   // a tap negotiates nothing, so it has nothing to close down
}

intptr NetTapStreamFilter_encode(_In_ NetTapStreamFilter* self, BufRing* src)
{
    // Identity, with a copy taken on the way past. Attached after another filter this stage sits
    // closer to the wire, so what lands in the sink is exactly what that filter produced.
    uint8 tmp[FILTER_SCRATCH];
    intptr moved = 0;
    size_t n;
    while ((n = bufringRead(src, tmp, sizeof(tmp))) > 0) {
        if (self->sink)
            bufringWrite(self->sink, tmp, n);
        bufringWrite(&self->encOut, tmp, n);
        moved += (intptr)n;
    }
    return moved;
}

intptr NetTapStreamFilter_decode(_In_ NetTapStreamFilter* self, BufRing* src)
{
    // The inbound direction is not recorded: one peer's outbound is the other's inbound, so a tap
    // on each end already covers both halves of the conversation without the two mixing in one
    // ring.
    uint8 tmp[FILTER_SCRATCH];
    intptr moved = 0;
    size_t n;
    while ((n = bufringRead(src, tmp, sizeof(tmp))) > 0) {
        bufringWrite(&self->decOut, tmp, n);
        moved += (intptr)n;
    }
    return moved;
}

_objfactory_guaranteed NetTapStreamFactory* NetTapStreamFactory_create(BufRing* sink)
{
    NetTapStreamFactory* self;
    self = objInstCreate(NetTapStreamFactory);

    self->sink = sink;

    objInstInit(self);
    return self;
}

NetFlowFilter* NetTapStreamFactory_createFlow(_In_ NetTapStreamFactory* self, NetSocketType type)
{
    unused_noeval(type);
    return NetFlowFilter(nettapstreamfilterCreate(self->sink));
}

bool NetTapStreamFactory_canFilter(_In_ NetTapStreamFactory* self, NetSocketType type)
{
    unused_noeval(self);
    return type == NST_Stream;
}

// Autogen begins -----
// clang-format off
#include "netfilterobj.auto.inc"
// clang-format on
// Autogen ends -------
