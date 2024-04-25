#pragma once
#include "taskqueue.h"
#include "tqobject.h"
#include "task.h"
#include "worker.h"

typedef struct TQMonitorState
{
    int64 lastrun;
} TQMonitorState;

int tqManagerThread(Thread *self);
void tqMonitorRun(TaskQueue *tq, int64 now, TQMonitorState *s);
