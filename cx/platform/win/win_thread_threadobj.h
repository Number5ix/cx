#pragma once
// This header file is auto-generated!
// Do not make changes to this file or they will be overwritten.
#include <cx/obj.h>
#include <cx/thread/threadobj.h>
#include <cx/platform/win.h>

typedef struct WinThread WinThread;
saDeclarePtr(WinThread);

typedef struct WinThread {
    union {
        ObjIface *_;
        void *_is_WinThread;
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
    bool background;
    HANDLE handle;
    DWORD id;
} WinThread;
extern ObjClassInfo WinThread_clsinfo;
#define WinThread(inst) ((WinThread*)(unused_noeval((inst) && &((inst)->_is_WinThread)), (inst)))
#define WinThreadNone ((WinThread*)NULL)

WinThread *WinThread_create();
// WinThread *_winthrobjCreate();
#define _winthrobjCreate() WinThread_create()


