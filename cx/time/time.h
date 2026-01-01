/// @file time.h
/// @brief Time manipulation and conversion functions

#pragma once

#include <time.h>
#include "cx/cx.h"

/// @addtogroup time
/// @{

/// Maximum representable time value (approximately year 294,276 CE)
#define timeForever 9223372036854775807LL

/// A time literal specified in seconds
/// @param s Number of seconds
/// @return Time value in microseconds
#define timeS(s) timeFromSeconds(s)

/// A time literal specified in milliseconds
/// @param ms Number of milliseconds
/// @return Time value in microseconds
#define timeMS(ms) timeFromMsec(ms)

/// @}

/// @defgroup time_manip Time Manipulation
/// @ingroup time
/// @{
///
/// Functions for converting, decomposing, and manipulating time values.

/// Days of the week (0-6)
enum WEEKDAYS {
    Sunday = 0,   ///< Sunday
    Monday,       ///< Monday
    Tuesday,      ///< Tuesday
    Wednesday,    ///< Wednesday
    Thursday,     ///< Thursday
    Friday,       ///< Friday
    Saturday      ///< Saturday
};

/// Full names of days of the week (indexed by WEEKDAYS enum)
extern strref timeDayName[];

/// Three-letter abbreviations of days of the week (indexed by WEEKDAYS enum)
extern strref timeDayAbbrev[];

/// Full names of months (indexed 1-12, index 0 is empty string)
extern strref timeMonthName[];

/// Three-letter abbreviations of months (indexed 1-12, index 0 is empty string)
extern strref timeMonthAbbrev[];

/// Decomposed time structure with individual date/time components
typedef struct TimeParts {
    uint32 usec;    ///< Microseconds (0-999999)
    int32 year;     ///< Year (negative for BCE, no year 0)
    uint8 month;    ///< Month (1-12)
    uint8 day;      ///< Day of month (1-31)
    uint8 hour;     ///< Hour (0-23)
    uint8 minute;   ///< Minute (0-59)
    uint8 second;   ///< Second (0-59)

    // Information-only fields, not used for composition
    uint8 wday;    ///< Day of week (0-6, Sunday=0)
    uint16 yday;   ///< Day of year (0-365)
} TimeParts;

/// Decompose a time value into individual date/time components.
///
/// Converts a microsecond timestamp into year, month, day, hour, minute, second, and microsecond
/// components. Also populates day of week (wday) and day of year (yday) fields for information.
///
/// @param out Pointer to TimeParts structure to receive the decomposed time
/// @param time Time value in microseconds since Julian epoch
/// @return True if successful, false if time is negative
bool timeDecompose(_Out_ TimeParts* out, _In_range_(0, timeForever) int64 time);

/// Compose a time value from individual date/time components.
///
/// Converts year, month, day, hour, minute, second, and microsecond components into
/// a single microsecond timestamp. The wday and yday fields are ignored.
///
/// @param parts Pointer to TimeParts structure with time components to compose
/// @return Time value in microseconds since Julian epoch, or 0 if invalid
_Ret_range_(0, timeForever) int64 timeCompose(_In_ TimeParts* parts);

/// Convert a time to local time.
///
/// Adjusts the given time for the local time zone rules currently in effect.
/// Optionally returns the offset applied.
///
/// @param time Time value in microseconds since Julian epoch
/// @param offset Optional pointer to receive the offset in microseconds between UTC and local time
/// @return Time value adjusted for local time zone, or 0 on failure
_Success_(return > 0) int64 timeLocal(int64 time, _Out_opt_ int64* offset);

/// @}
// end of time_manip group

/// @defgroup time_convert Time Unit Conversion
/// @ingroup time
/// @{
///
/// Functions for converting between time units and external time formats.

/// Convert microseconds to seconds (truncating).
/// @param time Time value in microseconds
/// @return Time value in seconds
_meta_inline int64 timeToSeconds(int64 time)
{
    return time / 1000000;
}

/// Convert seconds to microseconds.
/// @param seconds Time value in seconds
/// @return Time value in microseconds
_meta_inline int64 timeFromSeconds(int64 seconds)
{
    return seconds * 1000000;
}

/// Convert microseconds to milliseconds (truncating).
/// @param time Time value in microseconds
/// @return Time value in milliseconds
_meta_inline int64 timeToMsec(int64 time)
{
    return time / 1000;
}

/// Convert milliseconds to microseconds.
/// @param msec Time value in milliseconds
/// @return Time value in microseconds
_meta_inline int64 timeFromMsec(int64 msec)
{
    return msec * 1000;
}

/// Convert a relative timespec to microseconds.
///
/// Converts a POSIX timespec structure representing a relative time interval
/// (not an absolute timestamp) to microseconds.
///
/// @param ts Pointer to timespec structure
/// @return Time interval in microseconds
_Ret_range_(0, timeForever) _meta_inline int64 timeFromRelTimespec(_In_ struct timespec* ts)
{
    int64 ret = (int64)ts->tv_sec * 1000000;
    ret += ts->tv_nsec / 1000;
    return ret;
}

/// Convert microseconds to a relative timespec.
///
/// Converts a time interval in microseconds to a POSIX timespec structure
/// representing a relative time (not an absolute timestamp).
///
/// @param ts Pointer to timespec structure to populate
/// @param time Time interval in microseconds
_meta_inline void timeToRelTimespec(_Out_ struct timespec* ts, int64 time)
{
    ts->tv_sec  = time / 1000000;
    ts->tv_nsec = (time % 1000000) * 1000;
}

/// Convert a Unix absolute timespec to Julian epoch time.
///
/// Converts a POSIX timespec structure representing an absolute timestamp
/// (seconds since Unix epoch: Jan 1, 1970) to microseconds since Julian epoch.
///
/// Unix epoch (Jan 1, 1970 00:00:00 UTC) corresponds to Julian date 2440587.50000,
/// which is 210866760000000000 microseconds from the Julian epoch.
///
/// @param ts Pointer to timespec structure with Unix epoch timestamp
/// @return Time value in microseconds since Julian epoch
_Ret_range_(0, timeForever) _meta_inline int64 timeFromAbsTimespec(_In_ struct timespec* ts)
{
    int64 ret = (int64)ts->tv_sec * 1000000;
    ret += ts->tv_nsec / 1000;

    // Unix epoch is midnight on Jan 1, 1970
    // Which is a julian date of 2440587.50000
    // That's 210866760000 in seconds, or in microseconds...

    ret += 210866760000000000LL;   // adjust epoch

    // This still leaves us over 280,000 years before overflow

    return ret;
}

/// Convert Julian epoch time to a Unix absolute timespec.
///
/// Converts a time value in microseconds since Julian epoch to a POSIX timespec
/// structure representing seconds since Unix epoch (Jan 1, 1970).
///
/// @param ts Pointer to timespec structure to populate
/// @param time Time value in microseconds since Julian epoch
_meta_inline void timeToAbsTimespec(_Out_ struct timespec* ts, int64 time)
{
    time -= 210866760000000000LL;   // adjust epoch

    ts->tv_sec  = time / 1000000;
    ts->tv_nsec = (time % 1000000) * 1000;
}

/// Convert a Unix time_t to Julian epoch time.
///
/// Converts a standard C time_t value (seconds since Unix epoch: Jan 1, 1970)
/// to microseconds since Julian epoch.
///
/// @param tt time_t value
/// @return Time value in microseconds since Julian epoch
_Ret_range_(0, timeForever) _meta_inline int64 timeFromTimeT(time_t tt)
{
    int64 ret = (int64)tt * 1000000;
    ret += 210866760000000000LL;   // adjust epoch
    return ret;
}

/// Convert Julian epoch time to a Unix time_t.
///
/// Converts a time value in microseconds since Julian epoch to a standard C
/// time_t value (seconds since Unix epoch).
///
/// @param time Time value in microseconds since Julian epoch
/// @return time_t value (seconds since Unix epoch)
_meta_inline time_t timeToTimeT(int64 time)
{
    time -= 210866760000000000LL;   // adjust epoch
    return (time_t)(time / 1000000);
}

/// @}
// end of time_convert group
