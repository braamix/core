// C99 §7.12, answered by musl (src/math/musl/, MIT). Linked with
// `braam_add_program(... LIBS braam::math)`; text conversions are math/ftoa.h.
// Plain C: the vendored sources include this as <math.h>.
//
// No long double half: it is 113-bit quad here, and every operation on one is a
// compiler-rt call nothing provides.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

#define FP_ILOGB0   (-2147483647 - 1)
#define FP_ILOGBNAN (-2147483647 - 1)

// clang's freestanding <float.h> defines these identically when it is on the
// path, which it is for the vendored sources.
#ifndef INFINITY
#define INFINITY  __builtin_inff()
#endif
#ifndef NAN
#define NAN       __builtin_nanf("")
#endif
#define HUGE_VAL  __builtin_inf()
#define HUGE_VALF __builtin_inff()

// Errors are IEEE values, not errno: a domain error is NaN, a range error is an
// infinity or a zero. There is no errno here and no floating-point environment.
#define math_errhandling 0

// Named at fifteen #if sites in musl/, which until now read it undefined.
// clang's <float.h> derives 0 for this target; this is the fallback.
#ifndef FLT_EVAL_METHOD
#define FLT_EVAL_METHOD 0
#endif
typedef double double_t;
typedef float float_t;

#define M_E        2.7182818284590452354
#define M_LOG2E    1.4426950408889634074
#define M_LOG10E   0.43429448190325182765
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402
#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_1_PI     0.31830988618379067154
#define M_2_PI     0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2    1.41421356237309504880
#define M_SQRT1_2  0.70710678118654752440

int __fpclassify(double);
int __fpclassifyf(float);
int __signbit(double);
int __signbitf(float);

#define fpclassify(x) (sizeof(x) == sizeof(float) ? __fpclassifyf(x) : __fpclassify(x))
#define signbit(x)    (sizeof(x) == sizeof(float) ? __signbitf(x) : __signbit(x))
#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define isnormal(x)   (fpclassify(x) == FP_NORMAL)

#define isgreater(x, y)      __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define isless(x, y)         __builtin_isless(x, y)
#define islessequal(x, y)    __builtin_islessequal(x, y)
#define islessgreater(x, y)  __builtin_islessgreater(x, y)
#define isunordered(x, y)    __builtin_isunordered(x, y)

double acos(double);
double acosh(double);
double asin(double);
double asinh(double);
double atan(double);
double atan2(double, double);
double atanh(double);
double cbrt(double);
double ceil(double);
double copysign(double, double);
double cos(double);
double cosh(double);
double erf(double);
double erfc(double);
double exp(double);
double exp2(double);
double expm1(double);
double fabs(double);
double fdim(double, double);
double floor(double);
double fma(double, double, double);
double fmax(double, double);
double fmin(double, double);
double fmod(double, double);
double frexp(double, int *);
double hypot(double, double);
double ldexp(double, int);
double lgamma(double);
double log(double);
double log10(double);
double log1p(double);
double log2(double);
double logb(double);
double modf(double, double *);
double nan(const char *);
double nearbyint(double);
double nextafter(double, double);
double pow(double, double);
double remainder(double, double);
double remquo(double, double, int *);
double rint(double);
double round(double);
double scalbln(double, long);
double scalbn(double, int);
double sin(double);
double sinh(double);
double sqrt(double);
double tan(double);
double tanh(double);
double tgamma(double);
double trunc(double);
int ilogb(double);
long long llrint(double);
long long llround(double);
long lrint(double);
long lround(double);
void sincos(double, double *, double *);

float acosf(float);
float acoshf(float);
float asinf(float);
float asinhf(float);
float atan2f(float, float);
float atanf(float);
float atanhf(float);
float cbrtf(float);
float ceilf(float);
float copysignf(float, float);
float cosf(float);
float coshf(float);
float erfcf(float);
float erff(float);
float exp2f(float);
float expf(float);
float expm1f(float);
float fabsf(float);
float fdimf(float, float);
float floorf(float);
float fmaf(float, float, float);
float fmaxf(float, float);
float fminf(float, float);
float fmodf(float, float);
float frexpf(float, int *);
float hypotf(float, float);
float ldexpf(float, int);
float lgammaf(float);
float log10f(float);
float log1pf(float);
float log2f(float);
float logbf(float);
float logf(float);
float modff(float, float *);
float nanf(const char *);
float nearbyintf(float);
float nextafterf(float, float);
float powf(float, float);
float remainderf(float, float);
float remquof(float, float, int *);
float rintf(float);
float roundf(float);
float scalblnf(float, long);
float scalbnf(float, int);
float sinf(float);
float sinhf(float);
float sqrtf(float);
float tanf(float);
float tanhf(float);
float tgammaf(float);
float truncf(float);
int ilogbf(float);
long long llrintf(float);
long long llroundf(float);
long lrintf(float);
long lroundf(float);
void sincosf(float, float *, float *);

// Not C99. musl carries them, and a port may name one.
double drem(double, double);
double exp10(double);
double j0(double);
double j1(double);
double jn(int, double);
double lgamma_r(double, int *);
double significand(double);
double y0(double);
double y1(double);
double yn(int, double);
int finite(double);

float dremf(float, float);
float exp10f(float);
float j0f(float);
float j1f(float);
float jnf(int, float);
float lgammaf_r(float, int *);
float significandf(float);
float y0f(float);
float y1f(float);
float ynf(int, float);
int finitef(float);

extern int signgam;

#ifdef __cplusplus
} // extern "C"
#endif
