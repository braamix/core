# Release notes

Reasoning, alternatives and trade-offs behind the code. Comments in the source
say *what* a thing is; this file says *why* it is that way.
[Concept.md](Concept.md) remains the specification — where this document and the
spec disagree about intent, the spec wins and one of the two needs amending.

The release after 0.9 is being written, and starts below. New sections are
appended under it, and the whole moves to [releases/](releases/) when the
release is cut.

## `/etc/init`: the program a site boots into

0.9 gave a program a second screen ([`Sys::TermOpen`](System_Calls.md)), and
named the caller it was for: an emulator with a second console line. That caller
is [simbesm](https://github.com/besm6/v7besm), a BESM-6 booting Unix v7 with a
getty on each of its two Consul lines — and building the site for it found the
other half missing. The page could say which screens the emulator got, and could
not say that the emulator was what the tab was *for*: init ran `/bin/sh`, a
`constexpr` in [src/user/boot.h](../src/user/boot.h) with a comment saying
outright that nothing configures it. The visitor met a prompt and had to type a
command to reach the thing they came for.

So `/etc/init`: one line, the path of the program init runs on terminal 0.

**A file and not a mount option.** `mount({init: "/bin/besm6"})` reads better on
the page, beside the `shell: false` that arranges the screens — but a string has
to cross the JS boundary to get in, and §3.4 fixes that boundary at nine exports
and seven imports, asserted name by name and arity by arity in
[test/system/abi.mjs](../test/system/abi.mjs). A tenth export was never on. The
service route was: §6's rule is that *a new service is an enum value on each
side*, and `SvcOp::HostInfo` — a host-owned string, asked once at boot and
cached — is the exact precedent. What decided it against was that the *archive*
is where the rest of a site's own content already lives: `/etc/motd`, `/bin`,
and now the one line saying what the tab runs. A site that ships an archive
ships this with it, and nothing has to be threaded through `braam.js`,
`worker.js`, `svc.js` and `fakesvc.mjs` to say it. It costs one `read_file` on
the boot path, beside the motd's.

**Absent, empty and unreadable all mean the shell.** An archive without the file
boots exactly as it did, which is what makes this safe to add to a released
format. The line is trimmed and cut at the first newline; nothing checks the
shape of the path, because `exec_resolve` already has the only rule there is —
a bare word goes through `PATH`, a path does not.

**Terminal 0's alone.** `term_watch` starts `/bin/sh` on every terminal the host
makes later, and that does not change. The page's example is one program with a
panel: the second screen is that program's, opened with `TermOpen` and marked
`shell: false`, not a second copy of it.

**`SHELL=/bin/sh` stays in the environment.** The variable means the user's
shell — what `sh -c` and a `#!`-less script get — not what init happened to run.

**The restore offer is withheld.** A shell that will not resolve is offered an
unpack, because the archive is `/bin` and `/etc` and the shell is in it. A
program `/etc/init` named may be anywhere, so the offer would be a false promise
and the ending says something else: *there is nothing to run*. Every other line
about the program is the shell's with the name swapped in, and `called()` keeps
`/bin/sh` reading as "the shell" so the system suite's existing assertions hold
byte for byte.

Covered by [test/system/initprog.mjs](../test/system/initprog.mjs): a named
program runs and no prompt appears, the line is trimmed, a missing one says so
and is not offered an unpack, and an empty file is the shell.

## The page that failed silently

`web/embed.html` is the one that demonstrates the *embedding* arrangement — two
kernels on a page, a worker each, sharing nothing but the origin's storage
(§3.5) — so it is what somebody reads before putting a terminal on a site of
their own. It was also the only page of the four that said nothing when it went
wrong.

It had fallen a generation behind. `index.html`, `dual.html` and `quad.html`
each picked up a noscript notice, a boot watchdog and a `#status` pane as those
were written; `embed.html` was not touched again and kept none of them.
Scripting off was a blank page. A `braam.js` fetch a blocking extension held
open was a blank page. A browser without `OffscreenCanvas` was an uncaught throw
in a console. And a boot stuck behind any of that was a black canvas, because
`mount()` was called with no `onError` at all, so its own stall report — the
report written precisely for that case — went to `console.error` where nobody
was looking. The page most likely to be opened by somebody who does not yet know
how braam boots was the page that told them least.

None of this is new work; it is four blocks copied from `dual.html`, which is
the reference for a page with more than one pane. What is new is the one thing
those pages have no need of.

**A diagnostic says which kernel spoke.** `dual.html` and `quad.html` are one
`mount()`, so one boot watch and one voice. `embed.html` is two `mount()` calls
and therefore two independent stall timers, and an unprefixed pane would show
the same "boot is stuck" line twice with nothing to tell the two workers apart —
which is the opposite of what a diagnostic is for. So each mount takes an
`onLog`/`onError` pair that names it, and the two share the one status pane.

**One `try` for the pair.** `mount()` checks `transferControlToOffscreen` on
every spec before it makes a worker, and that is a property of the browser, not
of a canvas: the two mounts fail together or succeed together, so there is no
half-mounted page to unwind. The `catch` disables the dispose button, since a
page that mounted nothing has nothing to dispose.

**The key bars are per kernel.** `braam.js` only appends buttons into the
container a spec names and styles nothing; which bar is visible is the page's
CSS, keyed off a `data-pane` flag the page keeps in step on a pointerdown.
`dispose()` already removes that pane's buttons and its focus ring, so disposing
the right kernel needs one line here — move the flag back to the left, so the
bar still on screen belongs to the kernel still running.

The arrangement itself is unchanged, so §3.5 is untouched. `index.html` is still
short of the focus ring and the `overflow: hidden` the two multi-pane pages
have; that is a separate tidy.

Releases before this one are one file each in [releases/](releases/), newest
first:

- [0.9](releases/Release_Notes-v0.9.md) — the programs a pipeline needs, and
  the screen a program opens
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

