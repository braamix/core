#include "kernel/fmt.h"
#include "kernel/text.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/size.h"
#include "proc/usage.h"

// Returning early is what stops the producer upstream: the stage runner hangs
// up this program's input, and the next write on the other side reports Closed.
// That is also what ends `cat /dev/random | head -c 8`.
//
// Lines are split out of chunks here rather than read through File::getline:
// each file is opened here, for the per-file headers, and a File's bytes buy
// nothing else.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    head [-qv] [-n <count> | -c <count>] [<file>...]\n"
    "Options:\n"
    "    -n    how many lines; ten without it\n"
    "    -c    how many bytes instead\n"
    "    -q    never print the ==> <== header\n"
    "    -v    always print it\n";

// How much output is gathered before a write.
constexpr usize HEAD_ROWS = 4096;

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("head: ").put(why);
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

i32 io_bad(Error e)
{
    return e == Error::Cancelled ? 130 : 1;
}

// A count, with truncate(1)'s units. A modifier is not one.
bool count_of(Str s, u64 &out)
{
    Result<SizeSpec> r = parse_size(s);
    if (r.is_err() || r.value().mod != SizeMod::Set || r.value().n == 0)
        return false;
    out = r.value().n;
    return true;
}

// `-<digits>`, which BSD's obsolete() rewrites to -n.
bool num_flag(Str w)
{
    return w.size() >= 2 && w[0] == '-' && is_digit(w[1]);
}

// The span cut to what is left, so nothing is read that is not printed. What a
// read did not take stays on the descriptor.
Task<i32> head_bytes(u32 fd, u64 want, String &rows)
{
    for (u64 left = want; left;) {
        u32 take         = left > SYS_READ_MAX ? SYS_READ_MAX : u32(left);
        Result<String> r = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_some(fd, take))
            r = co_await t;
        if (r.is_err()) {
            if (r.error() == Error::Closed)
                break;
            co_return io_bad(r.error());
        }
        if (!rows.append(r.value().str()))
            co_return 1;
        left -= r.value().size();
        if (rows.size() >= HEAD_ROWS) {
            Task<Result<void>> w = flush_rows(rows);
            if (!w)
                co_return 1;
            if (Result<void> done = co_await w; done.is_err())
                co_return io_bad(done.error());
        }
    }
    co_return 0;
}

Task<i32> head_lines(u32 fd, u64 want, String &rows)
{
    u64 seen = 0;
    String pending; // a line split across two chunks

    while (seen < want) {
        Result<String> r = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_chunk(fd))
            r = co_await t;
        if (r.is_err()) {
            if (r.error() == Error::Closed)
                break;
            co_return io_bad(r.error());
        }

        String chunk = move(r.value());
        Str s        = chunk.str();
        while (!s.empty() && seen < want) {
            usize i = s.find('\n');
            if (i == Str::npos) {
                if (!pending.append(s))
                    co_return 1;
                break;
            }
            if (!rows.append(pending.str()) || !rows.append(s.substr(0, i + 1)))
                co_return 1;
            pending.clear();
            seen++;
            s = s.substr(i + 1);
            if (rows.size() >= HEAD_ROWS) {
                Task<Result<void>> w = flush_rows(rows);
                if (!w)
                    co_return 1;
                if (Result<void> done = co_await w; done.is_err())
                    co_return io_bad(done.error());
            }
        }
    }

    // A final fragment with no newline is a line, and gains none.
    if (seen < want && !pending.empty() && !rows.append(pending.str()))
        co_return 1;
    co_return 0;
}

Task<i32> head_one(u32 fd, u64 want, bool bytes, String &rows)
{
    Task<i32> t = bytes ? head_bytes(fd, want, rows) : head_lines(fd, want, rows);
    if (!t)
        co_return 1;
    co_return co_await t;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    u64 want    = 10;
    bool bytes  = false;
    bool by_n   = false;
    bool by_c   = false;
    bool quiet  = false;
    bool loud   = false;
    usize first = 1;

    // BSD's obsolete(): a leading run of -<digits> is -n, and the first word
    // that is not one ends it.
    while (first < args.size() && num_flag(args[first])) {
        if (!count_of(args[first].substr(1), want)) {
            if (Task<i32> t = complain("illegal line count", args[first].substr(1)))
                co_return co_await t;
            co_return 1;
        }
        by_n = true;
        first++;
    }

    // subspan(first - 1): OptParse starts at index 1, so the first unread word
    // has to land there.
    OptParse p(Args{ args.v.subspan(first - 1) }, Opts{ "qv", "cn" });
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
        if (o.name == 'q') {
            quiet = true;
            loud  = false;
        } else if (o.name == 'v') {
            loud  = true;
            quiet = false;
        } else {
            if (!count_of(o.value, want)) {
                Str why = o.name == 'n' ? "illegal line count" : "illegal byte count";
                if (Task<i32> t = complain(why, o.value))
                    co_return co_await t;
                co_return 1;
            }
            bytes = o.name == 'c';
            if (bytes)
                by_c = true;
            else
                by_n = true;
        }
    }
    if (by_n && by_c) {
        if (Task<i32> t = complain("can't combine line and byte counts", {}))
            co_return co_await t;
        co_return 1;
    }

    Args rest   = p.rest();
    bool banner = loud || (!quiet && rest.size() > 1);
    String rows;
    i32 status = 0;

    // No operand is stdin, which is never headed.
    if (rest.size() == 0) {
        Task<i32> t = head_one(SYS_STDIN, want, bytes, rows);
        if (!t)
            co_return 1;
        if (i32 bad = co_await t) {
            if (bad == 130)
                co_return 130;
            status = 1;
        }
    }

    bool begun = false; // whether a header has been printed yet
    for (usize i = 0; i < rest.size(); i++) {
        Result<i32> o = Err(Error::NoMemory);
        if (Task<Result<i32>> t = open_read(rest[i]))
            o = co_await t;
        if (o.is_err()) {
            if (o.error() == Error::Cancelled)
                co_return 130;
            if (Task<void> e = errln("head", rest[i], o.error()))
                co_await e;
            status = 1;
            continue;
        }
        u32 fd = u32(o.value());

        if (banner) {
            bool ok = (begun ? rows.push('\n') : true) && rows.append("==> ") &&
                      rows.append(rest[i]) && rows.append(" <==\n");
            if (!ok)
                co_return 1;
            begun = true;
        }

        Task<i32> t = head_one(fd, want, bytes, rows);
        i32 bad     = t ? co_await t : 1;
        if (Task<void> c = close_fd(fd))
            co_await c;
        if (bad == 130)
            co_return 130;
        if (bad)
            status = 1;
    }

    Task<Result<void>> w = flush_rows(rows);
    if (!w)
        co_return 1;
    if (Result<void> done = co_await w; done.is_err())
        co_return io_bad(done.error());
    co_return status;
}
