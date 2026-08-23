// Degenerate, as wasm has no floating-point environment: no exception flags and
// no rounding modes. arch/wasm32/fp_arch.h says the same on musl's side, and
// libm.h's WANT_ROUNDING follows from it.
#pragma once

#define FE_TONEAREST  0
#define FE_DOWNWARD   0
#define FE_UPWARD     0
#define FE_TOWARDZERO 0
#define FE_INEXACT    0
#define FE_INVALID    0
#define FE_DIVBYZERO  0
#define FE_OVERFLOW   0
#define FE_UNDERFLOW  0
#define FE_ALL_EXCEPT 0

typedef int fexcept_t;
typedef int fenv_t;

static inline int feclearexcept(int e) { (void)e; return 0; }
static inline int feraiseexcept(int e) { (void)e; return 0; }
static inline int fetestexcept(int e) { (void)e; return 0; }
static inline int fegetround(void) { return FE_TONEAREST; }
static inline int fesetround(int r) { (void)r; return 0; }
static inline int fegetenv(fenv_t *p) { (void)p; return 0; }
static inline int fesetenv(const fenv_t *p) { (void)p; return 0; }
static inline int feholdexcept(fenv_t *p) { (void)p; return 0; }
static inline int feupdateenv(const fenv_t *p) { (void)p; return 0; }
