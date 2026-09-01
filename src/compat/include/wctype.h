// <wctype.h>. The classes over kernel/text.h's rune_lower/rune_upper, so this
// is exactly their coverage -- ASCII, Latin-1, Latin Extended-A, Greek and
// Cyrillic -- plus a short table of the caseless letter blocks. Not full
// Unicode, and it says so rather than pretending.
#pragma once

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

// A locale's named case mappings. There is one locale, so a transform is the
// function itself rather than a name to look up.
typedef wint_t (*wctrans_t)(wint_t);

int iswalnum(wint_t c);
int iswalpha(wint_t c);
int iswblank(wint_t c);
int iswcntrl(wint_t c);
int iswdigit(wint_t c);
int iswgraph(wint_t c);
int iswlower(wint_t c);
int iswprint(wint_t c);
int iswpunct(wint_t c);
int iswspace(wint_t c);
int iswupper(wint_t c);
int iswxdigit(wint_t c);

wint_t towlower(wint_t c);
wint_t towupper(wint_t c);

// "toupper" and "tolower"; anything else is 0.
wctrans_t wctrans(const char *name);
wint_t towctrans(wint_t c, wctrans_t t);

#ifdef __cplusplus
}
#endif
