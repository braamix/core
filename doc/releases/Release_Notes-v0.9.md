# Release notes — 0.9

Reasoning, alternatives and trade-offs behind the code released as 0.9.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.9 — The programs a pipeline needs, and the screen a program opens

`BRAAM_VERSION_BASE` moves to 0.9; the commit count and the hash behind it carry
on unedited. 0.7 was written from the point of view of a program being
*ported*, and 0.8 from that of the *page*. 0.9 is written from the point of view
of the **command line**: what a pipeline can be assembled out of without leaving
the system. Eleven programs arrive, `/bin` goes from 45 names to 56, and Tab
finally completes — and the one thing that reached the kernel is the smallest
half of the release.

**`PROC_ABI` moves to 20, and that is the headline for anyone holding a
binary.** Every stamped binary must be rebuilt and every repository re-signed; a
package built against the 0.8 SDK is refused at exec. Adding an *operation*
would have been free, as `Truncate`, `FStat` and `Mount` all were inside 19.
What is not free is that eight existing operations now read a terminal out of
the upper bits of their argument, and an argument that changes meaning is
exactly what the number refuses a stale binary for. `SYS_TERM_SELF` is zero, so
every call site in `src/cmd/` and `src/proc/` compiles unchanged and the whole
migration is a rebuild.

**A screen is a descriptor.** `Sys::TermOpen` names a terminal and hands one
back, after which `Read`, `Write` and `Close` serve it — §4.3's own rule, which
is why writing text to another grid added no operation for it. 0.8 gave the page
four terminals and a process exactly one; an emulator with a second console
line, and `edit -S`, are the callers that asked for more, and §4.3's first rule
is that the caller comes first. A `/dev/tty1` path, two processes and a pipe,
and a "current screen" the process sets were the three alternatives, and the
last of them is the same bug 0.8 rejected one level up. `resize` grew a fourth
argument, `TERM_NO_SHELL`, because a shell at its prompt holds its terminal's
keyboard and a panel needs one that does not; `/proc/terms` lists what the page
put up, which is §4.3's other rule — a file rather than a call.

**Eleven new programs, and two rewritten.** `find`, `sort`, `uniq`, `du`,
`xargs`, `tee`, `cut`, `tr`, `seq`, `cmp` and `diff` are new, and `wc` and
`head` are ports where there had been sketches — `wc` had no options at all.
They are read against v7, LiteBSD and FreeBSD for the semantics, the option
letters and the output strings, because scripts read those; almost none of the
*code* survives the move, and naming what does not is most of what is written
below. None of them needed an operation the kernel did not have, which is what
the plan they came from predicted: the gap was the program layer, and it is
closed. The staging tree is 1,748,884 bytes of the 2,097,152 in
`tools/size_budget.txt`, and `kernel.wasm` is 192,517 of 262,144.

**They found one bug, and it was under everything.** `sort` trapped on a
512-line file with the stack four hundred frames deep. A `Task` that answers
**without suspending** resumes its awaiting coroutine from inside its own final
suspend, on the awaiter's own stack, so a loop over an already-buffered stream
never reaches the trampoline and the shadow stack grows a frame an item.
Measured before the fix: `cat big | tail -n 1` died at 1,024 lines, `less big`
at 2,048, `grep zzz big` at 4,096, and `du -s` at 1,000 entries in one
directory. `File::get`, `File::getline`, `LineReader::next` and `TreeWalk::next`
are awaiters with an `await_ready` fast path now rather than `Task`s, which is
§3.3 stated as a rule instead of discovered a program at a time.

**Tab completes, and nothing had to move for it.** It was the one entry in
[Shell.md](../Shell.md)'s list of deliberate absences with no argument anywhere
behind it — an omission wearing a gap's clothes. The line editor has been in the
shell *process* since the discipline left the kernel, so a `co_await
list_dir(…)` between two keystrokes is ordinary code; `Sys::List` and
`Sys::Stat` were already in `/bin/sh`'s import surface, put there by globbing.
The word under the cursor comes from the **lexer**, not from a backwards scan
for a blank, which would have been a second and worse copy of `tokenize.cpp`.
It is bash's two-Tab behaviour rather than zsh's cycling, and it costs 14,799
bytes: `/bin/sh` is 240,041, up from 225,242.

**Two things were destroying files, and one of them was filed as a missing
feature.** `cp -r a b` with `b/a` already there did not fail and did not merge:
it **removed `b/a` and everything under it**, gated on the two kinds rather than
on any flag, so the default over a directory it had copied before was a silent
recursive delete of whatever the source no longer had. And `vfs_rename` refused
an *empty* destination directory that `rename(2)` replaces, so `mv a b` with an
empty `b/a` was an error where Unix moves; a populated one is `Err(NotEmpty)`
now, which is `ENOTEMPTY` and reads as what happened. The ordering there is a
safety property rather than a preference — the verdict has to be reached before
the cross-mount `Err(Unsupported)`, because that answer is an *instruction* with
a destructive half.

**Two listings changed their minds.** `ls` hid nothing, so `*` and a bare `ls`
disagreed about what a directory held and the disagreement was `ls`'s; a leading
dot has to be asked for now, filtered where the `-R` walk and `-l`'s `total`
both see it, so hiding a name and not descending into it are one decision.
`.` and `..` are still not synthesized under `-a`, and `find` still shows
everything on purpose. And `df`'s columns are as wide as their widest value: BSD's
widths are a floor now, because a nine-figure `Avail` filled its field exactly
and ran into `Used`.

**The port kit is finished, and every port has stopped keeping a C library.**
Group A's remainder and the whole of Group B landed — streams, descriptors,
directories and `struct stat`, as `compat/cio.h` and the `b_*` family — and then
all nine packages in `braam-apps` were migrated onto them: 3,196 lines deleted
against 1,365 written, eight files gone, and `zip` alone moved about eight
hundred call sites. The kit was truncating every file past 2 GiB until `zip`
noticed, because `long` is 32 bits here and `off_t` is 64; `b_fseeko` and
`b_ftello` are the bodies now. `PORT NOFLOAT` drops `snprintf`'s float
conversions for the six ports that format none, about 5,090 bytes each, over two
archives and a **strong** reference — the obvious weak one resolves to null and
prints nothing, which is why the saving sat on the table for a release. None of
this reaches the system: `kernel.wasm`, every `src/cmd/` binary and `rootfs.zip`
are byte-identical across the whole of it, because nothing in this tree links
the kit.

**`doc/TODO.md` is deleted.** It held the sequence of what was left, and
sections A, B, D and P are all spent: A's twelve programs are all in `/bin`, B's
four defects are fixed, D asked four questions about a buffered stream and
three of the four answers were "measured, and it costs more than it buys", and P
built the kit and migrated everything onto it. An empty sequence is not a
document. Its one piece of standing reasoning — why the syscall table is *not*
the gap — is written down below instead, so that question is not re-derived.

**One thing CI found that three passing suites at home could not.** The kit's
`<endian.h>` argument in 0.8 was right about clang and wrong about *which*
clang: the freestanding header arrived in clang 23, and ubuntu-latest ships
18.1.3. The kit supplies one in `limits.h`'s shape — `#include_next` when the
compiler has it, the same names from the same predefines when it does not —
rather than moving the toolchain floor for every consumer of the SDK to clang
23 for thirty lines of `__builtin_bswap`.

**What moved is what a command line can be written out of.** Fourteen new system
cases bring that suite to 66; the kernel imports nothing new and grew one
operation and one export argument, and everything else in the release is a
program, a shell, or a library beside the system.
## The 5 KB of float a port that formats none no longer pays

TODO.md's P5, the last entry in section P and the last in the file.
`snprintf`'s float conversions are droppable: `braam_add_program(... PORT
NOFLOAT)` leaves musl's `fmtfp` out, and `zip`, `zipnote`, `zipsplit`,
`zipcloak`, `em` and `iconv` each shed about 5,090 bytes. `kernel.wasm`, every
`src/cmd/` binary and `rootfs.zip` are byte-identical: nothing in this tree
links the kit.

### Two archives, because a weak reference pulls nothing

The entry named the trap it had to avoid. The obvious split — the float arm in
a translation unit of its own, reached through a weak reference — **does not
work**: a weak undefined reference does not pull an archive member, so the
symbol resolves to null and `%f` prints nothing at all. A wrong answer is worse
than 5 KB, and that is why the saving had been left on the table for a release.

What works is two archives and a **strong** reference. `compat/cfmt.h` declares
one function, `compat_fmt_f64`; `cfmt.cpp` calls it unconditionally;
`braam_compat_float` defines it as musl's engine and `braam_compat_nofloat`
defines it as a trap. `braam_add_program` puts exactly one on the link line.
Nothing is weak, nothing is conditional at run time, and a program that somehow
names neither archive is an **undefined symbol at link**, which is the safest of
the three failures available.

The default is the engine, so no existing port changed and none had to be
re-examined. `NOFLOAT` is the thing you ask for, and asking for it wrongly is
loud: the stub calls `panic` with a message naming the flag. That is the same
answer the kit already gives `exit()` and `abort()` — a trap is a report, and
silence is not.

`NOFLOAT` without `PORT` is a configure-time error rather than a no-op.

### What it costs, and who it does not help

The split costs **80 bytes** in a binary that keeps the arm: one call that used
to be inlined into the format loop and is not any more. `examples/portio` is
75,101 → 75,181, and 70,075 with `NOFLOAT`.

| | float | NOFLOAT | |
| --- | --- | --- | --- |
| `zip` | 418,542 | 413,452 | −5,090 |
| `zipnote` | 196,147 | 191,049 | −5,098 |
| `zipsplit` | 201,850 | 196,752 | −5,098 |
| `zipcloak` | 224,688 | 219,590 | −5,098 |
| `em` | 290,881 | 285,790 | −5,091 |
| `iconv` | 219,873 | 214,783 | −5,090 |

Which ports could take it was decided by reading every format string literal in
`braam-apps`, not by guessing: `le` formats `%.17g` and `%.40lg` in its
calculator, `duremark` prints five `%.1f` and `%.2f` in its report, and
`simbesm` has a `%#.2g`, so those three keep the arm. `vi` and `ex` are the
interesting negative — they are **byte-identical with the flag and without**,
because `ex_out.cpp` is a `printf` of their own and they never reach `cfmt.cpp`
at all, so the arm was never in them. They do not carry the flag: a flag that
buys nothing and can still trap is worse than no flag.

`uemacs`'s README had been carrying the complaint since Group A landed — "very
nearly all of it is the kit's float conversions, which are one function with the
integer ones, so a port pays for them even where — as here — it formats nothing
but integers and strings". That sentence is what P5 existed to delete.

### The suite is a float target

`tests.wasm` links `braam_compat_pure`, and `cfmt.cpp` is in it, so the suite
needs an answer for `compat_fmt_f64` like any other program. It links
`braam_compat_float`, which is right on its own terms: `test_compat.cpp`
asserts four `%f` conversions.

Nothing in this tree links `braam_compat_nofloat`, so it is compile-checked
here and link-checked by the three ports that name it. That is the same shape
Group B has — `examples/portio` links it, `vi` runs it.

### Why the syscall table is not the gap

This was TODO.md's standing answer to a question that would otherwise be
re-derived, and it moves here rather than going with the file.

The table was measured against the generic Unix syscall set. Forty-nine
operations answer it, and what is left divides four ways:

- **Covered by another mechanism.** `fork` (the spawn model), `dup2` (`Spawn`
  takes an explicit `fd0/fd1/fd2` triple and *moves* them), `fcntl` (no
  `O_NONBLOCK` — everything is a coroutine; no `FD_CLOEXEC` — a move is already
  close-on-spawn), `poll`/`select` (concurrent tasks, `PROC_TASKS` = 8, which is
  what `/bin/chat` uses), `ioctl` (`Tty` and `KeyClaim`), `getppid`, `uname`,
  `sysinfo`, `statfs` (`/proc` and `Sys::Storage` — §4.3's fourth rule),
  `alarm` (`Sleep` in a second task, which is `/bin/timeout`), `setenv`
  (argued and rejected).
- **Impossible on the host.** `utime`/`utimensat` — OPFS cannot set a
  modification time at all. `fsync` — OPFS flushes on every write.
- **Deliberate.** `bg`/`^Z` with `Wait`'s `WNOHANG`, `chmod`/`access`/`umask`,
  `link`, CPU metering, per-process root, `Kill` restricted to children. Each is
  in CLAUDE.md's known gaps with its argument in Release_Notes.md.
- **Missing, and waiting for a caller.** Nothing, now. `fstat`, `O_EXCL` and
  `mount` were all here until a caller turned up for each — `/bin/unzip`,
  `/bin/edit` and `/bin/mount` — and Release_Notes.md has the three arguments.
  `Sys::Mount` is the one of them that answers `Err(Unsupported)`: it has its
  caller and not its filesystem, and §5.4 carries what that still wants.

The twelve missing *programs* were checked against the table one by one. None
was blocked on a syscall, and sections A, B, D and P are all spent: the program
layer is neither short of coverage nor known to be wrong, nor short of the
buffered stream D asked whether it wanted, nor of the port kit P built. What
adding a program costs is [Testing.md](../Testing.md) §6.

### Section P is spent, and so is TODO.md

P1 completed Group A, P2 added Group B, P3 and P4 migrated all nine ports onto
them, and P5 made the one part of the kit that could not be avoided avoidable.
With A, B and D already spent, TODO.md had no entries left in it at all — so
**the file is deleted**. It existed to say what was left and in what order, and
there is nothing left; an empty sequence is not a document. Its one piece of
standing reasoning is the section above. A future plan starts a new file, or a
new section of this one.

## Every port stops keeping a C library, and the `long` that could not reach a file

TODO.md's P4, and with it the port kit's whole reason for existing is
discharged: `zip`, `uemacs`, `le` and `iconv` are migrated, no package in
`braam-apps` keeps a stream layer or a header set of its own any more, and
section P has only P5 left. In this tree the change is `b_fseeko`/`b_ftello`,
one line of `<stdio.h>`, `examples/portio` and three documents. `kernel.wasm`,
every `src/cmd/` binary and `rootfs.zip` are byte-identical.

### The kit was truncating every file past 2 GiB, and `zip` is who noticed

`<stdio.h>` declared `fseeko` and `ftello` and pointed both diagnostics at
`b_fseek` and `b_ftell`. `long` is **32 bits** on this target and `off_t` is 64,
so a port that took the advice got its offsets truncated silently — and zip64 is
precisely the feature that exists because a `long` cannot address the file.

So `b_fseeko(FILE *, off_t, int)` and `b_ftello(FILE *) -> off_t` are the bodies
now, `b_fseek` and `b_ftell` are wrappers over them, and `b_ftell` answers -1
with `EOVERFLOW` where the position will not fit — which is what C says it must
do and what the kit could not previously say at all. `File::seek` already took
an `i64` and answered a `u64`; nothing under the kit had to change.

This is the fourth rule of §4.3 read from the other end: the operation existed,
the caller turned up, and the caller found the signature wrong.

### `zip`: 800 call sites and one thing the kit will not answer

`braam.h` was 217 lines and `braam.cpp` 465. What is left is 76 and 133: the two
date formats `sscanf` would have read, the DOS stamp, the argv copy, and a read
a `^C` has to reach. Everything else went to the kit at about eight hundred call
sites — `zfprintf` → `b_fprintf` at 282 of them, `zsprintf` → the kit's
`sprintf` at 81, `zfflush` at 97, `zfseeko`/`zftello` at 84, and the rest by
name. `zvformat`, sixty lines of format engine that ignored precision, is gone;
so is `ZFMT_MAX`, the 5,120-byte file-scope format buffer two of `PROC_TASKS`'
eight tasks could have collided over.

Three things the migration taught, none of them mechanical:

- **`b_fprintf` carries `__attribute__((format(printf, 2, 3)))`, and `zvformat`
  did not.** Two call sites printed a table of help lines *as* format strings —
  `b_fprintf(stdout, text[i])` — which a `%` in any future line would have made
  a bug. They are `b_fputs` now, which is what they always meant.
- **`stdout` is a macro**, so `File::stdout()` became `File::b_stdout()()`. Four
  ports hit this; the answer is that a `PORT` binary uses the kit's three
  streams and not `proc/file.h`'s.
- **`time()` is `unavailable` in the kit and names `clock_now()`**, which
  blocks. zip wants the cheap non-blocking approximation — the wall clock read
  once at startup plus `proc_now()` — so its own is `ztime()`, and the rename is
  the whole of that argument.

`zip` also carried D3's breakage: `File::getline` is an awaiter now, and five
open-coded `fgets` blocks in `fileio.cpp` and `zipfile.cpp` still assigned it to
a `Task`. Each was thirteen lines that read a `String`, measured it and
`memcpy`'d it into a `char *`; each is now one `co_await b_fgets(buf, N,
stdin)`, which is what upstream wrote.

### `uemacs`: the split that existed for a rule the kit already obeys

`fileio.cpp` carried a descriptor, a 4 KB buffer, two counts, an end-of-file
flag, an error flag, a direction flag, a drain and a refill — 299 lines for one
global stream. It is 165 now, over one `FILE *`.

The interesting half is what *unsplit*. Its reader was deliberately in two
pieces: a plain `fgetbyte()` over the buffer and an awaited `frefill()`, because
a `co_await` is a call and not a tail call here, so awaiting once per byte grew
the native stack by the length of the file. That is the same discovery §3.3
records — and it is exactly why `b_fgetc` is an **awaiter and not a `Task`**. So
the split is gone and the loop awaits a byte at a time again, which is
upstream's own shape. A port's hard-won workaround being deleted by the kit that learned
from it is the best evidence the kit is the right shape.

It costs 17,765 bytes, 273,036 to 290,801: `proc/file.h`'s buffered stream,
which a hand-written 4 KB buffer and a `memcpy` was avoiding.

### `le`: four files, and two places that were reading `errno` wrong

`lewchar.h`, `lewchar.cpp`, the vendored `wcwidth.c`, `leio.h`, `leio.cpp`,
`lefile.h`, `lefile.cpp` and `braam.cpp` — 1,079 lines and eight files — are
gone for `<wchar.h>`, `<wctype.h>`, `<fnmatch.h>`, `<fcntl.h>`, `<sys/stat.h>`,
`<unistd.h>` and `compat/cio.h`. `lesys.h` kept its POSIX typedefs, its own
`struct stat`, the whole `S_IF*` set and an errno enum; it is 35 lines and one
of them is `LE_PATHMAX`. 535,579 bytes against 531,887, so the whole of it cost
3,692.

Two seams are worth writing down, and both are now in Compat.md §7.

**The config parsers reach the `File` inside the kit's `FILE`.** `le` reads its
syntax files, keymaps, colour schemes and history with `kernel/text.h`'s
scanners — `scan_i64`, `scan_lit`, `scan_token`, `scan_until` — because §3
declines to supply `sscanf`. Those are `File`'s and have no stdio name, so the
call is `f->at->scan_i64()`. That is not a wart: `b_fgetc` reads through the
same `File`, and `File::unget` is pushback both halves see, where `b_ungetc`'s
own slot would be visible to only one of them. A parser that ungets a byte and
then scans a number — `highli.cpp` does exactly that — needs them to be the same
buffer.

**`le_errno` is gone, and it was doing two jobs.** One was holding a kernel
`Error` to be read back through `error_name()`, which is now
`error_name(error_of(errno))` over `compat/cerr.h`, as `vi`'s `syserror()` does.
The other was `errno = 0; op(); if (errno)` meaning *did that fail* — safe while
only `le`'s own six wrappers ever wrote it, and wrong the moment `errno` is C's,
because C only promises `errno` is meaningful after a call that **failed**. A
save that ends by stat-ing a file that is not there leaves `ENOENT` behind and
succeeds. Two sites had to start reading the operation's own status instead: the
autosave in `cmd()`, which now tests `SaveFile`'s return, and the keymap load in
`keymapfn.cpp`, which now tests `b_ferror` on the stream it just read. `le`'s
`lespawn` test caught the first — the shell escape stopped happening because the
editor believed it could not save.

### `iconv`: the copies that were shadowing the kit silently

`braam.h` carried 145 lines of `sys/queue.h` and 24 of the wide half, and
`braam.cpp` a `mbrtowc` and a `wcrtomb`. Deleting the *declarations* changed
nothing measurable — 219,556 bytes before and after — because `braam.cpp` is
compiled straight in and its definitions win over an archive member, so the
kit's were never pulled. That is §7's rule demonstrating itself: which one a
call reaches is silent, and here the silence was total. With the definitions
gone too it is 219,793, and the 237 bytes are the difference §3 describes —
`mbrtowc` re-encodes what `utf8_decode` handed back and compares bytes, so a
malformed sequence is `EILSEQ` where this port's own let U+FFFD through as a
successful conversion.

`PATH_MAX` at 256 and `LINE_MAX` at 256 **stay**, against the kit's 512 and
2048, and the settlement is written beside them. They are frame-budget numbers,
not filesystem ones: citrus builds paths in coroutine locals — three in
`_citrus_esdb_open`, four in `_citrus_csmapper_open` — and the longest path the
library ever builds is 81 bytes. The kit's archive is compiled against 512, so
the divergence is silent by construction, and what makes it safe is a rule
rather than a coincidence: no kit function is ever handed one of these buffers
with an implied size. Every call passes the length — `strlcpy`, `snprintf`,
`_lookup_alias` — and there is no `getcwd` or `realpath` in this port.

### What it cost

| | before | after |
| --- | --- | --- |
| `zip` | 420,903 | 418,462 |
| `zipnote` | 193,294 | 196,067 |
| `zipsplit` | 199,055 | 201,770 |
| `zipcloak` | 221,231 | 224,608 |
| `em` | 273,036 | 290,801 |
| `le` | 531,887 | 535,579 |
| `iconv` | 219,556 | 219,793 |
| `portio` | 73,594 | 75,101 |

3,196 lines deleted against 1,365 written, and eight files gone. `zip` is the
one that *shrank*: `zvformat` and the `z*` layer were larger than the archive
members that replaced them, and the three tools grew because each now links the
`FILE` family whole where it used to link only the part it reached. `portio`'s
1,507 is the two new calls in the example itself, not the kit.

All forty-five `braam-apps` tests pass, `convert.mjs` skipping as it always does
for want of a reference corpus.

## The two smallest shims, and the 10,697 bytes `duremark` did not spend

TODO.md's P3, in `braam-apps`: `benchmarks/duremark` and
`games/adventure`, the two smallest porting layers, against the kit now that P1
and P2 have finished it. Nothing in this tree changes but three documents —
`kernel.wasm`, every `src/cmd/` binary and `rootfs.zip` are byte-identical,
because nothing here links the kit.

### Half of it had already happened, and the entry did not say so

`PORT` went on both packages on 2026-08-29, when the kit was Group A minus its
remainder and Group B did not exist. Those two commits recorded 23,508 → 25,894
for `duremark`, whose hand-written formatter became the kit's `vsnprintf`, and
164,354 → 167,538 for `adventure`, which was asking the C library for `strlen`
and `atoi` and nothing else. P4's entry was rewritten by P1 and P2 to say what
each remaining package still has to delete; P3's was not, so what it was still
owed was never written down.

It is this: the calendar, and a decision about Group B in each. Both are small,
and that is the finding rather than a shortfall — `D2a` and `D3` ended the same
way.

### `adventure` gives up its calendar, and pays 431 bytes for it

`datime()` answers upstream's "days since 1977" and needs a day of the year,
which `civil()` does not carry. So the port had a `CUM_DAYS[12]` table and a
leap-year test over it. That is exactly the arithmetic P1 put in the kit:
`gmtime_r` fills `tm_yday`, and the table and the test are gone.

It costs **+431 bytes**, 167,734 → 168,165, because `gmtime_r` computes the
whole `struct tm` where twelve constants and one `if` computed one field. Taken
anyway: a port keeping a private copy of a calendar is what Compat.md §7 is
about, and a hand-cut leap year is the kind of thing that is wrong in 2100 and
right in every test that runs before it. `clock_now()` still supplies the epoch
and the zone — `localtime` is `BRAAM_ABSENT` and names that path — so
`adv_clock()` is untouched.

`adventure` also stopped building against this branch, which is D3's fallout
and not P3's: `LineReader::next` is an awaiter now, so
`if (Task<Result<bool>> t = lines->next(out))` no longer converts. It is
`co_await lines->next(out)`, as `sh`, `less`, `cp`, `mv` and `chat` already
write it. `archivers/zip` has the same three call sites over `File::getline`
and does **not** build; that is P4's commit to fix, and it is recorded here so
the next person does not think they broke it.

### Group B is declined in both, for two different reasons

`adventure`'s six file syscalls — `save.cpp`'s four, `main.cpp`'s
`remove_path`, `wizard.cpp`'s `stat_of` — were upstream's `open`, `write`,
`close`, `read`, `unlink` and `access`, and Group B has a name for each. It
keeps `proc/io.h` regardless. The `b_*` family spells a path `const char *`,
this port carries every path as a `Str`, and `String` has no `c_str()`: each
site would gain a NUL-terminated `char[PATH_MAX]` that §3 says belongs in a
heap block rather than a coroutine frame. That is a worse shape than
`open_at(Str)`, not a better one. `vi` took Group B because `vi` is still C and
its paths are already `char *` — the rule is what the port already is, not
which layer is newer.

`duremark`'s is a number. `du_printf` could be upstream's literal
`#define du_printf printf` again, as `co_await b_printf(...)`, with `out_buf`,
`du_flush` and `write_status` deleted. That binary was built, measured and
thrown away: **36,591 bytes against 25,894, +10,697**, or 41%, for a report of
eleven lines written once at the end of a run. §5's arms put `b_printf` at
+20,199 over an empty `PORT` program and this is half that, because `duremark`
already pays for the `snprintf` engine — what it would be buying is the `FILE`
and `proc/file.h`'s buffered stream underneath it. On a benchmark whose entire
C library surface is `printf` and `clock`, and whose README is about its size,
the buffered pair stays.

### Both numbers

| | before | after |
| --- | --- | --- |
| `duremark` | 25,894 | 25,894 |
| `adventure` | 167,734 | 168,165 |

The baselines are this branch and `PROC_ABI` 20, not the figures in the
2026-08-29 commits: `adventure` was 167,538 there and rebuilt to 167,734 for the
ABI. `duremark` does not move, and the entry's own claim that it is "the honest
size worst case" is what the 41% above measures — not what it kept.

## Group B, and the header that will not include `<stdio.h>`

TODO.md's P2, and with it the port kit is complete: streams,
descriptors, directories and `struct stat`, as `compat/cio.h` and the `b_*`
family. No operation, no `PROC_ABI`, no Concept.md amendment. `kernel.wasm`,
every binary and `rootfs.zip` are byte-identical, because nothing in the tree
links the kit.

### What was already written down, and not written

The tree had been advertising this for a release. `src/compat/include/stdio.h`
declared twenty blocking names `unavailable`, each naming a `b_*` replacement
"from `compat/cio.h`" — and there was no `compat/cio.h`, no `b_*` anywhere, and
`FILE` was `typedef struct CompatFile FILE;` with `CompatFile` defined nowhere.
`compat/cerr.h` had been written *for* this task: its comment names "the `b_*`
read family" and documents `fail_with` as "what most of Group B returns", and
it had no caller. `<sys/stat.h>`, `<dirent.h>`, `<unistd.h>` and `<fcntl.h>`
did not exist at all, so a port reaching for `open` or `readdir` met "file not
found" rather than a diagnostic naming the fix.

That is P1's own complaint one level up — a name the compiler accepts and the
linker refuses — with the difference that here the message was right and the
destination was missing.

### `b_fopen` returns a `FILE *`

`stdio.h`'s diagnostic said `b_fopen(&f, path, mode)`: an out-parameter and a
status. It has been changed to `b_fopen(path, mode)`, null on failure with
`errno` set, because §2's own rule is that C's error conventions are kept *so
the surrounding code does not change* — and

```c
    if ((f = co_await b_fopen(p, "r")) == NULL)
```

is upstream's line with three words in front of it, where the out-parameter
form is a rewrite at every call site. `le` set an errno on a failed open and
`zip` dropped the reason on the floor; the kit takes `le`'s answer, since
"could not open it" and "could not open it because it is a directory" are
different diagnostics and only one of them is worth printing.

### A byte is an awaiter, and its buffer belongs to the stream

`b_fgetc` and `b_fputc` are awaiters over `FileRead` and `FileWrite`, not
`Task`s. §3.3's rule is the reason: a `Task` that answers without suspending
resumes its awaiter on the awaiter's own stack, so a loop calling one per byte
never reaches the trampoline and the shadow stack grows a frame a byte. `zip`
found this and wrote `Zfgetc`/`Zfputc` for it.

It also found the flaw the kit does not keep. `File::get` and `File::put` are
*runes*, so a byte-level `fgetc` cannot forward to them and needs a one-byte
buffer whose address outlives the call; `zip` parked that byte in a file-scope
global and documented it as safe because the write is awaited before anything
else runs. That holds for one stream and not for two, and `PROC_TASKS` is 8.
Here the byte is a field of the `FILE`, which costs nothing and is reentrant.

`b_ungetc` gets a slot of its own in the `FILE` rather than calling
`File::unget`, which encodes a rune: pushing back byte 0xC3 through it would
put back two bytes. One byte is all C promises, and `b_fgetc`, `b_fgets` and
`b_fread` all consult it.

### `printf` cannot be a coroutine

A variadic function may not be a coroutine, and the reason survives the
language rule: the caller's arguments live in the caller's frame, which is gone
by the time a suspended body would read them. So `b_printf`, `b_fprintf` and
`b_vfprintf` are ordinary functions that format into a heap block *before*
returning, and hand back a `Task` that only writes it and frees it. There is no
shared format buffer — `zip`'s was 5,120 bytes at file scope — so two of a
process's eight tasks printing at once cannot interleave into one another.

### What a `struct stat` can honestly say

There are no permissions, no owners, no hard links and no devices here, so most
of the struct is furniture. `st_mode` is a kind plus a plausible constant and
answers `S_ISDIR`, `S_ISLNK` and `S_ISREG`; `st_dev` is 1, `st_nlink` 1,
`st_uid` and `st_gid` 0, and `st_atime` and `st_ctime` are `st_mtime`. Each
constant is written beside its field so a port reads what it is getting.

`st_ino` is the exception, and it is `le`'s discovery: it is **a hash of the
path**. `le` compares device and inode to decide whether a file changed under
the editor, and a constant inode makes every file look like every other, which
fires the warning on the first save. `b_fstat` has no path and so answers 0 —
a descriptor has no identity to give.

`vi`'s `ex_file.cpp` had already argued the other half of this: upstream used
the mode bits to warn about writing over a file it had not read, and the device
to keep a temp file on one filesystem, and "neither question has an answer
here". Group B does not tempt either back.

### `readdir` does not block, and says so

`Sys::List` answers with the whole listing, so `b_opendir` performs the syscall
and `b_readdir`, `b_closedir`, `b_rewinddir`, `b_telldir` and `b_seekdir` walk
a `Vec<DirEntry>` already in hand. The same is true of `b_feof`, `b_ferror`,
`b_clearerr`, `b_ungetc`, `b_fileno` and `b_setvbuf`, which the buffer answers.

Calling those without a `co_await` is not something to leave a porter to
discover, so `<sys/cdefs.h>` — one copy of the three diagnostics, in BSD's own
name for the file, rather than a copy per header — grew `BRAAM_RENAMED` beside
`BRAAM_BLOCKS` and `BRAAM_ABSENT`. "Renamed in the port kit: `b_readdir(d)`"
where `BRAAM_BLOCKS` would have said "blocking: `co_await` …" and been wrong.

### `compat/cio.h` does not include `<stdio.h>`

This is the rule the migration found, and it is `vi`'s. `vi` declares its own
`printf`, `putchar` and `getchar` — 104 call sites — and `#define`s `BUFSIZ` to
1024, which `ex_buf.h`'s block packing needs (`OFFBTS 10` degenerates to
identity only against 1024). Its output **cannot** block: `ex_out.cpp`
accumulates into one growable heap buffer that a single `exflush()` drains,
because upstream wrote to fd 1 from plain functions six frames down.

So a port may reasonably own the stdio names it does not want from the kit, and
`cio.h` pulling `<stdio.h>` in behind `<sys/stat.h>` would make Group B and
those names mutually exclusive. `cio.h` declares `FILE` itself — the identical
`typedef`, which is legal repeated — guards `EOF`, and leaves `BUFSIZ` alone.
`examples/portio` includes both headers and is what keeps that working.

`stdin`, `stdout` and `stderr` are macros over `b_stdin()` and its two
siblings, because the streams behind them are built on first use;
`<stdio.h>`'s declaration of the three is guarded so that either include order
works.

### `braam_compat_proc` stops being `getenv`'s odd corner

Group B blocks, so it cannot go in `braam_compat_pure` — that archive links
into `tests.wasm`, where a reference to `braam_proc` is a link error, and that
is the whole point of the split. It goes in `braam_compat_proc`, which already
linked `braam_proc` for `getenv`'s four lines and is already installed and
aliased, so no top-level `CMakeLists.txt`, `braamConfig.cmake.in` or install
list moves. The archive stops being "Group A's one impure member" and becomes
what its comment always described: the half that reaches `braam_proc`.

`cmode.cpp` is the counterweight. Group B's three decisions that perform no
syscall — the `fopen` mode string, `SYS_KIND_*` to an `st_mode`, and the path
hash — are pure, so they are in `braam_compat_pure` and `test_compat.cpp`
covers them. The mode string is where the bugs are: `"w+"` must keep its
truncation and `"a+"` its positioning, `"rb+"` and `"r+b"` are the same thing,
and `"x"` alone is not a mode at all and must answer `EINVAL` rather than
silently open for reading.

### Verified by a second example, not a system case

The in-wasm suite cannot run a program and the system suite needs one in
`rootfs/`; examples are not staged there, and making one a system program would
cost an `/etc/help` line, `test_zip.cpp`'s file count and `subst.mjs`'s three.
So the blocking half is verified by linking: `examples/portio` names all of it,
`--allow-undefined` is absent, and an unresolved symbol is a build failure.
`vi` running is the rest.

`portio` is a second example rather than an arm on `portlet` because §5
publishes `portlet` at 10,491 bytes for a named Group A set; it is still
exactly that.

### What it costs, and the 30 KB that is not the kit's

`portio` is 73,594 bytes, and §5 has the arms. The `FILE` family alone is
+30,817 over a `PORT` program that does nothing, which is larger than it looks
like it should be: `File::fill_` reaches `Input::read`, which reaches `errln`,
so opening one stream carries the multi-file reader whether or not the port
names one. That is `proc/file.h`'s shape and predates this; it is recorded here
because a port reading §5 will otherwise go looking for it in Group B.

`b_printf` is +20,199, of which the `snprintf` engine is most — including the
float arm P5 exists to make droppable. A port with both `snprintf` and
`b_printf` pays for the engine once.

### `vi` deleted its shim

`vi` was already fully coroutine-plumbed — 299 `co_await`s — with a private
146-line `ex_file.cpp` underneath. That file, the `struct exstat` in `ex.h` and
the `ex_errno` beside them are gone, replaced by `b_open`, `b_creat`, `b_close`,
`b_read`, `b_write`, `b_lseek`, `b_stat`, `b_fstat`, `b_unlink` and `b_chdir` at
thirty call sites, and by `struct stat` and `S_ISDIR` where two hand-cut fields
were. The `read_some` → `String` → `memcpy` that `ex_file.cpp` apologised for is
gone with it: `b_read` fills `genbuf` directly.

`syserror()` is the one place the migration did **not** take the kit's answer,
and it is worth writing down. `strerror` returns `"ENOENT"`; `error_name()`, in
`kernel/result.h`, returns `"not found"`, which is what every other program on
this system prints and what twenty-two of `vi`'s pinned messages say. §3 of
Compat.md had claimed the two mirrored each other — they do not, and the claim
is now corrected. So `syserror()` reads `error_name(error_of(errno))`, over
`compat/cerr.h`'s bridge in the direction it had not been used in yet. The kit
supplies the errno; which vocabulary a diagnostic speaks stays the port's
choice.

`braam-apps/CLAUDE.md` said "the `b_*` replacements are **not in this SDK** —
every port keeps the stream layer it wrote"; that is what this entry falsifies,
and `zip`, `le` and `uemacs` are the three still to go.

## Group A's remainder, and the six names that were a link error

TODO.md's P1, and with it the port kit's Group A is complete: the
calendar, the wide half, `fnmatch`, `sys/queue.h` and the `strtod` family. No
operation, no `PROC_ABI`, no Concept.md amendment — the kit is an archive
nothing links unless it names it, and `src/cmd/` still builds with
`#include <time.h>` unresolved.

### The hole that was a link error, not a diagnostic

`strtod`, `strtof`, `atof`, `sscanf` and `vsscanf` were **declared** in the
kit's headers and defined nowhere. That is the worst of both: a port writes the
call, the compiler accepts it, and the linker fails at the end naming a symbol
rather than a line. The kit's best idea is the `unavailable` attribute on
`<stdio.h>`'s blocking names, and it was not being used on the names that were
simply absent.

So the five split, on whether there is a caller. `strtod` has one — `simbesm`
carries a `sim_strtod` over `scan_f64` precisely because the real name would
not link — so it is implemented. `sscanf` has none, and the tree already
carries the argument against it: `zip/braam.h` says "a general one would be the
rest of stdio for two call sites", and fifteen former `sscanf` sites across
`le` and `zip` are already rewritten against `kernel/text.h`'s `scan_i64`,
`scan_u64`, `scan_token` and `scan_until` — one function per conversion, where
a format string defeats every check the compiler could make. So `sscanf` and
`vsscanf` become `unavailable` under a second macro, `BRAAM_ABSENT`, whose
message says "not in the port kit" rather than "blocking" and names the
replacement. `<time.h>` uses the same macro for four more.

### `strtod` is its own translation unit, and `scan_f64` had to grow an `err`

`scan_f64` was already strtod's shape — `used` is the endptr, `used == 0` is
"no conversion" — but it *discarded* `Cur::err`, so an overflow was
indistinguishable from an ordinary answer and `strtod` could not set `ERANGE`.
It gains an optional `i32 *err`, which is what the C signature needs. A
`scan_f32` comes with it: `strtof` as a narrowed `strtod` rounds the decimal
twice, and `__floatscan` already takes the precision as an argument.

`cstrtod.cpp` is separate from `cstrtox.cpp` because musl's `__floatscan` is
6,898 bytes — the single largest thing P1 adds — and `--gc-sections` works at
function granularity but archive extraction works at member granularity. A port
that names only `strtol` must not pay for it, and one translation unit is the
whole of the fix. This is P5's problem in a case where it happens to be easy:
the split works here because `strtol` never calls `strtod`, where `printf`'s
`%d` and `%f` really are one function.

### The calendar is `timegm` plus `tm_gmtoff`, because a zone is a coroutine

`civil()` and `civil_secs()` already existed in `proc/time.h` and already
normalised an out-of-range field, which the header calls mktime's contract. So
`<time.h>` is mostly a re-spelling — except for the zone.

There is no local zone available to Group A. The offset comes from
`clock_now()`, which is a `Task<Result<Clock>>`, and a C signature cannot block
here. Three answers were possible: pretend local is UTC (a lie that would
silently shift every timestamp), put the whole calendar in
`braam_compat_proc` and lose the unit tests with it, or make the offset the
caller's. The third is what BSD's `struct tm` already provides: `tm_gmtoff` and
`tm_zone`. So `timegm` reads the fields as UTC, `mktime` reads them as UTC
shifted by `tm_gmtoff`, and `time`, `clock`, `localtime`, `localtime_r`,
`ctime` and `ctime_r` are `unavailable`, naming `clock_now()` and `proc_now()`.
`zip` already works this way by hand — it adds `ztz_min * 60` before calling
`civil()` — so the shape is one a caller has already arrived at.

`time_t` is 64-bit. `zip` had chosen `i64` and `le` `long`, which is 32 bits on
wasm32; the kit takes musl's, which is the dialect its `errno` numbers already
come from, and 2038 is not a date worth reintroducing.

`strftime` carries the whole C99 set — every conversion including `%G`, `%V`
and `%U` — where the only caller in the tree wants seven of them. A partial C
signature that drops a conversion silently is exactly the wrong answer this
document refuses for P5's float arm, and the ISO-week arithmetic is twenty
lines. `civil_secs` supplies `tm_yday` by subtraction rather than a table.

`mktime`'s normalisation cannot go straight through `civil_secs`: `Civil`'s
fields are unsigned, so a negative `tm_mday` or `tm_sec` would wrap. Only the
month carry is delicate, and `civil_secs` already does that; the rest is plain
signed arithmetic in `epoch_of`, which is how `tm_sec = -1` on the first of a
month lands on the last second of the month before.

### Two `mbstate_t`s, and the one that can hold half a character

`iconv` and `le` both wrote a wide half and they are not the same shape.
`le`'s `mbstate_t` is `{ int dummy; }` with a comment saying UTF-8 has no shift
state, which is true and beside the point: the state is not for a shift, it is
for a *sequence that straddled a call*. `citrus_iconv_std.cpp` keeps one per
conversion in `sc_mbstate` and feeds it a batch at a time, so `le`'s would
corrupt every four-byte character that fell across a batch boundary. The kit
takes `iconv`'s, `{ unsigned char buf[4]; unsigned char len; }`, and `le` —
which resets before every decode and detects a split by arithmetic on its own
buffer — is unaffected by having a real one underneath.

The correctness gain is elsewhere. `utf8_decode` answers U+FFFD for every
malformed form, which is right for a screen — bad input is visible rather than
dropped — and wrong for a codec, where C requires `EILSEQ`. Both hand-written
copies inherited that: `iconv`'s cannot tell a bad byte from a real U+FFFD at
all, and `le`'s guesses with `used == 3 && s[0] == 0xEF`. The kit re-encodes
what came back and compares bytes, which is a general rule rather than a
special case: a genuine U+FFFD round-trips and a malformed sequence does not.
So `mbrtowc("\xff\xfe")` is `(size_t)-1` with `EILSEQ`, and
`mbtowc("\xef\xbf\xbd")` is 3 with the rune.

### `wcwidth` is Kuhn's, and the grid disagrees with it

This is the one place the kit answers for C rather than for this system, and it
is worth saying plainly. Markus Kuhn's tables give 2 columns to East Asian Wide
and Fullwidth; `kernel/screen.h`'s grid is one `Cell{char32_t ch}` per rune and
has no notion of a wide one. A port doing column arithmetic with `wcwidth` will
therefore disagree with the screen about a CJK character.

The alternative — a `wcwidth` that returns 1 for every printable rune, agreeing
with the grid — was considered and rejected: it would make the kit's `wcwidth`
mean something no other `wcwidth` means, and `le` would then have to keep its
own to get the answer it wants. Shipping the real one puts the disagreement
where it belongs, in the grid, and makes it a screen question rather than a
libc one. Nothing in P1 changes the grid.

`cwidth.cpp` is therefore the second piece of vendored third-party data in the
tree, after `src/math/`, and CLAUDE.md's sentence about that is amended. The
East Asian Ambiguous variant, which Kuhn ships beside it, has no caller and is
not here.

`<wctype.h>` is honest about a smaller thing: its classes are `rune_lower` and
`rune_upper`'s coverage — ASCII, Latin-1, Latin Extended-A, Greek, Cyrillic —
plus a short table of the letter blocks that have no case at all, which is what
`iswalpha` cannot find by asking for a case mapping. That is not full Unicode
and the header says so rather than implying otherwise.

### `fnmatch` is not `sh`'s matcher, and it honours the backslash

`src/cmd/sh/match.cpp` has a glob matcher already, and it cannot be reused:
it is `braam_sh`'s, `braam_sh` links `braam_proc`, and `src/cmd/` linking the
port kit is the thing the kit exists not to do. The two also want different
interfaces — `glob_match` takes the expander's quoting mask, one byte per byte
of the pattern, where `fnmatch` takes flags and reads a backslash out of the
pattern itself. So the kit gets its own, iteratively backtracking on `*` the
way `le`'s copy already does, so a pattern of stars cannot reach the shadow
stack.

It carries the whole flag set — `FNM_PATHNAME`, `FNM_NOESCAPE`, `FNM_PERIOD`,
`FNM_CASEFOLD`, `FNM_LEADING_DIR` — where `le` passes 0 at both its call sites,
and the POSIX character classes with it. `[[:digit:]]` read as an ordinary
bracket expression matches `[`, `:`, `d`, `i`, `g` and `t`, which is a silently
wrong answer; the table of twelve costs the twelve `ctype` predicates, since
`--gc-sections` cannot see through a table of function pointers, and that is
most of `fnmatch`'s 1,957 bytes.

`le`'s copy ignored the backslash, and C with flags 0 does not. That is a
behaviour change P4 inherits at `highli.cpp:311`, where a syntax file's pattern
is matched against a basename.

### `sys/queue.h` is macros, so the cost is the caller's

FreeBSD's `SLIST`, `LIST`, `STAILQ` and `TAILQ` with their `_SAFE` variants;
no `CIRCLEQ`, which the BSDs themselves dropped, and no debugging hooks.
`iconv` is the one caller and uses three of the four. `TAILQ_REMOVE_HEAD` is
Citrus's own name rather than FreeBSD's and stays with it.

### What it cost, and what the suite pins

Nothing in `rootfs/` links the kit, so `kernel.wasm` and the boot archive are
byte-for-byte what they were. Over a program that does nothing else, at 7,353
bytes: `sys/queue.h` +107 (which is the caller's own loop), `fnmatch` +1,957,
the wide half +3,463, the calendar +4,337, `strtod` +6,898.

`test_compat` gains five cases. The one worth naming is `test_wide`, which
feeds `mbrtowc` a four-byte sequence **one byte per call** and checks
`(size_t)-2` three times and then the rune — the case `le`'s `mbstate_t` cannot
represent, and the reason the kit has `iconv`'s. `test_time` pins every
`strftime` conversion against 2009-02-13 23:31:30 UTC and the four
normalisations, including the two with a negative field that `civil_secs`
alone would have wrapped.

---

## The line reader a `File` would only make bigger

TODO.md's D3. `File::getline` replaced the `LineReader` in `grep` and
`tail`, and `cp`, `mv`, `less`, `chat` and `sh -s` still hold one, so the entry
asked for the type gone and one line reader left. It was measured on the best of
the five and it does not pay. D3 joins D2, D2a and D4 — and it was the last of
them, so section D leaves TODO.md with all four of its answers here, and the
port kit, P, is the whole of what is left.

**The measurement.** `/bin/less` is the cleanest case in the set — its
`LineReader` is a pure slurp of eight lines, so the diff is the include, the
type and the method name and nothing else, and it already links `tty_of`.
Converted, `less.wasm` went 33,896 → **40,155 bytes, 19%**, and the staging tree
1,748,289 → 1,754,548 against the 2,097,152 in `tools/size_budget.txt`.
Accounting the wasm name sections beforehand predicted 4,710, so the symbol
arithmetic understated the real figure by a third; the other four are no
cheaper, and `cp`, `mv` and `chat` would each add `tty_of` on top. Call it 31 KB
across the five, and `chat` — 21,052 bytes today — would carry the worst of it.

**What that buys is 129 lines of source and not one byte of any binary.**
`LineReader` and `LineNext` are 43 lines of `proc/io.h` and 86 of `proc/io.cpp`.
Deleting them saves nothing anywhere, because `--gc-sections` already has:
`LineReader|LineNext` appears 0 times in `cat.wasm` and `grep.wasm`, which use a
`File`, and 0 times in `kernel.wasm`, whose budget is the tightest in the tree.
It appears 6 times in `cp` and `mv`, 4 in `sh`, 2 in `less` and `chat` — the
only places it costs anything, and there it is 1,202 bytes against the 5,912 of
the read-only half of a `File`. The type is free where it is unused and cheaper
than its replacement where it is not.

**There is no defect under it, either.** An `Input`-backed `File` allocates no
block at all — `block_ready_` answers true immediately when `src_` is set — and
`fill_` calls `src_->read()` at exactly the point `LineReader::next_slow_` did.
The conversion is syscall-neutral and allocation-neutral by construction, and it
is in fact a copy *cheaper*: `next_slow_` memmoves its unread tail down and
appends every chunk into a second heap `String`, where `fill_` moves the chunk
and adopts it in place. That is worth knowing and it is not worth 6 KB a
program. `fullscreen.mjs`'s 65,536 lines through the pager passed unchanged
under the conversion, which is the whole behavioural difference: none.

**`head` had already settled this from the other side.** Its comment says lines
are split out of chunks there rather than read through `File::getline` because
"a `File`'s bytes buy nothing else". Generalised, that is D3's answer. The D
section's criterion is that a `File` earns its bytes where several writes
coalesce into one syscall; the reading analogue would be several reads
coalescing into one, and `Input::read()` **is already that**, one `read_chunk`
per chunk, sitting underneath both line readers. So no reader in this tree can
earn a `File` on syscall grounds, and the criterion has no subset to pick out:
it excludes all five or none.

**There are three line readers, and the one D3 would delete is the one the wasm
suite cannot reach.** Besides `proc/io.h`'s and `File`'s there is the kernel
twin in `src/user/io.h`, whose comment says it is "the reference the
process-side twin in src/proc/io.h mirrors, and test_io is what keeps it
honest". `proc/io.cpp` is not compiled into `tests.wasm`, while
`FileBuf::take_line` — the core of the reader that would stay — has
`test_filebuf.cpp` already. So the deletion takes the count from three to two
rather than two to one, and strands the third's stated reason for existing. The
duplication D3 wants gone is not the duplication that costs anything.

**The two recorded constraints, re-checked, because both turned out narrower
than they read.** 0.7 kept `chat` off a `File` because two tasks writing one
stdout would share one `FileBuf` — that is about stdout, and `chat`'s stdin is
touched by nothing but the `LineReader`, so `chat` is convertible and is
excluded on cost alone. 0.3 recorded that `sh -s`'s reader buffers separately
from the `read` builtin on the same descriptor, which is a real fidelity loss; a
`File` leaves it exactly as it is, since both over-read a chunk neither can wind
back and `File::detach()` refuses an `Input`-backed stream outright.
`read.cpp`'s seek-back stays the only thing that gives bytes back either way.

**Two hazards recorded rather than fixed**, both latent in the `Input`-backed
`File` that already exists. `fill_`'s four-byte carry is safe today only because
`FileBuf::take_line` consumes the whole fragment it could not finish, so
`getline` always refills on an empty buffer; a caller mixing `get()`/`unget()`
with `getline()` across more than four straddling bytes would get
`Err(Invalid)`. And such a `File` leaves `fd_` at 0, which is `SYS_STDIN`, so a
`seek()` on one would seek stdin — unreachable, since nothing seeks a stream it
did not open.

**One thing came out of it, as with D2 and D2a.** Checking what a conversion
would have to preserve turned up an asymmetry: `cp` and `mv` ask on the same
`!force && ask`, and `rename.mjs` pins `mv` for a refusal, an acceptance and
`-f` overriding the ask, while `cp.mjs` pinned only the refusal, and only on the
`-r` path. `cp -i` answered `y`, and `cp -fi`, had never run. Both do now.

## The accumulator a `File` could only make longer

TODO.md's D2a. Three places build every output row into one heap
`String` and write it once — `pkg/query.cpp`'s `emit`, `unzip`'s listing, and
the sh builtins — and the entry asked whether a `File` should do that without
the allocation. One was built and measured, the other two are decided on
arguments that are not about size, and none of them moves. D2a joins D2 and D4:
recorded so it is not re-derived.

**The measurement.** `/bin/unzip`'s `list_them` was converted whole — output
byte-identical, the alignment checked — and `unzip.wasm` went 38,513 →
**48,062 bytes, 25%**. Its `co_await` sites went from 1 to 4, so most of that is
not the suspension points at all: it is the ~8.4 KB of `proc/file.cpp` and
`filebuf.cpp` that `--gc-sections` drops today, because nothing else in
`/bin/unzip` names a `File`. The staging tree went 1,748,289 → 1,757,838 against
the 2,097,152 in `tools/size_budget.txt`, 83% to 84%. What it buys is one heap
`String`, and what it costs on the listing path is one write syscall becoming
one per 512 bytes.

**The row is not a `Buf`, and that is the general result.** `df` and `ls` are
cheap on a `File` because a stack `Buf<N>` holds a whole row and the row reaches
the stream in one write — D2's finding, from the other direction. A zip entry
name is a path and a package description is a sentence, so neither fits a
`Buf<N>`: the row either splits into a write for the fixed part and a write for
the variable one, or it becomes a heap `String` *per row*, which is an
allocation per row in place of one that grows. That is why D2a's three cases
cannot have the shape that made the ten conversions pay, and it holds for all
three without building the other two.

**A finished accumulator handed to a `File` is the same syscall.**
`File::write_` reaches `write_slow_`, and `s.size() >= want_` there flushes and
calls `write_all(fd_, s)` directly. So keeping the `String` and writing it
through a stream is, for anything past the 512-byte buffer, byte-for-byte what
happens now with ~8 KB added. Only the per-row conversion changes the syscall
count, and it changes it upwards. The naive one is worse still: `File::stdout()`
is `Buffering::Auto`, `probe_` makes that `Line` on a console, and line mode
flushes on any write holding a `'\n'` — a syscall a row at a prompt, plus a
`Sys::Tty` probe to decide it. `ls` sets its buffering explicitly to dodge
exactly this.

**`/bin/pkg`'s writes are progress, not rows.** `emit` is not per-row to begin
with: all three of its call sites already accumulate the whole output and write
once, so there is no syscall there to save. The interesting question was whether
some *run* of `/bin/pkg` writes several times, which is what D's criterion asks
for — and none does. `pkg install` prints the plan, then downloads, hashes,
inflates and runs scripts, then prints `generation N, M packages`; `pkg update`
names the repository before fetching from it and reports after. Both are two
writes with the whole job between them, and coalescing them means the user
learns nothing until it is over. The flush points are the feature. Separately,
`pkg_info`'s `describe` is a pure non-coroutine chain of fifteen appends, and
converting it makes fifteen suspension points out of none — D2's `df`
measurement replayed on a worse shape.

**The shell's builtins are excluded on correctness, not size.** `PIPE_SLOTS` is
8 and counts writes rather than bytes (`user/io.h`): `pipe_write` makes one
chunk per call and `Sys::Write` is one `Stream::write` per syscall whatever the
payload, so one accumulator is **one slot at any size** while a `File` at
`FILE_BUF` is one slot per 512 bytes. `builtin.h`'s rule therefore stands, but
its stated reason — "nobody left to drain it" — is broader than the mechanism.
`job.cpp` spawns every program stage before it runs any builtin, so a *spawned*
reader does drain and `jobs | wc -l` would merely cost N syscalls for one. The
two shapes that actually hang are a builtin reader, where stage *i+1* runs only
after stage *i* returns, and command substitution, whose capture pipe is drained
after the entire builtin loop — so `x=$(set)` parks the shell inside its own
builtin past 4 KB, recoverable only by `^C`. Two smaller reasons point the same
way: `io.out` is a descriptor the builtin borrows and `job.cpp` closes as soon
as it returns, so CLAUDE.md's rule makes it `Buffering::None` — a syscall a
write; and `~File` does not flush, so every early `co_return` in `set`, `export`
or `jobs` would drop its output silently. This is the builtin half of D4's
verdict and belongs beside it.

**The bound the entry did not raise, recorded rather than acted on.** The
accumulator is unbounded, and it does have a ceiling: a payload over
`SYS_STAGE_MAX`, 1 MB, is refused by `proc_stage` and the write comes back
`Err(NoMemory)`. It does not change the verdict. In both unbounded cases the
listing is a projection of something the program already holds resident — the
whole `CheckedIndex`, the whole central directory — against a 16 MB cap, and
1 MB of `unzip -l` is some 25,000 entries. The failure is clean and reported.
If it ever did matter the answer is to write the accumulator in slices, five
lines inside `emit`, not to link a `File` for 8 KB.

**One thing came out of it, as it did from D2.** Checking that the conversion
printed the same bytes meant looking at what pins `unzip -l`, and every size in
the fixture is one digit — so `list_them`'s measured width had only ever been 1
and its padding loop had never run its body in the suite. That is the `ls -l`
gap `test/system/columns.mjs` was written for, in a second place.
`test/system/unzip.mjs` now lists an archive holding one byte and 1,234, and
pins the leading blanks; `row()` strips trailing space only, so they are
visible. A listing that ignored the width passes every other assertion in that
case and fails this one.

## The empty directory a rename would not step into

TODO.md's B4, and the divergence the section below it deferred.
`rename(2)` replaces a destination directory that is *empty* and answers
`ENOTEMPTY` for one that is not. `vfs_rename` refused both with `Err(Exists)`,
so `mv a b` with an empty `b/a` was `mv: a: already exists` where Unix moves.

**Telling the two apart is a listing, and that is the whole change.** The kinds
still have to agree, a merge is still refused, and the destination is still not
removed here: `Fs::rename`'s contract already says it replaces what it lands on,
so the VFS decides *whether* and the store does it. Which matters more than it
sounds, because no store moves a directory — `FileSystemHandle.move()` is
`getFileHandle` only — so an approved empty destination falls out of
`vfs_rename` as `Err(Unsupported)` and `/bin/mv` performs the replacement on
its copy path. Removing the destination in the VFS would destroy it for an
operation that then never happens, and mv's own `remove_path` would answer
`NotFound` over the corpse.

**`Err(NotEmpty)` rather than `Err(Exists)`.** It is `ENOTEMPTY`, the enum
already had it for `rm` without `-r`, and `error_name` already printed
"directory not empty" — so `mv: a: directory not empty` cost nothing and reads
as what happened. Nothing about the ABI moves; `NotEmpty` is 13 on both sides
already.

**The ordering is the safety property, not a preference.** The verdict has to
be reached before the cross-mount `Err(Unsupported)`, because that answer is an
*instruction* with a destructive half: the caller removes the destination and
copies over it. Move the emptiness test below `mf != mt` and a cross-mount
`mv a b` onto a populated `b/a` recursively deletes it — which is the case
`ENOTEMPTY` exists to prevent. The unit suite pins it from both mounts for that
reason.

**mv's fallback removes non-recursively now.** It was `remove_path(to, true)`,
which is the same recursive delete the section below caught in `cp`; it was
unreachable for a populated directory only because `vfs_rename` refused every
directory destination outright, and B4 is exactly what makes it reachable. `-f`
onto a populated directory is `ENOTEMPTY` in POSIX, so the recursive form was
always a latent violation. Non-recursive is identical on a file or a link, and
on a directory it now refuses whatever the VFS would have refused, which closes
the window between the listing and the removal.

**A mount point is now guarded at both ends.** The source check was there and
the destination one was not, because `Err(Exists)` masked every directory
destination. An empty mount point is empty, so without it `vfs_rename` would
ask a store to rename over its own root — `vfs_lookup` hands back `"/"` for a
path equal to the prefix. `/bin/mv` cannot reach it (a directory operand is
descended into, not replaced), which is why it was worth one line rather than
an argument.

**What it costs.** One listing, on the one path that already resolved the
destination and is about to copy a whole tree, and `vfs_list` rather than a
bespoke probe so that "what is in this directory" keeps one definition — a
mount folded under the destination has to count as a child, and two places that
must agree about that is two places one of them can be wrong in the direction of
deleting something. `vfs_rename`'s frame went 1008 to 1720 bytes and that is
free: the top size class is 512, so it was a whole span before and is a whole
span now. A helper coroutine to keep the `Vec<Entry>` out of the frame was built
and reverted for the same reason — it bought no allocation, cost one, and cost
593 bytes of kernel. The kernel is 192,517 bytes against a 262,144 budget.

**What did not change.** The replacement is still a copy, so it still restamps
— "a rename that is sometimes a copy" stays in the known gaps, and a store that
gains directory `move()` starts using it without another edit. `mv -i` still
asks before it discovers the destination is populated, as it did when the answer
was `Exists`. A destination symbolic link still resolves to `Link`, so a
directory onto one is `Err(NotDir)` and the new branch is never reached.

## The merge `cp -r` was deleting its way around

TODO.md's B2 said `copy_tree`'s destination must not exist, so
`cp -r a b` with `b/a` already there "fails rather than merging". The mechanism
it named was exactly right and the symptom was not. `cp` does not fail: it
**removes `b/a` and everything under it** and copies `a` in its place
(`cp.cpp`, `remove_path(to, true)`). That removal is gated on the two *kinds*
rather than on a flag — `-n` returns before it and `-i` only asks — so the
default `cp -r` over a directory it has copied before is a silent recursive
delete of whatever that directory had and the source does not. An entry filed
as a missing feature was a data-loss bug the whole time, and the reason it read
as a refusal is that the refusal is real one layer up: `copy_tree` opens with
`make_dir(to)`, and `cp` was written around it.

**`mv` has the same shape and does not reach it.** `vfs_rename` refuses two
directories with `Err(Exists)` — "no empty-directory case" — *before* it can
answer `Unsupported`, and `Unsupported` is the only thing that sends `mv` down
the copy path. So `mv a b` with `b/a` a directory is `mv: a: already exists`,
the copy-and-remove fallback is unreachable for it, and mv destroys nothing.
That
refusal is also `rename(2)`'s answer wherever the destination is non-empty, so
mv is left exactly as it was. What is left of B2's "same shape" is one
divergence and it is small: `rename(2)` *replaces* an empty destination
directory and this refuses it. That is a kernel policy change wanting its
argument in System_Calls.md, so it left B2 as **B4** rather than riding along
here.

**The stat the entry asked for was already written.** `make_dir_all` has
carried it since it landed — `Err(Exists)`, then `stat_of`, and only
`SYS_KIND_DIR` passes — so `dir_over` is that tail lifted into a helper rather
than new policy, and `make_dir_all` keeps its own copy: its loop's rule is "a
file part-way along fails the component below it", which is not the leaf's, and
folding the two would move where that error is reported.

**Only the kinds have to agree, and no rule costs a syscall to enforce.** A
file is written over because `copy_file` already opens `O_CREATE | O_TRUNC`; a
file meeting a directory is that open's `Err(IsDir)`; a directory meeting a
file or a link is `dir_over`'s `Err(Exists)`, which is one stat and only on the
collision. Nothing stats an entry that does not collide, which is the whole
tree in the ordinary case. A file meeting a *link* is written through, as GNU
`cp -r` does — `cp`'s "a link is replaced rather than written through" is a
rule about the name on the command line, not about names inside the tree.

**`link_over` is 1,022 of the 1,696 bytes and it buys idempotence.** Without
it, a source link landing on a name already taken is `make_link`'s
`Err(Exists)`, and `cp -r d e` run twice would fail the second time on any tree
containing a symbolic link — which is the one thing a merge exists to make
work. `dir_over` is the other 674.

**`-n` and `-i` needed no thought, which is worth saying because it looks like
they should have.** Both decide about the destination `cp` was *given*, and a
merge only happens when that destination exists — which is precisely when `-n`
returns early and `-i` asks. So neither reaches inside the tree and neither
had to learn how to.

What it cost: `cp` 40,377 → 42,073 and `mv` 41,234 → 42,898. Nothing else
moved — `--gc-sections` keeps `copy_tree` out of every binary that does not
copy — and the staging tree went from 1,836 KB to 1,840 KB against 2,048 KB.

The coverage is six assertions in `test/system/cp.mjs`, over a `/home/cp/m`
that holds a copy of the fixture: the merge itself, a link replaced inside it,
a file where a directory must go, a directory where a file must go, and `-n`
and `-i` still deciding at the named destination. Reverting the two source
files with the case left in place fails it at the first line, `cat: m/d/keep:
not found` — the file the old `cp` deleted.

## A line that must not enter a coroutine, and the walk with the same shape

B3 was written down when `sort` trapped on a 512-line file with the stack four
hundred frames deep, and it named the mechanism exactly: a `Task` that answers
**without suspending** resumes its awaiting coroutine from inside its own final
suspend, on the awaiter's own stack. `SysCall::await_ready` is always false, so
a real syscall is the only thing in a process that unwinds back to `_resume`'s
trampoline; a loop over an already-buffered stream performs none, and the shadow
stack grows a frame an item until the module traps. What the entry did not say
is how *near* the surface it was. Measured before the fix, on a file of `seq`
output:

| | died at |
| --- | --- |
| `grep zzz big` | 4,096 lines |
| `cat big \| tail -n 1` | 1,024 lines |
| `less big` | 2,048 lines |
| `du -s` on one directory | 1,000 entries |

The first three are `File::getline` and `LineReader::next`, which B3 named. The
fourth is `TreeWalk::next`, which it did not, and which was found while checking
that nothing else in `proc/io.h` had the shape: a walk answers most entries out
of the level it has already listed, so `du` — which sums from the listing and
performs no syscall per file — died at a thousand files in a directory. `find`
and `cp -r` walk the same tree and survive, because each writes or copies per
entry and unwinds on that syscall. That is luck rather than design, and it is
the argument for fixing the walk here rather than filing it: the two programs
that do not trap are one refactor away from being the two that do.

**The fix is the shape `FileGet`, `FileRead` and `FileWrite` have had since
0.7** — an awaiter whose `await_ready()` answers from what is in hand, so the
common case enters no coroutine at all, and whose slow path is a `Task` built
*into the awaiter*, which lives in the awaiting coroutine's own frame. Three
more of them now: `FileLine`, `LineNext` and `TreeNext`. A call that must refill
enters exactly one coroutine, and that coroutine always suspends, so depth is
bounded by what a single fill costs rather than by the length of the input.
`FileSlow` served a non-`File` type once `LineReader` and `TreeWalk` acquired
one, so it moved to `proc/io.h` under the name `SlowStep`, which is all the
rename was.

**A fast half must answer wholly or take nothing**, which is the one thing the
split is delicate about. `FileBuf::take_line` consumes the fragment it could not
finish, so a fast half that called it and then gave up would have eaten bytes
the slow half knows nothing about — the `seen` counter that makes "a final
fragment with no newline is a line" work is per call. Hence `FileBuf::has_line`,
inline and unit-tested: the fast half looks first and only then takes.
`LineReader::take_` and `TreeWalk::step_` answer the same way — a newline
already in the buffer, an entry already in the level, or nothing at all.

**`LineReader` was fixed in place rather than unwound onto `File`.** Converting
its callers was the alternative, and it would have cost `less`, `chat` and `sh`
a `File` each — some 7 KB apiece for a stream they only read — while D3 still
wants the type gone for a different reason. The awaiter costs `less` 134 bytes
and `chat` 135. The kernel twin in `src/user/io.h` stays a `Task`: it has no
caller but `test_io`, and what the two share is the answer shape — `ok(true)`
with a line, `ok(false)` at the end — which the awaiter keeps. Its comment now
says so, so the divergence is not read as drift.

**`getline` also gained the `settle_(false)` that `get_slow_` and `read_slow_`
always had.** An `Update` stream that wrote and then called `getline` read its
own output buffer back as input. No caller in the tree does that — there are two
`getline` callers and both are read-only — but the slow half is the place that
rule lives, and the split was the moment to put it there.

**The chunk-splitting in `sort`, `uniq`, `head`, `cut` and `diff` stays, and
its comments changed.** Each said a coroutine per line is a stack frame per
line, which was true when it was written and is not now. What remains is D2's
measurement: dropping `File` altogether took `sort` from 36,588 bytes to 26,277
and `uniq` from 31,216 to 21,173, and none of the five has a second reason to
carry one. `head` opens each file itself for its per-file headers as well. So
the loops are unchanged and the comments now say what actually keeps them.

What it cost, in bytes: `grep` 27,285 → 27,944, `tail` 33,868 → 34,478, `cp`
39,928 → 40,377, `mv` 40,780 → 41,234, `less` 33,762 → 33,896, `chat` 20,917 →
21,052, `du` 34,171 → 34,290, `find` 37,025 → 37,058, and `sh` 240,041 →
240,028, which is 13 bytes back for dropping a null-`Task` guard. The staging
tree went from 1,832 KB to 1,836 KB against a 2,048 KB budget. The awaiter's
state moves into the *caller's* frame, which is what those hundreds of bytes
are, and no frame near §8.2's 512-byte cliff was found: the two `getline`
callers hold a `String` and a `File` reference either way.

The coverage is four cases and one unit block. `grep` had no case at all — its
loop was B3's own example and nothing ran it — so `test/system/grep.mjs` is new,
with the 65,536-line file that used to trap and the semantics that had never
been asserted. `tail` gains the same scale on its streaming path, `du` a
2,000-entry directory, and `fullscreen` a 65,536-line page through `less`, which
is the `LineReader` regression. Those three fixtures are written into the store
rather than through the shell: every write moves the fake store's clock, and the
`ls -l` stamps pinned in `ls`, `glob` and `rename` are downstream of them.
`test_filebuf` covers `has_line`, which is the only piece of this that a suite
with no program in it can reach.

## Tab, and the word the lexer already knew where to find

`Tab` was unbound. Shell.md said so twice — "Tab is unbound" in §2, and "No Tab
completion" in §15's list of deliberate absences — but unlike every other entry
in that list it had no argument anywhere: no release note, no TODO entry, no
sentence in Concept.md. It was an omission wearing a gap's clothes, and this
entry is the first thing written about it.

Nothing had to move to fix it. The line editor has been in the shell *process*
since the discipline came out of the kernel, and `read_line` is a
`Task<Result<Line>>` — so a `co_await list_dir(…)` between two keystrokes is
ordinary code rather than a new mechanism. No operation was added and
`PROC_ABI` does not move: `Sys::List` and `Sys::Stat` were already in
`/bin/sh`'s import surface, put there by `glob.cpp`.

### The word under the cursor is the lexer's

The obvious way to find the word being completed is to scan backwards from the
cursor for a blank. That scanner would be a second, worse copy of `tokenize.cpp`
— it has to know that a blank inside `'…'` does not end a word, that `${x y}` is
one word, that `$(…)` is not searched at all — and the two would drift.

So `comp_site` runs the real `Lexer` and takes the token whose `pos()` reaches
the cursor. What makes that possible is one line in `tokenize.cpp`: `begin_` is
set *before* the token is scanned. A half-typed line is usually an unclosed
quote, so the lexer usually answers `Err(Invalid)` — and `begin()` is still the
start of the word it choked on. **The error is the ordinary path here, not a
failure**, and there is no repair pass and no fallback scanner.

Command position falls out of the same walk: the token before the word decides
it. Reserved words are the one thing the lexer will not say, since `if` and `do`
are ordinary `Tok::Word`s, so `complete.cpp` carries a nine-name table —
`if then else elif while until do { !`. `for`, `case` and `in` are deliberately
not in it: a name follows one and a word list the others, and file completion
serves both better than a wrong list of commands. Those nine names are a
*position* rule and not a candidate source; completion never offers a keyword.

The assignment prefix is six more lines and deletes two bugs at once. A
command-position word that reads `name=` does not spend command position, so a
`Tab` after `FOO=x ls` still completes a command; and when it *is* the word
being completed the site narrows past the `=`, so `PATH=/b` completes a
directory instead of offering builtins.

### Pure and impure, as `cond.cpp` and `condrun.cpp` are

Everything that can be wrong here is arithmetic over text — locating the word,
classifying it, removing quotes, putting them back, the longest common prefix,
the column layout. Everything that needs the store is a listing. That is exactly
the split the tree already has twice, so it is `complete.cpp` (pure, compiled
straight into `tests.wasm`, where a syscall would be a link error) and
`completerun.cpp` (in `braam_sh` only, beside `glob.cpp`).

The split paid immediately. `comp_common` computes the longest common prefix in
bytes, and bytes stop wherever they stop: `naïve` and `naïf` share `na` plus a
lead byte, and inserting that half-codepoint would put a `U+FFFD` on the screen
under §2.3's invariant. The unit case that pins it is two lines, and the system
suite — which reads the finished grid — could not have told the difference
between a trimmed prefix and an untrimmed one without a fixture built to trip
it.

### One function, not a `Vars`

`expand.h` reaches the shell's state through five function pointers, and the
temptation was to do completion the same way. But that struct exists for one
reason: `expand.cpp` is compiled into `tests.wasm`, so a syscall in it does not
compile, and the callbacks are how the state gets in regardless.
`complete.cpp` does not need shell state at all — it is handed a `Vec<String>`
someone else gathered — and `completerun.cpp` is allowed to await. A
five-pointer struct with exactly one implementation would have bought an
indirect call per candidate and no testability.

What crosses instead is one declaration: `edit.cpp` includes `completerun.h`,
and never `builtin.h`, `job.h` or `var.h`. Three accessors were added to make
that possible — `builtin_count`/`builtin_at` over the table that must stay an
explicit array, `func_count`/`func_at` over `job.cpp`'s function vector, and
`sh_path()`, which `builtin/command.cpp` now calls instead of its own private
copy. Variables were already enumerable. Both tables are copied into a
`Vec<String>` **before the first await**, so nothing can move one under a `Str`.

### A `PATH` candidate is not tested for being a program

`command -v` calls `file_runnable` on each candidate, which is an open, a read
and a close. `/bin` holds fifty-odd names and a syscall is two `postMessage`
hops at 34–45 µs (§4.4), so testing every candidate would put a visible pause on
one keystroke. It would also be answering a question this system does not have —
there are no file permissions — about a case that would be a bug in the tree.
Non-directories in each `PATH` element are offered, and with `SYS_PATH_DEFAULT`
being `/bin:/pkg/bin` a command completion costs two `Sys::List` calls.

A link is stat'ed only when it wins. `glob.cpp` stats every link in a listing it
means to descend into, because a wrong answer changes what matches; here the
answer only decides between a `/` and a space on a name that is already the
unique match, so the stat is paid once, at the end, and never for a listing.

### Two Tabs, bash's way and not zsh's

The first `Tab` inserts the longest prefix the candidates share and says nothing
else. Only when that inserts *nothing* — because it is already typed — does a
second consecutive `Tab` print the list, and the prompt is drawn again below it
through the `^L` path: `painted_ = 0`, then `anchor`. There is no menu, no
cycling, and no "display all 214 possibilities?" — a prompt inside a prompt
needs a second reader for the keyboard, and `CancelState::waiting` is one slot.
The flag is a local in `read_line` rather than a `LineEditor` member: it has no
second reader and it must not survive the line.

Quoting is read and written back, which is what makes a name with a space
usable. `comp_unquote` gives the literal the typed bytes stand for and
`comp_quote` puts the escaping back in whatever the word opened — a backslash
outside quotes, four bytes inside `"…"`, and `'\''` for the one byte `'…'`
cannot hold. Insertion is **append-only**: what goes in is always the tail past
what was typed, never a replacement for it, which is why splicing UTF-8 back
into a `Vec<char32_t>` needs no offset table.

### What is deliberately not completed

No `~`, because the shell has no tilde expansion to complete against. No `$`
expanded inside a path, nothing inside `$( … )` or backticks: expanding one
means running the expander from the key loop, and a word carrying one is left
alone rather than matched against the wrong text. And a word begun on an earlier
line of an unfinished construct is out of reach — the editor sees one physical
line, and `shell.cpp`'s `acc` holds the rest. `complete_line` takes the line as
a `Str` so that a `before` can be prepended later without the editor changing.

### A pasted tab is a space

`web/keys.js` turned a pasted `\t` into `KEY_TAB`, which was harmless while
nothing read one. Binding `Tab` would have made a paste run completions in the
middle of itself. It now pushes a space instead, and that is not a loss:
`/bin/edit` has always written a space for a `Tab`, so a pasted table keeps its
columns exactly as before, and a tab is already a blank to the shell (Shell.md
§3). Dropping the byte was the alternative and would have lost those columns.
`Tab` now reaches the kernel only when a key was pressed.

### The measurements

`/bin/sh` is 240,041 bytes, up from 225,242 — 14,799 for the two translation
units, the three accessors and the editor's new branch, most of it the three
coroutine frames a completion costs. `rootfs/` is 1,742,462 of the 2 MB budget.

## The dot `ls` was the only thing still showing

`ls` listed every name a directory held. The shell's globber has never done
that — `glob.cpp`'s "a leading dot has to be asked for" is as old as globbing
here — so `*` and a bare `ls` disagreed about what was in a directory, and the
disagreement was `ls`'s. It now hides a leading dot unless `-a` asks, which is
what every Unix `ls` has done since v7 and what makes the two agree.

The filter is four lines in `list_one`, and where they sit is the whole of the
design. They compact `st.ents` in place immediately after `list_dir` returns —
*before* `sort_block` and before the `-R` push walks the block for child
directories. So hiding a name and not descending into it are one decision
rather than two that can drift apart: `ls -R` skips `.git` because it never saw
it, not because a second test caught it. `-l`'s `total` falls out the same way,
counting the blocks of what is shown rather than of what is there. Filtering at
print time would have needed three separate exclusions and would have got one
of them wrong.

`.` and `..` are not synthesized under `-a`, and this is the one place the
listing departs from BSD. The VFS has no such entries — `path.cpp` and `fs.js`
know the two names only to reject them in a path — so producing them would mean
inventing a kind, a size and an mtime for each, and then a guard in the `-R`
walk against descending into the entry that *is* the directory being walked.
Two fabricated rows in every listing, to be excluded again everywhere they
matter. `-a` shows the names that exist.

Operands are exempt: `ls .profile` prints, and so does `ls -d .git`. The filter
is in `list_one`, which is fed by `list_dir`; `run` builds its block out of
argv and never passes through it. That is BSD's rule too, and here it needed no
code to get — a name the user typed is a name the user asked for.

`find` still shows everything, deliberately, and `test/system/find.mjs` pins
that. A dot rule belongs to the thing that *lists a directory for a reader* —
`ls` and the globber — not to the thing that walks a tree for a script.

`/home/t`, the fixture the twenty-one listing cases share, now has a `.dot` and
a `.d/` in it, and all twenty-one expectations are unchanged. That is the
assertion: the same strings, over a directory that now holds two more entries.

## `cmp` and `diff`, and an algorithm that is not FreeBSD's

TODO.md's **A8**, and section A is spent with it: every program that
section named is now in `/bin`. Neither needs an operation the kernel did not
have, so `PROC_ABI` does not move. Ported from FreeBSD's `usr.bin/cmp` and
`usr.bin/diff` — the semantics, the option letters and the output strings,
because scripts read those. Almost none of the code survives, and naming what
does not is most of what is worth recording.

### `cmp` is one loop where FreeBSD is five files

`usr.bin/cmp` is 751 lines across `cmp.c`, `regular.c`, `special.c`, `link.c`
and `misc.c`, and the split is the whole of its structure: `c_regular` maps both
files in 8 MiB windows and installs a `SIGSEGV` handler to turn an mmap I/O
error back into a diagnostic, `c_special` is the `getc()` fallback for anything
`mmap` refuses, and `c_link` is a third copy of the same byte loop over two
`readlink` buffers. All three end in the identical output branch.

Here there is no `mmap`, so the fallback is the only path, and the three
collapse into one loop over a `Side` that is refilled three ways — a
descriptor, stdin, or a link's target held whole. That is not just less code: it
**deletes a divergence between the two halves that FreeBSD still carries**.
`c_regular` computes `length = MIN(MIN(len1, len2), limit)` and then, after the
loop, reports EOF whenever `len1 != len2` — so `cmp -n 1` over `x` and `xy`
answers "EOF on the shorter", while `c_special` given the same two through a
pipe answers 0, its loop having stopped at `byte > limit` with neither `feof`
set. The streaming shape has only the second behaviour, which is also the sane
reading: a tail nobody asked to compare is not a difference.

**`-h` needs no `O_NOFOLLOW`, which is as well, because there is none.**
FreeBSD opens with it, catches `EMLINK`, and dispatches to `c_link` on that.
Here `stat_of(path, false)` says whether each side is a `SYS_KIND_LINK`, and
two links become two literal sides carrying their `read_link` targets — the same
loop, a different fill. One link and one not is `Err`, naming the side that is
not, as FreeBSD's `errx` does.

**Two deliberate divergences.** `-v` is gone: it prints a version, and no
program in this tree has one — the version is `uname` and `/proc`, and adding a
`--version` to `cmp` alone would be a wart that then wants adding to forty-seven
others. And the counts take `parse_size`'s grammar
([src/proc/size.h](../../src/proc/size.h), already `truncate`'s) rather than
`expand_number(3)`: `K M G T` for 1024 and `KB MB GB TB` for 1000, a leading
modifier refused, and **no `0x` or `0` prefix**. A second number grammar in the
tree would cost more than hexadecimal skip offsets are worth, and the usage
block says which one is in force.

### `diff` is Myers, because `diffreg.c` is a temp file

FreeBSD's `diffreg.c` is 1,736 lines, and `diffreg()` is a dispatcher: an input
it can handle goes to `libdiff` (Myers), and everything else — `-c`, `-q`, `-i`,
`-w`, `-b`, `-B`, and every format but normal and unified — falls back to
`diffreg_stone()`, Hunt–Szymanski over `prepare`/`prune`/`equiv`/`stone`/
`unravel`/`check`. So porting "the BSD algorithm" would have meant porting the
half BSD itself routes around, and that half wants a **seekable `FILE*`**: it
reads each file twice, once to hash and once in `check()` to confirm the hashes
were not lying, with `rewind` and `fseek` between — which is why `opentemp()`
exists, copying a pipe into `_PATH_TMP` so it can be read again. This system has
no temp directory and no reason to want one.

Myers' O(ND) greedy walk with the linear-space refinement is about 200 lines,
reads nothing twice, and needs no allocation per step: two `i32` arrays of
furthest-reaching diagonals, sized once for the whole problem and reused by
every box, and the boxes on an explicit `Vec` stack rather than recursion — the
rule [src/proc/io.h](../../src/proc/io.h)'s `TreeWalk` already states, that a deep
structure must not become a deep chain of coroutine frames. It is also what
`git` and FreeBSD's own preferred path use.

**The double read is replaced by comparing the hash against the bytes.**
`check()` exists because a hash collision would make `stone()` report lines
equal that are not — FreeBSD calls it a "jackpot" and re-reads both files to
find them. Here the line table is already in memory, so `eq()` compares the
64-bit hash first and the bytes only where it matched. That costs a `memcmp` per
*equal* pair and nothing per unequal one, which is the cheap direction, and it
is exact rather than probabilistic. "A 64-bit collision is unlikely" is not a
correctness argument; not needing the argument is better than making it.

**The input is held, and the usage block and `/etc/help` say so**, which is
`sort`'s sentence and `sort`'s argument reused: a process has 256 pages
(`BRAAM_BIN_MAX_PAGES`), so the ceiling is address space rather than a spill
path nobody can see working. The storage is `sort`'s too, and it is the one
thing that release said was worth copying — a chain of 64 KiB `String` blocks
each reserved once and never appended past its capacity, with a `Vec<Str>` over
them. A `String` that never regrows never reallocates, which is what keeps a
view valid.

**A cost ceiling, because O(ND) is O(N²) when nothing matches.** Past
`max(256, isqrt(N+M))` edits in one box the search stops and splits at the
furthest-reaching point it found, giving up optimality rather than time — GNU's
`too_expensive`, and the same instinct as the `MAX(256, sqrt(n))` bound in
`stone()`. Integer `isqrt`, so `braam::math` is not linked for one square root
the way FreeBSD links `-lm`. The recursion gives each sub-box its own budget, so
the ceiling costs a slightly longer edit script, never a wrong one.

### The half with no syscall in it, and why the split is where it is

`src/cmd/diff/` is three files: `main.cpp` reads, walks and writes;
`diffreg.cpp` and `emit.cpp` touch nothing but `Str`, `String` and `Vec`, and
are **compiled straight into `tests.wasm`** the way `sh/parse.cpp` and
`pkg/zip.cpp` are (doc/Testing.md §2), so a syscall in either is a link error.
That is not tidiness: the in-wasm suite cannot run a program, so without the
split the only way to reach the middle-snake split, the cost ceiling, the
context-frame clamp or a `@@` range with zero lines on one side would have been
to type it at a 60-column prompt. `test/unit/test_diff.cpp` reaches all of them,
including a 2,000-line pair with nothing in common, which is the ceiling's only
real exercise.

The emitters take one hunk (normal) or one group (unified, context) and append
to a `String` the caller flushes at 4 KB, rather than writing a line at a time —
`sort` and `cut`'s rule, that a write per line is a syscall per line — and that
is also what makes them callable from a test with no descriptor at all.

### Four things the formats get wrong if they are not thought about

**A `-u` context frame is bounded by the shorter side.** The obvious code
clamps each range on its own — `a0 = max(0, hunk.a0 - ctx)` and the same for
`b0` — and produces two ranges that are not the same lines when a hunk sits
within `ctx` of the start of one file but not the other. Context lines are
matched *pairs*, so the count is `min(ctx, a0, b0)` on the front and
`min(ctx, |a| - a1, |b| - b1)` on the back, taken once for both sides.

**An empty range is not `n,0` in every format.** Normal's `range()` prints the
line in *front* of the gap when the range is empty, which is what makes `2a3`
mean "after line 2"; unified prints `n,0`. They are different functions for a
reason and the tests pin both.

**A missing final newline is part of what a line is.** GNU's
`\ No newline at end of file` is usually treated as an output decoration, and
then `diff` of `x` against `x\n` prints nothing — which is wrong, and worse,
silently produces a patch that does not round-trip. It belongs in the
comparison: `eq()` answers false when the two sides' last lines are each
other's and only one of the files ended with a newline.

**`-B` is a hunk filter, not a line predicate.** `-i`, `-b` and `-w` change what
two lines are and so are folded into the hash, once, at read time — one place,
exactly as FreeBSD's `readhash()` does it. "Ignore changes where all lines are
blank" cannot be: it is a property of a whole hunk, and a hunk that inserts a
blank line *and* a real one still counts. It is applied where the hunks are
grouped.

### The directory walk descends in place

`diffdir.c` recurses at the point a subdirectory's name comes up, so its output
is in one name order all the way down. An explicit stack of directory *pairs* —
the obvious translation — is not that: it emits a whole directory, then its
children, and a LIFO gets the siblings backwards as well. The walk here keeps a
`Vec<Level>`, each level holding both listings and how far through the merge it
is, so it descends and returns exactly where recursion would, without a
coroutine frame per level. `list_dir` already answers in name order (`vfs_list`
sorts, and `ls` relies on it), so there is no sort to write.

### What the two of them cost

`cmp` is 36,356 bytes and `diff` 53,165, which makes `diff` the largest program
in the tree — three translation units, three emitters and a directory walk.
`rootfs/` is 1,812 KiB of the 2,048 in `tools/size_budget.txt`. The unit suite
gained one case, the system suite two, and the archive is 61 files.

One thing the usage block cost: `diff`'s twelve options do not fit a 60×16
screen one to a line — the first rows scroll off before the reader sees them —
so `-c -u -q`, `-C -U` and `-b -w` share a line each. `--help` that cannot be
read is not help, and doc/Testing.md §5's tenth rule is the same constraint seen
from the test's side.

## `wc` and `head`, and the columns twenty assertions were pinning

Both are ported against FreeBSD's `usr.bin/wc` and `usr.bin/head`, and neither
needed an operation the kernel did not have, so `PROC_ABI` does not move. They
were the two filters in `src/cmd/` that section A never came back for: `wc` had
**no options at all** — not `-l`, `-w` or `-c` — and `head` parsed argv by hand,
so `-n5`, `-5` and a bad option's diagnostic were all missing while every
command touched since carries them.

**What did not come across is most of each file.** From `wc.c`: libxo entire —
it is a third of that program, and `--libxo` goes with it; `SIGINFO`, which is
BSD-only and has nothing here to be (a process gets `Error::Cancelled`, not a
signal, and the interim counts it prints go to a stderr this program does not
have a second handle for); capsicum and casper; and `setlocale`, `mbstate_t`,
`mbrtowc` and `iswspace`. From `head.c`: casper, `getopt_long`'s long options —
`opt.h` says "no long options" and that stands — and `expand_number`.

**The encoding is always UTF-8, so `-m` needs no decoder and no state.** It
counts the bytes that are *not* continuation bytes, `(b & 0xC0) != 0x80`, which
is one test in the loop the byte count was already walking. That is exact for
well-formed UTF-8 and it is correct across a chunk boundary **for free** —
which is the whole of what `mbstate_t` was carrying upstream, and the whole of
why `wc.c` has a `(size_t)-2` case, a `memset(&mbs, 0, …)` on a bad sequence,
and a dangling-state check after the last read. None of the three arises. Words
stay ASCII-blank delimited, which is what BSD's own non-`-m` path does: a
continuation byte is never `iswspace`, so the two agree on every input either
can decode. What is given up is `iswspace`'s U+00A0 and U+2028, and that is
TODO.md's **P1** rather than something to half-do here.

**`-L` fixes an upstream bug rather than reproducing it.** `wc.c` folds the
running length into the maximum only when it sees a `'\n'`, and never at the
end of input — so `printf 'abcd' | wc -L` reports **0** there. A final fragment
with no newline is a line everywhere else in this tree (`File::getline`,
`tail`, `cut`, `uniq`, and `uniq`'s own entry above), so it is one here. It is
still not a *line* to `-l`, which counts newlines and is what POSIX says; the
two answers differ on that file and both are right, which is why `wc f` prints
`0 1 4` and `wc -L f` prints `4`.

**`-c` alone is a stat, not a read.** `stat_fd` was already in `proc/io.h`, and
BSD's guard comes with it: a pseudo-filesystem advertises a zero size, so
`/proc` and `/dev` fall through to the read loop rather than reporting nothing.
Anything that does not stat at all is `Err(Unsupported)` and falls through too,
so the fast path can only ever be an optimisation. It does not touch
`chunk.mjs`'s round-trip measurement, which counts `wc file` — the default
`-lwc`, which has to read.

**`Input` had to go, and that is the interesting part.** `Input` is the
files-or-stdin decision and reads the operands as **one concatenation**, which
is exactly the boundary a per-file row needs — so both programs open each
operand themselves. That is also the bug it fixes in `head`: `head -n 2 a b`
was printing the first two lines of `a` and `b` joined, where every `head`
prints two from each. `Input` stays where it belongs; three of the four filters
below still use it.

**BSD's columns, and the twenty assertions that were pinning the old ones.**
`wc` now writes `" %7ju"` per count and then the name, which is `du`'s
"right-aligned in seven" argument a second time and byte-for-byte what BSD and
macOS print. The old `"%u %u %u"` was pinned by about twenty exact-match
assertions across the suite — `pipe`, `subst`, `sort`, `seq`, `redirect`,
`cut`, `find`, `du`, `tee`, `tr`, `uniq`, `xargs`, `truncate`, `chunk`, `vars`,
`language`, `net`, `process`, `spawn`, `cwd` — because `wc` is the suite's
favourite instrument for "how much came out of that pipeline". They are now one
`counts(...)` helper in `harness.mjs` rather than twenty padded literals, so
the format is written down once and the next change to it is one line. Two that
looked at risk were not: `procfs.mjs` calls `.trim()` before `.split(/\s+/)`,
so the leading pad falls off and the byte column is still index `[2]`.

Three of the old assertions moved for a second reason and are worth naming.
`wc /home/ck/big` gained a **name** as well as a pad, since it names a file.
`echo $(echo hi | wc)` did **not** move at all — the substitution is unquoted,
so the shell splits it on blanks and `echo` rejoins with single spaces, which
is the padding cancelling itself out. And `echo "$1: $(wc < $1)"` in
`language.mjs`'s script *did*, because that one is quoted: it is the same
substitution with the field splitting turned off, and the two sitting three
lines apart in the suite is a better demonstration of what quoting does than
anything written to demonstrate it.

**The total counts operands, not successes.** `wc missing1 missing2` prints a
`total` row of zeros and exits 1, which is BSD's, because `total > 1` is a
count of what was named. `head nope b` heads `b` for the same reason. Both look
odd in isolation and both are what a shell loop over `"$@"` wants: whether a
header appears must not depend on which operands happened to open.

**`-NUM` is accepted where `sort` refused v7's `+pos`.** That refusal was
because "a `+1` on a command line here is a file name"; `-5` cannot be one, so
BSD's `obsolete()` rewrite comes across — as a scan in front of `OptParse`
rather than a rewrite of argv, since the value is a view into argv and nothing
needs allocating. It consumes a *leading* run only, as `obsolete()` does, so
`head -q -5 f` is still `bad option: 5` on both.

**Counts take `truncate`'s units, not `expand_number`'s.** `parse_size` was
already here, already pure and already unit-tested, and it is what `truncate
-s` documents — K/M/G/T are 1024, KB/MB/GB/TB are 1000. `expand_number`'s
lower-case `k`, its base-0 `strtoumax` (so `-c 010` is 8 bytes there) and its
optional trailing `b` are not reproduced; one spelling of a size in this tree
is worth more than agreement with a function nothing else here calls. A
modifier is rejected, so `head -n +5` is a usage error rather than a size
relative to nothing.

**`head` lost its `File`, which is B3's shape gone from one more caller.** Its
`-n` path was a `File::getline` loop, so `head -n 65536` had the frame-per-line
problem `sort` found; it now splits chunks itself like `cut`, `sort` and
`uniq`, and the system case runs 65,536 lines through it. That is an avoidance
and not the fix — B3 stands, and now names `tail` and `LineReader`'s two
callers. The binary went from **30,317 bytes to 24,898** on the trade `sort`
and `uniq` measured; `wc` went from 15,049 to **21,992** buying five options, a
per-file loop and a total, so the pair costs 1,524 bytes between them.
`rootfs/` is 1,637,124 of the 2 MB budget.

A last divergence, small and deliberate: a final line with no newline is
printed **without one**, where the old `head` added it. That is BSD's `getline`
and `fwrite` pair, it is what `cat` does with the same file, and a program that
invents a byte its input did not have is the wrong half of the pipe to fix that
in.

---

## `xargs`, and the stdin a child must not share

TODO.md's **A7**, ported from FreeBSD's `usr.bin/xargs`. It needed
no operation the kernel did not have — `spawn`, `wait_child` and `kill_child`
were already in `proc/io.h` with three callers in `src/cmd/` — so `PROC_ABI`
does not move. It is also what `find` was left without: `-exec` and `-ok` were
deferred to this entry, and `find … | xargs …` is now that composition.

### The child's stdin is `/dev/null`, and here it is not a choice

FreeBSD redirects it so the utility does not steal `xargs`' input; `-o` asks
for `/dev/tty` instead. Here it is stronger than a courtesy. This process
reads its own stdin in whole chunks and holds what it has not split yet, so a
child sharing descriptor 0 would not see "the rest of the input" — it would
see whatever happened to fall outside the last chunk boundary, which is a
different remainder every time the producer upstream writes differently.
`ChildIo` *moves* a descriptor at or above `SYS_FD_MIN`, so `/dev/null` is
opened once per run rather than once per process.

`-o` and `-p` are out with the same argument: both open `/dev/tty`, of which
there is none — a terminal here is a screen and a console (§3.5), reached by
`Sys::Tty` and `KeyClaim` rather than by a path. `-J`, `-P`, `-R` and `-S` are
FreeBSD extensions left out beside them. `-P` in particular would want a child
table and a reaper of its own to bound against `SYS_CHILD_MAX` (16); one child
at a time satisfies A7's warning by construction instead.

### Four of FreeBSD's rules kept, and one dropped

Kept, because each is load-bearing and none of them is guessable:

- **`-I` is a run to the line, and the arguments do not also go on it.** The
  line reaches the command only where the marker is; `xargs -I% echo x` prints
  `x` and not `x a`. The command's own name is never substituted either.
- **`-x` wants `-n`.** It says "fail rather than truncate", and there is
  nothing to truncate against without a limit to have overrun.
- **`-E`'s marker matches a whole argument**, not a prefix, so `-E a` does not
  stop at `ab`.
- **`-r` does nothing.** An empty input runs nothing here anyway, which is what
  the flag asks for elsewhere; it is taken so a script that passes it works.

Dropped: FreeBSD's flush test is `(Lflag <= count && xflag)`, and `Lflag` is 0
when `-L` was not given — so `-x`, which is only legal with `-n`, makes
`0 <= count` true at every line and `xargs -n2 -x` behaves as `-n1`. That is a
bug rather than a rule, and POSIX says `-x` bounds the size and not the
batching, so the line trigger here asks whether `-L` was given.

One rule is nobody's and is what the shipped implementations do rather than
what the source reads: **a line with nothing on it is not a line.** It does
not advance `-L`'s count, and no run is ever made over no arguments at all.

### There is no `ARG_MAX`, so the program declares one

FreeBSD sizes its buffer from `sysconf(_SC_ARG_MAX)` less the environment.
Nothing here answers that: `Sys::Spawn` bounds only the environment
(`SYS_ENV_MAX`), and the real ceiling is `SYS_STAGE_MAX` — a syscall payload
is staged, and argv rides in one. `LINE_MAX` is 128 KiB, well inside it, and
is both the default and the cap `-s` clamps to. The budget an input argument
may take is that less what the command's own words already cost, terminators
included, which is what makes `-s 16` over `/bin/echo` leave six bytes.

Without `-R` and `-S` there is no per-argument replacement count or byte cap,
so `-I` substitutes every occurrence in every word. FreeBSD's defaults of 5
and 255 exist to be tuned by those two flags; hardcoding numbers whose knobs
are absent would be arbitrary rather than compatible.

### A frame per run, not a frame per byte

The parser is a plain function over a chunk that returns as soon as the caller
has something to `co_await` — a run, or a fault. **B3**'s shape, arrived at
the same way `cut` and `tr` did: a coroutine per input unit is a shadow-stack
frame per input unit, and for `xargs` the unit is a byte. Arguments
accumulate as offsets into one `String` rather than as `Str`s, since a view
into a growing buffer dangles; the `Vec<Str>` a spawn is handed is built at
flush time, when nothing more will move it. The one place the buffer is not
simply cleared is FreeBSD's relocation: a batch that fills up in the middle of
an argument runs what is complete and then moves the unfinished tail to the
front, so `echo aa bbb | xargs -s 15` is two runs and not an error.

## Four filters, and the two libraries that had no caller

TODO.md's **A6** — `tee`, `cut`, `tr` and `seq` — and with `tr`, its
**D1**. The ports read against are v7's `tee` and `tr` by way of `v7besm/cmd/`,
LiteBSD's `cut` and FreeBSD's `seq`. None needed an operation the kernel did not
have, which is what A6's own entry predicted: the gap is the program layer.

**Two of the four are the first caller of something that was only unit-tested.**
`File::get`, `File::put` and `rune_lower` had no caller in `src/cmd/` at all —
D1 said so — and `/bin/tr` is now all three. `braam::math` was in the same
position: the library shipped, the manual documented it, and nothing in the tree
linked it. `/bin/seq` is the first `LIBS braam::math` in `src/cmd/`.

### `tr` is runes, so v7's three tables cannot be

v7's `tr` is eight-bit clean and its `code[256]`, `squeez[256]` and `vect[256]`
are byte tables. Its own manual page writes down what that costs: *"a multi-byte
character can only be named in string1 or string2 by writing out its bytes —
`tr п н` does not exchange two letters"*. What it buys is that `tr a-z A-Z`
passes Cyrillic through untouched, since every one of the 256 values maps to
itself unless named.

Over codepoints both halves get better, and the tables become **three
questions** rather than three arrays. `in1(c)` is membership, `to(c)` is the
mapping, `squeezed(c)` is the squeeze set, and every flag composes through them
exactly as it did through v7's arrays: string2 padded with its last element is
`to`'s `i >= size` branch, a member with no string2 at all is its
`runes.empty()` branch, and a member of string2 past the end of string1 still
counting for `-s` falls out of `squeezed` consulting the whole set.

**`-c` has to be a predicate.** v7 materialised the complement — `vect[]`
rewritten in place into a list of non-members — because it walked that list in
lockstep with string2 to pair the two. The complement of a rune set has 1.1
million members, so there is no list to walk; `in1` negates instead, and the
pairing it would have driven is gone with it. That is why `-c` with a string2 of
more than one element is refused: with no index into string1, every complemented
rune would take the same element, and saying so is better than picking one.

**The classes split in two, and the split is the whole design.**
`[:digit:]`, `[:space:]` and `[:punct:]` are a short ASCII run each, so they are
walked out into the set's `runes` and keep their positions — `tr '[:digit:]' xy`
pairs `0` with `x` and everything after with `y`, which is what a written-out
set would do. `[:lower:]`, `[:upper:]`, `[:alpha:]` and `[:alnum:]` have no list
that could be written down over runes and stay predicates.

**`[:lower:]` against `[:upper:]` is the one mapping, and it is `rune_lower` and
`rune_upper`.** No pairing of two element lists could express it — the Cyrillic
pair alone is sixty-six entries — so it is a function pointer set once and read
in `to()`. It is one branch in one place: `-c`, `-d` and `-s` go on reading the
class bits as before. And the two functions are their own membership test, since
a letter with another case differs from itself under one of them, which is why
`[:lower:]` needs no table either. `tr '[:lower:]' '[:upper:]'` uppercases
Greek and Cyrillic; `tr a-z A-Z` still does not, and the system case pins both
lines next to each other because the contrast is the point.

That is also the honest boundary. `rune_lower` and `rune_upper` map one
codepoint to one, so `[:lower:]` here means *a rune that has an upper-case
form* rather than Unicode's `Ll`: `ß` is not in it, and `[:alnum:]` misses every
caseless script. `tr -d '[:alnum:]'` leaves 日本 alone, and the case pins that
too, so the definition is nailed down rather than accidental.

**Three v7 behaviours are deliberately not carried over.** v7 drops a NUL from
the input and cannot hold one in a set, because 0 is its end-of-set marker; here
U+0000 is an ordinary rune both ways, which is why the copy loop needs an
explicit *nothing written yet* flag where v7 relied on zero being unproducible.
v7's escapes are `\ooo` and *`\c` is `c`*, so `\n` is the letter `n`; these are
GNU's, because a `\n` that means `n` is a trap and nothing else here spells a
newline that way. And **`-s` with one set squeezes that set**: v7's `squeez[]`
was built from string2 alone, so `tr -s a` did nothing at all there, where
POSIX and GNU squeeze string1 and that is what `tr -s ' '` is written for.

`\ooo` names a codepoint rather than a byte, which is the rune model showing
through: `\303` is U+00C3 and not the first byte of a two-byte sequence. A
`tr` over binary is not what this one is for — a malformed sequence reaches
`File::get` as U+FFFD — and that is the D1 bargain: a set element is a
character, and the price is byte transparency.

**The loop is the reason D1 wanted this program.** `FileGet` and `FilePut` are
awaiters with an `await_ready` fast path, so a rune already in the buffer never
enters a coroutine and the shadow stack does not grow per rune. That is the
shape B3 says `getline` should acquire, and `tr` is what proves it at scale: the
case pushes 5,120 runes through a real pipe.

### `cut`'s list is ranges, which deletes three bugs at once

BSD marks a byte per position in `positions[_POSIX2_LINE_MAX]` and reads fields
into an `lbuf` of the same size. Three of its behaviours follow from that array
and from nothing else: a position past 2,048 is `list: N too large`, a line past
2,048 bytes is `line too long` and fatal — **and so is a final line with no
newline, and so is a line holding a NUL**, since the scan looks for a `\0`
before a `\n` and cannot tell the three apart.

A `Vec<Range>`, sorted and merged once, has none of them. `-N` is `{1, N}` and
`N-` is `{N, 0}`, so BSD's `autostart`, `autostop` and `maxval` globals — and
the clamp that had to reconcile them — are not there either. The system case
cuts a 4,096-byte line and a last line with no newline, both of which were the
fatal errors upstream.

**`-c` counts characters and `-b` counts bytes**, which is the honest reading on
a UTF-8 system and is why POSIX's `-n` is absent: *do not split a character* is
what `-c` already means here. `cut -c 1-3` and `cut -b 1-6` give the same three
Cyrillic letters, and the case pins the pair.

### `seq` keeps `-f`, because the engine already had it

`-f` is a printf format, and this tree has no printf and argues against format
strings. But the conversion `-f` accepts is *only* a float one — FreeBSD's
`valid_format` admits `%[#0- +']*[0-9]*[.[0-9]*]?[aAeEfFgG]` and exactly one of
it — and the engine behind `fmt_f64` is musl's `fmt_fp`, which **already takes a
field width and printf's flag word**. `fmt_f64` passed 0 for both and threw the
rest away.

So the seam is one function, `fmt_f64_padded`, passing them through, with the
flags given as their printf characters so musl's `1u << (c - ' ')` encoding
stays inside `ftoa.cpp`. Nothing was added to the engine. **It is a separate
function rather than defaulted parameters on `fmt_f64`**, and that is the whole
of why it costs nothing: `-ffunction-sections` with `--gc-sections` drops an
uncalled function at link time, so every binary that links `braam::math` and
never asks for a width is unchanged, while defaulted parameters would have put
the flag-character scan on the common path and widened `fmt_f64_shortest`'s
seventeen calls for a feature they do not use. That is D2's measurement again,
in a smaller place.

One thing the engine alone gets wrong: `fmtfp.c` is musl's `fmt_fp` without the
`vfprintf` around it, and printf's rule that `-` overrides `0` lives in the
caller. `flag_bits` drops `ZERO_PAD` when `LEFT_ADJ` is set, and
`test_ftoa.cpp` pins it along with the case only a real width can produce — a
zero pad **inside** the sign, `-01.0` rather than `0-1.0`, which is exactly what
`-w` of a negative range needs and what no padding around a finished conversion
could give.

`'` is thousands grouping. It parses and does nothing, rather than being
refused: the C locale is the only one here and does not group, so glibc and
FreeBSD print `%'g` and `%g` identically — ignoring it is the exact answer
rather than an approximation. Width and precision are capped at 400 and 120,
because past those the conversion would be truncated inside its buffer without
saying so, and a silent wrong number is worse than `not a format`.

**`-w` is `generate_format` without `sprintf`.** Everything it took from
`sprintf`'s return value is on the `Str` that `fmt_f64` hands back — the printed
width is `size()`, the exponent form is a `find('e')`, and the decimal places
are the digits after the point. FreeBSD's asymmetry is reproduced on purpose and
commented as such: `first` and `incr` fold into the precision, `last` into the
width alone. So is the recomputation of `last` as the value the loop will
actually stop at, which is what sizes `seq -w 0 .05 .1` to `0.10` rather than to
`.1`. What is *not* reproduced is the undocumented second `-w`, which upstream
switches the pad from zeros to spaces; a repeated flag silently changing the
output is not something to keep.

**The default increment is +1 whichever way the range points.** FreeBSD flips
the sign by direction, so its `seq 3 1` counts down and its `seq 1 0` prints
`1 0`. GNU's does neither, and GNU's is right for the one place `seq` is
actually used: `for i in $(seq 1 $n)` with `n` of 0 must do nothing, not two
turns. An increment pointing the wrong way prints nothing too, for the same
reason; a **zero** increment is still an error, as it is in Plan 9, GNU and
FreeBSD alike. An empty range prints nothing at all — not even the newline
FreeBSD writes unconditionally.

FreeBSD's rounding fixup is kept exactly: `cur = first + incr * step` rather
than an accumulation, and at the end, if the value that ended the loop *prints*
as `last` does and not as the one before it, the loop stopped on rounding and
`last` is emitted. That is the whole of why `seq 1 0.1 1.2` ends at `1.2`.

**`OptParse` needed no change to take a negative operand.** FreeBSD guards
`getopt` with `!numeric(argv[optind])`; the same guard is `p.rest()[0]` tested
before each `next()`, which works because a word part-way through a bundle
necessarily begins with `-` and one of `w f s t`. `looks_numeric` is FreeBSD's
loose `numeric()` and not `parse_f64` on purpose — they answer different
questions, and keeping them apart is what makes `seq 1e` say *not a number*
rather than *bad option*.

### `tee` loses its buffers

v7 copies byte by byte from a `BSIZE` input buffer into a `BSIZE` output buffer,
and when any destination is a terminal or a pipe it breaks the fill loop early
and writes in sixteen-byte dribbles so that an interactive `tee` echoes a line
as it is typed. `read_chunk` hands over what arrived rather than blocking for a
full block, so both the second buffer and the dribble are answered by the shape
of the call. Nothing replaced them.

Three of v7's behaviours are fixed rather than kept. It sized `openf[]` at 20
and let the argument loop write past it into `n`, `t` and `aflag`; there is no
table here, only a `Vec` of descriptors and the kernel's own limit. It detected
a failed open by `stat`ing the *name* afterwards, so a `creat` that failed on an
existing file left `-1` in the table to write to for ever. And it returned 0
whatever happened; a file that will not open is reported and the status is 1,
while everything else named still gets the bytes.

A fourth is new rather than fixed: an output whose far end has gone ends the
run. v7 never looked at a `write`, so `seq 100000 | tee f | head -n 2` would
have read the whole input to write it nowhere; `Err(Closed)` on any of the
outputs breaks the loop, which is the same rule `head` follows to stop the
producer upstream.

`-i` is `sig_catch(SIG_INT)` and an `Err(Intr)` retried on the read. It is the
one thing here that needed a signal, and `SIG_INT` was already in
`SIG_CATCHABLE`.

### The measurements

`/bin/tee` is 18,113 bytes, `/bin/cut` 23,862, `/bin/seq` 31,237 — the one
linking `braam::math` — and `/bin/tr` 34,171, which is what a `File` costs.
`rootfs/` is 1,605,146 of the 2 MB budget.

---

## A screen a program opens, and `PROC_ABI` 20

0.8 gave the *page* up to four terminals and gave a *process* exactly one of
them. [System_Calls.md](../System_Calls.md) §10 said so in as many words — "no call
here names a screen, and that is deliberate" — and the reasoning behind it was
sound for what it was answering: two shells on two grids need no way to reach
each other's, and not building one kept `PROC_ABI` still. It was never an
argument that a *program* should not paint two grids; nobody had asked.

An emulator asks. A BESM-6 has a console line and a second Consul line, and the
second one is a telnet listener — which this system has no socket for, so its
only possible home in a browser tab is a second screen of the same process.
An editor started on one screen and painted on another is the same shape, and is
what [src/cmd/edit.cpp](../../src/cmd/edit.cpp)'s `-S` is: the caller §4.3's first
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
[Shell.md](../Shell.md) already states there is no `/dev/tty`. **Two processes and
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
kernel export's arity, which [test/system/abi.mjs](../../test/system/abi.mjs)
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

So the header is the kit's, in [limits.h](../../src/compat/include/limits.h)'s
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

`/bin/find` is TODO.md's A3, and the port it was read against is
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
[proc/io.h](../../src/proc/io.h) beside the copy helpers, `next(path, entry)`
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

`-name` is the shell's own matcher, [sh/match.h](../../src/cmd/sh/match.h), linked
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

TODO.md's A4, read against v7 by way of `v7besm/cmd/sort/` and
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
TODO.md's new B3, since the fix belongs in `getline` rather than in
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
[Testing.md](../Testing.md) §5's ninth rule, since the failure is silent and reads
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

---

## `du`, and a post-order over a pre-order walk

`/bin/du` is TODO.md's A5 and `TreeWalk`'s third caller, which is
what its own entry said it would be. The port it was read against is v7's, by
way of `v7besm/cmd/du/`, and most of that file is machinery this system does not
have: a `chdir(2)` per level and a `chdir("..")` back out, a `PATHSIZ` buffer
appended to once per level, a `DIRFDMAX` descriptor ration with a `telldir`
cookie so a deep tree does not hold one open per level, and the `NDENT` batch
that ration needed. `TreeWalk` answers all four at once — it holds a level's
entries rather than a descriptor, its path is built by `path_join` per entry,
and there is no cwd to move.

**The `ml[]` table has nothing to do.** v7's linked-file list is why its own
manual documents "if there are too many distinct linked files, *du* counts the
excess files multiply": a fixed `ML` of them, and the excess counted twice.
There are no hard links here (CLAUDE.md's known gaps), so there is no
double-counting to guard and no cap to get wrong. Nothing replaced it.

**Counted in `FS_BLOCK` = 512, printed in kibibytes, and rounded once.** The
unit is `df`'s `1K-blocks` column rather than v7's 512, since the two now agree
about what a number means; the *counting* is in blocks, because a file's cost is
`(size + 511) / 512` — the same figure `ls -l`'s `total` shows — and a parent
must be the sum of its children's blocks and not of their roundings. Three
two-byte files are three blocks and print as 2K; rounding each to a kibibyte
first would say 3. `-k` is that default named, and is what undoes an earlier
`-h`; `-h` is the same scaling `df` and `ls` spell, a third copy of a nine-line
`human()` that D2 already recorded the argument for.

**A directory contributes nothing of its own.** OPFS reports no size for one, so
what `du` sums is the files, and an empty directory prints 0 rather than the
block a real filesystem would charge for it. That is the same fact `ls -l`'s
`total` carries, and it is why `du` here reads as "what is in this tree" rather
than "what this tree costs the store" — `df` is the one that answers the second
question.

**`TreeWalk` is pre-order and a total is post-order**, so the program keeps a
stack of levels and emits one as the walk *leaves* it. Depth is the count of
`'/'` in `path.substr(walk.root_len())` — a name carries no slash, so it is
exact, and it is cheaper than the prefix test the same rule could have been
written as. An entry at depth *d* pops every level past *d*, each pop adding its
total into the one above; a directory pushes a level, a file adds to the top and
prints under `-a`. Recursion would have been shorter and is what §8.2 forbids: a
frame per level is the shape `TreeWalk` exists to avoid, and v7's `descend()`
recursing per directory is what forced that port to measure a `MAXDEPTH`.

The dropped-level case falls out of the same rule. A directory that will not
list is an `Err` naming itself in `at()`, and `find`'s handling — report it, set
the status, call `next()` again — leaves a level on `du`'s stack that no entry
will ever be reported under. The next entry's depth pops it, with its total
intact, so nothing had to be told that a level went away.

**No tab.** Every `du` writes `%d\t%s`, and there is no tab stop here: the
terminal is a cell grid (§2.3), `screen_write` handles `'\n'` and passes
everything else to `screen_put`, and `rune_safe` lets U+0009 through — so a tab
would land as one blank cell and the columns would not line up. The count is
right-aligned in seven with two spaces after it, which is what `df`'s columns
are, and what a tab was drawing anyway.

Three deviations from v7, each deliberate. **A file operand prints its own
line**, where v7 printed nothing without `-a` and said so under BUGS. **`-a` and
`-s` together are a usage error** rather than a silent precedence, which is what
POSIX asks for and what the two flags mean. And **`-h` and `-k` are ours**: v7
had one unit and no way to say which.

What did not come across: `-x` would need a mount to stop at, and while `/proc`
and `/dev` are mounts, naming one is how you walk it — there is no tree here
where a filesystem boundary is a surprise. `-H` and `-L` have nothing to switch
between, since a link is never followed and `SYS_KIND_DIR`-only descent is why
no cycle guard is needed. `-d` is GNU's, and `-s` plus a path is the whole of
what it buys at this size.

`/bin/du` is 34,284 bytes and `rootfs/` is 1,497,212 of the 2 MB budget.

## `df`'s columns are as wide as their widest value

BSD's widths put a row in sixty columns, and this program took them literally: a
nine-figure `Avail` filled its nine-wide field exactly, and `put_right` writes
something that will not fit whole rather than truncating it, so the number ran
into `Used` and the two read as one. Nine figures is 99 GiB in `1K-blocks`,
which is not a hypothetical quota — the fake's ten gibibytes were simply under
it, which is why three passing suites never saw this.

**The widths are now a floor, not a size.** A first pass over `/proc/mounts`
formats every cell and takes the widest of each column plus one space; a second
pass prints against those. BSD's numbers stay the minimum, so a table that fit
before is byte-identical, and the arithmetic is the same figures either way —
the cost is parsing the mount table twice, which is a `String` already in
memory and half a dozen lines long.

Two passes rather than a `Vec` of rows because a row holds four `Buf<32>`s and
the mounts are text that is already there: the second pass rebuilds each row
from the line for the price of a division. The row lives in a nested scope so
it stays off the coroutine frame, which is §8.2's 512 bytes.

`Used` and `Avail` widen independently. They are one width in BSD because the
figures are of a kind, but the header of each is its own word and a column that
does not need the room should not take it.
