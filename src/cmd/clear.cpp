#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    clear\n";

} // namespace

// The terminal is a cell grid, not a byte stream (Concept.md §2.3), so there
// is no escape sequence to send: blanking it is an operation.
Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Result<SysReply> r = co_await sys_call(Sys::ScreenClear, 0);
    co_return r.is_err() ? 1 : 0;
}
