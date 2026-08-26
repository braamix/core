# Release notes — 0.6

Reasoning, alternatives and trade-offs behind the code released as 0.6.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.6 — The programs a script assumed were there

`BRAAM_VERSION_BASE` moves to 0.6; the commit count and the hash behind it carry
on unedited. 0.4 was about how a program arrives and 0.5 about what it can count
on once it is here — a libm, a signal, an SDK that packages it. Both were
written from the point of view of a program being *written*. 0.6 is written from
the point of view of a script already running, and what that turned up was
smaller and more embarrassing: there was no `cp`, no `basename`, no `dirname`
and no `truncate`, and neither `pkg install` nor `pkg upgrade` would take an
operand naming what to act on. Every one of those is a line somebody had already
typed and got an error for.

**Three operations went in underneath, and only one of them was a new idea.**
`Sys::Seek` is op 30 and `Sys::Read` grew an optional length: the position had
existed since §5.2 — `FileIo::off`, on the process's own `Handle` — and no
program had an operation with which to name it. `Sys::Truncate` is op 31, whose
whole implementation had been complete and uncalled since M4, waiting for §8's
first rule to be satisfied forwards rather than backwards. The read span is not
an operation at all, just a number: `SYS_READ_MAX` is 65,532 where `SYS_CHUNK`
was 512, because the arithmetic that justified 512 had counted the payload and
forgotten the status word.

**`PROC_ABI` moves from 17 to 18, and that is the release's one real cost.** A
binary stamped for 0.5 meets `Err(Unsupported)` at `exec` — a diagnostic, not a
crash, and distinguishable from a file that was never a program. Every package
built against the 0.5 SDK has to be rebuilt and its repository re-signed;
the three in `braamix/apps` were, at a new packaging revision each, so that
`pkg upgrade` has a higher version to move to rather than an equal one to
ignore. `Sys::Truncate` then deliberately did *not* move it again, which is the
batching rule arriving one release late but arriving.

**The move is the assertion, not the count.** No commit between 0.5 and here
forced a new base. A copy that already existed three times over finally getting
one home in `src/proc/io.h`; a pair of path functions that TODO.md wrongly
believed were already written; an operand on two subcommands; a `ScreenClear`
that stopped being the last terminal operation nobody had to be authorised for;
a repository with an address of its own. Individually they are a system being
tidied. Together they change what a person can assume when they sit down to
write a shell script here — 0.5's answer to "what can I write" was "a program",
and 0.6's is "a program, or the five-line script that was going to call four
things that did not exist".

## A slot that waited four milestones for somebody to want it

`Sys::Truncate` is op 31, and `/bin/truncate` is the program that made it
legitimate. TODO.md's N2 was not "this is missing" but "this must not be built
yet": `vfs_truncate`, `Fs::truncate`, `OpfsFs::truncate`, `FsSyncOp::Truncate`
and `web/fs.js`'s `SYNC.TRUNCATE` were all complete and all uncalled, and
§8's opening rule — *every operation has a caller in `src/cmd/`* — reads
forwards, not backwards. What changed is that the caller arrived. Nothing under
`src/fs/` or `web/` was touched.

**By descriptor, not by path.** `Fs::truncate` takes an open handle, so op 31
is `ftruncate` and needs no new VFS entry point. A path form would have needed
one *and* would have walked into §5.2's exclusive-writer lock: the caller must
hold the file open to change it, and a second open of a file it is already
writing is `Err(Perm)` — it would have been refused for the very state that
makes the operation meaningful.

**The dispatcher case is `Seek`'s, and shorter.** Same guards — a descriptor
below `SYS_FD_MIN` or of any kind but `File` is `Err(Unsupported)`, a missing
handle or a short payload `Err(Invalid)` — plus the `busy_w` test §4.3 asks for.
It takes no `HandleRef` and no `HandleBusy`, because those arm a guard *for the
length of a syscall that suspends* and `vfs_truncate` is a plain `Result<void>`
that never awaits. Refusing a descriptor not opened for writing is
`vfs_truncate`'s own answer rather than a second check on top of it.

**`PROC_ABI` stays at 18, deferred to the next release.** Precedent said
otherwise — `Seek` moved it to 18 for exactly this shape of change — but slot 31
was already a hole in the enum, so nothing renumbers and no existing binary
names the new op. The bump is now something to batch rather than something each
operation pays for alone. The cost of waiting, written down so it is a decision
and not an oversight: a binary built today and run on a kernel built yesterday
meets `Err(Unsupported)` at the call instead of a diagnostic at `exec`. Within
one tree that cannot happen, since `make` rebuilds and re-stamps everything.

**The SIZE grammar lives in `src/proc/`, not in the program.** `truncate` takes
GNU's whole SIZE operand — `+`, `-`, `<`, `>`, `/`, `%`, and K/M/G/T against
KB/MB/GB/TB — and that parser is the only real logic in the change. **The
in-wasm suite cannot run a program**, so a grammar inside `src/cmd/truncate.cpp`
would have been untestable below the smoke suite. `src/proc/size.cpp` is
syscall-free and compiled straight into `tests`, which is the arrangement
`proc/opt.cpp` and `proc/time.cpp` already established, and `test_size.cpp`
covers every modifier, both unit families, and the three ways it refuses:
non-digits, an overflow past `SYS_SEEK_MAX`, and a rounding to a multiple of
zero. `SIZE_BLOCK` restates `FS_BLOCK` rather than reaching for it — a program
binary shares headers with the kernel, and the VFS's are not among them.

**N3 stays closed.** `truncate` is exactly the program that would have wanted an
`fstat`, and it did not need one: `seek_fd(fd, 0, SYS_SEEK_END)` on the
descriptor it already holds gives the only field anybody wants, which is what
N3 says. `-r` does not even need that — `stat_of` answers by path in one call
where an open, a seek and a close would be three.

**Only a modifier costs the extra call.** `truncate -s 0 f` is an open, a
truncate and a close; `+`, `-`, `<`, `>`, `/` and `%` add the seek, and `-r`
pays one `stat_of` for the whole run however many operands follow.

**`-c` succeeds on a file that is not there**, which is GNU's behaviour and
looks like a bug until you say why: the flag means *do not create*, so a missing
file is the outcome asked for rather than a failure. It reaches the program as
the `Err(NotFound)` an open without `SYS_O_CREATE` returns, which is the one
error the loop swallows.

**`-s` had to be a valued option for `-s -100` to parse.** `OptParse` takes the
whole following word for a valued letter, so the leading `-` never reaches the
flag-bundle path; a bare negative *operand* would still be eaten, but truncate
has none.

**The fake filesystem did not grow.** `test/unit/test_vfs.cpp`'s `TempFs`
implemented `truncate` as a shrink alone, which nothing had noticed because
nothing called it. `FileSystemSyncAccessHandle.truncate` zero-fills, and so do
`web/fs.js` and `test/fakefs.mjs`; the fixture now does too, and a VFS case
asserts a grow reads back as NULs. A divergence between a fake and the thing it
stands for is only harmless while the operation is dead.

Two things the smoke case had to be shaped around, both of them the suite's
rules rather than the program's. The screen is 60 columns and a command that
wraps puts its own tail into the output, so the case `cd`s into `/home/q` and
uses bare names, as `cp` does — and the usage line was shortened to fit, with
the modifiers moved into `/etc/help` where there is room to name them. And `wc`
reads a NUL-filled file as one word, since a NUL is not whitespace, so every
size assertion goes through one helper rather than thirty hand-written triples.

`truncate` is 18,057 bytes and takes `rootfs/` to about 1.28 MB of the 2 MiB in
`tools/size_budget.txt`.

---

## The last operation that could blank somebody else's screen

TODO.md's N1: `ScreenClear` was the one operation in the terminal block with no
authorisation check at all, so any process could blank the grid a full-screen
program was painting. It now refuses while another process holds the alternate
screen. The wire does not move — same op number, same empty argument and
payload, so `PROC_ABI` stays where it is. What moved is the answer.

**This supersedes "`ScreenBlit` is checked; `ScreenClear` is not"**, further
down this file, which stands where it is. That note argued the operation should
stay open "because `clear` and `watch` blank the shell's own screen without ever
claiming it, and refusing them would be a change to two programs to enforce a
rule about a third". The reasoning was sound and the arithmetic was wrong: it
priced the fix against `ScreenBlit`'s guard, which is the only one in the block
that would have cost those programs anything.

**There are two guard shapes, and this one is not a blit's.** `ScreenBlit` and
`KeyRead` ask *do you hold it* — `!p.alt` and `!p.keys` — because a blit is what
the claim exists for. `Cursor`'s set, `Style` and `Echo` ask the weaker
question, *does somebody else hold it*: `tty_screen_owner()` non-zero and not
the caller's pid. `ScreenClear` belongs with the second three, and taking their
test verbatim costs its callers nothing, because all of them run with the screen
free. The old note counted three programs where there are three callers and one
of them is not a program: `/bin/clear`, `watch`'s repaint, and the shell's `^L`,
which that note missed.

**The strict shape was not available anyway.** A `/bin/clear` that took the
screen to satisfy `!p.alt` would blank nothing: `~FullScreen` copies its
snapshot back, so claim-clear-release is a no-op by construction. The shell
could not have claimed for `^L` either — the prompt lives *on* the scrolling
grid, and taking the alternate screen is what hides it. An operation whose only
callers cannot satisfy a rule does not have that rule; it has the other one.

**A holder may still clear its own grid**, which is what `owner != p.pid` says
rather than `!owner`. Nothing does today — `edit` and `less` paint by blitting a
grid of their own — but refusing it would be a rule with no argument behind it,
and the three neighbours it copies do not refuse it either.

Nothing else changed. The callers were left alone: all three already returned
1 (or `Err(Io)`) on any error from the call, and a diagnostic would be written
to a grid the holder is painting over and the restore then puts back, which is
the same reason the test below writes its answer to a file.

**The test is the pipeline, not a background job.** `test/smoke/fullscreen.mjs`
runs `edit /home/m7.txt | sh -c 'sleep -m 200; clear; echo $? > /home/n1'`: the
editor takes the screen, the second stage's timer fires under it, and the clear
is refused. `&` would have been the natural way to put a second process beside a
foreground one, but `g_next_id` in `sh/job.cpp` only climbs, so a job taken here
would renumber `jobs.mjs`'s `[1]` two cases later — the cumulative session's
cost, and the reason the sleep does the ordering instead. Two assertions,
because the failure has two faces: the holder's grid still reads `hXello`, and
`/home/n1` reads 1. Before the change the first of those is a blank screen.

The line is typed in three goes because `KEY_RING` is 32 and `type()` does not
tick between characters, so a 71-character command loses its tail. That is the
ring's documented policy rather than a bug, but it is the kind of thing a case
discovers the hard way.

---

## `pkg upgrade` learns operands, and the pin learns how to come off

The section below says "**`pkg upgrade` keeps its no-operand form.** `pkg
upgrade hello` remains a usage error rather than a second spelling of the new
`install`: one command per act." That was written a commit ago and it is now
wrong, as is P20's older "There are no operands here … so an argument is a usage
error". Both stand where they are; this supersedes them. What changed is not the
reasoning but what the two commands are for.

**The act really is different, which is why it is an operand and not a second
command.** `install` adds a root to `/pkg/world` and then makes the world
consistent; `upgrade` adds nothing and never installs anything new. Both can be
asked to move one package, and asking `upgrade` to do it leaves world's shape
alone — which is precisely what you want when the package is already a
dependency of something else and you do not want it promoted to explicit.

**Without operands nothing moved.** `in.flags = SOLVE_UPGRADE` is the run-wide
flag over everything world reaches, and that is still exactly what a bare `pkg
upgrade` does. With operands it clears that and flags the named packages instead
— apk's own upgrade branch, whose second half we had not built.

**The flag inherits, where apk's `upgrade <pkg>` passes 0.**
`SolveRequest{name, SOLVE_UPGRADE, SOLVE_UPGRADE}`, the same pair `install` now
uses. apk confines it to the named name; we do not, for the reason the previous
section gives — a package upgraded onto libraries older than anything it was
built against is the state nobody wants and the one hardest to notice.
`test/unit/solve.data`'s `upgrade3.test` still models apk's shape and still
passes, because `apply_args` mirrors apk's applets, not ours — and the property
that fixture proves, an operand moving that name and leaving world's other
members alone, is the one we needed.

**The unpin is part of the act rather than a flag.** The previous commit made a
sideloaded archive pin itself in world as `name=version`, and left `pkg install
<name>` as the only way off. That is a poor pair: to get the newest version of a
package you have to type the command that adds roots. `pkg upgrade <name>` now
drops the clause, so the two ways of saying "stop holding this here" are the two
commands that could have meant it.

**Any version clause, not only `=`.** `world_unpin` clears `hello=1.0-r0`,
`hello>=1.0` and `hello~2.2` alike. One rule is easier to hold than two, and the
distinction between a pin `pkg install` wrote and a constraint someone typed is
not one the file records — both are the same sentence in §6's grammar. A `!name`
conflict is skipped: it is not a hold, and rewriting it as `name` would silently
invert it. That is the whole reason `world_unpin` tests `VER_CONFLICT`, and
`test_db.cpp` pins it, since no smoke fixture plants a conflict into world.

It needs no allocation. `dep_parse` sets `d.name` to a view of the spec's own
first bytes, so taking the clause off is `had = d.name` — the same block, a
shorter view. Every line naming the package, not the first, for the reason
`world_drop`'s comment gives.

**A name nothing installed answers to is refused**, `pkg: <name>: not installed`
and 1, which is what `pkg files` and `pkg verify` already say. `pkg remove
nonesuch` is silent about the same mistake, and that is right there — a removal
that finds nothing to remove got what it asked for. An upgrade did not: it will
never install the name, so a typo has no other way to surface. The check is over
the installed stanzas rather than the generation's `packages` file, so
`pkg upgrade cmd:hi` resolves through `p:` the way every other name in this
program does; `survives` and it now share one `stanza_provides`.

**The usage block got two columns wider.** `upgrade [<package>...]` is 22
characters against `install <package>...`'s 20, and `usage()` computes the
description column from the widest row — so every description moved right, and
the `-v, --verbose` line in `HEAD`, which is aligned to that column *by hand*,
moved with it. The note at the top of this file predicted this exact ripple when
the table was built. The `-v` row is now 61 characters and no longer fits the
smoke suite's 60-column grid, so `pkgcli` resizes around it as `pkg-update`
already does for `pkg search`.

## Installing something installed is upgrading it

`pkg install hello` used to print `generation 1, unchanged` when the index had
moved on, and P20's note called that "the difference between the two commands
stated as a test". It was the wrong difference to have. Nobody types `install`
to be told nothing happened, and the command that would have done what they
meant — `pkg upgrade` — moves *everything* world reaches, which is a much
larger act than the one asked for. The distinction that is worth keeping is
scope, not freshness: `install` moves what you name, `upgrade` moves the lot.

**One line of the plumbing, because the solver already had the knob.**
`pkg_install` pushes `SolveRequest{name, SOLVE_UPGRADE, SOLVE_UPGRADE}` instead
of no flags at all. `compare_providers` has an `ipkg` rung above the version
comparison that is skipped under `SOLVE_UPGRADE`
([solve.cpp:769](../../src/cmd/pkg/solve.cpp#L769)), which is the whole of why
the installed copy used to win. Nothing in `src/cmd/pkg/solve.cpp` changed, and
no new case was needed in `test/unit/solve.data`: apk's own `basic17` is `add
--upgrade a`, which `test_solve.cpp` already turns into exactly this pair of
flags.

**Per-name, not the run-wide flag `pkg upgrade` sets.** apk's upgrade branch
sets the run-wide flag and, given operands, clears it and flags the named
packages instead; this is that second form, reached from `install`. So world's
other members are untouched — `pkg install hello` cannot quietly bump something
that merely happens to be installed beside it.

**The third field is `SOLVE_UPGRADE` too, so it inherits.** A package's
dependencies move with it. The alternative — flag the operand alone and let
dependencies move only where a constraint forces them — leaves a package
upgraded onto libraries older than anything it was ever built against, which is
the state nobody wants and the one hardest to notice. apk's `add` inherits for
the same reason.

**A sideload now pins its version, and that is not a separate feature.** Once
`install` prefers what is new, an archive joining `/pkg/world` under its bare
name loses to a newer installed copy — `pkg install ./hello-0.9.zip` would have
printed `unchanged` and thrown the bytes away. So an archive joins as
`<name>=<version>`, apk's `apk_dep_from_pkg`, and installs what it named
whichever way the versions compare. `world_push` replaces by name, so naming
the package afterwards clears the pin; until then `pkg upgrade` leaves it
alone, which is what a pin is for.

`pin_spec` falls back to the bare name when `<name>=<version>` does not parse
back to the same two halves. §3.2 holds `V` to a *path component*, not to §7's
version grammar, so a sideload may legitimately carry a `V` that `dep_parse`
calls broken — and a broken dependency satisfies nothing, so pinning one would
have turned a package that installs today into one that cannot be resolved. A
name carrying `<`, `>`, `=` or `~` fails the same check and takes the same
route. `test/smoke/pkg-local.mjs` installs a `V:!!` to hold that open.

**The `installed at a different digest` refusal is now reachable from a second
command.** P20's analysis of `pkg upgrade` applies verbatim: skipping the early
`ipkg` rung means a local record and an index that disagree about the metadata
of one name-version can produce a `Replacing`, which `stem_state` refuses
because §8 makes a store directory immutable and a rollback target may be
executing out of it. The refusal is still the right answer; there is simply one
more way to arrive at it.

**`pkg upgrade` keeps its no-operand form.** `pkg upgrade hello` remains a usage
error rather than a second spelling of the new `install`: one command per act.

The smoke suite's `pkg-upgrade` case used to assert the old behaviour in one
line. It now spends a generation on the new one instead — rolled back to 1.0-r0
with nothing serving the archive, `pkg install hello` moves it to 1.1-r0 out of
the store directory that is already there, which also shows `stem_state`
skipping a fetch it does not need.

## `basename` and `dirname`, and the pair that was not already there

`doc/TODO.md` A2 said the work was done — "`src/fs/path.cpp` already has both".
It does not. `path_basename` and `path_dirname` take what the VFS deals in: an
absolute path that `path_resolve` has already normalised, with no trailing
slash and no empty component. Handed what a shell script hands `dirname`, they
answer `path_dirname("foo") == "f"`, `path_dirname("") == ""` and
`path_basename("foo/") == "/"` — each of them right for the caller they were
written for and wrong here.

**So the semantics stay apart.** POSIX's pair is string work over arbitrary
text: it opens nothing, resolves nothing, and has never heard of a working
directory. The VFS's pair is a step in resolution. Putting both in `path.cpp`
would have put two functions one adjective apart in a header that **every
binary in the tree links** — `src/proc/` compiles `path.cpp` in — where picking
the wrong one is a silent bug rather than a link error. What is reused is
`path_basename`, at the one point where the two agree: once the trailing
slashes are trimmed and the all-slashes path has answered `/`, the tail after
the last separator is the same tail. `dirname`'s rule is written out, because
dropping the separator *and the run in front of it* is what makes `a//b` say
`a`, and the kernel's version has no reason to do that.

**GNU's flags, not POSIX's minimum.** `basename` takes `-a` and `-s <suffix>`
as well as `<path> [<suffix>]`; `dirname` takes several operands. The suffix
form earns its place twice over here: this shell has no `${x%.c}` — the four
operators in Shell.md §6 are the whole set — so stripping an extension has no
other spelling. `-z` is left out: nothing in the tree reads NUL-separated
input, and the program that would (`xargs`, A7) is not built yet. One rule
worth writing down: a name that is *only* the suffix keeps it, so
`basename .c .c` is `.c` rather than nothing.

**They are programs, not builtins.** Both are pure string work whose whole cost
is the spawn, which is the second half of CLAUDE.md's builtin test — but that
set is closed at `test`, `[`, `:`, `echo`, `true` and `false`, and widening it
is a design change to argue in Concept.md first, not a thing to slip in with
two programs. A loop that calls `dirname` once a turn pays a worker a turn;
that is what the shell's own `$(…)` costs everywhere else.

9.5 KB and 7.4 KB, against §4.4's duplication; `rootfs/` is 1.26 MB of the 2 MB
budget.

**A line in `/etc/help` is somebody else's arithmetic, again.** Two entries went
in and three lines with them — `basename`'s wraps — and `test/smoke/subst.mjs`
concatenates that document three times and asserts what `wc` prints over it:
`345 2613 17385` became `354 2718 17994`. Its comment claimed "17,148 bytes is
thirty-four chunks against eight slots", which was already two changes out of
date — the byte count predates the last `/etc/help` edit and the chunk count
predates `SYS_READ_MAX`. The count is gone rather than re-derived: what a deep
pipeline now costs is TODO C2, to be measured rather than asserted in a
comment.

`doc/Shell.md` §14 gained three rows rather than two: it was missing `cp`, left
out when that landed.

---

## `/bin/cp`, and the copy that was already here three times

The system had `mv` and no `cp`. The copy was not missing — it was written
three times over: `copy_file` and `copy_tree` in `src/cmd/mv.cpp`, the same loop
open-coded in `src/cmd/fimport.cpp`, and the extract path in `/bin/pkg`.
`mv.cpp`'s comment said so outright ("fimport.cpp's loop") and nothing acted on
it.

Both now live in `src/proc/io.h`, which is where the header already said this
belongs — "the few helpers a program would otherwise write again" — beside
`make_dir_all`. `mv` calls them rather than owning them, so this is a program
gained and a duplicate lost rather than code added. `--gc-sections` keeps them
out of a binary that does not copy.

`cp` takes `-r`, `-f`, `-i` and `-n`, and resolves its operands exactly as `mv`
does: with several sources the last is the directory they land in. A symbolic
link is recreated rather than followed, which is `copy_tree` descending on
`SYS_KIND_DIR` alone and is why it needs no cycle guard.

**What it does not do.** There is no `-p`, and there cannot be: OPFS cannot set
a modification time (`web/fs.js`'s `touch` rewrites a byte so the browser
restamps to *now*, then reads the stamp back rather than believing it). The
same limit is why **`mv` of a directory already restamps the whole tree** —
`FileSystemFileHandle.move` is `getFileHandle` only, so a directory rename is
always `Err(Unsupported)` and always falls into `copy_tree`. "A rename that is
sometimes a copy" was in the known gaps; that the copy loses every mtime was
not, and now is.

`copy_tree` requires its destination not to exist, so `cp -r a b` merges into an
existing `b/a` no more than `mv` does. Consistent rather than clever; recorded
in `doc/TODO`.

---

## A read is a span, not half a kilobyte

`SYS_READ_MAX` is 65,532 and `read_chunk` names it. `svc_chunk` yields 64 KiB.
**`PROC_ABI` does not move**, and that is the point.

**The arithmetic the old number rested on was wrong.** `SYS_CHUNK` was 512
because "`FS_BLOCK` is the allocator's top size class, and one byte more costs a
whole 64 KiB span" — but the reply is not 512 bytes, it is `4 + 512`. The status
word goes in first, and `String::reserve` doubles from 16, so 516 asks for 1024,
which is past `MAX_SMALL` and takes the large path: **a full 512-byte read has
always cost a whole span**, on both sides of the wire. Under the stated
reasoning the right value would have been 508. The question was never "should a
read start paying for a span" — it was already paying — but "should it get 128
times the data for the one it pays for".

**It clamps, so the ceiling is not ABI.** `Sys::Read`'s length has always been
an upper bound that the kernel may lower, and a short read has always been
legal with the remainder kept on the descriptor. So an old binary names no
length and still gets 512; a new one naming 65,532 to an older kernel gets 512
and loops. Nothing stamped is invalidated and no installed package breaks —
which is what makes this cheap now and is the first thing to doubt. T6
(below) was decided against a cost that was not there.

`SYS_READ_MAX` is `65536 - 4` rather than 65,536 so that the status word and a
full read together are one span exactly, and `sys_read_want` moved to
`sysabi.h`: it is a wire rule, both ends want it, and on the header it is
reachable from the unit suite.

**The caller was already there and already being refused.** `FdZipSource::read`
(`src/cmd/pkg/unzip.cpp`) asks for the whole remaining span of a zip entry — an
arbitrary number — and was silently cut to 512 and made to loop. That is
`/bin/unzip` and `/bin/pkg`, both shipped. `read_exactly` in `/bin/tail` is the
second. §4.3's "every operation has a caller in `src/cmd/`" is satisfied by code
that was written before this change.

**The workload T6 asked for is `pkg verify`**, which opens every installed file
and hashes it: 1.1 MB, some 2,210 reads, about 100 ms of round trips. It is now
about eighteen. `test/smoke/chunk.mjs` measures the invariant rather than the
figure — `wc` of a 64 KiB file against `wc` of a four-byte one, asserting the
difference in round trips is a handful and not a hundred and twenty-eight.

### The stream branch was dropping what it read

Raising `svc_chunk` first turned `pkg install` red, and the bug it found was
older than the change. `Sys::Read` served a pushback for a pipe and for
descriptor 0, and **not for a fetched body, a socket, an inflate stream or a
picked file** — those four called `chunk_keep`, which *stores* the remainder on
the handle, and then never looked at it again. It was invisible while the host
could not yield more than `want`: the remainder was always empty. With a 64 KiB
yield against a 512-byte `want`, every read past the first 512 bytes of a
downloaded archive went into a buffer nobody read, and the digest check caught
it. The check is now hoisted above the kind dispatch, so every kind that fills
`pend` is served from it — one place instead of two, which is why the third and
fourth were missed.

### What this costs

A pipe's eight `Channel` slots can now hold 512 KiB rather than 4 KiB, and
`BRAAM_BIN_INITIAL_PAGES` is four, so a 64 KiB `_alloc` puts `memory.grow` — and
§8.4's detached views — on the read path rather than on the rare path. The file
read is bounded by what the file has left, so a large `want` on a small file
does not take a span to fetch a handful of bytes. `test -x` was the one caller
that had to be told to ask for less: it decides from the magic or the `#!` line
and now names `PROC_SHEBANG_MAX`, or every `test -x` would pull 64 KiB off the
store — which is what the `static_assert` tying the two together was for.

---

## A descriptor can be told where to be

`Sys::Seek` is op 30, and `Sys::Read` grows an optional length. `PROC_ABI` moves
to 18.

**The position already existed; nothing could name it.** Everything below the
process is positional — `vfs_read(fd, off, …)`, `vfs_write`, `vfs_size` — and
deliberately stateless, because that is what lets several descriptors share one
backend handle under OPFS's exclusive lock (§5.2). The offset therefore had to
live *above* the VFS, and it does: `FileIo::off`, on the process's own `Handle`.
So the kernel has always known where each descriptor was, and a program had no
operation with which to ask or say. `SYS_O_APPEND` was the one thing that moved
it, open-coded as a flag; it is now a `Seek(0, SYS_SEEK_END)` folded into the
open, which leaves exactly one place in the system where a position becomes
something other than the next byte.

**Why the answer is data and not the status.** A position is a `u64` and the
status is an `i32`. Returning a truncated one would be wrong only past 2 GiB,
which is the worst kind of wrong: invisible until it is not. `Stat`, `Clock` and
`Storage` already answer 0 and put their wide values in the data.

**Why 0, 1 and 2 refuse.** They are not descriptors at all — they are the
`Stdio` the pipeline stage was handed, and there is no handle behind them to
move. Making them seekable means identifying a file-backed stream by comparing a
function pointer through a type-erased context, which `console_is_input` already
does once and which should not become a habit without §4.3 saying so. None of
the callers needs it.

**The length on `Read` is the more interesting half.** It exists because of a
bug the seek work uncovered rather than caused. `/bin/sh`'s `read` has to stop
at a newline, but a chunk is whatever the writer wrote, so it over-read and kept
the remainder in a process-global buffer keyed by the descriptor *number*.
Numbers are reused. A pushback left by one pipe was therefore matched against
the next pipe to be handed the same number, and prepended another stream's bytes
to a later line — a real corruption, sitting in the tree, reachable from two
ordinary pipelines in a row.

Seek fixes it for files and cannot fix it for pipes, which nothing can wind
back. A length can: the shell now reads a pipe a byte at a time and never takes
more than the line. That costs a syscall per byte on a non-seekable descriptor,
which is the price of correctness on a stream whose lines are short; a *file*
still reads a chunk at a time and seeks back, so the fast path is unchanged.

For a length to be honest on a stream, the bytes it does not cover cannot be
discarded — so the kernel keeps them, on the `Handle` for a descriptor and on
the process record for 0. That is the same buffer the shell was keeping, moved
to the one place where it belongs to the open description rather than to a
number: it dies with the handle, so a reused descriptor cannot inherit one. The
shell's global is gone, and with it the gap `Shell.md` used to list.

A `max` above `SYS_CHUNK` clamps rather than growing the read. 512 is
`FS_BLOCK`, the allocator's top size class on both sides of the wire (§8.2), and
one byte more costs a whole 64 KiB span — so the length may shorten a read and
must never lengthen one.

**Op 31 is left free** for the `Truncate` that `vfs_truncate` would need if
anything ever called it. §8's rule against growing the table on speculation says
not to add it now.

### `tail` stops reading the whole file

`tail -n 10` on a four-megabyte log was some eight thousand reads, because the
last line is the last one and there was no way to start anywhere else. It now
seeks to a window at the end and widens it — 512 bytes, doubling — until the
window holds the lines asked for. A window that starts mid-line begins with a
fragment of a line that is not ours, and dropping it is the whole subtlety.

The cost is a re-read of at most the final window, so under twice the bytes of
the answer, against the whole file before. `TAIL_WINDOW_MAX` stops the doubling
at a megabyte: a file that is one enormous line must not be pulled into memory
by a program that handled it before, so past the cap it reads the file through
as it always did. Any refusal to seek does the same, which makes the fallback
one path rather than two.

**The fast path is one named file only.** Several files are the last lines of
their *concatenation* — not GNU's per-file headers — and seeking backwards
through a reversed file list to preserve that is real complexity for something
nobody runs. Both paths feed the same ring and the same printer, so the bytes
out are the bytes out; `test/smoke/tail.mjs` asserts the two agree by running
`tail -n N file` against `cat file | tail -n N`, stdin being the thing that
cannot seek.

`Input` was left alone. It multiplexes several paths and stdin and opens them
one at a time, and handing out a descriptor would leak that into every caller
for one program's benefit.

### The zip reader reads through a source

A zip is read back to front — the directory is at the end — and with no way to
seek, the only way to reach it was to hold the whole archive. `zip_entries` took
a `Str` of the entire file, and `unzip -l big.zip` therefore read every byte of
it to print a listing.

It now takes a `ZipSource`: `size()`, and a read of exactly *n* bytes at an
offset. `/bin/unzip` implements it over a descriptor with `Sys::Seek`, and
`/bin/pkg` and the unit suite over a buffer they already hold. Parsing is two
reads — the end record, then the directory — plus a thirty-byte local header per
entry, whatever the archive weighs.

The first of those is twenty-two bytes, not the sixty-four kilobyte window the
format allows: an archive with no comment has its end record exactly at the end,
and that is every archive `tools/pack.py` writes. The window is read only when
those twenty-two bytes are not it, which is what keeps a correct reader from
paying for a case almost nothing exercises.

**The shape of the interface is `PkgHost`'s, and for the same reason.**
`zip.cpp` is compiled *into* `tests.wasm`, where a syscall is a link error by
design (Testing.md), so the bytes have to arrive through something abstract. It
is the trick `trust.cpp` and `index.cpp` already use, applied once more.

**The central directory stays resident, and that is deliberate.** It is small,
and it is what a `ZipEntry::name` views — so `DbFile`, `local.cpp` and the
`§8.1` record keep their `Str`s and needed no edit at all. `ZipEntry` loses its
`data` view and gains the offset and length it was derived from; `ZipDir` holds
the directory and the entries together so the lifetime is one object's.

**What this buys, and what it does not.** `unzip -l` and `unzip one-entry` stop
holding the archive, so one larger than the sixteen megabytes a process gets is
now readable. `/bin/pkg` gains nothing: `acquire` hashes the whole archive and
`local_load` caps it at `PACKAGE_MAX` before reading, so it is resident either
way — it passes a `MemZipSource` over what it already has. Deflated entries gain
nothing either, since `Sys::Inflate` stages its input and the packed bytes must
be in memory to be handed over. And `tools/pack.py` always deflates, so the
stored path this makes cheap is exercised by the unit suite and by nothing in
the tree. The win is narrow and real; it is not a general reduction in what a
package costs.

`ZipRead` gains `Io`. As a coroutine over a source that can fail, a read error
had nowhere to go but `Malformed`, and calling a working archive malformed
because a descriptor hiccuped would be a lie.

---

## The repository has an address of its own

`/etc/repositories` moves from `https://pub.sergev.org/braam` to
`https://braamix.github.io`. Nothing in the code or the formats changes: it is
one line of configuration, and the URL never appears in a source file, a test
fixture or a manual — the documents say `https://packages.example/braam` and
`tools/mkrepo.py` says `https://packages.test/braam`, so a real host was
already only ever in this one place.

**The move is a move of hosting, not of trust.** `/etc/anchor` is untouched,
because the keys did not change — only the machine serving what they signed.
What did have to be redone is the index itself: `N` is inside the signed
region and `index.cpp` refuses an index whose `N` disagrees with the line that
fetched it (§7's `header` step, `Err(Perm)`), so the repository was re-issued
under the same index key rather than re-pointed. That check is why a repository
cannot be relocated by a redirect, and moving one is the case it was written
against.

**No client has to be told.** The boot unpack replaces `/etc` wholesale at
every version change (Package_Management.md §6), so a tab that has ever run
this version has the new line; a tab that never opens another version keeps
fetching an address that is no longer answered, and the `pkg -v` trace added in
0.4 is what tells it apart from a repository that is merely down. An upgrade
path for the file was not considered, for the same reason a stale `/share` is
not deleted at boot: the archive is the upgrade path.

Two prose files named the old address by implication rather than by spelling —
README.md said there was no public repository, and `rootfs/README` said a URL
"should be put in" a file that already had one. Both now say where the system
points, which is the only reason the change touches documents at all.

**`/etc/help` goes back to being a list.** The five prose passages explaining
command lookup, package checking, generations and `pkg verify`'s limits are
what the manuals are for; a page reached by typing `help` is worth more when
what is on it is the names and their spellings, and nothing on that page was
the only copy. `test/smoke/subst.mjs` counts three copies of the file as its
many-writes case, so the numbers beside it move with the trim — which is the
comment there saying they would.

**The README's launch button is a file in the tree, not a badge service.**
`https://braamix.github.io` serves the system itself at the root and the
package index beside it, so the same address the repository line names is also
the thing to click. The usual way to put a green button in a README is an
`img.shields.io` URL, which makes a project whose first claim is that nothing
is borrowed fetch its own front door from somebody else's server, and breaks
the day that server does. `doc/launch.svg` is two rectangles, a triangle and
one `<text>`, drawn with presentation attributes because GitHub strips
`<style>` out of an SVG, and naming a font *stack* rather than a font because
the browser rendering it is not this one.
