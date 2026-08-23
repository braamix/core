// long double is declared to be double-shaped, which is what steers libm.h and
// the vendored sources onto their LDBL_MANT_DIG == 53 paths. Nothing here is
// compiled with a real long double in it -- on wasm32 that is 113-bit quad, and
// every operation on one is a compiler-rt call nothing provides.
#pragma once

#define FLT_MANT_DIG   __FLT_MANT_DIG__
#define FLT_MIN_EXP    __FLT_MIN_EXP__
#define FLT_MAX_EXP    __FLT_MAX_EXP__
#define FLT_MIN_10_EXP __FLT_MIN_10_EXP__
#define FLT_MAX_10_EXP __FLT_MAX_10_EXP__
#define FLT_EPSILON    __FLT_EPSILON__
#define FLT_MIN        __FLT_MIN__
#define FLT_MAX        __FLT_MAX__

#define DBL_MANT_DIG   __DBL_MANT_DIG__
#define DBL_MIN_EXP    __DBL_MIN_EXP__
#define DBL_MAX_EXP    __DBL_MAX_EXP__
#define DBL_MIN_10_EXP __DBL_MIN_10_EXP__
#define DBL_MAX_10_EXP __DBL_MAX_10_EXP__
#define DBL_EPSILON    __DBL_EPSILON__
#define DBL_MIN        __DBL_MIN__
#define DBL_MAX        __DBL_MAX__

#define LDBL_MANT_DIG   __DBL_MANT_DIG__
#define LDBL_MIN_EXP    __DBL_MIN_EXP__
#define LDBL_MAX_EXP    __DBL_MAX_EXP__
#define LDBL_MIN_10_EXP __DBL_MIN_10_EXP__
#define LDBL_MAX_10_EXP __DBL_MAX_10_EXP__
#define LDBL_EPSILON    __DBL_EPSILON__
#define LDBL_MIN        __DBL_MIN__
#define LDBL_MAX        __DBL_MAX__
