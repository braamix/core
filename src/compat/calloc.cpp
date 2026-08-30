// malloc over kernel/alloc.h. No block header: heap_usable_size recovers a
// live block's capacity from the allocator's own side table, so realloc grows
// in place without one and every block keeps 16-byte alignment.
#include "kernel/alloc.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace {

// malloc(0) and realloc(p, 0) hand back a real block: a port that reads null as
// failure cannot then mistake success for it (doc/Compat.md §3).
constexpr size_t LEAST = 1;

bool mul_overflows(size_t a, size_t b)
{
    return a && b > SIZE_MAX / a;
}

} // namespace

extern "C" {

void *malloc(size_t n)
{
    void *p = heap_alloc(n ? n : LEAST);
    if (!p)
        errno = ENOMEM;
    return p;
}

void *calloc(size_t nmemb, size_t size)
{
    if (mul_overflows(nmemb, size)) {
        errno = ENOMEM;
        return nullptr;
    }
    size_t n = nmemb * size;
    void *p  = malloc(n);
    if (p)
        memset(p, 0, n ? n : LEAST);
    return p;
}

void *realloc(void *p, size_t n)
{
    if (!p)
        return malloc(n);
    if (!n)
        n = LEAST;

    size_t have = heap_usable_size(p);
    if (n <= have)
        return p;

    void *q = malloc(n);
    if (!q)
        return nullptr;
    memcpy(q, p, have);
    heap_free(p);
    return q;
}

void *reallocarray(void *p, size_t nmemb, size_t size)
{
    if (mul_overflows(nmemb, size)) {
        errno = ENOMEM;
        return nullptr;
    }
    return realloc(p, nmemb * size);
}

void free(void *p)
{
    heap_free(p);
}

} // extern "C"
