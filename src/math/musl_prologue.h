// Force-included into the vendored C sources: what is neither clang's nor
// ours. Everything else comes from clang's freestanding headers or math.h.
#pragma once

#include <float.h>

// long double is 113-bit quad here and every operation on one is a compiler-rt
// call nothing provides. Double-shaped puts libm.h and cvt/ on their
// LDBL_MANT_DIG == 53 paths.
#undef LDBL_MANT_DIG
#undef LDBL_MIN_EXP
#undef LDBL_MAX_EXP
#undef LDBL_MIN_10_EXP
#undef LDBL_MAX_10_EXP
#undef LDBL_EPSILON
#undef LDBL_MIN
#undef LDBL_MAX
#define LDBL_MANT_DIG   __DBL_MANT_DIG__
#define LDBL_MIN_EXP    __DBL_MIN_EXP__
#define LDBL_MAX_EXP    __DBL_MAX_EXP__
#define LDBL_MIN_10_EXP __DBL_MIN_10_EXP__
#define LDBL_MAX_10_EXP __DBL_MAX_10_EXP__
#define LDBL_EPSILON    __DBL_EPSILON__
#define LDBL_MIN        __DBL_MIN__
#define LDBL_MAX        __DBL_MAX__

// musl's src/internal: visibility and aliasing.
#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __BYTE_ORDER    __LITTLE_ENDIAN

// wasm has no floating-point environment. Defined so the #ifdefs pass, zero so
// the tests fail; libm.h's WANT_ROUNDING follows.
#define FE_TONEAREST  0
#define FE_DOWNWARD   0
#define FE_UPWARD     0
#define FE_TOWARDZERO 0
#define FE_INEXACT    0
#define FE_INVALID    0
#define FE_DIVBYZERO  0
#define FE_UNDERFLOW  0
#define FE_OVERFLOW   0
#define FE_ALL_EXCEPT 0

static inline int feclearexcept(int e) { (void)e; return 0; }
static inline int feraiseexcept(int e) { (void)e; return 0; }
static inline int fetestexcept(int e) { (void)e; return 0; }
static inline int fegetround(void) { return FE_TONEAREST; }
static inline int fesetround(int r) { (void)r; return 0; }

// fma.c reached this through atomic.h.
static inline int a_clz_64(unsigned long long x) { return x ? __builtin_clzll(x) : 64; }
