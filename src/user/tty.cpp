#include "tty.h"

#include "console.h"
#include "exec.h"
#include "io.h"
#include "kernel/alloc.h"
#include "kernel/screen.h"

namespace {

Result<usize> to_screen(void *ctx, Str s)
{
    screen_write(*static_cast<Term *>(ctx), s);
    return s.size();
}

// Pointers and words, so the globals stay trivially destructible (CLAUDE.md).
// Each route names its holder by the pid that took it, one pair per terminal.
struct Claims {
    KeyRing *raw;
    u32 raw_pid;

    FullScreen *alt;
    u32 alt_pid;
};

Claims g_claims[TERM_MAX];

Claims &claims(const Term &t)
{
    return g_claims[term_id(t)];
}

} // namespace

Stdio stdio_console(Term &t)
{
    return Stdio{ console_input(t), tty_sink(t), tty_sink(t) };
}

Stream tty_sink(Term &t)
{
    return Stream{ to_screen, nullptr, &t };
}

bool tty_is_console(const Stream &s)
{
    return s.fn == to_screen;
}

Term *tty_term_of(const Stream &s)
{
    return s.fn == to_screen ? static_cast<Term *>(s.ctx) : nullptr;
}

KeyInput::KeyInput(Term &t, u32 pid) : term_(&t)
{
    // Refused before anything is allocated: a claim that nested would leave the
    // pump pointing at a ring whose owner may die first.
    Claims &c = claims(t);
    if (c.raw) {
        err_ = Error::Perm;
        return;
    }
    ring_ = static_cast<KeyRing *>(heap_alloc(sizeof(KeyRing)));
    if (!ring_)
        return;
    new (ring_) KeyRing();
    c.raw     = ring_;
    c.raw_pid = pid;
}

KeyInput::~KeyInput()
{
    if (!ring_)
        return;
    Claims &c = claims(*term_);
    if (c.raw == ring_) {
        c.raw     = nullptr;
        c.raw_pid = 0;
    }

    // Type-ahead the claimant never read goes back to the console rather than
    // dying with the ring. The prompt is what makes this load bearing: a line
    // abandoned with ^C drops its claim with the rest of what was typed still
    // in it, and that has to reach the next prompt — which it did for free
    // while the editor received on keys() itself. The pump has drained the
    // channel by the time anything can release a claim, so this arrives ahead
    // of whatever the host queues next rather than behind it.
    for (Option<Key> k = ring_->try_recv(); k.has_value(); k = ring_->try_recv())
        keys(term_id(*term_)).try_send(k.value());

    ring_->~KeyRing();
    heap_free(ring_);
}

KeyRing *tty_raw(const Term &t)
{
    return claims(t).raw;
}

u32 tty_keys_owner(const Term &t)
{
    return claims(t).raw_pid;
}

u32 tty_screen_owner(const Term &t)
{
    return claims(t).alt_pid;
}

bool tty_keys_held_by(u32 pid)
{
    for (const Claims &c : g_claims)
        if (c.raw_pid && c.raw_pid == pid)
            return true;
    return false;
}

bool tty_screen_held_by(u32 pid)
{
    for (const Claims &c : g_claims)
        if (c.alt_pid && c.alt_pid == pid)
            return true;
    return false;
}

void tty_resized(Term &t)
{
    // Whoever is in front, which is ^C's audience and for ^C's reason, plus
    // whoever holds a route — a full-screen program has the keys and the
    // screen and may be in front as well, so each pid is told once.
    u32 told[CONSOLE_FG_MAX + 2];
    usize n = 0;

    auto tell = [&](u32 pid) {
        if (!pid)
            return;
        for (usize i = 0; i < n; i++)
            if (told[i] == pid)
                return;
        told[n++] = pid;
        sig_raise(pid, SIG_WINCH);
    };

    for (usize i = 0; i < console_fg_count(t); i++)
        tell(console_fg_at(t, i));
    tell(claims(t).alt_pid);
    tell(claims(t).raw_pid);
}

// ------------------------------------------------------------ the alternate
// screen

FullScreen::FullScreen(Term &t, u32 pid) : term_(&t)
{
    // Before the snapshot, or a second claimant would save the blanked grid the
    // first is painting and give *that* back to the shell.
    Claims &c = claims(t);
    if (c.alt) {
        err_ = Error::Perm;
        return;
    }

    // After the refusal and before the snapshot: a background job takes the
    // screen with no keystroke to have brought the view home first.
    screen_view_home(t);

    const Screen &s = screen(t);
    if (!s.cols || !s.rows || !screen_cells(t))
        return;

    usize n = usize(s.cols) * s.rows * sizeof(Cell);
    saved_  = static_cast<Cell *>(heap_alloc(n));
    if (!saved_)
        return;

    __builtin_memcpy(saved_, screen_cells(t), n);
    cols_      = s.cols;
    rows_      = s.rows;
    cursor_x_  = s.cursor_x;
    cursor_y_  = s.cursor_y;
    cursor_on_ = s.cursor_on != 0;

    c.alt     = this;
    c.alt_pid = pid;

    screen_ansi_reset(t); // a known parser to start from
    screen_cursor(t, false);
    screen_clear(t);
}

FullScreen::~FullScreen()
{
    if (!saved_)
        return;

    Term &t   = *term_;
    Claims &c = claims(t);
    if (c.alt == this) {
        c.alt     = nullptr;
        c.alt_pid = 0;
    }

    const Screen &s = screen(t);

    screen_ansi_reset(t); // margins and modes the program left behind

    // A resize while the program ran leaves the snapshot describing a grid that
    // no longer exists. Blanking is the honest answer: the shell repaints its
    // prompt on the next line either way.
    if (s.cols == cols_ && s.rows == rows_ && screen_cells(t)) {
        __builtin_memcpy(screen_cells(t), saved_, usize(cols_) * rows_ * sizeof(Cell));
        screen_touch(t, 0, 0, cols_, rows_);
        screen_move(t, cursor_x_, cursor_y_);
    } else {
        screen_clear(t);
    }
    screen_cursor(t, cursor_on_);

    heap_free(saved_);
    saved_ = nullptr;
}
