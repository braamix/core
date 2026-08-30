#include "time.h"

const Str TIME_MONTHS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// 1970-01-01 was a Thursday, and the weekday is days mod 7 from there.
const Str TIME_DAYS[7] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };

// Days since 1970-01-01 to a calendar date, by way of an era of 400 years —
// the usual branch-free civil_from_days, which needs no leap-year table.
Civil civil(i64 secs)
{
    i64 days = secs / 86400;
    i64 rem  = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }

    Civil c;
    c.weekday = u32(((days % 7) + 7) % 7);
    c.hour    = u32(rem / 3600);
    c.min     = u32((rem / 60) % 60);
    c.sec     = u32(rem % 60);

    i64 z   = days + 719468;
    i64 era = (z >= 0 ? z : z - 146096) / 146097;
    u64 doe = u64(z - era * 146097);
    u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    u64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    u64 mp  = (5 * doy + 2) / 153;
    c.day   = u32(doy - (153 * mp + 2) / 5 + 1);
    c.month = u32(mp < 10 ? mp + 3 : mp - 9);
    c.year  = i32(i64(yoe) + era * 400) + (c.month <= 2 ? 1 : 0);
    return c;
}

// days_from_civil, the same era arithmetic run backwards.
i64 civil_secs(const Civil &c)
{
    // Carry the month into the year: month 0 is December of the year before.
    i64 mz   = i64(c.month) - 1;
    i64 adj  = mz >= 0 ? mz / 12 : -((-mz + 11) / 12);
    i64 y    = i64(c.year) + adj;
    i64 m    = mz - adj * 12 + 1;

    y -= m <= 2;
    i64 era  = (y >= 0 ? y : y - 399) / 400;
    u64 yoe  = u64(y - era * 400);
    u64 doy  = u64((153 * u64(m > 2 ? m - 3 : m + 9) + 2) / 5) + c.day - 1;
    u64 doe  = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    i64 days = era * 146097 + i64(doe) - 719468;

    return days * 86400 + i64(c.hour) * 3600 + i64(c.min) * 60 + i64(c.sec);
}
