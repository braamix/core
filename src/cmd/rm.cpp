#include "kernel/fmt.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    rm [-rf] <path>...\n"
    "Options:\n"
    "    -r    remove directories, and what is in them\n"
    "    -f    pass over a path that is not there, silently\n";

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    bool all   = false;
    bool force = false;
    OptParse p(args, Opts{ "rf", "" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            Buf<64> b;
            b.put("rm: illegal option -- ").put(o.name).put('\n');
            co_await write_all(SYS_STDERR, b.str());
            co_return co_await usage_error(USAGE);
        }
        if (!r.value())
            break;
        if (o.name == 'r')
            all = true;
        else
            force = true;
    }

    Args rest = p.rest();
    if (rest.size() == 0) {
        if (force)
            co_return 0;
        co_return co_await usage_error(USAGE);
    }

    i32 status = 0;
    for (usize i = 0; i < rest.size(); i++) {
        Result<void> r = Err(Error::NoMemory);
        if (Task<Result<void>> t = remove_path(rest[i], all))
            r = co_await t;
        if (r.is_ok())
            continue;
        if (r.error() == Error::Cancelled)
            co_return 130;
        if (force && r.error() == Error::NotFound)
            continue;

        status = 1;
        if (Task<void> e = errln("rm", rest[i], r.error()))
            co_await e;
    }
    co_return status;
}
