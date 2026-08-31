# Release notes

Reasoning, alternatives and trade-offs behind the code. Comments in the source
say *what* a thing is; this file says *why* it is that way.
[Concept.md](Concept.md) remains the specification — where this document and the
spec disagree about intent, the spec wins and one of the two needs amending.

What has been written for the release after 0.8 is below, under a heading of its
own; the next goes above it, and all of them move to [releases/](releases/) when
the release is cut.

Releases before this one are one file each in [releases/](releases/), newest
first:

- [0.8](releases/Release_Notes-v0.8.md) — the screens a page can hold, and the
  libc it never linked
- [0.7](releases/Release_Notes-v0.7.md) — the devices a port opens, and the
  menus the browser already had
- [0.6](releases/Release_Notes-v0.6.md) — the programs a script assumed were
  there
- [0.5](releases/Release_Notes-v0.5.md) — a system a program can be written
  for, not only in
- [0.4](releases/Release_Notes-v0.4.md) — a system that can install software it
  was not built with
- [0.3](releases/Release_Notes-v0.3.md) — a shell with a language, and files
  with names of their own
- [0.2](releases/Release_Notes-v0.2.md) — a version that names the commit, and
  one program model
- [0.1.0](releases/Release_Notes-v0.1.md) — packaging, and M0–M9 with the
  criteria they were accepted against

---

## A screen a program opens, and `PROC_ABI` 20

0.8 gave the *page* up to four terminals and gave a *process* exactly one of
them. [System_Calls.md](System_Calls.md) §10 said so in as many words — "no call
here names a screen, and that is deliberate" — and the reasoning behind it was
sound for what it was answering: two shells on two grids need no way to reach
each other's, and not building one kept `PROC_ABI` still. It was never an
argument that a *program* should not paint two grids; nobody had asked.

An emulator asks. A BESM-6 has a console line and a second Consul line, and the
second one is a telnet listener — which this system has no socket for, so its
only possible home in a browser tab is a second screen of the same process.
An editor started on one screen and painted on another is the same shape, and is
what [src/cmd/edit.cpp](../src/cmd/edit.cpp)'s `-S` is: the caller §4.3's first
rule wants.

**A screen is a descriptor, because everything else here already is.**
`Sys::TermOpen` names a terminal and hands one back, after which `Read`, `Write`
and `Close` serve it — §4.3's own rule, and the reason `Write` on a screen is
text on that grid with no operation added for it. The claims move onto the
handle, which is what lets one pid hold them on two terminals: `g_claims[]` was
already per terminal and merely recorded the pid, so nothing below the syscall
boundary had to change at all. Closing the descriptor gives the screen back, and
so does dying — `~Handle` rather than `~Proc`, which is the same guarantee one
level down.

Three alternatives were weighed. **A `/dev/tty1` path** was the tempting one,
since `echo hi > /dev/tty1` would work from the shell for free and add no
operation: `devfs` is in `src/fs/` and its own header forbids it reaching the
screen, a `ttyfs` in `src/user/` would need a nested mount under `/dev`, and
[Shell.md](Shell.md) already states there is no `/dev/tty`. **Two processes and
a pipe** needs no ABI change and is what a port would have to do today; it
imposes an IPC protocol and a second installed binary on every program that
wants a panel, and `Sys::Spawn` cannot place a child on another terminal anyway.
**A "current screen" the process sets and clears** is the same bug 0.8 rejected
at the dispatcher, one level up: state set before an await.

**`PROC_ABI` moves to 20, and that is the cost.** Adding an operation is free —
`Truncate`, `FStat` and `Mount` were all taken inside 19 — but eight existing
operations now read a screen out of their argument, and an argument that changes
meaning is exactly what the number refuses a stale binary for. Every stamped
binary is rebuilt and every repository re-signed. The field is the argument's
upper bits, above each operation's own flags, so `SYS_TERM_SELF` is zero and
every call site in `src/cmd/` and `src/proc/` compiles unchanged; the whole
migration is a rebuild. `Style` is the one exception — its 24 bits are full of
colour, so its screen is a payload, the shape `SigAct`'s mask already has.
`Fg` deliberately did not move: a process is in front of one console, and what
`^C` means is a policy question that deserves its own argument.

**A bare terminal is not optional, and that was the surprise.** A shell sitting
at its prompt *holds* its terminal's raw-key claim — the prompt is no exception,
`tty.h` says so — so a program on screen 0 could take screen 1's grid and never
its keyboard. `resize` grew a fourth argument for it, `TERM_NO_SHELL`, which
`mount({ screens: [{ canvas, shell: false }] })` sets: the pump still runs, since
something must hold the keyboard, and only the session is withheld. That is a
kernel export's arity, which [test/system/abi.mjs](../test/system/abi.mjs)
asserts on purpose — the assertion 0.8 added after its own added argument slipped
past a names-only check, earning its keep one release later.

**Two things two screens found, both latent before.** `Sys::Tty` took the
console *flag* from the descriptor's stream and the *geometry* from `p.term`,
which is one answer while a process has one screen and the wrong one the moment
it has two; it reads the stream's own terminal now. And `ScreenBlit`'s
`Err(Perm)` had been asking whether the *process* held a screen rather than
whether it held *this* one.

**Watching both screens is two tasks, and that moved one line.** A key ring has
one receiver, so a second `KeyRead` parked on a screen already being read is
refused rather than silently displacing the first — `HandleBusy`'s guard,
extended to the claim. The subtler one: `ProcScreen::next_key()` resized only if
`sig_take(SIG_WINCH)` returned true, and `SIG_WINCH` is one process-wide bit
while `proc_interrupt` abandons *every* interruptible call. Two screens meant
both reads woke and only one could take the bit, leaving the loser's grid stale.
It now re-asks its own geometry on any `Err(Intr)` and collects the bit without
letting it decide. The cost is one `cursor_get` per `^C`.

**`/proc/terms` rather than an operation**, which is §4.3's other rule: the
terminals the page put up and their sizes. It is `id cols rows` and nothing
more. A fourth column saying whether a terminal has a shell was written and
removed — it reported what the *host asked for*, which stays true after a shell
exits, and the claim is the real answer to "can I paint here?" anyway.

**What is not here.** No way to move a process between terminals, no `chvt`,
`Sys::Fg` still names no screen, and `TERM_MAX` is still 4. And `TERM_NO_SHELL`
is checked in a browser rather than by the suite: the system tests are one
cumulative session in which `dual` and `quad` have spoken for every id by the
time a case could ask for a bare one.

---

## The kit ships an `endian.h` after all, and CI is why

0.8 argued that `braam::compat` should not supply `<endian.h>`: clang derives
the order from `__BYTE_ORDER__` and carries the whole `htobe`/`letoh` family,
and its answer is better than a hardcoded little-endian one. That argument
stands. What it missed is *which* clang — the freestanding `<endian.h>` arrived
in clang 23, and CI runs the distribution's, which on ubuntu-latest is 18.1.3.
`test/unit/test_compat.cpp` had never reached CI before the 0.8 push, since the
four commits that introduced it were local until then, and it failed there with
`'endian.h' file not found` on a tree whose three suites pass at home.

So the header is the kit's, in [limits.h](../src/compat/include/limits.h)'s
shape rather than as a replacement: `#include_next` when the compiler has one,
and the same names derived from the same predefines when it does not. A clang 23
build is byte-for-byte what it was — it reaches clang's header through one more
file — and a clang 18 build now works at all. The rejected alternative was
pinning CI to a versioned clang from apt.llvm.org: it would fix this one
symptom, and it would move the toolchain floor from "a C++20 clang" to "clang
23" for every consumer of the SDK, which is a much larger claim to make for a
header that is thirty lines of `__builtin_bswap`.

The fallback branch cannot be exercised on a machine whose clang has the real
header, which is the honest limit of the test: `test_endian` checks whichever
branch the compiler took, and the two now have to be checked on two compilers.
CI is the second one, which is the whole point.

`<float.h>` is still not wrapped, and must not be — `src/math/` overrides
`LDBL_*` for its vendored sources and a port must not inherit that lie.

Separately, the three actions leave Node 20, which GitHub is forcing onto Node
24 and warning about on every run. `checkout` and `setup-node` go to `v5`, which
is the first of each that targets Node 24; `upload-artifact`'s `v5` still does
not, so it goes to `v6` — its `v7` is an ESM rewrite with a new direct-upload
mode, and none of that is wanted for one `path:`.
