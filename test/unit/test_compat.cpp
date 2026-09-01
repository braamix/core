// The port kit's Group A. Every case names the divergence it exists to pin:
// the seven ports each wrote these, and disagreed about most of them.
#include <arpa/inet.h>
#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <fenv.h>
#include <fnmatch.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include "compat/cenv.h"
#include "compat/cerr.h"
#include "compat/cmode.h"
#include "harness.h"
#include "kernel/alloc.h"
#include "kernel/sysabi.h"

namespace {

// A key with a payload the comparator ignores, so ties are visible.
struct Keyed {
    int key;
    char tag;
};

int by_key(const void *a, const void *b)
{
    int x = static_cast<const Keyed *>(a)->key;
    int y = static_cast<const Keyed *>(b)->key;
    return x < y ? -1 : x > y ? 1 : 0;
}

int by_int(const void *a, const void *b)
{
    int x = *static_cast<const int *>(a);
    int y = *static_cast<const int *>(b);
    return x < y ? -1 : x > y ? 1 : 0;
}

void test_mem()
{
    char buf[16];
    memset(buf, 'x', sizeof(buf));
    CHECK(buf[0] == 'x' && buf[15] == 'x');

    // Overlap in both directions, and the d == s guard iconv's copy dropped.
    char ov[8] = { '0', '1', '2', '3', '4', '5', '6', '\0' };
    memmove(ov + 1, ov, 6);
    CHECK(memcmp(ov, "0012345", 7) == 0);
    char up[8] = { '0', '1', '2', '3', '4', '5', '6', '\0' };
    memmove(up, up + 1, 6);
    CHECK(memcmp(up, "1234566", 7) == 0);
    char same[4] = { 'a', 'b', 'c', '\0' };
    memmove(same, same, 3);
    CHECK(memcmp(same, "abc", 3) == 0);

    // n == 0 touches nothing.
    memcpy(nullptr, nullptr, 0);
    CHECK_EQ(memcmp("a", "b", 0), 0);
    CHECK(memchr("abc", 'c', 3) != nullptr);
    CHECK(memchr("abc", 'z', 3) == nullptr);
}

void test_str()
{
    CHECK_EQ(strlen(""), 0);
    CHECK_EQ(strlen("abc"), 3);
    CHECK_EQ(strnlen("abc", 2), 2);

    // strncpy pads and does not terminate a full copy.
    char pad[6];
    memset(pad, 'z', sizeof(pad));
    strncpy(pad, "ab", 5);
    CHECK(pad[0] == 'a' && pad[2] == '\0' && pad[4] == '\0' && pad[5] == 'z');
    char full[4];
    strncpy(full, "abcd", 4);
    CHECK(memcmp(full, "abcd", 4) == 0);

    CHECK(strcmp("a", "b") < 0);
    CHECK(strcmp("b", "a") > 0);
    CHECK_EQ(strcmp("ab", "ab"), 0);
    // High bytes compare unsigned.
    CHECK(strcmp("\xff", "a") > 0);
    CHECK_EQ(strncmp("abc", "abd", 2), 0);

    CHECK(strchr("abc", 'b') != nullptr);
    CHECK(strchr("abc", '\0') != nullptr); // the terminator is found, per C
    CHECK(strrchr("abcabc", 'b')[-1] == 'a');
    CHECK(strstr("hello", "ll") != nullptr);
    CHECK(strstr("hello", "") != nullptr);
    CHECK(strstr("hello", "z") == nullptr);
    CHECK(strcasestr("HeLLo", "ell") != nullptr);
    CHECK_EQ(strcasecmp("ABC", "abc"), 0);
    CHECK_EQ(strncasecmp("ABCz", "abcZ", 3), 0);

    CHECK_EQ(strspn("aabbc", "ab"), 4);
    CHECK_EQ(strcspn("aabbc", "c"), 4);
    CHECK(strpbrk("hello", "lo") != nullptr);
    CHECK(strpbrk("hello", "z") == nullptr);

    // strlcpy/strlcat return what they tried to build, so truncation shows.
    char s[4];
    CHECK_EQ(strlcpy(s, "abcdef", sizeof(s)), 6);
    CHECK(memcmp(s, "abc", 4) == 0);
    char c[8] = "ab";
    CHECK_EQ(strlcat(c, "cd", sizeof(c)), 4);
    CHECK(memcmp(c, "abcd", 5) == 0);

    char tok[] = "  a b  c ";
    char *save = nullptr;
    CHECK(strcmp(strtok_r(tok, " ", &save), "a") == 0);
    CHECK(strcmp(strtok_r(nullptr, " ", &save), "b") == 0);
    CHECK(strcmp(strtok_r(nullptr, " ", &save), "c") == 0);
    CHECK(strtok_r(nullptr, " ", &save) == nullptr);

    char *d = strdup("dup");
    CHECK(d && strcmp(d, "dup") == 0);
    free(d);
    char *n = strndup("abcdef", 3);
    CHECK(n && strcmp(n, "abc") == 0);
    free(n);
}

void test_ctype()
{
    // isspace matches kernel/text.h's is_space byte for byte: iconv's strtol
    // skipped only two of these six.
    for (int i = 0; i < 256; i++) {
        bool sp = i == ' ' || i == '\t' || i == '\n' || i == '\v' || i == '\f' || i == '\r';
        CHECK_EQ(isspace(i) != 0, sp);
    }
    CHECK(isdigit('5') && !isdigit('a'));
    CHECK(isalpha('a') && isalpha('Z') && !isalpha('0'));
    CHECK(isalnum('0') && isalnum('z'));
    CHECK(isxdigit('f') && isxdigit('F') && !isxdigit('g'));
    CHECK(ispunct('!') && !ispunct('a') && !ispunct(' '));
    CHECK(iscntrl('\n') && iscntrl(127) && !iscntrl('a'));
    CHECK(isprint(' ') && !isprint('\n'));
    CHECK(isgraph('a') && !isgraph(' '));
    CHECK(isblank(' ') && isblank('\t') && !isblank('\n'));
    CHECK_EQ(toupper('a'), 'A');
    CHECK_EQ(tolower('A'), 'a');
    CHECK_EQ(toupper('1'), '1');
}

void test_alloc_compat()
{
    // No block header any more, so every block keeps the allocator's alignment.
    void *p = malloc(1);
    CHECK(p && (usize(p) & 15) == 0);
    CHECK(heap_usable_size(p) >= 1);
    free(p);

    // malloc(0) is a real block: a port that reads null as failure must not
    // see one on success.
    void *z = malloc(0);
    CHECK(z != nullptr);
    free(z);

    // realloc grows in place while the size class has room, and copies when it
    // does not. Capacity comes from the allocator, so it is never stale.
    char *g = static_cast<char *>(malloc(8));
    memcpy(g, "1234567", 8);
    usize had = heap_usable_size(g);
    char *b   = static_cast<char *>(realloc(g, had));
    CHECK(b == g); // within the class: no copy
    char *big = static_cast<char *>(realloc(b, 4096));
    CHECK(big && memcmp(big, "1234567", 8) == 0);
    free(big);

    CHECK(realloc(nullptr, 8) != nullptr);

    // calloc zeroes, and refuses an overflowing product rather than wrapping.
    int *c = static_cast<int *>(calloc(4, sizeof(int)));
    CHECK(c && c[0] == 0 && c[3] == 0);
    free(c);
    CHECK(calloc(usize(-1) / 2, 4) == nullptr);
    CHECK(reallocarray(nullptr, usize(-1) / 2, 4) == nullptr);
}

void test_strtol()
{
    char *end = nullptr;

    CHECK_EQ(strtol("42", &end, 10), 42);
    CHECK(*end == '\0');
    CHECK_EQ(strtol("-42", nullptr, 10), -42);
    CHECK_EQ(strtol("  \n\v\f\r7", nullptr, 10), 7); // all six, not zip's two
    CHECK_EQ(strtol("0x1f", nullptr, 0), 31);
    CHECK_EQ(strtol("0b101", nullptr, 0), 5);
    CHECK_EQ(strtol("017", nullptr, 0), 15);
    CHECK_EQ(strtol("ff", nullptr, 16), 255);

    // A bare "0x" converts the 0 and leaves the x: zip's consumed the x.
    CHECK_EQ(strtol("0x", &end, 0), 0);
    CHECK(*end == 'x');

    // No conversion: endptr goes back to the start.
    const char *none = "  zz";
    CHECK_EQ(strtol(none, &end, 10), 0);
    CHECK(end == none);

    // Saturation and ERANGE, which none of the three ports' copies had.
    errno = 0;
    CHECK_EQ(strtol("99999999999", nullptr, 10), LONG_MAX);
    CHECK_EQ(errno, ERANGE);
    errno = 0;
    CHECK_EQ(strtol("-99999999999", nullptr, 10), LONG_MIN);
    CHECK_EQ(errno, ERANGE);
    errno = 0;
    CHECK(strtoul("99999999999", nullptr, 10) == ULONG_MAX);
    CHECK_EQ(errno, ERANGE);

    // strtoul negates modulo the type: iconv's returned 0 here.
    errno = 0;
    CHECK(strtoul("-1", nullptr, 10) == ULONG_MAX);
    CHECK_EQ(errno, 0);

    CHECK(strtoull("18446744073709551615", nullptr, 10) == ~0ull);
    CHECK(strtoll("-9223372036854775808", nullptr, 10) == (-9223372036854775807LL - 1));

    CHECK_EQ(atoi("  -12x"), -12);
}

void test_sort()
{
    int one[1] = { 7 };
    qsort(one, 1, sizeof(int), by_int);
    CHECK_EQ(one[0], 7);
    qsort(one, 0, sizeof(int), by_int); // n == 0 must not walk

    int two[2] = { 2, 1 };
    qsort(two, 2, sizeof(int), by_int);
    CHECK(two[0] == 1 && two[1] == 2);

    // Adversarial: reversed, which is the insertion sort's worst case.
    constexpr int N = 500;
    static int big[N];
    for (int i = 0; i < N; i++)
        big[i] = N - i;
    qsort(big, N, sizeof(int), by_int);
    bool sorted = true;
    for (int i = 1; i < N; i++)
        if (big[i - 1] > big[i])
            sorted = false;
    CHECK(sorted);

    int key = 250;
    CHECK(bsearch(&key, big, N, sizeof(int), by_int) != nullptr);
    key = 9999;
    CHECK(bsearch(&key, big, N, sizeof(int), by_int) == nullptr);
    CHECK(bsearch(&key, big, 0, sizeof(int), by_int) == nullptr);
}

// mergesort is the stable one. qsort is deliberately not, so nothing here
// asserts that it is: a port that needs stability changes the call.
void test_mergesort()
{
    // Equal keys, distinct payloads: stability is that the payloads come back
    // in the order they went in.
    Keyed k[8] = { { 1, 'a' }, { 0, 'b' }, { 1, 'c' }, { 0, 'd' },
                   { 1, 'e' }, { 0, 'f' }, { 1, 'g' }, { 0, 'h' } };
    CHECK_EQ(mergesort(k, 8, sizeof k[0], by_key), 0);
    char got[9];
    for (int i = 0; i < 8; i++)
        got[i] = k[i].tag;
    got[8] = '\0';
    CHECK(strcmp(got, "bdfhaceg") == 0);

    int one[1] = { 7 };
    CHECK_EQ(mergesort(one, 1, sizeof(int), by_int), 0);
    CHECK_EQ(one[0], 7);
    CHECK_EQ(mergesort(one, 0, sizeof(int), by_int), 0);

    // An odd number of passes has to copy back, which is where a ping-pong
    // merge sort goes wrong: three elements is two passes, five is three.
    for (int n = 2; n <= 9; n++) {
        static int a[9];
        for (int i = 0; i < n; i++)
            a[i] = n - i;
        CHECK_EQ(mergesort(a, usize(n), sizeof(int), by_int), 0);
        bool up = true;
        for (int i = 1; i < n; i++)
            if (a[i - 1] > a[i])
                up = false;
        CHECK(up);
    }

    // BSD's error convention, which is why this is not simply a stabler qsort.
    errno = 0;
    CHECK_EQ(mergesort(one, 1, 0, by_int), u32(-1));
    CHECK_EQ(errno, EINVAL);
    errno = 0;
}

// getenv interned per name, in place of the one static buffer five ports
// shared. env_intern is the half that has no environment under it.
void test_env_intern()
{
    char *a = env_intern("A", "1");
    char *b = env_intern("B", "2");
    CHECK(a != nullptr && b != nullptr);
    CHECK(a != b);
    // The bug this exists for: the second call must not have moved the first.
    CHECK(strcmp(a, "1") == 0);
    CHECK(strcmp(b, "2") == 0);

    // Same name, same pointer — a caller may compare them.
    CHECK(env_intern("A", "1") == a);

    // Unset and set-to-empty are one answer here: proc_env reports both empty.
    CHECK(env_intern("C", Str()) == nullptr);

    // Every old copy truncated at 512 (128 in iconv's).
    static char big[700];
    for (usize i = 0; i < sizeof big; i++)
        big[i] = char('a' + i % 26);
    char *v = env_intern("BIG", Str(big, sizeof big));
    CHECK(v != nullptr && strlen(v) == sizeof big);
}

void test_errno_bridge()
{
    CHECK_EQ(errno_of(Error::NotFound), ENOENT);
    CHECK_EQ(errno_of(Error::NoMemory), ENOMEM);
    CHECK_EQ(errno_of(Error::Perm), EACCES);
    // Both mean "abandoned by a signal".
    CHECK_EQ(errno_of(Error::Cancelled), EINTR);
    CHECK_EQ(errno_of(Error::Intr), EINTR);

    // Round-trips for everything with a distinct number.
    Error round[] = { Error::Invalid,     Error::NoMemory, Error::NotFound, Error::Exists,
                      Error::NotDir,      Error::IsDir,    Error::Io,       Error::Again,
                      Error::Unsupported, Error::Closed,   Error::NotEmpty, Error::Loop };
    for (Error e : round)
        CHECK(error_of(errno_of(e)) == e);

    CHECK(strcmp(strerror(ENOENT), "ENOENT") == 0);
    CHECK(strcmp(strerror(ERANGE), "ERANGE") == 0);

    // The two BSD names citrus needs; EFTYPE must stay distinct from EINVAL.
    CHECK(error_of(EOPNOTSUPP) == Error::Unsupported);
    CHECK(error_of(EFTYPE) == Error::Invalid);
    CHECK(EFTYPE != EINVAL && EFTYPE != EOPNOTSUPP);
    CHECK(strcmp(strerror(EFTYPE), "EFTYPE") == 0);
    CHECK(strcmp(strerror(EOPNOTSUPP), "EOPNOTSUPP") == 0);

    errno = 0;
    CHECK_EQ(fail_with(Error::IsDir), -1);
    CHECK_EQ(errno, EISDIR);
    errno = 0;
}

void eq(Str want, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // The format is the expression: it is what names the failing case.
    test_check(Str(buf, usize(n)) == want, fmt, __FILE_NAME__, __LINE__);
}

void test_printf()
{
    eq("", "");
    eq("hi", "hi");
    eq("100%", "100%%");

    // Integers, and every length modifier honoured. iconv's engine swallowed
    // l and z and read an int, misreading every 64-bit argument.
    eq("42", "%d", 42);
    eq("-42", "%d", -42);
    eq("2147483647", "%ld", 2147483647L);
    eq("9223372036854775807", "%lld", 9223372036854775807LL);
    eq("4294967295", "%zu", usize(4294967295u));
    eq("18446744073709551615", "%llu", ~0ull);
    eq("-1", "%hhd", -1);
    eq("255", "%hhu", 255);

    // The full flag set.
    eq("+42", "%+d", 42);
    eq(" 42", "% d", 42);
    eq("  42", "%4d", 42);
    eq("42  ", "%-4d", 42);
    eq("0042", "%04d", 42);
    eq("-042", "%04d", -42);
    eq("ff", "%x", 255);
    eq("FF", "%X", 255);
    eq("0xff", "%#x", 255);
    eq("777", "%o", 511);
    eq("0777", "%#o", 511);
    // The 0 flag yields to precision, per C.
    eq("  0042", "%6.4d", 42);

    // A negative * width is a left-justify, which uemacs's engine dropped.
    eq("  42", "%*d", 4, 42);
    eq("42  ", "%*d", -4, 42);
    eq("42", "%.*d", -1, 42);

    // Precision applied, not parsed and discarded as zip's was.
    eq("abc", "%s", "abc");
    eq("ab", "%.2s", "abc");
    eq("   ab", "%5.2s", "abc");
    eq("ab   ", "%-5.2s", "abc");
    eq("00042", "%.5d", 42);
    eq("(null)", "%s", static_cast<const char *>(nullptr));
    // A precision may cap a string that is not terminated within it.
    const char raw[3] = { 'x', 'y', 'z' };
    eq("xyz", "%.3s", raw);

    eq("A", "%c", 'A');
    eq("  A", "%3c", 'A');

    // Floats, over math/ftoa.h's engine.
    eq("3.140000", "%f", 3.14);
    eq("3.14", "%.2f", 3.14);
    eq("-3.14", "%.2f", -3.14);
    eq("  3.14", "%6.2f", 3.14);
    eq("+3.14", "%+.2f", 3.14);
    eq("3.14", "%g", 3.14);
    eq("0.000000", "%f", 0.0);

    // The return value is C99's: what would have been written.
    char small[4];
    CHECK_EQ(snprintf(small, sizeof(small), "abcdef"), 6);
    CHECK(Str(small) == "abc");
    CHECK_EQ(snprintf(nullptr, 0, "%d", 12345), 5);
    CHECK_EQ(snprintf(small, 1, "abc"), 3);
    CHECK(small[0] == '\0');
}

void test_endian()
{
    CHECK_EQ(BYTE_ORDER, LITTLE_ENDIAN);
    CHECK(htonl(0x01020304u) == 0x04030201u);
    CHECK(ntohl(0x04030201u) == 0x01020304u);
    CHECK(htons(0x0102) == 0x0201);
    CHECK(ntohs(0x0201) == 0x0102);
    // The le* pair is identity here; the be* pair swaps.
    CHECK(htole32(0x01020304u) == 0x01020304u);
    CHECK(htobe32(0x01020304u) == 0x04030201u);
    CHECK(be16toh(0x0102) == 0x0201);
}

void test_math_header()
{
    // <math.h> reaches braam::math, which a PORT target links through the kit.
    CHECK(sqrt(144.0) == 12.0);
    CHECK(fabs(-2.5) == 2.5);
    CHECK(floor(-1.5) == -2.0);
    CHECK(isnan(NAN) && !isnan(1.0));
    CHECK(isinf(INFINITY));
    CHECK_EQ(FLT_EVAL_METHOD, 0);

    // wasm has no floating-point environment: every call succeeds, doing
    // nothing, and no exception is ever flagged.
    CHECK_EQ(fetestexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(feclearexcept(FE_ALL_EXCEPT), 0);
    CHECK_EQ(fegetround(), FE_TONEAREST);
}

// The three names that were declared and defined nowhere, so a caller got a
// link error rather than an answer.
void test_strtod()
{
    char *end = nullptr;
    CHECK(strtod("3.5", &end) == 3.5);
    CHECK(*end == '\0');

    CHECK(strtod("  -2.5e2xyz", &end) == -250.0);
    CHECK(Str(end) == "xyz");

    // No conversion leaves the endptr at the string, which is how a caller
    // tells "0" from "not a number".
    const char *bad = "zzz";
    CHECK(strtod(bad, &end) == 0.0);
    CHECK(end == bad);

    // strtol's grammar, at the other radix: hex floats and the two names.
    CHECK(strtod("0x1p4", &end) == 16.0);
    CHECK(isinf(strtod("inf", nullptr)));
    CHECK(isnan(strtod("nan", nullptr)));

    // ERANGE, which scan_f64 had no way to report before.
    errno = 0;
    CHECK(isinf(strtod("1e400", &end)));
    CHECK_EQ(errno, ERANGE);
    errno = 0;

    CHECK(atof("1.25") == 1.25);
    CHECK(strtof("3.5", &end) == 3.5f);
    CHECK(*end == '\0');
}

void test_time()
{
    // 2009-02-13 23:31:30 UTC, a Friday.
    const time_t stamp = 1234567890;

    struct tm tm;
    CHECK(gmtime_r(&stamp, &tm) == &tm);
    CHECK_EQ(tm.tm_year, 109);
    CHECK_EQ(tm.tm_mon, 1);
    CHECK_EQ(tm.tm_mday, 13);
    CHECK_EQ(tm.tm_hour, 23);
    CHECK_EQ(tm.tm_min, 31);
    CHECK_EQ(tm.tm_sec, 30);
    CHECK_EQ(tm.tm_wday, 5);
    CHECK_EQ(tm.tm_yday, 43);
    CHECK_EQ(timegm(&tm), u32(stamp));

    char buf[64];
    auto fmt = [&](const char *f) {
        strftime(buf, sizeof buf, f, &tm);
        return Str(buf);
    };
    CHECK(fmt("%Y-%m-%d %H:%M:%S") == "2009-02-13 23:31:30");
    CHECK(fmt("%a %b %e") == "Fri Feb 13");
    CHECK(fmt("%A %B") == "Friday February");
    CHECK(fmt("%C%y %j %u %w") == "2009 044 5 5");
    CHECK(fmt("%I %p") == "11 PM");
    CHECK(fmt("%D|%F|%T|%R") == "02/13/09|2009-02-13|23:31:30|23:31");
    CHECK(fmt("%G-W%V %U %W") == "2009-W07 06 06");
    CHECK(fmt("%z %Z 100%%") == "+0000 UTC 100%");
    CHECK(fmt("%c") == "Fri Feb 13 23:31:30 2009");

    // Truncation is 0, not a short answer: the buffer holds no whole result.
    CHECK_EQ(strftime(buf, 4, "%Y", &tm), 0);
    CHECK_EQ(strftime(buf, 5, "%Y", &tm), 4);

    char ac[26];
    CHECK(Str(asctime_r(&tm, ac)) == "Fri Feb 13 23:31:30 2009\n");

    // mktime reads the fields through tm_gmtoff, which is the whole of what
    // local means without a zone database.
    struct tm loc = tm;
    loc.tm_hour   = 18;
    loc.tm_gmtoff = -5 * 3600;
    CHECK_EQ(mktime(&loc), u32(stamp));
    CHECK_EQ(loc.tm_hour, 18);
    CHECK_EQ(loc.tm_gmtoff, -5 * 3600);

    // Normalisation, in both directions -- civil_secs takes the month carry
    // and the rest is signed arithmetic.
    struct tm n = {};
    n.tm_year   = 109;
    n.tm_mon    = 12; // the thirteenth month
    n.tm_mday   = 1;
    timegm(&n);
    CHECK_EQ(n.tm_year, 110);
    CHECK_EQ(n.tm_mon, 0);

    n         = tm;
    n.tm_mon  = 0;
    n.tm_mday = 32; // 1 February
    timegm(&n);
    CHECK_EQ(n.tm_mon, 1);
    CHECK_EQ(n.tm_mday, 1);

    n         = tm;
    n.tm_mon  = -1; // December of the year before
    n.tm_mday = 1;
    timegm(&n);
    CHECK_EQ(n.tm_year, 108);
    CHECK_EQ(n.tm_mon, 11);

    n         = tm;
    n.tm_mday = 1;
    n.tm_hour = 0;
    n.tm_min  = 0;
    n.tm_sec  = -1; // the last second of the month before
    timegm(&n);
    CHECK_EQ(n.tm_mon, 0);
    CHECK_EQ(n.tm_mday, 31);
    CHECK_EQ(n.tm_sec, 59);

    CHECK(difftime(stamp + 60, stamp) == 60.0);
}

void test_wide()
{
    const char *poo = "\xf0\x9f\x92\xa9"; // U+1F4A9, four bytes

    // The whole reason the state is iconv's and not le's: a sequence split
    // across four calls, with the bytes held between them.
    mbstate_t st{};
    wchar_t wc = 0;
    CHECK_EQ(mbrtowc(&wc, poo, 1, &st), usize(-2));
    CHECK(!mbsinit(&st));
    CHECK_EQ(mbrtowc(&wc, poo + 1, 1, &st), usize(-2));
    CHECK_EQ(mbrtowc(&wc, poo + 2, 1, &st), usize(-2));
    CHECK_EQ(mbrtowc(&wc, poo + 3, 1, &st), 1);
    CHECK(wc == 0x1f4a9);
    CHECK(mbsinit(&st));

    CHECK_EQ(mbrtowc(&wc, poo, 4, nullptr), 4);
    CHECK_EQ(mbrtowc(&wc, "\0x", 2, nullptr), 0); // a NUL is 0, not 1

    // Malformed is EILSEQ. utf8_decode answers U+FFFD for every bad form, so
    // the two are told apart by re-encoding what came back.
    errno = 0;
    CHECK_EQ(mbrtowc(&wc, "\xff\xfe", 2, nullptr), usize(-1));
    CHECK_EQ(errno, EILSEQ);
    errno = 0;
    CHECK_EQ(mbtowc(&wc, "\xc3", 1), -1);
    errno = 0;

    // And a real U+FFFD is a rune, which le's copy could only guess at.
    CHECK_EQ(mbtowc(&wc, "\xef\xbf\xbd", 3), 3);
    CHECK(wc == 0xfffd);

    char out[4];
    CHECK_EQ(wcrtomb(out, wchar_t(0x1f4a9), nullptr), 4);
    CHECK(memcmp(out, poo, 4) == 0);
    CHECK_EQ(wctomb(out, wchar_t(0xe9)), 2);
    CHECK_EQ(mblen("\xc3\xa9", 2), 2);

    wchar_t wbuf[8];
    CHECK_EQ(mbstowcs(wbuf, "a\xc3\xa9z", 8), 3);
    CHECK(wbuf[0] == 'a' && wbuf[1] == 0xe9 && wbuf[2] == 'z' && wbuf[3] == 0);
    CHECK_EQ(wcslen(wbuf), 3);
    char mb[8];
    CHECK_EQ(wcstombs(mb, wbuf, sizeof mb), 4);
    CHECK(memcmp(mb, "a\xc3\xa9z", 5) == 0);

    // Kuhn's widths. The cell grid is one column per rune and disagrees about
    // the third of these; doc/Compat.md says so.
    CHECK_EQ(wcwidth(L'a'), 1);
    CHECK_EQ(wcwidth(wchar_t(0)), 0);
    CHECK_EQ(wcwidth(wchar_t(0x0301)), 0); // combining acute
    CHECK_EQ(wcwidth(wchar_t(0x4e00)), 2); // CJK
    CHECK_EQ(wcwidth(wchar_t(0x1b)), -1);  // ESC
    CHECK_EQ(wcswidth(L"a一", 2), 3);
}

void test_wctype()
{
    CHECK(iswalpha(L'a') && iswalpha(wchar_t(0x0410))); // Cyrillic А
    CHECK(iswalpha(wchar_t(0x4e00)));                   // a letter with no case
    CHECK(!iswalpha(L'1') && iswdigit(L'1') && iswalnum(L'1'));
    CHECK(iswupper(wchar_t(0x0410)) && iswlower(wchar_t(0x0430)));
    CHECK(towlower(wchar_t(0x0410)) == wint_t(0x0430));
    CHECK(towupper(L'a') == wint_t(L'A'));
    CHECK(iswspace(L' ') && iswspace(wchar_t(0x3000)));
    CHECK(iswblank(L'\t') && !iswblank(L'\n'));
    CHECK(iswprint(L'a') && !iswprint(wchar_t(0x1b)));
    CHECK(iswcntrl(wchar_t(0x1b)) && iswpunct(L',') && iswgraph(L','));
    CHECK(iswxdigit(L'f') && !iswxdigit(L'g'));

    // One locale, so a transform is the function rather than a name.
    wctrans_t up = wctrans("toupper");
    CHECK(up != nullptr && wctrans("tolower") != nullptr);
    CHECK(towctrans(L'a', up) == wint_t(L'A'));
    CHECK(wctrans("nope") == nullptr);
    CHECK(towctrans(L'a', nullptr) == wint_t(L'a'));
}

void test_fnmatch()
{
    CHECK_EQ(fnmatch("*.c", "main.c", 0), 0);
    CHECK_EQ(fnmatch("*.c", "main.h", 0), FNM_NOMATCH);
    CHECK_EQ(fnmatch("?at", "cat", 0), 0);
    CHECK_EQ(fnmatch("[a-c]at", "bat", 0), 0);
    CHECK_EQ(fnmatch("[!a-c]at", "bat", 0), FNM_NOMATCH);
    CHECK_EQ(fnmatch("[^a-c]at", "dat", 0), 0);
    CHECK_EQ(fnmatch("[]x]", "]", 0), 0); // a leading ']' is a literal
    CHECK_EQ(fnmatch("[[:digit:]]x", "7x", 0), 0);
    CHECK_EQ(fnmatch("[[:digit:]]x", "ax", 0), FNM_NOMATCH);

    // An unterminated '[' is a literal, as sh's matcher has it too.
    CHECK_EQ(fnmatch("[abc", "[abc", 0), 0);

    // Backtracking, not recursion: a pattern of stars cannot reach the stack.
    CHECK_EQ(fnmatch("********************a", "aaaaaaaaaaaaaaaaaaaaaaaaab", 0), FNM_NOMATCH);

    // le's copy ignored the backslash, and C does not. P4 inherits the change.
    CHECK_EQ(fnmatch("\\*", "*", 0), 0);
    CHECK_EQ(fnmatch("\\*", "x", 0), FNM_NOMATCH);
    CHECK_EQ(fnmatch("\\*", "\\x", FNM_NOESCAPE), 0);

    CHECK_EQ(fnmatch("a/b", "a/b", FNM_PATHNAME), 0);
    CHECK_EQ(fnmatch("a*b", "a/b", FNM_PATHNAME), FNM_NOMATCH);
    CHECK_EQ(fnmatch("a*b", "a/b", 0), 0);
    CHECK_EQ(fnmatch("*", ".x", FNM_PERIOD), FNM_NOMATCH);
    CHECK_EQ(fnmatch(".*", ".x", FNM_PERIOD), 0);
    CHECK_EQ(fnmatch("*/*", "a/.x", FNM_PATHNAME | FNM_PERIOD), FNM_NOMATCH);
    CHECK_EQ(fnmatch("ABC", "abc", FNM_CASEFOLD), 0);
    CHECK_EQ(fnmatch("a", "a/b", FNM_LEADING_DIR), 0);
    CHECK_EQ(fnmatch("a", "ab", FNM_LEADING_DIR), FNM_NOMATCH);
}

struct QNode {
    int v;
    TAILQ_ENTRY(QNode) tq;
    LIST_ENTRY(QNode) le;
    STAILQ_ENTRY(QNode) sq;
};
TAILQ_HEAD(QTailHead, QNode);
LIST_HEAD(QListHead, QNode);
STAILQ_HEAD(QStailHead, QNode);

// Macros only, so what is checked is that they compose and walk. The three
// flavours iconv names are LIST, STAILQ and TAILQ.

// Group B's three decisions that perform no syscall. The rest of Group B
// blocks and is a program's to exercise (doc/Testing.md §2).
void test_bmode()
{
    CHECK_EQ(fmode_flags("r"), SYS_O_READ);
    CHECK_EQ(fmode_flags("w"), SYS_O_WRITE | SYS_O_CREATE | SYS_O_TRUNC);
    CHECK_EQ(fmode_flags("a"), SYS_O_WRITE | SYS_O_CREATE | SYS_O_APPEND);

    // 'b' and 't' are noise, and the update bit is a '+' anywhere past it.
    CHECK_EQ(fmode_flags("rb"), SYS_O_READ);
    CHECK_EQ(fmode_flags("r+"), SYS_O_READ | SYS_O_WRITE);
    CHECK_EQ(fmode_flags("rb+"), SYS_O_READ | SYS_O_WRITE);
    CHECK_EQ(fmode_flags("r+b"), SYS_O_READ | SYS_O_WRITE);

    // "w+" and "a+" keep the truncation and the positioning "r+" has not.
    CHECK_EQ(fmode_flags("w+"), SYS_O_READ | SYS_O_WRITE | SYS_O_CREATE | SYS_O_TRUNC);
    CHECK_EQ(fmode_flags("a+"), SYS_O_READ | SYS_O_WRITE | SYS_O_CREATE | SYS_O_APPEND);
    CHECK_EQ(fmode_flags("wx"), SYS_O_WRITE | SYS_O_CREATE | SYS_O_TRUNC | SYS_O_EXCL);

    // Not a mode: 0, so b_fopen answers EINVAL rather than opening for read.
    CHECK_EQ(fmode_flags(""), 0u);
    CHECK_EQ(fmode_flags("+"), 0u);
    CHECK_EQ(fmode_flags("x"), 0u);
    CHECK_EQ(fmode_flags("rz"), 0u);

    CHECK(S_ISDIR(stat_mode(SYS_KIND_DIR)));
    CHECK(S_ISLNK(stat_mode(SYS_KIND_LINK)));
    CHECK(S_ISREG(stat_mode(SYS_KIND_FILE)));
    CHECK(!S_ISDIR(stat_mode(SYS_KIND_FILE)));
    CHECK_EQ(stat_mode(SYS_KIND_DIR) & 07777, 0755u);

    // st_ino is a path's identity: le compares device and inode to notice a
    // file changed under the editor, and a constant makes every file the same.
    CHECK(stat_ino("/a/b") != stat_ino("/a/c"));
    CHECK_EQ(stat_ino("/a/b"), stat_ino("/a/b"));
    CHECK(stat_ino("") != stat_ino("/"));
}

void test_queue()
{
    QNode n[4] = { { 0, {}, {}, {} }, { 1, {}, {}, {} }, { 2, {}, {}, {} }, { 3, {}, {}, {} } };

    struct QTailHead th;
    TAILQ_INIT(&th);
    CHECK(TAILQ_EMPTY(&th));
    for (QNode &e : n)
        TAILQ_INSERT_TAIL(&th, &e, tq);
    CHECK(TAILQ_FIRST(&th) == &n[0]);
    CHECK(TAILQ_LAST(&th, QTailHead) == &n[3]);
    CHECK(TAILQ_PREV(&n[3], QTailHead, tq) == &n[2]);

    int sum  = 0;
    QNode *p = nullptr;
    TAILQ_FOREACH(p, &th, tq)
    sum += p->v;
    CHECK_EQ(sum, 6);

    QNode extra = { 9, {}, {}, {} };
    TAILQ_INSERT_BEFORE(&n[2], &extra, tq);
    CHECK(TAILQ_NEXT(&n[1], tq) == &extra);

    QNode *tmp = nullptr;
    TAILQ_FOREACH_SAFE(p, &th, tq, tmp)
    TAILQ_REMOVE(&th, p, tq);
    CHECK(TAILQ_EMPTY(&th));

    struct QListHead lh;
    LIST_INIT(&lh);
    for (QNode &e : n)
        LIST_INSERT_HEAD(&lh, &e, le);
    CHECK(LIST_FIRST(&lh) == &n[3]);
    LIST_REMOVE(&n[3], le);
    CHECK(LIST_FIRST(&lh) == &n[2]);
    LIST_REMOVE(&n[1], le); // from the middle, with no head to hand
    CHECK(LIST_NEXT(&n[2], le) == &n[0]);

    struct QStailHead sh;
    STAILQ_INIT(&sh);
    for (QNode &e : n)
        STAILQ_INSERT_TAIL(&sh, &e, sq);
    CHECK(STAILQ_FIRST(&sh) == &n[0]);
    STAILQ_REMOVE_HEAD(&sh, sq);
    CHECK(STAILQ_FIRST(&sh) == &n[1]);
    sum = 0;
    STAILQ_FOREACH(p, &sh, sq)
    sum += p->v;
    CHECK_EQ(sum, 6);
}

} // namespace

void test_compat()
{
    test_begin("compat");

    test_mem();
    test_str();
    test_ctype();
    test_alloc_compat();
    test_strtol();
    test_strtod();
    test_sort();
    test_mergesort();
    test_env_intern();
    test_printf();
    test_endian();
    test_math_header();
    test_errno_bridge();
    test_time();
    test_wide();
    test_wctype();
    test_fnmatch();
    test_queue();
    test_bmode();
}
