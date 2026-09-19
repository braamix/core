// The four libc functions zstd calls, under the names sys/stdlib.h and
// sys/string.h give them.
#include <stddef.h>
#include <stdint.h>

#include "kernel/alloc.h"

extern "C" {

void *zstd_sys_malloc(size_t n)
{
    return heap_alloc(n ? n : 1);
}

void *zstd_sys_calloc(size_t nmemb, size_t size)
{
    if (nmemb && size > SIZE_MAX / nmemb)
        return nullptr;
    size_t n = nmemb * size;
    void *p  = zstd_sys_malloc(n);
    if (p)
        __builtin_memset(p, 0, n);
    return p;
}

void zstd_sys_free(void *p)
{
    heap_free(p);
}

int zstd_sys_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *ap = static_cast<const unsigned char *>(a);
    const unsigned char *bp = static_cast<const unsigned char *>(b);
    for (; n--; ap++, bp++)
        if (*ap != *bp)
            return *ap < *bp ? -1 : 1;
    return 0;
}

} // extern "C"
