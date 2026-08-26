# Release notes

Reasoning, alternatives and trade-offs behind the code. Comments in the source
say *what* a thing is; this file says *why* it is that way.
[Concept.md](Concept.md) remains the specification — where this document and the
spec disagree about intent, the spec wins and one of the two needs amending.

What has been written for the release after 0.7 is below, under a heading of its
own; the next goes above it, and all of them move to [releases/](releases/) when
the release is cut.

Releases before this one are one file each in [releases/](releases/), newest
first:

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

## Two screens of one kernel

`web/dual.html` splits a window between two terminals: the upper two thirds and
the lower third, each with a `/bin/sh` of its own. The question it settles is
which of the two arrangements a second terminal should be.

**Why not two kernels.** `web/embed.html` has mounted two instances on one page
since 0.2, and doing it again with a different layout would have been an
afternoon's work and no new code. It is the wrong answer, and the reason is
storage. Two kernels are two of everything *except* the origin's OPFS, which
they both mount as `/`. A sync access handle takes an exclusive lock
(`web/fs.js`, `createSyncAccessHandle`), the loser's rejection reaches the
kernel as `E.PERM` (`web/abi.js`) with no retry, and there is no open-file table
spanning two instances to prevent the collision — the VFS's own table is
per-kernel and knows nothing of a sibling.

Contention would be a nuisance. What makes it unacceptable is *misreading*: at
boot each kernel reads `/etc/version` to decide whether to unpack, a failed read
is indistinguishable from an absent file, and absent means unpack **without
asking** — which removes every top-level directory the archive carries, out from
under a kernel that is already running out of it. Two shells on one page would
have had a race that occasionally deletes `/bin`. That is not a trade-off worth
making for a layout.

So: one kernel, two screens. One scheduler, one heap, one VFS, one open-file
table, one `/home` and one `/tmp` — everything the two shells share, they share
through the code that already arbitrates it.

**The ABI grew an argument, and that is the cost.** `key` and `resize` take a
terminal id, and `host_present` carries one; the surface is still nine exports
and seven imports, and no process names a screen — `Sys::Tty`, `Sys::Fg`,
`Sys::Cursor`, `Sys::Style` and `Sys::Echo` keep their wire format and resolve
the caller's terminal from its `Proc`, so `PROC_ABI` does not move and nothing
in `src/cmd/` or `src/proc/` changed at all. Since a matching list of *names*
would not have caught an added argument, `test/system/abi.mjs` now asserts each
export's arity as well: a wasm export reaches JS as a function whose `length` is
its parameter count, which costs one table and pins the thing that actually
drifted here.

**The host decides how many, not the kernel.** A fixed pair would have meant a
second shell running invisibly on `index.html`. Instead a terminal is made by
the first `resize()` that names it: the kernel spawns its pump, init starts a
shell on it, and a page with one canvas has exactly one shell as it always did.
`TERM_MAX` is 4 and bounds an id arriving from JS; the id space is the only
reason for a limit at all.

**A process belongs to a terminal.** `Proc` carries it and a spawn inherits it,
exactly as `cwd` and `depth` are inherited — so the shell, its pipeline and
everything below reach one grid, one keyboard channel and one foreground set.
The alternative, a "current terminal" the dispatcher sets on entry, was rejected
outright: a syscall server awaits, and any global set before an await is a bug
waiting for two shells to be busy at once.

**The bug two terminals found.** Both shells start within a tick of each other
and both `exec_resolve("/bin/sh")`, which opens one file twice. The VFS shares a
backend handle per path and already recovered from a lost race — but only if the
winner had *registered* its record by the time the loser resumed, and when the
loser's rejection arrives first there is nothing to find. The store refuses the
second handle, and `/bin/sh: permission denied` is what the lower screen said.

The fix is not a retry: an in-flight open is now published before the await, so
a second opener waits for the record the first is about to register instead of
asking the store for a handle it is already taking. It costs a zero-delay tick
per waiter and removes the failed handle entirely. This was always a race — two
terminals only made it routine, and it is why `less /etc/help` on both screens
at once works, which is the thing two kernels could never have done.

**What is not here.** No way to move a process between terminals, no `chvt`, and
no terminal that outlives the page. A second terminal's shell that exits is not
replaced — the same rule terminal 0 has always had — and the page is what
decides a terminal exists at all.
