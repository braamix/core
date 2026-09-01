// The wide half. One encoding, so the conversions are kernel/text.h's codec;
// utf8_encode/utf8_decode stay undefined in the archive, as ctime.cpp's
// civil() does.
//
// utf8_decode answers U+FFFD for every malformed form, which C must not: a bad
// byte is EILSEQ and a real U+FFFD is a rune. The two are told apart by
// re-encoding what came back and comparing bytes.
#include "kernel/text.h"

#include <errno.h>
#include <wchar.h>
#include <wctype.h>

namespace {

// The bytes decoded, 0 when the sequence runs past the end, -1 when malformed.
int decode(const char *s, usize n, char32_t &out)
{
    usize used = utf8_decode(Str(s, n), 0, out);
    if (used == 0)
        return 0;

    char t[4];
    usize back = utf8_encode(out, t);
    if (back != used)
        return -1;
    for (usize i = 0; i < used; i++)
        if (t[i] != s[i])
            return -1;
    return int(used);
}

// Citrus avoids a null `ps` so as not to clobber shared state; the fallback is
// its own object for that reason.
mbstate_t own;

} // namespace

extern "C" {

size_t mbrtowc(wchar_t *out, const char *s, size_t n, mbstate_t *ps)
{
    if (!ps)
        ps = &own;
    if (!s) { // C's reset, spelt mbrtowc(0, 0, 0, ps)
        ps->len = 0;
        return 0;
    }

    usize used = 0;
    while (used < n) {
        if (ps->len >= sizeof(ps->buf)) {
            ps->len = 0;
            errno   = EILSEQ;
            return size_t(-1);
        }
        ps->buf[ps->len++] = (unsigned char)s[used++];

        char32_t ch = 0;
        int got     = decode((const char *)ps->buf, ps->len, ch);
        if (got == 0)
            continue; // needs another byte
        if (got < 0) {
            ps->len = 0;
            errno   = EILSEQ;
            return size_t(-1);
        }
        ps->len = 0;
        if (out)
            *out = wchar_t(ch);
        return ch == 0 ? 0 : used;
    }
    return size_t(-2); // incomplete, and every byte was taken
}

size_t wcrtomb(char *out, wchar_t c, mbstate_t *ps)
{
    if (ps)
        ps->len = 0;
    if (!out)
        return 1; // the reset sequence, which UTF-8 has not got
    return utf8_encode(char32_t(c), out);
}

int mbsinit(const mbstate_t *ps)
{
    return !ps || ps->len == 0;
}

int mbtowc(wchar_t *out, const char *s, size_t n)
{
    if (!s)
        return 0; // the reset
    if (n == 0)
        return -1;

    char32_t ch = 0;
    int got     = decode(s, n, ch);
    if (got <= 0) {
        errno = EILSEQ;
        return -1;
    }
    if (out)
        *out = wchar_t(ch);
    return ch == 0 ? 0 : got;
}

int mblen(const char *s, size_t n)
{
    return mbtowc(nullptr, s, n);
}

int wctomb(char *out, wchar_t c)
{
    if (!out)
        return 0;
    return int(utf8_encode(char32_t(c), out));
}

// `n` is the capacity of `out`. The count excludes the terminator, which is
// written only if it fits.
size_t mbstowcs(wchar_t *out, const char *s, size_t n)
{
    usize len = 0;
    while (s[len])
        len++;

    usize off = 0, written = 0;
    while (off < len) {
        if (out && written == n)
            return written;

        char32_t ch = 0;
        int got     = decode(s + off, len - off, ch);
        if (got <= 0) {
            errno = EILSEQ;
            return size_t(-1);
        }
        if (out)
            out[written] = wchar_t(ch);
        written++;
        off += usize(got);
    }
    if (out && written < n)
        out[written] = wchar_t(0);
    return written;
}

size_t wcstombs(char *out, const wchar_t *s, size_t n)
{
    usize written = 0;
    for (; *s; s++) {
        char t[4];
        usize len = utf8_encode(char32_t(*s), t);
        if (out) {
            if (written + len > n)
                return written;
            for (usize i = 0; i < len; i++)
                out[written + i] = t[i];
        }
        written += len;
    }
    if (out && written < n)
        out[written] = '\0';
    return written;
}

size_t wcslen(const wchar_t *s)
{
    usize n = 0;
    while (s[n])
        n++;
    return n;
}

int wcswidth(const wchar_t *s, size_t n)
{
    int total = 0;
    for (usize i = 0; i < n && s[i]; i++) {
        int w = wcwidth(s[i]);
        if (w < 0)
            return -1;
        total += w;
    }
    return total;
}

} // extern "C"
