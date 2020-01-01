#pragma once

// Old Windows-specific workthread implementation. TODO: Replace this with new atomic-based queue.

#include "cx/cx.h"
#include "cx/platform/win.h"

#define THREAD_SHOULD_EXIT (-1)

typedef struct WorkThread WorkThread;

typedef int(*ThreadNotifyCallback)(WorkThread *from, uint32_t id, uint32_t param, void *data);
typedef void(*ThreadRemoteCall)(WorkThread *from, uint32_t param, void *data);
typedef int(*ThreadProc)(WorkThread *self);
typedef void(*MemoryDestructor)(void* p);

typedef struct ThreadNotify {
    WorkThread *from;
    uint32_t id;
    uint32_t param;
    void *data;
    MemoryDestructor dtor;
    ThreadRemoteCall rcall;
} ThreadNotify;

typedef struct ThreadNotifyHandler {
    uint32_t id;
    ThreadNotifyCallback callback;
} ThreadNotifyHandler;

typedef struct WorkThread {
    HANDLE hThread;             // thread object
    HANDLE hNotify;             // event object for notify queue
    CRITICAL_SECTION cs;        // critical section for thread

    size_t szQueue;             // current max size of queue
    size_t nQueue;              // entries in queue
    ThreadNotify *queue;

    size_t szHandlers;
    size_t nHandlers;
    ThreadNotifyHandler *handlers;

    bool shutdown;
} WorkThread;

// Notify IDs are generally defined by the thread that *receives* them,
// but there are exceptions
enum ThreadNotifyIdBase {
    NOTIFY_GLOBAL = 1,          // global messages, not specific to any thread
    NOTIFY_MASTER = 1001,       // master thread
    NOTIFY_UI = 2001,           // UI thread
};

enum ThreadGlobalNotifyId {
    NGLOBAL_CREATE = NOTIFY_GLOBAL, // sent when thread is created, from creator
    NGLOBAL_SHUTDOWN,               // we are shutting down
    NGLOBAL_WMQUIT,                 // thread received a WM_QUIT message from windows queue
    NGLOBAL_WMTHREAD,               // thread received some other windows message (see data)
    NGLOBAL_REMOTECALL,             // generic remote call
};

//     proc: Thread procedure, or NULL to use a generic procedure that only
//           processes notifications.
// handlers: 0-terminated array of notificaiton handlers to populate the thread with.
//           If NULL, any notification handlers must be registered by the thread proc.
WorkThread *thrCreate(ThreadProc proc, ThreadNotifyHandler *handlers);

// The Ex variant lets you specify parameters to the NGLOBAL_CREATE notification
WorkThread *thrCreateEx(ThreadProc proc, ThreadNotifyHandler *handlers, bool suspended,
                        uint32_t param, void *data, MemoryDestructor dtor);

void thrDestroy(WorkThread *thr);

// Send an NGLOBAL_SHUTDOWN notification to every thread, wait for them to terminate, and
// destroy all the threads that did terminate. If force is true, don't take no for an
// answer and kill any thread that times out.
bool thrShutdown(int32_t ms, bool force);

bool thrNotify(WorkThread *dest, uint32_t id, uint32_t param);
// Be aware that data is not copied but the pointer is sent cross-thread, so you
// have to deal with that! If owndata is true, the event handler takes care of calling
// free() on data whether or not a handler receives it.
bool thrNotifyEx(WorkThread *dest, uint32_t id, uint32_t param, void *data, MemoryDestructor dtor);

bool thrRemoteCall(WorkThread *dest, ThreadRemoteCall func, uint32_t param, void *data, MemoryDestructor dtor);

// Sets a specific window to get dialog processing by the therad's message loop
HWND thrSetDialog(HWND dialog);

// These functions operate on the current thread! -----------------------------
WorkThread *thrCurrent();
bool thrRegisterHandler(uint32_t id, ThreadNotifyCallback cb);
bool thrWait(int32_t ms);                   // 0 for poll, -1 for infinite
bool thrHandleAll();                        // returns false if the thread should exit

// Block until specific notification is received, or timeout is up, while continuing
// to process other notifications.
bool thrWaitFor(uint32_t id, int32_t ms);

// Only call from main thread, sets stuff up. Don't try to destroy the returned thread!
WorkThread *initMasterThread(ThreadNotifyHandler *handlers);
