// The wide classes, over kernel/text.h's rune_lower/rune_upper. Their coverage
// is this file's: ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic have
// case, and a short table names the letter blocks that have none. Not full
// Unicode, and <wctype.h> says so.
#include "kernel/text.h"

#include <string.h>
#include <wchar.h>
#include <wctype.h>

namespace {

struct Block {
    unsigned first, last;
};

// Letters with no case mapping, so iswalpha cannot find them by asking for one.
const Block LETTERS[] = {
    { 0x0590, 0x06FF }, // Hebrew, Arabic
    { 0x0700, 0x07BF }, // Syriac, Arabic supplement, Thaana
    { 0x0900, 0x0DFF }, // Devanagari .. Sinhala
    { 0x0E00, 0x0FFF }, // Thai, Lao, Tibetan
    { 0x1000, 0x109F }, // Myanmar
    { 0x1100, 0x11FF }, // Hangul Jamo
    { 0x1200, 0x137F }, // Ethiopic
    { 0x13A0, 0x13FF }, // Cherokee
    { 0x1780, 0x17FF }, // Khmer
    { 0x3040, 0x30FF }, // kana
    { 0x3105, 0x312F }, // Bopomofo
    { 0x3400, 0x4DBF }, // CJK extension A
    { 0x4E00, 0x9FFF }, // CJK
    { 0xA000, 0xA4CF }, // Yi
    { 0xAC00, 0xD7A3 }, // Hangul syllables
    { 0xF900, 0xFAFF }, // CJK compatibility
    { 0x20000, 0x2FFFD },
};

bool caseless_letter(unsigned c)
{
    for (const Block &b : LETTERS)
        if (c >= b.first && c <= b.last)
            return true;
    return false;
}

wint_t to_lower(wint_t c)
{
    return wint_t(rune_lower(char32_t(c)));
}

wint_t to_upper(wint_t c)
{
    return wint_t(rune_upper(char32_t(c)));
}

} // namespace

extern "C" {

int iswupper(wint_t c)
{
    return c != to_lower(c);
}

int iswlower(wint_t c)
{
    return c != to_upper(c);
}

int iswalpha(wint_t c)
{
    return iswupper(c) || iswlower(c) || caseless_letter(unsigned(c));
}

int iswdigit(wint_t c)
{
    return c >= '0' && c <= '9';
}

int iswxdigit(wint_t c)
{
    return iswdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int iswalnum(wint_t c)
{
    return iswalpha(c) || iswdigit(c);
}

// The six bytes ctype's isspace takes, plus Unicode's separators.
int iswspace(wint_t c)
{
    unsigned v = unsigned(c);
    return v == ' ' || (v >= '\t' && v <= '\r') || v == 0x0085 || v == 0x00a0 || v == 0x1680 ||
           (v >= 0x2000 && v <= 0x200a) || v == 0x2028 || v == 0x2029 || v == 0x202f ||
           v == 0x205f || v == 0x3000;
}

// Space, tab and Unicode's space separators; not the line ones.
int iswblank(wint_t c)
{
    unsigned v = unsigned(c);
    return v == ' ' || v == '\t' || v == 0x00a0 || v == 0x1680 ||
           (v >= 0x2000 && v <= 0x200a) || v == 0x202f || v == 0x205f || v == 0x3000;
}

int iswcntrl(wint_t c)
{
    unsigned v = unsigned(c);
    return v < 0x20 || (v >= 0x7f && v < 0xa0);
}

int iswprint(wint_t c)
{
    return wcwidth(wchar_t(c)) >= 0;
}

int iswgraph(wint_t c)
{
    return c != 0 && iswprint(c) && !iswspace(c);
}

int iswpunct(wint_t c)
{
    return iswgraph(c) && !iswalnum(c);
}

wint_t towlower(wint_t c)
{
    return to_lower(c);
}

wint_t towupper(wint_t c)
{
    return to_upper(c);
}

wctrans_t wctrans(const char *name)
{
    if (!name)
        return nullptr;
    if (strcmp(name, "toupper") == 0)
        return towupper;
    if (strcmp(name, "tolower") == 0)
        return towlower;
    return nullptr;
}

wint_t towctrans(wint_t c, wctrans_t t)
{
    return t ? t(c) : c;
}

} // extern "C"
