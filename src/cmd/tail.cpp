#include "kernel/text.h"
#include "kernel/vec.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"

// A ring of the last `want` lines, so the input is read once and only the
// answer is held.
//
// The registry's tail, moved to a binary of its own (Concept.md §4). The body
// is the program it was; what changed is that its reads and writes are
// syscalls, and its ring is in sixteen megabytes that are nobody else's.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    tail [-n <count>] [<file>...]\n"
    "Options:\n"
    "    -n    how many lines; ten without it\n";

// The most the window may grow to before the whole file is read instead.
constexpr u64 TAIL_WINDOW_MAX = 1u << 20;

// The last line into the ring, oldest out.
bool ring_add(Vec<String> &ring, usize &head, u32 want, Str line)
{
    if (ring.size() < want) {
        String copy;
        return copy.assign(line) && ring.push(move(copy));
    }
    if (!ring[head].assign(line))
        return false;
    head = (head + 1) % ring.size();
    return true;
}

// Complete lines in `s`, a final fragment without a newline counting as one.
usize line_count(Str s)
{
    usize n = 0;
    for (char c : s)
        if (c == '\n')
            n++;
    if (!s.empty() && s[s.size() - 1] != '\n')
        n++;
    return n;
}

// `n` bytes at the descriptor's position.
Task<Result<String>> read_exactly(u32 fd, u64 n)
{
    String out;
    while (out.size() < n) {
        Result<String> r = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_chunk(fd))
            r = co_await t;
        if (r.is_err()) {
            if (r.error() == Error::Closed)
                break;
            co_return Err(r.error());
        }
        if (!out.append(r.value().str()))
            co_return Err(Error::NoMemory);
    }
    co_return move(out);
}

// The tail of `path` holding at least `want` whole lines, read from a window at
// the end rather than from the start. Err(Unsupported) means the caller must
// read the file through instead: the descriptor does not seek, or the window
// grew past its cap without holding enough.
Task<Result<String>> tail_window(u32 fd, u32 want)
{
    u64 end = 0;
    if (Task<Result<u64>> t = seek_fd(fd, 0, SYS_SEEK_END)) {
        Result<u64> r = co_await t;
        if (r.is_err())
            co_return Err(r.error() == Error::Cancelled ? Error::Cancelled : Error::Unsupported);
        end = r.value();
    }

    for (u64 window = SYS_CHUNK;; window *= 2) {
        u64 from = end > window ? end - window : 0;
        if (Task<Result<u64>> t = seek_fd(fd, i64(from), SYS_SEEK_SET))
            if (Result<u64> r = co_await t; r.is_err())
                co_return Err(r.error());

        Result<String> got = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_exactly(fd, end - from))
            got = co_await t;
        if (got.is_err())
            co_return Err(got.error());

        // A window that starts mid-line begins with a fragment of one that is
        // not ours to print.
        Str s = got.value().str();
        if (from > 0) {
            usize nl = s.find('\n');
            s        = nl == Str::npos ? Str() : s.substr(nl + 1);
        }
        if (from == 0 || line_count(s) >= want) {
            String out;
            if (!out.assign(s))
                co_return Err(Error::NoMemory);
            co_return move(out);
        }
        if (window >= TAIL_WINDOW_MAX)
            co_return Err(Error::Unsupported);
    }
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    u32 want    = 10;
    usize first = 1;
    if (args.size() >= 3 && args[1] == "-n") {
        Option<u32> n = parse_u32(args[2]);
        if (!n.has_value()) {
            co_return co_await usage_error(USAGE);
        }
        want  = n.value();
        first = 3;
    } else if (args.size() >= 2 && args[1].starts_with("-")) {
        co_return co_await usage_error(USAGE);
    }

    Args paths{ args.v.subspan(first) };
    Vec<String> ring;
    usize head = 0; // oldest, once the ring is full

    // One named file is read from the end. Several are one concatenation and
    // stdin does not seek, so both go through the loop below.
    bool windowed = false;
    if (want > 0 && paths.size() == 1) {
        Result<i32> fd = Err(Error::NoMemory);
        if (Task<Result<i32>> t = open_read(paths[0]))
            fd = co_await t;
        if (fd.is_err()) {
            if (fd.error() == Error::Cancelled)
                co_return 130;
            if (Task<void> e = errln("tail", paths[0], fd.error()))
                co_await e;
            co_return 1;
        }

        Result<String> got = Err(Error::NoMemory);
        if (Task<Result<String>> t = tail_window(u32(fd.value()), want))
            got = co_await t;
        if (Task<void> c = close_fd(u32(fd.value())))
            co_await c;

        if (got.is_err() && got.error() == Error::Cancelled)
            co_return 130;
        if (got.is_ok()) {
            Str rest = got.value().str();
            Str line;
            while (next_line(rest, line))
                if (!ring_add(ring, head, want, line))
                    co_return 1;
            windowed = true;
        }
        // Anything else falls through and reads the file the long way.
    }

    if (!windowed) {
        Input files(paths, SYS_STDIN, "tail");
        File in(files);
        String line;

        while (want > 0) {
            Result<bool> r = co_await in.getline(line);
            if (r.is_err())
                co_return r.error() == Error::Cancelled ? 130 : 1;
            if (!r.value())
                break;
            if (!ring_add(ring, head, want, line.str()))
                co_return 1;
        }
    }

    for (usize i = 0; i < ring.size(); i++) {
        String &s = ring[(head + i) % ring.size()];
        if (!s.push('\n'))
            co_return 1;
        if ((co_await File::stdout().write(s.str())).is_err())
            co_return 1;
    }

    if ((co_await File::stdout().flush()).is_err())
        co_return 1;
    co_return 0;
}
