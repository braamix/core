// The console: a terminal's keyboard receiver, the cooked bytes a foreground
// program reads, and which pids ^C reaches. One of each per terminal, named by
// the Term the call acts on.
//
// The pump used to be spawned per foreground pipeline and to belong to the job
// it watched. It is permanent now, because the shell is becoming a process:
// something has to receive on keys() while nothing is running, and a process
// cannot — it has no keys() at all, only a ring the pump fills (tty.h).
#pragma once

#include "io.h"
#include "kernel/screen.h"

// The pump. One per terminal, spawned when the terminal is made, and the only
// receiver on that terminal's keys(), which is what keeps Channel's
// one-receiver rule true (channel.h). It never ends on its own.
Task<i32> console_pump(Term &t);

// How many zero-delay ticks the pump will wait for a full claimant ring before
// it drops a key. Generous, because it is cheap: a claimant that is a process
// takes a syscall and a step per key, so draining three of them is a dozen
// ticks the host runs back to back.
constexpr usize KEY_WAIT = 64;

// What is in front, and therefore what ^C cancels: a pipeline's stages, armed
// by the shell, or one child armed by a process through Sys::Fg. With nobody in
// front, ^C is delivered to whoever holds the raw route as an ordinary key —
// which is how a line editor abandons the line being typed rather than being
// cancelled by it.
constexpr usize CONSOLE_FG_MAX = 8; // Tree::MAX_STAGES

// One pid, for a shell that puts a pipeline in front a stage at a time — which
// is what Sys::Fg does, since the op word carries one. False when there are
// more than room. The first arms the cooked input as well, so a ^D ends one
// command's input rather than the console, and the rest do not.
bool console_fg_add(Term &t, u32 pid, u32 by);

void console_fg_clear(Term &t);

usize console_fg_count(const Term &t);

// The i'th pid in front, for a caller that must reach all of them: tty_resized
// tells the foreground about a grid that changed shape, as the pump tells it
// about a ^C. 0 past the end.
u32 console_fg_at(const Term &t, usize i);

bool console_fg_has(const Term &t, u32 pid);

// Whether this pid is in front of any terminal — /proc's, for the same reason
// tty_screen_held_by is.
bool console_fg_anywhere(u32 pid);

// Who put what is in front there. The foreground belongs to whoever armed it:
// a shell arms its stages one at a time and is not the keyboard's owner while
// it does — it lets go before it spawns — so this is what says the second
// stage is the same caller as the first (syscall.cpp, Sys::Fg).
u32 console_fg_owner(const Term &t);

// How many times ^C has reached this terminal's foreground. The shell samples
// it around a pipeline rather than being told, because the pump outlives every
// job now and has nobody to report to.
u32 console_interrupts(const Term &t);

// The cooked bytes, for the stdin of whatever is in front. **One reader at a
// time**: this is a Channel, so a second receiver would displace the first
// silently. Only the foreground is ever given it — a background job is spawned
// with an end of input instead.
Source console_input(Term &t);

// Whether a source is some terminal's cooked input: tty_is_console()'s twin,
// and Sys::Tty's answer for fd 0. A background job is given null_source().
bool console_is_input(const Source &s);

// Which terminal's, or null when it is not one. Sys::Tty measures this rather
// than the caller's own screen.
Term *console_input_term(const Source &s);
