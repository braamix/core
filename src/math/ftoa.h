// Floating point to text and back, over musl's strtod and printf engines
// (src/math/cvt/). Part of braam::math, so a program links it the same way.
#pragma once

#include "kernel/fmt.h"
#include "kernel/result.h"
#include "kernel/str.h"
#include "kernel/types.h"

// Style is one of f e g a and their capitals, precision as printf's; -1 is
// printf's default of 6. The result names what fits in out[0..cap): a longer
// conversion is truncated, as Buf's is. %f of a large value is long -- 316
// characters at the default precision.
Str fmt_f64(char *out, usize cap, f64 v, i32 prec = -1, char style = 'g');

// The same, with printf's field width and flags, which the engine underneath
// takes and fmt_f64 does not pass: a zero pad goes inside the sign, which no
// padding around the result can produce. `flags` is the flag characters of a %
// conversion -- "#0- +" in any order -- and anything else in it is ignored.
Str fmt_f64_padded(char *out, usize cap, f64 v, i32 prec, char style, i32 width, Str flags);

// The fewest significant digits that parse back to v exactly.
Str fmt_f64_shortest(char *out, usize cap, f64 v);

// strtod's grammar: leading space, a sign, decimal or 0x hex, inf/infinity/nan.
// None on a string that is not wholly one number.
Option<f64> parse_f64(Str s);

// The same, stopping at the first character that cannot continue: `used` is how
// many were taken, 0 for none, which is strtod's endptr.
Option<f64> scan_f64(Str s, usize &used);

// 64 characters, so %e and %g always fit and %f does below 1e40. Past that,
// call fmt_f64 with a buffer of your own.
template <usize N>
Buf<N> &put_f64(Buf<N> &b, f64 v, i32 prec = -1, char style = 'g')
{
    char t[64];
    return b.put(fmt_f64(t, sizeof t, v, prec, style));
}
