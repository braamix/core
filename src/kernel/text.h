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
