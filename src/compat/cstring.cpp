// str*, including the BSD and GNU names the ports needed.
#include <stdlib.h>
#include <string.h>

namespace {

char fold(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c + 32) : c;
}

} // namespace

extern "C" {

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return size_t(p - s);
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *stpcpy(char *d, const char *s)
{
    while ((*d = *s++))
        d++;
    return d;
}

// Pads to n with NULs and does not terminate a full copy, as C requires.
char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n && *s) {
        *d++ = *s++;
        n--;
    }
    while (n--)
        *d++ = '\0';
    return r;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *p = d + strlen(d);
    while (n-- && *s)
        *p++ = *s++;
    *p = '\0';
    return d;
}

int strcmp(const char *a, const char *b)
{
    const unsigned char *ap = reinterpret_cast<const unsigned char *>(a);
    const unsigned char *bp = reinterpret_cast<const unsigned char *>(b);
    while (*ap && *ap == *bp) {
        ap++;
        bp++;
    }
    return *ap < *bp ? -1 : *ap > *bp ? 1 : 0;
}

int strncmp(const char *a, const char *b, size_t n)
{
    const unsigned char *ap = reinterpret_cast<const unsigned char *>(a);
    const unsigned char *bp = reinterpret_cast<const unsigned char *>(b);
    while (n && *ap && *ap == *bp) {
        ap++;
        bp++;
        n--;
    }
    if (!n)
        return 0;
    return *ap < *bp ? -1 : *ap > *bp ? 1 : 0;
}

// The C locale, so byte order.
int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

char *strchr(const char *s, int c)
{
    char want = char(c);
    for (;; s++) {
        if (*s == want)
            return const_cast<char *>(s);
        if (!*s)
            return nullptr;
    }
}

char *strchrnul(const char *s, int c)
{
    char want = char(c);
    while (*s && *s != want)
        s++;
    return const_cast<char *>(s);
}

char *strrchr(const char *s, int c)
{
    char want       = char(c);
    const char *hit = nullptr;
    for (;; s++) {
        if (*s == want)
            hit = s;
        if (!*s)
            return const_cast<char *>(hit);
    }
}

char *strstr(const char *h, const char *n)
{
    if (!*n)
        return const_cast<char *>(h);
    size_t len = strlen(n);
    for (; *h; h++)
        if (*h == *n && strncmp(h, n, len) == 0)
            return const_cast<char *>(h);
    return nullptr;
}

char *strcasestr(const char *h, const char *n)
{
    if (!*n)
        return const_cast<char *>(h);
    size_t len = strlen(n);
    for (; *h; h++)
        if (fold(*h) == fold(*n) && strncasecmp(h, n, len) == 0)
            return const_cast<char *>(h);
    return nullptr;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && fold(*a) == fold(*b)) {
        a++;
        b++;
    }
    unsigned char x = static_cast<unsigned char>(fold(*a));
    unsigned char y = static_cast<unsigned char>(fold(*b));
    return x < y ? -1 : x > y ? 1 : 0;
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n && *a && fold(*a) == fold(*b)) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    unsigned char x = static_cast<unsigned char>(fold(*a));
    unsigned char y = static_cast<unsigned char>(fold(*b));
    return x < y ? -1 : x > y ? 1 : 0;
}

size_t strspn(const char *s, const char *set)
{
    const char *p = s;
    for (; *p && strchr(set, *p); p++)
        ;
    return size_t(p - s);
}

size_t strcspn(const char *s, const char *set)
{
    const char *p = s;
    for (; *p && !strchr(set, *p); p++)
        ;
    return size_t(p - s);
}

char *strpbrk(const char *s, const char *set)
{
    s += strcspn(s, set);
    return *s ? const_cast<char *>(s) : nullptr;
}

char *strtok_r(char *s, const char *sep, char **save)
{
    if (!s)
        s = *save;
    if (!s)
        return nullptr;
    s += strspn(s, sep);
    if (!*s) {
        *save = nullptr;
        return nullptr;
    }
    char *end = s + strcspn(s, sep);
    if (*end) {
        *end  = '\0';
        *save = end + 1;
    } else {
        *save = nullptr;
    }
    return s;
}

char *strtok(char *s, const char *sep)
{
    static char *save;
    return strtok_r(s, sep, &save);
}

// Unlike strtok, returns empty fields.
char *strsep(char **sp, const char *sep)
{
    char *s = *sp;
    if (!s)
        return nullptr;
    char *end = s + strcspn(s, sep);
    if (*end) {
        *end = '\0';
        *sp  = end + 1;
    } else {
        *sp = nullptr;
    }
    return s;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p  = static_cast<char *>(malloc(n));
    if (p)
        memcpy(p, s, n);
    return p;
}

char *strndup(const char *s, size_t n)
{
    size_t len = strnlen(s, n);
    char *p    = static_cast<char *>(malloc(len + 1));
    if (p) {
        memcpy(p, s, len);
        p[len] = '\0';
    }
    return p;
}

// Both return the length they tried to build, which is how a caller detects
// truncation without a second pass.
size_t strlcpy(char *d, const char *s, size_t n)
{
    size_t len = strlen(s);
    if (n) {
        size_t fit = len < n - 1 ? len : n - 1;
        memcpy(d, s, fit);
        d[fit] = '\0';
    }
    return len;
}

size_t strlcat(char *d, const char *s, size_t n)
{
    size_t have = strnlen(d, n);
    if (have == n)
        return n + strlen(s);
    return have + strlcpy(d + have, s, n - have);
}

} // extern "C"
