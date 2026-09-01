#include "kernel/fmt.h"
#include "kernel/text.h"
#include "kernel/vec.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// Columns out of a line: bytes, characters or delimited fields. The list is
// ranges rather than a marked array, so nothing bounds a position or a line.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    cut -b|-c|-f <list> [-d <char>] [-s] [<file>...]\n"
    "Options:\n"
    "    -b    select these byte positions\n"
    "    -c    select these character positions\n"
    "    -f    select these fields\n"
    "    -d    the field separator; a tab without it\n"
    "    -s    drop the lines that hold no separator\n";

// How much output is gathered before a write.
constexpr usize CUT_ROWS = 4096;

// One-based and inclusive; `hi` 0 runs to the end of the line.
struct Pick {
    u32 lo = 0;
    u32 hi = 0;
};

// The selected positions, sorted and merged.
struct Picks {
    Vec<Pick> v;

    bool has(u32 n) const
    {
        for (usize i = 0; i < v.size(); i++) {
            if (n < v[i].lo)
                return false;
            if (v[i].hi == 0 || n <= v[i].hi)
                return true;
        }
        return false;
    }
};

constexpr Str ERR_OOM   = "out of memory";
constexpr Str ERR_LIST  = "not a list";
constexpr Str ERR_ZERO  = "a position starts at one";
constexpr Str ERR_ORDER = "the range ends before it starts";

// `N`, `N-`, `-N`, `N-M`, separated by commas or blanks. The diagnostic, or
// nothing when the list is good.
Str parse_list(Str s, Picks &out)
{
    Vec<Pick> raw;
    usize i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ',' || s[i] == ' ' || s[i] == '\t'))
            i++;
        if (i >= s.size())
            break;

        Pick r;
        bool open_lo = false;
        if (s[i] == '-') {
            open_lo = true;
            i++;
        }
        usize digits = 0;
        u64 n        = 0;
        while (i < s.size() && is_digit(s[i]) && n < 0x7fffffff) {
            n = n * 10 + u64(s[i] - '0');
            i++;
            digits++;
        }
        if (open_lo) {
            if (digits == 0)
                return ERR_LIST;
            r.lo = 1;
            r.hi = u32(n);
        } else {
            if (digits == 0)
                return ERR_LIST;
            r.lo = u32(n);
            r.hi = u32(n);
            if (i < s.size() && s[i] == '-') {
                i++;
                usize more = 0;
                u64 m      = 0;
                while (i < s.size() && is_digit(s[i]) && m < 0x7fffffff) {
                    m = m * 10 + u64(s[i] - '0');
                    i++;
                    more++;
                }
                r.hi = more ? u32(m) : 0;
                if (more && m == 0)
                    return ERR_ZERO;
            }
        }
        if (r.lo == 0)
            return ERR_ZERO;
        if (r.hi && r.hi < r.lo)
            return ERR_ORDER;
        if (i < s.size() && s[i] != ',' && s[i] != ' ' && s[i] != '\t')
            return ERR_LIST;
        if (!raw.push(r))
            return ERR_OOM;
    }
    if (raw.empty())
        return ERR_LIST;

    // Insertion sort by `lo`, then merge: a list is a handful of ranges.
    for (usize a = 1; a < raw.size(); a++) {
        Pick t  = raw[a];
        usize b = a;
        while (b > 0 && raw[b - 1].lo > t.lo) {
            raw[b] = raw[b - 1];
            b--;
        }
        raw[b] = t;
    }
    for (usize a = 0; a < raw.size(); a++) {
        if (!out.v.empty()) {
            Pick &back = out.v[out.v.size() - 1];
            if (back.hi == 0)
                continue;
            if (raw[a].lo <= back.hi + 1) {
                if (raw[a].hi == 0 || raw[a].hi > back.hi)
                    back.hi = raw[a].hi;
                continue;
            }
        }
        if (!out.v.push(raw[a]))
            return ERR_OOM;
    }
    return Str();
}

enum class Mode { Bytes, Runes, Fields };

struct Cutter {
    Picks picks;
    Mode how  = Mode::Bytes;
    char sep  = '\t';
    bool only = false; // -s
};

// Byte or character columns, out of one line.
bool cut_cols(const Cutter &c, Str line, String &rows)
{
    u32 col = 0;
    usize i = 0;
    while (i < line.size()) {
        usize n = 1;
        if (c.how == Mode::Runes) {
            char32_t ch = 0;
            n           = utf8_decode(line, i, ch);
            if (n == 0)
                n = 1;
        }
        col++;
        if (c.picks.has(col) && !rows.append(line.substr(i, n)))
            return false;
        i += n;
    }
    return rows.push('\n');
}

// Delimited fields. The output separator is the input's, between what is kept.
bool cut_fields(const Cutter &c, Str line, String &rows)
{
    if (line.find(c.sep) == Str::npos) {
        if (c.only)
            return true;
        return rows.append(line) && rows.push('\n');
    }

    u32 field = 0;
    usize i   = 0;
    bool put  = false;
    while (i <= line.size()) {
        usize e = line.substr(i).find(c.sep);
        usize n = e == Str::npos ? line.size() - i : e;
        field++;
        if (c.picks.has(field)) {
            if (put && !rows.push(c.sep))
                return false;
            if (!rows.append(line.substr(i, n)))
                return false;
            put = true;
        }
        if (e == Str::npos)
            break;
        i += n + 1;
    }
    return rows.push('\n');
}

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("cut: ").put(why);
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

    Cutter c;
    Str list;
    bool listed = false;
    bool delim  = false;
    OptParse p(args, Opts{ "s", "bcfd" });
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
        if (o.name == 's') {
            c.only = true;
        } else if (o.name == 'd') {
            if (o.value.size() != 1) {
                if (Task<i32> t = complain("the separator is one character", o.value))
                    co_return co_await t;
                co_return 1;
            }
            c.sep = o.value[0];
            delim = true;
        } else {
            if (listed) {
                if (Task<i32> t = complain("only one of -b, -c and -f", Str()))
                    co_return co_await t;
                co_return 1;
            }
            c.how  = o.name == 'b' ? Mode::Bytes : o.name == 'c' ? Mode::Runes : Mode::Fields;
            list   = o.value;
            listed = true;
        }
    }

    if (!listed) {
        if (Task<i32> t = complain("one of -b, -c and -f is needed", Str()))
            co_return co_await t;
        co_return 1;
    }
    if (c.how != Mode::Fields && (delim || c.only)) {
        if (Task<i32> t = complain("-d and -s go with -f", Str()))
            co_return co_await t;
        co_return 1;
    }
    if (Str bad = parse_list(list, c.picks); !bad.empty()) {
        if (Task<i32> t = complain(bad, list))
            co_return co_await t;
        co_return 1;
    }

    Input files(p.rest(), SYS_STDIN, "cut");

    String rows;
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

            oom = oom ||
                  !(c.how == Mode::Fields ? cut_fields(c, line, rows) : cut_cols(c, line, rows));
            pending.clear();
            if (oom)
                break;

            s = s.substr(i + 1);
            if (rows.size() >= CUT_ROWS) {
                Task<Result<void>> w = flush_rows(rows);
                if (!w)
                    co_return 1;
                if (Result<void> done = co_await w; done.is_err())
                    co_return done.error() == Error::Cancelled ? 130 : 1;
            }
        }
        if (oom)
            break;
    }

    // A final fragment with no newline is a line.
    if (!oom && !pending.empty())
        oom = !(c.how == Mode::Fields ? cut_fields(c, pending.str(), rows)
                                      : cut_cols(c, pending.str(), rows));
    if (oom) {
        co_await write_all(SYS_STDERR, "cut: out of memory\n");
        co_return 1;
    }

    Task<Result<void>> w = flush_rows(rows);
    if (!w)
        co_return 1;
    if (Result<void> done = co_await w; done.is_err())
        co_return done.error() == Error::Cancelled ? 130 : 1;
    co_return 0;
}
