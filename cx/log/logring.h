#pragma once
/// @file log/logring.h
/// @brief Bounded retention rings: the boot window, and the retroactive debug ring

/// @defgroup log_ring Retention Rings
/// @ingroup log
/// @{
///
/// A retention ring holds recent entries that no destination asked for, so that something which
/// happens *later* can decide they were worth keeping after all. Its default outcome is discard:
/// entries expire out of a ring and cost nothing but the memory they occupied while they were in
/// it.
///
/// There is one mechanism and two triggers:
///
/// - The **boot window** retains entries from the moment it opens and replays them into any
///   destination that registers while it is open. Startup logs before the log file is configured
///   -- or, once forwarding exists, before any remote subscriber has appeared -- reach the
///   destination anyway.
/// - The **retroactive debug ring** keeps the recent Debug/Trace traffic of a channel and dumps
///   it when an error fires there, so a failure arrives with the context that led to it and a
///   process that is behaving pays nothing for trace I/O.
///
/// The difference between the two is only in what fills a ring and what releases it. The boot
/// window keeps the **oldest** entries, because startup diagnostics are what it is for and the
/// interesting ones come first; the debug ring keeps the **newest**, because the context of a
/// failure is whatever immediately preceded it.
///
/// **A ring raises the channel's level ceiling.** Entries that no destination wants are normally
/// discarded at the call site, before an entry exists at all -- so a ring has to be accounted for
/// in that decision or it would never see anything. This is the cost of having one open: entries
/// get built, copied and retained that would otherwise have cost next to nothing to check and
/// discard.

#include <cx/log/log.h>

CX_C_BEGIN

/// Entries the boot window retains when no count is given
#define LOG_BOOT_DEFAULT_ENTRIES 4096

/// How long the boot window stays open when no duration is given
#define LOG_BOOT_DEFAULT_DURATION timeS(30)

/// Open the boot window
///
/// Every record at or below `maxlevel`, on any channel, is retained from this call onwards.
/// A destination registered while the window is open is backfilled from the ring first, filtered
/// by its own level and channel rules, before it receives anything live.
///
/// Retention stops at whichever cap is reached first; the window itself stays open, so a
/// destination registering later still gets whatever was retained. The window closes on
/// logBootWindowEnd(), or on the deadline, and the ring is discarded then. Nothing is lost by
/// that: the entries went to their local destinations when they were logged, and the ring is a
/// second chance for a destination that may never arrive, not the system of record.
///
/// The backfill defines where a backfilled destination's log starts: records older than the end
/// of it are not delivered to it a second time, whether they came from the ring or were still
/// waiting in a queue. Records logged before the window opened, or too verbose for it to have
/// retained, are dropped for that destination rather than arriving out of order ahead of the
/// backfill.
///
/// @param maxlevel Most verbose level to retain, e.g. LOG_Verbose
/// @param maxentries Entries to retain, or 0 for LOG_BOOT_DEFAULT_ENTRIES
/// @param maxbytes Approximate bytes to retain, or 0 for no byte cap
/// @param duration How long the window stays open, 0 for LOG_BOOT_DEFAULT_DURATION, negative for
///                 no deadline at all
/// @code
///   logBootWindowBegin(LOG_Verbose, 0, 0, 0);
///   ...                                          // config parsing, subsystem startup
///   LogDest *dest = logfileRegister(LOG_Info, NULL, lfd);   // backfilled with all of it
///   logBootWindowEnd();
/// @endcode
void logBootWindowBegin(int maxlevel, uint32 maxentries, uint64 maxbytes, int64 duration);

/// Close the boot window and discard what it retained
///
/// The application knows when it considers itself started, and knows it far better than any
/// timer. Safe to call when no window is open.
void logBootWindowEnd(void);

/// Is the boot window open?
///
/// @return true if a destination registering now would be backfilled
bool logBootWindowActive(void);

/// How many entries the boot window is currently holding
///
/// @return Retained entry count, 0 if no window is open
uint32 logBootWindowCount(void);

/// Entries a debug ring retains when no count is given
#define LOG_DEBUGRING_DEFAULT_ENTRIES 256

/// Give a channel a retroactive debug ring
///
/// **Off by default, everywhere.** A channel has no ring until this is called for it or for one
/// of its ancestors, and a process that never calls it pays nothing at all: the ring is what
/// raises the channel's level ceiling, so without one the verbose records it would have kept are
/// still discarded at the call site.
///
/// While a ring is configured, records on the channel that no destination wanted -- and only
/// those, so nothing is ever delivered twice -- are retained, oldest evicted first. When a record
/// at or below `triglevel` is logged on a channel the ring covers, everything it holds is
/// released to the destinations that route the channel, ahead of the record that released it.
///
/// A released record is filtered as if it were the severity of the event that released it, not
/// its own: a destination that would have seen the error sees the context leading up to it, and
/// one that would not see the error does not get a burst of trace either.
///
/// The ring is inherited down the path, so one on `net` covers all of `net/http/request`. Setting
/// one on the root channel (`chan` NULL) covers the whole process.
///
/// @param chan Channel to cover, or NULL for the root channel
/// @param maxlevel Most verbose level to retain, e.g. LOG_Trace
/// @param maxentries Entries to retain, or 0 for LOG_DEBUGRING_DEFAULT_ENTRIES
/// @param triglevel Level that releases the ring, e.g. LOG_Error
/// @return true if the ring was configured
/// @code
///   logChanSetDebugRing(logChan(_SL("net")), LOG_Trace, 512, LOG_Error);
/// @endcode
bool logChanSetDebugRing(_In_opt_ LogChannel* chan, int maxlevel, uint32 maxentries, int triglevel);

/// Take a channel's debug ring away
///
/// The subtree goes back to whatever it inherits, which is usually nothing. Anything the ring was
/// holding is discarded, not released.
///
/// @param chan Channel to clear, or NULL for the root channel
void logChanClearDebugRing(_In_opt_ LogChannel* chan);

/// How many entries the ring covering a channel is currently holding
///
/// @param chan Channel to inspect, or NULL for the root channel
/// @return Retained entry count, 0 if the channel has no ring
uint32 logChanDebugRingCount(_In_opt_ LogChannel* chan);

/// @}  // end of log_ring group

CX_C_END
