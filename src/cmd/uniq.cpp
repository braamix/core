#include "kernel/fmt.h"
#include "kernel/text.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// Adjacent lines compared, a run at a time. The line held is the run's first,
// and the count is written before it under -c.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    uniq [-cdu] [-f <n>] [-s <n>] [<file>...]\n"
    "Options:\n"
    "    -c    prefix each line with how many times it ran\n"
    "    -d    print only the lines that repeated\n"
    "    -u    print only the lines that did not\n"
    "    -f    ignore the first <n> blank-delimited fields\n"
    "    -s    ignore <n> further bytes\n";

// How much output is gathered before a write.
constexpr usize UNIQ_ROWS = 4096;

bool is_blank(char c)
{
    return c == ' ' || c == '\t';
}

// Where the compared part begins: past `fields` fields, then past `bytes`
// bytes. Bytes, not characters — a Cyrillic letter is two of them.
Str compared(Str s, u32 fields, u32 bytes)
{
    usize i = 0;
    for (u32 k = 0; k < fields; k++) {
        while (i < s.size() && is_blank(s[i]))
            i++;
        while (i < s.size() && !is_blank(s[i]))
            i++;
    }
    if (bytes > s.size() - i)
        i = s.size();
    else
        i += bytes;
    return s.substr(i);
}

// The flags, in one place.
struct Uniqer {
    bool count   = false;
    bool dups    = false;
    bool singles = false;
    u32 fields   = 0;
    u32 bytes    = 0;
};

// The run being counted, and what is written when it ends.
struct Runs {
    String held; // the run's first line, which is what prints
    String rows; // output gathered, written in batches
    u32 run   = 0;
    bool have = false;
};

// A group's line into the pending output, or nothing when the flags exclude it.
bool emit(const Uniqer &u, Runs &g)
{
    if ((u.dups && g.run < 2) || (u.singles && g.run != 1))
        return true;
    if (u.count) {
        Buf<16> b;
        b.put_right(u64(g.run), 4).put(' ');
        if (!g.rows.append(b.str()))
            return false;
    }
    return g.rows.append(g.held.str()) && g.rows.push('\n');
}

// One line: the run it continues, or the one it ends and the one it begins.
bool step(const Uniqer &u, Runs &g, Str line)
{
    if (g.have && compared(g.held.str(), u.fields, u.bytes) == compared(line, u.fields, u.bytes)) {
        g.run++;
        return true;
    }
    if (g.have && !emit(u, g))
        return false;
    if (!g.held.assign(line))
        return false;
    g.have = true;
    g.run  = 1;
    return true;
}

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("uniq: ").put(why);
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

    Uniqer u;
    OptParse p(args, Opts{ "cdu", "fs" });
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
        if (o.name == 'c') {
            u.count = true;
        } else if (o.name == 'd') {
            u.dups = true;
        } else if (o.name == 'u') {
            u.singles = true;
        } else {
            Option<u32> n = parse_u32(o.value);
            if (!n.has_value()) {
                if (Task<i32> t = complain("not a count", o.value))
                    co_return co_await t;
                co_return 1;
            }
            if (o.name == 'f')
                u.fields = n.value();
            else
                u.bytes = n.value();
        }
    }

    Args rest = p.rest();
    Input files(rest, SYS_STDIN, "uniq");

    Runs g;
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

        // Split here rather than through a line reader: a Task that answers
        // without suspending resumes its caller on the caller's own stack, so a
        // coroutine per line is a stack frame per line.
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

            oom = oom || !step(u, g, line);
            pending.clear();
            if (oom)
                break;

            s = s.substr(i + 1);
            if (g.rows.size() >= UNIQ_ROWS) {
                Task<Result<void>> w = write_all(SYS_STDOUT, g.rows.str());
                if (!w)
                    co_return 1;
                if (Result<void> done = co_await w; done.is_err())
                    co_return done.error() == Error::Cancelled ? 130 : 1;
                g.rows.clear();
            }
        }
        if (oom)
            break;
    }

    // A final fragment with no newline is a line, and the last run is written
    // here either way.
    if (!oom && !pending.empty())
        oom = !step(u, g, pending.str());
    if (!oom && g.have)
        oom = !emit(u, g);
    if (oom) {
        co_await write_all(SYS_STDERR, "uniq: out of memory\n");
        co_return 1;
    }

    if (!g.rows.empty()) {
        Task<Result<void>> w = write_all(SYS_STDOUT, g.rows.str());
        if (!w)
            co_return 1;
        if (Result<void> done = co_await w; done.is_err())
            co_return done.error() == Error::Cancelled ? 130 : 1;
    }
    co_return 0;
}
