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
was blocked on a syscall, and sections A, B and D are all spent: the program
layer is neither short of coverage nor known to be wrong, nor short of the
buffered stream D asked whether it wanted, and **what is left is the port
kit**, P. What adding a program costs is [Testing.md](Testing.md) §6, not a
section here.

---

## P — the port kit

`braam::compat` ([doc/Compat.md](doc/Compat.md)) is in, and `PORT` is the whole
of the opt-in. No entry here moves `PROC_ABI` or adds an operation; the caller
each names is a **ported package**, since the kit exists for callers outside
this tree.

- [ ] **P5. Make the float arm droppable.** It is 5,367 of `snprintf`'s 8,853
      bytes and every port pays it for `%d` alone, because the engine is one
      function. A separate TU behind a weak reference does not work — a weak
      reference does not pull an archive member, so `%f` would silently print
      nothing. Wants an OBJECT library the port names, or a strong reference
      only the float path emits.
