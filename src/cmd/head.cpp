#include "kernel/text.h"
#include "proc/file.h"

// Returning early is what stops the producer upstream: the stage runner hangs
// up this program's input, and the next write on the other side reports Closed.
// That is also what ends `cat /dev/random | head -c 8`.
Task<i32> proc_main(Args args)
{
    u32 want    = 10;
    bool bytes  = false;
    usize first = 1;
    if (args.size() >= 3 && (args[1] == "-n" || args[1] == "-c")) {
        Option<u32> n = parse_u32(args[2]);
        if (!n.has_value()) {
            co_await write_all(SYS_STDERR, "usage: head [-n <count>] [-c <count>] [<file>...]\n");
            co_return 2;
        }
        bytes = args[1] == "-c";
        want  = n.value();
        first = 3;
    } else if (args.size() >= 2 && args[1].starts_with("-")) {
        co_await write_all(SYS_STDERR, "usage: head [-n <count>] [-c <count>] [<file>...]\n");
        co_return 2;
    }

    Input files(Args{ args.v.subspan(first) }, SYS_STDIN, "head");

    File in(files);

    // Bytes, verbatim: the span is cut to what is left, so nothing is read that
    // is not printed, and nothing is appended to what is.
    if (bytes) {
        char buf[256];
        for (u32 left = want; left;) {
            usize take      = left < sizeof buf ? usize(left) : sizeof buf;
            Result<usize> r = co_await in.read(Span<char>(buf, take));
            if (r.is_err())
                break;
            if ((co_await File::stdout().write(Str(buf, r.value()))).is_err())
                co_return 1;
            left -= u32(r.value());
        }
        if ((co_await File::stdout().flush()).is_err())
            co_return 1;
        if (in.failed())
            co_return in.err() == Error::Cancelled ? 130 : 1;
        co_return 0;
    }

    String line;
    for (u32 seen = 0; seen < want; seen++) {
        Result<bool> r = co_await in.getline(line);
        if (r.is_err())
            co_return r.error() == Error::Cancelled ? 130 : 1;
        if (!r.value())
            break;
        if (!line.push('\n'))
            co_return 1;
        if ((co_await File::stdout().write(line.str())).is_err())
            co_return 1;
    }

    if ((co_await File::stdout().flush()).is_err())
        co_return 1;
    co_return 0;
}
