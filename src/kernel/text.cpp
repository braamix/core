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
