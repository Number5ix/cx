// Loss recovery and congestion control (RFC 9002).
//
// This is the whole answer to "when may something be sent, and what has to be sent again". It
// never builds a packet and never reads one: the connection reports what went out and what came
// back, and this reports how much may be in flight, when the next timer expires, and which
// packets have to be arranged for again.
//
// Three things make that possible without storing what each packet contained. Packet numbers in a
// space are handed out in order and none is ever skipped, so a sent packet is found by arithmetic
// rather than by searching. Everything that would have to be sent again already remembers the
// packet it went into, so an acknowledgement or a loss is fully described by a packet number.
// And a resolved packet stays in the ring for a while, so a run of losses can be told apart from
// a run with an acknowledgement in the middle of it.

#include "conn_private.h"

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// A ceiling on the congestion window. No path cx will be on has a bandwidth-delay product near
// this, and it keeps the pacing arithmetic well inside what an int64 holds.
#define QUIC_MAX_WINDOW (UINT64_C(64) << 20)

// ---------------------------------------------------------------------------------------------
// The ring of sent packets
// ---------------------------------------------------------------------------------------------

static void ringDestroy(_Inout_ QuicSentRing* ring)
{
    xaDestroy(&ring->pkts);
    memset(ring, 0, sizeof(*ring));
}

static _Ret_maybenull_ QuicSentPkt* ringAt(_In_ QuicSentRing* ring, uint64 pn)
{
    if (ring->count == 0 || pn < ring->basePn)
        return NULL;

    uint64 idx = pn - ring->basePn;
    if (idx >= ring->count)
        return NULL;

    return &ring->pkts[(ring->head + (uint32)idx) & (ring->cap - 1)];
}

static bool ringGrow(_Inout_ QuicSentRing* ring)
{
    uint32 cap = ring->cap ? ring->cap * 2 : 16;

    QuicSentPkt* p = xaAlloc(cap * sizeof(QuicSentPkt), XA_Zero | XA_Opt);
    if (!p)
        return false;

    for (uint32 i = 0; i < ring->count; i++)
        p[i] = ring->pkts[(ring->head + i) & (ring->cap - 1)];

    xaFree(ring->pkts);
    ring->pkts = p;
    ring->cap  = cap;
    ring->head = 0;
    return true;
}

// Records a packet at the end of the ring. Packet numbers arrive in order with no gaps, so the
// new packet always lands exactly one past the last one.
static bool ringPush(_Inout_ QuicSentRing* ring, uint64 pn, _In_ const QuicSentPkt* pkt)
{
    if (ring->count == 0) {
        ring->basePn = pn;
    } else if (pn != ring->basePn + ring->count) {
        return false;
    }

    if (ring->count >= ring->cap && !ringGrow(ring))
        return false;

    ring->pkts[(ring->head + ring->count) & (ring->cap - 1)] = *pkt;
    ring->count++;
    return true;
}

// Drops resolved packets from the old end, once they are old enough that no run of losses still
// being measured could reach back to them, or once there are simply too many to keep.
static void ringTrim(_Inout_ QuicSentRing* ring, int64 cutoff)
{
    while (ring->count > 0 && ring->pkts[ring->head].fate != QUIC_SENT_LIVE &&
           (ring->pkts[ring->head].sent <= cutoff || ring->count > QUIC_SENT_MAX_RETAIN)) {
        ring->head = (ring->head + 1) & (ring->cap - 1);
        ring->basePn++;
        ring->count--;
    }

    if (ring->count == 0)
        ring->head = 0;
}

// ---------------------------------------------------------------------------------------------
// Congestion control: NewReno (RFC 9002 section 7)
// ---------------------------------------------------------------------------------------------

static uint64 ccInitialWindow(size_t maxDatagram)
{
    uint64 ten   = 10 * (uint64)maxDatagram;
    uint64 floor = 2 * (uint64)maxDatagram;
    if (floor < 14720)
        floor = 14720;
    return ten < floor ? ten : floor;
}

static uint64 ccMinWindow(_In_ const QuicRecovery* r)
{
    return 2 * (uint64)r->maxDatagram;
}

// Halves the window, once per round trip rather than once per lost packet: a whole flight is
// usually lost together, and reacting to each packet of it would collapse the window to nothing.
static void ccOnCongestion(_Inout_ QuicRecovery* r, int64 sentTime, int64 now)
{
    if (r->inRecovery && sentTime <= r->recoveryStart)
        return;

    r->inRecovery     = true;
    r->recoveryStart  = now;
    r->ssthresh       = r->window / 2;

    uint64 min = ccMinWindow(r);
    if (r->ssthresh < min)
        r->ssthresh = min;

    r->window  = r->ssthresh;
    r->ccAcked = 0;
    r->ncongestion++;
}

// Grows the window for one acknowledged packet. This runs after loss detection, not before it:
// an acknowledgement that reveals a loss must not also be counted as evidence the path will carry
// more, and putting it second is what makes the window cut stick.
static void ccOnAcked(_Inout_ QuicRecovery* r, _In_ const QuicSentPkt* p)
{
    // A packet sent before the window was last cut has already been paid for.
    if (r->inRecovery && p->sent <= r->recoveryStart)
        return;

    // Growing the window on acknowledgements that came back while there was nothing to send would
    // measure how fast this endpoint runs out of data, not how much the path will carry.
    if (r->appLimited)
        return;

    if (r->window < r->ssthresh) {
        r->window += p->size;
    } else {
        r->ccAcked += p->size;
        while (r->ccAcked >= r->window) {
            r->ccAcked -= r->window;
            r->window += r->maxDatagram;
        }
    }

    if (r->window > QUIC_MAX_WINDOW)
        r->window = QUIC_MAX_WINDOW;
}

// ---------------------------------------------------------------------------------------------
// Round trip time (RFC 9002 section 5)
// ---------------------------------------------------------------------------------------------

static void rttUpdate(_Inout_ QuicRecovery* r, int64 latest, int64 ackDelay, int64 now)
{
    if (latest < 0)
        latest = 0;

    r->rtt.latest = latest;

    if (!r->rtt.have) {
        r->rtt.have        = true;
        r->rtt.firstSample = now;
        r->rtt.min         = latest;
        r->rtt.smoothed    = latest;
        r->rtt.var         = latest / 2;
        return;
    }

    // The minimum ignores the peer's reported delay: it is the closest thing to a measurement of
    // the path alone, which is what tells whether the reported delay is plausible at all.
    if (latest < r->rtt.min)
        r->rtt.min = latest;

    // Before the handshake is confirmed the peer's max_ack_delay is not known to be in force, so
    // its reported delay is taken as given rather than capped.
    int64 delay = ackDelay;
    if (r->handshakeConfirmed && delay > r->maxAckDelay)
        delay = r->maxAckDelay;

    int64 adjusted = latest;
    if (latest >= r->rtt.min + delay)
        adjusted = latest - delay;

    int64 diff = r->rtt.smoothed > adjusted ? r->rtt.smoothed - adjusted
                                            : adjusted - r->rtt.smoothed;

    r->rtt.var      = (3 * r->rtt.var + diff) / 4;
    r->rtt.smoothed = (7 * r->rtt.smoothed + adjusted) / 8;
}

// ---------------------------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------------------------

static uint32 elicitTotal(_In_ const QuicRecovery* r)
{
    uint32 n = 0;
    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        if (!r->discarded[sp])
            n += r->elicitInFlight[sp];
    }
    return n;
}

// Whether the peer has proved it is at the address this endpoint is sending to. A server learns
// nothing from this -- it is the one doing the validating -- so it is always true there.
static bool peerValidated(_In_ const QuicRecovery* r)
{
    return r->server || r->handshakeConfirmed || r->peerValidated;
}

static int64 ptoDuration(_In_ const QuicRecovery* r)
{
    if (!r->rtt.have)
        return 2 * QUIC_INITIAL_RTT;

    int64 v = 4 * r->rtt.var;
    if (v < QUIC_GRANULARITY)
        v = QUIC_GRANULARITY;

    return r->rtt.smoothed + v;
}

// The earliest time a packet in any space becomes old enough to declare lost, and which space.
static int64 lossTimeAndSpace(_In_ const QuicRecovery* r, _Out_ int* spOut)
{
    int64 best = 0;
    *spOut     = QUIC_PNS_INITIAL;

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        if (r->discarded[sp] || r->lossTime[sp] == 0)
            continue;
        if (best == 0 || r->lossTime[sp] < best) {
            best   = r->lossTime[sp];
            *spOut = sp;
        }
    }

    return best;
}

// When to give up waiting for an acknowledgement and send a probe, and which space to send it in.
static int64 ptoTimeAndSpace(_In_ const QuicRecovery* r, int64 now, _Out_ int* spOut)
{
    uint32 shift = r->ptoCount > QUIC_MAX_PTO_BACKOFF ? QUIC_MAX_PTO_BACKOFF : r->ptoCount;
    int64 base   = ptoDuration(r) << shift;

    if (elicitTotal(r) == 0) {
        // Nothing is in flight, so nothing can be lost. A client still arms a timer here, because
        // a server that has not validated its address may be holding a flight it is not allowed
        // to send, and only another packet from the client will release it.
        *spOut = (r->ready[QUIC_PNS_HANDSHAKE] && !r->discarded[QUIC_PNS_HANDSHAKE])
                     ? QUIC_PNS_HANDSHAKE
                     : QUIC_PNS_INITIAL;
        return now + base;
    }

    int64 best = timeForever;
    *spOut     = QUIC_PNS_INITIAL;

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++) {
        if (r->discarded[sp] || r->elicitInFlight[sp] == 0)
            continue;

        int64 d = base;
        if (sp == QUIC_PNS_APP) {
            // A probe in the application space is no use before the handshake is confirmed: the
            // peer may not have the keys to read it, and cannot acknowledge what it cannot read.
            if (!r->handshakeConfirmed)
                continue;
            d += r->maxAckDelay << shift;
        }

        int64 t = r->lastElicitSent[sp] + d;
        if (t < best) {
            best   = t;
            *spOut = sp;
        }
    }

    return best;
}

static void recovSetTimer(_Inout_ QuicRecovery* r, int64 now)
{
    int sp  = QUIC_PNS_INITIAL;
    int64 t = lossTimeAndSpace(r, &sp);

    if (t != 0) {
        r->timer = t;
        return;
    }

    if (elicitTotal(r) == 0 && peerValidated(r)) {
        r->timer = timeForever;
        return;
    }

    r->timer = ptoTimeAndSpace(r, now, &sp);
}

// ---------------------------------------------------------------------------------------------
// Loss detection (RFC 9002 section 6.1)
// ---------------------------------------------------------------------------------------------

static void notifyLost(_Inout_ QuicRecovery* r, int sp, uint64 pn)
{
    if (r->handlers && r->handlers->lost)
        r->handlers->lost(r->hctx, sp, pn);
}

// Whether the losses recorded amount to the path having gone away rather than being congested,
// which RFC 9002 section 7.6 answers by asking how long the unbroken run of them lasted. A run
// that reaches back past packets the ring no longer holds is measured short, which errs towards
// leaving the window alone.
// How long a run of losses has to last before the path counts as gone rather than congested.
static int64 persistentPeriod(_In_ const QuicRecovery* r)
{
    return (ptoDuration(r) + r->maxAckDelay) * QUIC_PERSISTENT_CONGESTION_THRESHOLD;
}

// Resolved packets are kept for twice that, since a run that only just exceeds the period starts
// exactly that far back and would otherwise have been dropped before it could be measured.
static int64 retainCutoff(_In_ const QuicRecovery* r, int64 now)
{
    return now - 2 * persistentPeriod(r);
}

static bool persistentCongestion(_In_ QuicRecovery* r, int sp)
{
    if (!r->rtt.have)
        return false;

    int64 period = persistentPeriod(r);

    QuicSentRing* ring = &r->sent[sp];
    int64 runStart     = 0;
    bool open          = false;

    for (uint32 i = 0; i < ring->count; i++) {
        QuicSentPkt* p = &ring->pkts[(ring->head + i) & (ring->cap - 1)];

        if (p->fate != QUIC_SENT_LOST) {
            open = false;
            continue;
        }
        if (!p->ackEliciting)
            continue;

        // Only a run that started after the first round trip was measured says anything: before
        // that there is no estimate to compare its length against.
        if (p->sent < r->rtt.firstSample) {
            open = false;
            continue;
        }

        if (!open) {
            open     = true;
            runStart = p->sent;
        } else if (p->sent - runStart > period) {
            return true;
        }
    }

    return false;
}

// Declares lost every packet the acknowledgements have left behind, either by number -- three
// later packets have been acknowledged -- or by age.
static void detectLoss(_Inout_ QuicRecovery* r, int sp, int64 now)
{
    QuicSentRing* ring = &r->sent[sp];
    r->lossTime[sp]    = 0;

    uint64 largest = r->largestAcked[sp];
    if (largest == QUIC_PN_NONE)
        return;

    int64 delay = r->rtt.have
                      ? (r->rtt.latest > r->rtt.smoothed ? r->rtt.latest : r->rtt.smoothed)
                      : QUIC_INITIAL_RTT;

    // Nine eighths of a round trip: enough slack that ordinary reordering is not mistaken for a
    // loss, and RFC 9002 section 6.1.2 fixes the fraction so both ends behave alike.
    delay = delay * 9 / 8;
    if (delay < QUIC_GRANULARITY)
        delay = QUIC_GRANULARITY;

    int64 lostBefore = now - delay;

    int64 largestLost   = 0;
    bool largestInFlight = false;

    for (uint32 i = 0; i < ring->count; i++) {
        uint64 pn      = ring->basePn + i;
        QuicSentPkt* p = &ring->pkts[(ring->head + i) & (ring->cap - 1)];

        if (pn > largest)
            break;
        if (p->fate != QUIC_SENT_LIVE)
            continue;

        if (p->sent > lostBefore && largest < pn + QUIC_PACKET_THRESHOLD) {
            // Not lost yet, but it will be if nothing acknowledges it in time.
            int64 t = p->sent + delay;
            if (r->lossTime[sp] == 0 || t < r->lossTime[sp])
                r->lossTime[sp] = t;
            continue;
        }

        p->fate = QUIC_SENT_LOST;
        r->nlost++;

        if (p->ackEliciting && r->elicitInFlight[sp] > 0)
            r->elicitInFlight[sp]--;

        if (p->inFlight) {
            r->inFlight -= p->size;

            // RFC 8899 section 3: a path MTU probe is deliberately larger than the path is known
            // to carry, so losing one says the path is too small rather than that it is congested.
            // Counting it would make every search cut the window.
            if (!(p->marks & QUIC_SENT_PMTU_PROBE)) {
                largestLost     = p->sent;
                largestInFlight = true;
            }
        }

        notifyLost(r, sp, pn);
    }

    if (largestInFlight) {
        ccOnCongestion(r, largestLost, now);

        if (persistentCongestion(r, sp)) {
            r->window     = ccMinWindow(r);
            r->ccAcked    = 0;
            r->inRecovery = false;
            r->npersistent++;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Pacing (RFC 9002 section 7.7)
// ---------------------------------------------------------------------------------------------

// How much may leave at once. A flight goes out in one burst; only sustained sending is spread.
static int64 pacerBurst(_In_ const QuicRecovery* r)
{
    return (int64)(10 * (uint64)r->maxDatagram);
}

_Use_decl_annotations_
void _quicRecovPacerRefill(QuicRecovery* r, int64 now)
{
    if (r->tokenTime == 0) {
        r->tokenTime = now;
        r->tokens    = pacerBurst(r);
        return;
    }

    if (now <= r->tokenTime)
        return;

    int64 elapsed = now - r->tokenTime;
    if (elapsed > timeS(1))
        elapsed = timeS(1);
    r->tokenTime = now;

    int64 srtt = r->rtt.have ? r->rtt.smoothed : QUIC_INITIAL_RTT;
    if (srtt < QUIC_GRANULARITY)
        srtt = QUIC_GRANULARITY;

    // A window per round trip is the rate the congestion controller has settled on; sending at
    // exactly that would never leave room to discover more, so the pacer runs a little ahead of
    // it -- and twice ahead while the window is still doubling, which is where a sender most
    // needs to not be held back by its own pacing.
    int64 rate = (int64)r->window;
    rate       = (r->window < r->ssthresh) ? rate * 2 : rate * 5 / 4;

    r->tokens += elapsed * rate / srtt;
    if (r->tokens > pacerBurst(r))
        r->tokens = pacerBurst(r);
}

_Use_decl_annotations_
bool _quicRecovPacerReady(const QuicRecovery* r)
{
    return r->tokens > 0;
}

_Use_decl_annotations_
int64 _quicRecovPacerNext(const QuicRecovery* r)
{
    if (r->tokens > 0)
        return r->tokenTime;

    int64 srtt = r->rtt.have ? r->rtt.smoothed : QUIC_INITIAL_RTT;
    if (srtt < QUIC_GRANULARITY)
        srtt = QUIC_GRANULARITY;

    int64 rate = (int64)r->window;
    rate       = (r->window < r->ssthresh) ? rate * 2 : rate * 5 / 4;
    if (rate < 1)
        rate = 1;

    // Enough for a whole packet, not for one more byte: a sender that woke up for every byte the
    // bucket earned would spend its time waking up.
    int64 need = (int64)r->maxDatagram - r->tokens;
    int64 wait = need * srtt / rate;
    if (wait < 1)
        wait = 1;

    return r->tokenTime + wait;
}

// ---------------------------------------------------------------------------------------------
// The connection's side of it
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _quicRecovInit(QuicRecovery* r, bool server, size_t maxDatagram)
{
    memset(r, 0, sizeof(*r));

    r->server      = server;
    r->maxDatagram = maxDatagram;
    r->window      = ccInitialWindow(maxDatagram);
    r->ssthresh    = UINT64_MAX;
    r->timer       = timeForever;
    r->maxAckDelay = timeMS(25);   // the default until the peer's parameters arrive

    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++)
        r->largestAcked[sp] = QUIC_PN_NONE;
}

_Use_decl_annotations_
void _quicRecovDestroy(QuicRecovery* r)
{
    for (int sp = 0; sp < QUIC_PNS_COUNT; sp++)
        ringDestroy(&r->sent[sp]);
}

_Use_decl_annotations_
void _quicRecovSetHandlers(QuicRecovery* r, const QuicRecovHandlers* handlers, void* ctx)
{
    r->handlers = handlers;
    r->hctx     = ctx;
}

_Use_decl_annotations_
void _quicRecovSetMaxAckDelay(QuicRecovery* r, int64 delay)
{
    r->maxAckDelay = delay;
}

_Use_decl_annotations_
void _quicRecovSetAppLimited(QuicRecovery* r, bool limited)
{
    r->appLimited = limited;
}

_Use_decl_annotations_
void _quicRecovSpaceReady(QuicRecovery* r, int sp)
{
    r->ready[sp] = true;
}

_Use_decl_annotations_
void _quicRecovDiscardSpace(QuicRecovery* r, int sp, int64 now)
{
    if (r->discarded[sp])
        return;

    QuicSentRing* ring = &r->sent[sp];
    for (uint32 i = 0; i < ring->count; i++) {
        QuicSentPkt* p = &ring->pkts[(ring->head + i) & (ring->cap - 1)];
        if (p->fate == QUIC_SENT_LIVE && p->inFlight)
            r->inFlight -= p->size;
    }
    ringDestroy(ring);

    r->discarded[sp]      = true;
    r->elicitInFlight[sp] = 0;
    r->lossTime[sp]       = 0;
    r->probes[sp]         = 0;

    // The handshake moving on is not evidence that anything was lost, so the backoff that was
    // building up in the space that just went away does not carry into the next one.
    r->ptoCount = 0;

    recovSetTimer(r, now);
}

_Use_decl_annotations_
void _quicRecovDiscardEarly(QuicRecovery* r, uint64 pnEnd, int64 now)
{
    QuicSentRing* ring = &r->sent[QUIC_PNS_APP];

    for (uint32 i = 0; i < ring->count; i++) {
        uint64 pn      = ring->basePn + i;
        QuicSentPkt* p = &ring->pkts[(ring->head + i) & (ring->cap - 1)];

        if (pn >= pnEnd)
            break;
        if (p->fate != QUIC_SENT_LIVE)
            continue;

        p->fate = QUIC_SENT_LOST;

        if (p->ackEliciting && r->elicitInFlight[QUIC_PNS_APP] > 0)
            r->elicitInFlight[QUIC_PNS_APP]--;
        if (p->inFlight)
            r->inFlight -= p->size;

        // Reported as lost so that whatever was in them is queued to go again, but not counted as
        // loss: nothing about the path went wrong, the peer simply declined to read them. Cutting
        // the window here would punish the connection for having tried.
        if (r->handlers && r->handlers->lost)
            r->handlers->lost(r->hctx, QUIC_PNS_APP, pn);
    }

    recovSetTimer(r, now);
}

_Use_decl_annotations_
void _quicRecovConfirmHandshake(QuicRecovery* r, int64 now)
{
    r->handshakeConfirmed = true;
    r->peerValidated      = true;
    recovSetTimer(r, now);
}

_Use_decl_annotations_
_Use_decl_annotations_
int64 _quicRecovPto(const QuicRecovery* r)
{
    return ptoDuration(r);
}

_Use_decl_annotations_
void _quicRecovMark(QuicRecovery* r, int sp, uint64 pn, uint8 marks)
{
    QuicSentPkt* p = ringAt(&r->sent[sp], pn);
    if (p)
        p->marks |= marks;
}

_Use_decl_annotations_
uint8 _quicRecovMarksOf(const QuicRecovery* r, int sp, uint64 pn)
{
    QuicSentPkt* p = ringAt((QuicSentRing*)&r->sent[sp], pn);
    return p ? p->marks : 0;
}

_Use_decl_annotations_
void _quicRecovOnEcnCe(QuicRecovery* r, int sp, uint64 largestAcked, int64 now)
{
    // The peer saw a router mark a packet rather than drop it. That is the same news a loss
    // carries and it arrives a round trip sooner, so it gets the same answer -- and, like a loss,
    // only once per round trip, which is what passing the marked packet's send time does.
    QuicSentPkt* p = ringAt(&r->sent[sp], largestAcked);
    ccOnCongestion(r, p ? p->sent : now, now);
}

_Use_decl_annotations_
void _quicRecovOnPathChange(QuicRecovery* r, int64 now)
{
    // RFC 9000 section 9.4. Everything below is a measurement of the path that was, and none of it
    // transfers: the window, what is in flight on it, the round trip, and the recovery episode.
    r->window     = ccInitialWindow(r->maxDatagram);
    r->ssthresh   = UINT64_MAX;
    r->ccAcked    = 0;
    r->inRecovery = false;
    r->recoveryStart = 0;

    memset(&r->rtt, 0, sizeof(r->rtt));
    r->rtt.smoothed = QUIC_INITIAL_RTT;
    r->rtt.min      = 0;
    r->rtt.var      = QUIC_INITIAL_RTT / 2;
    r->ptoCount     = 0;

    // Zeroing the refill time is the same "nothing has been sent yet" state a new connection is in,
    // which is what lets the first flight on the new path leave in one burst instead of waiting for
    // a pacer that has earned nothing.
    r->tokens    = 0;
    r->tokenTime = 0;
    unused_noeval(now);
}

_Use_decl_annotations_
void _quicRecovSetMaxDatagram(QuicRecovery* r, size_t maxDatagram)
{
    if (maxDatagram == 0 || maxDatagram == r->maxDatagram)
        return;

    r->maxDatagram = maxDatagram;

    // The window is counted in bytes, so a larger datagram only changes the floor it may not fall
    // below. Raising the window itself here would hand out capacity the path never demonstrated.
    uint64 min = ccMinWindow(r);
    if (r->window < min)
        r->window = min;
}

void _quicRecovOnSent(QuicRecovery* r, int sp, uint64 pn, size_t size, bool ackEliciting,
                      bool inFlight, int64 now)
{
    if (r->discarded[sp])
        return;

    QuicSentPkt p;
    memset(&p, 0, sizeof(p));
    p.sent         = now;
    p.size         = (uint32)size;
    p.fate         = QUIC_SENT_LIVE;
    p.ackEliciting = ackEliciting;
    p.inFlight     = inFlight;

    if (!ringPush(&r->sent[sp], pn, &p))
        return;

    r->tokens -= (int64)size;

    if (inFlight)
        r->inFlight += size;

    if (ackEliciting) {
        r->elicitInFlight[sp]++;
        r->lastElicitSent[sp] = now;
    }

    if (inFlight)
        recovSetTimer(r, now);
}

_Use_decl_annotations_
bool _quicRecovOnAck(QuicRecovery* r, int sp, const QuicFrame* f, int64 ackDelay, int64 now)
{
    if (r->discarded[sp])
        return true;

    QuicSentRing* ring = &r->sent[sp];

    if (r->largestAcked[sp] == QUIC_PN_NONE || f->ack.largest > r->largestAcked[sp])
        r->largestAcked[sp] = f->ack.largest;

    QuicAckIter it;
    QuicAckRange rg;
    if (!_quicAckIterInit(&it, f))
        return false;

    bool any        = false;
    bool sample     = false;   // the largest acknowledged packet can time a round trip
    int64 sampleSent = 0;

    while (_quicAckIterNext(&it, &rg)) {
        if (ring->count == 0)
            continue;

        // Ranges are clamped to what was actually sent, so a peer naming numbers this endpoint
        // never used costs a comparison rather than a walk through them.
        uint64 lo = rg.smallest > ring->basePn ? rg.smallest : ring->basePn;
        uint64 hi = ring->basePn + ring->count - 1;
        if (rg.largest < hi)
            hi = rg.largest;

        for (uint64 pn = lo; pn <= hi; pn++) {
            QuicSentPkt* p = ringAt(ring, pn);
            if (!p || p->fate != QUIC_SENT_LIVE)
                continue;

            p->fate  = QUIC_SENT_ACKED;
            p->fresh = true;
            any      = true;

            if (p->ackEliciting && r->elicitInFlight[sp] > 0)
                r->elicitInFlight[sp]--;

            if (p->inFlight)
                r->inFlight -= p->size;

            // Only the largest acknowledged packet times a round trip, and only if it is one the
            // peer was obliged to acknowledge: an acknowledgement the peer sent for its own
            // reasons says nothing about how long this endpoint's packet took to arrive.
            if (pn == f->ack.largest && p->ackEliciting) {
                sample     = true;
                sampleSent = p->sent;
            }

            if (r->handlers && r->handlers->acked)
                r->handlers->acked(r->hctx, sp, pn);
        }
    }

    if (it.bad)
        return false;

    if (!any) {
        recovSetTimer(r, now);
        return true;
    }

    if (sample)
        rttUpdate(r, now - sampleSent, ackDelay, now);

    // An acknowledgement in the handshake space is the client's proof that the server received
    // something from it, which is what ends the client's obligation to keep proving its address.
    if (sp == QUIC_PNS_HANDSHAKE)
        r->peerValidated = true;

    detectLoss(r, sp, now);

    // Now that the window knows about anything this acknowledgement revealed as lost, the packets
    // it did acknowledge can grow it.
    for (uint32 i = 0; i < ring->count; i++) {
        QuicSentPkt* p = &ring->pkts[(ring->head + i) & (ring->cap - 1)];
        if (!p->fresh)
            continue;

        p->fresh = false;
        if (p->inFlight)
            ccOnAcked(r, p);
    }

    ringTrim(ring, retainCutoff(r, now));

    if (peerValidated(r))
        r->ptoCount = 0;

    recovSetTimer(r, now);
    return true;
}

_Use_decl_annotations_
int64 _quicRecovTimer(const QuicRecovery* r)
{
    return r->timer;
}

_Use_decl_annotations_
void _quicRecovOnTimeout(QuicRecovery* r, int64 now)
{
    int sp  = QUIC_PNS_INITIAL;
    int64 t = lossTimeAndSpace(r, &sp);

    if (t != 0) {
        if (now >= t) {
            detectLoss(r, sp, now);
            ringTrim(&r->sent[sp], retainCutoff(r, now));
        }
        recovSetTimer(r, now);
        return;
    }

    if (r->timer == timeForever || now < r->timer)
        return;

    if (elicitTotal(r) == 0) {
        sp = (r->ready[QUIC_PNS_HANDSHAKE] && !r->discarded[QUIC_PNS_HANDSHAKE])
                 ? QUIC_PNS_HANDSHAKE
                 : QUIC_PNS_INITIAL;
    } else {
        ptoTimeAndSpace(r, now, &sp);
    }

    // Two packets, so that one being lost as well does not cost another whole timeout. They are
    // owed rather than sent: the connection is the only thing that can build one.
    if (r->probes[sp] < 2)
        r->probes[sp] = 2;

    r->ptoCount++;
    recovSetTimer(r, now);
}

_Use_decl_annotations_
uint32 _quicRecovProbes(const QuicRecovery* r, int sp)
{
    return r->probes[sp];
}

_Use_decl_annotations_
void _quicRecovProbeSent(QuicRecovery* r, int sp)
{
    if (r->probes[sp] > 0)
        r->probes[sp]--;
}

_Use_decl_annotations_
bool _quicRecovCanSend(const QuicRecovery* r)
{
    return r->inFlight < r->window;
}

_Use_decl_annotations_
bool _quicRecovCanSendSize(const QuicRecovery* r, size_t size)
{
    return r->inFlight + (uint64)size <= r->window;
}

_Use_decl_annotations_
size_t _quicRecovWindowRoom(const QuicRecovery* r)
{
    if (r->inFlight >= r->window)
        return 0;

    uint64 room = r->window - r->inFlight;
    return room > SIZE_MAX ? SIZE_MAX : (size_t)room;
}
