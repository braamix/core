// Character classes, UTF-8 encoding and integer parsing. screen_write decodes
// UTF-8; this is the other direction, plus the scraps a tokeniser needs.
#pragma once

#include "result.h"
#include "str.h"
#include "types.h"

constexpr bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

constexpr bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

// A codepoint the host can render, which is what a cell may hold: a surrogate
// or a value past U+10FFFF becomes U+FFFD. Every writer to the grid goes
// through this (Concept.md §2.3).
constexpr char32_t rune_safe(char32_t c)
{
    u32 v = u32(c);
    return (v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) ? char32_t(0xfffd) : c;
}

// Encodes one codepoint into `out`, which must hold four bytes, and returns
// the length. rune_safe first.
usize utf8_encode(char32_t ch, char *out);

// Decodes the codepoint at s[at]. Returns the bytes consumed, or 0 when the
// sequence runs past the end. Every malformed sequence — a stray continuation
// byte, a lead that cannot start one, a missing continuation, an overlong, a
// surrogate, a value past U+10FFFF — yields U+FFFD, so bad input is visible
// rather than silently dropped.
usize utf8_decode(Str s, usize at, char32_t &out);

// The other case of `c`, or `c` itself where there is none. ASCII, Latin-1,
// Latin Extended-A, Greek and Cyrillic, by range rather than by table. A
// mapping that is not one codepoint for one comes back unchanged.
char32_t rune_lower(char32_t c);
char32_t rune_upper(char32_t c);

// Decimal, no sign, no leading space, and the whole string must be digits.
// None on empty input, a stray character, or a value past 2^32 - 1.
Option<u32> parse_u32(Str s);

// ------------------------------------------------------- scanning a Str
//
// scanf's conversions, one function each rather than a format string: nothing
// here takes `...`, errors are values, and a format defeats every check the
// compiler could make. `used` is strtod's endptr in this tree's spelling — the
// bytes taken, so a caller advances by it and reads the next field.
//
// Two rules, both places where scanf is vague. **Leading whitespace follows
// scanf**: the numeric ones and scan_token skip it, scan_until does not. And
// **`used` is 0 on no match**, so nothing was consumed and a caller that wants
// to back up simply does not advance.

// Bytes of whitespace at the front of `s`.
usize scan_space(Str s);

// scanf's %d %i %u %o %x. `base` 0 is C's 0x / 0b / 0 prefix rules, which is
// what %i means; `width` is the field width in bytes, 0 for none. None when
// there is no digit after the space and the sign, and `used` is 0 then.
Option<i64> scan_i64(Str s, usize &used, u32 base = 10, usize width = 0);
Option<u64> scan_u64(Str s, usize &used, u32 base = 10, usize width = 0);

// scanf's %s: leading space skipped, then a run of non-space. Empty, with
// `used` 0, at end of input.
Str scan_token(Str s, usize &used, usize width = 0);

// scanf's %[^set]: no leading skip, a run up to any byte in `stop`. Empty,
// with `used` 0, when the first byte is already in `stop`.
Str scan_until(Str s, Str stop, usize &used, usize width = 0);
