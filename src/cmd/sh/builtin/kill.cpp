// Cancellation, which is all a kill can be for anything cooperative: a
// coroutine stops at its next await point and unwinds by returning. A program
// that never awaits cannot be stopped that way at all — that is what M9's
// worker and worker.terminate() are for, and a process really does die here.
//
// Jobs only. `Sys::Kill` refuses anything that is not a child of the caller
// (Concept.md §4.3), and a bare pid the shell did not start is exactly that —
// so `kill 12` is gone, and the authority it needed was never the shell's to
// have once the shell became a process.
#include "cmd/sh/job.h"
#include "decl.h"
#include "kernel/string.h"
#include "kernel/text.h"

namespace {

// `-9` and `-INT` alike. False for a word that is not a signal at all.
bool signal_arg(Str w, u32 &sig)
{
    if (!w.starts_with("-") || w.size() < 2)
        return false;
    w = w.substr(1);

    struct Named {
        Str name;
        u32 sig;
    };
    constexpr Named NAMES[] = {
        { "INT", SIG_INT },
        { "KILL", SIG_KILL },
        { "TERM", SIG_TERM },
        { "WINCH", SIG_WINCH },
    };

    Option<u32> n = parse_u32(w);
    for (const Named &e : NAMES)
        if (w == e.name || (n.has_value() && n.value() == e.sig)) {
            sig = e.sig;
            return true;
        }
    return false;
}

} // namespace

Task<i32> builtin_kill(Args args, ShIo io)
{
    usize first = 1;
    u32 sig     = SIG_KILL;

    // One signal, before the jobs, which is where every shell puts it.
    if (args.size() > 1 && args[1].starts_with("-")) {
        if (!signal_arg(args[1], sig)) {
            if (Task<void> e = errln("kill", args[1].substr(1), Error::Unsupported))
                co_await e;
            co_return 1;
        }
        first = 2;
    }

    if (first >= args.size()) {
        co_await write_all(io.err, "usage: kill [-<signal>] %n...\n");
        co_return 2;
    }

    i32 status = 0;
    String bad;
    for (usize i = first; i < args.size(); i++) {
        Str a = args[i];
        if (a.starts_with("%"))
            a = a.substr(1);

        Option<u32> n = parse_u32(a);
        bool ok       = false;
        if (n && n.value())
            if (Task<Result<void>> t = jobs_kill(n.value(), sig))
                ok = (co_await t).is_ok();
        if (!ok) {
            bad.append("kill: no such job\n");
            status = 1;
        }
    }

    if (!bad.empty())
        co_await write_all(io.err, bad.str());
    co_return status;
}
