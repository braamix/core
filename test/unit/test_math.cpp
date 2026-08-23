// braam::math, which is musl's libm (src/math/musl/). Three things are checked:
// the reference table in math.data to an ulp budget, the IEEE special cases the
// table cannot name, and the exactness the few exact routines promise.
#include "harness.h"
#include "kernel/fmt.h"
#include "math/math.h"

namespace {

struct MathCase {
    u64 x, y, want;
};

#include "math.data"

f64 of_bits(u64 b)
{
    return __builtin_bit_cast(f64, b);
}

u64 to_bits(f64 v)
{
    return __builtin_bit_cast(u64, v);
}

// Distance in representable steps, with the sign folded away so that either
// side of zero is one apart. U64_MAX when the two disagree about being finite.
u64 ulps(f64 got, f64 want)
{
    if (isnan(got) || isnan(want))
        return isnan(got) && isnan(want) ? 0 : ~0ull;
    if (got == want)
        return 0;
    if (isinf(got) || isinf(want))
        return ~0ull;

    auto order = [](f64 v) -> u64 {
        u64 b = to_bits(v);
        return b & (1ull << 63) ? ~b + 1 : b | (1ull << 63);
    };
    u64 a = order(got), b = order(want);
    return a > b ? a - b : b - a;
}

u64 worst = 0;
Str worst_at;

void check_table(Str name, const MathCase *cases, usize n, u64 budget, f64 (*fn)(f64))
{
    for (usize i = 0; i < n; i++) {
        u64 d = ulps(fn(of_bits(cases[i].x)), of_bits(cases[i].want));
        if (d > worst && d != ~0ull)
            worst = d, worst_at = name;
        if (d > budget) {
            Buf<200> b;
            b.put(name).put(" case ").put(i).put(": ").put_hex(u32(cases[i].x >> 32));
            b.put(' ').put_hex(u32(cases[i].x)).put(" off by ").put(u64(d)).put(" ulp");
            test_check(false, b.str(), __FILE_NAME__, __LINE__);
        }
    }
}

void check_table2(Str name, const MathCase *cases, usize n, u64 budget, f64 (*fn)(f64, f64))
{
    for (usize i = 0; i < n; i++) {
        u64 d = ulps(fn(of_bits(cases[i].x), of_bits(cases[i].y)), of_bits(cases[i].want));
        if (d > worst && d != ~0ull)
            worst = d, worst_at = name;
        if (d > budget) {
            Buf<200> b;
            b.put(name).put(" case ").put(i).put(" off by ").put(u64(d)).put(" ulp");
            test_check(false, b.str(), __FILE_NAME__, __LINE__);
        }
    }
}

#define TABLE(name, budget, fn)  check_table(name, fn##_CASES, ARRAY(fn##_CASES), budget, fn)
#define TABLE2(name, budget, fn) check_table2(name, fn##_CASES, ARRAY(fn##_CASES), budget, fn)
#define ARRAY(a)                 (sizeof(a) / sizeof(*(a)))

const f64 INF        = of_bits(0x7ff0000000000000ull);
const f64 QNAN       = of_bits(0x7ff8000000000000ull);
const f64 NZERO      = of_bits(0x8000000000000000ull);
const f64 DENORM_MIN = of_bits(1);

// Every branch below wants two ulp at most; the oracle is itself within one.
const u64 BUDGET = 2;

void reference_table()
{
    TABLE("acos", BUDGET, acos);
    TABLE("acosh", BUDGET, acosh);
    TABLE("asin", BUDGET, asin);
    TABLE("asinh", BUDGET, asinh);
    TABLE("atan", BUDGET, atan);
    TABLE("atanh", BUDGET, atanh);
    TABLE("cbrt", BUDGET, cbrt);
    TABLE("cos", BUDGET, cos);
    TABLE("cosh", BUDGET, cosh);
    TABLE("erf", BUDGET, erf);
    TABLE("erfc", BUDGET, erfc);
    TABLE("exp", BUDGET, exp);
    TABLE("exp2", BUDGET, exp2);
    TABLE("expm1", BUDGET, expm1);
    // lgamma has zeros at 1 and 2, and near one the cancellation costs digits
    // that no implementation gets back. musl is loosest there.
    TABLE("lgamma", 32, lgamma);
    TABLE("log", BUDGET, log);
    TABLE("log10", BUDGET, log10);
    TABLE("log1p", BUDGET, log1p);
    TABLE("log2", BUDGET, log2);
    TABLE("sin", BUDGET, sin);
    TABLE("sinh", BUDGET, sinh);
    TABLE("sqrt", 0, sqrt); // correctly rounded: it is f64.sqrt
    TABLE("tan", BUDGET, tan);
    TABLE("tanh", BUDGET, tanh);
    TABLE("tgamma", 4, tgamma); // musl's is the loosest of them

    TABLE2("atan2", BUDGET, atan2);
    TABLE2("fmod", 0, fmod); // exact by definition
    TABLE2("hypot", BUDGET, hypot);
    TABLE2("pow", BUDGET, pow);
    TABLE2("remainder", 0, remainder);
}

void classification()
{
    CHECK(isnan(QNAN));
    CHECK(!isnan(1.0));
    CHECK(isinf(INF) && isinf(-INF));
    CHECK(!isinf(QNAN));
    CHECK(isfinite(0.0) && isfinite(DENORM_MIN));
    CHECK(!isfinite(INF) && !isfinite(QNAN));
    CHECK(isnormal(1.0));
    CHECK(!isnormal(DENORM_MIN) && !isnormal(0.0) && !isnormal(QNAN));

    CHECK_EQ(fpclassify(QNAN), FP_NAN);
    CHECK_EQ(fpclassify(INF), FP_INFINITE);
    CHECK_EQ(fpclassify(0.0), FP_ZERO);
    CHECK_EQ(fpclassify(NZERO), FP_ZERO);
    CHECK_EQ(fpclassify(DENORM_MIN), FP_SUBNORMAL);
    CHECK_EQ(fpclassify(1.0), FP_NORMAL);

    // The whole of what the sign bit is for: -0.0 compares equal to 0.0 and is
    // not the same number.
    CHECK(signbit(NZERO));
    CHECK(!signbit(0.0));
    CHECK(NZERO == 0.0);
    CHECK(to_bits(copysign(1.0, NZERO)) == to_bits(-1.0));
    CHECK(to_bits(copysign(1.0, 0.0)) == to_bits(1.0));
    CHECK(1.0 / NZERO == -INF);
}

void special_values()
{
    // NaN in, NaN out, for every kernel that takes one.
    CHECK(isnan(sqrt(QNAN)) && isnan(exp(QNAN)) && isnan(log(QNAN)));
    CHECK(isnan(sin(QNAN)) && isnan(cos(QNAN)) && isnan(tan(QNAN)));
    CHECK(isnan(atan2(QNAN, 1.0)) && isnan(pow(QNAN, 2.0)) && isnan(fmod(QNAN, 1.0)));

    // The domain errors, which are NaN here and not errno.
    CHECK(isnan(sqrt(-1.0)));
    CHECK(isnan(log(-1.0)));
    CHECK(isnan(acos(2.0)) && isnan(asin(2.0)));
    CHECK(isnan(fmod(1.0, 0.0)));

    // The range errors, which are infinities and zeros.
    CHECK(log(0.0) == -INF);
    CHECK(exp(1000.0) == INF);
    CHECK(exp(-1000.0) == 0.0);
    CHECK(pow(0.0, -1.0) == INF);
    CHECK(to_bits(pow(NZERO, -3.0)) == to_bits(-INF));

    // C99's two: pow(1, anything) and pow(anything, 0) are 1, NaN included.
    CHECK(pow(1.0, QNAN) == 1.0);
    CHECK(pow(QNAN, 0.0) == 1.0);

    CHECK(sqrt(INF) == INF);
    CHECK(exp(INF) == INF && exp(-INF) == 0.0);
    CHECK(log(INF) == INF);
    CHECK(isnan(sin(INF)) && isnan(cos(INF)));
    CHECK(atan(INF) == M_PI_2);
    CHECK(tanh(INF) == 1.0 && tanh(-INF) == -1.0);
    CHECK(hypot(INF, QNAN) == INF); // C99: infinity wins over a NaN here

    // fmin and fmax treat a NaN as missing data, which is why f64.min and
    // f64.max -- which propagate it -- are not what answers them.
    CHECK(fmin(QNAN, 2.0) == 2.0);
    CHECK(fmax(QNAN, 2.0) == 2.0);
    CHECK(fmin(1.0, QNAN) == 1.0);
    CHECK(isnan(fmin(QNAN, QNAN)));

    // Zeros keep their sign through the routines that pass one along.
    CHECK(to_bits(sqrt(NZERO)) == to_bits(NZERO));
    CHECK(to_bits(sin(NZERO)) == to_bits(NZERO));
    CHECK(to_bits(cbrt(NZERO)) == to_bits(NZERO));
    CHECK(to_bits(trunc(-0.5)) == to_bits(NZERO));
    CHECK(to_bits(round(-0.25)) == to_bits(NZERO));
}

void rounding()
{
    // round is away from zero at the half, which floor(x + 0.5) gets wrong
    // twice: at 0.5 - 1ulp, and at every negative half.
    CHECK(round(0.5) == 1.0);
    CHECK(round(-0.5) == -1.0);
    CHECK(round(1.5) == 2.0);
    CHECK(round(2.5) == 3.0);
    CHECK(round(of_bits(to_bits(0.5) - 1)) == 0.0);

    // rint is to even, which is where it differs.
    CHECK(rint(0.5) == 0.0);
    CHECK(rint(1.5) == 2.0);
    CHECK(rint(2.5) == 2.0);
    CHECK(rint(-2.5) == -2.0);
    CHECK(nearbyint(2.5) == 2.0);

    CHECK(floor(-1.5) == -2.0 && ceil(-1.5) == -1.0);
    CHECK(floor(1.5) == 1.0 && ceil(1.5) == 2.0);
    CHECK(trunc(-1.9) == -1.0 && trunc(1.9) == 1.0);
    CHECK(lround(-2.5) == -3 && lround(2.5) == 3);
    CHECK(llround(0.5) == 1);
    CHECK(lrint(2.5) == 2);

    // Anything past 2^52 is already integral and comes back untouched.
    f64 big = 4503599627370497.0; // 2^52 + 1
    CHECK(floor(big) == big && ceil(big) == big && round(big) == big);
    CHECK(rint(big) == big && trunc(big) == big);
}

void exact()
{
    // fma keeps the low half of the product that x*y + z would round away.
    CHECK(fma(1e300, 1e300, -INF) == -INF);
    f64 a = 1.0 + of_bits(to_bits(1.0) + 1) - 1.0; // one ulp
    CHECK(fma(a, a, 0.0) == a * a);
    CHECK(fma(0x1.fffffffffffffp0, 0x1.fffffffffffffp0,
              -0x1.fffffffffffffp0 * 0x1.fffffffffffffp0) != 0.0);

    // frexp and ldexp are each other's inverse, subnormals included.
    const u64 SPLITS[] = { to_bits(1.0), to_bits(1e300), 1, to_bits(0.125) };
    for (u64 b : SPLITS) {
        i32 e = 0;
        f64 v = of_bits(b);
        f64 m = frexp(v, &e);
        CHECK(m >= 0.5 && m < 1.0);
        CHECK(to_bits(ldexp(m, e)) == b);
        CHECK(to_bits(scalbn(m, e)) == b);
    }

    // modf splits toward zero and gives both parts the sign of x.
    f64 ip = 0;
    CHECK(modf(-3.75, &ip) == -0.75 && ip == -3.0);
    CHECK(modf(3.75, &ip) == 0.75 && ip == 3.0);
    CHECK(to_bits(modf(NZERO, &ip)) == to_bits(NZERO) && to_bits(ip) == to_bits(NZERO));

    // nextafter walks one representable step, across zero and to infinity.
    CHECK(to_bits(nextafter(0.0, 1.0)) == 1);
    CHECK(to_bits(nextafter(0.0, -1.0)) == (1ull << 63) + 1);
    CHECK(nextafter(of_bits(0x7fefffffffffffffull), INF) == INF);
    CHECK(to_bits(nextafter(1.0, 2.0)) == to_bits(1.0) + 1);

    CHECK_EQ(ilogb(1.0), 0);
    CHECK_EQ(ilogb(0.5), -1);
    CHECK_EQ(ilogb(DENORM_MIN), -1074);
    CHECK(logb(8.0) == 3.0);

    i32 quo = 0;
    CHECK(remquo(5.0, 3.0, &quo) == -1.0 && quo == 2);
}

void identities()
{
    for (f64 x = -3.0; x < 3.0; x += 0.37) {
        CHECK(ulps(sin(x) * sin(x) + cos(x) * cos(x), 1.0) <= 2);
        CHECK(ulps(tanh(x), sinh(x) / cosh(x)) <= 4);
        CHECK(ulps(asinh(sinh(x)), x) <= 4 || x == 0.0);
    }
    for (f64 x = 0.1; x < 20.0; x += 1.7) {
        CHECK(ulps(exp(log(x)), x) <= 2);
        CHECK(ulps(sqrt(x) * sqrt(x), x) <= 2);
        CHECK(ulps(pow(x, 2.0), x * x) <= 2);
        CHECK(ulps(log2(x) * M_LN2, log(x)) <= 4);
        CHECK(ulps(cbrt(x * x * x), x) <= 4);
    }
    // The Bessel recurrence, which is what ties the family together.
    for (f64 x = 1.0; x < 10.0; x += 2.5)
        CHECK(ulps(j0(x) + jn(2, x), 2.0 / x * j1(x)) <= 64);

    // tgamma(n) is (n-1)! exactly for the small integers.
    CHECK(tgamma(5.0) == 24.0);
    CHECK(tgamma(11.0) == 3628800.0);
}

// The float half is real single-precision code, not a rounding of the double.
void singles()
{
    CHECK(sqrtf(4.0f) == 2.0f);
    CHECK(__builtin_fabsf(sinf(0.5f) - 0.479425538604203f) < 1e-7f);
    CHECK(__builtin_fabsf(expf(1.0f) - 2.718281828459045f) < 1e-6f);
    CHECK(__builtin_fabsf(logf(2.718281828459045f) - 1.0f) < 1e-6f);
    CHECK(powf(2.0f, 10.0f) == 1024.0f);
    CHECK(isnan(sqrtf(-1.0f)));
    CHECK(signbit(-0.0f));
    CHECK_EQ(fpclassify(0.0f), FP_ZERO);
}

} // namespace

void test_math()
{
    test_begin("math");

    reference_table();
    classification();
    special_values();
    rounding();
    exact();
    identities();
    singles();

    Buf<64> b;
    b.put("  math: worst reference error ").put(worst).put(" ulp, in ").put(worst_at);
    log(b.str());
}
