#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    rm [-r] <path>...\n"
    "Options:\n"
    "    -r    remove directories, and what is in them\n";

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    bool all = false;
    usize i  = 1;
    for (; i < args.size(); i++) {
        if (args[i] == "-r")
            all = true;
        else
            break;
    }
    if (i >= args.size())
        co_return co_await usage_error(USAGE);

    i32 status = 0;
    for (; i < args.size(); i++) {
        Result<void> r = Err(Error::NoMemory);
        if (Task<Result<void>> t = remove_path(args[i], all))
            r = co_await t;
        if (r.is_ok())
            continue;
        if (r.error() == Error::Cancelled)
            co_return 130;

        status = 1;
        if (Task<void> e = errln("rm", args[i], r.error()))
            co_await e;
    }
    co_return status;
}
