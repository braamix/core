// snprintf's %e %f %g %a, behind one name so the engine can be dropped.
//
// Two archives define it: braam_compat_float, which is musl's fmtfp, and
// braam_compat_nofloat, which traps. braam_add_program links exactly one.
// The reference is strong on purpose -- a weak one pulls no archive member,
// so a port that forgot to ask would print nothing for %f.
#pragma once

#include "kernel/str.h"

Str compat_fmt_f64(char *out, usize cap, f64 v, i32 prec, char style);
