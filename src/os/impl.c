#include "base/core.h"

#if OS_LINUX
    #include "os/linux/fs.c"
    #include "os/linux/time.c"
    #include "os/linux/info.c"
    #include "os/linux/threads.c"
#else
    #error "Bad os."
#endif

Time os_ms_to_time (Millisec ms) {
    return (Time){
        .hours    = ms / 3600000,
        .minutes  = (ms / 60000) % 60,
        .seconds  = (ms / 1000) % 60,
        .mseconds = ms % 1000,
    };
}

Millisec os_time_to_ms (Time time) {
    return time.hours*3600000 +
           time.minutes*60000 +
           time.seconds*1000 +
           time.mseconds;
}

Bool os_is_date_ymd_valid (Date date) {
    if (date.month < 1 || date.month > 12) return false;
    return (date.day >= 1) && (date.day <= os_days_in_month(date.year, date.month));
}

I32 os_date_cmp (Date a, Date b) {
    if (a.year < b.year) return -1;
    if (a.year > b.year) return 1;
    if (a.month < b.month) return -1;
    if (a.month > b.month) return 1;
    if (a.day < b.day) return -1;
    if (a.day > b.day) return 1;
    return 0;
}

String os_date_to_str (Mem *mem, Date d) {
    return astr_fmt(mem, "%04u-%02u-%02u", d.year, d.month, d.day);
}

Date os_str_to_date (String s) {
    tmem_new(tm);

    if (s.count != 10) return (Date){};
    if (s.data[4] != '-' || s.data[7] != '-') return (Date){};

    String ys = str_slice(s, 0, 4);
    String ms = str_slice(s, 5, 2);
    String ds = str_slice(s, 8, 2);

    U64 y = 0; Bool ye = str_to_u64(cstr(tm, ys), &y, 10);
    U64 m = 0; Bool me = str_to_u64(cstr(tm, ms), &m, 10);
    U64 d = 0; Bool de = str_to_u64(cstr(tm, ds), &d, 10);

    if (!ye || !me || !de) return (Date){};
    Date date = { .year=y, .month=m, .day=d };
    if (! os_is_date_ymd_valid(date)) date.month = 0;
    return date;
}

Date os_resolve_date_spec (DateSpec *spec) {
    switch (spec->tag) {
    case DATE_SPEC_NONE: return (Date){};
    case DATE_SPEC_TODAY: return os_get_date();
    case DATE_SPEC_SPECIFIC: return spec->date;
    }
    badpath;
}
