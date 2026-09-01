#include "kernel/fmt.h"
#include "kernel/text.h"
#include "kernel/vec.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"

// v7's three 256-entry tables over codepoints instead. A finite class is walked
// out, so a position in string1 still pairs with one in string2; the classes
// that have no list stay predicates, and so does -c.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    tr [-cds] <string1> [<string2>]\n"
    "Options:\n"
    "    -c    use the characters not in <string1>\n"
    "    -d    delete them rather than translating\n"
    "    -s    squeeze runs of a repeated output character\n"
    "A set takes a-z ranges, \\n \\t \\\\ and \\ooo escapes, and the\n"
    "classes [:alnum:] [:alpha:] [:digit:] [:lower:] [:punct:]\n"
    "[:space:] and [:upper:].\n";

// The classes with no list behind them. [:digit:], [:punct:] and [:space:] are
// a short ASCII run each and are walked out at parse time instead.
enum : u32 {
    CLS_LOWER = 1u << 0,
    CLS_UPPER = 1u << 1,
    CLS_ALPHA = 1u << 2,
    CLS_ALNUM = 1u << 3,
};

bool ascii_alpha(char32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool ascii_digit(char32_t c)
{
    return c >= '0' && c <= '9';
}

// rune_lower and rune_upper are their own membership test: a letter with
// another case differs from itself under one of them.
bool is_lower(char32_t c)
{
    return rune_upper(c) != c;
}

bool is_upper(char32_t c)
{
    return rune_lower(c) != c;
}

bool in_class(u32 bits, char32_t c)
{
    bool letter = ascii_alpha(c) || is_lower(c) || is_upper(c);
    if ((bits & CLS_LOWER) && is_lower(c))
        return true;
    if ((bits & CLS_UPPER) && is_upper(c))
        return true;
    if ((bits & CLS_ALPHA) && letter)
        return true;
    return (bits & CLS_ALNUM) && (letter || ascii_digit(c));
}

// One operand, expanded. `runes` is what was written out, in order, so
// index_of pairs it with the other set.
struct RuneSet {
    Vec<char32_t> runes;
    u32 classes = 0;

    bool has(char32_t c) const
    {
        if (classes && in_class(classes, c))
            return true;
        for (usize i = 0; i < runes.size(); i++)
            if (runes[i] == c)
                return true;
        return false;
    }

    usize index_of(char32_t c) const
    {
        for (usize i = 0; i < runes.size(); i++)
            if (runes[i] == c)
                return i;
        return Str::npos;
    }

    bool empty() const { return runes.empty() && classes == 0; }
};

// A cursor over one set operand.
struct SetScan {
    Str s;
    usize at = 0;

    bool done() const { return at >= s.size(); }
    bool at_class() const { return at + 1 < s.size() && s[at] == '[' && s[at + 1] == ':'; }
};

// One codepoint, with GNU's escapes. \ooo names a codepoint, not a byte.
char32_t scan_rune(SetScan &t)
{
    char32_t c = 0;
    usize n    = utf8_decode(t.s, t.at, c);
    t.at += n ? n : 1;
    if (c != '\\' || t.done())
        return c;

    if (t.s[t.at] >= '0' && t.s[t.at] <= '7') {
        u32 v = 0;
        for (usize i = 0; i < 3 && !t.done() && t.s[t.at] >= '0' && t.s[t.at] <= '7'; i++)
            v = v * 8 + u32(t.s[t.at++] - '0');
        return char32_t(v);
    }

    char e = t.s[t.at++];
    switch (e) {
    case 'a':
        return 7;
    case 'b':
        return 8;
    case 'f':
        return 12;
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'v':
        return 11;
    default:
        return char32_t(u8(e)); // an unrecognised escape is the character
    }
}

// [:name:] at the cursor. `bits` is 0 for a finite class, whose members go into
// `runes`; false is a name that is not a class at all.
bool scan_class(SetScan &t, u32 &bits, Str &finite)
{
    usize e = t.s.substr(t.at + 2).find(':');
    if (e == Str::npos || t.at + 3 + e >= t.s.size() || t.s[t.at + 3 + e] != ']')
        return false;

    Str name = t.s.substr(t.at + 2, e);
    bits     = 0;
    finite   = Str();
    if (name == "lower")
        bits = CLS_LOWER;
    else if (name == "upper")
        bits = CLS_UPPER;
    else if (name == "alpha")
        bits = CLS_ALPHA;
    else if (name == "alnum")
        bits = CLS_ALNUM;
    else if (name == "digit")
        finite = "0123456789";
    else if (name == "space")
        finite = " \t\n\v\f\r";
    else if (name == "punct")
        finite = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    else
        return false;

    t.at += e + 4;
    return true;
}

constexpr Str ERR_OOM   = "out of memory";
constexpr Str ERR_CLASS = "not a character class";
constexpr Str ERR_ENDS  = "a class cannot end a range";
constexpr Str ERR_ORDER = "the range ends before it starts";

// The diagnostic, or nothing when the set is good.
Str expand(Str spec, RuneSet &out)
{
    SetScan t{ spec, 0 };
    while (!t.done()) {
        if (t.at_class()) {
            u32 bits = 0;
            Str finite;
            if (!scan_class(t, bits, finite))
                return ERR_CLASS;
            if (!t.done() && t.s[t.at] == '-' && t.at + 1 < t.s.size())
                return ERR_ENDS;
            out.classes |= bits;
            for (usize i = 0; i < finite.size(); i++)
                if (!out.runes.push(char32_t(u8(finite[i]))))
                    return ERR_OOM;
            continue;
        }

        char32_t lo = scan_rune(t);
        // A trailing '-' is a literal one, as it is upstream.
        if (t.done() || t.s[t.at] != '-' || t.at + 1 >= t.s.size()) {
            if (!out.runes.push(lo))
                return ERR_OOM;
            continue;
        }

        t.at++;
        if (t.at_class())
            return ERR_ENDS;
        char32_t hi = scan_rune(t);
        if (hi < lo)
            return ERR_ORDER;
        for (char32_t c = lo; c <= hi; c++)
            if (!out.runes.push(c))
                return ERR_OOM;
    }
    return Str();
}

// The three v7 tables, as three questions.
struct Table {
    RuneSet s1;
    RuneSet s2;
    char32_t (*pair)(char32_t) = nullptr; // the [:lower:] <-> [:upper:] mapping
    bool comp                  = false;
    bool del                   = false;
    bool sq                    = false;

    bool in1(char32_t c) const { return comp ? !s1.has(c) : s1.has(c); }

    // v7's padding rule: string2 is extended with its last element, and a
    // member of string1 with no string2 at all is itself.
    char32_t to(char32_t c) const
    {
        if (pair)
            return pair(c);
        if (s2.runes.empty())
            return c;
        usize i = comp ? Str::npos : s1.index_of(c);
        if (i == Str::npos || i >= s2.runes.size())
            return s2.runes[s2.runes.size() - 1];
        return s2.runes[i];
    }

    // v7 squeezed over string2 alone; POSIX squeezes over string1 when that is
    // the only operand, which is what `tr -s ' '` means.
    bool squeezed(char32_t c) const { return s2.empty() ? in1(c) : s2.has(c); }
};

// [:lower:] against [:upper:] and the reverse: a mapping rune_lower and
// rune_upper answer for every script they know, which no pairing of two lists
// could. Only `to` changes; -c, -d and -s read the class bits as before.
bool case_pair(Table &t)
{
    if (!t.s1.runes.empty() || !t.s2.runes.empty())
        return false;
    if (t.s1.classes == CLS_LOWER && t.s2.classes == CLS_UPPER)
        t.pair = rune_upper;
    else if (t.s1.classes == CLS_UPPER && t.s2.classes == CLS_LOWER)
        t.pair = rune_lower;
    return t.pair != nullptr;
}

// The combinations that cannot mean anything, checked once so the copy loop has
// no case to answer for.
Str tr_check(const Table &t, bool two)
{
    constexpr Str ERR_PAIR = "only the opposite case class can be a target";
    constexpr Str ERR_FOLD = "-c cannot be given a case mapping";
    constexpr Str ERR_ONE  = "-c wants a one-character string2";
    constexpr Str ERR_DEL  = "-d takes one set unless -s is given too";

    if (t.s2.classes && !t.pair)
        return ERR_PAIR;
    if (t.comp && t.pair)
        return ERR_FOLD;
    // With no index into string1, every complemented rune would take the same
    // element of string2.
    if (t.comp && !t.del && t.s2.runes.size() > 1)
        return ERR_ONE;
    if (t.del && two && !t.sq)
        return ERR_DEL;
    return Str();
}

Task<i32> complain(Str why, Str word)
{
    Buf<160> b;
    b.put("tr: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    Table t;
    OptParse p(args, Opts{ "cds", "" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            if (Task<i32> c = complain("bad option", Str(&o.name, 1)))
                co_return co_await c;
            co_return 1;
        }
        if (!r.value())
            break;
        if (o.name == 'c')
            t.comp = true;
        else if (o.name == 'd')
            t.del = true;
        else
            t.sq = true;
    }

    Args rest = p.rest();
    if (rest.size() == 0 || rest.size() > 2)
        co_return co_await usage_error(USAGE);

    for (usize i = 0; i < rest.size(); i++) {
        if (Str bad = expand(rest[i], i == 0 ? t.s1 : t.s2); !bad.empty()) {
            if (Task<i32> c = complain(bad, rest[i]))
                co_return co_await c;
            co_return 1;
        }
    }

    case_pair(t);
    if (Str why = tr_check(t, rest.size() == 2); !why.empty()) {
        if (Task<i32> c = complain(why, Str()))
            co_return co_await c;
        co_return 1;
    }

    File &in  = File::stdin();
    File &out = File::stdout();

    // U+0000 is an ordinary rune here, so "nothing written yet" needs a flag of
    // its own rather than v7's unproducible zero.
    char32_t save = 0;
    bool held     = false;
    for (;;) {
        Result<char32_t> r = co_await in.get();
        if (r.is_err())
            break;

        char32_t c = r.value();
        if (t.in1(c)) {
            if (t.del)
                continue;
            c = t.to(c);
        }
        if (t.sq && held && c == save && t.squeezed(c))
            continue;
        if ((co_await out.put(c)).is_err())
            break;
        save = c;
        held = true;
    }

    if ((co_await out.flush()).is_err())
        co_return 1;
    if (in.failed())
        co_return in.err() == Error::Cancelled ? 130 : 1;
    co_return 0;
}
