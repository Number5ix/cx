// ---------------------------------------------------------------------------------------------
// HTTP-date
//
// RFC 9110 5.6.7. A sender must use the RFC 1123 form and a recipient must accept two obsolete ones
// as well, so this file is asymmetric on purpose: one format template, and one parse pattern with
// three alternatives.
//
// Every HTTP-date is GMT, which is what makes this simple -- there is no zone to resolve, so the
// whole thing is TimeParts arithmetic over cx's canonical UTC timestamps and no local-time call
// appears anywhere.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/format.h>
#include <cx/parse.h>
#include <cx/time/time.h>

// "Sun, 06 Nov 1994 08:49:37 GMT" -- day-of-week abbrev, zero-padded day, month abbrev, 4-digit
// year, zero-padded time. The literal GMT is required; an offset is not allowed here.
// Positional format arguments are matched per type family, not globally, so the two ${string}
// slots take the day and month abbreviations in order while the numeric slots take the numbers.
// The year is int32 (TimeParts stores it signed), which is why it is ${int} and not ${uint} --
// a ${uint} slot would never match it and the whole format would come back empty.
STR_CONST(kHttpDateFmt,
          "${string}, ${0uint(2)} ${string} ${0int(4)} "
          "${0uint(2)}:${0uint(2)}:${0uint(2)} GMT");

_Use_decl_annotations_
bool httpDateFormat(strhandle out, int64 time)
{
    TimeParts tp;
    if (!timeDecompose(&tp, time))
        return false;

    strFormat(out,
              kHttpDateFmt,
              stvar(strref, timeDayAbbrev[tp.wday]),
              stvar(uint8, tp.day),
              stvar(strref, timeMonthAbbrev[tp.month]),
              stvar(int32, tp.year),
              stvar(uint8, tp.hour),
              stvar(uint8, tp.minute),
              stvar(uint8, tp.second));
    return true;
}

// Month name to 1-based number, or 0 if it is not one of the twelve. Case-insensitive: the obsolete
// formats are not always as careful about capitalization as the preferred one.
static uint8 monthFromName(strref name)
{
    for (uint8 m = 1; m <= 12; m++) {
        if (strEqi(name, timeMonthAbbrev[m]))
            return m;
    }
    return 0;
}

// The three forms RFC 9110 requires a recipient to accept, as one pattern:
//
//   1. IMF-fixdate  "Sun, 06 Nov 1994 08:49:37 GMT"   the only one a sender may produce
//   2. RFC 850      "Sunday, 06-Nov-94 08:49:37 GMT"  obsolete, two-digit year
//   3. asctime      "Sun Nov  6 08:49:37 1994"        obsolete, space-padded day, no zone
//
// The field widths are the grammar's own, so writing them out as digits:# is what keeps the
// three forms from bleeding into each other -- a two-digit year is what makes the second form
// the second form. The day-of-week is matched but deliberately never checked against the date:
// a peer getting it wrong is not a reason to reject an otherwise valid date, so nothing binds
// ${string:wd} and it goes nowhere.
//
// The ranges are on the placeholders rather than in code afterwards because a value out of
// range has to fail its *branch*, so that the next form still gets its turn.
STR_PATTERN(kHttpDateParse,
            "(${string:wd}, ${uint:day(digits:2,min:1,max:31)} ${string:mon} "
            "${uint:year(digits:4)} ${uint:hh(digits:2,max:23)}:${uint:mi(digits:2,max:59)}:"
            "${uint:ss(digits:2,max:60)} GMT"
            "|${string:wd}, ${uint:day(digits:2,min:1,max:31)}-${string:mon}-"
            "${uint:year(digits:2)} ${uint:hh(digits:2,max:23)}:${uint:mi(digits:2,max:59)}:"
            "${uint:ss(digits:2,max:60)} GMT"
            "|${string:wd} ${string:mon} ${uint:day(maxdigits:2,min:1,max:31)} "
            "${uint:hh(digits:2,max:23)}:${uint:mi(digits:2,max:59)}:"
            "${uint:ss(digits:2,max:60)} ${uint:year(digits:4)}"
            "):form");

// Days in a month, for the calendar check below. timeCompose() normalizes an out-of-range day
// rather than rejecting it, so without this "31 Feb" would quietly become the 3rd of March -- a
// wrong answer that looks entirely plausible in an Expires header.
static uint8 daysInMonth(int32 year, uint8 month)
{
    static const uint8 days[13] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (month < 1 || month > 12)
        return 0;

    if (month == 2 && (year % 4 == 0) && (year % 100 != 0 || year % 400 == 0))
        return 29;

    return days[month];
}

_Use_decl_annotations_
bool httpDateParse(int64* out, strref s)
{
    string trimmed = 0;
    strDup(&trimmed, s);
    _httpTrimOWS(&trimmed);

    uint32 day = 0, year = 0, hour = 0, minute = 0, second = 0;
    string mon = 0;
    uint8 form = 0;

    bool ok = strPatternMatch(strPat(kHttpDateParse),
                              trimmed,
                              stvpk(day, uint32, &day),
                              stvpk(mon, string, &mon),
                              stvpk(year, uint32, &year),
                              stvpk(hh, uint32, &hour),
                              stvpk(mi, uint32, &minute),
                              stvpk(ss, uint32, &second),
                              stvpk(form, uint8, &form));

    strDestroy(&trimmed);

    uint8 month = ok ? monthFromName(mon) : 0;
    strDestroy(&mon);

    if (!ok || month == 0)
        return false;

    // RFC 850's two-digit year. RFC 9110 says to read a year that would put the date more than
    // 50 years in the future as being in the past instead; the fixed 1970 pivot below is the
    // usual approximation and is stable, which a sliding window is not.
    if (form == 2)
        year += (year < 70) ? 2000 : 1900;

    TimeParts tp = { 0 };
    tp.year      = (int32)year;
    tp.month     = month;
    tp.day       = (uint8)day;
    tp.hour      = (uint8)hour;
    tp.minute    = (uint8)minute;
    // A leap second (":60") is clamped rather than rejected. It is legal in the grammar, cx's
    // timestamps have no representation for it, and the alternative is failing to parse a date
    // that is merely one second unusual.
    tp.second    = (uint8)(second == 60 ? 59 : second);

    if (tp.day > daysInMonth(tp.year, tp.month))
        return false;

    // timeCompose ignores wday/yday and answers 0 for a date it cannot represent at all.
    int64 t = timeCompose(&tp);
    if (t == 0)
        return false;

    *out = t;
    return true;
}
