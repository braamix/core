// truncate(1)'s SIZE operand: a modifier, digits, a unit. Pure: no syscall, no
// host import, nothing that could not be compiled into the unit tests.
#pragma once

#include "kernel/result.h"
#include "kernel/str.h"
#include "kernel/types.h"

// What the leading punctuation means. Set is no modifier at all.
enum class SizeMod {
    Set,       // the size named
    Plus,      // + — longer by
    Minus,     // - — shorter by, floored at 0
    AtMost,    // < — shrink to it, or leave it alone
    AtLeast,   // > — grow to it, or leave it alone
    RoundDown, // / — down to a multiple of it
    RoundUp,   // % — up to a multiple of it
};

struct SizeSpec {
    SizeMod mod = SizeMod::Set;
    u64 n       = 0;
};

// What -o counts in. FS_BLOCK, restated rather than reached for: a program
// binary shares headers with the kernel and the VFS's are not among them.
constexpr u64 SIZE_BLOCK = 512;

// "100", "+1K", "%512". K, M, G and T are 1024; KB, MB, GB and TB are 1000.
// Err(Invalid) on anything else, an empty number, or an overflow.
Result<SizeSpec> parse_size(Str s);

// The spec against a size. Err(Invalid) on a rounding to a multiple of zero,
// and on an overflow past SYS_SEEK_MAX.
Result<u64> size_apply(SizeSpec spec, u64 cur);
