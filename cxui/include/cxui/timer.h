#pragma once

/// @file timer.h
/// @brief Timers that call back on the UI thread

#include <cx/closure.h>
#include <cxui/cxui_shared.h>
#include <cxui/events.h>

typedef struct CxUiTimer CxUiTimer;

CX_C_BEGIN

/// @defgroup cxui_timer Timers
/// @ingroup cxui
/// @{
/// A timer calls its handler on the UI thread, once or every period, while it runs.
///
/// - **The next call is timed from the end of the handler**, so a handler slower than the period
///   never has calls pile up behind it.
/// - **Starting and stopping are cheap**, and a handler may stop or restart its own timer.
/// - **Timers keep running inside modal loops**, such as a message box, and never hold one open.
/// - A timer stops when its last reference is released and when cxui shuts down; after a restart
///   it has to be started again.
///
/// @code
///   static CxUiResult onTip(stvlist* cvars, CxUiTimerEvent* ev)
///   {
///       tooltipShow(stvlNextPtr(cvars));
///       return CXUI_Default;
///   }
///
///   CxUiTimer* t = cxuiTimerCreate(timeMS(250), CXUI_Timer_OneShot, onTip, stvar(ptr, tip));
///   cxuiTimerStart(t);
/// @endcode

/// Flags for cxuiTimerCreate()
enum CxUiTimerFlags {
    /// Call the handler once, then stop
    CXUI_Timer_OneShot = 0x0001,
};

/// What a timer handler is called with
typedef struct CxUiTimerEvent {
    CxUiTimer* timer;
} CxUiTimerEvent;

/// A timer handler. The result is ignored.
typedef CxUiResult (*CxUiTimerFn)(stvlist* cvars, CxUiTimerEvent* ev);

/// CxUiTimer* cxuiTimerCreate(int64 period, flags_t flags, CxUiTimerFn fn, ...)
///
/// Creates a timer, stopped
///
/// @param period Time between calls, in microseconds (use timeMS() and the like)
/// @param flags CxUiTimerFlags
/// @param fn Handler function
/// @param ... Values to capture (`stvNone` for none)
/// @return The timer; release it with objRelease()
#define cxuiTimerCreate(period, flags, fn, ...) \
    cxuiTimerCreateWith(period, flags, closureCreateAs(CxUiTimerFn, fn, __VA_ARGS__))

/// @}  // end of cxui_timer group

CX_C_END

#include <cxui/core/cxuitimer.h>
