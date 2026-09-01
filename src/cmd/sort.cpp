#include "kernel/alloc.h"
#include "kernel/fmt.h"
#include "kernel/text.h"
#include "kernel/vec.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// Lines read into blocks that never move, a table of views over them, and an
// in-place heapsort. The whole input is held, so the sort is bounded by this
// process's memory (Concept.md §4.1).

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    sort [-bfnru] [-t <char>] [-k <key>]... [<file>...]\n"
    "Options:\n"
    "    -b    ignore blanks at the front of a field\n"
    "    -f    fold upper case onto lower\n"
    "    -n    compare an initial number by value\n"
    "    -r    reverse the order\n"
    "    -u    print one line of each equal run\n"
    "    -t    the field separator; blanks without it\n"
    "    -k    a key, <field>[.<byte>][bfnr], and after a comma\n"
    "          the field it ends in\n"
    "The input is held in memory.\n";

constexpr u8 SORT_B = 1, SORT_F = 2, SORT_N = 4, SORT_R = 8;

// One block's worth of lines; a longer line takes a block of its own.
constexpr usize SORT_BLOCK = 64 * 1024;

// How much output is gathered before a write.
constexpr usize SORT_ROWS = 4096;

// Fields are 1-based; f2 == 0 is the end of the line, c2 == 0 the end of the
// field. `own` means the key carries modifiers, which replace the globals.
struct SortKey {
    u32 f1   = 1;
    u32 c1   = 1;
    u32 f2   = 0;
    u32 c2   = 0;
    u8 flags = 0;
    bool own = false;
};

// Everything that outlives an await, in one heap block (Concept.md §8.2).
struct Sorter {
    Vec<String> blocks;
    Vec<Str> lines;
    Vec<SortKey> keys;
    u8 flags  = 0;
    bool uniq = false;
    char sep  = 0; // -t
};

bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

char fold_case(char c)
{
    return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
}

// A line into the last block with room, and a view of it into the table. A
// String never appended past its reserved capacity never reallocates, which is
// what keeps the view valid.
bool keep(Sorter &st, Str line)
{
    if (st.blocks.empty() || st.blocks.back().size() + line.size() > st.blocks.back().capacity()) {
        String b;
        if (!b.reserve(line.size() > SORT_BLOCK ? line.size() : SORT_BLOCK))
            return false;
        if (!st.blocks.push(move(b)))
            return false;
    }
    String &b = st.blocks.back();
    usize at  = b.size();
    if (!b.append(line))
        return false;
    return st.lines.push(Str(b.data() + at, line.size()));
}

// ------------------------------------------------------------------- fields

// Where field `n` begins. Its leading blanks belong to it, which is what -b is
// for; under -t the separator belongs to the field in front of it.
usize field_start(Str s, u32 n, char sep)
{
    usize i = 0;
    for (u32 k = 1; k < n; k++) {
        if (sep) {
            while (i < s.size() && s[i] != sep)
                i++;
            if (i < s.size())
                i++;
        } else {
            while (i < s.size() && is_blank(s[i]))
                i++;
            while (i < s.size() && !is_blank(s[i]))
                i++;
        }
    }
    return i;
}

usize field_end(Str s, u32 n, char sep)
{
    usize i = field_start(s, n, sep);
    if (sep) {
        while (i < s.size() && s[i] != sep)
            i++;
    } else {
        while (i < s.size() && is_blank(s[i]))
            i++;
        while (i < s.size() && !is_blank(s[i]))
            i++;
    }
    return i;
}

// The part of `line` a key names. No key at all is one whose f1 is 1 and whose
// f2 is 0, which is the whole line.
Str key_span(Str line, const SortKey &k, char sep, u8 flags)
{
    usize b = field_start(line, k.f1, sep);
    if (flags & SORT_B)
        while (b < line.size() && is_blank(line[b]))
            b++;
    b += k.c1 - 1;
    if (b > line.size())
        b = line.size();

    usize e = line.size();
    if (k.f2) {
        e = field_end(line, k.f2, sep);
        if (k.c2) {
            usize f = field_start(line, k.f2, sep);
            if (flags & SORT_B)
                while (f < line.size() && is_blank(line[f]))
                    f++;
            if (f + k.c2 < e)
                e = f + k.c2; // inclusive of the c2-th byte
        }
    }
    return e > b ? line.substr(b, e - b) : Str();
}

// ---------------------------------------------------------------- comparing

// An initial number, split rather than converted: no float, and no range.
struct Num {
    bool neg = false;
    Str ip; // integer digits, leading zeros dropped
    Str fp; // fraction digits, trailing zeros dropped
};

Num number(Str s)
{
    Num n;
    usize i = 0;
    while (i < s.size() && is_blank(s[i]))
        i++;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        n.neg = s[i] == '-';
        i++;
    }

    usize b = i;
    while (i < s.size() && is_digit(s[i]))
        i++;
    Str ip  = s.substr(b, i - b);
    usize z = 0;
    while (z < ip.size() && ip[z] == '0')
        z++;
    n.ip = ip.substr(z);

    if (i < s.size() && s[i] == '.') {
        b = ++i;
        while (i < s.size() && is_digit(s[i]))
            i++;
        Str fp = s.substr(b, i - b);
        while (!fp.empty() && fp[fp.size() - 1] == '0')
            fp = fp.substr(0, fp.size() - 1);
        n.fp = fp;
    }
    if (n.ip.empty() && n.fp.empty())
        n.neg = false; // no digits at all is zero, and zero has no sign
    return n;
}

int digits_cmp(Str a, Str b)
{
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    for (usize i = 0; i < a.size(); i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

// Column by column, a missing digit being zero.
int frac_cmp(Str a, Str b)
{
    usize n = a.size() > b.size() ? a.size() : b.size();
    for (usize i = 0; i < n; i++) {
        char x = i < a.size() ? a[i] : '0';
        char y = i < b.size() ? b[i] : '0';
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

int num_cmp(Str a, Str b)
{
    Num x = number(a), y = number(b);
    if (x.neg != y.neg)
        return x.neg ? -1 : 1;
    int r = digits_cmp(x.ip, y.ip);
    if (!r)
        r = frac_cmp(x.fp, y.fp);
    return x.neg ? -r : r;
}

// Bytes, which in UTF-8 is codepoint order. -f folds ASCII alone: a Cyrillic
// letter's case is two bytes, and this is not the place for it.
int text_cmp(Str a, Str b, u8 flags)
{
    usize n = a.size() < b.size() ? a.size() : b.size();
    for (usize i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (flags & SORT_F) {
            x = fold_case(x);
            y = fold_case(y);
        }
        if (x != y)
            return u8(x) < u8(y) ? -1 : 1;
    }
    if (a.size() == b.size())
        return 0;
    return a.size() < b.size() ? -1 : 1;
}

int cmp_lines(const Sorter &st, Str a, Str b)
{
    for (usize i = 0; i < st.keys.size(); i++) {
        const SortKey &k = st.keys[i];
        u8 f             = k.own ? k.flags : st.flags;
        Str x            = key_span(a, k, st.sep, f);
        Str y            = key_span(b, k, st.sep, f);
        int r            = (f & SORT_N) ? num_cmp(x, y) : text_cmp(x, y, f);
        if (r)
            return (f & SORT_R) ? -r : r;
    }
    // Equal keys are equal lines under -u; otherwise the whole line breaks the
    // tie, with every byte significant.
    if (st.uniq)
        return 0;
    int r = text_cmp(a, b, 0);
    return (st.flags & SORT_R) ? -r : r;
}

// ----------------------------------------------------------------- the sort

void sift(const Sorter &st, Vec<Str> &v, usize root, usize n)
{
    for (;;) {
        usize big = root, l = 2 * root + 1, r = l + 1;
        if (l < n && cmp_lines(st, v[big], v[l]) < 0)
            big = l;
        if (r < n && cmp_lines(st, v[big], v[r]) < 0)
            big = r;
        if (big == root)
            return;
        swap(v[root], v[big]);
        root = big;
    }
}

// Heapsort: O(n log n), no recursion, and no table but the one being sorted.
void heap_sort(Sorter &st)
{
    Vec<Str> &v = st.lines;
    usize n     = v.size();
    for (usize i = n / 2; i > 0; i--)
        sift(st, v, i - 1, n);
    for (usize e = n; e > 1; e--) {
        swap(v[0], v[e - 1]);
        sift(st, v, 0, e - 1);
    }
}

// ----------------------------------------------------------------- the keys

bool take_u32(Str s, usize &i, u32 &out)
{
    usize b = i;
    while (i < s.size() && is_digit(s[i]))
        i++;
    if (i == b)
        return false;
    Option<u32> v = parse_u32(s.substr(b, i - b));
    if (!v.has_value())
        return false;
    out = v.value();
    return true;
}

void take_mods(Str s, usize &i, SortKey &k)
{
    for (; i < s.size(); i++) {
        u8 bit = 0;
        switch (s[i]) {
        case 'b':
            bit = SORT_B;
            break;
        case 'f':
            bit = SORT_F;
            break;
        case 'n':
            bit = SORT_N;
            break;
        case 'r':
            bit = SORT_R;
            break;
        default:
            return;
        }
        k.flags |= bit;
        k.own = true;
    }
}

// POSIX -k spelling. v7's `+pos1 -pos2` is not accepted.
Result<void> parse_key(Str s, SortKey &k)
{
    usize i = 0;
    if (!take_u32(s, i, k.f1) || k.f1 == 0)
        return Err(Error::Invalid);
    if (i < s.size() && s[i] == '.') {
        i++;
        if (!take_u32(s, i, k.c1) || k.c1 == 0)
            return Err(Error::Invalid);
    }
    take_mods(s, i, k);

    if (i < s.size()) {
        if (s[i] != ',')
            return Err(Error::Invalid);
        i++;
        if (!take_u32(s, i, k.f2) || k.f2 == 0)
            return Err(Error::Invalid);
        if (i < s.size() && s[i] == '.') {
            i++;
            if (!take_u32(s, i, k.c2))
                return Err(Error::Invalid);
        }
        take_mods(s, i, k);
    }
    return i == s.size() ? Result<void>() : Result<void>(Err(Error::Invalid));
}

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("sort: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Sorter *st = heap_new<Sorter>();
    if (!st) {
        co_await write_all(SYS_STDERR, "sort: out of memory\n");
        co_return 1;
    }
    struct Free {
        ~Free() { heap_delete(p); }
        Sorter *p;
    } free_sorter{ st };

    OptParse p(args, Opts{ "bfnru", "kt" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            Str why = r.error() == Error::NotFound ? "needs a value" : "bad option";
            if (Task<i32> t = complain(why, Str(&o.name, 1)))
                co_return co_await t;
            co_return 1;
        }
        if (!r.value())
            break;
        switch (o.name) {
        case 'b':
            st->flags |= SORT_B;
            break;
        case 'f':
            st->flags |= SORT_F;
            break;
        case 'n':
            st->flags |= SORT_N;
            break;
        case 'r':
            st->flags |= SORT_R;
            break;
        case 'u':
            st->uniq = true;
            break;
        case 't':
            if (o.value.size() != 1) {
                if (Task<i32> t = complain("-t takes one character", o.value))
                    co_return co_await t;
                co_return 1;
            }
            st->sep = o.value[0];
            break;
        case 'k': {
            SortKey k;
            if (parse_key(o.value, k).is_err()) {
                if (Task<i32> t = complain("bad key", o.value))
                    co_return co_await t;
                co_return 1;
            }
            if (!st->keys.push(k))
                co_return 1;
            break;
        }
        default:
            break;
        }
    }

    // No -k is one key that is the whole line.
    if (st->keys.empty() && !st->keys.push(SortKey{}))
        co_return 1;

    Args rest = p.rest();
    Input files(rest, SYS_STDIN, "sort");
    String pending; // a line split across two chunks
    bool oom = false;
    for (;;) {
        Task<Result<String>> t = files.read();
        if (!t)
            co_return 1;
        Result<String> r = co_await t;
        if (r.is_err()) {
            if (r.error() == Error::Closed)
                break;
            co_return r.error() == Error::Cancelled ? 130 : 1;
        }

        // Split here rather than through a line reader: this program holds
        // no File, and a File's bytes buy nothing it needs.
        String chunk = move(r.value());
        Str s        = chunk.str();
        while (!s.empty()) {
            usize i = s.find('\n');
            if (i == Str::npos) {
                oom = !pending.append(s);
                break;
            }
            Str line = s.substr(0, i);
            if (!pending.empty()) {
                oom  = !pending.append(line);
                line = pending.str();
            }
            oom = oom || !keep(*st, line);
            pending.clear();
            if (oom)
                break;
            s = s.substr(i + 1);
        }
        if (oom)
            break;
    }
    // A final fragment with no newline is a line.
    if (!oom && !pending.empty())
        oom = !keep(*st, pending.str());
    if (oom) {
        co_await write_all(SYS_STDERR, "sort: out of memory\n");
        co_return 1;
    }

    heap_sort(*st);

    // Batched into one write: a write per line is a syscall per line.
    String rows;
    for (usize i = 0; i <= st->lines.size(); i++) {
        if (i < st->lines.size()) {
            if (st->uniq && i && cmp_lines(*st, st->lines[i - 1], st->lines[i]) == 0)
                continue;
            if (!rows.append(st->lines[i]) || !rows.push('\n')) {
                co_await write_all(SYS_STDERR, "sort: out of memory\n");
                co_return 1;
            }
            if (rows.size() < SORT_ROWS)
                continue;
        }
        if (rows.empty())
            continue;
        Task<Result<void>> t = write_all(SYS_STDOUT, rows.str());
        if (!t)
            co_return 1;
        if (Result<void> w = co_await t; w.is_err())
            co_return w.error() == Error::Cancelled ? 130 : 1;
        rows.clear();
    }
    co_return 0;
}
