#include "ftoa.h"

#include "cvt/api.h"

namespace {

// prec 0 is float and 1 is double; pok 1 is strtod's, which ungets a partial
// match rather than failing on it.
Option<f64> scan_prec(Str s, usize &used, i32 *err, int prec)
{
    auto *p = reinterpret_cast<const unsigned char *>(s.data());
    Cur c   = Cur{ p, p, p + s.size(), 0 };
    f64 v   = __floatscan(&c, prec, 1);
    usize n = usize(c.p - c.start);
    // A lookahead past the end is ungot, so n can exceed the string only when
    // nothing matched at all.
    used = n < s.size() ? n : s.size();
    if (err)
        *err = c.err == 34 ? 34 : 0;
    if (used == 0 || c.err == 22)
        return None;
    return v;
}

// fmtfp.c's flag word: one bit per flag character, at 1 << (c - ' ').
int flag_bits(Str flags)
{
    constexpr int LEFT_ADJ = 1 << ('-' - ' ');
    constexpr int ZERO_PAD = 1 << ('0' - ' ');

    int fl = 0;
    for (usize i = 0; i < flags.size(); i++) {
        char c = flags[i];
        if (c == '#' || c == '0' || c == '-' || c == ' ' || c == '+')
            fl |= 1 << (c - ' ');
    }
    // printf's '-' overrides '0'. vfprintf drops the bit before calling the
    // engine; fmtfp.c is the engine alone, so it is dropped here.
    if (fl & LEFT_ADJ)
        fl &= ~ZERO_PAD;
    return fl;
}

// The signed integer after the 'e' of an %e conversion.
i32 exponent_of(Str s)
{
    usize i = 0;
    while (i < s.size() && s[i] != 'e' && s[i] != 'E')
        i++;
    if (i == s.size())
        return 0;
    i++;
    bool neg = i < s.size() && s[i] == '-';
    if (i < s.size() && (s[i] == '-' || s[i] == '+'))
        i++;
    i32 e = 0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++)
        e = e * 10 + (s[i] - '0');
    return neg ? -e : e;
}

} // namespace

Str fmt_f64(char *out, usize cap, f64 v, i32 prec, char style)
{
    Sink sink{ out, cap, 0 };
    fmt_fp(&sink, v, 0, prec, 0, style);
    return Str(out, sink.n < cap ? sink.n : cap);
}

Str fmt_f64_padded(char *out, usize cap, f64 v, i32 prec, char style, i32 width, Str flags)
{
    Sink sink{ out, cap, 0 };
    fmt_fp(&sink, v, width, prec, flag_bits(flags), style);
    return Str(out, sink.n < cap ? sink.n : cap);
}

Str fmt_f64_shortest(char *out, usize cap, f64 v)
{
    for (i32 digits = 1; digits <= 17; digits++) {
        // %e rather than %g, whose choice of form depends on the precision:
        // the question here is how many digits v needs, not how to print them.
        char t[32];
        Str s            = fmt_f64(t, sizeof t, v, digits - 1, 'e');
        Option<f64> back = parse_f64(s);
        if (!back.has_value())
            return fmt_f64(out, cap, v, 0, 'g'); // an infinity or a NaN
        if (__builtin_bit_cast(u64, back.value()) != __builtin_bit_cast(u64, v))
            continue;

        // %g goes to exponent form below the precision, so 100 at one digit is
        // 1e+02. Ask for enough to keep the plain form where there is one; the
        // trailing zeros it does not need are dropped anyway.
        i32 e    = exponent_of(s);
        i32 prec = (e >= -4 && e < 17 && e + 1 > digits) ? e + 1 : digits;
        return fmt_f64(out, cap, v, prec, 'g');
    }
    return fmt_f64(out, cap, v, 17, 'g');
}

Option<f64> parse_f64(Str s)
{
    usize used    = 0;
    Option<f64> v = scan_f64(s, used);
    if (!v.has_value() || used != s.size())
        return None;
    return v;
}

Option<f64> scan_f64(Str s, usize &used, i32 *err)
{
    return scan_prec(s, used, err, 1);
}

Option<f32> scan_f32(Str s, usize &used, i32 *err)
{
    Option<f64> v = scan_prec(s, used, err, 0);
    if (!v.has_value())
        return None;
    return f32(v.value());
}
