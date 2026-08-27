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

## A usage message that says what the options do

`unzip`'s usage was one line naming three options and explaining none of them,
which is the shape thirty-odd programs in `src/cmd/` print. It is enough where
the option letters are the familiar ones — `ls -l`, `rm -r`, `cp -i` — and it is
not enough here: `-p` is not "print", it is "extract to a pipe and say nothing
else", and a reader with only the line in front of them cannot tell that from
`-l`. So it grew a block, `Usage:` then `Options:` with a row per letter.

The shape is not new. `/bin/pkg` has printed it since 0.4
([src/cmd/pkg/pkg.cpp](../src/cmd/pkg/pkg.cpp)), where a table of twelve
subcommands left no choice, and taking it for `unzip` costs a longer string
constant and nothing else — the same one write to stderr, the same status 2.
The rest of `src/cmd/` keeps its one-liners. What decides between them is
whether an option's name gives its meaning away, not how many there are, and
that is a judgement per program rather than a rule to apply across the tree.

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

**The screen left `/proc/host`.** The second thing two terminals found: `uname
-a` printed `screen 124x28` on both, because the file it reformats built that
line from `screen(*term_open(0))`. The comment above it argued the case — the
file is the machine and not the caller, and a process asks `Sys::Tty` for its
own — and the argument was sound while there was one screen and wrong the moment
there were two. A machine with up to `TERM_MAX` grids has no geometry of its
own, so the field was not per-caller, it was *not a machine fact at all*, and
`/proc/host` now stops at `system`, `release`, `machine` and what the host said
at boot.

Three other shapes were considered and rejected. Plumbing the reader into
procfs: `/proc` is generated at open and cannot know who is reading — that is
why `cwd` is per-pid rather than the kernel's — and threading a caller through
the VFS for one line is a large change to buy a small one. Keeping the line as
terminal 0's and letting `uname` override it: `cat /proc/host` would still
answer the wrong number on the lower screen, and a file that is only right when
reformatted is worse than no field. Listing every terminal: it makes `uname -a`
answer a question nobody asked, and `vmstat` still could not tell which of the
list it was printing into.

So `uname -a` prints the terminal it is running on after `machine`, where the
field always was, and a one-screen page's output is byte-identical to what it
was. Taking the field out did expose a second thing: the stored description
carries a blank line, `cat /proc/host` had been showing it as a gap all along,
and with nothing after `machine` it read as the hole the screen had left. It is
not a row of the table — it is `banner_half`'s marker for how far down the boot
grid the fields go — so procfs drops it on the way out and boot keeps reading it
in the string it was always in.

`uname -g` is the geometry on its own, the shape `-s`, `-r` and `-m` already
had for the fields that are in the file, and the only one of the five that does
not read `/proc/host` at all. It is what a script wants — `$(uname -g)` rather
than `uname -a | grep screen` — and it is the answer to a question the file
could not have held even before this: a size that is the caller's, not the
machine's. Off the grid it prints nothing and exits 1, which is what a field
that is not there already did. `vmstat`'s header repeat moved the same
way and is more correct for it: it was sizing its output to terminal 0 and
re-reading terminal 0 on every `SIG_WINCH`, and now follows the window the rows
actually land in. Down a pipe both print no geometry, since `Sys::Tty` answers
zero and a pipe has no width — the rule `ls` has always run by.

**What is not here.** No way to move a process between terminals, no `chvt`, and
no terminal that outlives the page. A second terminal's shell that exits is not
replaced — the same rule terminal 0 has always had — and the page is what
decides a terminal exists at all.
