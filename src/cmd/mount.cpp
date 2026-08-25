#include "kernel/fmt.h"
#include "proc/file.h"

// Listing the table is /proc/mounts, which the kernel publishes as text, so no
// operand reformats that rather than asking for an operation. Two operands are
// the operation: Sys::Mount, which refuses until §5.4 has an Fs to build.
Task<i32> proc_main(Args args)
{
    if (args.size() == 3) {
        Result<void> r = Err(Error::NoMemory);
        if (Task<Result<void>> t = mount_at(args[1], args[2]))
            r = co_await t;
        if (r.is_ok())
            co_return 0;
        if (r.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> e = errln("mount", args[1], r.error()))
            co_await e;
        co_return 1;
    }
    if (args.size() > 1) {
        co_await write_all(SYS_STDERR, "usage: mount [special mount_point]\n");
        co_return 2;
    }

    Result<String> r = Err(Error::NoMemory);
    if (Task<Result<String>> t = read_file("/proc/mounts"))
        r = co_await t;
    if (r.is_err()) {
        if (r.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> e = errln("mount", "/proc/mounts", r.error()))
            co_await e;
        co_return 1;
    }

    Str rest = r.value().str(), line;
    while (next_line(rest, line)) {
        Str prefix = next_field(line);
        Str kind   = next_field(line);
        Str mode   = next_field(line);
        if (prefix.empty())
            continue;

        Buf<96> b;
        b.put(prefix).put(" — ").put(kind);
        b.put(mode == "rw" ? Str(" (rw)\n") : Str(" (ro)\n"));
        if ((co_await File::stdout().write(b.str())).is_err())
            co_return 1;
    }
    if ((co_await File::stdout().flush()).is_err())
        co_return 1;
    co_return 0;
}
