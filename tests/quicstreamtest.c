// The QUIC stream layer, driven between two endpoints with no connection and no sockets under it.
//
// The harness wires two QuicStreams objects together through a fake packetizer: one side is asked
// to fill a packet, the frames in it are decoded and handed to the other side, and the packet is
// then declared acknowledged or lost. Every frame is a real one, encoded and decoded by the same
// codec a connection uses, so a test that says data arrived has established the frame layer with
// it.
//
// Nothing here has a clock. Loss is something the test decides, not something a timer discovers,
// which is what lets a retransmission be asserted exactly rather than waited for.

#include <cxquic.h>

#include "../cxquic/stream_private.h"

#define TEST_FILE  quicstreamtest
#define TEST_FUNCS quicstreamtest_funcs
#include "common.h"

// bool CHECK(const char *what, bool cond);
#define CHECK(what, cond)                                                 \
    do {                                                                  \
        if (!(cond)) {                                                    \
            TEST_FAILV(ret, 1, _SL("${string}"), stvar(strref, _S what));  \
            goto out;                                                     \
        }                                                                 \
    } while (0)

// bool CHECK_U(const char *what, int64 got, int64 want);
#define CHECK_U(what, got, want)                                              \
    do {                                                                      \
        if ((int64)(got) != (int64)(want)) {                                  \
            TEST_FAILV(ret, 1, _SL("${string}: got ${int}, expected ${int}"),  \
                       stvar(strref, _S what), stvar(int64, (int64)(got)),     \
                       stvar(int64, (int64)(want)));                          \
            goto out;                                                         \
        }                                                                     \
    } while (0)

// ---------------------------------------------------------------------------------------------
// Two stream layers wired to each other
// ---------------------------------------------------------------------------------------------

// The room a 1-RTT packet leaves for frames, near enough. The exact figure does not matter to
// anything here; what matters is that it is one fixed number so a test can count packets.
#define SBUDGET 1100
#define SMAXQ   64

typedef struct SPkt {
    uint8 data[SBUDGET];
    size_t len;
    uint64 pn;
} SPkt;

typedef struct SSide {
    QuicStreams ss;
    uint64 nextPn;

    SPkt q[SMAXQ];      // packets built but not yet handed over
    uint32 nq;

    // What the handlers saw.
    uint32 nopened, nreadable, nwritable, nreset, nstopped, nclosed;
    uint64 lastOpened, lastClosed;
    uint64 resetId, resetError;
    uint64 stopId, stopError;

    bool refuseOpen;    // turn down streams the peer opens
} SSide;

typedef struct SFix {
    SSide a;            // the client
    SSide b;            // the server
} SFix;

static bool sOpened(_In_opt_ void* ctx, uint64 id)
{
    SSide* s = ctx;
    s->nopened++;
    s->lastOpened = id;
    return !s->refuseOpen;
}

static void sReadable(_In_opt_ void* ctx, uint64 id)
{
    SSide* s = ctx;
    unused_noeval(id);
    s->nreadable++;
}

static void sWritable(_In_opt_ void* ctx, uint64 id)
{
    SSide* s = ctx;
    unused_noeval(id);
    s->nwritable++;
}

static void sReset(_In_opt_ void* ctx, uint64 id, uint64 error)
{
    SSide* s     = ctx;
    s->nreset++;
    s->resetId    = id;
    s->resetError = error;
}

static void sStopped(_In_opt_ void* ctx, uint64 id, uint64 error)
{
    SSide* s     = ctx;
    s->nstopped++;
    s->stopId    = id;
    s->stopError = error;
}

static void sClosed(_In_opt_ void* ctx, uint64 id)
{
    SSide* s = ctx;
    s->nclosed++;
    s->lastClosed = id;
}

static const QuicStreamHandlers sHandlers = {
    .opened   = sOpened,
    .readable = sReadable,
    .writable = sWritable,
    .reset    = sReset,
    .stopped  = sStopped,
    .closed   = sClosed,
};

static void sTpDefaults(_Out_ QuicTransportParams* tp)
{
    _quicTpDefaults(tp);
    tp->initMaxData         = 1 << 16;
    tp->initMaxSdBidiLocal  = 1 << 14;
    tp->initMaxSdBidiRemote = 1 << 14;
    tp->initMaxSdUni        = 1 << 14;
    tp->initMaxStreamsBidi  = 4;
    tp->initMaxStreamsUni   = 4;
}

// Brings both ends up with the parameters each one advertises, having already exchanged them the
// way a completed handshake would.
static void sFixInit2(_Out_ SFix* f, _In_ const QuicTransportParams* atp,
                      _In_ const QuicTransportParams* btp)
{
    memset(f, 0, sizeof(*f));

    _quicStreamsInit(&f->a.ss, false, atp);
    _quicStreamsInit(&f->b.ss, true, btp);

    _quicStreamsSetHandlers(&f->a.ss, &sHandlers, &f->a);
    _quicStreamsSetHandlers(&f->b.ss, &sHandlers, &f->b);

    _quicStreamsPeerParams(&f->a.ss, btp);
    _quicStreamsPeerParams(&f->b.ss, atp);
}

static void sFixInit(_Out_ SFix* f)
{
    QuicTransportParams tp;
    sTpDefaults(&tp);
    sFixInit2(f, &tp, &tp);
}

static void sFixDestroy(_Inout_ SFix* f)
{
    _quicStreamsDestroy(&f->a.ss);
    _quicStreamsDestroy(&f->b.ss);
}

// Builds one packet's worth of frames. Returns how many bytes were produced; zero means the side
// had nothing to say.
static size_t sBuild(_Inout_ SSide* from, size_t budget)
{
    if (from->nq >= SMAXQ)
        return 0;

    SPkt* p = &from->q[from->nq];
    bool elicit;

    p->pn  = from->nextPn;
    p->len = _quicStreamsFill(&from->ss, p->data, budget, p->pn, &elicit);
    if (p->len == 0)
        return 0;

    from->nextPn++;
    from->nq++;
    return p->len;
}

// Hands one packet to the other side, one frame at a time. Returns false if a frame was refused,
// which is a connection error and is what the error tests look for.
static bool sFeed(_Inout_ SSide* to, _In_ const SPkt* p)
{
    QuicRd rd;
    _quicRdInit(&rd, p->data, p->len);

    QuicFrame fr;
    while (_quicFrameDecode(&fr, &rd)) {
        if (!_quicStreamsFrame(&to->ss, &fr))
            return false;
    }

    return !rd.bad;
}

// Delivers everything one side has built and acknowledges it, which is the ordinary lossless
// case. Returns false on a connection error at the receiver.
static bool sDeliver(_Inout_ SSide* from, _Inout_ SSide* to)
{
    uint32 n  = from->nq;
    from->nq  = 0;
    bool ok   = true;

    for (uint32 i = 0; i < n; i++) {
        if (ok && !sFeed(to, &from->q[i]))
            ok = false;
        _quicStreamsAcked(&from->ss, from->q[i].pn);
    }

    return ok;
}

// Delivers only the packets whose bit is set in `keep`, and declares the rest lost. This is how a
// test decides exactly which packet went missing rather than dropping a whole flight.
static bool sDeliverSome(_Inout_ SSide* from, _Inout_ SSide* to, uint32 keep)
{
    uint32 n = from->nq;
    from->nq = 0;
    bool ok  = true;

    for (uint32 i = 0; i < n; i++) {
        if ((keep >> i) & 1) {
            if (ok && !sFeed(to, &from->q[i]))
                ok = false;
            _quicStreamsAcked(&from->ss, from->q[i].pn);
        } else {
            _quicStreamsLost(&from->ss, from->q[i].pn);
        }
    }

    return ok;
}

// Throws away everything one side has built, telling it so.
static void sDrop(_Inout_ SSide* from)
{
    uint32 n = from->nq;
    from->nq = 0;

    for (uint32 i = 0; i < n; i++)
        _quicStreamsLost(&from->ss, from->q[i].pn);
}

// Runs both sides until neither has anything more to send.
static bool sRun(_Inout_ SFix* f)
{
    for (int i = 0; i < 64; i++) {
        bool any = false;

        while (sBuild(&f->a, SBUDGET) > 0)
            any = true;
        while (sBuild(&f->b, SBUDGET) > 0)
            any = true;

        if (!sDeliver(&f->a, &f->b) || !sDeliver(&f->b, &f->a))
            return false;

        if (!any)
            break;
    }

    return true;
}

// Reads everything readable on a stream, appending to `out`.
static bool sRead(_Inout_ SSide* s, uint64 id, _Inout_ Buffer* out, _Out_ bool* fin)
{
    uint8 tmp[4096];
    *fin = false;

    for (;;) {
        bool done = false;
        size_t n  = _quicStreamRecv(&s->ss, id, tmp, sizeof(tmp), &done);
        if (done)
            *fin = true;
        if (n > 0)
            bufAppendBytes(out, tmp, n);
        if (n == 0)
            break;
    }

    return true;
}

// Counts the frames of one type across everything a side has built but not yet handed over.
static uint32 sCountFrames(_In_ const SSide* s, uint64 type)
{
    uint32 n = 0;

    for (uint32 i = 0; i < s->nq; i++) {
        QuicRd rd;
        _quicRdInit(&rd, s->q[i].data, s->q[i].len);

        QuicFrame fr;
        while (_quicFrameDecode(&fr, &rd)) {
            if (fr.type == type)
                n++;
        }
    }

    return n;
}

// Finds the last frame of one type a side has built, so a test can look at what it carried.
static bool sLastFrame(_In_ const SSide* s, uint64 type, _Out_ QuicFrame* out)
{
    bool found = false;
    memset(out, 0, sizeof(*out));

    for (uint32 i = 0; i < s->nq; i++) {
        QuicRd rd;
        _quicRdInit(&rd, s->q[i].data, s->q[i].len);

        QuicFrame fr;
        while (_quicFrameDecode(&fr, &rd)) {
            if (fr.type == type) {
                *out  = fr;
                found = true;
            }
        }
    }

    return found;
}

// STREAM frames carry three flag bits in their type, so counting them means masking those off.
static uint32 sCountStreams(_In_ const SSide* s)
{
    uint32 n = 0;

    for (uint32 i = 0; i < s->nq; i++) {
        QuicRd rd;
        _quicRdInit(&rd, s->q[i].data, s->q[i].len);

        QuicFrame fr;
        while (_quicFrameDecode(&fr, &rd)) {
            if ((fr.type & ~(uint64)0x07) == QUIC_FRAME_STREAM)
                n++;
        }
    }

    return n;
}

// Injects one hand-built frame, which is how the tests that need an endpoint to misbehave do it.
static bool sInject(_Inout_ SSide* to, _In_ const QuicFrame* f)
{
    uint8 buf[512];
    QuicWr wr;
    _quicWrInit(&wr, buf, sizeof(buf));
    if (!_quicFrameEncode(&wr, f))
        return false;

    SPkt p;
    memcpy(p.data, buf, _quicWrLen(&wr));
    p.len = _quicWrLen(&wr);
    p.pn  = 0;

    return sFeed(to, &p);
}

// ---------------------------------------------------------------------------------------------
// Stream identifiers
// ---------------------------------------------------------------------------------------------

int test_quicstreamtest_ids(void)
{
    int ret = 0;

    CHECK_U("client bidi 0", _quicStreamMakeId(0, false, false), 0);
    CHECK_U("server bidi 0", _quicStreamMakeId(0, true, false), 1);
    CHECK_U("client uni 0", _quicStreamMakeId(0, false, true), 2);
    CHECK_U("server uni 0", _quicStreamMakeId(0, true, true), 3);
    CHECK_U("client bidi 3", _quicStreamMakeId(3, false, false), 12);

    CHECK("stream 2 runs one way", _quicStreamUni(2));
    CHECK("stream 4 runs both ways", !_quicStreamUni(4));
    CHECK_U("stream 13 counts as", _quicStreamIndex(13), 3);
    CHECK_U("stream 6 is a uni", _quicStreamDir(6), QUIC_SDIR_UNI);
    CHECK_U("stream 5 is a bidi", _quicStreamDir(5), QUIC_SDIR_BIDI);

    // The same id is local to one role and remote to the other, which is the whole point of the
    // low bit.
    CHECK("a client opened stream 4", _quicStreamLocal(4, false));
    CHECK("a server did not open stream 4", !_quicStreamLocal(4, true));
    CHECK("a server opened stream 5", _quicStreamLocal(5, true));

out:
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Opening streams
// ---------------------------------------------------------------------------------------------

int test_quicstreamtest_open(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    for (uint64 i = 0; i < 4; i++) {
        CHECK("the client could open a bidi stream", _quicStreamOpen(&f.a.ss, false, &id));
        CHECK_U("the id it got", id, i * 4);
    }

    for (uint64 i = 0; i < 4; i++) {
        CHECK("the client could open a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
        CHECK_U("the id it got", id, i * 4 + 2);
    }

    // The server's streams are numbered in the same order but with the other low bit.
    CHECK("the server could open a bidi stream", _quicStreamOpen(&f.b.ss, false, &id));
    CHECK_U("the server's first bidi", id, 1);
    CHECK("the server could open a uni stream", _quicStreamOpen(&f.b.ss, true, &id));
    CHECK_U("the server's first uni", id, 3);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_openlimit(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    for (int i = 0; i < 4; i++)
        CHECK("the client could open a bidi stream", _quicStreamOpen(&f.a.ss, false, &id));

    CHECK("the fifth was refused", !_quicStreamOpen(&f.a.ss, false, &id));
    CHECK("the client wants to say so", _quicStreamsWantsToSend(&f.a.ss));

    // Asking again does not queue a second complaint about the same limit.
    CHECK("the sixth was refused too", !_quicStreamOpen(&f.a.ss, false, &id));
    CHECK("the packet was built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAMS_BLOCKED frames sent", sCountFrames(&f.a, QUIC_FRAME_STREAMS_BLOCKED_BIDI), 1);

    QuicFrame fr;
    CHECK("the complaint is there", sLastFrame(&f.a, QUIC_FRAME_STREAMS_BLOCKED_BIDI, &fr));
    CHECK_U("the limit it names", fr.streamsBlocked.limit, 4);

    // Uni streams are counted separately, so the bidi limit does not touch them.
    CHECK("a uni stream was still available", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("its id", id, 2);

out:
    sFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Moving data
// ---------------------------------------------------------------------------------------------

int test_quicstreamtest_send(void)
{
    int ret   = 0;
    Buffer got = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));

    static const char msg[] = "hello over a quic stream";
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)msg, sizeof(msg) - 1),
            sizeof(msg) - 1);
    _quicStreamFinish(&f.a.ss, id);

    // The end of the stream has been named, so there is nowhere left to put anything.
    CHECK_U("no room once the end is queued", _quicStreamWritable(&f.a.ss, id), 0);

    CHECK("no connection error", sRun(&f));

    CHECK_U("streams the server saw open", f.b.nopened, 1);
    CHECK_U("which one", f.b.lastOpened, id);

    bool fin = false;
    sRead(&f.b, id, &got, &fin);

    CHECK_U("bytes read", got ? got->len : 0, sizeof(msg) - 1);
    CHECK("the bytes match", got && memcmp(got->data, msg, sizeof(msg) - 1) == 0);
    CHECK("the end of the stream came with them", fin);

    // A bidirectional stream runs until both directions are over, so the server ending its own
    // half is what finishes it. Until then each end is still holding a stream it might yet write
    // on.
    CHECK_U("nobody has let it go yet", f.a.nclosed + f.b.nclosed, 0);

    _quicStreamFinish(&f.b.ss, id);
    CHECK("no connection error on the way back", sRun(&f));

    Buffer back = 0;
    bool bfin   = false;
    sRead(&f.a, id, &back, &bfin);
    bufDestroy(&back);
    CHECK("the client saw the other direction end", bfin);
    CHECK("no connection error", sRun(&f));

    CHECK_U("the client let it go", f.a.nclosed, 1);
    CHECK_U("the server let it go", f.b.nclosed, 1);
    CHECK("the client forgot it", _quicStreamFind(&f.a.ss, id) == NULL);
    CHECK("the server forgot it", _quicStreamFind(&f.b.ss, id) == NULL);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_bulk(void)
{
    int ret    = 0;
    Buffer got = 0;
    uint8* src = NULL;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 1 << 20;
    tp.initMaxSdBidiLocal  = 1 << 20;
    tp.initMaxSdBidiRemote = 1 << 20;
    sFixInit2(&f, &tp, &tp);

    // More than one packet holds and more than the send buffer takes at once, so this exercises
    // the writable handler as well as the packet split.
    const size_t total = 40000;
    src                = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8)(i * 31 + 7);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));

    size_t off = 0;
    for (int round = 0; round < 200 && off < total; round++) {
        off += _quicStreamSend(&f.a.ss, id, src + off, total - off);
        CHECK("no connection error", sRun(&f));
    }

    CHECK_U("everything was queued", off, total);
    _quicStreamFinish(&f.a.ss, id);
    CHECK("no connection error at the end", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);

    CHECK_U("bytes read", got ? got->len : 0, total);
    CHECK("the bytes match", got && memcmp(got->data, src, total) == 0);
    CHECK("the end of the stream arrived", fin);

out:
    xaFree(src);
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_uni(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("its id", id, 2);

    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"one way", 7), 7);
    _quicStreamFinish(&f.a.ss, id);
    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 7);
    CHECK("the end arrived", fin);

    // The receiving end has no sending half at all, so nothing it is asked to write is taken.
    CHECK_U("the server cannot write back", _quicStreamSend(&f.b.ss, id, (const uint8*)"x", 1), 0);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_interleave(void)
{
    int ret  = 0;
    Buffer g1 = 0, g2 = 0;
    SFix f;
    sFixInit(&f);

    uint64 a = 0, b = 0;
    CHECK("opened the first stream", _quicStreamOpen(&f.a.ss, false, &a));
    CHECK("opened the second stream", _quicStreamOpen(&f.a.ss, false, &b));

    CHECK_U("first stream queued", _quicStreamSend(&f.a.ss, a, (const uint8*)"aaaaaaaa", 8), 8);
    CHECK_U("second stream queued", _quicStreamSend(&f.a.ss, b, (const uint8*)"bbbb", 4), 4);
    _quicStreamFinish(&f.a.ss, a);
    _quicStreamFinish(&f.a.ss, b);

    // One packet is big enough for both, which is the case worth checking: the frames have to
    // stay separable rather than running together.
    CHECK("one packet held both", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAM frames in it", sCountStreams(&f.a), 2);
    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, a, &g1, &fin);
    CHECK("the first stream ended", fin);
    sRead(&f.b, b, &g2, &fin);
    CHECK("the second stream ended", fin);

    CHECK("the first stream's bytes", g1 && g1->len == 8 && memcmp(g1->data, "aaaaaaaa", 8) == 0);
    CHECK("the second stream's bytes", g2 && g2->len == 4 && memcmp(g2->data, "bbbb", 4) == 0);

out:
    bufDestroy(&g1);
    bufDestroy(&g2);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_fairness(void)
{
    int ret = 0;
    uint8* src = NULL;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 1 << 20;
    tp.initMaxSdBidiLocal  = 1 << 20;
    tp.initMaxSdBidiRemote = 1 << 20;
    sFixInit2(&f, &tp, &tp);

    src = xaAlloc(20000, XA_Zero);

    uint64 hog = 0, small = 0;
    CHECK("opened the busy stream", _quicStreamOpen(&f.a.ss, false, &hog));
    CHECK("opened the quiet stream", _quicStreamOpen(&f.a.ss, false, &small));

    CHECK_U("the busy stream queued", _quicStreamSend(&f.a.ss, hog, src, 20000), 20000);
    CHECK_U("the quiet stream queued", _quicStreamSend(&f.a.ss, small, (const uint8*)"hi", 2), 2);

    // The busy stream has far more than one packet's worth. If it were served to exhaustion the
    // quiet stream would wait for twenty packets; taking turns means it goes out in the second.
    CHECK("first packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK("second packet built", sBuild(&f.a, SBUDGET) > 0);

    bool sawSmall = false;
    for (uint32 i = 0; i < f.a.nq; i++) {
        QuicRd rd;
        _quicRdInit(&rd, f.a.q[i].data, f.a.q[i].len);

        QuicFrame fr;
        while (_quicFrameDecode(&fr, &rd)) {
            if ((fr.type & ~(uint64)0x07) == QUIC_FRAME_STREAM && fr.stream.id == small)
                sawSmall = true;
        }
    }

    CHECK("the quiet stream got a turn", sawSmall);

out:
    xaFree(src);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_reorder(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;
    sFixInit(&f);

    // Three pieces of one stream, handed over back to front. Nothing can be read until the gap in
    // front of it is filled, and then all of it can.
    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type          = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_OFF | QUIC_STREAM_FIN;
    fr.stream.id     = 0;
    fr.stream.offset = 8;
    fr.stream.len    = 4;
    fr.stream.data   = (const uint8*)"cccc";
    CHECK("the last piece was accepted", sInject(&f.b, &fr));
    CHECK_U("nothing is readable yet", _quicStreamReadable(&f.b.ss, 0), 0);

    // The end of the stream is known, but the bytes in front of it are not all here. A reader
    // must not be told the stream is over while there is still a hole in it.
    uint8 tmp[16];
    bool early = false;
    CHECK_U("a read gets nothing", _quicStreamRecv(&f.b.ss, 0, tmp, sizeof(tmp), &early), 0);
    CHECK("and is not told the stream ended", !early);

    fr.type          = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_OFF;
    fr.stream.offset = 4;
    fr.stream.data   = (const uint8*)"bbbb";
    CHECK("the middle piece was accepted", sInject(&f.b, &fr));
    CHECK_U("still nothing readable", _quicStreamReadable(&f.b.ss, 0), 0);

    fr.type          = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    fr.stream.offset = 0;
    fr.stream.data   = (const uint8*)"aaaa";
    CHECK("the first piece was accepted", sInject(&f.b, &fr));

    bool fin = false;
    sRead(&f.b, 0, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 12);
    CHECK("in order", got && memcmp(got->data, "aaaabbbbcccc", 12) == 0);

    // The end of the stream was named by the piece that arrived first, and only counts as reached
    // once everything in front of it has been read.
    CHECK("the end of the stream was reached", fin);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Flow control
// ---------------------------------------------------------------------------------------------

int test_quicstreamtest_flow_stream(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxSdBidiRemote = 10;   // what the client may put on a stream it opened
    sFixInit2(&f, &atp, &btp);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("room before anything was written", _quicStreamWritable(&f.a.ss, id), 10);

    static const uint8 src[32] = { 0 };
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, src, sizeof(src)), 10);
    CHECK_U("no room left", _quicStreamWritable(&f.a.ss, id), 0);

    // Running out of a stream's window is a STREAM_DATA_BLOCKED, not a connection-wide one.
    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAM_DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_STREAM_DATA_BLOCKED), 1);
    CHECK_U("DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_DATA_BLOCKED), 0);
    CHECK("delivered", sDeliver(&f.a, &f.b));

    // Being refused again at the same limit is not news. The peer already knows where the stream
    // has stopped, and repeating it on every refused write would fill the connection with frames
    // saying nothing new.
    CHECK_U("still nothing taken", _quicStreamSend(&f.a.ss, id, src, sizeof(src)), 0);
    CHECK_U("and nothing more to send", sBuild(&f.a, SBUDGET), 0);

    CHECK("no connection error", sRun(&f));

    // Reading on the far end pushes the limit out again, and the sender hears about it.
    uint32 before = f.a.nwritable;
    bool fin      = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 10);

    CHECK("no connection error after the read", sRun(&f));
    CHECK("the sender was told it could write again", f.a.nwritable > before);
    CHECK("and it can", _quicStreamWritable(&f.a.ss, id) > 0);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_flow_conn(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxData = 12;   // across every stream at once
    sFixInit2(&f, &atp, &btp);

    uint64 a = 0, b = 0;
    CHECK("opened the first stream", _quicStreamOpen(&f.a.ss, false, &a));
    CHECK("opened the second stream", _quicStreamOpen(&f.a.ss, false, &b));

    static const uint8 src[32] = { 0 };
    CHECK_U("the first stream took", _quicStreamSend(&f.a.ss, a, src, 8), 8);

    // The second stream's own window is wide open, so what stops it is the connection's.
    CHECK_U("the second stream took", _quicStreamSend(&f.a.ss, b, src, 8), 4);
    CHECK_U("nothing more fits", _quicStreamWritable(&f.a.ss, b), 0);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_DATA_BLOCKED), 1);

    QuicFrame fr;
    CHECK("the complaint is there", sLastFrame(&f.a, QUIC_FRAME_DATA_BLOCKED, &fr));
    CHECK_U("the limit it names", fr.dataBlocked.limit, 12);

    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, a, &got, &fin);
    CHECK_U("the first stream's bytes", got ? got->len : 0, 8);

    CHECK("no connection error after the read", sRun(&f));
    CHECK("the connection window opened", _quicStreamWritable(&f.a.ss, b) > 0);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

// A sender that stops with room to spare still has to be woken.
//
// Spending a window down to exactly zero is something a framed protocol often cannot do: three
// spare bytes are not a frame, and there is nothing useful to put in them. The refused write is
// what says the sender is waiting, and it is the only thing that does -- no limit has been
// reached, so there is no complaint to send the peer and nothing on the wire to remember it by.
int test_quicstreamtest_flow_refused_stream(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxSdBidiRemote = 10;   // what the client may put on a stream it opened
    sFixInit2(&f, &atp, &btp);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));

    static const uint8 src[32] = { 0 };
    bool blocked               = false;
    CHECK("seven of the ten bytes taken", _quicStreamSendAll(&f.a.ss, id, src, 7, &blocked));
    CHECK_U("room left over", _quicStreamWritable(&f.a.ss, id), 3);

    CHECK("a write too large for it takes nothing",
          !_quicStreamSendAll(&f.a.ss, id, src, 8, &blocked));
    CHECK_U("and queues nothing behind it", _quicStreamWritable(&f.a.ss, id), 3);

    // The limit still has room, so there is nothing to complain about: asking the peer to raise a
    // limit that was never reached asks it for the wrong thing.
    CHECK("the refusal was not reported as a limit", !blocked);

    // Everything from the refusal on: the sender stopped here, so this is the point after which it
    // has to be woken. Which event does it is not the sender's business -- an acknowledgement
    // emptying the buffer will do it as readily as the window update, and either way it is told.
    uint32 before = f.a.nwritable;

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAM_DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_STREAM_DATA_BLOCKED), 0);
    CHECK_U("DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_DATA_BLOCKED), 0);
    CHECK("delivered", sDeliver(&f.a, &f.b));
    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 7);

    CHECK("no connection error after the read", sRun(&f));
    CHECK("the sender was told it could write again", f.a.nwritable > before);
    CHECK("and the write it was refused now fits", _quicStreamSendAll(&f.a.ss, id, src, 8, NULL));
    CHECK_U("which used the room the peer gave", _quicStreamWritable(&f.a.ss, id), 2);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

// The same, against the limit every stream shares. The connection is nowhere near its own limit
// when the sender gives up -- there are three bytes of it left -- so nothing about the connection
// says a stream is waiting. Only the stream that was refused knows.
int test_quicstreamtest_flow_refused_conn(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxData = 12;   // across every stream at once
    sFixInit2(&f, &atp, &btp);

    uint64 a = 0, b = 0;
    CHECK("opened the first stream", _quicStreamOpen(&f.a.ss, false, &a));
    CHECK("opened the second stream", _quicStreamOpen(&f.a.ss, false, &b));

    static const uint8 src[32] = { 0 };
    bool blocked               = false;
    CHECK("the first stream took nine", _quicStreamSendAll(&f.a.ss, a, src, 9, NULL));

    // The second stream's own window is wide open; what is left is the connection's three bytes.
    CHECK_U("what the connection leaves the second stream", _quicStreamWritable(&f.a.ss, b), 3);
    CHECK("a write too large for it takes nothing",
          !_quicStreamSendAll(&f.a.ss, b, src, 8, &blocked));
    CHECK("the refusal was not reported as a limit", !blocked);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_DATA_BLOCKED), 0);
    CHECK("delivered", sDeliver(&f.a, &f.b));
    CHECK("no connection error", sRun(&f));

    uint32 before = f.a.nwritable;
    bool fin      = false;
    sRead(&f.b, a, &got, &fin);
    CHECK_U("the first stream's bytes", got ? got->len : 0, 9);

    CHECK("no connection error after the read", sRun(&f));

    // Only the stream that was waiting: the first one was never held up, and telling it there is
    // room is noise it has to walk its own send path to discover it did not need.
    CHECK_U("wake-ups the raised limit produced", f.a.nwritable - before, 1);
    CHECK("and the refused write now fits", _quicStreamSendAll(&f.a.ss, b, src, 8, NULL));

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

// A write refused right at the peer's limit is one the peer can do something about, and it is
// told. This is the same complaint a partial write makes, from the path that takes nothing at all
// -- which is the only path a caller who cannot use a partial write ever goes down.
int test_quicstreamtest_flow_refused_blocked(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxSdBidiRemote = 10;
    sFixInit2(&f, &atp, &btp);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));

    static const uint8 src[32] = { 0 };
    bool blocked               = false;
    CHECK("the whole window taken", _quicStreamSendAll(&f.a.ss, id, src, 10, &blocked));
    CHECK("a write that fits reports no limit", !blocked);
    CHECK_U("no room left", _quicStreamWritable(&f.a.ss, id), 0);

    CHECK("nothing more is taken", !_quicStreamSendAll(&f.a.ss, id, src, 4, &blocked));
    CHECK("and this refusal is one the peer is told about", blocked);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAM_DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_STREAM_DATA_BLOCKED), 1);
    CHECK_U("DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_DATA_BLOCKED), 0);

    QuicFrame fr;
    CHECK("the complaint is there", sLastFrame(&f.a, QUIC_FRAME_STREAM_DATA_BLOCKED, &fr));
    CHECK_U("the limit it names", fr.streamDataBlocked.limit, 10);

    CHECK("delivered", sDeliver(&f.a, &f.b));
    CHECK("no connection error", sRun(&f));

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_flow_violate_stream(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxSdBidiRemote = 8;
    sFixInit2(&f, &tp, &tp);

    static const uint8 src[16] = { 0 };

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type          = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_OFF;
    fr.stream.id     = 0;
    fr.stream.offset = 4;
    fr.stream.len    = 8;   // reaches offset 12, past the 8 the server advertised
    fr.stream.data   = src;

    CHECK("the frame was refused", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_FLOW_CONTROL_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_flow_violate_conn(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 8;
    tp.initMaxSdBidiRemote = 1 << 14;   // the stream's own window is not what runs out
    sFixInit2(&f, &tp, &tp);

    static const uint8 src[16] = { 0 };

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    fr.stream.id   = 0;
    fr.stream.len  = 6;
    fr.stream.data = src;
    CHECK("the first frame was fine", sInject(&f.b, &fr));

    fr.type          = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_OFF;
    fr.stream.id     = 4;
    fr.stream.offset = 0;
    fr.stream.len    = 6;   // six more, on another stream, is twelve across the connection
    CHECK("the second frame was refused", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_FLOW_CONTROL_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_window(void)
{
    int ret    = 0;
    Buffer got = 0;
    uint8* src = NULL;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 4096;
    tp.initMaxSdBidiLocal  = 1024;
    tp.initMaxSdBidiRemote = 1024;
    sFixInit2(&f, &tp, &tp);

    src = xaAlloc(1024, XA_Zero);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));

    // A quarter of the stream's window: not enough for the receiver to say anything about.
    CHECK_U("first batch queued", _quicStreamSend(&f.a.ss, id, src, 256), 256);
    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 256);
    CHECK("nothing to announce yet", !_quicStreamsWantsToSend(&f.b.ss));

    // Past half the window, the limit is pushed out and the peer is told the new absolute offset.
    bufClear(got);
    CHECK_U("second batch queued", _quicStreamSend(&f.a.ss, id, src, 512), 512);
    CHECK("no connection error", sRun(&f));
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 512);

    CHECK("the receiver has something to say", _quicStreamsWantsToSend(&f.b.ss));
    CHECK("packet built", sBuild(&f.b, SBUDGET) > 0);

    QuicFrame fr;
    CHECK("MAX_STREAM_DATA was sent", sLastFrame(&f.b, QUIC_FRAME_MAX_STREAM_DATA, &fr));
    CHECK_U("the stream it names", fr.maxStreamData.id, id);
    CHECK_U("the new limit", fr.maxStreamData.max, 1024 + 768);

    CHECK("no connection error", sRun(&f));
    CHECK_U("the sender took the new limit", _quicStreamFind(&f.a.ss, id)->sendMax, 1024 + 768);

out:
    xaFree(src);
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Loss
// ---------------------------------------------------------------------------------------------

int test_quicstreamtest_retransmit(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"lost then found", 15), 15);
    _quicStreamFinish(&f.a.ss, id);

    // The one packet holding all of it never arrives.
    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("nothing more to send while it is in flight", sBuild(&f.a, SBUDGET), 0);
    sDrop(&f.a);

    CHECK("the sender has it again", _quicStreamsWantsToSend(&f.a.ss));
    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 15);
    CHECK("the bytes match", got && memcmp(got->data, "lost then found", 15) == 0);
    CHECK("the end of the stream arrived with them", fin);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

// What a stream reports as writable and what a send takes have to agree, however fragmented the
// send buffer has become.
//
// A write is all or nothing and an application sizes it by what it was told is available, so a
// refusal while room is reported leaves it with no way to make progress. Sending splits the ranges
// the buffer tracks, so this drives the stream a packet at a time -- each one leaving a range of
// its own -- and checks the two still agree afterwards.
// The end of a stream named after everything before it was acknowledged.
//
// By then the send buffer is empty and the stream has nothing else to say, so the end has nothing
// to ride out on and has to go in a frame of its own.
int test_quicstreamtest_finlate(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint8 src[5000];
    memset(src, 'q', sizeof(src));

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("queued", _quicStreamSend(&f.a.ss, id, src, sizeof(src)), sizeof(src));
    CHECK("ran", sRun(&f));

    Buffer got = 0;
    bool fin   = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("arrived", got ? got->len : 0, sizeof(src));
    bufDestroy(&got);

    CHECK("nothing more to send", sBuild(&f.a, SBUDGET) == 0);

    _quicStreamFinish(&f.a.ss, id);
    CHECK("the stream wants to send again", _quicStreamsWantsToSend(&f.a.ss));
    CHECK("a packet came out", sBuild(&f.a, SBUDGET) > 0);

    QuicFrame fr;
    bool sawFin = false;
    for (uint32 i = 0; i < f.a.nq; i++) {
        QuicRd rd;
        _quicRdInit(&rd, f.a.q[i].data, f.a.q[i].len);
        while (_quicFrameDecode(&fr, &rd)) {
            if ((fr.type & ~0x07u) == QUIC_FRAME_STREAM && (fr.type & QUIC_STREAM_FIN))
                sawFin = true;
        }
    }
    CHECK("the end of the stream went out", sawFin);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_sendchunks(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 1 << 20;   // neither flow control limit is what is being watched here
    tp.initMaxSdBidiLocal  = 1 << 20;
    tp.initMaxSdBidiRemote = 1 << 20;
    sFixInit2(&f, &tp, &tp);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));

    // One byte per packet leaves one range behind per packet, and ranges carried by different
    // packets cannot be joined, so the buffer ends up as fragmented as it can get.
    for (uint32 i = 0; i < SMAXQ; i++) {
        CHECK("there is room", _quicStreamWritable(&f.a.ss, id) > 0);
        CHECK_U("and a byte of it is taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"x", 1), 1);
        CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    }

    // Every one of those packets is still tracked separately, and the stream is still writable.
    QuicStreamState* st = _quicStreamFind(&f.a.ss, id);
    CHECK("the stream is still there", st != NULL);
    CHECK_U("nothing was forgotten", _quicSendBufOutstanding(&st->out), SMAXQ);
    CHECK("there is still room", _quicStreamWritable(&f.a.ss, id) > 0);
    CHECK_U("and it is taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"x", 1), 1);

    // Acknowledging them all collapses the ranges back to what is left unsent.
    for (uint32 i = 0; i < f.a.nq; i++)
        _quicStreamsAcked(&f.a.ss, f.a.q[i].pn);

    st = _quicStreamFind(&f.a.ss, id);
    CHECK("the stream is still there", st != NULL);
    CHECK_U("only the unsent byte is left", _quicSendBufOutstanding(&st->out), 1);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_retransmit_window(void)
{
    int ret    = 0;
    uint8* src = NULL;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 1 << 16;   // the connection window is not what is being watched here
    tp.initMaxSdBidiLocal  = 1024;
    tp.initMaxSdBidiRemote = 1024;
    sFixInit2(&f, &tp, &tp);

    src = xaAlloc(1024, XA_Zero);
    uint8 tmp[512];
    bool fin = false;

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("the whole window queued", _quicStreamSend(&f.a.ss, id, src, 1024), 1024);
    CHECK("no connection error", sRun(&f));

    // Half the window read is enough to be worth announcing.
    CHECK_U("first read", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 512);
    CHECK("packet built", sBuild(&f.b, SBUDGET) > 0);

    QuicFrame fr;
    CHECK("MAX_STREAM_DATA was in it", sLastFrame(&f.b, QUIC_FRAME_MAX_STREAM_DATA, &fr));
    CHECK_U("the limit it named", fr.maxStreamData.max, 1536);
    sDrop(&f.b);

    // It is owed again, and by the time it goes the reader has taken the rest.
    CHECK("the announcement is owed again", _quicStreamsWantsToSend(&f.b.ss));
    CHECK_U("second read", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 512);

    // What goes out is the limit as it stands, not the one that was lost. A window update names
    // an absolute offset, so the newest one says everything an older one would have.
    CHECK("packet built", sBuild(&f.b, SBUDGET) > 0);
    CHECK("MAX_STREAM_DATA went again", sLastFrame(&f.b, QUIC_FRAME_MAX_STREAM_DATA, &fr));
    CHECK_U("carrying the current limit", fr.maxStreamData.max, 2048);

    CHECK("no connection error", sRun(&f));
    CHECK_U("the sender took it", _quicStreamFind(&f.a.ss, id)->sendMax, 2048);

out:
    xaFree(src);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_retransmit_fin(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"body", 4), 4);
    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK("delivered", sDeliver(&f.a, &f.b));

    // The end of the stream has nothing left to ride along with, so it goes in a frame of its
    // own -- and that frame can be lost like any other.
    _quicStreamFinish(&f.a.ss, id);
    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAM frames in it", sCountStreams(&f.a), 1);

    QuicFrame fr;
    CHECK("the frame is there", sLastFrame(&f.a, QUIC_FRAME_STREAM | QUIC_STREAM_LEN |
                                                 QUIC_STREAM_OFF | QUIC_STREAM_FIN, &fr));
    CHECK_U("it carries no bytes", fr.stream.len, 0);
    CHECK_U("at the end of the stream", fr.stream.offset, 4);
    sDrop(&f.a);

    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, 4);
    CHECK("the end of the stream arrived after all", fin);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Giving up on a stream
// ---------------------------------------------------------------------------------------------

int test_quicstreamtest_reset(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"partial", 7), 7);
    CHECK("no connection error", sRun(&f));

    _quicStreamReset(&f.a.ss, id, 42);
    CHECK_U("nothing more may be written", _quicStreamWritable(&f.a.ss, id), 0);
    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);

    QuicFrame fr;
    CHECK("RESET_STREAM was sent", sLastFrame(&f.a, QUIC_FRAME_RESET_STREAM, &fr));
    CHECK_U("the stream it names", fr.resetStream.id, id);
    CHECK_U("the error it carries", fr.resetStream.error, 42);
    CHECK_U("where the stream stopped", fr.resetStream.finalSize, 7);

    CHECK("no connection error", sRun(&f));
    CHECK_U("the receiver heard about it", f.b.nreset, 1);
    CHECK_U("on which stream", f.b.resetId, id);
    CHECK_U("with which code", f.b.resetError, 42);

    // Whatever had arrived is gone, and the reader is told the stream ended rather than being
    // left waiting for the rest.
    CHECK_U("nothing is readable", _quicStreamReadable(&f.b.ss, id), 0);

    uint8 tmp[16];
    bool fin = false;

    // A retransmission of what was already sent arrives after the reset. There is nowhere for it
    // to go: the reader has been told the stream is over and will never come back for it.
    QuicFrame late;
    memset(&late, 0, sizeof(late));
    late.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    late.stream.id   = id;
    late.stream.len  = 7;
    late.stream.data = (const uint8*)"partial";
    CHECK("the late frame was accepted", sInject(&f.b, &late));
    CHECK_U("and nothing became readable", _quicStreamReadable(&f.b.ss, id), 0);

    CHECK_U("the read returns nothing", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 0);
    CHECK("but says the stream is over", fin);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_reset_credit(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData = 64;
    sFixInit2(&f, &tp, &tp);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"0123456789", 10), 10);
    CHECK("no connection error", sRun(&f));

    CHECK_U("the receiver counted them", f.b.ss.recvOff, 10);
    CHECK_U("and has read none", f.b.ss.recvRead, 0);

    // Bytes the reader will never see still have to give their credit back, or a connection that
    // resets streams runs its own window down to nothing.
    _quicStreamReset(&f.a.ss, id, 7);
    CHECK("no connection error", sRun(&f));

    CHECK_U("the credit came back", f.b.ss.recvRead, 10);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_stop(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"unwanted", 8), 8);
    CHECK("no connection error", sRun(&f));

    _quicStreamStopSending(&f.b.ss, id, 99);
    CHECK("no connection error", sRun(&f));

    CHECK_U("the sender heard about it", f.a.nstopped, 1);
    CHECK_U("on which stream", f.a.stopId, id);
    CHECK_U("with which code", f.a.stopError, 99);

    // RFC 9000 section 3.5: the answer is a RESET_STREAM, so the peer learns where the stream
    // ended rather than waiting for data that is not coming.
    CHECK_U("the receiver was reset", f.b.nreset, 1);
    CHECK_U("carrying the code it asked with", f.b.resetError, 99);
    CHECK_U("the sender takes nothing more", _quicStreamSend(&f.a.ss, id, (const uint8*)"x", 1), 0);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_reset_close(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"gone", 4), 4);
    CHECK("no connection error", sRun(&f));

    _quicStreamReset(&f.a.ss, id, 3);
    CHECK("no connection error", sRun(&f));

    // The sender's half is over once the reset is acknowledged, and a unidirectional stream has
    // no other half to wait for.
    CHECK_U("the sender let it go", f.a.nclosed, 1);
    CHECK("the sender forgot it", _quicStreamFind(&f.a.ss, id) == NULL);

    // The receiver still has to be told, which happens when the reader reaches the stream.
    CHECK_U("the receiver still holds it", f.b.nclosed, 0);

    uint8 tmp[8];
    bool fin = false;
    _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin);
    CHECK("the reader saw the end", fin);
    CHECK_U("and the receiver let it go", f.b.nclosed, 1);

out:
    sFixDestroy(&f);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// What a peer is not allowed to do
// ---------------------------------------------------------------------------------------------

int test_quicstreamtest_retransmit_reset(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"partial", 7), 7);
    CHECK("no connection error", sRun(&f));

    _quicStreamReset(&f.a.ss, id, 5);
    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("RESET_STREAM frames", sCountFrames(&f.a, QUIC_FRAME_RESET_STREAM), 1);
    sDrop(&f.a);

    // Where a stream ended is not something a later frame repeats, so the reset itself has to go
    // again or the peer waits forever for data that is not coming.
    CHECK("it is owed again", _quicStreamsWantsToSend(&f.a.ss));
    CHECK("no connection error", sRun(&f));
    CHECK_U("the receiver heard about it", f.b.nreset, 1);
    CHECK_U("with the code it carried", f.b.resetError, 5);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_retransmit_maxdata(void)
{
    int ret     = 0;
    uint8* src  = NULL;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 1024;   // the connection window, which is what is watched here
    tp.initMaxSdBidiLocal  = 1 << 16;
    tp.initMaxSdBidiRemote = 1 << 16;
    sFixInit2(&f, &tp, &tp);

    src = xaAlloc(1024, XA_Zero);
    uint8 tmp[1024];
    bool fin = false;

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("the whole window queued", _quicStreamSend(&f.a.ss, id, src, 1024), 1024);
    CHECK("no connection error", sRun(&f));

    CHECK_U("everything was read", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 1024);
    CHECK("packet built", sBuild(&f.b, SBUDGET) > 0);
    CHECK_U("MAX_DATA frames", sCountFrames(&f.b, QUIC_FRAME_MAX_DATA), 1);
    sDrop(&f.b);

    CHECK("it is owed again", _quicStreamsWantsToSend(&f.b.ss));
    CHECK("no connection error", sRun(&f));
    CHECK_U("the sender took the new limit", f.a.ss.sendMax, 2048);

out:
    xaFree(src);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_blocked_stale(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxSdBidiRemote = 8;
    sFixInit2(&f, &atp, &btp);

    static const uint8 src[32] = { 0 };

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, src, sizeof(src)), 8);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAM_DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_STREAM_DATA_BLOCKED), 1);
    sDrop(&f.a);

    // The limit moves before the complaint can be repeated. Saying it now would be telling the
    // peer about a wall that is no longer there.
    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type              = QUIC_FRAME_MAX_STREAM_DATA;
    fr.maxStreamData.id  = id;
    fr.maxStreamData.max = 1000;
    CHECK("the new limit arrived", sInject(&f.a, &fr));

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("no stale complaint went out", sCountFrames(&f.a, QUIC_FRAME_STREAM_DATA_BLOCKED), 0);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_fin_outstanding(void)
{
    int ret    = 0;
    Buffer got = 0;
    uint8* src = NULL;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxSdUni = 1 << 16;
    sFixInit2(&f, &tp, &tp);

    const size_t total = 2000;
    src                = xaAlloc(total);
    for (size_t i = 0; i < total; i++)
        src[i] = (uint8)(i * 17 + 3);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, src, total), total);
    _quicStreamFinish(&f.a.ss, id);

    // Two packets, and the end of the stream is in the second one. Only that one arrives.
    CHECK("first packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK("second packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("that was all of it", sBuild(&f.a, SBUDGET), 0);
    CHECK("the second was delivered", sDeliverSome(&f.a, &f.b, 2));

    // The end of the stream being acknowledged does not mean the stream is finished with: what
    // came before it is still owed, and letting go here would lose it.
    CHECK("the sender still holds the stream", _quicStreamFind(&f.a.ss, id) != NULL);
    CHECK_U("and has not let it go", f.a.nclosed, 0);

    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK_U("bytes read", got ? got->len : 0, total);
    CHECK("the bytes match", got && memcmp(got->data, src, total) == 0);
    CHECK("the end of the stream arrived", fin);

out:
    xaFree(src);
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_reset_pending(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"never sent", 10), 10);
    _quicStreamFinish(&f.a.ss, id);

    // Giving up on a stream before any of it has gone out. Everything queued stops being owed,
    // including the end of the stream that was about to be named.
    _quicStreamReset(&f.a.ss, id, 9);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("RESET_STREAM frames", sCountFrames(&f.a, QUIC_FRAME_RESET_STREAM), 1);
    CHECK_U("STREAM frames", sCountStreams(&f.a), 0);

    QuicFrame fr;
    CHECK("the reset is there", sLastFrame(&f.a, QUIC_FRAME_RESET_STREAM, &fr));
    CHECK_U("naming where the stream stopped", fr.resetStream.finalSize, 10);

    CHECK("no connection error", sRun(&f));
    CHECK_U("the sender let it go", f.a.nclosed, 1);
    CHECK("the sender forgot it", _quicStreamFind(&f.a.ss, id) == NULL);
    CHECK_U("the receiver heard about it", f.b.nreset, 1);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_blocked_stale_conn(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxData = 8;
    sFixInit2(&f, &atp, &btp);

    static const uint8 src[32] = { 0 };

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, src, sizeof(src)), 8);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_DATA_BLOCKED), 1);
    sDrop(&f.a);

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_MAX_DATA;
    fr.maxData.max = 1000;
    CHECK("the new limit arrived", sInject(&f.a, &fr));

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("no stale complaint went out", sCountFrames(&f.a, QUIC_FRAME_DATA_BLOCKED), 0);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_blocked_stale_streams(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    for (int i = 0; i < 4; i++)
        CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK("the fifth was refused", !_quicStreamOpen(&f.a.ss, false, &id));

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("STREAMS_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_STREAMS_BLOCKED_BIDI), 1);
    sDrop(&f.a);

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type            = QUIC_FRAME_MAX_STREAMS_BIDI;
    fr.maxStreams.max  = 8;
    CHECK("the new limit arrived", sInject(&f.a, &fr));

    CHECK("nothing is owed any more", !_quicStreamsWantsToSend(&f.a.ss));
    sBuild(&f.a, SBUDGET);
    CHECK_U("no stale complaint went out", sCountFrames(&f.a, QUIC_FRAME_STREAMS_BLOCKED_BIDI), 0);
    CHECK("and a fifth stream can be opened", _quicStreamOpen(&f.a.ss, false, &id));

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_reset_drops_blocked(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    btp.initMaxSdBidiRemote = 8;
    sFixInit2(&f, &atp, &btp);

    static const uint8 src[32] = { 0 };

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, src, sizeof(src)), 8);

    // Giving up on the stream makes the complaint about its window pointless: there is nothing
    // left that a wider window would let through.
    _quicStreamReset(&f.a.ss, id, 1);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("RESET_STREAM frames", sCountFrames(&f.a, QUIC_FRAME_RESET_STREAM), 1);
    CHECK_U("STREAM_DATA_BLOCKED frames", sCountFrames(&f.a, QUIC_FRAME_STREAM_DATA_BLOCKED), 0);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_window_ended(void)
{
    int ret     = 0;
    uint8* src  = NULL;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxData         = 1 << 16;
    tp.initMaxSdBidiLocal  = 1024;
    tp.initMaxSdBidiRemote = 1024;
    sFixInit2(&f, &tp, &tp);

    src = xaAlloc(1024, XA_Zero);
    uint8 tmp[2048];
    bool fin = false;

    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, false, &id));
    CHECK_U("the whole window queued", _quicStreamSend(&f.a.ss, id, src, 1024), 1024);
    _quicStreamFinish(&f.a.ss, id);
    CHECK("no connection error", sRun(&f));

    CHECK_U("everything was read", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 1024);
    CHECK("the stream ended", fin);

    // Far more than half the window was read, but the stream is over: offering the peer room on a
    // stream it has already finished says nothing.
    sBuild(&f.b, SBUDGET);
    CHECK_U("no window was offered", sCountFrames(&f.b, QUIC_FRAME_MAX_STREAM_DATA), 0);

out:
    xaFree(src);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_partial_read(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"0123456789", 10), 10);
    _quicStreamFinish(&f.a.ss, id);
    CHECK("no connection error", sRun(&f));

    CHECK_U("the reader was told there was something", f.b.nreadable > 0, 1);

    // Everything has arrived, but the reader has only taken part of it. The stream is not over
    // until the reader has actually seen the end of it.
    uint8 tmp[4];
    bool fin = false;
    CHECK_U("first read", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 4);
    CHECK("the stream has not ended", !fin);
    CHECK_U("the rest is still there", _quicStreamReadable(&f.b.ss, id), 6);
    CHECK_U("the receiver still holds it", f.b.nclosed, 0);

    CHECK_U("second read", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 4);
    CHECK("still not over", !fin);
    CHECK_U("third read", _quicStreamRecv(&f.b.ss, id, tmp, sizeof(tmp), &fin), 2);
    CHECK("now it is", fin);
    CHECK_U("and the receiver let it go", f.b.nclosed, 1);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_reset_twice(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"partial", 7), 7);

    _quicStreamReset(&f.a.ss, id, 1);

    // Asking again changes nothing. A second reset would name a different place for the stream to
    // have ended, which the peer is entitled to treat as a connection error.
    _quicStreamReset(&f.a.ss, id, 2);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("RESET_STREAM frames", sCountFrames(&f.a, QUIC_FRAME_RESET_STREAM), 1);

    QuicFrame fr;
    CHECK("the reset is there", sLastFrame(&f.a, QUIC_FRAME_RESET_STREAM, &fr));
    CHECK_U("where the stream stopped", fr.resetStream.finalSize, 7);
    CHECK_U("with the first code", fr.resetStream.error, 1);

    CHECK("no connection error", sRun(&f));
    CHECK_U("the receiver heard about it once", f.b.nreset, 1);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_finish_after_reset(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"gone", 4), 4);

    _quicStreamReset(&f.a.ss, id, 4);

    // The stream has already been abandoned, so there is no end left to name.
    _quicStreamFinish(&f.a.ss, id);
    CHECK_U("and nothing more may be written", _quicStreamSend(&f.a.ss, id, (const uint8*)"x", 1),
            0);

    CHECK("packet built", sBuild(&f.a, SBUDGET) > 0);
    CHECK_U("RESET_STREAM frames", sCountFrames(&f.a, QUIC_FRAME_RESET_STREAM), 1);
    CHECK_U("STREAM frames", sCountStreams(&f.a), 0);

    CHECK("no connection error", sRun(&f));
    CHECK_U("the receiver heard only about the reset", f.b.nreset, 1);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_stop_after_end(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;
    sFixInit(&f);

    uint64 id = 0;
    CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"all of it", 9), 9);
    _quicStreamFinish(&f.a.ss, id);
    CHECK("no connection error", sRun(&f));

    // The whole stream is here, so there is nothing left for the peer to stop sending.
    _quicStreamStopSending(&f.b.ss, id, 1);
    CHECK("nothing is owed", !_quicStreamsWantsToSend(&f.b.ss));
    sBuild(&f.b, SBUDGET);
    CHECK_U("no STOP_SENDING went out", sCountFrames(&f.b, QUIC_FRAME_STOP_SENDING), 0);

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK("the stream still ended normally", fin);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_reset_duplicate(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type                  = QUIC_FRAME_RESET_STREAM;
    fr.resetStream.id        = 0;
    fr.resetStream.error     = 8;
    fr.resetStream.finalSize = 6;

    CHECK("the reset arrived", sInject(&f.b, &fr));
    CHECK_U("the receiver heard about it", f.b.nreset, 1);
    CHECK_U("and gave the credit back", f.b.ss.recvRead, 6);

    // The sender retransmits it because it has not been acknowledged yet. Saying the same thing
    // twice is not news, and must not be counted twice either.
    CHECK("the copy arrived", sInject(&f.b, &fr));
    CHECK_U("the receiver was not told again", f.b.nreset, 1);
    CHECK_U("and the credit was not given twice", f.b.ss.recvRead, 6);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_final_size(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    static const uint8 src[16] = { 0 };

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_FIN;
    fr.stream.id   = 0;
    fr.stream.len  = 8;
    fr.stream.data = src;
    CHECK("the stream ended at eight", sInject(&f.b, &fr));

    // A byte past the end that was named.
    fr.type          = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_OFF;
    fr.stream.offset = 8;
    fr.stream.len    = 1;
    CHECK("more data was refused", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_FINAL_SIZE_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_final_size_moved(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    static const uint8 src[16] = { 0 };

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_FIN;
    fr.stream.id   = 0;
    fr.stream.len  = 8;
    fr.stream.data = src;
    CHECK("the stream ended at eight", sInject(&f.b, &fr));

    // A second end, in a different place. Where a stream ends is settled once and cannot move,
    // in either direction.
    fr.stream.len = 4;
    CHECK("a nearer end was refused", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_FINAL_SIZE_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_final_size_reset(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    static const uint8 src[16] = { 0 };

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    fr.stream.id   = 0;
    fr.stream.len  = 10;
    fr.stream.data = src;
    CHECK("ten bytes arrived", sInject(&f.b, &fr));

    // A reset says where the stream stopped, and it cannot stop before bytes that were already
    // sent on it.
    memset(&fr, 0, sizeof(fr));
    fr.type                  = QUIC_FRAME_RESET_STREAM;
    fr.resetStream.id        = 0;
    fr.resetStream.error     = 1;
    fr.resetStream.finalSize = 4;
    CHECK("the reset was refused", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_FINAL_SIZE_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_final_size_moved_reset(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    static const uint8 src[16] = { 0 };

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_FIN;
    fr.stream.id   = 0;
    fr.stream.len  = 8;
    fr.stream.data = src;
    CHECK("the stream ended at eight", sInject(&f.b, &fr));

    // A reset after the end of the stream is already known has to agree with it. This is the same
    // rule as two STREAM frames disagreeing, reached along the other path.
    memset(&fr, 0, sizeof(fr));
    fr.type                  = QUIC_FRAME_RESET_STREAM;
    fr.resetStream.id        = 0;
    fr.resetStream.error     = 1;
    fr.resetStream.finalSize = 12;
    CHECK("the reset was refused", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_FINAL_SIZE_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_limit(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    static const uint8 src[4] = { 0 };

    // The server advertised four bidi streams, so index four is one too many.
    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    fr.stream.id   = 16;
    fr.stream.len  = 4;
    fr.stream.data = src;

    CHECK("the frame was refused", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_STREAM_LIMIT_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_implicit(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);

    static const uint8 src[4] = { 0 };

    // Naming stream 8 opens 0 and 4 as well: the peer may use them in any order, so they have to
    // exist the moment one past them does.
    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    fr.stream.id   = 8;
    fr.stream.len  = 4;
    fr.stream.data = src;
    CHECK("the frame was accepted", sInject(&f.b, &fr));

    CHECK_U("streams opened", f.b.nopened, 3);
    CHECK("stream 0 exists", _quicStreamFind(&f.b.ss, 0) != NULL);
    CHECK("stream 4 exists", _quicStreamFind(&f.b.ss, 4) != NULL);
    CHECK("stream 8 exists", _quicStreamFind(&f.b.ss, 8) != NULL);

    // Bidirectional streams are counted apart from unidirectional ones, so opening 8 says nothing
    // about how many of those exist.
    CHECK("no uni streams were opened", _quicStreamFind(&f.b.ss, 2) == NULL);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_state_errors(void)
{
    int ret = 0;
    SFix f;

    // A stream this endpoint opened one way cannot have data sent back along it.
    {
        sFixInit(&f);

        uint64 id = 0;
        CHECK("opened a uni stream", _quicStreamOpen(&f.a.ss, true, &id));

        static const uint8 src[4] = { 0 };
        QuicFrame fr;
        memset(&fr, 0, sizeof(fr));
        fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
        fr.stream.id   = id;
        fr.stream.len  = 4;
        fr.stream.data = src;

        CHECK("data on it was refused", !sInject(&f.a, &fr));
        CHECK_U("the error", f.a.ss.error, QUIC_ERR_STREAM_STATE_ERROR);
        sFixDestroy(&f);
    }

    // The mirror: a stream the peer opened one way cannot be told to stop sending on the end that
    // never sends.
    {
        sFixInit(&f);

        QuicFrame fr;
        memset(&fr, 0, sizeof(fr));
        fr.type             = QUIC_FRAME_STOP_SENDING;
        fr.stopSending.id   = 3;   // a server-initiated uni stream, seen by the server
        fr.stopSending.error = 1;

        CHECK("it was refused", !sInject(&f.a, &fr));
        CHECK_U("the error", f.a.ss.error, QUIC_ERR_STREAM_STATE_ERROR);
        sFixDestroy(&f);
    }

    // A window announcement for a stream the announcer can only receive on.
    {
        sFixInit(&f);

        QuicFrame fr;
        memset(&fr, 0, sizeof(fr));
        fr.type                = QUIC_FRAME_MAX_STREAM_DATA;
        fr.maxStreamData.id    = 2;   // a client-initiated uni stream, seen by the client
        fr.maxStreamData.max   = 1000;

        CHECK("it was refused", !sInject(&f.b, &fr));
        CHECK_U("the error", f.b.ss.error, QUIC_ERR_STREAM_STATE_ERROR);
        sFixDestroy(&f);
    }

    // A frame about a stream this endpoint would have opened but has not.
    {
        sFixInit(&f);

        QuicFrame fr;
        memset(&fr, 0, sizeof(fr));
        fr.type                = QUIC_FRAME_MAX_STREAM_DATA;
        fr.maxStreamData.id    = 0;   // client-initiated, and the client has opened nothing
        fr.maxStreamData.max   = 1000;

        CHECK("it was refused", !sInject(&f.a, &fr));
        CHECK_U("the error", f.a.ss.error, QUIC_ERR_STREAM_STATE_ERROR);
        sFixDestroy(&f);
    }

    memset(&f, 0, sizeof(f));

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_refuse(void)
{
    int ret = 0;
    SFix f;
    sFixInit(&f);
    f.b.refuseOpen = true;

    static const uint8 src[4] = { 0 };

    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    fr.stream.id   = 0;
    fr.stream.len  = 4;
    fr.stream.data = src;

    CHECK("the stream was turned down", !sInject(&f.b, &fr));
    CHECK_U("the error", f.b.ss.error, QUIC_ERR_STREAM_LIMIT_ERROR);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_closed_ignored(void)
{
    int ret    = 0;
    Buffer got = 0;
    SFix f;
    sFixInit(&f);

    // A unidirectional stream has only the one half, so reading it to the end is all it takes
    // for the receiver to be finished with it.
    uint64 id = 0;
    CHECK("opened a stream", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"done", 4), 4);
    _quicStreamFinish(&f.a.ss, id);
    CHECK("no connection error", sRun(&f));

    bool fin = false;
    sRead(&f.b, id, &got, &fin);
    CHECK("the stream ended", fin);
    CHECK("no connection error", sRun(&f));
    CHECK("the server forgot the stream", _quicStreamFind(&f.b.ss, id) == NULL);

    // A retransmission of something the server has already finished with arrives after the stream
    // is gone. There is nothing to do with it, but it is not an error either -- the sender simply
    // had not heard yet.
    QuicFrame fr;
    memset(&fr, 0, sizeof(fr));
    fr.type        = QUIC_FRAME_STREAM | QUIC_STREAM_LEN | QUIC_STREAM_FIN;
    fr.stream.id   = id;
    fr.stream.len  = 4;
    fr.stream.data = (const uint8*)"done";

    CHECK("the late frame was ignored", sInject(&f.b, &fr));
    CHECK_U("no error was raised", f.b.ss.error, 0);
    CHECK("and no stream came back", _quicStreamFind(&f.b.ss, id) == NULL);

out:
    bufDestroy(&got);
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_maxstreams(void)
{
    int ret = 0;
    SFix f;

    QuicTransportParams tp;
    sTpDefaults(&tp);
    tp.initMaxStreamsUni = 1;   // one at a time, so the limit has to move for a second to open
    sFixInit2(&f, &tp, &tp);

    uint64 id = 0;
    CHECK("opened the one stream allowed", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK("a second was refused", !_quicStreamOpen(&f.a.ss, true, &id));

    CHECK_U("bytes taken", _quicStreamSend(&f.a.ss, id, (const uint8*)"first", 5), 5);
    _quicStreamFinish(&f.a.ss, id);
    CHECK("no connection error", sRun(&f));

    Buffer got = 0;
    bool fin   = false;
    sRead(&f.b, id, &got, &fin);
    bufDestroy(&got);
    CHECK("the stream ended", fin);

    // Letting go of a stream the peer opened gives its place back, which the peer hears as a
    // MAX_STREAMS naming the new total.
    CHECK("the server let it go", f.b.nclosed == 1);
    CHECK("no connection error", sRun(&f));

    CHECK("a second stream can be opened now", _quicStreamOpen(&f.a.ss, true, &id));
    CHECK_U("with the next id", id, 6);

    // Letting go of a stream this endpoint opened gives nothing back to the peer: the place that
    // was freed was one of this endpoint's own, and the peer's allowance is a separate count.
    CHECK_U("the client's allowance to the server is untouched",
            f.a.ss.recvMaxStreams[QUIC_SDIR_UNI], 1);

out:
    sFixDestroy(&f);
    return ret;
}

int test_quicstreamtest_bidi_params(void)
{
    int ret = 0;
    SFix f;

    // Deliberately different in every direction, so a value read from the wrong parameter shows
    // up as the wrong number rather than as the right one by luck.
    QuicTransportParams atp, btp;
    sTpDefaults(&atp);
    sTpDefaults(&btp);
    atp.initMaxSdBidiLocal  = 111;
    atp.initMaxSdBidiRemote = 222;
    btp.initMaxSdBidiLocal  = 333;
    btp.initMaxSdBidiRemote = 444;
    sFixInit2(&f, &atp, &btp);

    uint64 cid = 0, sid = 0;
    CHECK("the client opened a stream", _quicStreamOpen(&f.a.ss, false, &cid));
    CHECK("the server opened a stream", _quicStreamOpen(&f.b.ss, false, &sid));

    // Each parameter is named for whoever opened the stream, so the sender on a stream reads the
    // peer's parameter for the end that did not open it.
    CHECK_U("what the client may put on its own stream", _quicStreamWritable(&f.a.ss, cid), 444);
    CHECK_U("what the server may put on its own stream", _quicStreamWritable(&f.b.ss, sid), 222);

    // And the receiving end's limit comes from its own parameter for the same stream.
    QuicStreamState* st = _quicStreamFind(&f.a.ss, cid);
    CHECK("the client's stream exists", st != NULL);
    CHECK_U("what the client will accept on it", st->in.limit, 111);

    st = _quicStreamFind(&f.b.ss, sid);
    CHECK("the server's stream exists", st != NULL);
    CHECK_U("what the server will accept on it", st->in.limit, 333);

out:
    sFixDestroy(&f);
    return ret;
}

// Each group below runs several of the subtests above in one process, so ctest spends one
// process launch per feature area instead of one per subtest. The individual subtests stay
// registered under their own names too, for running or debugging one in isolation.

int test_quicstreamtest_grp_lifecycle(void)
{
    TEST_CHAIN(test_quicstreamtest_ids, test_quicstreamtest_open, test_quicstreamtest_openlimit,
               test_quicstreamtest_limit, test_quicstreamtest_implicit,
               test_quicstreamtest_bidi_params, test_quicstreamtest_maxstreams);
}

int test_quicstreamtest_grp_transfer(void)
{
    TEST_CHAIN(test_quicstreamtest_send, test_quicstreamtest_bulk, test_quicstreamtest_uni,
               test_quicstreamtest_interleave, test_quicstreamtest_fairness,
               test_quicstreamtest_reorder, test_quicstreamtest_sendchunks,
               test_quicstreamtest_partial_read);
}

int test_quicstreamtest_grp_flow(void)
{
    TEST_CHAIN(test_quicstreamtest_flow_stream, test_quicstreamtest_flow_conn,
               test_quicstreamtest_flow_refused_stream, test_quicstreamtest_flow_refused_conn,
               test_quicstreamtest_flow_refused_blocked, test_quicstreamtest_flow_violate_stream,
               test_quicstreamtest_flow_violate_conn, test_quicstreamtest_window,
               test_quicstreamtest_window_ended);
}

int test_quicstreamtest_grp_blocked(void)
{
    TEST_CHAIN(test_quicstreamtest_blocked_stale, test_quicstreamtest_blocked_stale_conn,
               test_quicstreamtest_blocked_stale_streams, test_quicstreamtest_reset_drops_blocked);
}

int test_quicstreamtest_grp_retransmit(void)
{
    TEST_CHAIN(test_quicstreamtest_retransmit, test_quicstreamtest_retransmit_window,
               test_quicstreamtest_finlate, test_quicstreamtest_retransmit_fin,
               test_quicstreamtest_retransmit_reset, test_quicstreamtest_retransmit_maxdata,
               test_quicstreamtest_fin_outstanding);
}

int test_quicstreamtest_grp_reset(void)
{
    TEST_CHAIN(test_quicstreamtest_reset, test_quicstreamtest_reset_credit,
               test_quicstreamtest_stop, test_quicstreamtest_reset_close,
               test_quicstreamtest_reset_twice, test_quicstreamtest_finish_after_reset,
               test_quicstreamtest_stop_after_end, test_quicstreamtest_reset_duplicate,
               test_quicstreamtest_reset_pending);
}

int test_quicstreamtest_grp_finalsize(void)
{
    TEST_CHAIN(test_quicstreamtest_final_size, test_quicstreamtest_final_size_moved,
               test_quicstreamtest_final_size_reset, test_quicstreamtest_final_size_moved_reset,
               test_quicstreamtest_state_errors, test_quicstreamtest_refuse,
               test_quicstreamtest_closed_ignored);
}

testfunc quicstreamtest_funcs[] = {
    { "ids", test_quicstreamtest_ids },
    { "open", test_quicstreamtest_open },
    { "openlimit", test_quicstreamtest_openlimit },
    { "send", test_quicstreamtest_send },
    { "bulk", test_quicstreamtest_bulk },
    { "uni", test_quicstreamtest_uni },
    { "interleave", test_quicstreamtest_interleave },
    { "fairness", test_quicstreamtest_fairness },
    { "reorder", test_quicstreamtest_reorder },
    { "flow_stream", test_quicstreamtest_flow_stream },
    { "flow_conn", test_quicstreamtest_flow_conn },
    { "flow_refused_stream", test_quicstreamtest_flow_refused_stream },
    { "flow_refused_conn", test_quicstreamtest_flow_refused_conn },
    { "flow_refused_blocked", test_quicstreamtest_flow_refused_blocked },
    { "flow_violate_stream", test_quicstreamtest_flow_violate_stream },
    { "flow_violate_conn", test_quicstreamtest_flow_violate_conn },
    { "window", test_quicstreamtest_window },
    { "retransmit", test_quicstreamtest_retransmit },
    { "retransmit_window", test_quicstreamtest_retransmit_window },
    { "sendchunks",      test_quicstreamtest_sendchunks      },
    { "finlate",         test_quicstreamtest_finlate         },
    { "retransmit_fin", test_quicstreamtest_retransmit_fin },
    { "reset", test_quicstreamtest_reset },
    { "reset_credit", test_quicstreamtest_reset_credit },
    { "stop", test_quicstreamtest_stop },
    { "reset_close", test_quicstreamtest_reset_close },
    { "partial_read", test_quicstreamtest_partial_read },
    { "reset_twice", test_quicstreamtest_reset_twice },
    { "finish_after_reset", test_quicstreamtest_finish_after_reset },
    { "stop_after_end", test_quicstreamtest_stop_after_end },
    { "reset_duplicate", test_quicstreamtest_reset_duplicate },
    { "reset_pending", test_quicstreamtest_reset_pending },
    { "retransmit_reset", test_quicstreamtest_retransmit_reset },
    { "retransmit_maxdata", test_quicstreamtest_retransmit_maxdata },
    { "blocked_stale", test_quicstreamtest_blocked_stale },
    { "blocked_stale_conn", test_quicstreamtest_blocked_stale_conn },
    { "blocked_stale_streams", test_quicstreamtest_blocked_stale_streams },
    { "reset_drops_blocked", test_quicstreamtest_reset_drops_blocked },
    { "window_ended", test_quicstreamtest_window_ended },
    { "fin_outstanding", test_quicstreamtest_fin_outstanding },
    { "final_size", test_quicstreamtest_final_size },
    { "final_size_moved", test_quicstreamtest_final_size_moved },
    { "final_size_reset", test_quicstreamtest_final_size_reset },
    { "final_size_moved_reset", test_quicstreamtest_final_size_moved_reset },
    { "limit", test_quicstreamtest_limit },
    { "implicit", test_quicstreamtest_implicit },
    { "state_errors", test_quicstreamtest_state_errors },
    { "refuse", test_quicstreamtest_refuse },
    { "closed_ignored", test_quicstreamtest_closed_ignored },
    { "maxstreams", test_quicstreamtest_maxstreams },
    { "bidi_params", test_quicstreamtest_bidi_params },
    { "grp_lifecycle", test_quicstreamtest_grp_lifecycle },
    { "grp_transfer", test_quicstreamtest_grp_transfer },
    { "grp_flow", test_quicstreamtest_grp_flow },
    { "grp_blocked", test_quicstreamtest_grp_blocked },
    { "grp_retransmit", test_quicstreamtest_grp_retransmit },
    { "grp_reset", test_quicstreamtest_grp_reset },
    { "grp_finalsize", test_quicstreamtest_grp_finalsize },
    { NULL, NULL },
};
