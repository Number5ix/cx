// ---------------------------------------------------------------------------------------------
// Timers
//
// One binary min-heap per queue, keyed on absolute deadline, plus an id -> heap-index hashtable so
// a cancel is O(log n) rather than a scan. Every entry belongs to a flow, which is what gives a
// timer both its ordering domain and its lifetime:
//
//   ordering   an application timer is delivered as NET_Timer through the flow's inbox, so it
//              runs on a worker behind everything already queued for that flow
//   lifetime   the entry holds a strong reference to its flow, and the flow's terminal path drops
//              every timer it still had armed
//
// Two things arm timers. Applications call netflowAddTimer() and get NET_Timer. The framework
// itself (currently only the connect state machine) passes a NetTimerFn, which is fired inline on
// whichever thread ran the sweep -- the connect timeout must stay on the ingest thread, because
// advancing to the next address closes and replaces the socket's OS handle, and doing that from a
// worker while the ingest thread sits in select() on the old descriptor is a use-after-close.
//
// Backends drive all of this with two calls around their wait: _nextDeadline() to bound the sleep
// and _timerSweep() to fire whatever came due.
// ---------------------------------------------------------------------------------------------

#include "net_private.h"
#include <cx/container.h>
#include <cx/time/clock.h>

// Ceiling on how many timers one sweep fires. A sweep that hits this leaves the rest due, so
// _nextDeadline() comes back in the past and the backend's next wait is the 1ms floor -- the
// remainder runs almost immediately rather than waiting out an unrelated timeout. The bound
// matters because firing runs application callbacks, and a backend's ingest thread should not be
// held for an unbounded number of them in one pass.
#define NET_TIMER_SWEEP_MAX 64

// ---------------------------------------------------------------------------------------------
// Heap
//
// All of this runs under timerLock. The index hashtable is updated by exactly one function
// (heapPlace), so there is no path that moves an entry without moving its index entry with it.
// ---------------------------------------------------------------------------------------------

// Write an entry into a slot and point the index at it. Every heap movement goes through here.
static void heapPlace(NetQueue* q, uint32 slot, const NetTimerEntry* e)
{
    q->timers[slot] = *e;
    htInsert(&q->timerIdx, uint64, e->id, uint32, slot);
}

// Both sift helpers return where the entry ended up, so a caller that has to try both directions
// does not have to look the id back up in between.
static uint32 heapSiftUp(NetQueue* q, uint32 slot)
{
    NetTimerEntry e = q->timers[slot];
    while (slot > 0) {
        uint32 parent = (slot - 1) / 2;
        if (q->timers[parent].deadline <= e.deadline)
            break;
        heapPlace(q, slot, &q->timers[parent]);
        slot = parent;
    }
    heapPlace(q, slot, &e);
    return slot;
}

static uint32 heapSiftDown(NetQueue* q, uint32 slot)
{
    NetTimerEntry e = q->timers[slot];
    for (;;) {
        uint32 kid = slot * 2 + 1;
        if (kid >= q->ntimers)
            break;
        if (kid + 1 < q->ntimers && q->timers[kid + 1].deadline < q->timers[kid].deadline)
            kid++;
        if (e.deadline <= q->timers[kid].deadline)
            break;
        heapPlace(q, slot, &q->timers[kid]);
        slot = kid;
    }
    heapPlace(q, slot, &e);
    return slot;
}

// Move an entry that is already in the heap but whose deadline just changed. It can be either
// lighter or heavier than the hole it sits in, so both directions are tried; exactly one moves it.
static void heapReposition(NetQueue* q, uint32 slot)
{
    heapSiftDown(q, heapSiftUp(q, slot));
}

// Remove the entry at `slot`, moving the last entry into the hole and restoring the invariant. The
// caller takes over the removed entry's flow reference.
static NetTimerEntry heapRemove(NetQueue* q, uint32 slot)
{
    NetTimerEntry out = q->timers[slot];
    htRemove(&q->timerIdx, uint64, out.id);

    q->ntimers--;
    if (slot < q->ntimers) {
        heapPlace(q, slot, &q->timers[q->ntimers]);
        heapReposition(q, slot);
    }

    atomicStore(uint32, &q->timersLive, q->ntimers, Relaxed);
    return out;
}

// Look up an armed timer's slot. Returns false for an id that was cancelled or has already been
// popped for delivery.
_Success_(return) static bool heapFind(NetQueue* q, NetTimerId id, _Out_ uint32* slot)
{
    if (id == 0)
        return false;
    return htFind(q->timerIdx, uint64, id, uint32, slot);
}

static void heapPush(NetQueue* q, const NetTimerEntry* e)
{
    if (q->ntimers == q->timersCap) {
        uint32 cap = q->timersCap ? q->timersCap * 2 : 16;
        xaResize(&q->timers, cap * sizeof(NetTimerEntry));
        q->timersCap = cap;
    }

    uint32 slot = q->ntimers++;
    heapPlace(q, slot, e);
    heapSiftUp(q, slot);

    atomicStore(uint32, &q->timersLive, q->ntimers, Relaxed);
}

// ---------------------------------------------------------------------------------------------
// Arming and cancelling
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
NetTimerId NetQueue__addTimer(NetQueue* self, NetFlow* flow, int64 delay, flags_t flags,
                              NetTimerFn fn, void* ctx)
{
    if (!flow)
        return 0;

    // A dying flow will never dispatch another event, and its terminal path has already dropped
    // whatever it had armed -- arming now would strand an entry holding the last reference to it.
    if (atomicLoad(uint32, &flow->dying, Acquire) != 0)
        return 0;

    if (delay < 0)
        delay = 0;

    NetTimerEntry e = {
        .deadline = clockTimer() + delay,
        .interval = delay,
        .flow     = objAcquire(flow),
        .fn       = fn,
        .ctx      = ctx,
        .flags    = flags,
    };

    bool nearest = false;
    withMutex (&self->timerLock) {
        e.id = ++self->timerSerial;

        // Whether this becomes the new soonest deadline decides if a parked backend has to be
        // woken. Read before the push, since after it the new entry may itself be the root.
        nearest = self->ntimers == 0 || e.deadline < self->timers[0].deadline;

        heapPush(self, &e);
        saPush(&flow->timers, uint64, e.id);
    }

    // The backend is asleep on a bound computed before this timer existed, so without a nudge the
    // timer would not fire until whatever it was already waiting for woke it.
    if (nearest && self->wake)
        self->wake(self->wakeCtx);

    return e.id;
}

// Drop `id` from its flow's reverse index. Caller holds timerLock.
static void forgetFlowTimer(NetFlow* flow, NetTimerId id)
{
    for (int32 i = 0; i < saSize(flow->timers); i++) {
        if (flow->timers.a[i] == id) {
            saRemove(&flow->timers, i);
            return;
        }
    }
}

_Use_decl_annotations_
bool NetQueue__cancelTimer(NetQueue* self, NetTimerId id)
{
    // NULL unless this call is the one that took the timer out of the heap, in which case it is the
    // entry's flow reference, now ours to give back.
    NetFlow* flow = NULL;

    withMutex (&self->timerLock) {
        uint32 slot;
        // A miss means unknown, already cancelled, or already popped for delivery -- all of which
        // answer false, which is what makes a true return usable as a claim token.
        if (heapFind(self, id, &slot)) {
            NetTimerEntry e = heapRemove(self, slot);
            forgetFlowTimer(e.flow, id);
            flow = e.flow;
        }
    }

    // Outside the lock: releasing the last reference to a flow runs its destructor, which must not
    // happen with timerLock held.
    bool ret = flow != NULL;
    objRelease(&flow);
    return ret;
}

_Use_decl_annotations_
bool NetQueue__rearmTimer(NetQueue* self, NetTimerId id, int64 delay)
{
    if (delay < 0)
        delay = 0;

    bool ok      = false;
    bool nearest = false;

    withMutex (&self->timerLock) {
        uint32 slot;
        if (heapFind(self, id, &slot)) {
            int64 deadline = clockTimer() + delay;
            nearest        = deadline < self->timers[0].deadline;

            self->timers[slot].deadline = deadline;
            self->timers[slot].interval = delay;
            heapReposition(self, slot);

            ok = true;
        }
    }

    if (ok && nearest && self->wake)
        self->wake(self->wakeCtx);

    return ok;
}

_Use_decl_annotations_
void NetQueue__cancelFlowTimers(NetQueue* self, NetFlow* flow)
{
    if (!flow || saSize(flow->timers) == 0)
        return;

    // Every entry here belongs to `flow`, so count them under the lock and give the references back
    // afterwards. Releasing under timerLock would risk running ~NetFlow with it held.
    int32 dropped = 0;

    withMutex (&self->timerLock) {
        for (int32 i = 0; i < saSize(flow->timers); i++) {
            uint32 slot;
            if (!heapFind(self, flow->timers.a[i], &slot))
                continue;   // already popped for delivery on a sweep that has not fired it yet
            heapRemove(self, slot);
            dropped++;
        }
        saClear(&flow->timers);
    }

    for (int32 i = 0; i < dropped; i++) {
        NetFlow* ref = flow;
        objRelease(&ref);
    }
}

// ---------------------------------------------------------------------------------------------
// Sweep
// ---------------------------------------------------------------------------------------------

_Use_decl_annotations_
int64 NetQueue__nextDeadline(NetQueue* self)
{
    if (atomicLoad(uint32, &self->timersLive, Relaxed) == 0)
        return 0;

    int64 dl = 0;
    withMutex (&self->timerLock) {
        if (self->ntimers > 0)
            dl = self->timers[0].deadline;
    }
    return dl;
}

// Deliver one due timer. Runs with no lock held, so a callback is free to arm, cancel, or send.
static void fireTimer(NetQueue* q, const NetTimerEntry* e)
{
    // A flow that started closing between the pop and here will never dispatch another event, and
    // the application has been told (or is about to be told) the connection is gone.
    if (atomicLoad(uint32, &e->flow->dying, Acquire) != 0)
        return;

    if (e->fn) {
        e->fn(e->flow, e->id, e->ctx);
        return;
    }

    NetMessage* msg = netpoolAllocHeader(e->flow->pool);
    msg->kind       = NMSG_Timer;
    msg->timerId    = e->id;
    netqueue_submit(q, e->flow, msg);
}

_Use_decl_annotations_
void NetQueue__timerSweep(NetQueue* self)
{
    if (atomicLoad(uint32, &self->timersLive, Relaxed) == 0)
        return;

    // One `now` for the whole sweep: every due timer is judged against the same instant, so a
    // repeating timer cannot re-arm into the pass that just fired it.
    int64 now = clockTimer();

    for (int fired = 0; fired < NET_TIMER_SWEEP_MAX; fired++) {
        NetTimerEntry e;
        bool got = false;

        withMutex (&self->timerLock) {
            if (self->ntimers > 0 && self->timers[0].deadline <= now) {
                e = self->timers[0];
                if (e.flags & NTF_Repeat) {
                    // Re-arm in place, keeping the id: one cancel stops a repeating timer for
                    // good, including one issued from inside its own callback. The heap keeps its
                    // reference, so firing needs one of its own.
                    //
                    // The next deadline is measured from the one that just fired, not from now, so
                    // a periodic timer keeps its cadence instead of drifting by however long the
                    // sweep took to reach it. If that has already passed -- a long callback, a
                    // stalled worker, or simply an interval shorter than a sweep -- the missed
                    // periods collapse into one rather than firing a burst to catch up. Keeping
                    // the next deadline strictly after `now` is also what stops this loop from
                    // spinning on the timer it just re-armed.
                    int64 next = e.deadline + e.interval;
                    if (next <= now)
                        next = now + 1;

                    self->timers[0].deadline = next;
                    heapSiftDown(self, 0);
                    objAcquire(e.flow);
                } else {
                    heapRemove(self, 0);   // the entry's flow reference transfers to `e`
                    forgetFlowTimer(e.flow, e.id);
                }
                got = true;
            }
        }

        if (!got)
            break;

        fireTimer(self, &e);
        objRelease(&e.flow);
    }
}
