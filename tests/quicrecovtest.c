// Loss recovery, congestion control, and the send buffer that feeds them.
//
// Nothing here builds a packet or opens one. The harness tells the recovery engine that a packet
// of a given size went out at a given moment, hands it acknowledgements built with the real ACK
// frame encoder, and moves a clock it owns -- which is what makes the round trip estimates, the
// probe timeouts, and the congestion window trajectories exact numbers a test can assert rather
// than ranges it has to tolerate.

#include <cxquic.h>

#include "../cxquic/conn_private.h"

#define TEST_FILE  quicrecovtest
#define TEST_FUNCS quicrecovtest_funcs
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

#define RMTU 1200

// ---------------------------------------------------------------------------------------------
// The harness
// ---------------------------------------------------------------------------------------------

#define RMAX_REPORT 64

typedef struct RFix {
    QuicRecovery r;

    uint64 acked[RMAX_REPORT];
    uint32 nacked;
    uint64 lost[RMAX_REPORT];
    uint32 nlost;

    int64 now;
} RFix;

static void rAckedCb(_In_opt_ void* ctx, int sp, uint64 pn)
{
    RFix* rf = ctx;
    unused_noeval(sp);
    if (rf->nacked < RMAX_REPORT)
        rf->acked[rf->nacked++] = pn;
}

static void rLostCb(_In_opt_ void* ctx, int sp, uint64 pn)
{
    RFix* rf = ctx;
    unused_noeval(sp);
    if (rf->nlost < RMAX_REPORT)
        rf->lost[rf->nlost++] = pn;
}

static const QuicRecovHandlers rHandlers = {
    .acked = rAckedCb,
    .lost  = rLostCb,
};

static void rInit(_Out_ RFix* rf, bool server)
{
    memset(rf, 0, sizeof(*rf));
    _quicRecovInit(&rf->r, server, RMTU);
    _quicRecovSetHandlers(&rf->r, &rHandlers, rf);
    rf->now = timeS(1000);
}

static void rDestroy(_Inout_ RFix* rf)
{
    _quicRecovDestroy(&rf->r);
}

// Sends one full-sized ack-eliciting packet.
static void rSend(_Inout_ RFix* rf, int sp, uint64 pn)
{
    _quicRecovOnSent(&rf->r, sp, pn, RMTU, true, true, rf->now);
}

// Hands over an acknowledgement of one contiguous range, with `delay` as the time the peer says
// it held on to it.
static bool rAck(_Inout_ RFix* rf, int sp, uint64 smallest, uint64 largest, int64 delay)
{
    QuicAckRange rg;
    rg.largest  = largest;
    rg.smallest = smallest;

    uint8 rb[QUIC_MAX_ACK_RANGES * 20];
    QuicFrame f;
    if (!_quicAckBuild(&f, 0, &rg, 1, NULL, rb, sizeof(rb)))
        return false;

    rf->nacked = 0;
    rf->nlost  = 0;
    return _quicRecovOnAck(&rf->r, sp, &f, delay, rf->now);
}

static bool rReported(_In_reads_(n) const uint64* list, uint32 n, uint64 pn)
{
    for (uint32 i = 0; i < n; i++) {
        if (list[i] == pn)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// The send buffer
// ---------------------------------------------------------------------------------------------

int test_quicrecovtest_sendbuf(void)
{
    int ret = 0;
    QuicSendBuf sb;
    _quicSendBufInit(&sb);

    uint64 off;
    const uint8* data;
    size_t len;

    CHECK("nothing is waiting in an empty buffer", !_quicSendBufPending(&sb));
    CHECK("an empty buffer offers nothing", !_quicSendBufNext(&sb, 100, &off, &data, &len));

    CHECK("add", _quicSendBufAdd(&sb, (const uint8*)"abcdefghij", 10));
    CHECK("something is waiting", _quicSendBufPending(&sb));
    CHECK_U("outstanding", _quicSendBufOutstanding(&sb), 10);

    // Only part of it fits, so only part of it is handed out.
    CHECK("next", _quicSendBufNext(&sb, 4, &off, &data, &len));
    CHECK_U("offset", off, 0);
    CHECK_U("length", len, 4);
    CHECK("contents", memcmp(data, "abcd", 4) == 0);

    _quicSendBufSent(&sb, 0, 4, 7);

    CHECK("the rest is still waiting", _quicSendBufNext(&sb, 100, &off, &data, &len));
    CHECK_U("offset of the rest", off, 4);
    CHECK_U("length of the rest", len, 6);
    _quicSendBufSent(&sb, 4, 6, 8);

    CHECK("everything has been sent", !_quicSendBufPending(&sb));
    CHECK_U("outstanding after sending", _quicSendBufOutstanding(&sb), 10);

    // Losing the second packet puts only its bytes back.
    _quicSendBufLost(&sb, 8);
    CHECK("the lost range is waiting again", _quicSendBufNext(&sb, 100, &off, &data, &len));
    CHECK_U("offset of the lost range", off, 4);
    CHECK_U("length of the lost range", len, 6);
    CHECK("contents of the lost range", memcmp(data, "efghij", 6) == 0);
    _quicSendBufSent(&sb, 4, 6, 9);

    // Acknowledging out of order releases nothing until the front goes, since what is behind it
    // may still have to be sent again.
    _quicSendBufAcked(&sb, 9);
    CHECK_U("base before the front was acknowledged", sb.base, 0);
    CHECK_U("outstanding after the tail was acknowledged", _quicSendBufOutstanding(&sb), 4);

    _quicSendBufAcked(&sb, 7);
    CHECK_U("base after everything was acknowledged", sb.base, 10);
    CHECK_U("outstanding after everything was acknowledged", _quicSendBufOutstanding(&sb), 0);
    CHECK_U("the buffer emptied", sb.nchunks, 0);

    // A stream carries on from where it left off.
    CHECK("add after acknowledgement", _quicSendBufAdd(&sb, (const uint8*)"klmno", 5));
    CHECK("next after acknowledgement", _quicSendBufNext(&sb, 100, &off, &data, &len));
    CHECK_U("offset carries on", off, 10);
    CHECK_U("length carries on", len, 5);
    CHECK("contents carry on", memcmp(data, "klmno", 5) == 0);

    // A probe repeats what is in flight without disturbing it, so the original packet still
    // resolves the way it would have.
    _quicSendBufSent(&sb, 10, 5, 20);
    CHECK("probe", _quicSendBufProbe(&sb, 100, &off, &data, &len));
    CHECK_U("probe offset", off, 10);
    CHECK_U("probe length", len, 5);
    CHECK("a probe does not queue anything", !_quicSendBufPending(&sb));

    _quicSendBufAcked(&sb, 20);
    CHECK_U("the probed range still resolved", sb.base, 15);
    CHECK("nothing is left to probe", !_quicSendBufProbe(&sb, 100, &off, &data, &len));

    // A Retry means the server saw none of it, so all of it is queued again.
    CHECK("add before reset", _quicSendBufAdd(&sb, (const uint8*)"pq", 2));
    _quicSendBufSent(&sb, 15, 2, 30);
    _quicSendBufReset(&sb);
    CHECK("everything is waiting after a reset", _quicSendBufNext(&sb, 100, &off, &data, &len));
    CHECK_U("reset offset", off, 15);
    CHECK_U("reset length", len, 2);

out:
    _quicSendBufDestroy(&sb);
    return ret;
}

// Many packets outstanding at once, each leaving a range of its own behind.
//
// How many ranges that takes is decided by how much this endpoint has in flight, which is the
// congestion window's business and not the buffer's. So the table follows rather than capping it:
// a buffer that stopped handing out data once it ran out of slots would be a throughput limit
// wearing a bookkeeping limit's clothes, and there would be nothing the layer above could do.
int test_quicrecovtest_sendbuf_many(void)
{
    int ret = 0;
    QuicSendBuf sb;
    _quicSendBufInit(&sb);

    uint64 off;
    const uint8* data;
    size_t len;

    const uint32 packets = 500;

    uint8 blob[600];
    memset(blob, 'z', sizeof(blob));
    CHECK("add", _quicSendBufAdd(&sb, blob, sizeof(blob)));

    // A byte per packet, so every one of them leaves a range that cannot be joined to its
    // neighbours -- different packets, so an acknowledgement means something different for each.
    uint32 sent = 0;
    for (uint32 i = 0; i < packets; i++) {
        if (!_quicSendBufNext(&sb, 1, &off, &data, &len))
            break;
        _quicSendBufSent(&sb, off, len, 100 + i);
        sent++;
    }

    CHECK_U("packets sent", sent, packets);
    CHECK_U("ranges tracked", sb.nchunks, packets + 1);   // the bytes not yet sent hold the last
    CHECK_U("nothing acknowledged yet", _quicSendBufOutstanding(&sb), sizeof(blob));

    // Acknowledged in order, the ranges collapse back to one as the front is released.
    for (uint32 i = 0; i < packets; i++)
        _quicSendBufAcked(&sb, 100 + i);

    CHECK_U("only the unsent tail is left", _quicSendBufOutstanding(&sb), sizeof(blob) - packets);
    CHECK_U("one range left", sb.nchunks, 1);
    CHECK_U("the buffer moved on", sb.base, packets);

out:
    _quicSendBufDestroy(&sb);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Round trip estimation
// ---------------------------------------------------------------------------------------------

int test_quicrecovtest_rtt(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);
    _quicRecovSetMaxAckDelay(&rf.r, timeMS(100));

    // The first sample is the estimate outright: there is nothing to average it against, and half
    // of it is as good a guess at the variation as any.
    rSend(&rf, QUIC_PNS_APP, 0);
    rf.now += timeMS(100);
    CHECK("first acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));

    CHECK_U("latest after one sample", rf.r.rtt.latest, timeMS(100));
    CHECK_U("smoothed after one sample", rf.r.rtt.smoothed, timeMS(100));
    CHECK_U("variation after one sample", rf.r.rtt.var, timeMS(50));
    CHECK_U("minimum after one sample", rf.r.rtt.min, timeMS(100));

    // A second sample moves the average an eighth of the way and the variation a quarter.
    rSend(&rf, QUIC_PNS_APP, 1);
    rf.now += timeMS(200);
    CHECK("second acknowledgement", rAck(&rf, QUIC_PNS_APP, 1, 1, 0));

    CHECK_U("smoothed after two samples", rf.r.rtt.smoothed, 112500);
    CHECK_U("variation after two samples", rf.r.rtt.var, 62500);
    CHECK_U("minimum is unchanged by a longer sample", rf.r.rtt.min, timeMS(100));

    // The peer says it sat on this one for 50ms, so that much is not the path's doing -- but the
    // minimum still counts the whole thing, because it is the closest thing to a measurement of
    // the path on its own.
    int64 before = rf.r.rtt.smoothed;
    rSend(&rf, QUIC_PNS_APP, 2);
    rf.now += timeMS(200);
    CHECK("third acknowledgement", rAck(&rf, QUIC_PNS_APP, 2, 2, timeMS(50)));

    CHECK_U("latest is the whole round trip", rf.r.rtt.latest, timeMS(200));
    CHECK_U("smoothed discounts the reported delay",
            rf.r.rtt.smoothed, (7 * before + timeMS(150)) / 8);
    CHECK_U("minimum ignores the reported delay", rf.r.rtt.min, timeMS(100));

    // A delay that would put the sample below the shortest round trip ever measured cannot be
    // true, so it is not taken off.
    before = rf.r.rtt.smoothed;
    rSend(&rf, QUIC_PNS_APP, 3);
    rf.now += timeMS(120);
    CHECK("fourth acknowledgement", rAck(&rf, QUIC_PNS_APP, 3, 3, timeMS(50)));
    CHECK_U("an impossible delay is ignored", rf.r.rtt.smoothed, (7 * before + timeMS(120)) / 8);

    // A peer that claims a delay longer than the one it advertised does not get to shrink the
    // estimate with it either.
    before = rf.r.rtt.smoothed;
    rSend(&rf, QUIC_PNS_APP, 4);
    rf.now += timeMS(200);
    CHECK("fifth acknowledgement", rAck(&rf, QUIC_PNS_APP, 4, 4, timeS(10)));
    CHECK_U("an overstated delay is capped at max_ack_delay",
            rf.r.rtt.smoothed, (7 * before + timeMS(100)) / 8);

    // An acknowledgement that does not name a packet this endpoint sent measures nothing.
    before = rf.r.rtt.smoothed;
    CHECK("stale acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 4, 0));
    CHECK_U("a repeated acknowledgement is not a new sample", rf.r.rtt.smoothed, before);

out:
    rDestroy(&rf);
    return ret;
}

// A packet that is never acknowledged is not a round trip sample either. Only an ack-eliciting
// packet the peer had to answer says how long the path took.
int test_quicrecovtest_rtt_noelicit(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    // A packet carrying nothing but acknowledgements: the peer answers it when it feels like it.
    _quicRecovOnSent(&rf.r, QUIC_PNS_APP, 0, 60, false, false, rf.now);
    rf.now += timeMS(100);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));
    CHECK("a packet the peer did not have to answer is not a sample", !rf.r.rtt.have);

    rSend(&rf, QUIC_PNS_APP, 1);
    rf.now += timeMS(80);
    CHECK("second acknowledgement", rAck(&rf, QUIC_PNS_APP, 1, 1, 0));
    CHECK_U("the first real sample", rf.r.rtt.smoothed, timeMS(80));

out:
    rDestroy(&rf);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Loss detection
// ---------------------------------------------------------------------------------------------

int test_quicrecovtest_loss_packet(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    for (uint64 pn = 0; pn < 5; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    CHECK_U("bytes in flight", rf.r.inFlight, 5 * RMTU);

    // Acknowledging packet 4 leaves 0 and 1 three or more behind it, which RFC 9002 section 6.1.1
    // treats as lost. Packets 2 and 3 are close enough behind to still be in transit.
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 4, 4, 0));

    CHECK_U("packets declared lost", rf.nlost, 2);
    CHECK("packet 0 was declared lost", rReported(rf.lost, rf.nlost, 0));
    CHECK("packet 1 was declared lost", rReported(rf.lost, rf.nlost, 1));
    CHECK("packet 2 was not declared lost", !rReported(rf.lost, rf.nlost, 2));
    CHECK_U("packets acknowledged", rf.nacked, 1);
    CHECK("packet 4 was acknowledged", rReported(rf.acked, rf.nacked, 4));

    // Three packets are gone from the window: one acknowledged and two lost.
    CHECK_U("bytes in flight after the losses", rf.r.inFlight, 2 * RMTU);
    CHECK_U("congestion events", rf.r.ncongestion, 1);

    // A lost packet is no longer waiting for an answer, which is what the probe timer counts.
    CHECK_U("packets still waiting for an answer", rf.r.elicitInFlight[QUIC_PNS_APP], 2);

out:
    rDestroy(&rf);
    return ret;
}

int test_quicrecovtest_loss_time(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    int64 t0 = rf.now;

    rSend(&rf, QUIC_PNS_APP, 0);
    rf.now += timeMS(10);
    rSend(&rf, QUIC_PNS_APP, 1);

    // Packet 1 is answered a hundred milliseconds later. Packet 0 is only one behind it, so the
    // packet count says nothing; it is old, but not yet old enough.
    rf.now += timeMS(100);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 1, 1, 0));
    CHECK_U("nothing was declared lost yet", rf.nlost, 0);

    // Nine eighths of the round trip after it was sent, it is.
    int64 want = t0 + timeMS(100) * 9 / 8;
    CHECK_U("the loss timer", _quicRecovTimer(&rf.r), want);

    rf.nlost = 0;
    rf.now   = want;
    _quicRecovOnTimeout(&rf.r, rf.now);

    CHECK_U("packets declared lost by age", rf.nlost, 1);
    CHECK("packet 0 was declared lost", rReported(rf.lost, rf.nlost, 0));
    CHECK_U("bytes in flight afterwards", rf.r.inFlight, 0);

out:
    rDestroy(&rf);
    return ret;
}

// Reordering is not loss. A packet that arrives late still counts, and the window is not cut for
// it a second time.
int test_quicrecovtest_reorder(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    for (uint64 pn = 0; pn < 3; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    // Packets 1 and 2 arrive; 0 is only two behind the largest, one short of the three that would
    // make it lost, so it is treated as still in transit.
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 1, 2, 0));
    CHECK_U("nothing was declared lost", rf.nlost, 0);
    CHECK_U("packets acknowledged", rf.nacked, 2);

    // It turns up next.
    CHECK("late acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 2, 0));
    CHECK_U("the late packet was acknowledged", rf.nacked, 1);
    CHECK("the late packet was packet 0", rReported(rf.acked, rf.nacked, 0));
    CHECK_U("nothing was declared lost by the late arrival", rf.nlost, 0);
    CHECK_U("no congestion event", rf.r.ncongestion, 0);
    CHECK_U("bytes in flight", rf.r.inFlight, 0);

    // Acknowledgements can arrive out of order too. One naming an earlier packet does not undo
    // what a later one already established.
    CHECK("stale acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));
    CHECK_U("the largest acknowledged went backwards", rf.r.largestAcked[QUIC_PNS_APP], 2);

out:
    rDestroy(&rf);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Probe timeouts
// ---------------------------------------------------------------------------------------------

int test_quicrecovtest_pto(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    int64 t0 = rf.now;
    rSend(&rf, QUIC_PNS_APP, 0);

    // With no round trip measured yet, RFC 9002 section 6.2.2 uses twice the assumed one, plus
    // the delay the peer is allowed before it has said otherwise.
    int64 want = t0 + 2 * QUIC_INITIAL_RTT + timeMS(25);
    CHECK_U("the first probe timeout", _quicRecovTimer(&rf.r), want);

    rf.now = want;
    _quicRecovOnTimeout(&rf.r, rf.now);

    CHECK_U("probes owed", _quicRecovProbes(&rf.r, QUIC_PNS_APP), 2);
    CHECK_U("timeouts so far", rf.r.ptoCount, 1);
    CHECK_U("nothing was declared lost by a timeout", rf.nlost, 0);
    CHECK_U("a timeout does not change what is in flight", rf.r.inFlight, RMTU);

    // Each timeout doubles the wait, so a peer that has gone away is asked about less and less
    // often rather than being flooded.
    CHECK_U("the second probe timeout", _quicRecovTimer(&rf.r),
            t0 + 2 * (2 * QUIC_INITIAL_RTT + timeMS(25)));

    _quicRecovProbeSent(&rf.r, QUIC_PNS_APP);
    CHECK_U("probes owed after one went out", _quicRecovProbes(&rf.r, QUIC_PNS_APP), 1);

    // An acknowledgement clears the backoff.
    rf.now += timeMS(10);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));
    CHECK_U("the backoff was cleared", rf.r.ptoCount, 0);

out:
    rDestroy(&rf);
    return ret;
}

// A probe goes in the space that has been waiting longest, and never in a space the peer may not
// have the keys to read.
int test_quicrecovtest_pto_space(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovSpaceReady(&rf.r, QUIC_PNS_INITIAL);
    _quicRecovSpaceReady(&rf.r, QUIC_PNS_HANDSHAKE);

    rSend(&rf, QUIC_PNS_INITIAL, 0);
    rf.now += timeMS(5);
    rSend(&rf, QUIC_PNS_HANDSHAKE, 0);
    rf.now += timeMS(5);
    rSend(&rf, QUIC_PNS_APP, 0);

    rf.now = _quicRecovTimer(&rf.r);
    _quicRecovOnTimeout(&rf.r, rf.now);

    CHECK_U("the probe went in the oldest space", _quicRecovProbes(&rf.r, QUIC_PNS_INITIAL), 2);
    CHECK_U("no probe in the handshake space", _quicRecovProbes(&rf.r, QUIC_PNS_HANDSHAKE), 0);
    CHECK_U("no probe in the application space", _quicRecovProbes(&rf.r, QUIC_PNS_APP), 0);

    // Once the handshake space is the only one left, the probe moves there. The application space
    // stays out of it until the handshake is confirmed, because before that the peer may have no
    // way to read a packet sent in it.
    _quicRecovDiscardSpace(&rf.r, QUIC_PNS_INITIAL, rf.now);
    rf.now = _quicRecovTimer(&rf.r);
    _quicRecovOnTimeout(&rf.r, rf.now);
    CHECK_U("the probe moved to the handshake space",
            _quicRecovProbes(&rf.r, QUIC_PNS_HANDSHAKE), 2);
    CHECK_U("still no probe in the application space", _quicRecovProbes(&rf.r, QUIC_PNS_APP), 0);

    // With the application space the only one left holding anything, and the handshake still
    // unconfirmed, there is nowhere a probe could usefully go -- so no timer is armed at all
    // rather than one that would produce a packet the peer cannot read.
    _quicRecovDiscardSpace(&rf.r, QUIC_PNS_HANDSHAKE, rf.now);
    CHECK_U("something is still unanswered in the application space",
            rf.r.elicitInFlight[QUIC_PNS_APP], 1);
    CHECK_U("a probe was armed for a space the peer cannot read",
            _quicRecovTimer(&rf.r), timeForever);

    // Confirming the handshake is what makes it reachable.
    _quicRecovConfirmHandshake(&rf.r, rf.now);
    CHECK("confirming the handshake armed the timer", _quicRecovTimer(&rf.r) != timeForever);
    rf.now = _quicRecovTimer(&rf.r);
    _quicRecovOnTimeout(&rf.r, rf.now);
    CHECK_U("the probe reached the application space", _quicRecovProbes(&rf.r, QUIC_PNS_APP), 2);

out:
    rDestroy(&rf);
    return ret;
}

// A client with nothing in flight still arms a timer, because a server that has not validated its
// address may be sitting on a flight it is not allowed to send.
int test_quicrecovtest_pto_deadlock(void)
{
    int ret = 0;
    RFix cli, srv;
    rInit(&cli, false);
    rInit(&srv, true);
    _quicRecovSpaceReady(&cli.r, QUIC_PNS_INITIAL);
    _quicRecovSpaceReady(&srv.r, QUIC_PNS_INITIAL);

    // Both send an Initial and have it acknowledged, which leaves neither with anything in flight
    // and neither with a finished handshake.
    rSend(&cli, QUIC_PNS_INITIAL, 0);
    rSend(&srv, QUIC_PNS_INITIAL, 0);
    cli.now += timeMS(50);
    srv.now += timeMS(50);
    CHECK("client acknowledgement", rAck(&cli, QUIC_PNS_INITIAL, 0, 0, 0));
    CHECK("server acknowledgement", rAck(&srv, QUIC_PNS_INITIAL, 0, 0, 0));

    CHECK_U("client bytes in flight", cli.r.inFlight, 0);
    CHECK_U("server bytes in flight", srv.r.inFlight, 0);

    // The client keeps a timer anyway: the server may be holding a flight it is not allowed to
    // send until the client's address is validated, and only another packet releases it.
    CHECK("a client with nothing in flight still waits", _quicRecovTimer(&cli.r) != timeForever);
    CHECK_U("a server with nothing in flight does not", _quicRecovTimer(&srv.r), timeForever);

    // Once the peer has proved it received something, the client stops as well.
    _quicRecovConfirmHandshake(&cli.r, cli.now);
    CHECK_U("a validated client stops waiting", _quicRecovTimer(&cli.r), timeForever);

out:
    rDestroy(&cli);
    rDestroy(&srv);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Congestion control
// ---------------------------------------------------------------------------------------------

int test_quicrecovtest_cwnd(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    // Ten packets, which is what RFC 9002 section 7.2 starts a connection with.
    CHECK_U("the initial window", rf.r.window, 10 * RMTU);
    CHECK("a fresh connection may send", _quicRecovCanSend(&rf.r));

    for (uint64 pn = 0; pn < 10; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    CHECK_U("bytes in flight", rf.r.inFlight, 10 * RMTU);
    CHECK("a full window sends nothing more", !_quicRecovCanSend(&rf.r));

    // While the window is still doubling, every acknowledged byte adds a byte to it.
    rf.now += timeMS(50);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 9, 0));
    CHECK_U("the window doubled", rf.r.window, 20 * RMTU);
    CHECK_U("nothing is in flight", rf.r.inFlight, 0);
    CHECK("a drained window sends again", _quicRecovCanSend(&rf.r));

out:
    rDestroy(&rf);
    return ret;
}

int test_quicrecovtest_cwnd_loss(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    for (uint64 pn = 0; pn < 10; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    rf.now += timeMS(50);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 4, 9, 0));

    // Packets 0 through 3 fell three or more behind the largest acknowledged, so they are lost.
    // The window halves once, not once per lost packet.
    CHECK_U("packets declared lost", rf.nlost, 4);
    CHECK_U("congestion events", rf.r.ncongestion, 1);
    CHECK_U("the window halved", rf.r.window, 5 * RMTU);
    CHECK_U("the slow start threshold", rf.r.ssthresh, 5 * RMTU);

    // The acknowledgement that revealed the loss did not also grow the window: the packets it
    // covered were sent before the window was cut, so they were already paid for.
    CHECK_U("nothing counted towards the next increase", rf.r.ccAcked, 0);

    // Past the threshold the window grows by one packet per window acknowledged rather than by
    // every byte, which is what stops it climbing straight back to where it just failed.
    // Sent after the window was cut, so it is one of the packets that measures whether the
    // smaller window works rather than one that was already paid for.
    uint64 before = rf.r.window;
    rf.now += timeMS(1);
    rSend(&rf, QUIC_PNS_APP, 10);
    rf.now += timeMS(50);
    CHECK("acknowledgement after recovery", rAck(&rf, QUIC_PNS_APP, 10, 10, 0));
    CHECK_U("the window did not jump", rf.r.window, before);
    CHECK_U("progress towards the next increase", rf.r.ccAcked, RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// The window never falls below two packets, however much is lost.
int test_quicrecovtest_cwnd_floor(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    uint64 pn = 0;
    for (int round = 0; round < 8; round++) {
        uint64 first = pn;
        for (int i = 0; i < 6; i++)
            rSend(&rf, QUIC_PNS_APP, pn++);

        rf.now += timeMS(50);
        CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, pn - 1, pn - 1, 0));
        unused_noeval(first);
    }

    CHECK("the window reached its floor", rf.r.window >= 2 * (uint64)RMTU);
    CHECK("the window did not fall below its floor", rf.r.window == 2 * (uint64)RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// A long enough run of losses with nothing getting through is a path that has gone away rather
// than one that is congested, and RFC 9002 section 7.6 answers that by giving up the window
// entirely instead of halving it.
int test_quicrecovtest_persistent(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    // One packet gets through first, so there is a round trip estimate to measure the run against.
    rSend(&rf, QUIC_PNS_APP, 0);
    rf.now += timeMS(50);
    CHECK("first acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));

    int64 pto    = rf.r.rtt.smoothed + 4 * rf.r.rtt.var + timeMS(25);
    int64 period = pto * QUIC_PERSISTENT_CONGESTION_THRESHOLD;

    // Then a long silence: packets go out across more than the persistent congestion period and
    // none of them is answered.
    uint64 pn = 1;
    int64 span = 0;
    while (span <= period + timeMS(100)) {
        rSend(&rf, QUIC_PNS_APP, pn++);
        rf.now += timeMS(50);
        span += timeMS(50);
    }

    // One finally gets through, which is what lets everything behind it be declared lost at once.
    rSend(&rf, QUIC_PNS_APP, pn);
    rf.now += timeMS(50);
    CHECK("acknowledgement after the silence", rAck(&rf, QUIC_PNS_APP, pn, pn, 0));

    CHECK("packets were declared lost", rf.nlost > 0);
    CHECK_U("persistent congestion was declared", rf.r.npersistent, 1);

    // Everything is given up: the two packet minimum, plus the one packet that did get through,
    // which is acknowledged after the collapse and counts towards the window like any other.
    CHECK_U("the window collapsed to its minimum", rf.r.window, 3 * RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// A run of losses that fits inside the period is congestion, not a dead path.
int test_quicrecovtest_persistent_short(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    rSend(&rf, QUIC_PNS_APP, 0);
    rf.now += timeMS(50);
    CHECK("first acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));

    for (uint64 pn = 1; pn <= 4; pn++) {
        rSend(&rf, QUIC_PNS_APP, pn);
        rf.now += timeMS(10);
    }

    rSend(&rf, QUIC_PNS_APP, 5);
    rf.now += timeMS(50);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 5, 5, 0));

    CHECK("packets were declared lost", rf.nlost > 0);
    CHECK_U("a short run is not persistent congestion", rf.r.npersistent, 0);
    CHECK("the window was halved rather than collapsed", rf.r.window > 2 * (uint64)RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// Acknowledgements that arrive while there is nothing waiting to be sent measure how fast this
// endpoint ran out of data, not how much the path will carry.
int test_quicrecovtest_applimited(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    uint64 before = rf.r.window;

    _quicRecovSetAppLimited(&rf.r, true);
    for (uint64 pn = 0; pn < 4; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    rf.now += timeMS(50);
    CHECK("acknowledgement while app limited", rAck(&rf, QUIC_PNS_APP, 0, 3, 0));
    CHECK_U("the window did not grow", rf.r.window, before);
    CHECK_U("the bytes still left the window", rf.r.inFlight, 0);

    // With something waiting again it grows as usual.
    _quicRecovSetAppLimited(&rf.r, false);
    for (uint64 pn = 4; pn < 8; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    rf.now += timeMS(50);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 4, 7, 0));
    CHECK_U("the window grew", rf.r.window, before + 4 * RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Discarding a number space
// ---------------------------------------------------------------------------------------------

int test_quicrecovtest_discard(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovSpaceReady(&rf.r, QUIC_PNS_INITIAL);

    rSend(&rf, QUIC_PNS_INITIAL, 0);
    rSend(&rf, QUIC_PNS_INITIAL, 1);
    rSend(&rf, QUIC_PNS_APP, 0);
    CHECK_U("bytes in flight", rf.r.inFlight, 3 * RMTU);

    // A timeout in the space builds up a backoff, which the handshake moving on does not carry
    // into the space that replaces it.
    rf.now = _quicRecovTimer(&rf.r);
    _quicRecovOnTimeout(&rf.r, rf.now);
    CHECK_U("a timeout happened", rf.r.ptoCount, 1);

    _quicRecovDiscardSpace(&rf.r, QUIC_PNS_INITIAL, rf.now);

    CHECK_U("the discarded space left the window", rf.r.inFlight, RMTU);
    CHECK_U("the backoff was cleared", rf.r.ptoCount, 0);
    CHECK_U("probes owed in the discarded space", _quicRecovProbes(&rf.r, QUIC_PNS_INITIAL), 0);

    // Nothing more can be recorded there.
    rSend(&rf, QUIC_PNS_INITIAL, 2);
    CHECK_U("a discarded space records nothing", rf.r.inFlight, RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// ---------------------------------------------------------------------------------------------
// Pacing
// ---------------------------------------------------------------------------------------------

int test_quicrecovtest_pacing(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    // A flight leaves in one burst: pacing is there to spread sustained sending, not to hold back
    // the handshake or a single window's worth of data.
    _quicRecovPacerRefill(&rf.r, rf.now);
    CHECK("the pacer starts ready", _quicRecovPacerReady(&rf.r));

    for (uint64 pn = 0; pn < 10; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    _quicRecovPacerRefill(&rf.r, rf.now);
    CHECK("the burst is spent", !_quicRecovPacerReady(&rf.r));

    // It refills over a round trip. With no measurement yet that is the assumed one, and the
    // sender is allowed to run ahead of the window while it is still doubling.
    int64 next = _quicRecovPacerNext(&rf.r);
    CHECK("the pacer says when it will be ready", next > rf.now);
    CHECK("the wait is under a round trip", next < rf.now + QUIC_INITIAL_RTT);

    rf.now = next;
    _quicRecovPacerRefill(&rf.r, rf.now);
    CHECK("the pacer refilled", _quicRecovPacerReady(&rf.r));

    // It never accumulates more than one burst, however long nothing was sent.
    rf.now += timeS(10);
    _quicRecovPacerRefill(&rf.r, rf.now);
    CHECK_U("the bucket is capped", rf.r.tokens, 10 * RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// The window is cut once per round trip, not once per acknowledgement that reveals more of the
// same flight missing. RFC 9002 section 7.3.2 draws the line at when the packet was sent: one
// that left before the window was last cut has already been paid for.
int test_quicrecovtest_cwnd_recovery(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    for (uint64 pn = 0; pn < 6; pn++)
        rSend(&rf, QUIC_PNS_APP, pn);

    // The first hole in the flight shows up.
    rf.now += timeMS(50);
    CHECK("first acknowledgement", rAck(&rf, QUIC_PNS_APP, 3, 3, 0));
    CHECK_U("packets declared lost", rf.nlost, 1);
    CHECK_U("congestion events", rf.r.ncongestion, 1);

    uint64 cut = rf.r.window;
    CHECK_U("the window halved", cut, 5 * RMTU);

    // More of the same flight turns out to be missing. It was all sent before the window was cut,
    // so it is the same event, not a second one.
    rf.now += timeMS(10);
    CHECK("second acknowledgement", rAck(&rf, QUIC_PNS_APP, 5, 5, 0));
    CHECK("more packets were declared lost", rf.nlost > 0);
    CHECK_U("the window was cut twice for one flight", rf.r.ncongestion, 1);
    CHECK_U("the window shrank again", rf.r.window, cut);

out:
    rDestroy(&rf);
    return ret;
}

// A run of losses with something getting through in the middle of it is congestion, however long
// the run is either side: the path is plainly still there.
int test_quicrecovtest_persistent_broken(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    rSend(&rf, QUIC_PNS_APP, 0);
    rf.now += timeMS(50);
    CHECK("first acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));

    int64 period = (rf.r.rtt.smoothed + 4 * rf.r.rtt.var + timeMS(25)) *
                   QUIC_PERSISTENT_CONGESTION_THRESHOLD;

    uint64 pn  = 1;
    int64 span = 0;

    while (span < timeMS(200)) {
        rSend(&rf, QUIC_PNS_APP, pn++);
        rf.now += timeMS(50);
        span += timeMS(50);
    }

    // One gets through, right in the middle of the silence.
    uint64 mid = pn++;
    rSend(&rf, QUIC_PNS_APP, mid);
    rf.now += timeMS(50);
    CHECK("acknowledgement in the middle", rAck(&rf, QUIC_PNS_APP, mid, mid, 0));

    // Then a run three quarters as long as the period: short enough on its own, but long enough
    // that joining it to what came before the acknowledgement would cross the line.
    span = 0;
    while (span < period * 3 / 4) {
        rSend(&rf, QUIC_PNS_APP, pn++);
        rf.now += timeMS(50);
        span += timeMS(50);
    }

    rSend(&rf, QUIC_PNS_APP, pn);
    rf.now += timeMS(50);
    CHECK("final acknowledgement", rAck(&rf, QUIC_PNS_APP, pn, pn, 0));

    CHECK("packets were declared lost", rf.nlost > 0);
    CHECK_U("a run with a gap in it was called persistent congestion", rf.r.npersistent, 0);
    CHECK("the window was halved rather than collapsed", rf.r.window > 2 * (uint64)RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// A blackout does not always come to light all at once: some of it is revealed by the packet that
// finally gets through, and the rest only when a timer gives up on it. The run has to be measured
// across both, which means a packet that has already been declared lost stays on the record for
// long enough to still be part of one.
int test_quicrecovtest_persistent_split(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    rSend(&rf, QUIC_PNS_APP, 0);
    rf.now += timeMS(50);
    CHECK("first acknowledgement", rAck(&rf, QUIC_PNS_APP, 0, 0, 0));

    int64 t1     = rf.now;
    int64 period = (rf.r.rtt.smoothed + 4 * rf.r.rtt.var + timeMS(25)) *
                   QUIC_PERSISTENT_CONGESTION_THRESHOLD;
    CHECK("the period is shorter than the silence to come", period < timeMS(600));

    rSend(&rf, QUIC_PNS_APP, 1);
    rf.now = t1 + timeMS(600);
    rSend(&rf, QUIC_PNS_APP, 2);

    // Something carrying nothing the peer has to answer gets through, which reveals the first
    // packet as lost but says nothing yet about the second.
    rf.now = t1 + timeMS(620);
    _quicRecovOnSent(&rf.r, QUIC_PNS_APP, 3, 60, false, false, rf.now);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 3, 3, 0));

    CHECK_U("the first packet was declared lost", rf.nlost, 1);
    CHECK_U("nothing is persistent about a single loss", rf.r.npersistent, 0);
    CHECK_U("the window was halved", rf.r.ncongestion, 1);

    // The second follows when its own timer expires, and the two together are what the silence
    // actually was.
    rf.now   = _quicRecovTimer(&rf.r);
    rf.nlost = 0;
    _quicRecovOnTimeout(&rf.r, rf.now);

    CHECK_U("the second packet was declared lost", rf.nlost, 1);
    CHECK_U("the two halves of the silence were not joined up", rf.r.npersistent, 1);
    CHECK_U("the window did not collapse", rf.r.window, 2 * RMTU);

out:
    rDestroy(&rf);
    return ret;
}

// Persistent congestion is measured against a round trip estimate, so without one there is
// nothing to measure it against and the window is left alone however long the run of losses.
int test_quicrecovtest_persistent_nortt(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    int64 t0 = rf.now;

    rSend(&rf, QUIC_PNS_APP, 0);
    rf.now = t0 + timeS(3);
    rSend(&rf, QUIC_PNS_APP, 1);

    // A packet carrying nothing the peer had to answer, so acknowledging it measures nothing.
    rf.now = t0 + timeS(3) + timeMS(100);
    _quicRecovOnSent(&rf.r, QUIC_PNS_APP, 2, 60, false, false, rf.now);

    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 2, 2, 0));
    CHECK("no round trip was measured", !rf.r.rtt.have);
    CHECK_U("the first packet was declared lost by age", rf.nlost, 1);

    // The second follows once it is old enough, which puts three seconds between the two -- far
    // longer than the period would be if there were an estimate to work one out from.
    rf.now = _quicRecovTimer(&rf.r);
    CHECK("a loss timer was armed", rf.now < t0 + timeS(10));
    rf.nlost = 0;
    _quicRecovOnTimeout(&rf.r, rf.now);

    CHECK_U("the second packet was declared lost", rf.nlost, 1);
    CHECK("no round trip was measured after the timeout either", !rf.r.rtt.have);
    CHECK_U("persistent congestion was declared with nothing to measure it against",
            rf.r.npersistent, 0);

out:
    rDestroy(&rf);
    return ret;
}

// A run that started before the first round trip was ever measured says nothing either: the
// estimate it would be compared against did not exist while it was happening.
int test_quicrecovtest_persistent_firstsample(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    // No measurement to begin with, so the period is the one built on the assumed round trip.
    int64 period = (2 * QUIC_INITIAL_RTT + timeMS(25)) * QUIC_PERSISTENT_CONGESTION_THRESHOLD;

    uint64 pn  = 0;
    int64 span = 0;
    while (span <= period + timeMS(500)) {
        rSend(&rf, QUIC_PNS_APP, pn++);
        rf.now += timeMS(100);
        span += timeMS(100);
    }

    // The first thing ever acknowledged is also the first round trip sample, so every packet
    // behind it was sent before there was anything to judge the silence by.
    rSend(&rf, QUIC_PNS_APP, pn);
    rf.now += timeMS(50);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, pn, pn, 0));

    CHECK("a round trip was measured", rf.r.rtt.have);
    CHECK("packets were declared lost", rf.nlost > 0);
    CHECK_U("a run from before the first measurement was counted", rf.r.npersistent, 0);

out:
    rDestroy(&rf);
    return ret;
}

// Nothing sent after the largest acknowledged packet can be judged lost, however old it gets. No
// later packet has arrived to say anything about it, and age alone proves nothing: it may still
// be in a queue somewhere on the path.
int test_quicrecovtest_loss_beyond(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    rSend(&rf, QUIC_PNS_APP, 0);
    rSend(&rf, QUIC_PNS_APP, 1);
    rSend(&rf, QUIC_PNS_APP, 2);

    // Only the middle one is answered, which says something about the one before it and nothing
    // at all about the one after.
    rf.now += timeMS(10);
    CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, 1, 1, 0));
    CHECK_U("nothing was declared lost yet", rf.nlost, 0);

    rf.now   = _quicRecovTimer(&rf.r);
    rf.nlost = 0;
    _quicRecovOnTimeout(&rf.r, rf.now);

    CHECK_U("packets declared lost", rf.nlost, 1);
    CHECK("the packet before the acknowledged one was declared lost",
          rReported(rf.lost, rf.nlost, 0));
    CHECK("a packet nothing had been heard about was declared lost",
          !rReported(rf.lost, rf.nlost, 2));

out:
    rDestroy(&rf);
    return ret;
}

// The record of sent packets is a ring indexed by how far a packet number is from the oldest one
// still held, so both ends of it have to move in step: the oldest number as packets fall out, and
// the contents as it is made bigger. Anything out of step there mixes up which packet is which.
int test_quicrecovtest_ring(void)
{
    int ret = 0;
    RFix rf;
    rInit(&rf, false);
    _quicRecovConfirmHandshake(&rf.r, rf.now);

    uint64 pn = 0;

    // Batches that grow, with long enough between them that what has been resolved falls out of
    // the ring before the next batch arrives. That is what makes the ring both wrap and grow.
    for (uint32 round = 0; round < 5; round++) {
        uint32 batch = 20 + round * 15;
        uint64 first = pn;

        for (uint32 i = 0; i < batch; i++)
            rSend(&rf, QUIC_PNS_APP, pn++);

        CHECK_U("bytes in flight", rf.r.inFlight, (uint64)batch * RMTU);

        rf.now += timeMS(10);
        CHECK("acknowledgement", rAck(&rf, QUIC_PNS_APP, first, pn - 1, 0));

        CHECK_U("packets acknowledged", rf.nacked, batch > RMAX_REPORT ? RMAX_REPORT : batch);
        CHECK_U("packets declared lost", rf.nlost, 0);
        CHECK_U("bytes left in flight", rf.r.inFlight, 0);
        CHECK_U("the largest acknowledged", rf.r.largestAcked[QUIC_PNS_APP], pn - 1);

        rf.now += timeS(1);
    }

out:
    rDestroy(&rf);
    return ret;
}

// Each group below runs several of the subtests above in one process, so ctest spends one
// process launch per feature area instead of one per subtest. The individual subtests stay
// registered under their own names too, for running or debugging one in isolation.

int test_quicrecovtest_grp_sendbuf(void)
{
    TEST_CHAIN(test_quicrecovtest_sendbuf, test_quicrecovtest_sendbuf_many,
               test_quicrecovtest_ring);
}

int test_quicrecovtest_grp_lossdetect(void)
{
    TEST_CHAIN(test_quicrecovtest_rtt, test_quicrecovtest_rtt_noelicit,
               test_quicrecovtest_loss_packet, test_quicrecovtest_loss_time,
               test_quicrecovtest_reorder, test_quicrecovtest_loss_beyond);
}

int test_quicrecovtest_grp_pto(void)
{
    TEST_CHAIN(test_quicrecovtest_pto, test_quicrecovtest_pto_space,
               test_quicrecovtest_pto_deadlock);
}

int test_quicrecovtest_grp_congestion(void)
{
    TEST_CHAIN(test_quicrecovtest_cwnd, test_quicrecovtest_cwnd_loss,
               test_quicrecovtest_cwnd_floor, test_quicrecovtest_cwnd_recovery,
               test_quicrecovtest_applimited);
}

int test_quicrecovtest_grp_persistent(void)
{
    TEST_CHAIN(test_quicrecovtest_persistent, test_quicrecovtest_persistent_short,
               test_quicrecovtest_persistent_broken, test_quicrecovtest_persistent_firstsample,
               test_quicrecovtest_persistent_split, test_quicrecovtest_persistent_nortt);
}

int test_quicrecovtest_grp_pacing(void)
{
    TEST_CHAIN(test_quicrecovtest_discard, test_quicrecovtest_pacing);
}

testfunc quicrecovtest_funcs[] = {
    { "sendbuf", test_quicrecovtest_sendbuf },
    { "sendbuf_many", test_quicrecovtest_sendbuf_many },
    { "rtt", test_quicrecovtest_rtt },
    { "rtt_noelicit", test_quicrecovtest_rtt_noelicit },
    { "loss_packet", test_quicrecovtest_loss_packet },
    { "loss_time", test_quicrecovtest_loss_time },
    { "reorder", test_quicrecovtest_reorder },
    { "loss_beyond", test_quicrecovtest_loss_beyond },
    { "ring", test_quicrecovtest_ring },
    { "pto", test_quicrecovtest_pto },
    { "pto_space", test_quicrecovtest_pto_space },
    { "pto_deadlock", test_quicrecovtest_pto_deadlock },
    { "cwnd", test_quicrecovtest_cwnd },
    { "cwnd_loss", test_quicrecovtest_cwnd_loss },
    { "cwnd_floor", test_quicrecovtest_cwnd_floor },
    { "persistent", test_quicrecovtest_persistent },
    { "persistent_short", test_quicrecovtest_persistent_short },
    { "persistent_broken", test_quicrecovtest_persistent_broken },
    { "persistent_firstsample", test_quicrecovtest_persistent_firstsample },
    { "persistent_split", test_quicrecovtest_persistent_split },
    { "persistent_nortt", test_quicrecovtest_persistent_nortt },
    { "cwnd_recovery", test_quicrecovtest_cwnd_recovery },
    { "applimited", test_quicrecovtest_applimited },
    { "discard", test_quicrecovtest_discard },
    { "pacing", test_quicrecovtest_pacing },
    { "grp_sendbuf", test_quicrecovtest_grp_sendbuf },
    { "grp_lossdetect", test_quicrecovtest_grp_lossdetect },
    { "grp_pto", test_quicrecovtest_grp_pto },
    { "grp_congestion", test_quicrecovtest_grp_congestion },
    { "grp_persistent", test_quicrecovtest_grp_persistent },
    { "grp_pacing", test_quicrecovtest_grp_pacing },
    { NULL, NULL },
};
