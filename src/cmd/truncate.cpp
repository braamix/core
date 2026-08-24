#include "kernel/fmt.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/size.h"

namespace {

constexpr Str USAGE = "usage: truncate [-co] [-r <rfile>] [-s <size>] <file>...\n";

struct Want {
    SizeSpec spec;
    u64 basis   = 0; // -r's size
    bool sized  = false;
    bool ref    = false;
    bool create = true;
};

// The length this file is to have. Only a modifier needs the size it has now,
// and only without -r does that cost a seek.
Task<Result<u64>> length_of(const Want &w, u32 fd)
{
    if (w.spec.mod == SizeMod::Set && w.sized)
        co_return w.spec.n;

    u64 cur = w.basis;
    if (!w.ref) {
        Result<u64> end = Err(Error::NoMemory);
        if (Task<Result<u64>> t = seek_fd(fd, 0, SYS_SEEK_END))
            end = co_await t;
        if (end.is_err())
            co_return Err(end.error());
        cur = end.value();
    }
    if (!w.sized)
        co_return cur;
    co_return size_apply(w.spec, cur);
}

Task<Result<void>> one(const Want &w, Str path)
{
    Result<i32> fd = Err(Error::NoMemory);
    if (Task<Result<i32>> t = open_at(path, SYS_O_WRITE | (w.create ? SYS_O_CREATE : 0)))
        fd = co_await t;
    if (fd.is_err())
        co_return Err(fd.error());

    Result<u64> n = Err(Error::NoMemory);
    if (Task<Result<u64>> t = length_of(w, u32(fd.value())))
        n = co_await t;

    Result<void> r = n.is_err() ? Err(n.error()) : Result<void>();
    if (n.is_ok())
        if (Task<Result<void>> t = truncate_fd(u32(fd.value()), n.value()))
            r = co_await t;

    if (Task<void> k = close_fd(u32(fd.value())))
        co_await k;
    co_return r;
}

} // namespace

Task<i32> proc_main(Args args)
{
    Want w;
    Str rfile;
    bool blocks = false;

    OptParse p(args, Opts{ "co", "rs" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            Buf<64> b;
            b.put("truncate: ")
                .put(r.error() == Error::NotFound ? "option requires an argument -- "
                                                  : "illegal option -- ")
                .put(o.name)
                .put('\n');
            co_await write_all(SYS_STDERR, b.str());
            co_await write_all(SYS_STDERR, USAGE);
            co_return 2;
        }
        if (!r.value())
            break;
        if (o.name == 'c')
            w.create = false;
        else if (o.name == 'o')
            blocks = true;
        else if (o.name == 'r') {
            rfile = o.value;
            w.ref = true;
        } else if (o.name == 's') {
            Result<SizeSpec> s = parse_size(o.value);
            if (s.is_err()) {
                if (Task<void> e = errln("truncate", o.value, Error::Invalid))
                    co_await e;
                co_return 2;
            }
            w.spec  = s.value();
            w.sized = true;
        }
    }

    // -o counts the size in blocks, and may arrive either side of -s.
    if (blocks && w.sized) {
        if (w.spec.n > SYS_SEEK_MAX / SIZE_BLOCK) {
            if (Task<void> e = errln("truncate", "", Error::Invalid))
                co_await e;
            co_return 2;
        }
        w.spec.n *= SIZE_BLOCK;
    }

    Args rest = p.rest();
    if (rest.size() == 0 || (!w.sized && !w.ref)) {
        co_await write_all(SYS_STDERR, USAGE);
        co_return 2;
    }

    if (w.ref) {
        Result<FileInfo> st = Err(Error::NoMemory);
        if (Task<Result<FileInfo>> t = stat_of(rfile))
            st = co_await t;
        if (st.is_err()) {
            if (st.error() == Error::Cancelled)
                co_return 130;
            if (Task<void> e = errln("truncate", rfile, st.error()))
                co_await e;
            co_return 1;
        }
        w.basis = st.value().size;
    }

    i32 status = 0;
    for (usize i = 0; i < rest.size(); i++) {
        Result<void> r = Err(Error::NoMemory);
        if (Task<Result<void>> t = one(w, rest[i]))
            r = co_await t;
        if (r.is_ok())
            continue;
        if (r.error() == Error::Cancelled)
            co_return 130;
        // -c asked for no file to be made, so one that is not there is not an
        // error — it is the file this run was told to leave alone.
        if (!w.create && r.error() == Error::NotFound)
            continue;
        if (Task<void> e = errln("truncate", rest[i], r.error()))
            co_await e;
        status = 1;
    }
    co_return status;
}
