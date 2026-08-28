#include "text.h"

usize utf8_encode(char32_t ch, char *out)
{
    u32 v = u32(rune_safe(ch));

    if (v < 0x80) {
        out[0] = char(v);
        return 1;
    }
    if (v < 0x800) {
        out[0] = char(0xc0 | (v >> 6));
        out[1] = char(0x80 | (v & 0x3f));
        return 2;
    }
    if (v < 0x10000) {
        out[0] = char(0xe0 | (v >> 12));
        out[1] = char(0x80 | ((v >> 6) & 0x3f));
        out[2] = char(0x80 | (v & 0x3f));
        return 3;
    }
    out[0] = char(0xf0 | (v >> 18));
    out[1] = char(0x80 | ((v >> 12) & 0x3f));
    out[2] = char(0x80 | ((v >> 6) & 0x3f));
    out[3] = char(0x80 | (v & 0x3f));
    return 4;
}

usize utf8_decode(Str s, usize at, char32_t &out)
{
    if (at >= s.size())
        return 0;

    u8 c = u8(s[at]);
    char32_t ch;
    usize len;
    char32_t least; // the smallest value this length may spell
    if (c < 0x80) {
        out = c;
        return 1;
    } else if ((c & 0xe0) == 0xc0 && c >= 0xc2) { // c0 and c1 are overlong
        ch    = c & 0x1f;
        len   = 2;
        least = 0x80;
    } else if ((c & 0xf0) == 0xe0) {
        ch    = c & 0x0f;
        len   = 3;
        least = 0x800;
    } else if ((c & 0xf8) == 0xf0 && c <= 0xf4) { // f5 and up are past U+10FFFF
        ch    = c & 0x07;
        len   = 4;
        least = 0x10000;
    } else {
        // A stray continuation byte, or a lead that cannot start one. Before
        // the length check: bad input, not short input.
        out = 0xfffd;
        return 1;
    }

    if (at + len > s.size())
        return 0;

    // One byte on a bad continuation, so the next lead byte resynchronises.
    for (usize k = 1; k < len; k++) {
        if ((u8(s[at + k]) & 0xc0) != 0x80) {
            out = 0xfffd;
            return 1;
        }
        ch = (ch << 6) | (u8(s[at + k]) & 0x3f);
    }

    // The shape was right, so all of it goes: one U+FFFD, not four.
    out = ch < least ? char32_t(0xfffd) : rune_safe(ch);
    return len;
}

namespace {

// Latin Extended-A runs whose uppercase member is the even codepoint of a
// pair. The dotted and dotless i sit inside the first and are not a pair.
bool even_upper(char32_t c)
{
    if (c == 0x130 || c == 0x131)
        return false;
    return (c >= 0x100 && c <= 0x137) || (c >= 0x14a && c <= 0x177);
}

// The runs where it is the odd one.
bool odd_upper(char32_t c)
{
    return (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e);
}

} // namespace

char32_t rune_lower(char32_t c)
{
    if (c < 0x80)
        return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xc0 && c <= 0xde && c != 0xd7)
        return c + 32;
    if (c == 0x178)
        return 0xff;
    if (even_upper(c))
        return c | 1;
    if (odd_upper(c))
        return (c & 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3ab && c != 0x3a2)
        return c + 32;
    if (c >= 0x410 && c <= 0x42f)
        return c + 32;
    if (c >= 0x400 && c <= 0x40f)
        return c + 80;
    return c;
}

char32_t rune_upper(char32_t c)
{
    if (c < 0x80)
        return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if (c >= 0xe0 && c <= 0xfe && c != 0xf7)
        return c - 32;
    if (c == 0xff)
        return 0x178;
    if (even_upper(c))
        return c & ~char32_t(1);
    if (odd_upper(c))
        return (c & 1) ? c : c - 1;
    if (c == 0x3c2) // final sigma
        return 0x3a3;
    if (c >= 0x3b1 && c <= 0x3cb)
        return c - 32;
    if (c >= 0x430 && c <= 0x44f)
        return c - 32;
    if (c >= 0x450 && c <= 0x45f)
        return c - 80;
    return c;
}

Option<u32> parse_u32(Str s)
{
    if (s.empty())
        return None;

    u32 v = 0;
    for (usize i = 0; i < s.size(); i++) {
        if (!is_digit(s[i]))
            return None;
        u32 d = u32(s[i] - '0');
        if (v > (0xffffffffu - d) / 10)
            return None;
        v = v * 10 + d;
    }
    return Option<u32>(v);
}

// ----------------------------------------------------------- scanning a Str

usize scan_space(Str s)
{
    usize n = 0;
    while (n < s.size() && is_space(s[n]))
        n++;
    return n;
}

namespace {

// The value of `c` in `base`, or -1.
int digit_in(char c, u32 base)
{
    u32 v;

    if (c >= '0' && c <= '9')
        v = u32(c - '0');
    else if (c >= 'a' && c <= 'z')
        v = u32(c - 'a') + 10;
    else if (c >= 'A' && c <= 'Z')
        v = u32(c - 'A') + 10;
    else
        return -1;
    return v < base ? int(v) : -1;
}

// The shared body: whitespace, a sign, a base prefix, then digits. `neg` says
// whether one was there; `used` counts everything consumed, and is 0 when
// there was no digit to take.
bool scan_number(Str s, usize &used, u32 base, usize width, u64 &out, bool &neg)
{
    usize i   = scan_space(s);
    usize end = width ? i + width : s.size();

    used = 0;
    neg  = false;
    if (end > s.size())
        end = s.size();
    if (i < end && (s[i] == '-' || s[i] == '+'))
        neg = s[i++] == '-';

    if ((base == 0 || base == 16) && i + 1 < end && s[i] == '0' &&
        (s[i + 1] == 'x' || s[i + 1] == 'X') && i + 2 < end && digit_in(s[i + 2], 16) >= 0) {
        base = 16;
        i += 2;
    } else if (base == 0 && i + 1 < end && s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B') &&
               i + 2 < end && digit_in(s[i + 2], 2) >= 0) {
        base = 2;
        i += 2;
    } else if (base == 0) {
        base = (i < end && s[i] == '0') ? 8 : 10;
    }

    u64 v   = 0;
    bool any = false;
    for (int d; i < end && (d = digit_in(s[i], base)) >= 0; i++) {
        v   = v * base + u64(d);
        any = true;
    }
    if (!any)
        return false;
    used = i;
    out  = v;
    return true;
}

} // namespace

Option<i64> scan_i64(Str s, usize &used, u32 base, usize width)
{
    u64 v;
    bool neg;

    if (!scan_number(s, used, base, width, v, neg))
        return None;
    return Option<i64>(neg ? -i64(v) : i64(v));
}

Option<u64> scan_u64(Str s, usize &used, u32 base, usize width)
{
    u64 v;
    bool neg;

    if (!scan_number(s, used, base, width, v, neg))
        return None;
    return Option<u64>(neg ? u64(-i64(v)) : v);
}

Str scan_token(Str s, usize &used, usize width)
{
    usize i   = scan_space(s);
    usize beg = i;
    usize end = width ? i + width : s.size();

    if (end > s.size())
        end = s.size();
    while (i < end && !is_space(s[i]))
        i++;
    if (i == beg) {
        used = 0;
        return Str();
    }
    used = i;
    return s.substr(beg, i - beg);
}

Str scan_until(Str s, Str stop, usize &used, usize width)
{
    usize i   = 0;
    usize end = width && width < s.size() ? width : s.size();

    while (i < end && stop.find(s[i]) == Str::npos)
        i++;
    used = i;
    return s.substr(0, i);
}
