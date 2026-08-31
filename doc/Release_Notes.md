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

---

## `find`, and the walk lifted out of `copy_tree`

`/bin/find` is [TODO.md](TODO.md)'s A3, and the port it was read against is
v7's, by way of `v7besm/cmd/find/`. Two of that port's three headline findings
do not survive the move, and saying which is most of what is worth recording.

**Its parse tree was a union by pointer reinterpretation.** `struct anode { int
(*F)(); struct anode *L, *R; }` carried, in `L` and `R`, whichever of an `int`,
a `char` or a `char *` the primary wanted, and each of eighteen primaries read
the three words back through a private struct shape of its own. On the BESM-6
that was a live bug — a fat `char *` cast to `struct anode *` floors to the
word, so `-name` matched the wrong bytes. `FindNode` here carries a `pat`, a
`kind` and an `mtime` of the right types, three fields nobody reinterprets.

**`descend()` recursed once per directory, and the whole of that port's second
section is a measured ceiling** — `MAXDEPTH 20`, arrived at by disassembling the
prologue after an estimate came out wrong by a factor of two in the unsafe
direction. There is no ceiling to measure here, because §8.2 forbids the shape
that would need one: a deep tree must not be a deep chain of coroutine frames.
`copy_tree` has walked an explicit stack since it was written, and A3's entry
said to consider lifting that walk when a second caller turned up.

**So `TreeWalk` is the lift** — a pull-style walker in
[proc/io.h](../src/proc/io.h) beside the copy helpers, `next(path, entry)`
reporting everything under a root until it answers `ok(false)`. Pull rather than
a callback, because a callback that may `co_await` is a `Task`-returning virtual
and a frame per entry; the shape is `LineReader::next`'s, which a program's loop
already reads like. `copy_tree` is rewritten over it in the same commit rather
than left beside it, since two walks was the duplication the lift was for, and
A5's `du` is the third caller.

The one behaviour that changed with the conversion is the **order**.
`copy_tree`'s stack listed a whole directory, pushed its subdirectories and
popped the last of them, so `a/b`'s children came after `a/c`; `TreeWalk` keeps
a level per open listing and is a true pre-order, so a directory is reported
immediately before what is in it. `copy_tree` never cared. `find`'s reader does,
and `list_dir` delivers name order, so a whole listing is a thing a test can
assert. The cost is a level's entries held per level of depth rather than a path
of names — bounded by the tree, on the heap, and never a frame.

A directory that will not list is an `Err` naming itself in `at()`, and the
walk drops that level so a further `next()` carries on. That is the one place
the two callers want different things: `find` reports and keeps walking,
`copy_tree` returns. Making it the caller's decision is cheaper than a policy
flag, and neither has to say which it is.

**The expression is evaluated by a plain recursion, not a coroutine.** `-print`
is the only action `find` has, so nothing in the tree writes: `eval()` records
that a live `-print` was reached and the walk writes the path once. A tree of
`co_await`ing nodes would have cost a suspension point per node — D2 measured
that at ~375 bytes of state machine each — for a program whose whole output is
one line per name. It costs one deviation from v7, and only one: `-print
-print` prints once rather than twice. Everything else follows, including `find
. -name a -o -print`, where the short circuit is what keeps `-print` from being
reached, and `find . ! -print`, where the side effect happens and the negation
does not undo it.

**The implicit print is ours, not v7's.** v7 prints nothing without `-print`;
POSIX prints, and so does every reader's muscle memory. A positioned `-print` is
what turns the implicit one off, which is the rule that makes the two agree.

What did not come across, and why: `-cpio` wrote a PDP-11 archive to a tape reel
this system has no driver for; `-user`, `-group`, `-perm` and `-inum` name
things that do not exist here — no uids, no permissions, no inode numbers —
and `-links` none either, since there are no hard links; `-mtime`, `-atime` and
`-ctime` want three timestamps where there is one, and `-newer` orders it.
`-exec` and `-ok` are left to A7's `xargs`, which is the entry that owns
`Spawn`/`Wait`, and `find … | xargs` is the composition in the meantime.

`-name` is the shell's own matcher, [sh/match.h](../src/cmd/sh/match.h), linked
as one pure file the way `/bin/test` links `sh/cond.cpp`. It has no
leading-dot rule — the shell's is in `glob.cpp`'s walk, not in the matcher —
which is what POSIX `find` wants and v7's `gmatch` got wrong.

An operand is stat'd without following, so a link named on the command line is
the link; a link inside the tree is reported and not descended, which is
`SYS_KIND_DIR`-only descent and the reason no cycle guard is needed. `-newer`
*does* follow, so its reference file may be a link — and since OPFS keeps no
mtime for a directory, a directory is newer than nothing and a `-newer` whose
reference resolves to one matches everything. The help line says so.

`/bin/find` is 37,025 bytes and `rootfs/` is 1,415,017 of the 2 MB budget.

---

## `sort` and `uniq`, and the memory that bounds one

[TODO.md](TODO.md)'s A4, read against v7 by way of `v7besm/cmd/sort/` and
`v7besm/cmd/uniq/`. Almost none of v7's `sort` survives the move, and naming
what does not is most of what is worth recording: the `brk()` arena, the
back-off in 512-byte clicks, the stdio buffers reserved by allocating and
freeing them, the seven-way temp-file merge and the four character tables are
every one of them an answer to a PDP-11. What survives is the *semantics* — the
key model, the option letters and the tie-break rule.

**The input is held, and the entry said to say so.** A process has 256 pages
(`BRAAM_BIN_MAX_PAGES`), so `sort` is bounded at 16 MB of address space rather
than by a temp directory, and the usage block and `/etc/help` both carry the
sentence. There is no spill path and none is planned: spilling would mean
writing runs to the store and merging them back, which is the half of v7 that
cost it its arena, its `-T`, its `-m` and two of its bugs. A ceiling that is
said out loud is a better trade than a merge nobody can see working.

**Neither program reads through `File::getline`, and finding out why is what
this task actually cost.** The first `sort` was `grep`'s loop with a `keep()`
where the write was, and it trapped on a 512-line file with the stack four
hundred frames deep, alternating `proc_main` and `getline`. A `Task` that
answers **without suspending** resumes its awaiting coroutine from inside its
own final suspend, so the loop's next iteration runs on top of the frame that
was meant to have ended: over an already-buffered stream nothing ever returns to
the trampoline, and the shadow stack grows a frame a line. `FileGet`, `FileRead`
and `FileWrite` are awaiters with an `await_ready` that answers from the buffer
**without entering a coroutine at all**, which is why nothing else in the tree
had shown it — and `grep`, whose loop writes, unwinds on the flush. `grep zzz`
over 65,536 short lines traps like the first `sort` did. That is
[TODO.md](TODO.md)'s new B3, since the fix belongs in `getline` rather than in
its callers; what is here is the avoidance — both programs read a chunk at a
time through `Input` and split it themselves, so the only coroutine in the loop
is the one syscall that always suspends. Both write in 4 KB batches for the same
reason and gained the rest of D2's argument for nothing: dropping `File`
altogether took `sort` from 36,588 bytes to 26,277 and `uniq` from 31,216 to
21,173.

**What that ceiling buys is a storage decision, and it is the one thing here
worth copying.** Lines go into a chain of 64 KiB `String` blocks, each reserved
once and never appended past its capacity, with a `Vec<Str>` of views over
them; a line longer than a block takes a block of its own. A `String` that never
regrows never reallocates, which is what keeps a view valid — so the line table
is 8 bytes an entry and points straight at the bytes. The obvious alternative,
one arena that doubles, has a peak of three times its steady state at the copy
(the old block, the new block, and no way to overlap them), and under a hard cap
that is the difference between sorting about 5 MB and about 14 MB. The other
alternative, a `String` per line, pays an allocator block and a size class per
line for a table that is no smaller.

**Heapsort, in place.** O(n log n) with no recursion and no second table: a
bottom-up mergesort would be stable, but it wants another 8 bytes a line of the
same 16 MB, and stability is not what a sort is for here. GNU's is not stable
either without `-s`, and v7's own page already says which member of a set of
equal lines survives `-u` is not defined. What replaces stability is the
last-resort comparison POSIX asks for: when every key ties, the whole line is
compared with every byte significant — and under `-u` that one is skipped, since
equal keys are equal lines.

**The comparison needs no table at all.** `v7besm`'s port found `fold[]`,
`nofold[]`, `nonprint[]` and `dict[]` rotated by 128 for a signed `char`, and
read 128 bytes off the end of each of them on a machine whose `char` is
unsigned; it then had to decide what `-d` and `-i` mean above `0177` and write
the divergence down. None of that arises. The default order is bytes, and in
UTF-8 byte order *is* codepoint order — the one line of v7's `cmpa()` that was
already right — so `-d` and `-i` are simply not here, and `-f` folds ASCII alone
because a Cyrillic letter's case is a two-byte operation and `grep -i` in this
tree already draws the line there.

**`-n` compares text rather than a number.** The sign, the integer digits with
leading zeros dropped, then the fraction column by column with a missing digit
read as zero. That is a dozen lines, it has no range — the system case sorts a
23-digit value — and it keeps `braam_math` out of a binary that would otherwise
link a libm to compare `1.5` with `1.25`. A line with no digits in it is zero,
which is what every `sort -n` does.

**`-k` is the whole of v7's key model in POSIX spelling.** `<field>[.<byte>]`,
optional `bfnr` modifiers that replace the globals for that key, an optional
end position after a comma, and `-t` for the separator. Without `-t` a field is
a run of non-blanks *together with the blanks in front of it*, which is what
makes `-b` mean something; with `-t` a field is what lies between two
separators, so a key spanning fields carries the separators between them. The
`.byte` is a byte and not a character, the divergence `uniq.1.umm` had to write
down for `+n` and for the same reason. v7's `+pos1 -pos2` form is not accepted:
it says what `-k` says, and a `+1` on a command line here is a file name.
Without any `-k` the key is one that runs from field 1 to the end of the line,
so there is no second code path for the common case — `-b` reaches the whole
line through the same door.

**`uniq` cannot repeat any of its three upstream bugs.** `gline()` had no bound
and wrote through the end of a 1000-byte buffer into the one beside it;
`File::getline` grows a `String`, so there is no buffer to run off. Its end of
file threw away a final line with no newline, twice over — `File::getline` calls
that fragment a line, and the system case pins it. And `isdigit(argv[1][1])`
indexed a 129-entry table with a byte the caller chose, which cannot arise when
the skips are `-f` and `-s` values rather than a flag letter that might be a
digit. That last is also the flag-spelling change: v7's `-n` and `+n` become
BSD's `-f` and `-s`, since a `-2` that means "two fields" cannot coexist with
option bundling. `-c` combines with `-d` or `-u` rather than superseding them,
which is POSIX rather than v7's single `mode` character, and the count keeps
v7's four-column field. Input is files-or-stdin through `Input` like every other
filter here rather than v7's `[input [output]]` pair: the second operand there
is a file to *write*, and a redirection is how this tree spells that.

The blank-delimited field walk is written twice, once in each binary. There is
no library between two programs and there should not be one for nine lines;
`/bin/sort` reaches it through a key and `/bin/uniq` through a skip, and the two
would not share a signature anyway.

The two system cases turned up a rule the harness had never written down.
`submit` types a whole line before the kernel runs again, and what is typed
waits in that terminal's keyboard queue — `Channel<Key>`'s default 64 — so a
fixture line of 73 characters arrived cut at 64, with an unterminated quote and
every line after it answering a continuation prompt. It is now
[Testing.md](Testing.md) §5's ninth rule, since the failure is silent and reads
like a shell bug rather than a full ring. **Raising the queue was weighed and
dropped.** The two candidates are not the same constant: `KEY_RING` (32,
`src/user/tty.h`) is a claimant's ring and lives on the heap, where 64 slots is
536 bytes — past `MAX_SMALL`, so each one would cost a whole 64 KiB span
instead of a 384-byte block; the keyboard queue is a global and doubling it is
2 KiB of static memory, but `term` pastes past the ring **on purpose**, to prove
the pacing loop in `web/worker.js`, and a bigger ring only moves the length that
case has to paste. Nothing in the running system is bounded by either number —
a real paste is already paced — so the limit is the harness's alone, and it is
cheaper to write it down than to raise it.

`/bin/sort` is 26,277 bytes, `/bin/uniq` 21,173, and `rootfs/` is 1,462,781 of
the 2 MB budget. The system cases sort 65,536 lines and 128 KB — past one block,
which is the only thing that tells a chain of blocks from a single buffer, and
far past the depth the first version died at.
