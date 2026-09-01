// strtod and its two relatives, over math/ftoa.h's scan_f64, whose `used` is
// already strtod's endptr. A translation unit of their own: a port that names
// only strtol must not pull musl's __floatscan in behind them.
#include "math/ftoa.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace {

// musl's ERANGE, which is cvt/api.h's Cur::err.
constexpr i32 CVT_ERANGE = 34;

} // namespace

extern "C" {

double strtod(const char *s, char **end)
{
    if (end)
        *end = const_cast<char *>(s);
    if (!s)
        return 0;

    usize used    = 0;
    i32 err       = 0;
    Option<f64> v = scan_f64(Str(s, strlen(s)), used, &err);
    if (!v.has_value())
        return 0;
    if (end)
        *end = const_cast<char *>(s) + used;
    if (err == CVT_ERANGE)
        errno = ERANGE;
    return v.value();
}

// scan_f32, not a narrowed strtod: double then float rounds twice.
float strtof(const char *s, char **end)
{
    if (end)
        *end = const_cast<char *>(s);
    if (!s)
        return 0;

    usize used    = 0;
    i32 err       = 0;
    Option<f32> v = scan_f32(Str(s, strlen(s)), used, &err);
    if (!v.has_value())
        return 0;
    if (end)
        *end = const_cast<char *>(s) + used;
    if (err == CVT_ERANGE)
        errno = ERANGE;
    return v.value();
}

double atof(const char *s)
{
    return strtod(s, nullptr);
}

} // extern "C"
