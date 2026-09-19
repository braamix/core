#include "poll.h"

#include "io.h"
#include "kernel/sched.h"

namespace {

// One descriptor of one call. `in` and `out` are the streams behind it, unset
// in the direction that was not named; a File leaves both unset, which is what
// "always ready" is made of.
struct PollSlot {
    u32 fd      = 0;
    u32 events  = 0;
    u32 revents = 0;
    Handle *h   = nullptr; // held, or null for a descriptor below SYS_FD_MIN
    Source in;
    Stream out;
    bool pend = false; // what a short read left: readable without asking
};

void poll_drop(Proc &p, PollSlot &s)
{
    if (!s.h) {
        p.io_busy[s.fd] = false;
        return;
    }
    if (s.events & SYS_POLL_IN)
        s.h->busy_r = false;
    if (s.events & SYS_POLL_OUT)
        s.h->busy_w = false;
    handle_release(s.h);
}

// Takes the one-user-per-direction guard, and a reference on the handle so a
// Close in another task cannot free it under a parked poll. False is a
// descriptor somebody already holds — including this call, which is what
// naming one twice amounts to.
bool poll_take(Proc &p, PollSlot &s)
{
    if (!s.h) {
        if (p.io_busy[s.fd])
            return false;
        p.io_busy[s.fd] = true;
        return true;
    }
    bool r = s.events & SYS_POLL_IN;
    bool w = s.events & SYS_POLL_OUT;
    if ((r && s.h->busy_r) || (w && s.h->busy_w))
        return false;
    s.h->refs++;
    if (r)
        s.h->busy_r = true;
    if (w)
        s.h->busy_w = true;
    return true;
}

// What the descriptor is, and whether it can answer the directions asked. A
// direction a descriptor does not have would never be ready, so it is
// Err(Invalid) rather than a wait nothing ends.
Result<void> poll_open(Proc &p, PollSlot &s)
{
    if (s.events & ~SYS_POLL_ASKED)
        return Err(Error::Invalid);

    if (s.fd < SYS_FD_MIN) {
        if (s.fd == SYS_STDIN) {
            if (s.events & SYS_POLL_OUT)
                return Err(Error::Invalid);
            s.in   = p.io.in;
            s.pend = !p.in_pend.empty();
            return {};
        }
        if (s.events & SYS_POLL_IN)
            return Err(Error::Invalid);
        s.out = s.fd == SYS_STDOUT ? p.io.out : p.io.err;
        return {};
    }

    Handle *h = proc_handle(p, s.fd);
    if (!h)
        return Err(Error::Invalid);

    switch (h->kind) {
    case Handle::Kind::PipeRead:
        if (s.events & SYS_POLL_OUT)
            return Err(Error::Invalid);
        s.in   = pipe_source(h->pipe.q->ch);
        s.pend = !h->pend.empty();
        break;
    case Handle::Kind::PipeWrite:
        if (s.events & SYS_POLL_IN)
            return Err(Error::Invalid);
        s.out = pipe_sink(h->pipe.q->ch);
        break;
    case Handle::Kind::File:
        break; // the VFS is synchronous: a file never parks, either way
    default:
        // A body, a socket, a picked file: these wait on a host call rather
        // than on a channel, and there is nothing to arm.
        return Err(Error::Unsupported);
    }
    s.h = h;
    return {};
}

// Each descriptor's revents, and how many of them are non-zero. Asking costs
// nothing and consumes nothing, which is the whole point of the call.
usize poll_scan(Vec<PollSlot> &slots)
{
    usize n = 0;
    for (PollSlot &s : slots) {
        s.revents = 0;
        if (s.events & SYS_POLL_IN) {
            if (s.pend || s.in.ready())
                s.revents |= SYS_POLL_IN;
            if (s.in.gone())
                s.revents |= SYS_POLL_HUP;
        }
        if (s.events & SYS_POLL_OUT) {
            if (s.out.ready())
                s.revents |= SYS_POLL_OUT;
            if (s.out.gone())
                s.revents |= SYS_POLL_HUP;
        }
        if (s.revents)
            n++;
    }
    return n;
}

// The holds the call took, released however it leaves — including a frame
// destroyed while parked, which is what ^C does to it.
struct PollHolds {
    explicit PollHolds(Proc &q) : p(q) {}

    PollHolds(const PollHolds &)            = delete;
    PollHolds &operator=(const PollHolds &) = delete;

    ~PollHolds()
    {
        for (PollSlot &s : slots)
            poll_drop(p, s);
    }

    Proc &p;
    Vec<PollSlot> slots;
};

// One token on every channel named, plus the timer. Both registrations are the
// scheduler's business to unwind together; a channel that fires after the
// waiter has gone is the late event sched_wake already answers false to.
struct PollWait {
    PollWait(Vec<PollSlot> &slots, u32 ms, bool forever)
        : slots_(slots), ms_(ms), forever_(forever)
    {
    }

    PollWait(const PollWait &)            = delete;
    PollWait &operator=(const PollWait &) = delete;

    ~PollWait()
    {
        disarm();
        sched_unwait(&w_);
    }

    bool await_ready() const noexcept { return false; }

    template <class P>
    bool await_suspend(std::coroutine_handle<P> h)
    {
        w_.h      = h;
        w_.cancel = h.promise().cancel;
        if (w_.cancel && w_.cancel->cancelled) {
            w_.cancelled = true;
            return false;
        }
        w_.token  = sched_token();
        w_.parked = true; // parked on channels: /proc says park, not host
        if (!sched_wait_token(&w_) || (!forever_ && !sched_wait_timer(&w_, ms_))) {
            sched_unwait(&w_);
            w_.token  = 0;
            w_.failed = true;
            return false;
        }
        // Arming after registering, and with nothing awaited in between, is
        // what makes a wake impossible to lose: the scan that found nothing
        // ready and this arming are one turn of the loop.
        armed_ = true;
        for (PollSlot &s : slots_) {
            if ((s.events & SYS_POLL_IN) && s.in.park)
                s.in.park(s.in.ctx, w_.token, true);
            if ((s.events & SYS_POLL_OUT) && s.out.park)
                s.out.park(s.out.ctx, w_.token, true);
        }
        return true;
    }

    Result<void> await_resume()
    {
        disarm();
        if (w_.cancelled || (w_.cancel && w_.cancel->cancelled))
            return Err(Error::Cancelled);
        if (w_.failed)
            return Err(Error::NoMemory);
        return {};
    }

private:
    // Disarming compares tokens, so a channel that already fired and cleared
    // its own is left alone.
    void disarm()
    {
        if (!armed_)
            return;
        armed_ = false;
        for (PollSlot &s : slots_) {
            if ((s.events & SYS_POLL_IN) && s.in.park)
                s.in.park(s.in.ctx, w_.token, false);
            if ((s.events & SYS_POLL_OUT) && s.out.park)
                s.out.park(s.out.ctx, w_.token, false);
        }
    }

    Vec<PollSlot> &slots_;
    u32 ms_;
    bool forever_;
    bool armed_ = false;
    Waiter w_;
};

} // namespace

Task<Result<usize>> poll_wait(Proc &p, Str payload, String &reply)
{
    if (payload.size() < 4 || (payload.size() - 4) % 8)
        co_return Err(Error::Invalid);
    usize pairs = (payload.size() - 4) / 8;
    if (pairs > SYS_POLL_MAX)
        co_return Err(Error::Invalid);

    const u8 *at = reinterpret_cast<const u8 *>(payload.data());
    u32 timeout  = sys_get_u32(at);

    // Parsing and taking the holds happens before the first await, so nothing
    // the payload points at has to survive one.
    PollHolds held(p);
    if (!held.slots.reserve(pairs))
        co_return Err(Error::NoMemory);
    for (usize i = 0; i < pairs; i++) {
        PollSlot s;
        s.fd           = sys_get_u32(at + 4 + i * 8);
        s.events       = sys_get_u32(at + 8 + i * 8);
        Result<void> r = poll_open(p, s);
        if (r.is_err())
            co_return Err(r.error());
        if (!poll_take(p, s))
            co_return Err(Error::Busy);
        if (!held.slots.push(s)) {
            poll_drop(p, s);
            co_return Err(Error::NoMemory);
        }
    }

    usize ready  = poll_scan(held.slots);
    bool forever = timeout == SYS_POLL_FOREVER;
    f64 deadline = sched_now() + f64(timeout);

    while (ready == 0 && (forever || sched_now() < deadline)) {
        u32 left = SYS_POLL_FOREVER;
        if (!forever) {
            f64 d = deadline - sched_now();
            left  = d <= 0 ? 0 : u32(d + 0.999); // round up: never wake early
        }
        PollWait wait(held.slots, left, forever);
        Result<void> r = co_await wait;
        if (r.is_err())
            co_return Err(r.error());
        ready = poll_scan(held.slots);
    }

    for (PollSlot &s : held.slots)
        if (!reply_u32(reply, s.revents))
            co_return Err(Error::NoMemory);
    co_return ready;
}
