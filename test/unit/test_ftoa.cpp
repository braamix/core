// math/ftoa.h: musl's strtod and printf float engines, over a Str and a
// caller's buffer. After test_math: it stands on frexp, fmod and scalbn.
#include "harness.h"
#include "math/ftoa.h"
#include "math/math.h"

namespace {

f64 of_bits(u64 b)
{
    return __builtin_bit_cast(f64, b);
}

u64 to_bits(f64 v)
{
    return __builtin_bit_cast(u64, v);
}

void check_fmt(f64 v, i32 prec, char style, Str want)
{
    char out[64];
    Str got = fmt_f64(out, sizeof out, v, prec, style);
    if (got != want) {
        Buf<200> b;
        b.put("fmt_f64: got \"").put(got).put("\" want \"").put(want).put('"');
        test_check(false, b.str(), __FILE_NAME__, __LINE__);
    }
}

void check_parse(Str s, f64 want)
{
    Option<f64> got = parse_f64(s);
    bool ok         = got.has_value() && to_bits(got.value()) == to_bits(want);
    if (!ok) {
        Buf<200> b;
        b.put("parse_f64(\"").put(s).put("\") wrong");
        test_check(false, b.str(), __FILE_NAME__, __LINE__);
    }
}

void formatting()
{
    check_fmt(0.0, 2, 'f', "0.00");
    check_fmt(1.5, 2, 'f', "1.50");
    check_fmt(-1.5, 2, 'f', "-1.50");
    check_fmt(0.125, 3, 'f', "0.125");
    check_fmt(1.0 / 3.0, 6, 'f', "0.333333");
    check_fmt(2.5, 0, 'f', "2"); // to even, as printf's is
    check_fmt(3.5, 0, 'f', "4");

    check_fmt(1234.5, 3, 'e', "1.234e+03");
    check_fmt(1234.5, 3, 'E', "1.234E+03");
    check_fmt(0.0, 2, 'e', "0.00e+00");

    check_fmt(1234.5, 6, 'g', "1234.5");
    check_fmt(0.0001, 6, 'g', "0.0001");
    check_fmt(0.00001, 6, 'g', "1e-05");
    check_fmt(1e20, 6, 'g', "1e+20");
    check_fmt(100.0, 0, 'g', "1e+02"); // precision 0 means 1

    // The default precision is printf's six.
    check_fmt(1.0 / 3.0, -1, 'f', "0.333333");

    // %a is exact and its own round trip.
    check_fmt(1.0, -1, 'a', "0x1p+0");
    check_fmt(0.5, -1, 'a', "0x1p-1");

    // Infinity and NaN are words, and the sign survives.
    check_fmt(of_bits(0x7ff0000000000000ull), 2, 'f', "inf");
    check_fmt(of_bits(0xfff0000000000000ull), 2, 'f', "-inf");
    check_fmt(of_bits(0x7ff8000000000000ull), 2, 'f', "nan");
    check_fmt(of_bits(0x8000000000000000ull), 1, 'f', "-0.0");

    // A conversion longer than the buffer truncates rather than running off it.
    char small[4];
    Str cut = fmt_f64(small, sizeof small, 1234.5678, 4, 'f');
    CHECK(cut.size() == 4);
    CHECK(cut == "1234");
}

void parsing()
{
    check_parse("0", 0.0);
    check_parse("1", 1.0);
    check_parse("-1", -1.0);
    check_parse("  1.5", 1.5);
    check_parse("+1.5", 1.5);
    check_parse("1e10", 1e10);
    check_parse("1E10", 1e10);
    check_parse("1e-10", 1e-10);
    check_parse(".5", 0.5);
    check_parse("5.", 5.0);
    check_parse("0x1p+0", 1.0);
    check_parse("0x1.8p+1", 3.0);

    // -0.0 is not 0.0, and the parser must keep the difference.
    CHECK(to_bits(parse_f64("-0").value()) == to_bits(of_bits(0x8000000000000000ull)));

    check_parse("inf", of_bits(0x7ff0000000000000ull));
    check_parse("Infinity", of_bits(0x7ff0000000000000ull));
    check_parse("-inf", of_bits(0xfff0000000000000ull));
    CHECK(isnan(parse_f64("nan").value()));

    // The ends of the range: the largest finite, the smallest subnormal, and
    // what lies past each.
    check_parse("1.7976931348623157e308", of_bits(0x7fefffffffffffffull));
    check_parse("5e-324", of_bits(1));
    check_parse("1e400", of_bits(0x7ff0000000000000ull));
    check_parse("1e-400", 0.0);

    // Correctly rounded, which is the reason for taking musl's scanner rather
    // than a digit-at-a-time accumulation: these three are the classic misses.
    check_parse("0.1", of_bits(0x3fb999999999999aull));
    check_parse("2.2250738585072011e-308", of_bits(0x000fffffffffffffull));
    check_parse("8.98846567431158e307", of_bits(0x7fe0000000000000ull));

    // Not a number at all, or not wholly one.
    CHECK(!parse_f64("").has_value());
    CHECK(!parse_f64("abc").has_value());
    CHECK(!parse_f64("1.5x").has_value());
    CHECK(!parse_f64("1 2").has_value());
    CHECK(!parse_f64("+").has_value());

    // scan_f64 stops where parse_f64 refuses, and says how far it got.
    usize used    = 0;
    Option<f64> v = scan_f64("1.5x", used);
    CHECK(v.has_value() && v.value() == 1.5 && used == 3);
    used = 0;
    CHECK(!scan_f64("abc", used).has_value() && used == 0);
}

void round_trip()
{
    const u64 CASES[] = {
        to_bits(0.0),
        0x8000000000000000ull,
        1,
        0x000fffffffffffffull,
        to_bits(0.1),
        to_bits(1.0 / 3.0),
        to_bits(1.0),
        to_bits(1e300),
        to_bits(1e-300),
        0x7fefffffffffffffull,
        to_bits(3.141592653589793),
        to_bits(2.718281828459045),
    };
    for (u64 b : CASES) {
        char out[64];
        f64 v            = of_bits(b);
        Str s            = fmt_f64_shortest(out, sizeof out, v);
        Option<f64> back = parse_f64(s);
        if (!back.has_value() || to_bits(back.value()) != b) {
            Buf<200> t;
            t.put("round trip failed on \"").put(s).put('"');
            test_check(false, t.str(), __FILE_NAME__, __LINE__);
        }
    }

    // Shortest means shortest: these are the forms the digits ask for.
    char out[64];
    CHECK(fmt_f64_shortest(out, sizeof out, 1.0) == "1");
    CHECK(fmt_f64_shortest(out, sizeof out, 0.1) == "0.1");
    CHECK(fmt_f64_shortest(out, sizeof out, 1.5) == "1.5");
    CHECK(fmt_f64_shortest(out, sizeof out, 100.0) == "100");
}

void into_buf()
{
    Buf<64> b;
    put_f64(b, 1.5);
    CHECK(b.str() == "1.5");

    b.clear();
    put_f64(b, 1.0 / 3.0, 3, 'f').put(' ');
    put_f64(b, 2.0, 1, 'e');
    CHECK(b.str() == "0.333 2.0e+00");
}

} // namespace

void test_ftoa()
{
    test_begin("ftoa");

    formatting();
    parsing();
    round_trip();
    into_buf();
}
