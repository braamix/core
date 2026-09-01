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
was blocked on a syscall, and sections A and B are both spent: the program layer
is neither short of coverage nor known to be wrong, and **what is left is the
port layer**, D and P. What adding a program costs is
[Testing.md](Testing.md) §6, not a section here.

---

## D — the port layer

`src/proc/file.h` is in, and the ten programs in `src/cmd/` that write a row at
a time go through it. The rest keep `write_all`: a `File` earns its ~7 KB only
where several writes coalesce into one syscall. None of the below moves
`PROC_ABI` or adds an operation, and each names the caller that would satisfy
§4.3's first rule.

- [ ] **D2. Nothing, for formatted output over a `File`.** Recorded so it is
      not re-derived: typed `put`s were built and reverted. `df` was converted
      whole — output byte-identical, columns verified — and cost **7,494 bytes,
      27%**, because its `co_await` sites went from 8 to 28 and each added
      suspension point is ~375 bytes of the caller's state machine. A `Buf` and
      one `write` batch a row into one suspension point; typed `put`s cannot.
      The frame it buys back is 256 bytes, and `df`'s frame was never shown to
      be near §8.2's 512-byte cliff. The shared `fmt_u64` primitive lost too,
      independently of `File`: every binary that formats a number grew ~430–550
      bytes, `inline` recovered only 100 of them, and routing `date`'s `put2`
      through it — the case that should have paid it back — grew `date` further.
      One general width/pad/sign routine cannot beat a tight digit loop the
      optimiser specialises per call site, so the four "duplicated" loops in
      `fmt.h` are duplication the optimiser was exploiting.
      `test/system/columns.mjs` now pins what the conversion would have broken.
- [ ] **D2a. Nothing, for the `String` accumulators.** Recorded so it is not
      re-derived: `pkg/query.cpp`'s `emit`, `unzip`'s listing and the sh
      builtins keep theirs. `unzip -l` was converted whole — output
      byte-identical — and cost **9,549 bytes, 25%**, nearly all of it the
      `proc/file.cpp` that `--gc-sections` drops from a binary naming no
      `File`. The general result is that these rows are unbounded text: a zip
      name and a package description do not fit a `Buf<N>`, so the row splits
      into several writes or becomes a `String` per row, and neither can have
      the shape that made `df` and `ls` pay. Handing a finished accumulator to
      a `File` changes nothing — `write_slow_`'s `s.size() >= want_` is the
      same `write_all` — and the naive conversion is worse, since
      `File::stdout()` probes the tty and line mode then flushes a row at a
      time. `emit` is not per-row anyway; `/bin/pkg` has no run in which two
      writes could coalesce, because its multi-write runs are progress
      (`install`'s plan then generation, `update`'s repository then result) and
      the flush points are the feature. The sh builtins are excluded on
      **correctness**: `PIPE_SLOTS` counts writes, not bytes, so an
      accumulator is one slot at any size and a `File` is one per 512 — see D4
      and Release_Notes.md for which shapes hang and which merely cost
      syscalls. The accumulator's one real ceiling is `SYS_STAGE_MAX`, 1 MB,
      above which the write is a clean `Err(NoMemory)`; nothing approaches it,
      and slicing `emit` would answer it before a `File` would.
      `test/system/unzip.mjs` now pins the listing width the conversion would
      have broken.
- [ ] **D3. `Input` and `LineReader` onto `File`.** `File::getline` replaced
      the `LineReader` in `grep` and `tail`; `cp`, `mv`, `less`, `chat` and
      `sh -s` still hold one, so two line readers exist and one of them cannot
      be unwound. Nothing is blocked on it: B3 made both awaiters, so what is
      left is duplication rather than a defect, and unwinding it costs each of
      those five a `File`. `Input` stays either way: it is the files-or-stdin
      decision, which `File` takes rather than makes.
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
      `civil_secs`, `sscanf`, `fnmatch`, `sys/queue.h`. Callers: `le` and
      `iconv` for the wide half, `zip` for the calendar, `le` for `fnmatch`.
      Byte order is done — `<sys/endian.h>` and `<arpa/inet.h>` over clang's
      `<endian.h>`. So is `getenv`, interned per name in a heap block, which
      is what freed the five ports of the aliasing bug; and `mergesort`, BSD's
      stable sort, for the ports that leaned on glibc's `qsort` being one.
      `strtod`/`strtof`/`atof` are still declared in `<stdlib.h>` and defined
      nowhere, which is a link error rather than a diagnostic at the call
      site: implement them over `parse_f64`, or mark them `unavailable` the
      way `<stdio.h>`'s blocking names are. `sscanf` has the same hole.
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
