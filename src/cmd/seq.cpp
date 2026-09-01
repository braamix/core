#include "kernel/fmt.h"
#include "kernel/text.h"
#include "math/ftoa.h"
#include "math/math.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// A range of doubles, printed. -f is printf's float conversion and nothing
// else, driven through the engine braam::math already has; -w derives one from
// the operands the way FreeBSD's generate_format does.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    seq [-w] [-f <fmt>] [-s <str>] [-t <str>]\n"
    "        [<first> [<incr>]] <last>\n"
    "Options:\n"
    "    -w    pad with zeros so every number is the same width\n"
    "    -f    a printf conversion for one number, %g without it\n"
    "    -s    what goes between them; a newline without it\n"
    "    -t    what goes after the last one\n";

// How much output is gathered before a write.
constexpr usize SEQ_ROWS = 4096;

// A conversion past these would be truncated inside the buffer without saying
// so, so it is refused instead. %f of a large value is 309 digits.
constexpr i32 SEQ_WIDTH_MAX = 400;
constexpr i32 SEQ_PREC_MAX  = 120;
constexpr usize SEQ_NUM_BUF = 560;

// One %[flags][width][.prec][conv] and the literal text around it. The Strs
// view the unescaped format, which must outlive this and must not be regrown.
struct Shape {
    Str head;
    Str tail;
    Str flags;
    i32 width  = 0;
    i32 prec   = -1;
    char style = 'g';
};

// FreeBSD's numeric(): where the options stop, not whether the number parses.
// Deliberately loose -- `1e` passes here and fails in parse_f64, which is the
// error the caller wants to see.
bool looks_numeric(Str s)
{
    usize i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+'))
        i++;
    bool dot = false;
    while (i < s.size()) {
        if (is_digit(s[i])) {
            i++;
            continue;
        }
        if (!dot && s[i] == '.') {
            i++;
            dot = true;
            continue;
        }
        if (s[i] == 'e' || s[i] == 'E') {
            i++;
            if (i < s.size() && (s[i] == '-' || s[i] == '+' || is_digit(s[i]))) {
                i++;
                continue;
            }
        }
        break;
    }
    return i == s.size();
}

// GNU's escapes, into a buffer of our own: argv is const here, so this is not
// FreeBSD's in-place shrink.
bool unescape(Str s, String &out)
{
    for (usize i = 0; i < s.size(); i++) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            if (!out.push(s[i]))
                return false;
            continue;
        }
        char e = s[++i];
        if (e >= '0' && e <= '7') {
            u32 v = 0;
            for (usize k = 0; k < 3 && i < s.size() && s[i] >= '0' && s[i] <= '7'; k++)
                v = v * 8 + u32(s[i++] - '0');
            i--;
            if (!out.push(char(v & 0xff)))
                return false;
            continue;
        }
        char c = e;
        switch (e) {
        case 'a':
            c = 7;
            break;
        case 'b':
            c = 8;
            break;
        case 'e':
            c = 27;
            break;
        case 'f':
            c = 12;
            break;
        case 'n':
            c = '\n';
            break;
        case 'r':
            c = '\r';
            break;
        case 't':
            c = '\t';
            break;
        case 'v':
            c = 11;
            break;
        case '\\':
        case '\'':
        case '"':
            break;
        default:
            // Anything else keeps its backslash, as it does upstream.
            if (!out.push('\\'))
                return false;
            break;
        }
        if (!out.push(c))
            return false;
    }
    return true;
}

// valid_format's grammar: one floating conversion, %% for a literal per cent,
// and anything else refused.
bool parse_fmt(Str f, Shape &out)
{
    u32 convs = 0;
    for (usize i = 0; i < f.size(); i++) {
        if (f[i] != '%')
            continue;
        i++;
        if (i < f.size() && f[i] == '%')
            continue;
        if (convs++)
            return false;
        out.head = f.substr(0, i - 1);

        // `'` is thousands grouping, which the C locale -- the only one here --
        // does not group by, so it parses and does nothing.
        usize at = i;
        while (i < f.size() && Str("#0- +'").find(f[i]) != Str::npos)
            i++;
        out.flags = f.substr(at, i - at);

        at = i;
        while (i < f.size() && is_digit(f[i]))
            i++;
        if (i > at) {
            Option<u32> n = parse_u32(f.substr(at, i - at));
            if (!n.has_value() || n.value() > u32(SEQ_WIDTH_MAX))
                return false;
            out.width = i32(n.value());
        }

        if (i < f.size() && f[i] == '.') {
            i++;
            at = i;
            while (i < f.size() && is_digit(f[i]))
                i++;
            out.prec = 0; // `%.f` is a precision of zero, as printf's is
            if (i > at) {
                Option<u32> n = parse_u32(f.substr(at, i - at));
                if (!n.has_value() || n.value() > u32(SEQ_PREC_MAX))
                    return false;
                out.prec = i32(n.value());
            }
        }

        if (i >= f.size() || Str("aAeEfFgG").find(f[i]) == Str::npos)
            return false;
        out.style = f[i];
        out.tail  = f.substr(i + 1);
    }
    return convs == 1;
}

// `%%` is one per cent; nothing else in the literal halves is special.
bool put_literal(String &rows, Str s)
{
    for (usize i = 0; i < s.size(); i++) {
        if (!rows.push(s[i]))
            return false;
        if (s[i] == '%' && i + 1 < s.size() && s[i + 1] == '%')
            i++;
    }
    return true;
}

// Not a coroutine: the buffer must not become part of proc_main's frame.
bool put_number(String &rows, const Shape &f, f64 v)
{
    char num[SEQ_NUM_BUF];
    Str s = fmt_f64_padded(num, sizeof num, v, f.prec, f.style, f.width, f.flags);
    return put_literal(rows, f.head) && rows.append(s) && put_literal(rows, f.tail);
}

// What generate_format wants out of a "%g": its printed width, its decimal
// places, and whether it came out in exponent form.
struct Rendered {
    i32 width  = 0;
    i32 places = 0;
    bool exp   = false;
};

Rendered render(f64 v)
{
    char t[32];
    Str s = fmt_f64(t, sizeof t, v, -1, 'g');
    Rendered h;
    h.width = i32(s.size());
    h.exp   = s.find('e') != Str::npos;
    usize d = s.find('.');
    if (d != Str::npos)
        for (usize i = d + 1; i < s.size() && is_digit(s[i]); i++)
            h.places++;
    return h;
}

// generate_format, minus sprintf. `last` is first rewritten as the value the
// loop will actually stop at rather than the bound asked for.
void equal_width(Shape &f, f64 first, f64 incr, f64 last)
{
    last = first > last ? first - incr * floor((first - last) / incr)
                        : first + incr * floor((last - first) / incr);

    Rendered hi = render(incr), h1 = render(first), h2 = render(last);

    // FreeBSD folds first's and incr's places into the precision and last's
    // into the width alone. Not symmetric, and not an accident.
    i32 prec = hi.places > h1.places ? hi.places : h1.places;
    i32 w1   = h1.width - (h1.places ? h1.places + 1 : 0);
    i32 w2   = h2.width - (h2.places ? h2.places + 1 : 0);
    i32 w    = w1 > w2 ? w1 : w2;
    bool exp = hi.exp || h1.exp || h2.exp;

    f.flags = "0";
    if (prec) {
        f.width = w + 1 + prec; // the point, and the fraction
        f.prec  = prec;
        f.style = exp ? 'e' : 'f';
    } else {
        f.width = w;
        f.prec  = -1;
        f.style = exp ? 'e' : 'g';
    }
}

// Did the loop stop on a rounding error rather than on the range? It did when
// the value that ended it prints as `last` does and not as the one before it.
bool missed_last(const Shape &f, f64 cur, f64 prev, f64 last)
{
    char a[SEQ_NUM_BUF], b[SEQ_NUM_BUF];
    Str sc = fmt_f64_padded(a, sizeof a, cur, f.prec, f.style, f.width, f.flags);
    Str sl = fmt_f64_padded(b, sizeof b, last, f.prec, f.style, f.width, f.flags);
    if (sc != sl)
        return false;
    Str sp = fmt_f64_padded(b, sizeof b, prev, f.prec, f.style, f.width, f.flags);
    return sc != sp;
}

Task<i32> complain(Str why, Str word)
{
    Buf<160> b;
    b.put("seq: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

Task<Result<void>> flush_rows(String &rows)
{
    if (rows.empty())
        co_return {};
    Task<Result<void>> w = write_all(SYS_STDOUT, rows.str());
    if (!w)
        co_return Err(Error::NoMemory);
    Result<void> done = co_await w;
    rows.clear();
    co_return done;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    Str raw_fmt, raw_term;
    Str raw_sep   = "\\n";
    bool has_fmt  = false;
    bool has_term = false;
    bool equalize = false;

    OptParse p(args, Opts{ "w", "fst" });
    for (Opt o;;) {
        // OptParse has no stop-at-a-negative-number rule and seq's operands may
        // begin with one. The word here is always at a bundle's start, and a
        // bundle begins with one of w f s t, so a numeric one is an operand.
        Args left = p.rest();
        if (left.size() && looks_numeric(left[0]))
            break;

        Result<bool> r = p.next(o);
        if (r.is_err()) {
            Str why = r.error() == Error::NotFound ? "needs a value" : "bad option";
            if (Task<i32> t = complain(why, Str(&o.name, 1)))
                co_return co_await t;
            co_return 1;
        }
        if (!r.value())
            break;
        if (o.name == 'w') {
            equalize = !has_fmt;
        } else if (o.name == 'f') {
            raw_fmt  = o.value;
            has_fmt  = true;
            equalize = false;
        } else if (o.name == 's') {
            raw_sep = o.value;
        } else {
            raw_term = o.value;
            has_term = true;
        }
    }

    Args rest = p.rest();
    if (rest.size() == 0 || rest.size() > 3)
        co_return co_await usage_error(USAGE);

    f64 num[3] = { 0, 0, 0 };
    for (usize i = 0; i < rest.size(); i++) {
        Option<f64> v = parse_f64(rest[i]);
        if (!v.has_value()) {
            if (Task<i32> t = complain("not a number", rest[i]))
                co_return co_await t;
            co_return 1;
        }
        if (!isfinite(v.value())) {
            if (Task<i32> t = complain("not a finite number", rest[i]))
                co_return co_await t;
            co_return 1;
        }
        num[i] = v.value();
    }

    f64 first = rest.size() > 1 ? num[0] : 1.0;
    f64 incr  = rest.size() > 2 ? num[1] : 1.0;
    f64 last  = num[rest.size() - 1];
    if (rest.size() > 2 && incr == 0.0) {
        if (Task<i32> t = complain("the increment is zero", rest[1]))
            co_return co_await t;
        co_return 1;
    }

    // The escaped strings the Shape's views point into, built before it.
    String fmt_text, sep, term;
    if (!unescape(raw_sep, sep) || !unescape(raw_term, term))
        co_return 1;

    Shape shape;
    if (has_fmt) {
        // Twice: an escape must not manufacture a conversion, nor a second one.
        if (!parse_fmt(raw_fmt, shape) || !unescape(raw_fmt, fmt_text) ||
            !parse_fmt(fmt_text.str(), shape)) {
            if (Task<i32> t = complain("not a format", raw_fmt))
                co_return co_await t;
            co_return 1;
        }
    } else if (equalize) {
        equal_width(shape, first, incr, last);
    }

    String rows;
    bool oom = false;
    bool any = false;
    f64 prev = first;
    f64 cur  = first;
    for (f64 step = 1; incr > 0 ? cur <= last : cur >= last; cur = first + incr * step++) {
        if (any && !rows.append(sep.str())) {
            oom = true;
            break;
        }
        if (!put_number(rows, shape, cur)) {
            oom = true;
            break;
        }
        prev = cur;
        any  = true;

        if (rows.size() >= SEQ_ROWS) {
            Task<Result<void>> w = flush_rows(rows);
            if (!w)
                co_return 1;
            if (Result<void> done = co_await w; done.is_err())
                co_return done.error() == Error::Cancelled ? 130 : 1;
        }
    }

    if (!oom && any && missed_last(shape, cur, prev, last))
        oom = !rows.append(sep.str()) || !put_number(rows, shape, last);
    if (!oom && has_term)
        oom = (any && !rows.append(sep.str())) || !rows.append(term.str());
    // An empty range is empty: the trailing newline belongs to output there is.
    if (!oom && (any || has_term))
        oom = !rows.push('\n');
    if (oom) {
        co_await write_all(SYS_STDERR, "seq: out of memory\n");
        co_return 1;
    }

    Task<Result<void>> w = flush_rows(rows);
    if (!w)
        co_return 1;
    if (Result<void> done = co_await w; done.is_err())
        co_return done.error() == Error::Cancelled ? 130 : 1;
    co_return 0;
}
