// Heapsort, in place of the O(n^2) insertion sort three ports shared. O(n log n)
// worst case and O(1) extra memory, so it cannot fail — which matters where
// there are no exceptions — and it is iterative, so it cannot overflow the
// 128 KiB shadow stack. Not stable, and doc/Compat.md says so.
#include <stdlib.h>
#include <string.h>

namespace {

using Cmp3 = int (*)(const void *, const void *, void *);

void swap_bytes(char *a, char *b, size_t n)
{
    while (n--) {
        char t = *a;
        *a++   = *b;
        *b++   = t;
    }
}

// count is the heap size, root the node to push down.
void sift(char *base, size_t count, size_t root, size_t size, Cmp3 cmp, void *arg)
{
    for (;;) {
        size_t big   = root;
        size_t left  = 2 * root + 1;
        size_t right = left + 1;
        if (left < count && cmp(base + left * size, base + big * size, arg) > 0)
            big = left;
        if (right < count && cmp(base + right * size, base + big * size, arg) > 0)
            big = right;
        if (big == root)
            return;
        swap_bytes(base + root * size, base + big * size, size);
        root = big;
    }
}

void heapsort(void *vbase, size_t n, size_t size, Cmp3 cmp, void *arg)
{
    if (n < 2 || size == 0)
        return;
    char *base = static_cast<char *>(vbase);
    for (size_t i = n / 2; i-- > 0;)
        sift(base, n, i, size, cmp, arg);
    for (size_t end = n - 1; end > 0; end--) {
        swap_bytes(base, base + end * size, size);
        sift(base, end, 0, size, cmp, arg);
    }
}

int call2(const void *a, const void *b, void *arg)
{
    return reinterpret_cast<int (*)(const void *, const void *)>(arg)(a, b);
}

} // namespace

extern "C" {

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    heapsort(base, n, size, call2, reinterpret_cast<void *>(cmp));
}

void qsort_r(void *base, size_t n, size_t size, Cmp3 cmp, void *arg)
{
    heapsort(base, n, size, cmp, arg);
}

void *bsearch(const void *key, const void *vbase, size_t n, size_t size,
              int (*cmp)(const void *, const void *))
{
    const char *base = static_cast<const char *>(vbase);
    while (n) {
        size_t mid  = n / 2;
        const char *p = base + mid * size;
        int r         = cmp(key, p);
        if (r == 0)
            return const_cast<char *>(p);
        if (r > 0) {
            base = p + size;
            n -= mid + 1;
        } else {
            n = mid;
        }
    }
    return nullptr;
}

} // extern "C"
