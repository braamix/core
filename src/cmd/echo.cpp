#include "proc/file.h"

Task<i32> proc_main(Args args)
{
    // Full rather than Auto: the whole output is one flush, so the buffering
    // question is not worth a tty_of to answer.
    File::stdout().set_buffering(Buffering::Full);

    usize i      = 1;
    bool newline = true;
    if (i < args.size() && args[i] == "-n") {
        newline = false;
        i++;
    }

    for (bool first = true; i < args.size(); i++, first = false) {
        if (!first && (co_await File::stdout().write(" ")).is_err())
            co_return 1;
        if ((co_await File::stdout().write(args[i])).is_err())
            co_return 1;
    }
    if (newline && (co_await File::stdout().write("\n")).is_err())
        co_return 1;

    co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
}
