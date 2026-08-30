// One strtol, in place of three that disagreed. Against the three it replaces:
// all six isspace bytes are skipped, a bare "0x" consumes only the 0, base 0
// takes 0b as well as 0x, strtoul handles '-', and the whole family saturates
// and sets ERANGE.
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

namespace {

constexpr unsigned long long U64_MAX = ~0ull;

int digit_of(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    return -1;
}

// The shared scan. `limit` is the magnitude the caller saturates at.
unsigned long long scan(const char *s, char **end, int base, bool &neg, bool &over,
                        unsigned long long limit)
{
    const char *start = s;
    neg               = false;
    over              = false;

    while (isspace(static_cast<unsigned char>(*s)))
        s++;
    if (*s == '+' || *s == '-')
        neg = *s++ == '-';

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && digit_of(s[2]) >= 0 &&
            digit_of(s[2]) < 16) {
            base = 16;
            s += 2;
        } else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B') &&
                   (s[2] == '0' || s[2] == '1')) {
            base = 2;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
               digit_of(s[2]) >= 0 && digit_of(s[2]) < 16) {
        s += 2;
    } else if (base == 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B') &&
               (s[2] == '0' || s[2] == '1')) {
        s += 2;
    }

    unsigned long long v = 0;
    const char *first    = s;
    for (int d; (d = digit_of(*s)) >= 0 && d < base; s++) {
        if (over)
            continue;
        if (v > (U64_MAX - unsigned(d)) / unsigned(base)) {
            over = true;
            continue;
        }
        v = v * unsigned(base) + unsigned(d);
        if (v > limit)
            over = true;
    }

    // No digit converted: endptr is the start, per C.
    if (s == first) {
        if (end)
            *end = const_cast<char *>(start);
        return 0;
    }
    if (end)
        *end = const_cast<char *>(s);
    if (over) {
        errno = ERANGE;
        return limit;
    }
    return v;
}

template <typename S, typename U>
S signed_of(const char *s, char **end, int base, U pos_max)
{
    bool neg, over;
    unsigned long long v = scan(s, end, base, neg, over, pos_max + 1);
    if (neg) {
        if (over || v > (unsigned long long)pos_max + 1) {
            errno = ERANGE;
            return S(-(S)pos_max - 1);
        }
        if (v == (unsigned long long)pos_max + 1)
            return S(-(S)pos_max - 1);
        return S(-(long long)v);
    }
    if (over || v > pos_max) {
        errno = ERANGE;
        return S(pos_max);
    }
    return S(v);
}

template <typename U>
U unsigned_of(const char *s, char **end, int base, U max)
{
    bool neg, over;
    unsigned long long v = scan(s, end, base, neg, over, max);
    if (over) {
        errno = ERANGE;
        return max;
    }
    // C negates modulo the type, so strtoul("-1") is ULONG_MAX.
    return neg ? U(0) - U(v) : U(v);
}

} // namespace

extern "C" {

long strtol(const char *s, char **end, int base)
{
    return signed_of<long, unsigned long>(s, end, base, 0x7fffffffUL);
}

unsigned long strtoul(const char *s, char **end, int base)
{
    return unsigned_of<unsigned long>(s, end, base, 0xffffffffUL);
}

long long strtoll(const char *s, char **end, int base)
{
    return signed_of<long long, unsigned long long>(s, end, base, 0x7fffffffffffffffULL);
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    return unsigned_of<unsigned long long>(s, end, base, U64_MAX);
}

int atoi(const char *s)
{
    return int(strtol(s, nullptr, 10));
}

long atol(const char *s)
{
    return strtol(s, nullptr, 10);
}

long long atoll(const char *s)
{
    return strtoll(s, nullptr, 10);
}

} // extern "C"
