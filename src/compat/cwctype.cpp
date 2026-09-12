// The wide classes, over kernel/text.h's rune_is_* — the same coverage, which
// is case plus a table of the letter blocks that have none. Not full Unicode,
// and <wctype.h> says so. The three that ask about rendering are this file's
// own, over wcwidth: <wctype.h> answers a width where the grid answers a cell.
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "kernel/text.h"

namespace {

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
    return rune_is_upper(char32_t(c));
}

int iswlower(wint_t c)
{
    return rune_is_lower(char32_t(c));
}

int iswalpha(wint_t c)
{
    return rune_is_alpha(char32_t(c));
}

int iswdigit(wint_t c)
{
    return rune_is_digit(char32_t(c));
}

int iswxdigit(wint_t c)
{
    return rune_is_xdigit(char32_t(c));
}

int iswalnum(wint_t c)
{
    return rune_is_alnum(char32_t(c));
}

int iswspace(wint_t c)
{
    return rune_is_space(char32_t(c));
}

int iswblank(wint_t c)
{
    return rune_is_blank(char32_t(c));
}

int iswcntrl(wint_t c)
{
    return rune_is_cntrl(char32_t(c));
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
