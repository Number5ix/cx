#include <stdio.h>
#include <cx/string.h>
#include <cx/thread.h>

#define TEST_FILE prqtest
#define TEST_FUNCS prqtest_funcs
#include "common.h"

static int test_prqtest_basic()
{
    int ret = 0;
    PrQueue queue;

    prqInit(&queue, 64, 64, PRQ_Grow_None);

    int testdata[64];
    int i;
    for(i = 0; i < 64; i++) {
        testdata[i] = i;
    }

    for(i = 0; i < 63; i++) {
        if(!prqPush(&queue, &testdata[i]))
            ret = 1;
    }

    // queue should be full as one slot is reserved
    if(prqPush(&queue, &testdata[63]))
        ret = 1;

    for(i = 0; i < 63; i++) {
        int *pint = prqPop(&queue);
        if(!pint || *pint != i)
            ret = 1;
    }

    // queue should be empty now
    if(prqPop(&queue) != NULL)
        ret = 1;

    for(i = 0; i < 64; i++) {
        testdata[i] = i + 64;
    }

    for(int j = 0; j < 4; j++) {
        for(i = 0; i < 16; i++) {
            if(!prqPush(&queue, &testdata[j * 16 + i]))
                ret = 1;
        }

        for(i = 0; i < 8; i++) {
            int *pint = prqPop(&queue);
            if(!pint || *pint != 64 + j * 8 + i)
                ret = 1;
        }
    }

    for(i = 0; i < 32; i++) {
        int *pint = prqPop(&queue);
        if(!pint || *pint != 96 + i)
            ret = 1;
    }

    // interleaved push/pop

    for(i = 0; i < 64; i++) {
        testdata[i] = i + 128;
    }

    for(i = 0; i < 64; i++) {
        if(!prqPush(&queue, &testdata[i]))
            ret = 1;

        int *pint = prqPop(&queue);
        if(!pint || *pint != 128 + i)
            ret = 1;
    }

    return ret;
}

#define PRQ_PRODUCERS 4
#define PRQ_RELAYS 3
#define PRQ_CONSUMERS 6
#define PRQ_NUM 61440

static atomic(bool) mtfail;
static atomic(uint64) pushfail;

static int mtTestProduce(Thread *self)
{
    PrQueue *queue;
    int *testdata;
    int32 tdstart, tdend;
    if(!stvlNext(&self->args, ptr, &queue) ||
       !stvlNext(&self->args, ptr, &testdata) ||
       !stvlNext(&self->args, int32, &tdstart) ||
       !stvlNext(&self->args, int32, &tdend))
        return 0;

    for(int i = tdstart; i < tdend; i++) {
        while(!prqPush(queue, &testdata[i])) {
            atomicFetchAdd(uint64, &pushfail, 1, Relaxed);
            osYield();
        }
    }

    return 0;
}

static int mtTestRelay(Thread *self)
{
    PrQueue *queue1, *queue2;
    int32 num;
    if(!stvlNext(&self->args, ptr, &queue1) ||
       !stvlNext(&self->args, ptr, &queue2) ||
       !stvlNext(&self->args, int32, &num))
        return 0;

    int count = 0;
    while (count < num) {
        int *pint = prqPop(queue1);
        if(pint) {
            while(!prqPush(queue2, pint)) {
                osYield();
            }
            count++;
        } else {
            osYield();
        }
    }

    return 0;
}

static int mtTestConsume(Thread *self)
{
    PrQueue *queue;
    Event *ev;
    int32 num;
    atomic(uint32) *total;
    atomic(uint32) *done;

    if(!stvlNext(&self->args, ptr, &queue) ||
       !stvlNext(&self->args, ptr, &ev) ||
       !stvlNext(&self->args, int32, &num) ||
       !stvlNext(&self->args, ptr, &total) ||
       !stvlNext(&self->args, ptr, &done))
        return 0;

    unsigned int subtotal = 0;
    int count = 0;
    while(count < num) {
        unsigned int *pint = prqPop(queue);
        if(pint) {
            subtotal += *pint;
            count++;
        } else {
            osYield();
        }
    }

    atomicFetchAdd(uint32, total, subtotal, AcqRel);
    atomicFetchAdd(uint32, done, 1, AcqRel);
    eventSignal(ev);
    return 0;
}

static int test_prqtest_mt()
{
    int ret = 0;
    PrQueue queue1, queue2;
    Event ev;
    atomic(uint32) total = atomicInit(0), done = atomicInit(0);

    prqInit(&queue1, 65, 65, PRQ_Grow_None);
    prqInit(&queue2, 65, 65, PRQ_Grow_None);

    unsigned int *testdata = xaAlloc(sizeof(int) * PRQ_NUM);
    unsigned int expected = 0;
    for(int i = 0; i < PRQ_NUM; i++) {
        testdata[i] = i;
        expected += i;
    }

    eventInit(&ev);
    for(int i = 0; i < PRQ_CONSUMERS; i++) {
        thrRun(mtTestConsume, _S"mtTestConsume",
               stvar(ptr, &queue2),
               stvar(ptr, &ev),
               stvar(int32, PRQ_NUM / PRQ_CONSUMERS),
               stvar(ptr, &total),
               stvar(ptr, &done));
    }

    for(int i = 0; i < PRQ_RELAYS; i++) {
        thrRun(mtTestRelay, _S"mtTestRelay",
               stvar(ptr, &queue1),
               stvar(ptr, &queue2),
               stvar(int32, PRQ_NUM / PRQ_RELAYS));
    }

    for(int i = 0; i < PRQ_PRODUCERS; i++) {
        thrRun(mtTestProduce, _S"mtTestProduce",
               stvar(ptr, &queue1),
               stvar(ptr, testdata),
               stvar(int32, (PRQ_NUM / PRQ_PRODUCERS) * i),
               stvar(int32, (PRQ_NUM / PRQ_PRODUCERS) * (i +1)));
    }

    int64 starttime = clockWall();
    while(atomicLoad(uint32, &done, Acquire) < PRQ_CONSUMERS) {
        eventWaitTimeout(&ev, timeS(10));
        if(atomicLoad(bool, &mtfail, Acquire) || clockWall() > starttime + timeS(60)) {
            ret = 1;
            break;
        }
    }

    xaFree(testdata);

    unsigned int result = atomicLoad(uint32, &total, Acquire);
    if(result != expected)
        ret = 1;

    return ret;
}

static int test_prqtest_grow()
{
    int ret = 0;
    PrQueue queue;

    prqInit(&queue, 8, 64, PRQ_Grow_100);

    int testdata[117];
    int i;
    for(i = 0; i < 117; i++) {
        testdata[i] = i;
    }

    // This should expand the queue 3 times, filling
    // the original 7 slots, then 15, then 31, finally 63 more
    for(i = 0; i < 116; i++) {
        if(!prqPush(&queue, &testdata[i]))
            ret = 1;
    }

    // queue should be full as one slot is reserved
    if(prqPush(&queue, &testdata[116]))
        ret = 1;

    for(i = 0; i < 116; i++) {
        int *pint = prqPop(&queue);
        if(!pint || *pint != i)
            ret = 1;
    }

    // queue should be empty now
    if(prqPop(&queue) != NULL)
        ret = 1;

    // now that it's expanded, we should only have 63 usable slots, so repeat the test and ensure it fulls up at the right time

    for(i = 0; i < 117; i++) {
        testdata[i] = i + 117;
    }

    for(i = 0; i < 63; i++) {
        if(!prqPush(&queue, &testdata[i]))
            ret = 1;
    }

    // queue should be full as one slot is reserved
    if(prqPush(&queue, &testdata[63]))
        ret = 1;

    for(i = 0; i < 63; i++) {
        int *pint = prqPop(&queue);
        if(!pint || *pint != testdata[i])
            ret = 1;
    }

    // queue should be empty now
    if(prqPop(&queue) != NULL)
        ret = 1;

    return ret;
}

static int test_prqtest_gc()
{
    int ret = 0;
    PrQueue queue;

    prqInit(&queue, 8, 64, PRQ_Grow_100);

    int testdata[117];
    int i;
    for(i = 0; i < 117; i++) {
        testdata[i] = i;
    }

    // fill up first segment, then 1 more to force an expansion
    for(i = 0; i < 8; i++) {
        if(!prqPush(&queue, &testdata[i]))
            ret = 1;
    }

    PrqSegment *seg1 = atomicLoad(ptr, &queue.current, Acquire);
    if(!atomicLoad(ptr, &seg1->nextseg, Acquire) || seg1->size != 8)
        ret = 1;

    for(i = 0; i < 7; i++) {
        int *pint = prqPop(&queue);
        if(!pint || *pint != testdata[i])
            ret = 1;
    }

    prqCollect(&queue);

    // seg1 should be retired and freed, seg2 should not have a next
    PrqSegment *seg2 = atomicLoad(ptr, &queue.current, Acquire);
    if(seg1 == seg2 || atomicLoad(ptr, &seg2->nextseg, Acquire) || seg2->size != 16)
        ret = 1;

    // should be able to push 14 more without allocating a new segment
    for (i = 8; i < 22; i++) {
        if(!prqPush(&queue, &testdata[i]))
            ret = 1;
    }
    if(atomicLoad(ptr, &seg2->nextseg, Acquire))
        ret = 1;

    // should do nothing
    prqCollect(&queue);

    if(!prqPush(&queue, &testdata[22]))
        ret = 1;

    PrqSegment *seg3 = atomicLoad(ptr, &seg2->nextseg, Acquire);
    if(!seg3 || seg3->size != 32)
        ret = 1;

    // should still do nothing
    prqCollect(&queue);
    if(atomicLoad(ptr, &queue.current, Acquire) != seg2)
        ret = 1;

    // pop all of them but one
    for(i = 7; i < 21; i++) {
        int *pint = prqPop(&queue);
        if(!pint || *pint != testdata[i])
            ret = 1;
    }

    // should STILL do nothing
    prqCollect(&queue);
    if(atomicLoad(ptr, &queue.current, Acquire) != seg2)
        ret = 1;

    for(i = 21; i < 22; i++) {
        int *pint = prqPop(&queue);
        if(!pint || *pint != testdata[i])
            ret = 1;
    }

    // finally should collect the segment
    prqCollect(&queue);
    if(atomicLoad(ptr, &queue.current, Acquire) != seg3)
        ret = 1;

    // should be 1 item left in the queue
    if(prqPop(&queue) == NULL)          // 22
        ret = 1;

    // queue should be empty now
    if(prqPop(&queue) != NULL)
        ret = 1;

    return ret;
}

testfunc prqtest_funcs[] = {
    { "basic", test_prqtest_basic },
    { "mt", test_prqtest_mt },
    { "grow", test_prqtest_grow },
    { "gc", test_prqtest_gc },
    { 0, 0 }
};
