// The float conversions, dropped: what PORT NOFLOAT links instead of
// cfmt_float.cpp.
#include "cfmt.h"

#include "kernel/host.h"

Str compat_fmt_f64(char *, usize, f64, i32, char)
{
    panic("printf: a float conversion, in a program built PORT NOFLOAT");
}
