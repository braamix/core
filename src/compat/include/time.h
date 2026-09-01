// <time.h>. The calendar, over proc/time.h's civil() and civil_secs().
//
// There is no local zone in Group A -- the offset comes from clock_now(), a
// coroutine -- so mktime is timegm plus tm_gmtoff, and time/localtime/clock/
// ctime are unavailable, naming the call that does answer them.
#pragma once

#include <stddef.h>
#include <sys/cdefs.h>

#define CLOCKS_PER_SEC 1000000

#ifdef __cplusplus
extern "C" {
#endif

// 64-bit, as musl's is on a 32-bit target.
typedef long long time_t;
typedef long long clock_t;

struct tm {
    int tm_sec;   // 0..60, a leap second included
    int tm_min;   // 0..59
    int tm_hour;  // 0..23
    int tm_mday;  // 1..31
    int tm_mon;   // 0..11
    int tm_year;  // years since 1900
    int tm_wday;  // 0..6, Sunday first
    int tm_yday;  // 0..365
    int tm_isdst; // always 0: no zone database
    // BSD's two, and what %z and %Z read.
    long tm_gmtoff;
    const char *tm_zone;
};

struct tm *gmtime_r(const time_t *t, struct tm *out);
struct tm *gmtime(const time_t *t);

// timegm reads the fields as UTC, mktime as UTC shifted by tm_gmtoff. Both
// normalise what they were given.
time_t timegm(struct tm *tm);
time_t mktime(struct tm *tm);

double difftime(time_t a, time_t b);

// 26 bytes: "Www Mmm dd hh:mm:ss yyyy\n" and a NUL.
char *asctime_r(const struct tm *tm, char *buf);
char *asctime(const struct tm *tm);

size_t strftime(char *buf, size_t cap, const char *fmt, const struct tm *tm)
    __attribute__((format(strftime, 3, 0)));

#ifdef __cplusplus
}
#endif

#ifndef BRAAM_COMPAT_BUILDING

// Declared so the diagnostic can name the fix. None of these is defined.
time_t time(time_t *t) BRAAM_ABSENT("co_await clock_now() (proc/io.h) for the wall clock");
clock_t clock(void) BRAAM_ABSENT("proc_now() (proc/host.h) for milliseconds since boot");
struct tm *localtime(const time_t *t)
    BRAAM_ABSENT("no local zone: set tm_gmtoff from clock_now()'s tz_min, then gmtime_r");
struct tm *localtime_r(const time_t *t, struct tm *out)
    BRAAM_ABSENT("no local zone: set tm_gmtoff from clock_now()'s tz_min, then gmtime_r");
char *ctime(const time_t *t) BRAAM_ABSENT("asctime_r(gmtime_r(t, &tm), buf)");
char *ctime_r(const time_t *t, char *buf) BRAAM_ABSENT("asctime_r(gmtime_r(t, &tm), buf)");

#endif // BRAAM_COMPAT_BUILDING
