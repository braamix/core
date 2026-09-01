#include "fs/path.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// The copy itself is proc/io.h's, shared with /bin/mv, which falls back to it
// wherever the store will not rename.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    cp [-r] [-fi] [-n] <src> <dst>\n"
    "    cp [-r] [-fi] [-n] <src>... <dir>\n"
    "Options:\n"
    "    -r    copy directories, and what is in them\n"
    "    -f    replace what stands at the destination\n"
    "    -i    ask before replacing it\n"
    "    -n    keep it, and copy nothing over it\n";

struct Flags {
    bool recurse = false; // -r
    bool force   = false; // -f
    bool ask     = false; // -i
    bool no_clob = false; // -n
};

// "overwrite <dst>? " and one line of the answer. mv.cpp's rule: anything but a
// leading y declines.
Task<Result<bool>> confirm(Str dst, LineReader &answers)
{
    co_await write_all(SYS_STDERR, "overwrite ");
    co_await write_all(SYS_STDERR, dst);
    co_await write_all(SYS_STDERR, "? ");

    String line;
    Result<bool> r = co_await answers.next(line);
    if (r.is_err())
        co_return Err(r.error());
    co_return r.value() && !line.empty() && (line.str()[0] == 'y' || line.str()[0] == 'Y');
}

// One source to one destination, both absolute.
Task<Result<void>> copy_one(Str from, Str to, Flags f, LineReader &answers)
{
    if (from == to)
        co_return Err(Error::Invalid);
    if (path_under(from, to))
        co_return Err(Error::Invalid);

    Result<FileInfo> src = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_of(from, false))
        src = co_await t;
    if (src.is_err())
        co_return Err(src.error());

    if (src.value().kind == SYS_KIND_DIR && !f.recurse)
        co_return Err(Error::IsDir);

    Result<FileInfo> dst = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_of(to, false))
        dst = co_await t;
    if (dst.is_err() && dst.error() != Error::NotFound)
        co_return Err(dst.error());

    if (dst.is_ok()) {
        if (f.no_clob)
            co_return {};
        if (!f.force && f.ask) {
            Result<bool> yes = Err(Error::NoMemory);
            if (Task<Result<bool>> t = confirm(to, answers))
                yes = co_await t;
            if (yes.is_err())
                co_return Err(yes.error());
            if (!yes.value())
                co_return {};
        }
        // Two directories merge; every other pair needs the name cleared, and
        // a link is replaced rather than written through.
        bool merge = src.value().kind == SYS_KIND_DIR && dst.value().kind == SYS_KIND_DIR;
        if (!merge && (src.value().kind != SYS_KIND_FILE || dst.value().kind != SYS_KIND_FILE)) {
            Result<void> d = Err(Error::NoMemory);
            if (Task<Result<void>> t = remove_path(to, true))
                d = co_await t;
            if (d.is_err())
                co_return d;
        }
    }

    if (src.value().kind == SYS_KIND_LINK) {
        Result<String> target = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_link(from))
            target = co_await t;
        if (target.is_err())
            co_return Err(target.error());
        if (Task<Result<void>> t = make_link(target.value().str(), to))
            co_return co_await t;
        co_return Err(Error::NoMemory);
    }
    if (src.value().kind == SYS_KIND_DIR) {
        if (Task<Result<void>> t = copy_tree(from, to))
            co_return co_await t;
        co_return Err(Error::NoMemory);
    }
    if (Task<Result<void>> t = copy_file(from, to))
        co_return co_await t;
    co_return Err(Error::NoMemory);
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    Flags f;
    OptParse p(args, Opts{ "rRfin", "" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            co_await write_all(SYS_STDERR, "cp: bad option\n");
            co_return co_await usage_error(USAGE);
        }
        if (!r.value())
            break;
        if (o.name == 'r' || o.name == 'R')
            f.recurse = true;
        else if (o.name == 'f')
            f.force = true;
        else if (o.name == 'i')
            f.ask = true;
        else
            f.no_clob = true;
    }

    Args rest = p.rest();
    if (rest.size() < 2)
        co_return co_await usage_error(USAGE);

    // Absolute throughout: copy_one compares its two paths, and a relative one
    // would not compare.
    Result<String> here = Err(Error::NoMemory);
    if (Task<Result<String>> t = cwd_get())
        here = co_await t;
    if (here.is_err())
        co_return here.error() == Error::Cancelled ? 130 : 1;

    String dest;
    if (path_resolve(here.value().str(), rest[rest.size() - 1], dest).is_err())
        co_return 1;

    // With one source the last operand is the new name; with more it is the
    // directory they go in.
    Result<FileInfo> s = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_of(dest.str(), true))
        s = co_await t;
    if (s.is_err() && s.error() == Error::Cancelled)
        co_return 130;

    bool into_dir = s.is_ok() && s.value().kind == SYS_KIND_DIR;
    if (!into_dir && rest.size() > 2) {
        co_return co_await usage_error(USAGE);
    }

    Input replies(Args{}, SYS_STDIN, "cp");
    LineReader answers(replies);

    i32 status = 0;
    for (usize i = 0; i + 1 < rest.size(); i++) {
        String from, to;
        if (path_resolve(here.value().str(), rest[i], from).is_err())
            co_return 1;
        if (into_dir) {
            if (path_join(dest.str(), path_basename(from.str()), to).is_err())
                co_return 1;
        } else if (!to.assign(dest.str())) {
            co_return 1;
        }

        if (from.str() != to.str() && path_under(from.str(), to.str())) {
            co_await write_all(SYS_STDERR, "cp: ");
            co_await write_all(SYS_STDERR, rest[i]);
            co_await write_all(SYS_STDERR, ": cannot copy a directory into itself\n");
            status = 1;
            continue;
        }

        Result<void> r = Err(Error::NoMemory);
        if (Task<Result<void>> t = copy_one(from.str(), to.str(), f, answers))
            r = co_await t;
        if (r.is_ok())
            continue;
        if (r.error() == Error::Cancelled)
            co_return 130;

        status = 1;
        if (Task<void> e = errln("cp", rest[i], r.error()))
            co_await e;
    }
    co_return status;
}
