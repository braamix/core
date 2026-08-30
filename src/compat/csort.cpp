// Heapsort, in place of the O(n^2) insertion sort three ports shared. O(n log n)
// worst case and O(1) extra memory, so it cannot fail — which matters where
// there are no exceptions — and it is iterative, so it cannot overflow the
// 128 KiB shadow stack. Not stable, and doc/Compat.md says so.
//
// mergesort is the stable one, under BSD's name and BSD's error convention: a
// port that leaned on glibc's qsort being stable in practice changes the call
// and can see it fail, which a stabler qsort would not let it do.
#include <errno.h>
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

using Cmp2 = int (*)(const void *, const void *);

// One pass: merge the sorted runs [lo, mid) and [mid, hi) of src into dst.
// `<= 0` takes from the left run, which is what makes it stable.
void merge_run(const char *src, char *dst, size_t lo, size_t mid, size_t hi, size_t size,
               Cmp2 cmp)
{
    size_t i = lo, j = mid, k = lo;

    while (i < mid && j < hi) {
        if (cmp(src + i * size, src + j * size) <= 0)
            memcpy(dst + k++ * size, src + i++ * size, size);
        else
            memcpy(dst + k++ * size, src + j++ * size, size);
    }
    while (i < mid)
        memcpy(dst + k++ * size, src + i++ * size, size);
    while (j < hi)
        memcpy(dst + k++ * size, src + j++ * size, size);
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

// Bottom-up, so it is iterative and the run width is the whole of the state.
// One scratch block of n * size, and the passes ping-pong between it and the
// caller's array; an odd pass count copies back, so base always ends up sorted.
int mergesort(void *vbase, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    if (size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (n < 2)
        return 0;

    char *base = static_cast<char *>(vbase);
    char *tmp  = static_cast<char *>(malloc(n * size));
    if (!tmp) {
        errno = ENOMEM;
        return -1;
    }

    char *src = base, *dst = tmp;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi  = lo + 2 * width < n ? lo + 2 * width : n;
            merge_run(src, dst, lo, mid, hi, size, cmp);
        }
        char *t = src;
        src     = dst;
        dst     = t;
    }
    if (src != base)
        memcpy(base, src, n * size);

    free(tmp);
    return 0;
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
