#include "cmd/sh/job.h"
#include "decl.h"
#include "kernel/fmt.h"
#include "kernel/string.h"
#include "kernel/text.h"

namespace {

// What can be trapped, by name and by number: 0 is the shell ending and the
// rest are SIG_CATCHABLE. Every other number is refused rather than kept.
struct Named {
    Str name;
    u32 sig;
};

constexpr Named NAMES[] = {
    { "EXIT", 0 },
    { "INT", SIG_INT },
    { "TERM", SIG_TERM },
    { "WINCH", SIG_WINCH },
};

bool signal_of(Str w, u32 &sig)
{
    Option<u32> n = parse_u32(w);
    for (const Named &e : NAMES)
        if (w == e.name || (n.has_value() && n.value() == e.sig)) {
            sig = e.sig;
            return true;
        }
    return false;
}

bool put_one(String &out, u32 sig)
{
    Str action;
    if (!trap_get(sig, action))
        return true;
    Buf<8> tail;
    tail.put("' ").put(sig).put('\n');
    return out.append("trap -- '") && out.append(action) && out.append(tail.str());
}

// The shell is told about a signal only while it has a trap for it: with none
// set the default action stands, which is what every version before this did.
Task<Result<void>> follow(u32 sig)
{
    if (!sig)
        co_return Result<void>{}; // EXIT is not a signal
    Str action;
    Task<Result<void>> t = sig_catch(sig, trap_get(sig, action));
    co_return t ? co_await t : Err(Error::NoMemory);
}

} // namespace

// A builtin because the action is a text this shell must run in its own walk,
// and the walk is what a child does not have.
Task<i32> builtin_trap(Args args, ShIo io)
{
    if (args.size() == 1) {
        String out;
        for (const Named &e : NAMES)
            if (!put_one(out, e.sig))
                co_return 1;
        if (Task<Result<void>> t = write_all(io.out, out.str()))
            co_return (co_await t).is_ok() ? 0 : 1;
        co_return 1;
    }

    Str action  = args[1];
    bool remove = action == "-";
    usize first = 2;

    // `trap 0` with no action is v7's reset, and the only form where the first
    // operand is a signal.
    u32 sig = 0;
    if (args.size() == 2 && signal_of(action, sig)) {
        remove = true;
        first  = 1;
    }

    if (first >= args.size()) {
        co_await write_all(io.err, "usage: trap [<action>|-] <signal>...\n");
        co_return 2;
    }

    for (usize i = first; i < args.size(); i++) {
        if (!signal_of(args[i], sig)) {
            if (Task<void> e = errln("trap", args[i], Error::Unsupported))
                co_await e;
            co_return 1;
        }
        // An empty action is v7's "ignore", which is a trap that runs nothing:
        // asking for the signal is what declines the default.
        if (remove)
            trap_clear(sig);
        else if (!trap_set(sig, action)) {
            co_await write_all(io.err, "trap: out of memory\n");
            co_return 1;
        }
        if (Task<Result<void>> t = follow(sig); !t || (co_await t).is_err()) {
            co_await write_all(io.err, "trap: cannot ask for that signal\n");
            co_return 1;
        }
    }
    co_return 0;
}
