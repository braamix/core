#include "size.h"

#include "kernel/sysabi.h"
#include "kernel/text.h"

namespace {

struct Unit {
    Str name;
    u64 scale;
};

// Longest first, so "KB" is not read as "K" with junk after it.
const Unit UNITS[] = {
    { "KB", 1000ull },
    { "MB", 1000ull * 1000 },
    { "GB", 1000ull * 1000 * 1000 },
    { "TB", 1000ull * 1000 * 1000 * 1000 },
    { "K", 1024ull },
    { "M", 1024ull * 1024 },
    { "G", 1024ull * 1024 * 1024 },
    { "T", 1024ull * 1024 * 1024 * 1024 },
};

bool mul_ok(u64 a, u64 b, u64 &out)
{
    if (b && a > SYS_SEEK_MAX / b)
        return false;
    out = a * b;
    return true;
}

bool add_ok(u64 a, u64 b, u64 &out)
{
    if (a > SYS_SEEK_MAX - b)
        return false;
    out = a + b;
    return true;
}

} // namespace

Result<SizeSpec> parse_size(Str s)
{
    SizeSpec spec;
    if (s.empty())
        return Err(Error::Invalid);

    switch (s[0]) {
    case '+': spec.mod = SizeMod::Plus; break;
    case '-': spec.mod = SizeMod::Minus; break;
    case '<': spec.mod = SizeMod::AtMost; break;
    case '>': spec.mod = SizeMod::AtLeast; break;
    case '/': spec.mod = SizeMod::RoundDown; break;
    case '%': spec.mod = SizeMod::RoundUp; break;
    default: break;
    }
    if (spec.mod != SizeMod::Set)
        s = s.substr(1);

    usize digits = 0;
    while (digits < s.size() && is_digit(s[digits]))
        digits++;
    if (digits == 0)
        return Err(Error::Invalid);

    for (usize i = 0; i < digits; i++) {
        u64 d = u64(s[i] - '0');
        if (spec.n > (SYS_SEEK_MAX - d) / 10)
            return Err(Error::Invalid);
        spec.n = spec.n * 10 + d;
    }

    Str unit = s.substr(digits);
    if (unit.empty())
        return spec;
    for (const Unit &u : UNITS) {
        if (unit != u.name)
            continue;
        if (!mul_ok(spec.n, u.scale, spec.n))
            return Err(Error::Invalid);
        return spec;
    }
    return Err(Error::Invalid);
}

Result<u64> size_apply(SizeSpec spec, u64 cur)
{
    u64 out = 0;
    switch (spec.mod) {
    case SizeMod::Set:
        return spec.n;
    case SizeMod::Plus:
        if (!add_ok(cur, spec.n, out))
            return Err(Error::Invalid);
        return out;
    case SizeMod::Minus:
        return cur > spec.n ? cur - spec.n : 0;
    case SizeMod::AtMost:
        return cur < spec.n ? cur : spec.n;
    case SizeMod::AtLeast:
        return cur > spec.n ? cur : spec.n;
    case SizeMod::RoundDown:
        if (!spec.n)
            return Err(Error::Invalid);
        return cur - cur % spec.n;
    case SizeMod::RoundUp:
        if (!spec.n)
            return Err(Error::Invalid);
        if (u64 over = cur % spec.n; over) {
            if (!add_ok(cur, spec.n - over, out))
                return Err(Error::Invalid);
            return out;
        }
        return cur;
    }
    return Err(Error::Invalid);
}
