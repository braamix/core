#include "kernel/fmt.h"
#include "proc/file.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// The kernel publishes what the system is and what it runs on as /proc/host,
// so this reformats that file rather than asking for an operation of its own —
// mount.cpp's arrangement, for mount.cpp's reason.
//
// -a prints the whole block rather than packing it onto one line the way POSIX
// uname does. There is no POSIX contract here, and the block is what is worth
// reading: a browser discloses more about itself than four fields hold.
//
// The screen is not in the file: Sys::Tty answers it, -g is it alone, and a
// pipe has no width.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    uname [-a|-s|-r|-m|-g]\n"
    "Options:\n"
    "    -a    every field, and the host this runs on\n"
    "    -s    the system name, which is what no flag prints\n"
    "    -r    the release\n"
    "    -m    the machine\n"
    "    -g    this terminal's geometry\n";

// The value of one `name value` line, or empty when the file has no such name.
Str field(Str text, Str want)
{
    Str rest = text, line;
    while (next_line(rest, line)) {
        Str key = next_field(line);
        if (key != want)
            continue;

        // next_field stops at the run of spaces that pads the column and leaves
        // it on the line. Only that goes: a value may hold a space of its own.
        usize at = 0;
        while (at < line.size() && line[at] == ' ')
            at++;
        return line.substr(at);
    }
    return Str();
}

Task<i32> one(Str text, Str key)
{
    Str value = field(text, key);
    if (value.empty())
        co_return 1;

    Buf<96> b;
    b.put(value).put('\n');
    if ((co_await File::stdout().write(b.str())).is_err())
        co_return 1;
    co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
}

// This terminal's grid, zero when stdout is not the console. The one error is
// Cancelled.
Task<Result<Geometry>> grid()
{
    Result<TtyInfo> tty = Err(Error::Unsupported);
    if (Task<Result<TtyInfo>> t = tty_of(SYS_STDOUT))
        tty = co_await t;
    if (tty.is_err())
        co_return tty.error() == Error::Cancelled ? Result<Geometry>(Err(Error::Cancelled))
                                                  : Result<Geometry>(Geometry{});
    co_return tty.value().console ? tty.value().at : Geometry{};
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Str flag;
    if (args.size() > 2)
        co_return co_await usage_error(USAGE);
    if (args.size() == 2) {
        flag = args[1];
        if (flag != "-a" && flag != "-s" && flag != "-r" && flag != "-m" && flag != "-g")
            co_return co_await usage_error(USAGE);
    }

    // -g asks the terminal and not the file, so it is answered before the read.
    // Nothing to print down a pipe, which is the status a missing field has.
    if (flag == "-g") {
        Result<Geometry> at = Geometry{};
        if (Task<Result<Geometry>> t = grid())
            at = co_await t;
        if (at.is_err())
            co_return 130;
        if (!at.value().cols)
            co_return 1;

        Buf<32> b;
        b.put(at.value().cols).put('x').put(at.value().rows).put('\n');
        if ((co_await File::stdout().write(b.str())).is_err())
            co_return 1;
        co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
    }

    Result<String> r = Err(Error::NoMemory);
    if (Task<Result<String>> t = read_file("/proc/host"))
        r = co_await t;
    if (r.is_err()) {
        if (r.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> e = errln("uname", "/proc/host", r.error()))
            co_await e;
        co_return 1;
    }
    Str text = r.value().str();

    // The default is the system name, as uname's has always been.
    if (flag.empty() || flag == "-s")
        co_return co_await one(text, "system");
    if (flag == "-r")
        co_return co_await one(text, "release");
    if (flag == "-m")
        co_return co_await one(text, "machine");

    // -a: every field the file carries, in its order, with this terminal's
    // geometry after `machine`, where the kernel's own fields end.
    Result<Geometry> at = Geometry{};
    if (Task<Result<Geometry>> t = grid())
        at = co_await t;
    if (at.is_err())
        co_return 130;

    Str rest = text, line;
    while (next_line(rest, line)) {
        if (line.empty())
            continue;

        Buf<160> b;
        b.put(line).put('\n');
        if ((co_await File::stdout().write(b.str())).is_err())
            co_return 1;

        Str head = line;
        if (!at.value().cols || next_field(head) != "machine")
            continue;

        Buf<32> g;
        g.put("screen   ").put(at.value().cols).put('x').put(at.value().rows).put('\n');
        if ((co_await File::stdout().write(g.str())).is_err())
            co_return 1;
    }
    if ((co_await File::stdout().flush()).is_err())
        co_return 1;
    co_return 0;
}
