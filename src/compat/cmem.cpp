// mem* and the BSD byte functions.
#include <string.h>

extern "C" {

void *memcpy(void *d, const void *s, size_t n)
{
    char *dp       = static_cast<char *>(d);
    const char *sp = static_cast<const char *>(s);
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    char *dp       = static_cast<char *>(d);
    const char *sp = static_cast<const char *>(s);
    if (dp == sp)
        return d;
    if (dp < sp) {
        while (n--)
            *dp++ = *sp++;
    } else {
        dp += n;
        sp += n;
        while (n--)
            *--dp = *--sp;
    }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *dp = static_cast<unsigned char *>(d);
    while (n--)
        *dp++ = static_cast<unsigned char>(c);
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *ap = static_cast<const unsigned char *>(a);
    const unsigned char *bp = static_cast<const unsigned char *>(b);
    for (; n--; ap++, bp++)
        if (*ap != *bp)
            return *ap < *bp ? -1 : 1;
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = static_cast<const unsigned char *>(s);
    unsigned char want     = static_cast<unsigned char>(c);
    for (; n--; p++)
        if (*p == want)
            return const_cast<unsigned char *>(p);
    return nullptr;
}

void bzero(void *d, size_t n)
{
    memset(d, 0, n);
}

void bcopy(const void *s, void *d, size_t n)
{
    memmove(d, s, n);
}

} // extern "C"
