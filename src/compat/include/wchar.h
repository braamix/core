// <wchar.h>. One encoding: this system is UTF-8 throughout and wchar_t is
// UTF-32, so there is no locale to ask.
//
// Includable from C -- a port compiles its regex and its width table that way.
#pragma once

#include <stddef.h>

#define MB_CUR_MAX 4
#define WEOF       ((wint_t) - 1)

#ifdef __cplusplus
extern "C" {
#endif

typedef __WINT_TYPE__ wint_t;

// A UTF-8 sequence is at most four bytes; the state holds one that straddled a
// call, which is what makes mbrtowc restartable.
typedef struct {
    unsigned char buf[4];
    unsigned char len;
} mbstate_t;

// Restartable. 0 for a NUL, (size_t)-1 with EILSEQ for a malformed sequence,
// (size_t)-2 for one that ran out of bytes with the state keeping them.
size_t mbrtowc(wchar_t *out, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *out, wchar_t c, mbstate_t *ps);
int mbsinit(const mbstate_t *ps);

// Stateless. -1 for a malformed sequence, 0 for a NUL, else the bytes taken.
// mbtowc(0, 0, 0) and mblen(0, 0) are C's "reset", which UTF-8 does not need.
int mbtowc(wchar_t *out, const char *s, size_t n);
int mblen(const char *s, size_t n);
int wctomb(char *out, wchar_t c);

// `n` is the capacity of `out`. (size_t)-1 on a malformed sequence.
size_t mbstowcs(wchar_t *out, const char *s, size_t n);
size_t wcstombs(char *out, const wchar_t *s, size_t n);

size_t wcslen(const wchar_t *s);

// Columns: 0 for a combining mark, 2 for East Asian Wide and Fullwidth, -1 for
// a codepoint that has no printable form.
int wcwidth(wchar_t c);
int wcswidth(const wchar_t *s, size_t n);

#ifdef __cplusplus
}
#endif
