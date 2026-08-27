#include "job.h"
#include "kernel/text.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"
#include "shell.h"

// The shell, as a program like any other (Concept.md §4). There is no in-kernel
// shell left to be §4's exception: what a prompt needs — a pipeline, a
// redirection, a job, a working directory, the keyboard and the cursor — is
// what the §4.3 syscall table is.
//
// Four ways in, and the argument arithmetic that picks $0 out of them is v7's
// (v7's own sh/args.c and sh/main.c): `sh -c cmd name args...` names $0 itself,
// a script file is its own $0, and otherwise $0 is argv[0].
namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    sh [-eux] [-s | -c <command> | <file>] [<arg>...]\n"
    "Options:\n"
    "    -c    run the argument as a command\n"
    "    -s    read commands from the input\n"
    "    -e    stop at the first command that fails\n"
    "    -u    a name that is not set is an error\n"
    "    -x    trace each command before it runs\n";

} // namespace

Task<i32> proc_main(Args args)
{
    ShellStart s;

    // Bare `sh` is a shell, so only the long spelling asks.
    if (args.size() == 2 && args[1] == "--help")
        co_return co_await usage_asked(USAGE);

    OptParse opts(args, Opts{ "seux", "c" });
    Opt o;
    for (;;) {
        Result<bool> more = opts.next(o);
        if (more.is_err()) {
            co_return co_await usage_error(USAGE);
        }
        if (!more.value())
            break;
        if (o.name == 's')
            s.from = ShellIn::Stdin;
        else if (o.name == 'c') {
            s.from = ShellIn::Command;
            s.text = o.value;
        } else
            s.flags |= sh_flag_of(o.name);
    }

    Args rest = opts.rest();
    s.name0   = args.name().empty() ? "sh"_s : args.name();

    // An operand with no -s and no -c is the script to run.
    if (s.from == ShellIn::Console && rest.size() > 0)
        s.from = ShellIn::File;

    if (s.from == ShellIn::File) {
        s.text  = rest[0];
        s.name0 = rest[0];
        rest    = rest.tail();
    } else if (s.from == ShellIn::Command && rest.size() > 0) {
        s.name0 = rest[0];
        rest    = rest.tail();
    }
    s.args = rest;

    Task<i32> t = shell(s);
    co_return t ? co_await t : 1;
}
