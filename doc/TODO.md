# TODO

What is left, in the order it earns its place. Reasoning belongs in
[Release_Notes.md](Release_Notes.md) and the specification in
[Concept.md](Concept.md); this file is the sequence.

Every entry names **the caller that satisfies §4.3's first rule** — *every
operation has a caller in `src/cmd/`* — and whether it moves `PROC_ABI`. An ABI
bump invalidates every stamped binary and every installed package, so an entry
that needs one says so and is batched with anything else that does.

Every entry carries a tag — `A3`, `B2`, `D1` — so it can be named from a commit
or a conversation. A tag belongs to its entry for good: amend one, never reuse
or renumber it. A finished entry leaves this file, since what is left is the
whole of what it is for, and the gap it leaves stays a gap: number a new entry
past its section's highest, never into a hole. The tags missing below are spent,
not free, and Release_Notes.md is where each of them went.

Nothing here is a licence to build it. An entry that changes what the system
*is* — an operation, a flag, a rule — wants its argument in Concept.md first,
and the argument lands in the same commit as the code.

---

## Why the syscall table is not the gap

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
is blocked on a syscall. **The gap is the program layer**, which is what
sections A and B are.

---

## A — the program layer

No ABI change. Each is one file in `src/cmd/`, a name in `BRAAM_BIN_LIST`
(`src/cmd/CMakeLists.txt`), **a line in `rootfs/etc/help` in the same commit** —
nothing notices at run time when that goes stale and the `help` system case
fails on a forgotten line — and a case in `test/system/` with a line in the
`CASES` table in `test/run.mjs`. The in-wasm suite cannot run a program, so
every one of these is a system case.

Adding a program also moves two counts that are written down: `compared` in
`test/unit/test_zip.cpp` (the archive's file count) and the byte count in
`test/system/subst.mjs` (which concatenates `/etc/help` three times).

- [ ] **A3. `find`** — `-name`, `-type`, `-newer`; `Sys::List` already carries
      kind and mtime per entry, and `copy_tree`'s explicit-stack walk is the
      shape to reuse. Consider lifting that walk beside the copy helpers when
      the second caller appears.
- [ ] **A4. `sort`, `uniq`** — pipeline staples. `sort` is bounded by memory;
      say so in `help` and in the release note rather than pretending otherwise.
- [ ] **A5. `du`** — over the same walk as A3's `find`.
- [ ] **A6. `tee`, `cut`, `tr`, `seq`** — one read/write loop each.
- [ ] **A7. `xargs`** — `Spawn`/`Wait`; watch `SYS_CHILD_MAX` (16).
- [ ] **A8. `cmp`, `diff`** — `diff` last, the only one needing an algorithm.

Size is not the constraint: `rootfs/` is around 1.3 MB of the 2 MB in
`tools/size_budget.txt`, at 13–19 KB a program, or 20–27 KB for one carrying a
`File`.

## B — program-layer correctness

- [ ] **B2. `copy_tree` will not merge.** Its destination must not exist, so
      `cp -r a b` with `b/a` already there fails rather than merging. `mv` has
      the same shape. Distinguishing "a directory is already there" from "a file
      is in the way" needs a stat that `make_dir`'s `Err(Exists)` does not give.

## D — the port layer

`src/proc/file.h` is in, and the ten programs in `src/cmd/` that write a row at
a time go through it. The rest keep `write_all`: a `File` earns its ~7 KB only
where several writes coalesce into one syscall. None of the below moves
`PROC_ABI` or adds an operation, and each names the caller that would satisfy
§4.3's first rule.

- [ ] **D1. `/bin/tr`.** A6 already names it, and it is the rune path's first
      real caller: `File::get`, `File::put` and `rune_lower` have no caller in
      `src/cmd/` today and are covered by `test/unit/test_filebuf.cpp` and the
      manual's worked example alone. Wants a line in `rootfs/etc/help` and a
      system case, and moves `compared` in `test_zip.cpp` and the byte count in
      `subst.mjs` as any new program does.
- [ ] **D2. Formatted output over a `File`.** "A write per field is a syscall
      per field" stopped being true the moment the stream buffered, so `Buf<N>`
      is no longer the only way to put a number on the screen. Ten callers now
      build a `Buf` and write it to a `File`; what they want is
      `out.put(pid)` with the padding, and the hard part is that a `put`
      which may have to flush cannot be the synchronous chainable call
      `Buf::put` is.
      `math/ftoa.h`'s `put_f64` is the shape to match.
- [ ] **D2a. The `String` accumulators.** `pkg/query.cpp`'s `emit`, `unzip`'s
      listing and the sh builtins build every row into one heap `String` and
      write it once, because a write per row is a syscall per row. A `File`
      does that without the allocation — but only pays for itself where the
      program has other rows to write, which is the same test D applies
      everywhere.
- [ ] **D3. `Input` and `LineReader` onto `File`.** `File::getline` replaced
      the `LineReader` in `grep`, `head` and `tail`; `cp` and `mv` still have
      one, so two line readers exist and one of them cannot be unwound. `Input`
      stays either way: it is the files-or-stdin decision, which `File` takes
      rather than makes.
- [ ] **D4. Nothing, for `/bin/sh`.** Recorded so it is not re-derived: the
      shell was converted and reverted. Its builtins already write once
      (`builtin.h`), `job.cpp` writes to descriptors that are often a pipe or a
      redirect and must not be buffered, and the per-prompt newline must flush
      at once. `term.mjs`'s two round-trip counts survived the experiment
      untouched, so §4.4 is not what stands in the way — there is simply no
      syscall to save.

## P — the port kit

`braam::compat` ([doc/Compat.md](doc/Compat.md)) is in, and `PORT` is the whole
of the opt-in. Not to be confused with **D**, which is `src/cmd/` adopting
`File` and has nothing to do with porting. No entry here moves `PROC_ABI` or
adds an operation; the caller each names is a **ported package**, since the kit
exists for callers outside this tree.

- [ ] **P1. Group A's remainder.** `wchar`/`wctype` (`iconv`'s `mbstate_t`, not
      `le`'s, which cannot hold a split sequence), `strftime`/`mktime` over
      `civil_secs`, `sscanf`, `fnmatch`, `sys/queue.h`, byte order, `getenv`
      over one heap block. Callers: `le` and `iconv` for the wide half, `zip`
      for the calendar, `le` for `fnmatch`.
- [ ] **P2. Group B.** `FILE` over `proc/file.h`, the `b_*` family, `struct
      stat`, dirent. `zip` and `le` each wrote this; `vi` is the smallest real
      surface and the migration to prove it on.
- [ ] **P3. Migrate `duremark` and `adventure`.** The smallest shims, and
      `duremark` at 23,601 bytes is the honest size worst case: record both
      numbers.
- [ ] **P4. Migrate the rest**, `iconv` last — its own `include/` tree must go
      in the same commit, and its errno numbers change. `dhrystone` never.
- [ ] **P5. Make the float arm droppable.** It is 5,367 of `snprintf`'s 8,853
      bytes and every port pays it for `%d` alone, because the engine is one
      function. A separate TU behind a weak reference does not work — a weak
      reference does not pull an archive member, so `%f` would silently print
      nothing. Wants an OBJECT library the port names, or a strong reference
      only the float path emits.
