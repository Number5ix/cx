#include "time.h"

#define SECSPERMIN  60
#define SECSPERHOUR (60 * 60)
#define SECSPERDAY  (24 * 60 * 60)
#define DAYSPERWEEK 7

#define EPOCH_WDAY Monday

STR_CONSTR(kNoMonth, "");

STR_CONSTR(kSunday, "Sunday");
STR_CONSTR(kMonday, "Monday");
STR_CONSTR(kTuesday, "Tuesday");
STR_CONSTR(kWednesday, "Wednesday");
STR_CONSTR(kThursday, "Thursday");
STR_CONSTR(kFriday, "Friday");
STR_CONSTR(kSaturday, "Saturday");

STR_CONSTR(kSun, "Sun");
STR_CONSTR(kMon, "Mon");
STR_CONSTR(kTue, "Tue");
STR_CONSTR(kWed, "Wed");
STR_CONSTR(kThu, "Thu");
STR_CONSTR(kFri, "Fri");
STR_CONSTR(kSat, "Sat");

STR_CONSTR(kJanuary, "January");
STR_CONSTR(kFebruary, "February");
STR_CONSTR(kMarch, "March");
STR_CONSTR(kApril, "April");
STR_CONSTR(kMay, "May");
STR_CONSTR(kJune, "June");
STR_CONSTR(kJuly, "July");
STR_CONSTR(kAugust, "August");
STR_CONSTR(kSeptember, "September");
STR_CONSTR(kOctober, "October");
STR_CONSTR(kNovember, "November");
STR_CONSTR(kDecember, "December");

STR_CONSTR(kJan, "Jan");
STR_CONSTR(kFeb, "Feb");
STR_CONSTR(kMar, "Mar");
STR_CONSTR(kApr, "Apr");
STR_CONSTR(kJun, "Jun");
STR_CONSTR(kJul, "Jul");
STR_CONSTR(kAug, "Aug");
STR_CONSTR(kSep, "Sep");
STR_CONSTR(kOct, "Oct");
STR_CONSTR(kNov, "Nov");
STR_CONSTR(kDec, "Dec");

strref timeDayName[7] = { _SR(kSunday),   _SR(kMonday), _SR(kTuesday), _SR(kWednesday),
                          _SR(kThursday), _SR(kFriday), _SR(kSaturday) };
strref timeDayAbbrev[7] = { _SR(kSun), _SR(kMon), _SR(kTue), _SR(kWed),
                            _SR(kThu), _SR(kFri), _SR(kSat) };
strref timeMonthName[13] = { _SR(kNoMonth), _SR(kJanuary),   _SR(kFebruary), _SR(kMarch),
                             _SR(kApril),   _SR(kMay),       _SR(kJune),     _SR(kJuly),
                             _SR(kAugust),  _SR(kSeptember), _SR(kOctober),  _SR(kNovember),
                             _SR(kDecember) };
strref timeMonthAbbrev[13] = { _SR(kNoMonth), _SR(kJan), _SR(kFeb), _SR(kMar), _SR(kApr),
                               _SR(kMay),     _SR(kJun), _SR(kJul), _SR(kAug), _SR(kSep),
                               _SR(kOct),     _SR(kNov), _SR(kDec) };

_Use_decl_annotations_
bool timeDecompose(TimeParts* out, int64 time)
{
    if (time < 0)
        return false;

    int64 secs;
    int32 days;
    int32 rem;

    // Julian date starts at noon, but the math is much easier if we start at
    // midnight, so sacrifice a tiny bit of range a few hundred thousand years
    // in the future to pretend that the epoch is actually midnight.
    time += 43200000000;

    // time of day is the easy part
    out->usec = time % 1000000;
    secs      = time / 1000000;
    days      = (int32)(secs / SECSPERDAY);
    rem       = secs % SECSPERDAY;
    out->hour = (uint8)(rem / SECSPERHOUR);
    rem %= SECSPERHOUR;
    out->minute = (uint8)(rem / SECSPERMIN);
    out->second = rem % SECSPERMIN;
    out->wday   = (EPOCH_WDAY + days) % DAYSPERWEEK;

    // now the hard part
    // implemented using the Fliegel-Van Flandern algorithm
    int32 p, q, r, s, t, u, v;
    p = days + 68569;
    q = 4 * p / 146097;
    r = p - (146097 * q + 3) / 4;
    s = 4000 * (r + 1) / 1461001;
    t = r - 1461 * s / 4 + 31;
    u = 80 * t / 2447;
    v = u / 11;

    out->year  = 100 * (q - 49) + s + v;
    out->month = u + 2 - 12 * v;
    out->day   = t - 2447 * u / 80;

    int32 ysd = 1461 * (out->year + 4799) / 4 - (3 * ((out->year + 4899) / 100)) / 4 - 31739;
    out->yday = days - ysd;

    return true;
}

_Use_decl_annotations_
int64 timeCompose(TimeParts* parts)
{
    int64 time;

    int32 m1 = (parts->month - 14) / 12;
    int32 y1 = parts->year + 4800;
    int32 j  = 1461 * (y1 + m1) / 4 + 367 * (parts->month - 2 - 12 * m1) / 12 -
        (3 * ((y1 + m1 + 100) / 100)) / 4 + parts->day - 32075;

    if (j < 0)
        return 0;

    time = (int64)j * SECSPERDAY;
    time -= 43200;   // julian half day adjustment

    int32 secs = (parts->hour) * SECSPERHOUR + (parts->minute) * SECSPERMIN + parts->second;
    time += (int64)secs;
    time *= 1000000;
    time += parts->usec;

    return time;
}
