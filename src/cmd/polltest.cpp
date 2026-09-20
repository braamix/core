#include "kernel/text.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/usage.h"

// The caller Sys::Poll needs (Concept.md §4.3): a program cannot be written
// that waits on two descriptors without it, so nothing in /bin would exercise
// one. Four scenarios, each one line at a prompt, and `polltest two` is the
// one a program would actually write.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    polltest two       two children, two pipes, whichever answers first\n"
    "    polltest -t <ms>   a pipe nobody writes, until the timeout\n"
    "    polltest busy      a read and a poll of a descriptor a poll holds\n"
    "    polltest wait      a poll nothing ends, for ^C\n";

// Every literal this program names, as a constant: a Str built from a literal
// at run time is a call to strlen, which is not here.
constexpr Str AV[]  = { "echo", "first" };
constexpr Str BV[]  = { "sh", "-c", "sleep -m 100; echo second" };
constexpr Str TAG[] = { "a: ", "b: " };

constexpr Str READ_PERM = "read: perm\n";
constexpr Str POLL_BUSY = "poll: busy\n";
constexpr Str ODD_READ  = "read: unexpected\n";
constexpr Str ODD_POLL  = "poll: unexpected\n";
constexpr Str TIMED_OUT = "timeout\n";
constexpr Str WAS_READY = "ready\n";
constexpr Str WOKE      = "woke\n";
constexpr Str NOT_WOKE  = "failed\n";
constexpr Str PING      = "ping\n";

Task<Result<void>> say(Str s)
{
    if (Task<Result<void>> t = write_all(SYS_STDOUT, s))
        co_return co_await t;
    co_return Err(Error::NoMemory);
}

Task<i32> fail(Str what, Error e)
{
    if (Task<void> t = errln("polltest", what, e))
        co_await t;
    co_return e == Error::Intr || e == Error::Cancelled ? 130 : 1;
}

// A child writing into a pipe of its own. The write end is moved into it, so
// this process has no copy left open and the reader sees an end of input when
// the child goes.
Task<Result<i32>> start(Args cmd, u32 w)
{
    Result<u32> pid = Err(Error::NoMemory);
    if (Task<Result<u32>> t = spawn(cmd, ChildIo{ SYS_STDIN, w, SYS_STDERR }))
        pid = co_await t;
    if (pid.is_err())
        co_return Err(pid.error());
    co_return i32(pid.value());
}

// Two children, one answering at once and one after a delay, so the order the
// pipes are ready in is not the order they were started in. Each line is
// printed as it arrives, which is the whole point: reading the first pipe to
// the end would hold the second one up.
Task<i32> run_two()
{
    Result<Piped> a = Err(Error::NoMemory);
    Result<Piped> b = Err(Error::NoMemory);
    if (Task<Result<Piped>> t = make_pipe())
        a = co_await t;
    if (a.is_err())
        co_return co_await fail("pipe", a.error());
    if (Task<Result<Piped>> t = make_pipe())
        b = co_await t;
    if (b.is_err())
        co_return co_await fail("pipe", b.error());

    if (Task<Result<i32>> t = start(Args{ Span<const Str>(AV) }, u32(a.value().w)))
        if (Result<i32> r = co_await t; r.is_err())
            co_return co_await fail("echo", r.error());
    if (Task<Result<i32>> t = start(Args{ Span<const Str>(BV) }, u32(b.value().w)))
        if (Result<i32> r = co_await t; r.is_err())
            co_return co_await fail("sh", r.error());

    u32 fd[2]   = { u32(a.value().r), u32(b.value().r) };
    bool eof[2] = { false, false };

    while (!eof[0] || !eof[1]) {
        // Only what is still open is named: a descriptor at end of input is
        // ready for ever, and polling it again would spin.
        PollFd want[2];
        usize which[2];
        usize n = 0;
        for (usize i = 0; i < 2; i++) {
            if (eof[i])
                continue;
            want[n]  = PollFd{ fd[i], SYS_POLL_IN, 0 };
            which[n] = i;
            n++;
        }

        Result<usize> r = Err(Error::NoMemory);
        if (Task<Result<usize>> t = poll_fds(Span<PollFd>(want, n)))
            r = co_await t;
        if (r.is_err())
            co_return co_await fail("poll", r.error());

        for (usize k = 0; k < n; k++) {
            if (!(want[k].revents & SYS_POLL_IN))
                continue;
            Result<String> c = Err(Error::NoMemory);
            if (Task<Result<String>> t = read_some(want[k].fd, 64))
                c = co_await t;
            if (c.is_err()) {
                if (c.error() != Error::Closed)
                    co_return co_await fail("read", c.error());
                eof[which[k]] = true;
                continue;
            }
            if ((co_await say(TAG[which[k]])).is_err() ||
                (co_await say(c.value().str())).is_err())
                co_return 1;
        }
    }
    co_return 0;
}

// A pipe this process holds both ends of, so nothing can ever arrive on it.
Task<i32> run_timeout(u32 ms)
{
    Result<Piped> p = Err(Error::NoMemory);
    if (Task<Result<Piped>> t = make_pipe())
        p = co_await t;
    if (p.is_err())
        co_return co_await fail("pipe", p.error());

    PollFd one[] = { PollFd{ u32(p.value().r), SYS_POLL_IN, 0 } };
    Result<usize> r = Err(Error::NoMemory);
    if (Task<Result<usize>> t = poll_fds(Span<PollFd>(one), ms))
        r = co_await t;
    if (r.is_err())
        co_return co_await fail("poll", r.error());

    co_return (co_await say(r.value() == 0 ? TIMED_OUT : WAS_READY)).is_err() ? 1 : 0;
}

// Parked in a poll while the root task tries to use the same descriptor, then
// woken by what the root writes. The handshake pipe is how the root knows this
// task is through, since a process ends when its root returns.
Task<i32> waiter(u32 fd, u32 note)
{
    PollFd one[] = { PollFd{ fd, SYS_POLL_IN, 0 } };
    Result<usize> r = Err(Error::NoMemory);
    if (Task<Result<usize>> t = poll_fds(Span<PollFd>(one)))
        r = co_await t;
    if (r.is_ok())
        if (Task<Result<String>> t = read_some(fd, 64))
            co_await t;
    if (Task<Result<void>> t = write_all(note, r.is_ok() ? WOKE : NOT_WOKE))
        co_await t;
    co_return 0;
}

Task<i32> run_busy()
{
    Result<Piped> p = Err(Error::NoMemory);
    Result<Piped> h = Err(Error::NoMemory);
    if (Task<Result<Piped>> t = make_pipe())
        p = co_await t;
    if (p.is_err())
        co_return co_await fail("pipe", p.error());
    if (Task<Result<Piped>> t = make_pipe())
        h = co_await t;
    if (h.is_err())
        co_return co_await fail("pipe", h.error());

    // The task starts at once and runs until its first suspension, which is
    // the poll — so it holds the descriptor before anything below runs.
    if (!proc_spawn(waiter(u32(p.value().r), u32(h.value().w))))
        co_return co_await fail("task", Error::NoMemory);

    Result<String> rd = Err(Error::NoMemory);
    if (Task<Result<String>> t = read_some(u32(p.value().r), 16))
        rd = co_await t;
    if ((co_await say(rd.is_err() && rd.error() == Error::Perm ? READ_PERM : ODD_READ)).is_err())
        co_return 1;

    PollFd one[]    = { PollFd{ u32(p.value().r), SYS_POLL_IN, 0 } };
    Result<usize> r = Err(Error::NoMemory);
    if (Task<Result<usize>> t = poll_fds(Span<PollFd>(one), 0))
        r = co_await t;
    if ((co_await say(r.is_err() && r.error() == Error::Busy ? POLL_BUSY : ODD_POLL)).is_err())
        co_return 1;

    if (Task<Result<void>> t = write_all(u32(p.value().w), PING))
        if ((co_await t).is_err())
            co_return co_await fail("write", Error::Io);

    Result<String> note = Err(Error::NoMemory);
    if (Task<Result<String>> t = read_chunk(u32(h.value().r)))
        note = co_await t;
    if (note.is_err())
        co_return co_await fail("read", note.error());
    co_return (co_await say(note.value().str())).is_err() ? 1 : 0;
}

// A poll nothing but a signal can end.
Task<i32> run_wait()
{
    Result<Piped> p = Err(Error::NoMemory);
    if (Task<Result<Piped>> t = make_pipe())
        p = co_await t;
    if (p.is_err())
        co_return co_await fail("pipe", p.error());

    PollFd one[]    = { PollFd{ u32(p.value().r), SYS_POLL_IN, 0 } };
    Result<usize> r = Err(Error::NoMemory);
    if (Task<Result<usize>> t = poll_fds(Span<PollFd>(one)))
        r = co_await t;
    if (r.is_err())
        co_return co_await fail("poll", r.error());
    co_return (co_await say(WAS_READY)).is_err() ? 1 : 0;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() < 2 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    if (args[1] == "two")
        co_return co_await run_two();
    if (args[1] == "busy")
        co_return co_await run_busy();
    if (args[1] == "wait")
        co_return co_await run_wait();
    if (args[1] == "-t" && args.size() == 3) {
        Option<u32> ms = parse_u32(args[2]);
        if (!ms.has_value())
            co_return co_await usage_error(USAGE);
        co_return co_await run_timeout(ms.value());
    }
    co_return co_await usage_error(USAGE);
}
