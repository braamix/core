#include "kernel/fmt.h"
#include "kernel/text.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// Counts over the raw chunks, so nothing here depends on where a chunk breaks.
// Each operand is opened here rather than through Input, which reads them as
// one concatenation and would lose the boundary a per-file row needs.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    wc [-Lclmw] [<file>...]\n"
    "Options:\n"
    "    -l    count lines\n"
    "    -w    count words\n"
    "    -c    count bytes, undoing an earlier -m\n"
    "    -m    count characters, undoing an earlier -c\n"
    "    -L    the longest line's length\n";

// The count's column, as du's and df's are.
constexpr usize W_COUNT = 7;

// Which columns were asked for. `multi` is -m: a character rather than a byte.
struct Wanted {
    bool lines   = false;
    bool words   = false;
    bool chars   = false;
    bool longest = false;
    bool multi   = false;
};

struct Counts {
    u64 lines   = 0;
    u64 words   = 0;
    u64 chars   = 0;
    u64 longest = 0;
};

// The carry between chunks: a word and a line both span one.
struct Counter {
    Counts c;
    bool in_word = false;
    u64 cur      = 0; // the line being measured, for -L

    void feed(const Wanted &w, Str s)
    {
        for (usize i = 0; i < s.size(); i++) {
            char ch = s[i];
            // A continuation byte belongs to the rune in front of it, so -m
            // needs no decoder and no state across a chunk.
            bool rune = !w.multi || (u8(ch) & 0xC0) != 0x80;
            if (rune)
                c.chars++;
            if (ch == '\n') {
                c.lines++;
                if (cur > c.longest)
                    c.longest = cur;
                cur = 0;
            } else if (rune) {
                cur++;
            }
            if (is_space(ch))
                in_word = false;
            else if (!in_word) {
                in_word = true;
                c.words++;
            }
        }
    }

    // A final fragment with no newline is a line, which BSD's -L forgets.
    void done()
    {
        if (cur > c.longest)
            c.longest = cur;
        cur = 0;
    }
};

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("wc: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

// One row: a leading blank and a seven-wide field per column, then the name.
// The name goes through `row` and not the Buf, since a path may be longer.
Task<i32> emit(const Wanted &w, const Counts &c, Str name, String &row)
{
    Buf<64> b;
    if (w.lines)
        b.put(' ').put_right(c.lines, W_COUNT);
    if (w.words)
        b.put(' ').put_right(c.words, W_COUNT);
    if (w.chars)
        b.put(' ').put_right(c.chars, W_COUNT);
    if (w.longest)
        b.put(' ').put_right(c.longest, W_COUNT);

    row.clear();
    if (!row.append(b.str()))
        co_return 1;
    if (!name.empty() && (!row.push(' ') || !row.append(name)))
        co_return 1;
    if (!row.push('\n'))
        co_return 1;
    if (Result<void> r = co_await write_all(SYS_STDOUT, row.str()); r.is_err())
        co_return r.error() == Error::Cancelled ? 130 : 1;
    co_return 0;
}

Task<Result<void>> count_fd(u32 fd, const Wanted &w, Counter &st)
{
    for (;;) {
        Result<String> r = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_chunk(fd))
            r = co_await t;
        if (r.is_err()) {
            if (r.error() == Error::Closed)
                break;
            co_return Err(r.error());
        }
        st.feed(w, r.value().str());
    }
    st.done();
    co_return {};
}

// One operand, or stdin when `path` is empty.
Task<i32> wc_one(const Wanted &w, Str path, Counts &total, String &row)
{
    u32 fd   = SYS_STDIN;
    bool own = !path.empty();
    if (own) {
        Result<i32> o = Err(Error::NoMemory);
        if (Task<Result<i32>> t = open_read(path))
            o = co_await t;
        if (o.is_err()) {
            if (o.error() == Error::Cancelled)
                co_return 130;
            if (Task<void> e = errln("wc", path, o.error()))
                co_await e;
            co_return 1;
        }
        fd = u32(o.value());
    }

    Counter st;
    Result<void> read = {};

    // Bytes alone off a file are a stat, not a read. BSD's guard comes with
    // it: a pseudo-filesystem advertises no size, so /proc still reads.
    bool stated = false;
    if (own && w.chars && !w.multi && !w.lines && !w.words && !w.longest) {
        Result<FileInfo> s = Err(Error::NoMemory);
        if (Task<Result<FileInfo>> t = stat_fd(fd))
            s = co_await t;
        if (s.is_ok() && s.value().size > 0) {
            st.c.chars = s.value().size;
            stated     = true;
        }
    }

    if (!stated) {
        read = Err(Error::NoMemory);
        if (Task<Result<void>> t = count_fd(fd, w, st))
            read = co_await t;
    }

    if (own)
        if (Task<void> c = close_fd(fd))
            co_await c;

    if (read.is_err()) {
        if (read.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> e = errln("wc", own ? path : Str("stdin"), read.error()))
            co_await e;
        co_return 1;
    }

    total.lines += st.c.lines;
    total.words += st.c.words;
    total.chars += st.c.chars;
    if (st.c.longest > total.longest)
        total.longest = st.c.longest;

    Task<i32> t = emit(w, st.c, path, row);
    if (!t)
        co_return 1;
    co_return co_await t;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Wanted w;
    OptParse p(args, Opts{ "Lclmw", "" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            if (Task<i32> t = complain("bad option", Str(&o.name, 1)))
                co_return co_await t;
            co_return 1;
        }
        if (!r.value())
            break;
        if (o.name == 'l')
            w.lines = true;
        else if (o.name == 'w')
            w.words = true;
        else if (o.name == 'L')
            w.longest = true;
        else {
            // -c and -m cancel each other; the last one wins.
            w.chars = true;
            w.multi = o.name == 'm';
        }
    }
    if (!w.lines && !w.words && !w.chars && !w.longest)
        w.lines = w.words = w.chars = true;

    Args rest = p.rest();
    Counts total;
    String row;
    i32 status = 0;

    for (usize i = 0; i < (rest.size() ? rest.size() : 1); i++) {
        Task<i32> t = wc_one(w, rest.size() ? rest[i] : Str(), total, row);
        if (!t)
            co_return 1;
        if (i32 bad = co_await t) {
            if (bad == 130)
                co_return 130;
            status = 1;
        }
    }

    // The total counts the operands named, failed ones included.
    if (rest.size() > 1) {
        Task<i32> t = emit(w, total, "total", row);
        if (!t)
            co_return 1;
        if (i32 bad = co_await t)
            co_return bad;
    }
    co_return status;
}
