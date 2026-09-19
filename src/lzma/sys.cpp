// The six libc functions liblzma calls, under the names sys/stdlib.h and
// sys/string.h give them.
#include <stddef.h>
#include <stdint.h>

#include "kernel/alloc.h"

extern "C" {

void *lzma_sys_malloc(size_t n)
{
    return heap_alloc(n ? n : 1);
}

void *lzma_sys_calloc(size_t nmemb, size_t size)
{
    if (nmemb && size > SIZE_MAX / nmemb)
        return nullptr;
    size_t n = nmemb * size;
    void *p  = lzma_sys_malloc(n);
    if (p)
        __builtin_memset(p, 0, n);
    return p;
}

void lzma_sys_free(void *p)
{
    heap_free(p);
}

int lzma_sys_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *ap = static_cast<const unsigned char *>(a);
    const unsigned char *bp = static_cast<const unsigned char *>(b);
    for (; n--; ap++, bp++)
        if (*ap != *bp)
            return *ap < *bp ? -1 : 1;
    return 0;
}

void *lzma_sys_memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = static_cast<const unsigned char *>(s);
    unsigned char want     = static_cast<unsigned char>(c);
    for (; n--; p++)
        if (*p == want)
            return const_cast<unsigned char *>(p);
    return nullptr;
}

size_t lzma_sys_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return size_t(p - s);
}

} // extern "C"
