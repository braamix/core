// What musl's own src/internal/libc.h and features.h give its sources: the
// visibility and branch-hint macros, and the two bit counters fma.c reaches for
// through atomic.h.
#pragma once

#define hidden         __attribute__((__visibility__("hidden")))
#define weak           __attribute__((__weak__))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))

#define predict_true(x)  __builtin_expect(!!(x), 1)
#define predict_false(x) __builtin_expect(!!(x), 0)

static inline int a_clz_32(unsigned int x)
{
	return x ? __builtin_clz(x) : 32;
}

static inline int a_clz_64(unsigned long long x)
{
	return x ? __builtin_clzll(x) : 64;
}
