#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/thread/threadobj.h>
#include <pthread.h>

typedef struct UnixThread UnixThread;
saDeclarePtr(UnixThread);

typedef struct UnixThread {
    union {
        ObjIface *_;
        void *_is_UnixThread;
        void *_is_Thread;
        void *_is_ObjInst;
    };
    ObjClassInfo *_clsinfo;
    atomic(intptr) _ref;

    threadFunc entry;
    string name;
    int exitCode;        // only valid once 'running' become false
    stvlist args;
    sa_stvar _argsa;        // should use the stvlist instead where possible
    atomic(bool) running;
    atomic(bool) requestExit;
    Event notify;
    pthread_t pthr;
    int id;
    bool joined;
} UnixThread;
extern ObjClassInfo UnixThread_clsinfo;
#define UnixThread(inst) ((UnixThread*)(unused_noeval((inst) && &((inst)->_is_UnixThread)), (inst)))
#define UnixThreadNone ((UnixThread*)NULL)

UnixThread *UnixThread_create();
// UnixThread *_unixthrobjCreate();
#define _unixthrobjCreate() UnixThread_create()


