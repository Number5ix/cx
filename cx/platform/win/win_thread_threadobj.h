#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/thread/threadobj.h>
#include <cx/platform/win.h>

typedef struct WinThread WinThread;
saDeclarePtr(WinThread);

typedef struct WinThread {
    ObjIface *_;
    union {
        ObjClassInfo *_clsinfo;
        void *_is_WinThread;
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
    bool background;
    HANDLE handle;
    DWORD id;
} WinThread;
extern ObjClassInfo WinThread_clsinfo;
#define WinThread(inst) ((WinThread*)((void)((inst) && &((inst)->_is_WinThread)), (inst)))
#define WinThreadNone ((WinThread*)NULL)

WinThread *WinThread_create();
#define _winthrobjCreate() WinThread_create()

