// The eight wasm answers with one instruction each, so musl's software versions
// are not vendored. Defined as well as declared: a caller reaching one through
// a pointer, or built with -fno-builtin, still needs the symbol.
//
// round, fmin, fmax and fma are not here -- __builtin_round, __builtin_fmin,
// __builtin_fmax and __builtin_fma each emit an undefined symbol on this
// target, so musl answers those four.

#include <math.h>

double sqrt(double x)
{
    return __builtin_sqrt(x);
}
double fabs(double x)
{
    return __builtin_fabs(x);
}
double floor(double x)
{
    return __builtin_floor(x);
}
double ceil(double x)
{
    return __builtin_ceil(x);
}
double trunc(double x)
{
    return __builtin_trunc(x);
}
double rint(double x)
{
    return __builtin_rint(x);
}
double copysign(double x, double y)
{
    return __builtin_copysign(x, y);
}

// No floating-point environment, so it cannot differ from rint.
double nearbyint(double x)
{
    return __builtin_rint(x);
}

float sqrtf(float x)
{
    return __builtin_sqrtf(x);
}
float fabsf(float x)
{
    return __builtin_fabsf(x);
}
float floorf(float x)
{
    return __builtin_floorf(x);
}
float ceilf(float x)
{
    return __builtin_ceilf(x);
}
float truncf(float x)
{
    return __builtin_truncf(x);
}
float rintf(float x)
{
    return __builtin_rintf(x);
}
float copysignf(float x, float y)
{
    return __builtin_copysignf(x, y);
}
float nearbyintf(float x)
{
    return __builtin_rintf(x);
}
