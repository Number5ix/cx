#include "taskqueue_private.h"
#include "userfunctask.h"
#include <cx/time/clock.h>
#include <cx/platform/os.h>

_Use_decl_annotations_
void tqPresetSingle(TaskQueueConfig *tqconfig)
{
    *tqconfig = (TaskQueueConfig){
        .wInitial = 1,
        .wIdle = 1,
        .wBusy = 1,
        .wMax = 1,
    };
}

_Use_decl_annotations_
void tqPresetMinimal(TaskQueueConfig *tqconfig)
{
    int ncpus = osPhysicalCPUs();

    *tqconfig = (TaskQueueConfig){
        .wInitial = 1,
        .wIdle = 1,
        .wBusy = (ncpus > 2) ? (ncpus + 1) / 2 : 1,
        .wMax = ncpus,
        .tIdle = timeMS(1000),
        .tRampUp = timeMS(50),
        .tRampDown = timeMS(200),
        .loadFactor = 8,
    };
}

_Use_decl_annotations_
void tqPresetBalanced(TaskQueueConfig *tqconfig)
{
    int npcpus = osPhysicalCPUs();
    int nlcpus = osLogicalCPUs();

    *tqconfig = (TaskQueueConfig){
        .wInitial = 1,
        .wIdle = (npcpus >= 2) ? 2 : 1,
        .wBusy = npcpus,
        .wMax = nlcpus,
        .tIdle = timeMS(5000),
        .tRampUp = timeMS(100),
        .tRampDown = timeMS(1000),
        .loadFactor = 2,
    };
}

_Use_decl_annotations_
void tqEnableMonitor(TaskQueueConfig *tqconfig)
{
    tqconfig->mInterval = timeS(5);
    tqconfig->mSuppress = timeS(60);
    tqconfig->mTaskRunning = timeS(120);
    tqconfig->mTaskWaiting = timeS(30);
    tqconfig->mTaskStalled = timeS(60);
}

_Use_decl_annotations_
TaskQueue *tqCreate(strref name, TaskQueueConfig *tqconfig)
{
    tqconfig->wMax = clamplow(tqconfig->wMax, 1);
    tqconfig->wIdle = clamp(tqconfig->wIdle, 1, tqconfig->wMax);
    tqconfig->wInitial = clamp(tqconfig->wInitial, tqconfig->wIdle, tqconfig->wMax);
    tqconfig->wBusy = clamp(tqconfig->wBusy, tqconfig->wIdle, tqconfig->wMax);

    tqconfig->loadFactor = clamplow(tqconfig->loadFactor, 1);

    TaskQueue *tq = taskqueueCreate(strEmpty(name) ? _S"TaskQueue" : name, tqconfig);

    return tq;
}

_Use_decl_annotations_
bool tqStart(TaskQueue *tq)
{
    if(tq->state == TQState_Running)
        return true;
    if(tq->state == TQState_Starting || tq->state == TQState_Stopping)
        return false;

    // This event is used for the manager to notify that startup was complete
    Event startev;
    eventInit(&startev);

    bool ret = false;
    if(taskqueueStart(tq, &startev)) {
        eventWait(&startev);
        ret = (tq->state == TQState_Running);
    }

    eventDestroy(&startev);
    return ret;
}

static _meta_inline bool tqStateCheck(_In_ BasicTask *task)
{
    int32 state = btaskState(task);
    bool ret = (state != TASK_Waiting && state != TASK_Running);
    devAssertMsg(ret, "Task not in a good state to be added to queue");
    return ret;
}

_Use_decl_annotations_
bool _tqAdd(_Inout_ TaskQueue *tq, _In_ BasicTask *task)
{
    if(tq->state != TQState_Running || !tqStateCheck(task))
        return false;

    // add a reference count which becomes owned by the queue
    objAcquire(task);

    atomicStore(int32, &task->state, TASK_Waiting, Release);
    if(!prqPush(&tq->runq, task)) {
        atomicStore(int32, &task->state, TASK_Failed, Release);
        return false;
    }
    // Signal the worker pool
    eventSignal(&tq->workev);
    return true;
}

bool _tqDefer(_Inout_ TaskQueue *tq, _In_ Task *task, int64 delay)
{
    if(tq->state != TQState_Running || !tqStateCheck(BasicTask(task)))
        return false;

    // add a reference count which becomes owned by the queue
    objAcquire(task);

    atomicStore(int32, &task->state, TASK_Waiting, Release);
    task->nextrun = (delay > 0) ? clockTimer() + delay : 0;

    // By putting it in the done queue, the manager thread will pick it up
    // and stick it in the deferred list, which we can't safely access from
    // other threads.
    if(!prqPush(&tq->doneq, task)) {
        atomicStore(int32, &task->state, TASK_Failed, Release);
        return false;
    }
    // Signal the manager to do something with it
    eventSignal(&tq->manager->notify);
    return true;
}

_Use_decl_annotations_
bool tqCall(TaskQueue *tq, UserTaskCB func, void *userdata)
{
    UserFuncTask *task = userfunctaskCreate(func, userdata);
    bool ret = _tqAdd(tq, BasicTask(task));
    objRelease(&task);
    return ret;
}

int32 tqWorkers(_In_ TaskQueue *tq)
{
    return atomicLoad(int32, &tq->nworkers, Relaxed);       // no ordering requirements
}

_Use_decl_annotations_
bool tqShutdown(TaskQueue *tq, int64 wait)
{
    if(tq->state == TQState_Shutdown || tq->state == TQState_Init)
        return true;

    // if queue is still running, tell it to shut down
    if(tq->state == TQState_Running) {
        tq->state = TQState_Stopping;
        eventSignal(&tq->manager->notify);
    }

    if(wait > 0)
        eventWaitTimeout(&tq->shutdownev, wait);

    return (tq->state == TQState_Shutdown);
}
