#include "kernel/fmt.h"
#include "kernel/vec.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    tee [-ai] [<file>...]\n"
    "Options:\n"
    "    -a    add to the files rather than truncating them\n"
    "    -i    keep going when ^C arrives\n";

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("tee: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

} // namespace

// A chunk in, the same chunk out to stdout and to every named file. No second
// buffer and no dribble: read_chunk hands over what arrived, so a pipeline
// echoes a line as it is typed.
Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    bool add    = false;
    bool nointr = false;
    OptParse p(args, Opts{ "ai", "" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            if (Task<i32> t = complain("bad option", Str(&o.name, 1)))
                co_return co_await t;
            co_return 1;
        }
        if (!r.value())
            break;
        if (o.name == 'a')
            add = true;
        else
            nointr = true;
    }

    if (nointr && (co_await sig_catch(SIG_INT)).is_err())
        co_return 1;

    // The descriptors this process opened, in the order they were named.
    Vec<i32> outs;
    i32 status = 0;
    Args files = p.rest();
    u32 flags  = SYS_O_WRITE | SYS_O_CREATE | (add ? SYS_O_APPEND : SYS_O_TRUNC);
    for (usize i = 0; i < files.size(); i++) {
        Result<i32> fd = co_await open_at(files[i], flags);
        if (fd.is_err()) {
            co_await errln("tee", files[i], fd.error());
            status = 1;
            continue;
        }
        if (!outs.push(fd.value())) {
            co_await close_fd(u32(fd.value()));
            status = 1;
        }
    }

    for (;;) {
        Result<String> r = co_await read_chunk(SYS_STDIN);
        if (r.is_err()) {
            if (r.error() == Error::Closed)
                break;
            if (r.error() == Error::Intr) {
                sig_take(SIG_INT);
                continue;
            }
            status = r.error() == Error::Cancelled ? 130 : 1;
            break;
        }

        // An output whose far end is gone ends the run, which is what stops
        // the producer upstream; anything else is reported and carried on.
        Str s     = r.value().str();
        bool gone = false;
        for (usize i = 0; i <= outs.size() && !gone; i++) {
            u32 fd            = i == 0 ? SYS_STDOUT : u32(outs[i - 1]);
            Result<void> done = co_await write_all(fd, s);
            if (done.is_err()) {
                gone = done.error() == Error::Closed;
                if (!gone)
                    status = done.error() == Error::Cancelled ? 130 : 1;
            }
        }
        if (gone)
            break;
    }

    for (usize i = 0; i < outs.size(); i++)
        co_await close_fd(u32(outs[i]));
    co_return status;
}
