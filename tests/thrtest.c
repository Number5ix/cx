#include <cx/thread.h>
#include <cx/string/strtest.h>
#include <cx/container.h>
#include <cx/thread/condvar.h>
#include <cx/thread/futex.h>
#include <cx/thread/event.h>
#include <cx/thread/mutex.h>
#include <cx/thread/rwlock.h>
#include <cx/thread/sema.h>
#include <cx/platform/os.h>
#include <stddef.h>

#define TEST_FILE thrtest
#define TEST_FUNCS thrtest_funcs
#include "common.h"

int thrtest1[16] = {0};
Thread *thrtest2[16] = {0};

static int thrproc1(Thread *self)
{
    int32 slot, count;

    if (!(stvlNext(&self->args, int32, &slot) &&
          stvlNext(&self->args, int32, &count)))
        return 0;

    thrtest2[slot] = thrCurrent();

    for (int i = 0; i < count; i++) {
        thrtest1[slot]++;
    }

    return 0;
}

#define BASIC_THREADS 16
static int test_basic()
{
    Thread *threads[BASIC_THREADS];
    int ret = 0;
    int i;

    for (i = 0; i < BASIC_THREADS; i++) {
        threads[i] = thrCreate(thrproc1, _S"Basic Test Thread", stvar(int32, i), stvar(int32, 1000000 + i * 100000));
        if (!threads[i])
            TEST_FAIL(1, _SL("thrCreate failed for basic test thread ${int}"), stvar(int32, i));
        thrSetPriorityV(threads[i], i % (THREAD_Realtime + 1));
    }

    for (i = 0; i < BASIC_THREADS; i++) {
        thrWait(threads[i], timeForever);
        thrShutdown(threads[i]);
        thrRelease(&threads[i]);
        if (thrtest1[i] != 1000000 + i * 100000)
            TEST_FAILV(ret, 1, _SL("thrtest1[${int}]=${int} != expected ${int}"), stvar(int32, i), stvar(int32, thrtest1[i]), stvar(int32, 1000000 + i * 100000));
    }

    return ret;
}

static Futex testftx;

static int thrproc2(Thread *self)
{
    bool dec;
    int32 count;

    if (!(stvlNext(&self->args, uint8, &dec) &&
          stvlNext(&self->args, int32, &count)))
        return 0;

    for (int i = 0; i < count; i++) {
        if (dec) {
            int32 val = atomicLoad(int32, &testftx.val, Relaxed);
            while (val == 0 || !atomicCompareExchange(int32, weak, &testftx.val, &val, val - 1, Acquire, Relaxed)) {
                if (val == 0) {
                    futexWait(&testftx, 0, timeForever);
                } else
                    osYield();
                val = atomicLoad(int32, &testftx.val, Relaxed);
            }
        }
        else {
            atomicFetchAdd(int32, &testftx.val, 1, Relaxed);
            futexWake(&testftx);
        }
    }

    return 0;
}

#define FUTEX_PRODUCERS 2
#define FUTEX_CONSUMERS 8
#define FUTEX_COUNT 524288
static int test_futex()
{
    int ret = 0;
    futexInit(&testftx, 0);

    int i;
    Thread *producers[FUTEX_PRODUCERS];
    Thread *consumers[FUTEX_CONSUMERS];
    for (i = 0; i < FUTEX_CONSUMERS; i++) {
        consumers[i] = thrCreate(thrproc2, _S"Futex Consumer", stvar(uint8, 1), stvar(int32, FUTEX_COUNT / FUTEX_CONSUMERS));
        if (!consumers[i])
            TEST_FAIL(1, _SL("thrCreate failed for futex consumer thread ${int}"), stvar(int32, i));
    }
    for (i = 0; i < FUTEX_PRODUCERS; i++) {
        producers[i] = thrCreate(thrproc2, _S"Futex Producer", stvar(uint8, 0), stvar(int32, FUTEX_COUNT / FUTEX_PRODUCERS));
        if (!producers[i])
            TEST_FAIL(1, _SL("thrCreate failed for futex producer thread ${int}"), stvar(int32, i));
    }

    for (i = 0; i < FUTEX_PRODUCERS; i++) {
        thrWait(producers[i], timeForever);
        thrShutdown(producers[i]);
        thrRelease(&producers[i]);
    }
    for (i = 0; i < FUTEX_CONSUMERS; i++) {
        thrWait(consumers[i], timeForever);
        thrShutdown(consumers[i]);
        thrRelease(&consumers[i]);
    }

    if (atomicLoad(int32, &testftx.val, Acquire) != 0)
        TEST_FAILV(ret, 1, _SL("final futex value=${int} != 0"), stvar(int32, atomicLoad(int32, &testftx.val, Acquire)));

    return ret;
}

static Semaphore testsem;

static int thrproc2s(Thread *self)
{
    bool dec;
    int32 count;

    if (!(stvlNext(&self->args, uint8, &dec) &&
          stvlNext(&self->args, int32, &count)))
        return 0;

    for (int i = 0; i < count; i++) {
        if (dec)
            semaDec(&testsem);
        else
            semaInc(&testsem, 1);
    }

    return 0;
}

#define SEMA_PRODUCERS 2
#define SEMA_CONSUMERS 8
#define SEMA_COUNT 524288
static int test_sema()
{
    int ret = 0;
    semaInit(&testsem, 0);

    int i;
    Thread *producers[SEMA_PRODUCERS] = { 0 };
    Thread *consumers[SEMA_CONSUMERS] = { 0 };
    for (i = 0; i < SEMA_CONSUMERS; i++) {
        consumers[i] = thrCreate(thrproc2s, _S"Semaphore Consumer", stvar(uint8, 1), stvar(int32, SEMA_COUNT / SEMA_CONSUMERS));
        if (!consumers[i])
            TEST_FAIL(1, _SL("thrCreate failed for semaphore consumer thread ${int}"), stvar(int32, i));
    }
    for (i = 0; i < SEMA_PRODUCERS; i++) {
        producers[i] = thrCreate(thrproc2s, _S"Semaphore Producer", stvar(uint8, 0), stvar(int32, SEMA_COUNT / SEMA_PRODUCERS));
        if (!producers[i])
            TEST_FAIL(1, _SL("thrCreate failed for semaphore producer thread ${int}"), stvar(int32, i));
    }

    for (i = 0; i < SEMA_PRODUCERS; i++) {
        thrWait(producers[i], timeForever);
        thrShutdown(producers[i]);
        thrRelease(&producers[i]);
    }
    for (i = 0; i < SEMA_CONSUMERS; i++) {
        thrWait(consumers[i], timeForever);
        thrShutdown(consumers[i]);
        thrRelease(&consumers[i]);
    }

    if (atomicLoad(int32, &testsem.ftx.val, Acquire) != 0)
        TEST_FAILV(ret, 1, _SL("final semaphore value=${int} != 0"), stvar(int32, atomicLoad(int32, &testsem.ftx.val, Acquire)));

    if (!semaDestroy(&testsem))
        TEST_FAILV(ret, 1, _SL("semaDestroy failed"), stvNone);

    return ret;
}

static atomic(bool) fail;
static int64 testint1;
static int64 testint2;
static int64 testint3;
static Mutex testmtx;

static int thrproc3(Thread *self)
{
    int32 count;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        mutexAcquire(&testmtx);
        testint1++;
        testint2++;
        testint3++;
        mutexRelease(&testmtx);

        withMutex(&testmtx) {
            if (testint1 != testint2 || testint2 != testint3)
                atomicStore(bool, &fail, true, Release);
        }
    }

    return 0;
}

#define MTX_THREADS 32
#define MTX_COUNT 1048576
static int test_mutex()
{
    atomicStore(bool, &fail, false, Release);
    testint1 = 0;
    testint2 = 0;
    testint3 = 0;
    mutexInit(&testmtx);

    int i;
    Thread *threads[MTX_THREADS];

    for (i = 0; i < MTX_THREADS; i++) {
        threads[i] = thrCreate(thrproc3, _S"Mutex Test", stvar(int32, MTX_COUNT / MTX_THREADS));
        if (!threads[i])
            TEST_FAIL(1, _SL("thrCreate failed for mutex test thread ${int}"), stvar(int32, i));
    }

    for (i = 0; i < MTX_THREADS; i++) {
        thrWait(threads[i], timeForever);
        thrShutdown(threads[i]);
        thrRelease(&threads[i]);
    }

    int ret = 0;
    if (atomicLoad(bool, &fail, Acquire))
        TEST_FAILV(ret, 1, _SL("mutex-protected counters diverged during concurrent access (worker thread observed testint1/testint2/testint3 mismatch)"), stvNone);
    if (testint1 != MTX_COUNT || testint2 != MTX_COUNT || testint3 != MTX_COUNT)
        TEST_FAILV(ret, 1, _SL("final counters exp=${int}: testint1=${int} testint2=${int} testint3=${int}"), stvar(int32, MTX_COUNT), stvar(int64, testint1), stvar(int64, testint2), stvar(int64, testint3));

    mutexDestroy(&testmtx);

    return ret;
}

static RWLock testrw;
static atomic(bool) rthread_exit = atomicInit(false);

static int thrproc4r(Thread *self)
{
    while (!atomicLoad(bool, &rthread_exit, Acquire)) {
        withReadLock(&testrw) {
            for (int i = 0; i < 16; i++) {
                if (testint1 != testint2 || testint2 != testint3)
                    atomicStore(bool, &fail, true, Release);

            }
        }
        osYield();
    }

    return 0;
}

static int thrproc4w(Thread *self)
{
    int32 count;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        rwlockAcquireWrite(&testrw);
        testint1++;
        testint2++;
        testint3++;
        if (i % 16 == 0) {
            // every so often try a downgrade and make sure read consistency is good
            rwlockDowngradeWrite(&testrw);
            for (int j = 0; j < 16; j++) {
                if (testint1 != testint2 || testint2 != testint3)
                    atomicStore(bool, &fail, true, Release);
            }
            rwlockReleaseRead(&testrw);
        } else {
            rwlockReleaseWrite(&testrw);
        }
    }

    return 0;
}

#define RW_WTHREADS 4
#define RW_RTHREADS 16
#define RW_COUNT 32768
static int test_rwlock()
{
    atomicStore(bool, &fail, false, Release);
    testint1 = 0;
    testint2 = 0;
    testint3 = 0;
    rwlockInit(&testrw);

    int i;
    Thread *rthreads[RW_RTHREADS];
    Thread *wthreads[RW_WTHREADS];

    for (i = 0; i < RW_RTHREADS; i++) {
        rthreads[i] = thrCreate(thrproc4r, _S"Reader Thread", stvar(int32, 0));
        if (!rthreads[i])
            TEST_FAIL(1, _SL("thrCreate failed for reader thread ${int}"), stvar(int32, i));
    }
    for (i = 0; i < RW_WTHREADS; i++) {
        wthreads[i] = thrCreate(thrproc4w, _S"Writer Thread", stvar(int32, RW_COUNT / RW_WTHREADS));
        if (!wthreads[i])
            TEST_FAIL(1, _SL("thrCreate failed for writer thread ${int}"), stvar(int32, i));
    }

    for (i = 0; i < RW_WTHREADS; i++) {
        thrWait(wthreads[i], timeForever);
        thrShutdown(wthreads[i]);
        thrRelease(&wthreads[i]);
    }
    atomicStore(bool, &rthread_exit, true, Release);
    for (i = 0; i < RW_RTHREADS; i++) {
        thrWait(rthreads[i], timeForever);
        thrShutdown(rthreads[i]);
        thrRelease(&rthreads[i]);
    }

    rwlockDestroy(&testrw);

    int ret = 0;
    if (atomicLoad(bool, &fail, Acquire))
        TEST_FAILV(ret, 1, _SL("rwlock-protected counters diverged during concurrent access (worker thread observed testint1/testint2/testint3 mismatch)"), stvNone);
    if (testint1 != RW_COUNT || testint2 != RW_COUNT || testint3 != RW_COUNT)
        TEST_FAILV(ret, 1, _SL("final counters exp=${int}: testint1=${int} testint2=${int} testint3=${int}"), stvar(int32, RW_COUNT), stvar(int64, testint1), stvar(int64, testint2), stvar(int64, testint3));

    return ret;
}

static Event testev;

#define EVENT_CONSUMERS 4
#define EVENT_PRODUCERS 4
#define EVENT_COUNT 32768

static int32 evthrcount[EVENT_CONSUMERS];
static atomic(int32) evsignaled;
static atomic(int32) evwork;
static atomic(int32) evdone;

static int thrproc5c(Thread *self)
{
    int thrid;
    if (!stvlNext(&self->args, int32, &thrid))
        return 0;

    int32 work;
    for (;;) {
        eventWait(&testev);

        if (atomicLoad(bool, &rthread_exit, Acquire))
            break;

        int32 sig = atomicLoad(int32, &evsignaled, Relaxed);
        // we shouldn't wake up if the event hasn't been signaled
        if (sig == 0)
            atomicStore(bool, &fail, true, Relaxed);
        else
            atomicFetchSub(int32, &evsignaled, 1, Relaxed);

        work = atomicLoad(int32, &evwork, Relaxed);
        do {
            if (work > 0 && atomicCompareExchange(int32, strong, &evwork, &work, work - 1, Acquire, Acquire)) {
                evthrcount[thrid]++;
                atomicFetchAdd(int32, &evdone, 1, Release);
            }
            work = atomicLoad(int32, &evwork, Relaxed);
        } while (work > 0);
    }

    return 0;
}

static int thrproc5p(Thread *self)
{
    int count;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        atomicFetchAdd(int32, &evwork, 1, Acquire);
        atomicFetchAdd(int32, &evsignaled, 1, Acquire);
        while (!eventSignal(&testev)) {
            osYield();
        }
    }

    return 0;
}

static int test_event_sub(bool spin)
{
    atomicStore(bool, &rthread_exit, false, Release);
    atomicStore(bool, &fail, false, Release);
    atomicStore(int32, &evwork, 0, Release);
    atomicStore(int32, &evdone, 0, Release);
    memset(evthrcount, 0, sizeof(evthrcount));

    if (spin)
        eventInit(&testev, EV_Spin);
    else
        eventInit(&testev);

    int i;
    Thread *cthreads[EVENT_CONSUMERS];
    Thread *pthreads[EVENT_PRODUCERS];

    for (i = 0; i < EVENT_CONSUMERS; i++) {
        cthreads[i] = thrCreate(thrproc5c, _S"Event Consumer", stvar(int32, i));
        if (!cthreads[i])
            TEST_FAIL(1, _SL("thrCreate failed for event consumer thread ${int}"), stvar(int32, i));
    }
    for (i = 0; i < EVENT_PRODUCERS; i++) {
        pthreads[i] = thrCreate(thrproc5p, _S"Event Producer", stvar(int32, EVENT_COUNT / EVENT_PRODUCERS));
        if (!pthreads[i])
            TEST_FAIL(1, _SL("thrCreate failed for event producer thread ${int}"), stvar(int32, i));
    }

    for (i = 0; i < EVENT_PRODUCERS; i++) {
        thrWait(pthreads[i], timeForever);
        thrShutdown(pthreads[i]);
        thrRelease(&pthreads[i]);
    }

    while (atomicLoad(int32, &evwork, Relaxed) > 0) {
        osYield();
    }

    atomicStore(bool, &rthread_exit, true, Release);
    eventSignalLock(&testev);
    for (i = 0; i < EVENT_CONSUMERS; i++) {
        thrWait(cthreads[i], timeForever);
        thrShutdown(cthreads[i]);
        thrRelease(&cthreads[i]);
    }

    eventDestroy(&testev);

    int tcount = 0;
    for (int i = 0; i < EVENT_CONSUMERS; i++) {
        tcount += evthrcount[i];
    }

    if (atomicLoad(bool, &fail, Acquire))
        TEST_FAIL(1, _SL("event consumer observed an unexpected wakeup (evsignaled==0) or other race"), stvNone);

    if (atomicLoad(int32, &evsignaled, Acquire) != 0)
        TEST_FAIL(1, _SL("final evsignaled=${int} != 0"), stvar(int32, atomicLoad(int32, &evsignaled, Acquire)));

    if (atomicLoad(int32, &evdone, Acquire) != EVENT_COUNT)
        TEST_FAIL(1, _SL("final evdone=${int} != expected ${int}"), stvar(int32, atomicLoad(int32, &evdone, Acquire)), stvar(int32, EVENT_COUNT));

    if (tcount != EVENT_COUNT)
        TEST_FAIL(1, _SL("tcount=${int} != expected ${int}"), stvar(int32, tcount), stvar(int32, EVENT_COUNT));

    return 0;
}

static int test_event()
{
    return test_event_sub(false);
}

static int test_event_s()
{
    return test_event_sub(true);
}

static int thrproc6(Thread *self)
{
    // first test should take less than 100ms
    if (futexWait(&testftx, 0, timeFromMsec(100)) != FUTEX_Waited)
        atomicStore(bool, &fail, true, Release);

    atomicFetchSub(int32, &testftx.val, 1, Relaxed);

    // second test should take more than 100ms
    if (futexWait(&testftx, 0, timeFromMsec(100)) != FUTEX_Timeout)
        atomicStore(bool, &fail, true, Release);

    atomicFetchSub(int32, &testftx.val, 1, Relaxed);

    return 0;
}

static int test_timeout()
{
    atomicStore(bool, &fail, false, Release);
    futexInit(&testftx, 0);

    Thread *testthr = thrCreate(thrproc6, _S"Timeout Test", stvNone);
    if (!testthr)
        TEST_FAIL(1, _SL("thrCreate failed for timeout test thread"), stvNone);

    osSleep(timeFromMsec(50));
    atomicFetchAdd(int32, &testftx.val, 1, Relaxed);
    futexWake(&testftx);

    osSleep(timeFromMsec(150));
    atomicFetchAdd(int32, &testftx.val, 1, Relaxed);
    futexWake(&testftx);

    thrWait(testthr, timeForever);
    thrShutdown(testthr);
    thrRelease(&testthr);

    int ret = 0;
    if (atomicLoad(bool, &fail, Acquire))
        TEST_FAILV(ret, 1, _SL("futexWait timing check failed (expected wait or expected timeout did not occur as scheduled)"), stvNone);
    return ret;
}

#define CV_CONSUMERS 4
#define CV_PRODUCERS 4
#define CV_COUNT 32768

static Mutex cvmtx;
static CondVar dataneeded;
static CondVar dataready;
static int cvready;
static int cvin;
static int cvout;
static int cvproduced[CV_PRODUCERS];
static int cvconsumed[CV_CONSUMERS];

static int thrproc7p(Thread *self)
{
    int slot, count;
    if (!stvlNext(&self->args, int32, &slot))
        return 0;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        mutexAcquire(&cvmtx);
        while (cvready == 1)
            cvarWait(&dataneeded, &cvmtx);
        cvin++;
        cvproduced[slot]++;
        cvready = 1;
        cvarSignal(&dataready);
        mutexRelease(&cvmtx);
    }

    return 0;
}

static int thrproc7c(Thread *self)
{
    int slot, count;
    if (!stvlNext(&self->args, int32, &slot))
        return 0;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++) {
        mutexAcquire(&cvmtx);
        while (cvready == 0)
            cvarWait(&dataready, &cvmtx);
        cvin--;
        cvout++;
        cvconsumed[slot]++;
        cvready = 0;
        cvarSignal(&dataneeded);
        mutexRelease(&cvmtx);
    }

    return 0;
}

static int test_condvar()
{
    mutexInit(&cvmtx);
    cvarInit(&dataneeded);
    cvarInit(&dataready);

    int i;
    Thread *cthreads[CV_CONSUMERS];
    Thread *pthreads[CV_PRODUCERS];

    for (i = 0; i < CV_CONSUMERS; i++) {
        cthreads[i] = thrCreate(thrproc7c, _S"Condition Variable Consumer", stvar(int32, i), stvar(int32, CV_COUNT / CV_CONSUMERS));
        if (!cthreads[i])
            TEST_FAIL(1, _SL("thrCreate failed for condvar consumer thread ${int}"), stvar(int32, i));
    }
    for (i = 0; i < CV_PRODUCERS; i++) {
        pthreads[i] = thrCreate(thrproc7p, _S"Condition Variable Producer", stvar(int32, i), stvar(int32, CV_COUNT / CV_PRODUCERS));
        if (!pthreads[i])
            TEST_FAIL(1, _SL("thrCreate failed for condvar producer thread ${int}"), stvar(int32, i));
    }

    for (i = 0; i < CV_PRODUCERS; i++) {
        thrWait(pthreads[i], timeForever);
        thrShutdown(pthreads[i]);
        thrRelease(&pthreads[i]);
    }

    for (i = 0; i < CV_CONSUMERS; i++) {
        thrWait(cthreads[i], timeForever);
        thrShutdown(cthreads[i]);
        thrRelease(&cthreads[i]);
    }

    cvarDestroy(&dataneeded);
    cvarDestroy(&dataready);
    mutexDestroy(&cvmtx);

    if (cvin != 0 || cvout != CV_COUNT || cvready != 0)
        TEST_FAIL(1, _SL("cvin=${int} cvout=${int} cvready=${int} (expected cvin=0, cvout=${int}, cvready=0)"), stvar(int32, cvin), stvar(int32, cvout), stvar(int32, cvready), stvar(int32, CV_COUNT));

    int tcount = 0;
    for (int i = 0; i < CV_CONSUMERS; i++) {
        tcount += cvconsumed[i];
    }
    if (tcount != CV_COUNT)
        TEST_FAIL(1, _SL("consumed tcount=${int} != expected ${int}"), stvar(int32, tcount), stvar(int32, CV_COUNT));

    tcount = 0;
    for (int i = 0; i < CV_PRODUCERS; i++) {
        tcount += cvproduced[i];
    }
    if (tcount != CV_COUNT)
        TEST_FAIL(1, _SL("produced tcount=${int} != expected ${int}"), stvar(int32, tcount), stvar(int32, CV_COUNT));

    return 0;
}

// --- int64/uint64 atomics ---------------------------------------------------------------
//
// The interesting target for these is 32-bit x86: MSVC has no native Interlocked
// intrinsic for most 64-bit ops there (cx/platform/msvc/msvc_atomic.h falls back to a
// compare-exchange retry loop), and its plain 8-byte load/store used to tear across the
// two 32-bit halves before this test existed.

typedef struct {
    char pad;
    atomic(int64) a;
} AtomicI64AlignCheck;

_Static_assert(sizeof(atomic(int64)) == 8, "atomic(int64) must be exactly 8 bytes");
_Static_assert(sizeof(atomic(uint64)) == 8, "atomic(uint64) must be exactly 8 bytes");
_Static_assert(offsetof(AtomicI64AlignCheck, a) == 8,
               "atomic(int64) must be 8-byte aligned as a struct member");

static int test_atomic64_ops()
{
    atomic(int64) v;
    atomic(uint64) u;

    int64 ivals[] = { 0, 1, -1, (int64)0x0123456789ABCDEFLL, INT64_MIN, INT64_MAX };
    for (size_t i = 0; i < sizeof(ivals) / sizeof(ivals[0]); i++) {
        atomicStore(int64, &v, ivals[i], Relaxed);
        if (atomicLoad(int64, &v, Relaxed) != ivals[i])
            TEST_FAIL(1, _SL("int64 relaxed load/store round-trip failed for ${int}"), stvar(int64, ivals[i]));
        atomicStore(int64, &v, ivals[i], Release);
        if (atomicLoad(int64, &v, Acquire) != ivals[i])
            TEST_FAIL(1, _SL("int64 acquire/release load/store round-trip failed for ${int}"), stvar(int64, ivals[i]));
        atomicStore(int64, &v, ivals[i], SeqCst);
        if (atomicLoad(int64, &v, SeqCst) != ivals[i])
            TEST_FAIL(1, _SL("int64 seqcst load/store round-trip failed for ${int}"), stvar(int64, ivals[i]));
    }

    uint64 uvals[] = { 0, 1, UINT64_MAX, 0x0123456789ABCDEFULL, 0x8000000000000000ULL };
    for (size_t i = 0; i < sizeof(uvals) / sizeof(uvals[0]); i++) {
        atomicStore(uint64, &u, uvals[i], Relaxed);
        if (atomicLoad(uint64, &u, Relaxed) != uvals[i])
            TEST_FAIL(1, _SL("uint64 relaxed load/store round-trip failed for ${uint}"), stvar(uint64, uvals[i]));
    }

    atomicStore(int64, &v, 100, Relaxed);
    if (atomicExchange(int64, &v, 200, AcqRel) != 100)
        TEST_FAIL(1, _SL("int64 exchange did not return the previous value"), stvNone);
    if (atomicLoad(int64, &v, Relaxed) != 200)
        TEST_FAIL(1, _SL("int64 exchange did not store the new value"), stvNone);

    atomicStore(int64, &v, 42, Relaxed);
    int64 expected = 42;
    if (!atomicCompareExchange(int64, strong, &v, &expected, 43, AcqRel, Relaxed))
        TEST_FAIL(1, _SL("int64 CAS should have succeeded"), stvNone);
    if (atomicLoad(int64, &v, Relaxed) != 43)
        TEST_FAIL(1, _SL("int64 CAS success did not store the desired value"), stvNone);

    expected = 99;   // deliberately wrong, to force failure
    if (atomicCompareExchange(int64, strong, &v, &expected, 44, AcqRel, Relaxed))
        TEST_FAIL(1, _SL("int64 CAS should have failed"), stvNone);
    if (expected != 43)
        TEST_FAIL(1, _SL("int64 CAS failure did not write back the current value (got ${int})"), stvar(int64, expected));
    if (atomicLoad(int64, &v, Relaxed) != 43)
        TEST_FAIL(1, _SL("int64 CAS failure should not have modified the stored value"), stvNone);

    // fetchAdd/fetchSub crossing the 32-bit word boundary, and the fetchSub-via-negation
    // path used by CX_GENERATE_INT_ATOMICS.
    atomicStore(uint64, &u, 0xFFFFFFFFULL, Relaxed);
    if (atomicFetchAdd(uint64, &u, 1, AcqRel) != 0xFFFFFFFFULL)
        TEST_FAIL(1, _SL("uint64 fetchAdd did not return the previous value"), stvNone);
    if (atomicLoad(uint64, &u, Relaxed) != 0x100000000ULL)
        TEST_FAIL(1, _SL("uint64 fetchAdd across the word boundary produced the wrong result"), stvNone);

    atomicStore(int64, &v, 0, Relaxed);
    atomicFetchSub(int64, &v, 1, AcqRel);
    if (atomicLoad(int64, &v, Relaxed) != -1)
        TEST_FAIL(1, _SL("int64 fetchSub underflow produced the wrong result"), stvNone);

    // fetchAnd/Or/Xor with high-word-only and low-word-only masks.
    atomicStore(uint64, &u, 0xFFFFFFFF00000000ULL, Relaxed);
    atomicFetchAnd(uint64, &u, 0x00000000FFFFFFFFULL, AcqRel);
    if (atomicLoad(uint64, &u, Relaxed) != 0)
        TEST_FAIL(1, _SL("uint64 fetchAnd across the word boundary produced the wrong result"), stvNone);

    atomicStore(uint64, &u, 0, Relaxed);
    atomicFetchOr(uint64, &u, 0xFFFFFFFF00000000ULL, AcqRel);
    if (atomicLoad(uint64, &u, Relaxed) != 0xFFFFFFFF00000000ULL)
        TEST_FAIL(1, _SL("uint64 fetchOr into the high word produced the wrong result"), stvNone);

    atomicStore(uint64, &u, 0xFFFFFFFFFFFFFFFFULL, Relaxed);
    atomicFetchXor(uint64, &u, 0x00000000FFFFFFFFULL, AcqRel);
    if (atomicLoad(uint64, &u, Relaxed) != 0xFFFFFFFF00000000ULL)
        TEST_FAIL(1, _SL("uint64 fetchXor on the low word produced the wrong result"), stvNone);

    return 0;
}

#define A64_ADD_THREADS 8
#define A64_ADD_PER_THREAD 131072

static atomic(uint64) a64counter;

static int thrproc_a64add(Thread *self)
{
    int32 count;
    if (!stvlNext(&self->args, int32, &count))
        return 0;

    for (int i = 0; i < count; i++)
        atomicFetchAdd(uint64, &a64counter, 1, AcqRel);

    return 0;
}

#define A64_PATTERN_ITERS 500000
#define A64_PATTERN_READERS 4

static atomic(uint64) a64pattern;

static int thrproc_a64reader(Thread *self)
{
    while (!atomicLoad(bool, &rthread_exit, Acquire)) {
        uint64 pat = atomicLoad(uint64, &a64pattern, Acquire);
        uint32 hi = (uint32)(pat >> 32);
        uint32 lo = (uint32)pat;
        if (hi != lo)
            atomicStore(bool, &fail, true, Release);
    }
    return 0;
}

static int thrproc_a64storewriter(Thread *self)
{
    for (uint32 w = 1; w <= A64_PATTERN_ITERS; w++)
        atomicStore(uint64, &a64pattern, ((uint64)w << 32) | w, Release);
    atomicStore(bool, &rthread_exit, true, Release);
    return 0;
}

static int thrproc_a64xorwriter(Thread *self)
{
    // A full-word toggle keeps the two halves equal both before and after a correctly
    // atomic op; a reader that observes hi != lo mid-flip caught a torn RMW.
    for (uint32 i = 0; i < A64_PATTERN_ITERS; i++)
        atomicFetchXor(uint64, &a64pattern, 0xFFFFFFFFFFFFFFFFULL, AcqRel);
    atomicStore(bool, &rthread_exit, true, Release);
    return 0;
}

static int test_atomic64_contend()
{
    int i, ret = 0;

    // (1) fetchAdd under contention: lost-update check, with the running total crossing
    // the 32-bit boundary many times over.
    atomicStore(uint64, &a64counter, 0, Relaxed);
    Thread *addthreads[A64_ADD_THREADS];
    for (i = 0; i < A64_ADD_THREADS; i++) {
        addthreads[i] = thrCreate(thrproc_a64add, _S"Atomic64 Add", stvar(int32, A64_ADD_PER_THREAD));
        if (!addthreads[i])
            TEST_FAIL(1, _SL("thrCreate failed for atomic64 add thread ${int}"), stvar(int32, i));
    }
    for (i = 0; i < A64_ADD_THREADS; i++) {
        thrWait(addthreads[i], timeForever);
        thrShutdown(addthreads[i]);
        thrRelease(&addthreads[i]);
    }
    uint64 expectedtotal = (uint64)A64_ADD_THREADS * (uint64)A64_ADD_PER_THREAD;
    if (atomicLoad(uint64, &a64counter, Relaxed) != expectedtotal)
        TEST_FAILV(ret, 1, _SL("uint64 fetchAdd total=${uint} != expected ${uint} (lost update under contention)"), stvar(uint64, atomicLoad(uint64, &a64counter, Relaxed)), stvar(uint64, expectedtotal));

    // (2) and (3): store/load and fetchXor tearing, driven by one writer thread and
    // several tight-spinning readers checking self-consistency of a (w<<32)|w pattern.
    static int (*const writers[])(Thread*) = { thrproc_a64storewriter, thrproc_a64xorwriter };
    for (int phase = 0; phase < 2; phase++) {
        atomicStore(bool, &fail, false, Release);
        atomicStore(bool, &rthread_exit, false, Release);
        atomicStore(uint64, &a64pattern, 0, Relaxed);

        Thread *readers[A64_PATTERN_READERS];
        for (i = 0; i < A64_PATTERN_READERS; i++) {
            readers[i] = thrCreate(thrproc_a64reader, _S"Atomic64 Pattern Reader", stvNone);
            if (!readers[i])
                TEST_FAIL(1, _SL("thrCreate failed for atomic64 reader thread ${int}"), stvar(int32, i));
        }
        Thread *writer = thrCreate(writers[phase], _S"Atomic64 Pattern Writer", stvNone);
        if (!writer)
            TEST_FAIL(1, _SL("thrCreate failed for atomic64 writer thread (phase ${int})"), stvar(int32, phase));

        thrWait(writer, timeForever);
        thrShutdown(writer);
        thrRelease(&writer);
        for (i = 0; i < A64_PATTERN_READERS; i++) {
            thrWait(readers[i], timeForever);
            thrShutdown(readers[i]);
            thrRelease(&readers[i]);
        }

        if (atomicLoad(bool, &fail, Acquire))
            TEST_FAILV(ret, 1, _SL("uint64 tearing detected in pattern phase ${int} (store/load or fetchXor split across a reader's atomicLoad)"), stvar(int32, phase));
    }

    return ret;
}

testfunc thrtest_funcs[] = {
    { "basic", test_basic },
    { "futex", test_futex },
    { "sema", test_sema },
    { "mutex", test_mutex },
    { "rwlock", test_rwlock },
    { "event", test_event },
    { "event_s", test_event_s },
    { "timeout", test_timeout },
    { "condvar", test_condvar },
    { "atomic64", test_atomic64_ops },
    { "atomic64_mt", test_atomic64_contend },
    { 0, 0 }
};
