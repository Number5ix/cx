// ---------------------------------------------------------------------------------------------
// HTTP-date
//
// RFC 9110 5.6.7. A sender must use the RFC 1123 form and a recipient must accept two obsolete ones
// as well, so this file is asymmetric on purpose: one formatter, three parsers.
//
// Every HTTP-date is GMT, which is what makes this simple -- there is no zone to resolve, so the
// whole thing is TimeParts arithmetic over cx's canonical UTC timestamps and no local-time call
// appears anywhere.
// ---------------------------------------------------------------------------------------------

#include "http_private.h"

#include <cx/format.h>
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

// Parse an unsigned decimal field, requiring the whole slice to be digits. Strict on purpose: a
// date is either well formed or it is not usable, and a permissive parse here turns a corrupt
// Expires into a plausible-looking wrong answer.
_Success_(return) static bool numField(_Out_ uint32* out, strref s)
{
    uint64 v = 0;
    if (!strToUInt64(&v, s, 10, true) || v > UINT32_MAX)
        return false;
    *out = (uint32)v;
    return true;
}

// Split "HH:MM:SS" into its three parts.
_Success_(return) static bool timeField(_Inout_ TimeParts* tp, strref s)
{
    sa_string parts = { 0 };   // strSplit clears rather than initializes; a zeroed array self-sizes
    bool ok         = false;

    if (strSplit(&parts, s, _SL(":"), true) == 3) {
        uint32 h = 0, m = 0, sec = 0;
        if (numField(&h, parts.a[0]) && numField(&m, parts.a[1]) && numField(&sec, parts.a[2]) &&
            h < 24 && m < 60 && sec <= 60) {
            // A leap second (":60") is clamped rather than rejected. It is legal in the grammar,
            // cx's timestamps have no representation for it, and the alternative is failing to
            // parse a date that is merely one second unusual.
            tp->hour   = (uint8)h;
            tp->minute = (uint8)m;
            tp->second = (uint8)(sec == 60 ? 59 : sec);
            ok         = true;
        }
    }

    saDestroy(&parts);
    return ok;
}

// RFC 1123: "Sun, 06 Nov 1994 08:49:37 GMT"
// RFC 850:  "Sunday, 06-Nov-94 08:49:37 GMT"
//
// Both start with a day name and a comma, so they are told apart by the separator inside the date
// field: '-' for the obsolete form, ' ' for the preferred one.
_Success_(return) static bool parseDatedForm(_Out_ TimeParts* tp, strref s)
{
    // Everything after the comma; the day-of-week is redundant with the date and is not checked
    // against it, because a peer getting it wrong is not a reason to reject an otherwise valid
    // date.
    int32 comma = strFindChar(s, 0, ',');
    if (comma < 0)
        return false;

    string rest = 0;
    strSubStr(&rest, s, comma + 1, strEnd);
    _httpTrimOWS(&rest);

    sa_string fields = {
        0
    };   // strSplit clears rather than initializes; a zeroed array self-sizes
    bool ok = false;

    // "06 Nov 1994 08:49:37 GMT" or "06-Nov-94 08:49:37 GMT": splitting on both separators at once
    // handles either without having to decide which form this is first.
    if (strSplitAny(&fields, rest, _SL(" -"), false) == 5 && strEqi(fields.a[4], _SL("GMT"))) {
        uint32 day = 0, year = 0;
        uint8 mon = monthFromName(fields.a[1]);

        if (mon && numField(&day, fields.a[0]) && numField(&year, fields.a[2]) && day >= 1 &&
            day <= 31) {
            // RFC 850's two-digit year. RFC 9110 says to interpret a year that would put the date
            // more than 50 years in the future as being in the past instead; the fixed 1970 pivot
            // below is the usual approximation and is stable, which a sliding window is not.
            if (strLen(fields.a[2]) == 2)
                year += (year < 70) ? 2000 : 1900;

            tp->year  = (int32)year;
            tp->month = mon;
            tp->day   = (uint8)day;
            ok        = timeField(tp, fields.a[3]);
        }
    }

    saDestroy(&fields);
    strDestroy(&rest);
    return ok;
}

// asctime: "Sun Nov  6 08:49:37 1994". No comma, and the day is space-padded rather than
// zero-padded, so the double space before a single-digit day is significant to a naive splitter --
// which is why this splits on runs rather than on single separators.
_Success_(return) static bool parseAsctime(_Out_ TimeParts* tp, strref s)
{
    sa_string fields = {
        0
    };   // strSplit clears rather than initializes; a zeroed array self-sizes
    bool ok = false;

    // keepempty = false collapses the padding run, so "Nov  6" yields two fields either way.
    if (strSplitAny(&fields, s, _SL(" "), false) == 5) {
        uint32 day = 0, year = 0;
        uint8 mon = monthFromName(fields.a[1]);

        if (mon && numField(&day, fields.a[2]) && numField(&year, fields.a[4]) && day >= 1 &&
            day <= 31) {
            tp->year  = (int32)year;
            tp->month = mon;
            tp->day   = (uint8)day;
            ok        = timeField(tp, fields.a[3]);
        }
    }

    saDestroy(&fields);
    return ok;
}

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

    TimeParts tp = { 0 };
    bool ok;

    // A comma is what distinguishes the two dated forms from asctime; which of the two it is falls
    // out of the field splitting rather than needing its own test.
    if (strFindChar(trimmed, 0, ',') >= 0)
        ok = parseDatedForm(&tp, trimmed);
    else
        ok = parseAsctime(&tp, trimmed);

    strDestroy(&trimmed);

    if (!ok)
        return false;

    if (tp.day > daysInMonth(tp.year, tp.month))
        return false;

    // timeCompose ignores wday/yday and answers 0 for a date it cannot represent at all.
    int64 t = timeCompose(&tp);
    if (t == 0)
        return false;

    *out = t;
    return true;
}
