#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    dirname <path>...\n";

// Text, not a path: nothing here opens anything. path.cpp's path_dirname takes
// a normalised absolute path and answers "f" for "foo".
Str dir_of(Str p)
{
    while (p.size() > 1 && p[p.size() - 1] == '/')
        p = p.substr(0, p.size() - 1);
    if (p == "/")
        return p;

    usize i = p.size();
    while (i > 0 && p[i - 1] != '/')
        i--;
    if (i == 0)
        return ".";

    // The separator itself, and any run before it: "a//b" is "a".
    usize j = i - 1;
    while (j > 0 && p[j - 1] == '/')
        j--;
    return j == 0 ? Str("/") : p.substr(0, j);
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    String out;
    for (usize i = 1; i < args.size(); i++)
        if (!out.append(dir_of(args[i])) || !out.push('\n'))
            co_return 1;
    if ((co_await write_all(SYS_STDOUT, out.str())).is_err())
        co_return 1;
    co_return 0;
}
