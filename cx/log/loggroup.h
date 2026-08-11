#pragma once
/// @file log/loggroup.h
/// @brief Drain groups: which thread does a destination's work

/// @defgroup log_group Drain Groups
/// @ingroup log
/// @{
///
/// A **group** owns one queue and one drain thread; a destination names the group its work runs
/// on. Every destination starts out in the `default` group unless told otherwise, so a program
/// that never mentions groups behaves exactly as it did when there was a single drain thread.
///
/// @code
///   LogDest *bulk = logfileRegister(LOG_Trace, _SL("net/**"), lfd);
///   logDestSetGroup(bulk, _SL("bulk"));   // ...but not on the console's thread
/// @endcode
///
/// **Create a second group when a destination is expensive, not just slow.** Rotation, retention
/// scans, an fsync per batch, and formatting records for a dozen differently-configured text
/// destinations are all occasionally expensive, and on one thread they stall everything behind
/// them. Name groups after the kind of work they do rather than creating one per destination --
/// destinations that share a drain thread also share wakeups, so batching several destinations
/// onto one group is cheaper than giving each its own thread:
///
/// | Group | Contents |
/// |---|---|
/// | `default` | Console, main application log. Low latency, never expensive. |
/// | `bulk` | Per-subsystem debug/trace files. High volume, latency-tolerant. |
/// | `remote` | Forwarders. |
/// | `archive` | Compression, encryption, rotation-heavy destinations. |
///
/// **Ordering is preserved within a group, not across groups.** Two destinations in different
/// groups can no longer be collated exactly by timestamp. Batches still keep their lines
/// together, because a batch is delivered to one destination and a destination lives in one
/// group; where exact cross-group order matters afterward, `LogRecord.seq` recovers it.
///
/// Each group costs one thread and one queue, so keep the count low. There are at most
/// #LOG_GROUP_MAX of them, and most programs need no more than the four in the table above.

#include <cx/log/log.h>

CX_C_BEGIN

/// Largest number of groups the process may have
///
/// Groups are cheap but not meant to be created freely -- a handful, split by the kind of work
/// each one does (see the table above), is enough for almost any program. 32 is far more than
/// that split ever needs.
#define LOG_GROUP_MAX 32

/// A named drain group: one queue, one thread
typedef struct LogGroup LogGroup;

/// Look up a drain group by name, creating it if it does not exist yet
///
/// Calling this again with the same name returns the same group rather than creating a new one.
/// Groups are permanent for the lifetime of the process, so the returned pointer can be cached.
/// Creating a group starts its drain thread.
///
/// @param name Group name; empty means the default group
/// @return Group handle, or NULL if the logging system is not running or LOG_GROUP_MAX is reached
/// @code
///   LogGroup *bulk = logGroup(_SL("bulk"));
/// @endcode
_Ret_opt_valid_ LogGroup* logGroup(_In_opt_ strref name);

/// The default group, which holds every destination that has not been moved to another group
///
/// Starts the logging system if nothing has logged yet, so the first call in a process is as good
/// as any later one.
///
/// @return The default group, or NULL if the logging system has been shut down
_Ret_opt_valid_ LogGroup* logDefaultGroup(void);

/// A group's name
///
/// @param group Group to inspect
/// @return The group's name; empty for the default group
_Ret_opt_valid_ strref logGroupName(_In_ LogGroup* group);

/// Move a destination onto a drain group
///
/// **Call this immediately after registering the destination**, before it can receive any log
/// records. Moving a destination flushes its queue first so nothing already queued is lost, but
/// a record enqueued in the brief gap between that flush and the move is dropped for this
/// destination rather than delivered twice. That gap only matters for a destination that is
/// already receiving records; a destination moved right after registering never hits it.
///
/// @param dhandle Destination handle from logRegisterDest() or a transport's Register function
/// @param name Group name; empty moves the destination back to the default group
/// @return false if the destination is not registered, or the group could not be created
/// @code
///   LogDest *dest = logfileRegister(LOG_Trace, _SL("net/**"), lfd);
///   logDestSetGroup(dest, _SL("bulk"));
/// @endcode
bool logDestSetGroup(_In_ LogDest* dhandle, _In_opt_ strref name);

/// @}  // end of log_group group

CX_C_END
