#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    cat [<file>...]\n";

} // namespace

// Chunks, not lines: cat is byte-exact, so a last line without a newline stays
// that way. Named files are read end to end as one stream.
Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Input files(args.tail(), SYS_STDIN, "cat");
    File in(files);
    File &out = File::stdout();

    // A span at a time. On the console the buffering stays a line.
    if (out.reserve(SYS_READ_MAX).is_err())
        co_return 1;

    char buf[256];
    for (;;) {
        Result<usize> n = co_await in.read(buf);
        if (n.is_err())
            break;
        if ((co_await out.write(Str(buf, n.value()))).is_err())
            break;
    }

    if ((co_await out.flush()).is_err())
        co_return 1;
    if (in.failed())
        co_return in.err() == Error::Cancelled ? 130 : 1;
    co_return 0;
}
