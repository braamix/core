#include "fs/path.h"
#include "kernel/fmt.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    basename <path> [<suffix>]\n"
    "    basename [-a] [-s <suffix>] <path>...\n"
    "Options:\n"
    "    -a    every operand is a path, none of them a suffix\n"
    "    -s    strip this suffix, and imply -a\n";

// Text, not a path: nothing here opens anything. path.cpp's path_basename
// takes a normalised absolute path, so the trailing slashes go first.
Str base_of(Str p, Str suffix)
{
    if (p.empty())
        return p;
    while (p.size() > 1 && p[p.size() - 1] == '/')
        p = p.substr(0, p.size() - 1);
    if (p == "/")
        return p;

    Str name = path_basename(p);
    // A name that is only the suffix keeps it, rather than coming out empty.
    if (!suffix.empty() && name != suffix && name.ends_with(suffix))
        name = name.substr(0, name.size() - suffix.size());
    return name;
}

} // namespace

Task<i32> proc_main(Args args)
{
    bool all = false;
    Str suffix;

    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    OptParse p(args, Opts{ "a", "s" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            Buf<64> b;
            b.put("basename: ")
                .put(r.error() == Error::NotFound ? "option requires an argument -- "
                                                  : "illegal option -- ")
                .put(o.name)
                .put('\n');
            co_await write_all(SYS_STDERR, b.str());
            co_return co_await usage_error(USAGE);
        }
        if (!r.value())
            break;
        if (o.name == 's')
            suffix = o.value;
        all = true; // -s implies -a
    }

    // Without either flag a second operand is the suffix, and there is no third.
    Args rest  = p.rest();
    usize take = rest.size();
    if (take == 0 || (!all && take > 2))
        co_return co_await usage_error(USAGE);
    if (!all && take == 2) {
        suffix = rest[1];
        take   = 1;
    }

    String out;
    for (usize i = 0; i < take; i++)
        if (!out.append(base_of(rest[i], suffix)) || !out.push('\n'))
            co_return 1;
    if ((co_await write_all(SYS_STDOUT, out.str())).is_err())
        co_return 1;
    co_return 0;
}
