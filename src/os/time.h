#pragma once

#include "base/core.h"

istruct (Date) {
    U32 year;
    U32 month; // 1-indexed
    U32 day; // 1-indexed
    U32 wday; // [0, 6] (Sunday = 0)
};

istruct (Time) {
    U32 hours;
    U32 minutes;
    U32 seconds;
    U32 mseconds;
};

ienum (DateSpecTag, U8) {
    DATE_SPEC_NONE,
    DATE_SPEC_TODAY,
    DATE_SPEC_SPECIFIC,
};

istruct (DateSpec) {
    DateSpecTag tag;
    Date date;
};

typedef U64 Millisec;
typedef U64 Seconds;
typedef U64 Minutes;

Date     os_get_date          ();
U32      os_first_weekday     (U32 year, U32 month);
U32      os_days_in_month     (U32 year, U32 month);
Time     os_get_wall_time     ();
U64      os_get_time_ms       ();
Void     os_sleep_ms          (Millisec);
Time     os_ms_to_time        (Millisec);
Millisec os_time_to_ms        (Time);
Bool     os_is_date_ymd_valid (Date);
I32      os_date_cmp          (Date, Date);
Date     os_resolve_date_spec (DateSpec *);

// These deal with a string in the ISO format (year/month/day, 2000-01-01).
// The os_str_to_date() func will set Date.month == 0 if the input is invalid.
String   os_date_to_str       (Mem *, Date);
Date     os_str_to_date       (String);
