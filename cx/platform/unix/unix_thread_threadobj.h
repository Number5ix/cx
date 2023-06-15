#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/thread/threadobj.h>
#include <pthread.h>

typedef struct UnixThread UnixThread;
saDeclarePtr(UnixThread);

typedef struct UnixThread {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_UnixThread;
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
    pthread_t pthr;
    int id;
    bool joined;
} UnixThread;
extern ObjClassInfo UnixThread_clsinfo;
#define UnixThread(inst) ((UnixThread*)((void)((inst) && &((inst)->_is_UnixThread)), (inst)))
#define UnixThreadNone ((UnixThread*)NULL)

UnixThread *UnixThread_create();
#define _unixthrobjCreate() UnixThread_create()

