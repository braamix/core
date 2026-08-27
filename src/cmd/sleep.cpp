#include "kernel/text.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// Seconds, or milliseconds with -m.

namespace {

constexpr u32 MAX_SECS = 4294967; // as many as convert to ms inside a u32

constexpr Str USAGE =
    "Usage:\n"
    "    sleep [-m] <seconds>\n"
    "Options:\n"
    "    -m    the number is milliseconds\n";

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    usize i    = 1;
    bool milli = i < args.size() && args[i] == "-m";
    if (milli)
        i++;

    Option<u32> n = args.size() == i + 1 ? parse_u32(args[i]) : None;
    if (!n.has_value() || (!milli && n.value() > MAX_SECS)) {
        co_return co_await usage_error(USAGE);
    }
    u32 ms = milli ? n.value() : n.value() * 1000;

    Result<void> r = Err(Error::NoMemory);
    if (Task<Result<void>> t = sleep_for(ms))
        r = co_await t;
    if (r.is_err())
        co_return 130;

    co_return 0;
}
