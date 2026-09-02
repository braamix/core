// The float conversions: musl's engine, under the name cfmt.cpp calls.
#include "cfmt.h"

#include "math/ftoa.h"

Str compat_fmt_f64(char *out, usize cap, f64 v, i32 prec, char style)
{
    return fmt_f64(out, cap, v, prec, style);
}
