# TODO

What is left, in the order it earns its place. Reasoning belongs in
[Release_Notes.md](Release_Notes.md) and the specification in
[Concept.md](Concept.md); this file is the sequence.

Every entry names **the caller that satisfies §4.3's first rule** — *every
operation has a caller in `src/cmd/`* — and whether it moves `PROC_ABI`. An ABI
bump invalidates every stamped binary and every installed package, so an entry
that needs one says so and is batched with anything else that does.

Every entry carries a tag — `A1`, `B2`, `N5` — so it can be named from a commit
or a conversation. A tag belongs to its entry for good: amend one, never reuse
or renumber it.

---

## Why the syscall table is not the gap

The table was measured against the generic Unix syscall set. Forty-eight
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
- **Missing, and waiting for a caller.** `fstat`, `O_EXCL`, `mount`. See "Not
  scheduled" below — none of them has one.

The twelve missing *programs* were checked against the table one by one. None
is blocked on a syscall. **The gap is the program layer**, which is what
sections A and B are.

---

## A — the program layer

No ABI change. Each is one file in `src/cmd/`, a name in `BRAAM_BIN_LIST`
(`src/cmd/CMakeLists.txt`), **a line in `rootfs/etc/help` in the same commit** —
nothing notices at run time when that goes stale and the `help` smoke case fails
on a forgotten line — and a case in `test/smoke/` with a line in the `CASES`
table in `test/run.mjs`. The in-wasm suite cannot run a program, so every one of
these is a smoke case.

Adding a program also moves two counts that are written down: `compared` in
`test/unit/test_zip.cpp` (the archive's file count) and the byte count in
`test/smoke/subst.mjs` (which concatenates `/etc/help` three times).

- [x] **A1. `cp`** — done. The copy moved to `src/proc/io.h` and `mv` now
      shares it.
- [x] **A2. `basename`, `dirname`** — done. Text in, text out, no syscall but
      the write. `path.cpp`'s pair turned out to be the wrong semantics — it
      takes a normalised absolute path — so only `path_basename` is reused, and
      after the trailing slashes are trimmed.
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

Size is not the constraint: `rootfs/` is around 1.2 MB of the 2 MB in
`tools/size_budget.txt`, at 13–19 KB a program.

## B — program-layer correctness

- [ ] **B1. `edit` saves through a temp file.** `save()` opens
      `O_WRITE|O_CREATE|O_TRUNC` — destroying the file — and only then writes,
      so a failure or a signal mid-`write_all` loses the original. Write
      `<path>.tmp.<pid>` (the pid from `Sys::GetPid`, no randomness needed) and
      rename it. **No syscall is needed and `Sys::Truncate` is not the fix** —
      truncate-after-write only moves the failure window from "empty" to "new
      prefix, old suffix", which parses and compiles and is worse. `vfs_mount`
      is called from two places and there is one writable mount, so `Rename`'s
      cross-mount `Err(Unsupported)` cannot fire between two paths under `/`;
      what *can* fire is a browser with no `FileSystemFileHandle.move`, so it
      needs `mv`'s existing copy-then-remove fallback.
- [ ] **B2. `copy_tree` will not merge.** Its destination must not exist, so
      `cp -r a b` with `b/a` already there fails rather than merging. `mv` has
      the same shape. Distinguishing "a directory is already there" from "a file
      is in the way" needs a stat that `make_dir`'s `Err(Exists)` does not give.

## C — measured, not guessed

- [ ] **C1. `pkg verify` over a megabyte.** `SYS_READ_MAX` should have taken it
      from ~2,210 reads to ~18. `test/smoke/chunk.mjs` asserts the invariant on
      `wc`; the megabyte workload has not been measured since, and the figure in
      the release note is derived rather than observed.
- [ ] **C2. The pipe now carries more.** Eight `Channel` slots at 64 KiB is
      512 KiB per pipe against 4 KiB before, and `BRAAM_BIN_INITIAL_PAGES` is
      four, so a 64 KiB `_alloc` puts `memory.grow` on the read path. Neither
      has been measured under a deep pipeline.

## Not scheduled

Each needs an argument in Concept.md before any of it is built.

- **N3. `fstat`.** The only field anyone wants from a descriptor is its size,
  and `seek_fd(fd, 0, SYS_SEEK_END)` already gives it — `/bin/tail` does exactly
  that. `Fs` has no stat-by-handle either, so mtime could not be answered.
- **N4. `O_EXCL`.** Its only caller would be a `mktemp` that would use the pid
  anyway, and `Open` already refuses a second concurrent writer.
- **N5. Randomness** — done, and not the way this entry argued. It held that
  `$RANDOM` was a shell-local PRNG seeded from `Sys::Now ^ Sys::GetPid` and
  not an ABI, on the ground that nothing in the tree wanted a nonce or a key.
  What landed is `Sys::Random`, op 59, over an appended `SvcOp` and
  `crypto.getRandomValues`; the caller §4.3 asks for is `/bin/sh`, which is
  `$RANDOM` itself. The PRNG survived and only its seed moved. It rode the
  `PROC_ABI` 18 → 19 bump rather than forcing one. Release_Notes.md has the
  argument.
- **N6. `Sys::Mount`.** §5.4 says it is unbuilt; it needs the `Fs` backend *and*
  an answer to the namespace question §5.1 leaves open.
- **N7. A batched step protocol.** Reconsider only if a workload survives
  `SYS_READ_MAX`.
