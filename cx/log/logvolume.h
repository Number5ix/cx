#pragma once
/// @file log/logvolume.h
/// @brief Volume control: what happens when there is too much to log

/// @defgroup log_volume Volume Control and Backpressure
/// @ingroup log
/// @{
///
/// Everything that decides how much actually gets logged, and what happens to the rest. Four
/// separate mechanisms, none of them on by default except the last:
///
/// - **Sampling** (logChanSetSampling()) keeps 1 record in N on a channel, at the call site,
///   before an entry is allocated. The cheapest of the four and the bluntest.
/// - **Deduplication** (logSetDedup()) runs on the drain thread, keyed by call site, and can say
///   what it suppressed -- "...and 4,812 more like this in the last 10s". A call site gating
///   itself with logStrEveryN() cannot, because it never sees the other call sites.
/// - **Backpressure by severity** (logSetSyncLevel()) decides what happens when a queue is full.
///   The default is that a Fatal is never dropped: it is written from the logging thread instead.
/// - **Metrics** (logGetStats()) count what all of the above did.
///
/// Sampling and deduplication are complementary to the per-call-site gating in @ref
/// log_ratelimit, not replacements for it: that gates one site before an entry exists, this
/// works across every site and can report on what it dropped.

#include <cx/log/log.h>

CX_C_BEGIN

/// What the log system has been doing
///
/// Counters are cumulative since startup or the last logResetStats(); depths are instantaneous.
typedef struct LogStats {
    uint64 enqueued;      ///< Entries created and handed to at least one drain group
    uint64 dropped;       ///< Entries lost because a queue and its overflow list were both full
    uint64 sampled;       ///< Entries never created because sampling discarded them
    uint64 suppressed;    ///< Records the drain-thread deduplicator did not deliver
    uint64 synchronous;   ///< Entries written from the logging thread under backpressure
    uint32 queued;        ///< Entries currently waiting in drain queues, summed over groups
    uint32 queuedmax;     ///< High-water mark of the above
    uint32 groups;        ///< Number of drain groups
} LogStats;

/// Read the log system's counters
///
/// @param out Receives the current statistics
/// @code
///   LogStats st;
///   logGetStats(&st);
///   if (st.dropped)
///       reportLogLoss(st.dropped);
/// @endcode
void logGetStats(_Out_ LogStats* out);

/// Zero the cumulative counters and the queue high-water mark
void logResetStats(void);

/// Log the statistics periodically to the restricted `cx/log/stats` channel
///
/// The record is emitted by a drain thread when it next goes idle and the interval has passed, so
/// there is no timer and no thread of its own, and a process logging nothing produces nothing.
/// The channel is declared LOG_Restricted, so a destination has to name it: metrics do not
/// appear in a general-purpose log by accident (see logDeclareChan()).
///
/// @param interval Time between records, or 0 to stop (the default)
/// @code
///   logfileRegister(LOG_Notice, _SL("cx/log/stats"), lfd);
///   logSetStatsInterval(timeS(60));
/// @endcode
void logSetStatsInterval(int64 interval);

/// Keep only one record in N on a channel
///
/// Applied at the call site, before an entry exists, so a sampled-away record costs a counter
/// increment and nothing else. **Fatal and Error are never sampled**, whatever the rate: losing
/// an error to a sampling rate set for debug traffic is not a trade anyone intends to make.
///
/// The surviving record carries the rate it survived at (`LogRecord.sample`), so a structured
/// destination can scale the counts back up. Sampling is per channel, not inherited by children.
///
/// @param chan Channel to sample, or NULL for the default channel
/// @param n Keep one record in N; 0 or 1 turns sampling off
/// @code
///   logChanSetSampling(logChan(_SL("net/http/request")), 100);
/// @endcode
void logChanSetSampling(_In_opt_ LogChannel* chan, uint32 n);

/// Collapse repeats from the same call site on the drain thread
///
/// Within each window, the first `threshold` records from a call site are delivered normally and
/// the rest are counted. When the window closes, one summary record goes out in their place --
/// the text of the first suppressed record, plus how many followed it.
///
/// Keyed by the address of the call site's LogSite (@ref log_ratelimit), which is stable,
/// discloses nothing, and is a better key than hashing the message: two sites that happen to log
/// the same sentence keep separate budgets, and one site whose message varies per record still
/// shares one. Records logged without a call site -- anything generated dynamically -- are never
/// deduplicated.
///
/// @param window Length of the counting window, or 0 to turn deduplication off (the default)
/// @param threshold Records per site per window delivered before suppression starts
/// @code
///   logSetDedup(timeS(10), 5);   // 5 per site per 10s, then a summary
/// @endcode
void logSetDedup(int64 window, uint32 threshold);

/// Severity at or above which a record is never dropped for lack of queue space
///
/// When a drain queue and its per-thread overflow list are both full, records less severe than
/// this are dropped; records this severe or worse are written from the logging thread instead,
/// which is slow and blocks the caller but does not lose the message. Silently dropping a Fatal
/// is the worst failure mode this system has, so the default is LOG_Error.
///
/// @param level Log level, e.g. LOG_Error; pass -1 to disable synchronous writes entirely, or
/// LOG_Count to make every write synchronous (but really, don't do that)
/// @code
///   logSetSyncLevel(LOG_Warn);    // warnings survive a full queue too
/// @endcode
void logSetSyncLevel(int level);

/// Write everything queued, from this thread, without waiting for the drain threads
///
/// For a crashing process. Unlike logFlush(), it does not depend on any drain thread being alive
/// or responsive: it takes over the queues itself, waiting only briefly for an in-flight dispatch
/// to finish before proceeding regardless. That makes it unsafe to call concurrently with normal
/// operation in the general case, and correct in the one case it exists for.
///
/// @code
///   static void onFatalSignal(int sig) {
///       logStr(Fatal, _SL("caught a fatal signal"));
///       logPanicFlush();
///   }
/// @endcode
void logPanicFlush(void);

/// @}  // end of log_volume group

CX_C_END
