#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    pbpaste\n";

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);
    if (args.size() != 1)
        co_return co_await usage_error(USAGE);

    Result<String> got = Err(Error::NoMemory);
    if (Task<Result<String>> t = clip_get(false))
        got = co_await t;

    // Refused because this is not a user gesture, which it cannot be: the
    // keystroke that ran the command returned before the request left. Asking
    // for a paste turns that around — it is a gesture, and needs no permission.
    if (got.is_err() && got.error() == Error::Perm) {
        co_await write_all(SYS_STDERR, "pbpaste: press ⌘V or ctrl-V to paste\n");
        if (Task<Result<String>> t = clip_get(true))
            got = co_await t;
    }

    if (got.is_err()) {
        if (got.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> e = errln("pbpaste", Str(), got.error()))
            co_await e;
        co_return 1;
    }

    if ((co_await write_all(SYS_STDOUT, got.value().str())).is_err())
        co_return 1;
    co_return 0;
}
