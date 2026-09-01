// The calendar, over proc/time.h's civil()/civil_secs(). Those stay undefined
// in the archive: braam::proc answers them for a program, the compiled-in
// time.cpp for tests.wasm, as cenv_intern.cpp's heap_alloc already is.
#define BRAAM_COMPAT_BUILDING 1
#include "proc/time.h"

#include <string.h>
#include <time.h>

namespace {

const char *const FULL_DAYS[7] = { "Sunday",   "Monday", "Tuesday", "Wednesday",
                                   "Thursday", "Friday", "Saturday" };

const char *const FULL_MONTHS[12] = { "January", "February", "March",     "April",
                                      "May",     "June",     "July",      "August",
                                      "September", "October", "November", "December" };

// TIME_DAYS starts at Thursday; tm_wday starts at Sunday.
Str day_abbrev(int tm_wday)
{
    return TIME_DAYS[(tm_wday + 3) % 7];
}

i64 day_of(i64 secs)
{
    i64 d = secs / 86400;
    return secs % 86400 < 0 ? d - 1 : d;
}

// The nine C fields; tm_gmtoff and tm_zone stay the caller's.
void fill(i64 secs, struct tm *out)
{
    Civil c = civil(secs);

    out->tm_sec   = int(c.sec);
    out->tm_min   = int(c.min);
    out->tm_hour  = int(c.hour);
    out->tm_mday  = int(c.day);
    out->tm_mon   = int(c.month) - 1;
    out->tm_year  = c.year - 1900;
    out->tm_wday  = int((c.weekday + 4) % 7);
    out->tm_isdst = 0;

    Civil jan    = { c.year, 1, 1, 0, 0, 0, 0 };
    out->tm_yday = int(day_of(secs) - day_of(civil_secs(jan)));
}

// The fields as an epoch, every one signed: civil_secs takes the month carry,
// the rest is plain arithmetic, so a negative field normalises.
i64 epoch_of(const struct tm *tm)
{
    i64 mon  = tm->tm_mon;
    i64 year = i64(tm->tm_year) + 1900;
    i64 adj  = mon >= 0 ? mon / 12 : -((-mon + 11) / 12);
    year += adj;
    mon -= adj * 12;

    Civil first = { i32(year), u32(mon) + 1, 1, 0, 0, 0, 0 };
    return civil_secs(first) + (i64(tm->tm_mday) - 1) * 86400 + i64(tm->tm_hour) * 3600 +
           i64(tm->tm_min) * 60 + i64(tm->tm_sec);
}

// 53 weeks, or 52.
int iso_weeks(i64 y)
{
    auto p = [](i64 v) { return int(((v + v / 4 - v / 100 + v / 400) % 7 + 7) % 7); };
    return 52 + (p(y) == 4 || p(y - 1) == 3);
}

// The ISO 8601 week and its year, which is not always tm_year.
void iso_week(const struct tm *tm, int &week, i64 &year)
{
    int mon0 = (tm->tm_wday + 6) % 7; // Monday first
    year     = i64(tm->tm_year) + 1900;
    week     = (tm->tm_yday - mon0 + 10) / 7;
    if (week < 1) {
        year--;
        week = iso_weeks(year);
    } else if (week > iso_weeks(year)) {
        year++;
        week = 1;
    }
}

// Truncating: `n` counts what was wanted, as cfmt.cpp's Sink does.
struct Out {
    char *buf;
    usize cap, n;

    void put(char c)
    {
        if (n < cap)
            buf[n] = c;
        n++;
    }

    void put(const char *s)
    {
        for (; *s; s++)
            put(*s);
    }

    void put(Str s)
    {
        for (usize i = 0; i < s.size(); i++)
            put(s[i]);
    }

    // At least `width` digits; a wider value is not truncated.
    void num(i64 v, int width, char pad = '0')
    {
        char t[24];
        int k = 0;
        bool neg = v < 0;
        u64 m    = neg ? u64(-(v + 1)) + 1 : u64(v);
        do {
            t[k++] = char('0' + m % 10);
            m /= 10;
        } while (m);
        if (neg)
            width--;
        for (int i = k; i < width; i++)
            put(pad);
        if (neg)
            put('-');
        while (k)
            put(t[--k]);
    }
};

void conversion(Out &o, char c, const struct tm *tm);

void put_fmt(Out &o, const char *fmt, const struct tm *tm)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            o.put(*fmt);
            continue;
        }
        fmt++;
        // C's locale modifiers; one locale makes no difference.
        while (*fmt == 'E' || *fmt == 'O')
            fmt++;
        if (!*fmt)
            return;
        conversion(o, *fmt, tm);
    }
}

void conversion(Out &o, char c, const struct tm *tm)
{
    int wday = ((tm->tm_wday % 7) + 7) % 7;
    int mon  = tm->tm_mon >= 0 && tm->tm_mon < 12 ? tm->tm_mon : 0;

    switch (c) {
    case 'a': o.put(day_abbrev(wday)); break;
    case 'A': o.put(FULL_DAYS[wday]); break;
    case 'b':
    case 'h': o.put(TIME_MONTHS[mon]); break;
    case 'B': o.put(FULL_MONTHS[mon]); break;
    case 'c': put_fmt(o, "%a %b %e %H:%M:%S %Y", tm); break;
    case 'C': o.num((i64(tm->tm_year) + 1900) / 100, 2); break;
    case 'd': o.num(tm->tm_mday, 2); break;
    case 'D': put_fmt(o, "%m/%d/%y", tm); break;
    case 'e': o.num(tm->tm_mday, 2, ' '); break;
    case 'F': put_fmt(o, "%Y-%m-%d", tm); break;
    case 'H': o.num(tm->tm_hour, 2); break;
    case 'I': o.num(tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12, 2); break;
    case 'j': o.num(tm->tm_yday + 1, 3); break;
    case 'm': o.num(tm->tm_mon + 1, 2); break;
    case 'M': o.num(tm->tm_min, 2); break;
    case 'n': o.put('\n'); break;
    case 'p': o.put(tm->tm_hour < 12 ? "AM" : "PM"); break;
    case 'r': put_fmt(o, "%I:%M:%S %p", tm); break;
    case 'R': put_fmt(o, "%H:%M", tm); break;
    case 'S': o.num(tm->tm_sec, 2); break;
    case 't': o.put('\t'); break;
    case 'T': put_fmt(o, "%H:%M:%S", tm); break;
    case 'u': o.num(wday == 0 ? 7 : wday, 1); break;
    case 'U': o.num((tm->tm_yday + 7 - wday) / 7, 2); break;
    case 'w': o.num(wday, 1); break;
    case 'W': o.num((tm->tm_yday + 7 - (wday + 6) % 7) / 7, 2); break;
    case 'x': put_fmt(o, "%m/%d/%y", tm); break;
    case 'X': put_fmt(o, "%H:%M:%S", tm); break;
    case 'y': o.num(((i64(tm->tm_year) + 1900) % 100 + 100) % 100, 2); break;
    case 'Y': o.num(i64(tm->tm_year) + 1900, 1); break;
    case '%': o.put('%'); break;

    case 'G':
    case 'g':
    case 'V': {
        int week = 0;
        i64 year = 0;
        iso_week(tm, week, year);
        if (c == 'V')
            o.num(week, 2);
        else if (c == 'G')
            o.num(year, 1);
        else
            o.num((year % 100 + 100) % 100, 2);
        break;
    }

    case 'z': {
        long off = tm->tm_gmtoff;
        o.put(off < 0 ? '-' : '+');
        long m = off < 0 ? -off : off;
        o.num(m / 3600, 2);
        o.num(m / 60 % 60, 2);
        break;
    }
    case 'Z':
        if (tm->tm_zone)
            o.put(tm->tm_zone);
        break;

    // An unknown conversion is written out, as the BSDs do.
    default:
        o.put('%');
        o.put(c);
        break;
    }
}

} // namespace

extern "C" {

struct tm *gmtime_r(const time_t *t, struct tm *out)
{
    if (!t || !out)
        return nullptr;
    out->tm_gmtoff = 0;
    out->tm_zone   = "UTC";
    fill(i64(*t), out);
    return out;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm shared;
    return gmtime_r(t, &shared);
}

time_t timegm(struct tm *tm)
{
    if (!tm)
        return -1;
    i64 secs = epoch_of(tm);
    fill(secs, tm);
    tm->tm_gmtoff = 0;
    tm->tm_zone   = "UTC";
    return time_t(secs);
}

// The fields are read as local, and tm_gmtoff is the whole of what local means.
time_t mktime(struct tm *tm)
{
    if (!tm)
        return -1;
    long off = tm->tm_gmtoff;
    i64 secs = epoch_of(tm) - off;
    fill(secs + off, tm);
    tm->tm_gmtoff = off;
    return time_t(secs);
}

double difftime(time_t a, time_t b)
{
    return double(a - b);
}

char *asctime_r(const struct tm *tm, char *buf)
{
    if (!tm || !buf)
        return nullptr;
    Out o{ buf, 26, 0 };
    put_fmt(o, "%a %b %e %H:%M:%S %Y\n", tm);
    buf[o.n < 25 ? o.n : 25] = '\0';
    return buf;
}

char *asctime(const struct tm *tm)
{
    static char shared[26];
    return asctime_r(tm, shared);
}

size_t strftime(char *buf, size_t cap, const char *fmt, const struct tm *tm)
{
    if (!buf || !fmt || !tm || cap == 0)
        return 0;
    Out o{ buf, cap - 1, 0 };
    put_fmt(o, fmt, tm);
    if (o.n >= cap)
        return 0; // C: the buffer holds an indeterminate value
    buf[o.n] = '\0';
    return o.n;
}

} // extern "C"
