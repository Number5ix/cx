#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/core/stvar.h>
#include <cx/container/sarray.h>
#include <cx/thread/atomic.h>
#include <cx/thread/event.h>
#include <cx/thread/threadbase.h>

typedef struct Thread Thread;
saDeclarePtr(Thread);

typedef struct Thread {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_Thread;
        void *_is_ObjInst;
    };
    atomic(intptr) _ref;

    threadFunc entry;
    string name;
    int exitCode;
    stvlist args;
    sa_stvar _argsa;
    atomic(bool) running;
    atomic(bool) requestExit;
    Event notify;
} Thread;
extern ObjClassInfo Thread_clsinfo;
#define Thread(inst) ((Thread*)((void)((inst) && &((inst)->_is_Thread)), (inst)))
#define ThreadNone ((Thread*)NULL)

Thread *Thread_create(threadFunc func, strref name, int n, stvar args[], bool ui);
#define _throbjCreate(func, name, n, args, ui) Thread_create(func, name, n, args, ui)
