// QUIC streams: the two halves of each one, and the flow control they share.
//
// Three ideas carry most of this file.
//
// A stream is two independent halves. The sending half and the receiving half of the same stream
// have their own states and their own reasons to end, and a bidirectional stream is simply one
// where both exist. That is why a unidirectional stream is not a special case here: the half that
// cannot exist starts in its NONE state and is never looked at again.
//
// Flow control is two limits over the same bytes. Every byte counts against the stream it is on
// and against the connection as a whole, so both have to have room before anything can be sent,
// and both have to be extended as the application reads. A limit is raised by announcing a new
// absolute offset, never a delta, so a lost announcement is superseded by the next one rather
// than retransmitted.
//
// Retransmission works the way the rest of cxquic's does: nothing records what a packet held.
// Stream data lives in a QuicSendBuf, which remembers the packet each range went into, and each
// single-shot frame is a QuicCtl doing the same for itself.

#include "stream_private.h"

#include <cx/format.h>
#include <cx/container.h>

#undef LOG_CHANNEL
#define LOG_CHANNEL QuicLogChannel

// The smallest useful STREAM frame: a type byte, a stream id, an offset, a length, and a byte to
// carry. Below this there is no point asking the send buffer for a range.
#define QUIC_STREAM_MIN_FRAME 6

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

static void streamsFail(_Inout_ QuicStreams* ss, uint64 error, uint64 frameType)
{
    if (ss->error == 0) {
        ss->error      = error;
        ss->errorFrame = frameType;
    }
}

_Use_decl_annotations_
QuicStreamState* _quicStreamFind(const QuicStreams* ss, uint64 id)
{
    QuicStreamState* st = NULL;
    if (!htFind(ss->byId, uint64, id, ptr, &st))
        return NULL;
    return st;
}

// Puts a stream on the list of those the packet builder walks. Everything that gives a stream
// something to say goes through here, so a stream that is merely open costs nothing to have.
static void streamArm(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st)
{
    if (st->active)
        return;

    st->active = true;
    saPush(&ss->active, uint64, st->id);
}

static bool streamPending(_In_ const QuicStreamState* st)
{
    return st->stopSending.state == QUIC_CTL_PENDING || st->reset.state == QUIC_CTL_PENDING ||
           st->maxData.state == QUIC_CTL_PENDING || st->blocked.state == QUIC_CTL_PENDING ||
           st->fin.state == QUIC_CTL_PENDING || _quicSendBufPending(&st->out);
}

static bool streamOutstanding(_In_ const QuicStreamState* st)
{
    return st->stopSending.state == QUIC_CTL_SENT || st->reset.state == QUIC_CTL_SENT ||
           st->maxData.state == QUIC_CTL_SENT || st->blocked.state == QUIC_CTL_SENT ||
           st->fin.state == QUIC_CTL_SENT || _quicSendBufOutstanding(&st->out) > 0;
}

static bool sendHalfDone(_In_ const QuicStreamState* st)
{
    return st->sendState == QUIC_SEND_DATA_DONE || st->sendState == QUIC_SEND_RESET_DONE ||
           st->sendState == QUIC_SEND_NONE;
}

static bool recvHalfDone(_In_ const QuicStreamState* st)
{
    return st->recvState == QUIC_RECV_DATA_READ || st->recvState == QUIC_RECV_RESET_READ ||
           st->recvState == QUIC_RECV_NONE;
}

// How much room the peer's two flow control limits leave on this stream right now.
static uint64 sendRoom(_In_ const QuicStreams* ss, _In_ const QuicStreamState* st)
{
    if (st->sendState != QUIC_SEND_READY && st->sendState != QUIC_SEND_SEND)
        return 0;

    uint64 room = st->sendMax > st->out.end ? st->sendMax - st->out.end : 0;

    uint64 conn = ss->sendMax > ss->sendOff ? ss->sendMax - ss->sendOff : 0;
    if (conn < room)
        room = conn;

    uint64 held = st->out.end - st->out.base;
    uint64 buf  = held < ss->sendBufMax ? ss->sendBufMax - held : 0;
    return buf < room ? buf : room;
}

// ---------------------------------------------------------------------------------------------
// Creating and destroying streams
// ---------------------------------------------------------------------------------------------

static _Ret_maybenull_ QuicStreamState* streamCreate(_Inout_ QuicStreams* ss, uint64 id)
{
    QuicStreamState* st = xaAlloc(sizeof(QuicStreamState), XA_Zero);
    st->id         = id;

    bool local = _quicStreamLocal(id, ss->server);
    bool uni   = _quicStreamUni(id);

    _quicSendBufInit(&st->out);

    uint64 recvLimit = 0;
    if (uni) {
        // Only one half of a unidirectional stream exists, and which one depends on who opened it.
        if (local) {
            st->recvState = QUIC_RECV_NONE;
            st->sendMax   = ss->peerSdUni;
        } else {
            st->sendState = QUIC_SEND_NONE;
            recvLimit     = ss->localSdUni;
        }
    } else {
        // The two bidirectional parameters are named from the point of view of whoever opened the
        // stream, so each end reads the opposite one of the peer's from the one it reads of its
        // own.
        recvLimit   = local ? ss->localSdBidiLocal : ss->localSdBidiRemote;
        st->sendMax = local ? ss->peerSdBidiRemote : ss->peerSdBidiLocal;
    }

    _quicReasmInit(&st->in, recvLimit);
    st->recvWindow  = recvLimit;

    // A limit of zero is a real limit to complain about, so "nothing has been said yet" needs a
    // value no limit can take.
    st->sendBlocked = UINT64_MAX;

    htInsert(&ss->byId, uint64, id, ptr, st);
    return st;
}

static void streamFree(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st)
{
    uint64 id = st->id;

    _quicSendBufDestroy(&st->out);
    _quicReasmDestroy(&st->in);
    xaFree(st);

    htRemove(&ss->byId, uint64, id);
}

// Raises the number of streams of one kind the peer may open, which is what makes room for a new
// one after a finished one is forgotten.
static void streamsGrantMore(_Inout_ QuicStreams* ss, int dir)
{
    if (ss->recvMaxStreams[dir] >= QUIC_MAX_STREAM_COUNT)
        return;

    ss->recvMaxStreams[dir]++;
    _quicCtlQueue(&ss->maxStreams[dir]);
}

// Forgets a stream once neither half has anything left to do. A stream the peer opened gives its
// place back to the peer as it goes.
static void streamSettle(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st)
{
    if (!sendHalfDone(st) || !recvHalfDone(st))
        return;

    uint64 id  = st->id;
    bool local = _quicStreamLocal(id, ss->server);
    int dir    = _quicStreamDir(id);

    if (ss->handlers && ss->handlers->closed)
        ss->handlers->closed(ss->hctx, id);

    streamFree(ss, st);

    if (!local)
        streamsGrantMore(ss, dir);
}

// Brings a stream the peer named into existence, along with every lower-numbered one of its kind:
// RFC 9000 section 2.1 opens those implicitly, since the peer may use them in any order.
//
// Returns false when the id cannot be admitted. That is a connection error if `error` was set and
// otherwise a stream that has already been finished and forgotten, whose frames are ignored.
static bool streamAdmit(_Inout_ QuicStreams* ss, uint64 id, uint64 frameType)
{
    int dir    = _quicStreamDir(id);
    uint64 idx = _quicStreamIndex(id);

    if (_quicStreamLocal(id, ss->server)) {
        // A stream this endpoint has not opened cannot be spoken about; one it has opened and
        // finished is simply gone.
        if (idx >= ss->nextLocal[dir])
            streamsFail(ss, QUIC_ERR_STREAM_STATE_ERROR, frameType);
        return false;
    }

    if (idx < ss->nextRemote[dir])
        return false;

    if (idx >= ss->recvMaxStreams[dir]) {
        streamsFail(ss, QUIC_ERR_STREAM_LIMIT_ERROR, frameType);
        return false;
    }

    for (uint64 i = ss->nextRemote[dir]; i <= idx; i++) {
        QuicStreamState* st = streamCreate(ss, _quicStreamMakeId(i, !ss->server, dir == QUIC_SDIR_UNI));
        if (!st) {
            streamsFail(ss, QUIC_ERR_INTERNAL_ERROR, frameType);
            return false;
        }

        ss->nextRemote[dir] = i + 1;

        if (ss->handlers && ss->handlers->opened && !ss->handlers->opened(ss->hctx, st->id)) {
            streamsFail(ss, QUIC_ERR_STREAM_LIMIT_ERROR, frameType);
            return false;
        }
    }

    return true;
}

// Finds the stream a received frame names, creating it if the peer is allowed to. `ignore` says
// the frame is about a stream that no longer exists, which is not an error.
static _Ret_maybenull_ QuicStreamState* streamForFrame(_Inout_ QuicStreams* ss, uint64 id,
                                                  uint64 frameType, _Out_ bool* ignore)
{
    *ignore = false;

    QuicStreamState* st = _quicStreamFind(ss, id);
    if (st)
        return st;

    if (!streamAdmit(ss, id, frameType)) {
        *ignore = ss->error == 0;
        return NULL;
    }

    return _quicStreamFind(ss, id);
}

// ---------------------------------------------------------------------------------------------
// Flow control windows this endpoint advertises
// ---------------------------------------------------------------------------------------------

// Moves a receive limit forward once the application has read enough for the move to be worth a
// frame. Announcing every byte read would cost a frame per read; waiting for the window to run
// out would stall the peer, so the limit is pushed out again once half of it has been used.
static void recvWindowUpdate(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st)
{
    if (st->recvWindow == 0 || st->recvState != QUIC_RECV_RECV)
        return;

    uint64 want = st->recvRead + st->recvWindow;
    if (want <= st->in.limit || want - st->in.limit < st->recvWindow / 2)
        return;

    st->in.limit = want;
    _quicCtlQueue(&st->maxData);
    streamArm(ss, st);
}

static void connWindowUpdate(_Inout_ QuicStreams* ss)
{
    if (ss->recvWindow == 0)
        return;

    uint64 want = ss->recvRead + ss->recvWindow;
    if (want <= ss->recvMax || want - ss->recvMax < ss->recvWindow / 2)
        return;

    ss->recvMax = want;
    _quicCtlQueue(&ss->maxData);
}

// Bytes that will never reach the application still have to give their flow control credit back,
// or a connection that resets streams runs its own window down to nothing.
static void recvRelease(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st, uint64 upto)
{
    if (upto <= st->recvRead)
        return;

    ss->recvRead += upto - st->recvRead;
    st->recvRead = upto;

    connWindowUpdate(ss);
}

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
void _quicStreamsInit(QuicStreams* ss, bool server, const QuicTransportParams* tp)
{
    memset(ss, 0, sizeof(*ss));
    ss->server = server;

    htInit(&ss->byId, uint64, ptr, 16);
    saInit(&ss->active, uint64, 8);

    ss->recvMax     = tp->initMaxData;
    ss->recvWindow  = tp->initMaxData;
    ss->sendBlocked = UINT64_MAX;

    // Overwritten by whatever owns the connection if it has been configured; on its own the stream
    // layer buffers the default and wakes a refused sender as soon as what it asked for fits.
    ss->sendBufMax = QUIC_STREAM_SEND_MAX;
    ss->sendLow    = 0;

    for (int dir = 0; dir < QUIC_SDIR_COUNT; dir++)
        ss->streamsBlockedAt[dir] = UINT64_MAX;

    ss->localSdBidiLocal  = tp->initMaxSdBidiLocal;
    ss->localSdBidiRemote = tp->initMaxSdBidiRemote;
    ss->localSdUni        = tp->initMaxSdUni;

    ss->recvMaxStreams[QUIC_SDIR_BIDI] = tp->initMaxStreamsBidi;
    ss->recvMaxStreams[QUIC_SDIR_UNI]  = tp->initMaxStreamsUni;
}

_Use_decl_annotations_
void _quicStreamsDestroy(QuicStreams* ss)
{
    htiter it;
    htiInit(&it, ss->byId);
    while (htiValid(&it)) {
        QuicStreamState* st = htiVal(ptr, it);
        _quicSendBufDestroy(&st->out);
        _quicReasmDestroy(&st->in);
        xaFree(st);
        htiNext(&it);
    }
    htiFinish(&it);

    htDestroy(&ss->byId);
    saDestroy(&ss->active);
    memset(ss, 0, sizeof(*ss));
}

_Use_decl_annotations_
void _quicStreamsSetHandlers(QuicStreams* ss, const QuicStreamHandlers* handlers, void* ctx)
{
    ss->handlers = handlers;
    ss->hctx     = ctx;
}

_Use_decl_annotations_
void _quicStreamsPeerParams(QuicStreams* ss, const QuicTransportParams* tp)
{
    ss->sendMax = tp->initMaxData;

    ss->peerSdBidiLocal  = tp->initMaxSdBidiLocal;
    ss->peerSdBidiRemote = tp->initMaxSdBidiRemote;
    ss->peerSdUni        = tp->initMaxSdUni;

    ss->sendMaxStreams[QUIC_SDIR_BIDI] = tp->initMaxStreamsBidi;
    ss->sendMaxStreams[QUIC_SDIR_UNI]  = tp->initMaxStreamsUni;
}

// ---------------------------------------------------------------------------------------------
// Frames arriving
// ---------------------------------------------------------------------------------------------

// Whether this endpoint can receive on a stream at all. A unidirectional stream it opened itself
// runs the other way, so anything the peer says about that stream's contents is a state error.
static bool canRecv(_In_ const QuicStreams* ss, uint64 id)
{
    return !(_quicStreamUni(id) && _quicStreamLocal(id, ss->server));
}

// The mirror: a unidirectional stream the peer opened cannot be written to.
static bool canSend(_In_ const QuicStreams* ss, uint64 id)
{
    return !(_quicStreamUni(id) && !_quicStreamLocal(id, ss->server));
}

// Takes the connection and stream credit a newly seen offset uses up. The two limits are checked
// together because a frame that overruns either one is a connection error.
static bool recvAccount(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st, uint64 end,
                        uint64 frameType)
{
    if (end <= st->recvHighest)
        return true;

    if (end > st->in.limit) {
        streamsFail(ss, QUIC_ERR_FLOW_CONTROL_ERROR, frameType);
        return false;
    }

    uint64 delta = end - st->recvHighest;
    if (ss->recvOff + delta > ss->recvMax) {
        streamsFail(ss, QUIC_ERR_FLOW_CONTROL_ERROR, frameType);
        return false;
    }

    ss->recvOff += delta;
    st->recvHighest = end;
    return true;
}

// Records the offset the peer says the stream ends at. Once named it can never change, and no
// byte may appear past it.
static bool recvFinalSize(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st, uint64 size,
                          uint64 frameType)
{
    if (st->recvFinalKnown) {
        if (size != st->recvFinal) {
            streamsFail(ss, QUIC_ERR_FINAL_SIZE_ERROR, frameType);
            return false;
        }
        return true;
    }

    if (size < st->recvHighest) {
        streamsFail(ss, QUIC_ERR_FINAL_SIZE_ERROR, frameType);
        return false;
    }

    st->recvFinal      = size;
    st->recvFinalKnown = true;
    return true;
}

static void recvNotify(_Inout_ QuicStreams* ss, _In_ const QuicStreamState* st)
{
    if (ss->handlers && ss->handlers->readable)
        ss->handlers->readable(ss->hctx, st->id);
}

static bool handleStreamData(_Inout_ QuicStreams* ss, _In_ const QuicFrame* f)
{
    if (!canRecv(ss, f->stream.id)) {
        streamsFail(ss, QUIC_ERR_STREAM_STATE_ERROR, f->type);
        return false;
    }

    bool ignore;
    QuicStreamState* st = streamForFrame(ss, f->stream.id, f->type, &ignore);
    if (!st)
        return ignore;

    uint64 end = f->stream.offset + f->stream.len;
    bool fin   = (f->type & QUIC_STREAM_FIN) != 0;

    // Nothing may follow the end of the stream, whether the end was named by this frame or an
    // earlier one.
    if (st->recvFinalKnown && (end > st->recvFinal || (fin && end != st->recvFinal))) {
        streamsFail(ss, QUIC_ERR_FINAL_SIZE_ERROR, f->type);
        return false;
    }

    if (fin && !recvFinalSize(ss, st, end, f->type))
        return false;

    if (!recvAccount(ss, st, end, f->type))
        return false;

    // A stream the peer already reset, or one this endpoint has read to the end of, keeps its
    // flow control accounting -- the offsets were still used -- but has nowhere to put the bytes.
    if (st->recvState == QUIC_RECV_RESET || st->recvState == QUIC_RECV_RESET_READ ||
        st->recvState == QUIC_RECV_DATA_READ)
        return true;

    if (f->stream.len > 0 &&
        !_quicReasmAdd(&st->in, f->stream.offset, f->stream.data, (size_t)f->stream.len)) {
        streamsFail(ss, QUIC_ERR_INTERNAL_ERROR, f->type);
        return false;
    }

    if (st->recvFinalKnown && st->recvState == QUIC_RECV_RECV)
        st->recvState = QUIC_RECV_SIZE_KNOWN;

    if (st->recvState == QUIC_RECV_SIZE_KNOWN &&
        st->in.base + _quicReasmReadable(&st->in) == st->recvFinal)
        st->recvState = QUIC_RECV_DATA_RECVD;

    if (_quicReasmReadable(&st->in) > 0 || st->recvState == QUIC_RECV_DATA_RECVD)
        recvNotify(ss, st);

    return true;
}

static bool handleResetStream(_Inout_ QuicStreams* ss, _In_ const QuicFrame* f)
{
    if (!canRecv(ss, f->resetStream.id)) {
        streamsFail(ss, QUIC_ERR_STREAM_STATE_ERROR, f->type);
        return false;
    }

    bool ignore;
    QuicStreamState* st = streamForFrame(ss, f->resetStream.id, f->type, &ignore);
    if (!st)
        return ignore;

    if (!recvFinalSize(ss, st, f->resetStream.finalSize, f->type))
        return false;

    if (st->recvState == QUIC_RECV_RESET || st->recvState == QUIC_RECV_RESET_READ)
        return true;

    // The peer stopped at the final size, so every offset up to it is spoken for even though the
    // bytes will never arrive.
    if (!recvAccount(ss, st, st->recvFinal, f->type))
        return false;

    st->recvState = QUIC_RECV_RESET;

    // What was buffered is not going to be read, and its credit is owed back to the peer.
    uint64 limit = st->in.limit;
    _quicReasmDestroy(&st->in);
    _quicReasmInit(&st->in, limit);
    recvRelease(ss, st, st->recvFinal);

    if (ss->handlers && ss->handlers->reset)
        ss->handlers->reset(ss->hctx, st->id, f->resetStream.error);

    recvNotify(ss, st);
    return true;
}

// Abandons the sending half. Used both when the application asks and when the peer does with a
// STOP_SENDING frame.
static void sendReset(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st, uint64 error)
{
    if (st->sendState != QUIC_SEND_READY && st->sendState != QUIC_SEND_SEND &&
        st->sendState != QUIC_SEND_DATA_SENT)
        return;

    st->sendFinal  = st->out.end;
    st->sendState  = QUIC_SEND_RESET_SENT;
    st->resetError = error;

    // Nothing queued is worth sending any more, and the frames that would have carried it stop
    // being owed.
    _quicSendBufDestroy(&st->out);
    _quicSendBufInit(&st->out);
    memset(&st->fin, 0, sizeof(st->fin));
    memset(&st->blocked, 0, sizeof(st->blocked));

    _quicCtlQueue(&st->reset);
    streamArm(ss, st);
}

static bool handleStopSending(_Inout_ QuicStreams* ss, _In_ const QuicFrame* f)
{
    if (!canSend(ss, f->stopSending.id)) {
        streamsFail(ss, QUIC_ERR_STREAM_STATE_ERROR, f->type);
        return false;
    }

    bool ignore;
    QuicStreamState* st = streamForFrame(ss, f->stopSending.id, f->type, &ignore);
    if (!st)
        return ignore;

    st->stopError = f->stopSending.error;

    if (ss->handlers && ss->handlers->stopped)
        ss->handlers->stopped(ss->hctx, st->id, f->stopSending.error);

    // RFC 9000 section 3.5: the answer to STOP_SENDING is a RESET_STREAM, so the peer learns
    // where the stream ended rather than waiting for data that is not coming.
    sendReset(ss, st, f->stopSending.error);
    return true;
}


// Records that the sender has stopped, and how much room it would take to be worth waking. `want`
// is what it asked for and could not have; `room` is what was left over. A stream already waiting
// keeps the higher watermark -- it has already turned that much room down once.
//
// A sender that spent its window to exactly zero without being refused anything asks for nothing
// in particular, and passing `want` of 1 gets it the old behaviour: any room at all will do.
static void sendWait(_In_ const QuicStreams* ss, _Inout_ QuicStreamState* st, uint64 want,
                     uint64 room)
{
    // Never wake for less than the configured watermark, never wait for more than the buffer could
    // hold even when empty, and never wait for what the sender already has -- the last two would
    // be waiting for something that may never arrive.
    uint64 low = want > ss->sendLow ? want : ss->sendLow;
    if (low > ss->sendBufMax)
        low = ss->sendBufMax;
    if (low <= room)
        low = room + 1;

    if (st->wantWritable && st->sendWaitLow >= low)
        return;

    st->wantWritable = true;
    st->sendWaitLow  = low;
}

// More room arriving is the one thing that can free a stream whose sender has stopped, and only
// that stream hears about it. One that was never held up has nothing to be told.
//
// Falling short of the watermark means waiting, but only while waiting is certain to end. Bytes
// still in flight will release buffer space when they are acknowledged, so more room is coming
// whatever the peer does -- that is the case the watermark exists for, since acknowledgements
// arrive a packet at a time and would otherwise wake the sender once for each.
//
// With nothing in flight, only the peer can make more room, and it may have already given
// everything it ever will. Waiting on a watermark nothing will reach is how a transfer stops for
// good, so the sender is woken with what there is and left to decide. That costs at most one extra
// wake-up per stall, and it is what makes the watermark safe to apply at all.
static void sendUnblock(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st)
{
    if (!st->wantWritable)
        return;

    uint64 room = sendRoom(ss, st);
    if (room == 0 || (room < st->sendWaitLow && _quicSendBufOutstanding(&st->out) > 0))
        return;

    st->wantWritable = false;

    if (ss->handlers && ss->handlers->writable)
        ss->handlers->writable(ss->hctx, st->id);
}

// The connection's limit holds back every stream at once, so raising it can free all of them.
static void connMaxData(_Inout_ QuicStreams* ss, uint64 max)
{
    if (max <= ss->sendMax)
        return;

    // Which streams were held up has to be read before the limit moves, since sendRoom() measures
    // against it. There is nothing worth testing at the connection level to decide whether this
    // walk is needed: the connection can be nowhere near its limit while an individual stream sits
    // on the last few bytes of it, unable to spend them.
    htiter it;
    htiInit(&it, ss->byId);
    while (htiValid(&it)) {
        QuicStreamState* st = htiVal(ptr, it);
        if (sendRoom(ss, st) == 0)
            sendWait(ss, st, 1, 0);
        htiNext(&it);
    }
    htiFinish(&it);

    ss->sendMax = max;

    // The wall the last complaint described is gone, so the complaint goes with it -- whether it
    // is still queued or already in flight, repeating it would only mislead.
    ss->dataBlocked.state = QUIC_CTL_IDLE;

    htiInit(&it, ss->byId);
    while (htiValid(&it)) {
        sendUnblock(ss, htiVal(ptr, it));
        htiNext(&it);
    }
    htiFinish(&it);
}

static bool handleMaxStreamData(_Inout_ QuicStreams* ss, _In_ const QuicFrame* f)
{
    if (!canSend(ss, f->maxStreamData.id)) {
        streamsFail(ss, QUIC_ERR_STREAM_STATE_ERROR, f->type);
        return false;
    }

    bool ignore;
    QuicStreamState* st = streamForFrame(ss, f->maxStreamData.id, f->type, &ignore);
    if (!st)
        return ignore;

    if (f->maxStreamData.max <= st->sendMax)
        return true;

    // Measured against every bound, not just the one this frame raises: a stream out of room for
    // any reason is one that stopped writing and is waiting to hear it may start again.
    if (sendRoom(ss, st) == 0)
        sendWait(ss, st, 1, 0);

    st->sendMax       = f->maxStreamData.max;
    st->blocked.state = QUIC_CTL_IDLE;

    sendUnblock(ss, st);
    return true;
}

static bool handleMaxStreams(_Inout_ QuicStreams* ss, _In_ const QuicFrame* f)
{
    int dir = f->type == QUIC_FRAME_MAX_STREAMS_UNI ? QUIC_SDIR_UNI : QUIC_SDIR_BIDI;
    if (f->maxStreams.max > ss->sendMaxStreams[dir]) {
        ss->sendMaxStreams[dir]       = f->maxStreams.max;
        ss->streamsBlocked[dir].state = QUIC_CTL_IDLE;
    }

    return true;
}

// The peer saying it has run out of room. Nothing has to be done about it -- this endpoint sends
// its limits when the application reads, not when the peer complains -- but the stream id still
// has to be one the peer could legitimately name.
static bool handleBlocked(_Inout_ QuicStreams* ss, _In_ const QuicFrame* f)
{
    if (f->type != QUIC_FRAME_STREAM_DATA_BLOCKED)
        return true;

    if (!canRecv(ss, f->streamDataBlocked.id)) {
        streamsFail(ss, QUIC_ERR_STREAM_STATE_ERROR, f->type);
        return false;
    }

    bool ignore;
    return streamForFrame(ss, f->streamDataBlocked.id, f->type, &ignore) != NULL || ignore;
}

_Use_decl_annotations_
bool _quicStreamsFrame(QuicStreams* ss, const QuicFrame* f)
{
    if ((f->type & ~(uint64)0x07) == QUIC_FRAME_STREAM)
        return handleStreamData(ss, f);

    switch (f->type) {
    case QUIC_FRAME_RESET_STREAM:
        return handleResetStream(ss, f);

    case QUIC_FRAME_STOP_SENDING:
        return handleStopSending(ss, f);

    case QUIC_FRAME_MAX_DATA:
        connMaxData(ss, f->maxData.max);
        return true;

    case QUIC_FRAME_MAX_STREAM_DATA:
        return handleMaxStreamData(ss, f);

    case QUIC_FRAME_MAX_STREAMS_BIDI:
    case QUIC_FRAME_MAX_STREAMS_UNI:
        return handleMaxStreams(ss, f);

    case QUIC_FRAME_DATA_BLOCKED:
    case QUIC_FRAME_STREAM_DATA_BLOCKED:
    case QUIC_FRAME_STREAMS_BLOCKED_BIDI:
    case QUIC_FRAME_STREAMS_BLOCKED_UNI:
        return handleBlocked(ss, f);

    default:
        streamsFail(ss, QUIC_ERR_PROTOCOL_VIOLATION, f->type);
        return false;
    }
}

// ---------------------------------------------------------------------------------------------
// Frames going out
// ---------------------------------------------------------------------------------------------

static bool putFrame(_Inout_ QuicWr* wr, _In_ const QuicFrame* f)
{
    size_t need = _quicFrameSize(f);
    if (need == 0 || need > _quicWrLeft(wr))
        return false;

    return _quicFrameEncode(wr, f);
}

// Writes a frame that carries one number and is owed until the peer acknowledges it.
static void putCtl(_Inout_ QuicWr* wr, _Inout_ QuicCtl* ctl, _In_ const QuicFrame* f, uint64 pn,
                   _Inout_ bool* eliciting)
{
    if (ctl->state != QUIC_CTL_PENDING || !putFrame(wr, f))
        return;

    _quicCtlSent(ctl, pn);
    *eliciting = true;
}

// The connection-wide frames: how much this endpoint will accept, how many streams it will
// accept, and the two complaints that say it has run out of room itself.
static void fillConn(_Inout_ QuicStreams* ss, _Inout_ QuicWr* wr, uint64 pn,
                     _Inout_ bool* eliciting)
{
    QuicFrame f;

    memset(&f, 0, sizeof(f));
    f.type        = QUIC_FRAME_MAX_DATA;
    f.maxData.max = ss->recvMax;
    putCtl(wr, &ss->maxData, &f, pn, eliciting);

    memset(&f, 0, sizeof(f));
    f.type            = QUIC_FRAME_DATA_BLOCKED;
    f.dataBlocked.limit = ss->sendMax;
    putCtl(wr, &ss->dataBlocked, &f, pn, eliciting);

    for (int dir = 0; dir < QUIC_SDIR_COUNT; dir++) {
        memset(&f, 0, sizeof(f));
        f.type = dir == QUIC_SDIR_UNI ? QUIC_FRAME_MAX_STREAMS_UNI : QUIC_FRAME_MAX_STREAMS_BIDI;
        f.maxStreams.max = ss->recvMaxStreams[dir];
        putCtl(wr, &ss->maxStreams[dir], &f, pn, eliciting);

        memset(&f, 0, sizeof(f));
        f.type = dir == QUIC_SDIR_UNI ? QUIC_FRAME_STREAMS_BLOCKED_UNI
                                      : QUIC_FRAME_STREAMS_BLOCKED_BIDI;
        f.streamsBlocked.limit = ss->sendMaxStreams[dir];
        putCtl(wr, &ss->streamsBlocked[dir], &f, pn, eliciting);
    }
}

// The room a STREAM frame needs beyond the bytes it carries. The offset varint is measured
// against the end of the stream, which never underestimates it, so the budget can be worked out
// before a range has been picked.
static size_t streamOverhead(_In_ const QuicStreamState* st)
{
    return 1 + _quicVarintSize(st->id) + _quicVarintSize(st->out.end) + 2;
}

static bool putStreamFrame(_Inout_ QuicWr* wr, _In_ const QuicStreamState* st, uint64 off,
                           _In_reads_opt_(len) const uint8* data, size_t len, bool fin)
{
    QuicFrame f;
    memset(&f, 0, sizeof(f));

    f.type = QUIC_FRAME_STREAM | QUIC_STREAM_LEN;
    if (off > 0)
        f.type |= QUIC_STREAM_OFF;
    if (fin)
        f.type |= QUIC_STREAM_FIN;

    f.stream.id     = st->id;
    f.stream.offset = off;
    f.stream.len    = len;
    f.stream.data   = data;

    return putFrame(wr, &f);
}

static void fillStreamData(_Inout_ QuicStreamState* st, _Inout_ QuicWr* wr, uint64 pn,
                           _Inout_ bool* eliciting)
{
    if (st->sendState != QUIC_SEND_SEND && st->sendState != QUIC_SEND_DATA_SENT)
        return;

    for (;;) {
        size_t overhead = streamOverhead(st);
        size_t left     = _quicWrLeft(wr);
        if (left <= overhead)
            break;

        uint64 off;
        const uint8* data;
        size_t n;
        if (!_quicSendBufNext(&st->out, left - overhead, &off, &data, &n))
            break;

        // The end of the stream rides along with the bytes that reach it, when those are the ones
        // going out.
        bool fin = st->fin.state == QUIC_CTL_PENDING && off + n == st->out.end;

        if (!putStreamFrame(wr, st, off, data, n, fin))
            break;

        _quicSendBufSent(&st->out, off, n, pn);
        if (fin)
            _quicCtlSent(&st->fin, pn);

        *eliciting = true;
    }

    // Nothing left to attach it to, so the end of the stream goes out on its own.
    if (st->fin.state == QUIC_CTL_PENDING && !_quicSendBufPending(&st->out) &&
        putStreamFrame(wr, st, st->out.end, NULL, 0, true)) {
        _quicCtlSent(&st->fin, pn);
        *eliciting = true;
    }
}

static void fillStream(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st, _Inout_ QuicWr* wr,
                       uint64 pn, _Inout_ bool* eliciting)
{
    QuicFrame f;

    memset(&f, 0, sizeof(f));
    f.type              = QUIC_FRAME_STOP_SENDING;
    f.stopSending.id    = st->id;
    f.stopSending.error = st->stopError;
    putCtl(wr, &st->stopSending, &f, pn, eliciting);

    memset(&f, 0, sizeof(f));
    f.type                   = QUIC_FRAME_MAX_STREAM_DATA;
    f.maxStreamData.id       = st->id;
    f.maxStreamData.max      = st->in.limit;
    putCtl(wr, &st->maxData, &f, pn, eliciting);

    if (st->reset.state != QUIC_CTL_IDLE) {
        memset(&f, 0, sizeof(f));
        f.type                    = QUIC_FRAME_RESET_STREAM;
        f.resetStream.id          = st->id;
        f.resetStream.error       = st->resetError;
        f.resetStream.finalSize   = st->sendFinal;
        putCtl(wr, &st->reset, &f, pn, eliciting);
    }

    memset(&f, 0, sizeof(f));
    f.type                      = QUIC_FRAME_STREAM_DATA_BLOCKED;
    f.streamDataBlocked.id      = st->id;
    f.streamDataBlocked.limit   = st->sendMax;
    putCtl(wr, &st->blocked, &f, pn, eliciting);

    fillStreamData(st, wr, pn, eliciting);
}

_Use_decl_annotations_
size_t _quicStreamsFill(QuicStreams* ss, uint8* buf, size_t bufsz, uint64 pn, bool* eliciting)
{
    QuicWr wr;
    _quicWrInit(&wr, buf, bufsz);
    *eliciting = false;

    fillConn(ss, &wr, pn, eliciting);

    // Each stream is taken from the front of the list and put back on the end, so a stream with
    // more to send than one packet holds does not keep the others from ever being reached.
    uint32 visits = saSize(ss->active);
    for (uint32 i = 0; i < visits && saSize(ss->active) > 0 &&
                       _quicWrLeft(&wr) >= QUIC_STREAM_MIN_FRAME;
         i++) {
        uint64 id = ss->active.a[0];
        saRemove(&ss->active, 0);

        QuicStreamState* st = _quicStreamFind(ss, id);
        if (!st)
            continue;

        fillStream(ss, st, &wr, pn, eliciting);

        if (streamPending(st) || streamOutstanding(st)) {
            saPush(&ss->active, uint64, id);
        } else {
            st->active = false;
            streamSettle(ss, st);
        }
    }

    return _quicWrLen(&wr);
}

_Use_decl_annotations_
bool _quicStreamsWantsToSend(const QuicStreams* ss)
{
    if (ss->maxData.state == QUIC_CTL_PENDING || ss->dataBlocked.state == QUIC_CTL_PENDING)
        return true;

    for (int dir = 0; dir < QUIC_SDIR_COUNT; dir++) {
        if (ss->maxStreams[dir].state == QUIC_CTL_PENDING ||
            ss->streamsBlocked[dir].state == QUIC_CTL_PENDING)
            return true;
    }

    for (int32 i = 0; i < saSize(ss->active); i++) {
        const QuicStreamState* st = _quicStreamFind(ss, ss->active.a[i]);
        if (st && streamPending(st))
            return true;
    }

    return false;
}

// ---------------------------------------------------------------------------------------------
// What came back
// ---------------------------------------------------------------------------------------------

// Walks the streams that have something in flight. A packet number is all this gets, the same as
// everywhere else in cxquic: whatever was in the packet finds itself.
static void streamsResolve(_Inout_ QuicStreams* ss, uint64 pn, bool acked)
{
    for (int32 i = 0; i < saSize(ss->active);) {
        QuicStreamState* st = _quicStreamFind(ss, ss->active.a[i]);
        if (!st) {
            saRemove(&ss->active, i);
            continue;
        }

        // Acknowledging stream data releases the buffer holding it, and that buffer is one of the
        // three things sendRoom() is bounded by. A stream that filled it is waiting to be told
        // there is room again, and no flow control limit is going to rise to say so.
        if (acked && sendRoom(ss, st) == 0)
            sendWait(ss, st, 1, 0);

        if (acked) {
            _quicSendBufAcked(&st->out, pn);
            _quicCtlAcked(&st->stopSending, pn);
            _quicCtlAcked(&st->maxData, pn);
            _quicCtlAcked(&st->blocked, pn);

            if (_quicCtlAcked(&st->fin, pn))
                st->finAcked = true;
            if (_quicCtlAcked(&st->reset, pn))
                st->sendState = QUIC_SEND_RESET_DONE;

            if (st->sendState == QUIC_SEND_DATA_SENT && st->finAcked &&
                _quicSendBufOutstanding(&st->out) == 0)
                st->sendState = QUIC_SEND_DATA_DONE;

            sendUnblock(ss, st);
        } else {
            _quicSendBufLost(&st->out, pn);
            _quicCtlLost(&st->stopSending, pn);
            _quicCtlLost(&st->fin, pn);
            _quicCtlLost(&st->reset, pn);
            _quicCtlLost(&st->maxData, pn);
            _quicCtlLost(&st->blocked, pn);
        }

        if (!streamPending(st) && !streamOutstanding(st)) {
            saRemove(&ss->active, i);
            st->active = false;
            streamSettle(ss, st);
            continue;
        }

        i++;
    }
}

_Use_decl_annotations_
void _quicStreamsAcked(QuicStreams* ss, uint64 pn)
{
    _quicCtlAcked(&ss->maxData, pn);
    _quicCtlAcked(&ss->dataBlocked, pn);
    for (int dir = 0; dir < QUIC_SDIR_COUNT; dir++) {
        _quicCtlAcked(&ss->maxStreams[dir], pn);
        _quicCtlAcked(&ss->streamsBlocked[dir], pn);
    }

    streamsResolve(ss, pn, true);
}

_Use_decl_annotations_
void _quicStreamsLost(QuicStreams* ss, uint64 pn)
{
    _quicCtlLost(&ss->maxData, pn);
    _quicCtlLost(&ss->dataBlocked, pn);

    for (int dir = 0; dir < QUIC_SDIR_COUNT; dir++) {
        _quicCtlLost(&ss->maxStreams[dir], pn);
        _quicCtlLost(&ss->streamsBlocked[dir], pn);
    }

    streamsResolve(ss, pn, false);
}

// ---------------------------------------------------------------------------------------------
// What the application calls
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
bool _quicStreamOpen(QuicStreams* ss, bool uni, uint64* id)
{
    int dir = uni ? QUIC_SDIR_UNI : QUIC_SDIR_BIDI;

    if (ss->nextLocal[dir] >= ss->sendMaxStreams[dir]) {
        // Telling the peer costs one frame and is the only way it learns to raise the limit, but
        // it is only worth saying once for each limit it has set.
        if (ss->streamsBlockedAt[dir] != ss->sendMaxStreams[dir]) {
            ss->streamsBlockedAt[dir] = ss->sendMaxStreams[dir];
            _quicCtlQueue(&ss->streamsBlocked[dir]);
        }
        return false;
    }

    uint64 sid = _quicStreamMakeId(ss->nextLocal[dir], ss->server, uni);

    QuicStreamState* st = streamCreate(ss, sid);
    if (!st)
        return false;

    ss->nextLocal[dir]++;
    *id = sid;
    return true;
}

_Use_decl_annotations_
size_t _quicStreamWritable(const QuicStreams* ss, uint64 id)
{
    const QuicStreamState* st = _quicStreamFind(ss, id);
    if (!st)
        return 0;

    // The buffer cap bounds this well below anything a size_t cannot hold.
    return (size_t)sendRoom(ss, st);
}

// A write that could not be taken whole. Two things follow from that, and they are deliberately
// tested separately.
//
// The application is waiting on room, whatever it ran out of -- including the local send buffer,
// which is not the peer's business at all.
//
// The peer is told only when one of its own limits is what stopped the write, which is what
// RFC 9000 section 4.1 asks for. A write refused with credit to spare -- too large for the room
// left, but not at the limit -- is not blocked in that sense and must not claim to be, or the peer
// is asked to raise a limit that was never reached. Either complaint is worth making once per
// limit rather than on every refused write.
static bool sendRefused(_Inout_ QuicStreams* ss, _Inout_ QuicStreamState* st, uint64 want)
{
    // The room left is read after the write rather than before it, since a partial write spends
    // what it was given and leaves none.
    sendWait(ss, st, want, sendRoom(ss, st));

    bool blocked = false;

    if (st->sendMax <= st->out.end && st->sendBlocked != st->sendMax) {
        st->sendBlocked = st->sendMax;
        _quicCtlQueue(&st->blocked);
        streamArm(ss, st);
        blocked = true;
    }

    if (ss->sendMax <= ss->sendOff && ss->sendBlocked != ss->sendMax) {
        ss->sendBlocked = ss->sendMax;
        _quicCtlQueue(&ss->dataBlocked);
        blocked = true;
    }

    return blocked;
}

_Use_decl_annotations_
size_t _quicStreamSend(QuicStreams* ss, uint64 id, const uint8* data, size_t len)
{
    QuicStreamState* st = _quicStreamFind(ss, id);
    if (!st || (st->sendState != QUIC_SEND_READY && st->sendState != QUIC_SEND_SEND))
        return 0;

    size_t want = len;
    uint64 room = sendRoom(ss, st);
    if (len > room)
        len = (size_t)room;

    if (len > 0) {
        if (!_quicSendBufAdd(&st->out, data, len))
            return 0;

        ss->sendOff += len;
        st->sendState = QUIC_SEND_SEND;
        streamArm(ss, st);
    }

    if (want > len)
        sendRefused(ss, st, want);

    return len;
}

_Use_decl_annotations_
bool _quicStreamSendAll(QuicStreams* ss, uint64 id, const uint8* data, size_t len, bool* blocked)
{
    if (blocked)
        *blocked = false;

    QuicStreamState* st = _quicStreamFind(ss, id);
    if (!st || (st->sendState != QUIC_SEND_READY && st->sendState != QUIC_SEND_SEND))
        return false;

    if (sendRoom(ss, st) < len) {
        bool b = sendRefused(ss, st, len);
        if (blocked)
            *blocked = b;
        return false;
    }

    return _quicStreamSend(ss, id, data, len) == len;
}

_Use_decl_annotations_
void _quicStreamFinish(QuicStreams* ss, uint64 id)
{
    QuicStreamState* st = _quicStreamFind(ss, id);
    if (!st || (st->sendState != QUIC_SEND_READY && st->sendState != QUIC_SEND_SEND))
        return;

    st->sendState = QUIC_SEND_DATA_SENT;

    _quicCtlQueue(&st->fin);
    streamArm(ss, st);
}

_Use_decl_annotations_
size_t _quicStreamReadable(const QuicStreams* ss, uint64 id)
{
    const QuicStreamState* st = _quicStreamFind(ss, id);
    return st ? _quicReasmReadable(&st->in) : 0;
}

_Use_decl_annotations_
size_t _quicStreamRecv(QuicStreams* ss, uint64 id, uint8* out, size_t outsz, bool* fin)
{
    *fin = false;

    QuicStreamState* st = _quicStreamFind(ss, id);
    if (!st)
        return 0;

    // A reset ends the receiving half wherever it stood. Whatever had arrived is gone, and the
    // reason came through the reset handler.
    if (st->recvState == QUIC_RECV_RESET) {
        st->recvState = QUIC_RECV_RESET_READ;
        *fin          = true;
        streamSettle(ss, st);
        return 0;
    }

    size_t n = _quicReasmReadable(&st->in);
    if (n > outsz)
        n = outsz;

    if (n > 0) {
        memcpy(out, _quicReasmData(&st->in), n);
        _quicReasmConsume(&st->in, n);

        st->recvRead += n;
        ss->recvRead += n;

        recvWindowUpdate(ss, st);
        connWindowUpdate(ss);
    }

    if (st->recvState == QUIC_RECV_DATA_RECVD && _quicReasmReadable(&st->in) == 0) {
        st->recvState = QUIC_RECV_DATA_READ;
        *fin          = true;
        streamSettle(ss, st);
    }

    return n;
}

_Use_decl_annotations_
void _quicStreamReset(QuicStreams* ss, uint64 id, uint64 error)
{
    QuicStreamState* st = _quicStreamFind(ss, id);
    if (st)
        sendReset(ss, st, error);
}

_Use_decl_annotations_
void _quicStreamStopSending(QuicStreams* ss, uint64 id, uint64 error)
{
    QuicStreamState* st = _quicStreamFind(ss, id);
    if (!st || st->recvState != QUIC_RECV_RECV)
        return;

    st->stopError = error;
    _quicCtlQueue(&st->stopSending);
    streamArm(ss, st);
}

// ---------------------------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------------------------

#define DBGLINE(...)                       \
    do {                                   \
        string _l = 0;                     \
        strFormat(&_l, __VA_ARGS__);       \
        strAppend(out, _l);                \
        strDestroy(&_l);                   \
    } while (0)

_Use_decl_annotations_
void _quicStreamsDebug(QuicStreams* ss, strhandle out)
{
    // What connWindowUpdate() is waiting for, so a window that is not moving says how far off it
    // is rather than only that it has not moved.
    uint64 want    = ss->recvRead + ss->recvWindow;
    int64 toUpdate = ss->recvWindow == 0 ? -1
                                         : (int64)(ss->recvWindow / 2) - (int64)(want - ss->recvMax);

    DBGLINE(_SLL("fc: sendMax=${uint} sendOff=${uint} sendCredit=${int} maxDataCtl=${uint} "
                "dataBlockedCtl=${uint}\n"),
            stvar(uint64, ss->sendMax), stvar(uint64, ss->sendOff),
            stvar(int64, (int64)ss->sendMax - (int64)ss->sendOff),
            stvar(uint32, (uint32)ss->maxData.state), stvar(uint32, (uint32)ss->dataBlocked.state));

    DBGLINE(_SLL("fc: recvMax=${uint} recvOff=${uint} recvRead=${uint} window=${uint} "
                "bytesTillUpdate=${int}\n"),
            stvar(uint64, ss->recvMax), stvar(uint64, ss->recvOff), stvar(uint64, ss->recvRead),
            stvar(uint64, ss->recvWindow), stvar(int64, toUpdate));

    DBGLINE(_SL("streams: live=${uint} active=${uint}\n"), stvar(uint32, htSize(ss->byId)),
            stvar(uint32, saSize(ss->active)));

    foreach (hashtable, hti, ss->byId) {
        const QuicStreamState* st = (const QuicStreamState*)htiVal(ptr, hti);
        if (!st)
            continue;

        DBGLINE(_SLL("stream ${uint}: send=${uint} outBase=${uint} outEnd=${uint} pending=${uint} "
                    "outstanding=${uint} nchunks=${uint} sendMax=${uint} fin=${uint} "
                    "reset=${uint} blockedCtl=${uint} active=${uint}\n"),
                stvar(uint64, st->id), stvar(uint32, st->sendState), stvar(uint64, st->out.base),
                stvar(uint64, st->out.end),
                stvar(uint32, _quicSendBufPending(&st->out) ? 1u : 0u),
                stvar(uint64, _quicSendBufOutstanding(&st->out)), stvar(uint32, st->out.nchunks),
                stvar(uint64, st->sendMax), stvar(uint32, (uint32)st->fin.state),
                stvar(uint32, (uint32)st->reset.state), stvar(uint32, (uint32)st->blocked.state),
                stvar(uint32, st->active ? 1u : 0u));

        // nranges above one means the bytes past the first gap cannot be read no matter how many
        // more arrive, which is what tells a stalled reader apart from a stalled sender.
        DBGLINE(_SLL("stream ${uint}: recv=${uint} inBase=${uint} inLimit=${uint} highest=${uint} "
                    "read=${uint} readable=${uint} nranges=${uint} firstGapAt=${int} "
                    "maxDataCtl=${uint} stopCtl=${uint}\n"),
                stvar(uint64, st->id), stvar(uint32, st->recvState), stvar(uint64, st->in.base),
                stvar(uint64, st->in.limit), stvar(uint64, st->recvHighest),
                stvar(uint64, st->recvRead), stvar(uint64, (uint64)_quicReasmReadable(&st->in)),
                stvar(uint32, st->in.nranges),
                stvar(int64, st->in.nranges > 1 ? (int64)st->in.ranges[0].end : -1),
                stvar(uint32, (uint32)st->maxData.state),
                stvar(uint32, (uint32)st->stopSending.state));
    }
}

#undef DBGLINE
