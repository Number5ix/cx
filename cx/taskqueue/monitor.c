#include "taskqueue_private.h"
#include <cx/format.h>
#include <cx/string/strmanip.h>

static uint16 ptrHash(void *ptr)
{
    uint16 ret = 0;
    uint8 *p = (uint8 *)&ptr;
    for(int i = 0; i < sizeof(void *); i += 2, p += 2) {
        ret ^= ((uint16)p[0] << 8) | p[1];
    }
    return ret;
}

static void dumpTask(LogCategory *cat, void *ptr, int64 now, int wnum, bool isdefer)
{
    if(!ptr) {
        if(wnum > -1) {
            logFmtC(Diag, cat, _S"    ${int}: Idle", stvar(int32, wnum + 1));
        }
        return;
    }

    BasicTask *btask = ptr;
    Task *task = objDynCast(btask, Task);

    strref state = _S"Unknown";
    switch(btaskState(btask)) {
    case TASK_Created:
        state = _S"Created";
        break;
    case TASK_Running:
        state = _S"Running";
        break;
    case TASK_Waiting:
        state = _S"Waiting";
        break;
    case TASK_Deferred:
        state = _S"Deferred";
        break;
    case TASK_Succeeded:
        state = _S"Succeeded";
        break;
    case TASK_Failed:
        state = _S"Failed";
        break;
    }

    string prefix = 0;
    string suffix = 0;
    strTemp(&prefix, 16);
    strTemp(&suffix, 24);

    if(wnum > -1) {
        strFormat(&prefix, _S"${int}: ", stvar(int32, wnum + 1));
    }

    if(task && task->last > 0) {
        strFormat(&suffix, _S" [${int}ms]", stvar(int64, isdefer ? timeToMsec(now - task->lastprogress) : timeToMsec(now - task->last)));
        if(isdefer && task->nextrun == timeForever) {
            strAppend(&suffix, _S" [Defer Forever]");
        } else if (isdefer && task->nextrun > now) {
            string temp = 0;
            strTemp(&temp, 32);
            strFormat(&temp, _S" [Defer for ${int}ms]", stvar(int64, timeToMsec(task->nextrun - now)));
            strAppend(&suffix, temp);
            strDestroy(&temp);
        }
    }

    logFmtC(Diag, cat, _S"    ${string}${string}-${0uint(4,hex)} (${string})${string}",
            stvar(strref, prefix),
            stvar(strref, task ? task->name : _S"BasicTask"),
            stvar(uint16, ptrHash(ptr)),
            stvar(strref, state),
            stvar(strref, suffix));

    strDestroy(&prefix);
    strDestroy(&suffix);
}

static void dumpPRQ(LogCategory *cat, PrQueue *prq, int64 now)
{
    uint32 qcount = prqCount(prq);
    for(uint32 ui = 0; ui < qcount; ui++) {
        dumpTask(cat, prqPeek(prq, ui), now, -1, false);
    }
}

static void dumpQueue(TaskQueue *tq, int64 now)
{
    TaskQueueConfig *c = &tq->tqconfig;
    logStrC(Diag, c->mLogCat, _S"  Worker Threads:");
    int32 nworkers = atomicLoad(int32, &tq->nworkers, Relaxed);
    for(int i = 0; i < nworkers; i++) {
        dumpTask(c->mLogCat, atomicLoad(ptr, &tq->workers.a[i]->curtask, Relaxed), now, i, false);
    }

    logStrC(Diag, c->mLogCat, _S"  Run Queue:");
    dumpPRQ(c->mLogCat, &tq->runq, now);
    logStrC(Diag, c->mLogCat, _S"  Done Queue:");
    dumpPRQ(c->mLogCat, &tq->doneq, now);

    logStrC(Diag, c->mLogCat, _S"  Defer List:");
    for(int i = 0, ndefer = saSize(tq->deferred); i < ndefer; i++) {
        dumpTask(c->mLogCat, tq->deferred.a[i], now, -1, true);
    }
}

static void doWarn(TaskQueue *tq, bool *warned)
{
    if(*warned)
        return;

    logBatchBegin();
    logFmtC(Warn, tq->tqconfig.mLogCat, _S"Task Queue Monitor (${string}):", stvar(string, tq->name));

    *warned = true;
}

void _tqMonitorRun(TaskQueue *tq, int64 now, TQMonitorState *s)
{
    // monitor runs from the manager thread; so we can safely access all
    // the tasks and structures without locking
    TaskQueueConfig *c = &tq->tqconfig;

    if(now < s->lastwarn + c->mSuppress)
        return;

    bool warned = false;
    bool dumpq = false;

    // check the run queue for tasks that have been there too long
    uint32 qcount = prqCount(&tq->runq);
    for(uint32 ui = 0; ui < qcount; ui++) {
        void *ptr = prqPeek(&tq->runq, ui);
        if(ptr) {
            Task *task = objDynCast((BasicTask*)ptr, Task);
            if(task) {
                int32 state = btaskState(task);
                if(state == TASK_Running && now > task->last + c->mTaskRunning) {
                    doWarn(tq, &warned);
                    logFmtC(Warn, c->mLogCat, _S"  ${string}-${0uint(4,hex)} running for ${int} ms",
                            stvar(strref, task->name), stvar(uint16, ptrHash(task)),
                            stvar(int64, timeToMsec(task->last - now)));
                } else if(state == TASK_Waiting && now > task->last + c->mTaskWaiting && !dumpq) {
                    doWarn(tq, &warned);
                    dumpq = true;
                    logFmtC(Warn, c->mLogCat, _S"  ${string}-${0uint(4,hex)} waiting for ${int} ms",
                            stvar(strref, task->name), stvar(uint16, ptrHash(task)),
                            stvar(int64, timeToMsec(task->last - now)));
                }
            }
        }
    }

    // check the defer queue for tasks that aren't making progress
    int dcount = saSize(tq->deferred);
    for(int i = 0; i < dcount; i++) {
        Task *task = tq->deferred.a[i];
        if(task && !dumpq && now > task->lastprogress + c->mTaskStalled && task->nextrun != timeForever) {
            doWarn(tq, &warned);
            dumpq = true;
            logFmtC(Warn, c->mLogCat, _S"  ${string}-${0uint(4,hex)} stalled for ${int} ms",
                    stvar(strref, task->name), stvar(uint16, ptrHash(task)),
                    stvar(int64, timeToMsec(now - task->lastprogress)));
        }
    }

    if(dumpq)
        dumpQueue(tq, now);

    if(warned) {
        logBatchEnd();
        s->lastwarn = now;
    }
}
