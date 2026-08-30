// The port kit's Group A. Every case names the divergence it exists to pin:
// the seven ports each wrote these, and disagreed about most of them.
#include "compat/cerr.h"
#include "harness.h"
#include "kernel/alloc.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <endian.h>
#include <fenv.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

namespace {

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

void test_errno_bridge()
{
    CHECK_EQ(errno_of(Error::NotFound), ENOENT);
    CHECK_EQ(errno_of(Error::NoMemory), ENOMEM);
    CHECK_EQ(errno_of(Error::Perm), EACCES);
    // Both mean "abandoned by a signal".
    CHECK_EQ(errno_of(Error::Cancelled), EINTR);
    CHECK_EQ(errno_of(Error::Intr), EINTR);

    // Round-trips for everything with a distinct number.
    Error round[] = { Error::Invalid, Error::NoMemory, Error::NotFound, Error::Exists,
                      Error::NotDir,  Error::IsDir,    Error::Io,       Error::Again,
                      Error::Unsupported, Error::Closed, Error::NotEmpty, Error::Loop };
    for (Error e : round)
        CHECK(error_of(errno_of(e)) == e);

    CHECK(strcmp(strerror(ENOENT), "ENOENT") == 0);
    CHECK(strcmp(strerror(ERANGE), "ERANGE") == 0);

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

} // namespace

void test_compat()
{
    test_begin("compat");

    test_mem();
    test_str();
    test_ctype();
    test_alloc_compat();
    test_strtol();
    test_sort();
    test_printf();
    test_endian();
    test_math_header();
    test_errno_bridge();
}
