#include "console.h"

#include "exec.h"
#include "kernel/alloc.h"
#include "kernel/key.h"
#include "kernel/screen.h"
#include "kernel/text.h"
#include "tty.h"

namespace {

// Words and a pointer, so the globals stay trivially destructible (CLAUDE.md).
// The cooked pipe holds Strings and therefore has a destructor, so it is a heap
// block built on first use rather than a namespace-scope Channel.
struct Con {
    u32 fg[CONSOLE_FG_MAX];
    usize fg_n;
    u32 fg_by; // who armed it
    u32 interrupts;
    Pipe *cooked_in;
};

Con g_con[TERM_MAX];

Con &con(const Term &t)
{
    return g_con[term_id(t)];
}

Pipe &cooked(const Term &t)
{
    Con &c = con(t);
    if (!c.cooked_in) {
        c.cooked_in = static_cast<Pipe *>(heap_alloc(sizeof(Pipe)));
        if (!c.cooked_in)
            panic("console: out of memory");
        new (c.cooked_in) Pipe();
    }
    return *c.cooked_in;
}

// End of input belonged to the foreground that was there when ^D was typed, so
// a new one gets a channel that is open again — and empty, since type-ahead
// meant for what has gone is not input for what replaces it. Reopening rather
// than rebuilding leaves a reader that is still unwinding parked on something
// that exists.
void rearm(const Term &t)
{
    cooked(t).clear();
    cooked(t).reopen();
}

} // namespace

// Both pids are reserved while they are here: a set naming one that has been
// reused would send ^C to a stranger.
bool console_fg_add(Term &t, u32 pid, u32 by)
{
    Con &c = con(t);
    if (c.fg_n >= CONSOLE_FG_MAX || !sched_pid_hold(pid))
        return false;
    if (!c.fg_n) {
        if (!sched_pid_hold(by)) {
            sched_pid_drop(pid);
            return false;
        }
        c.fg_by = by;
        rearm(t);
    }
    c.fg[c.fg_n++] = pid;
    return true;
}

void console_fg_clear(Term &t)
{
    Con &c = con(t);
    for (usize i = 0; i < c.fg_n; i++)
        sched_pid_drop(c.fg[i]);
    sched_pid_drop(c.fg_by);
    c.fg_n  = 0;
    c.fg_by = 0;
}

usize console_fg_count(const Term &t)
{
    return con(t).fg_n;
}

u32 console_fg_at(const Term &t, usize i)
{
    const Con &c = con(t);
    return i < c.fg_n ? c.fg[i] : 0;
}

bool console_fg_has(const Term &t, u32 pid)
{
    const Con &c = con(t);
    for (usize i = 0; i < c.fg_n; i++)
        if (c.fg[i] == pid)
            return true;
    return false;
}

bool console_fg_anywhere(u32 pid)
{
    for (const Con &c : g_con)
        for (usize i = 0; i < c.fg_n; i++)
            if (c.fg[i] == pid)
                return true;
    return false;
}

u32 console_fg_owner(const Term &t)
{
    const Con &c = con(t);
    return c.fg_n ? c.fg_by : 0;
}

u32 console_interrupts(const Term &t)
{
    return con(t).interrupts;
}

Source console_input(Term &t)
{
    return pipe_source(cooked(t));
}

bool console_is_input(const Source &s)
{
    return console_input_term(s) != nullptr;
}

Term *console_input_term(const Source &s)
{
    for (u32 id = 0; id < TERM_MAX; id++)
        if (g_con[id].cooked_in && s.ctx == g_con[id].cooked_in)
            return term_at(id);
    return nullptr;
}

Task<i32> console_pump(Term &t)
{
    String line; // the cooked line, sent whole on Enter
    Con &c = con(t);

    for (;;) {
        Result<Key> r = co_await keys(term_id(t)).recv();
        if (r.is_err()) {
            if (r.error() == Error::Again)
                continue; // a stray wake
            co_return 0;  // cancelled, which only happens on the way down
        }

        Key k = r.value();

        // Shift+PageUp and Shift+PageDown page the screen's history, half a
        // screen at a time; Shift+Up and Shift+Down move it a row, which is
        // what the page turns a wheel notch into. Not while a program holds the
        // screen, whose grid that is not. Every other key returns to the live
        // screen.
        if (!tty_screen_owner(t) && (k.mods & MOD_SHIFT) &&
            (k.code == KEY_PAGE_UP || k.code == KEY_PAGE_DOWN || k.code == KEY_UP ||
             k.code == KEY_DOWN)) {
            bool back = k.code == KEY_PAGE_UP || k.code == KEY_UP;
            bool page = k.code == KEY_PAGE_UP || k.code == KEY_PAGE_DOWN;
            i32 step  = page ? i32(max(1u, screen(t).rows / 2)) : 1;
            screen_view_scroll(t, back ? -step : step);
            continue;
        }
        screen_view_home(t);

        // ^C reaches whatever is in front, whatever is claimed: a program that
        // has taken the screen and stopped answering must still be killable,
        // and that is also M4's acceptance criterion. With nobody in front it
        // is an ordinary key, which is what lets a line editor abandon a line
        // instead of being cancelled by it.
        if ((k.mods & MOD_CTRL) && k.code == 'c' && c.fg_n) {
            screen_write(t, "^C");
            screen_newline(t);
            // Delivered to a stage that asked for it, cancelling the rest.
            for (usize i = 0; i < c.fg_n; i++)
                sig_raise(c.fg[i], SIG_INT);
            c.interrupts++;
            line.clear();
            continue;
        }

        // Raw: no echo and no line discipline, since the claimant is painting
        // the screen itself.
        //
        // A full ring is waited on, briefly, before anything is dropped. This
        // loop never suspends while the channel has something in it, so a burst
        // — a paste, or typing between two frames — arrives here as one run,
        // and without the wait a line longer than the ring lost its tail before
        // the claimant was ever scheduled. A claimant that *is* a process needs
        // several of these per key, since every one of its reads is a syscall
        // and a step.
        //
        // Bounded, and through the timer queue at zero delay rather than by
        // parking on the ring: the host answers a delay of 0 with another pump
        // straight away, so this costs microseconds when somebody is reading
        // and ends in a dropped key when nobody is — which is what keeps a
        // wedged program from taking ^C down with it.
        for (usize spin = 0; spin < KEY_WAIT && tty_raw(t) && tty_raw(t)->full(); spin++)
            if ((co_await Sleep(0)).is_err())
                co_return 0;
        if (KeyRing *raw = tty_raw(t)) {
            raw->try_send(k);
            continue;
        }

        Pipe &to = cooked(t);

        if (k.mods & MOD_CTRL) {
            if (k.code != 'd')
                continue;

            // End of input, not end of the console. A partial line goes first:
            // ^D after typing is "send what I have", as it is anywhere. The
            // channel is re-armed when something else is put in front.
            if (!line.empty()) {
                String chunk;
                if (chunk.assign(line.str()))
                    to.try_send(move(chunk));
                line.clear();
            }
            to.close();
            continue;
        }

        // A line at a time, which is what "cooked" means and what the pipe can
        // hold: one chunk per keystroke filled its eight slots after seven
        // characters, and the pump drops rather than parks, so the rest of the
        // line was lost. Echo stays per keystroke, so typing still appears as
        // it is typed.
        if (k.code == KEY_ENTER) {
            screen_newline(t);
            if (line.push('\n')) {
                String chunk;
                if (chunk.assign(line.str()))
                    to.try_send(move(chunk));
            }
            line.clear();
        } else if (k.printable()) {
            char utf8[4];
            usize n = utf8_encode(k.code, utf8);
            line.append(Str(utf8, n));
            screen_put(t, k.code);
        }
    }
}
