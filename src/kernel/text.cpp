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

// Greek Extended, the rows whose uppercase eight sit directly above their
// lowercase eight. Answers the lowercase member of c's pair, or zero.
char32_t greek_ext_row(char32_t c)
{
    const char32_t lower = c & ~char32_t(8), n = c & 7;
    switch (c & ~char32_t(0xf)) {
    case 0x1f00:
    case 0x1f20:
    case 0x1f30:
    case 0x1f60:
    case 0x1f80:
    case 0x1f90:
    case 0x1fa0:
        return lower;
    case 0x1f10: // epsilon and omicron: no perispomeni, so six of eight
    case 0x1f40:
        return n < 6 ? lower : 0;
    case 0x1f50: // upsilon: only the four with dasia
        return (c & 1) ? lower : 0;
    case 0x1fb0: // alpha, iota, upsilon: the vrachy and macron pairs
    case 0x1fd0:
    case 0x1fe0:
        return n < 2 ? lower : 0;
    }
    return 0;
}

// The rest of the block: the vowels with varia or oxia, whose uppercase was
// put in the last four rows, and the four with an iota subscript.
const struct {
    char32_t lower, upper;
} GREEK_EXT[] = {
    { 0x1f70, 0x1fba }, { 0x1f71, 0x1fbb }, { 0x1f72, 0x1fc8 }, { 0x1f73, 0x1fc9 },
    { 0x1f74, 0x1fca }, { 0x1f75, 0x1fcb }, { 0x1f76, 0x1fda }, { 0x1f77, 0x1fdb },
    { 0x1f78, 0x1ff8 }, { 0x1f79, 0x1ff9 }, { 0x1f7a, 0x1fea }, { 0x1f7b, 0x1feb },
    { 0x1f7c, 0x1ffa }, { 0x1f7d, 0x1ffb }, { 0x1fb3, 0x1fbc }, { 0x1fc3, 0x1fcc },
    { 0x1fe5, 0x1fec }, { 0x1ff3, 0x1ffc },
};

// Letter blocks with no case, which asking for the other case cannot find.
const struct {
    char32_t first, last;
} CASELESS[] = {
    { 0x0590, 0x06ff }, // Hebrew, Arabic
    { 0x0700, 0x07bf }, // Syriac, Arabic supplement, Thaana
    { 0x0900, 0x0dff }, // Devanagari .. Sinhala
    { 0x0e00, 0x0fff }, // Thai, Lao, Tibetan
    { 0x1000, 0x109f }, // Myanmar
    { 0x1100, 0x11ff }, // Hangul Jamo
    { 0x1200, 0x137f }, // Ethiopic
    { 0x13a0, 0x13ff }, // Cherokee
    { 0x1780, 0x17ff }, // Khmer
    { 0x3040, 0x30ff }, // kana
    { 0x3105, 0x312f }, // Bopomofo
    { 0x3400, 0x4dbf }, // CJK extension A
    { 0x4e00, 0x9fff }, // CJK
    { 0xa000, 0xa4cf }, // Yi
    { 0xac00, 0xd7a3 }, // Hangul syllables
    { 0xf900, 0xfaff }, // CJK compatibility
    { 0x20000, 0x2fffd },
};

bool caseless_letter(char32_t c)
{
    for (const auto &b : CASELESS)
        if (c >= b.first && c <= b.last)
            return true;
    return false;
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
    if (c >= 0x1f00 && c <= 0x1fff) {
        if (char32_t lower = greek_ext_row(c))
            return lower;
        for (const auto &p : GREEK_EXT)
            if (p.upper == c)
                return p.lower;
    }
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
    if (c == 0x1fbe) // prosgegrammeni, whose uppercase is plain iota
        return 0x399;
    if (c >= 0x1f00 && c <= 0x1fff) {
        if (char32_t lower = greek_ext_row(c))
            return lower | 8;
        for (const auto &p : GREEK_EXT)
            if (p.lower == c)
                return p.upper;
    }
    return c;
}

bool rune_is_upper(char32_t c)
{
    return c != rune_lower(c);
}

bool rune_is_lower(char32_t c)
{
    return c != rune_upper(c);
}

bool rune_is_alpha(char32_t c)
{
    return rune_is_upper(c) || rune_is_lower(c) || caseless_letter(c);
}

bool rune_is_digit(char32_t c)
{
    return c >= '0' && c <= '9';
}

bool rune_is_xdigit(char32_t c)
{
    return rune_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool rune_is_alnum(char32_t c)
{
    return rune_is_alpha(c) || rune_is_digit(c);
}

// The six ASCII ones, plus Unicode's separators.
bool rune_is_space(char32_t c)
{
    return c == ' ' || (c >= '\t' && c <= '\r') || c == 0x0085 || c == 0x00a0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
           c == 0x205f || c == 0x3000;
}

// Space separators only, not the line ones.
bool rune_is_blank(char32_t c)
{
    return c == ' ' || c == '\t' || c == 0x00a0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x202f || c == 0x205f || c == 0x3000;
}

bool rune_is_cntrl(char32_t c)
{
    return c < 0x20 || (c >= 0x7f && c < 0xa0);
}

// What rune_safe leaves alone and the grid can put in a cell.
bool rune_is_print(char32_t c)
{
    return !rune_is_cntrl(c) && rune_safe(c) == c;
}

bool rune_is_graph(char32_t c)
{
    return c != 0 && rune_is_print(c) && !rune_is_space(c);
}

bool rune_is_punct(char32_t c)
{
    return rune_is_graph(c) && !rune_is_alnum(c);
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

    u64 v    = 0;
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
