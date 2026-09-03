# Braam — Concept

An interactive, CLI-oriented operating system that runs entirely inside a
browser tab, written from scratch in freestanding C++20 and compiled to
WebAssembly.

This document is the **top-level design**: the principles, and the approach that
follows from them. It does not describe individual commands, syscalls or
source files. Its section numbers are cited from source comments, so amend a
section rather than renumbering it. Where the detail lives:

| Document | What it holds |
|---|---|
| [Release_Notes.md](Release_Notes.md) | why the code is the way it is |
| [System_Calls.md](System_Calls.md) | the kernel↔process mechanism, operation by operation (§4.3) |
| [Shell.md](Shell.md) | the manual for `/bin/sh` (§4.5) |
| [Programming_Manual.md](Programming_Manual.md) | writing a program against the SDK |
| [Package_Management.md](Package_Management.md) | what a package must prove, and how keys are held |
| [Package_Formats.md](Package_Formats.md) | the package, index and anchor grammars |
| [Testing.md](Testing.md) | how the two test suites are organised |

---

## 1. Goal

A kernel, a shell, a filesystem, a terminal and a set of programs, reached by
opening a URL and deployable as a **static site** — no server, no build-time
secrets, no special HTTP headers.

That constraint decides most of the design:

- **No `SharedArrayBuffer`**, so no `COOP`/`COEP` headers, so any dumb static
  host serves it as-is.
- **No Asyncify**, no JSPI, no stack switching of any kind.
- **No Emscripten runtime.** Nothing is linked that we did not write.

Three things are deliberately not goals:

- **POSIX compatibility *of the system*.** No `open`/`read`/`write`/`fork` in
  the kernel or in `src/cmd/`, and no aim to run third-party C *unmodified*.
  That costs the ability to drop in existing C programs, and buys a system an
  order of magnitude smaller.
- **A VT100 emulator.** No ANSI escapes, no `xterm.js`.
- **A general-purpose libc *under* the system.** Only the foundation our own
  code needs.

The first and the last of those are about what the system is *made of*, and
neither has moved. What the SDK ships *beside* it is a **port kit** — an archive
nothing links unless it names it, whose headers are not on the default path,
which adds no operation, moves no `PROC_ABI` and cannot make a program that does
not ask for it one byte larger. Seven ported packages each wrote one by hand
before it existed. A port is still a rewrite and not a recompile: everything
that blocks is a `co_await`, which is Group B of doc/Compat.md and the reason
that document exists.

---

## 2. Organizing principles

Three invariants hold the design together. Nearly every "how should X work?" is
answered by one of them.

### 2.1 Coroutines are processes; the event loop is the scheduler

C++20 coroutines *are* the process abstraction, and the browser event loop *is*
the scheduler. Everything that would block becomes a `co_await`. Nothing blocks,
so nothing needs a stack of its own: a suspended process is a coroutine frame in
a hash map, costing one allocation.

### 2.2 An import never returns data — only accepts a token

Every JS import returns immediately. It accepts a *wake token*, and the result
arrives later through the `wake()` export. The boundary stays uniform, and a new
asynchronous browser API is a ~20-line change on each side.

Three exceptions are sanctioned, each because no promise is involved at any
point: `host_now()`, a clock read; **OPFS sync access handles**, where an
already-open file is genuinely synchronous (§5.2); and `host_random`, where
`crypto.getRandomValues` fills the buffer it is given and returns. A fourth
needs a written justification here. A class of exceptions would be a second
calling convention, and then there are two ABIs and no invariant.

Calls in the other direction are exports rather than exceptions: `ref()` (§3.7)
and `sys`/`sys_async` (§4.3).

### 2.3 The terminal is a cell grid, not a byte stream

The kernel owns a buffer of cells in linear memory and the renderer draws it.
There is no stream of bytes carrying control codes, because there are no control
codes. Colours are struct fields, cursor addressing is array indexing, and a
`curses`-style layout layer is trivial rather than a parser.

**A cell's character is always a codepoint the host can draw**, an invariant
held where cells are written rather than where they are drawn. Malformed UTF-8
decodes to U+FFFD, so `cat` of a binary file is garbage on the screen rather
than a broken tab.

---

## 3. Architecture

```
┌─────────────────────── main thread ────────────────────────┐
│  boot: capability probe, navigator.storage.persist()       │
│  input: KeyboardEvent → {code, mods} → postMessage         │
│  selection: pointer events → cells → clipboard             │
│  render: OffscreenCanvas (transferred to the worker)       │
└───────────────────────────┬────────────────────────────────┘
                            │  postMessage (no SharedArrayBuffer)
┌───────────────────────────┴────────────────────────────────┐
│                   kernel Web Worker                        │
│  ┌──────────────── JS host shim ────────────────────────┐  │
│  │  imports: log, now, present, random, fs, fs_sync, svc│  │
│  │  exports: init, wake, tick, key, resize, ref,        │  │
│  │           sys, sys_async, memory                     │  │
│  │  externref table · OPFS handle table · canvas blit   │  │
│  │  compiled-Module cache · worker pool                 │  │
│  └───────────────────────┬──────────────────────────────┘  │
│  ┌───────────────────────┴──── kernel.wasm ─────────────┐  │
│  │  allocator · core types · Task<T> · scheduler        │  │
│  │  Channel<T> · scheduler jobs · CancelToken           │  │
│  │  screen cells · VFS mount table · console · exec     │  │
│  └──────────────────────────────────────────────────────┘  │
└───────────────────────────┬────────────────────────────────┘
                            │  postMessage: bind, step
┌───────────────────────────┴────────────────────────────────┐
│         Web Worker, one process — every program            │
│  imports: env.memory, kernel.sys, kernel.sys_async         │
│  exports: _start, _resume, _alloc, _free                   │
│  own linear memory, own import closure, own memory cap     │
└────────────────────────────────────────────────────────────┘
```

The kernel runs in a Web Worker and communicates by plain `postMessage`.
Rendering goes to an `OffscreenCanvas` transferred into that worker, so the main
thread stays free and a "reset kernel" button is `worker.terminate()` followed
by a reboot. A runaway program hangs neither: it is a worker of its own (§4.2),
and the kernel is merely waiting for a reply it can stop waiting for.

### 3.1 Toolchain and language subset

Target `wasm32-unknown-unknown`, freestanding. Any clang with that target and
`wasm-ld` will do, because it is used purely as a compiler: none of its runtime
is linked, and of its headers only the freestanding ones — `<stdint.h>`,
`<stddef.h>`, `<stdarg.h>`, `<limits.h>`, `<float.h>`, `<endian.h>` — which
declare no functions. There is no sysroot, so `<stdio.h>` does not resolve.
**No exceptions, no RTTI** —
errors are values, `Result<T, E>` propagated through `co_await` with a `TRY()`
macro rather than by unwinding.

```
clang++ --no-default-config --target=wasm32-unknown-unknown -std=gnu++20 -Os \
    -nostdlib -nostdinc++ -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -mreference-types -mbulk-memory -msign-ext -mmutable-globals -mnontrapping-fptoint \
    -ffunction-sections -fdata-sections \
    -Wl,--no-entry -Wl,--gc-sections -Wl,--stack-first -Wl,-z,stack-size=131072
```

Appendix C explains each flag, and why `--export-dynamic` and
`--allow-undefined` are deliberately absent: adding either back is a regression.
libc++'s `<coroutine>` cannot be used freestanding, so
[src/kernel/coroutine.h](../src/kernel/coroutine.h) shims the `__builtin_coro_*`
intrinsics.

### 3.2 The foundation we own

About 1,500 lines: an allocator — a bump arena plus size-class free lists over
`memory.grow` — and the core types `Str`, `String`, `Vec<T>`, `Span<T>`,
`Result<T, E>`, `Option<T>` and `HashMap<K, V>`. Coroutine frames go through the
allocator, so it is built for that workload (§8.2).

One library is not ours: `braam::math` is musl's libm, vendored, so that a Unix
port has `<cmath>` to link against. A program opts into it, it carries no host
import, and the kernel does not link it.

Beside them, and linked by nothing here, is the opt-in port kit `braam::compat`
(doc/Compat.md), for a program being ported from Unix.

Nothing else is available. There is no libc, `new` is not used, and a
namespace-scope global must be trivially destructible, since a non-trivial
destructor needs `__cxa_atexit`.

### 3.3 The core abstraction

```cpp
template <class T> struct Task {           // lazy, movable, awaitable
    struct promise_type { ... };           // symmetric transfer on final_suspend
};

struct Waiter {                            // a suspension record, in the frame
    std::coroutine_handle<> h;
    CancelState *cancel;
    u32 token;                             // handed to JS, comes back later
    u32 payload_ptr, payload_len;          // what wake() delivered
};
```

The scheduler is a ready queue, a timer queue, and a map of suspended tasks
keyed by wake token. The `Waiter` lives in the suspended coroutine's own frame,
so registering costs no allocation and `wake()` has somewhere to put its
payload. An awaiter takes its token *before* it suspends, so it can tell the
host which token to signal and only then park. `tick()` is the only thing that
ever resumes a coroutine.

A `Task` that answers **without suspending** resumes its awaiter from inside its
own final suspend, on the awaiter's own stack: a loop that calls one per item
never gets back to the trampoline, and the shadow stack grows a frame an item
until it traps. **A per-item step is therefore an awaiter with an `await_ready`
fast path, not a `Task`** — the buffer, the line or the entry that is already in
hand must enter no coroutine at all. `File::get`, `File::getline`,
`LineReader::next` and `TreeWalk::next` are that shape.

### 3.4 The JS boundary

The whole surface is nine exports and seven imports, asserted exactly in
[test/system/abi.mjs](../test/system/abi.mjs) — the names, and each export's
argument count, since an argument added to one of these is drift a matching list
of names would not catch. Drift is a bug.

```
init(heap_base)                         // host -> kernel
wake(token, payload_ptr, payload_len)   // host signals an event
tick(now_ms)                            // drains the ready queue; ms to the next timer
key(term, code, mods) -> u32            // fast path; 0 if the ring was full
resize(term, cols, rows, flags)         // returns the screen descriptor's address
ref(slot, obj)                          // host deposits a JS object (§3.7)
sys(pid, op, a0, a1, a2) -> i32         // a process's synchronous syscall (§4.3)
sys_async(pid, op, token, len) -> i32   // a process's asynchronous syscall (§4.3)

host_now(), host_log(ptr, len)          // kernel -> host, all non-blocking
host_present(term, x, y, w, h)          // one terminal's dirty rectangle
host_fs(op, token, req)                 // storage, async  (§5.2)
host_fs_sync(op, handle, ptr, len, off) // storage, sync   (§5.2)
host_random(ptr, len)                   // entropy, sync   (§2.2)
host_svc(op, token, req, ref)           // host services, async (§6)
```

Storage and services are **multiplexed rather than named per operation**: one
import per *calling convention*. A new operation is an enum value on each side
rather than a new import, so the surface stays fixed while the system grows.

Both asynchronous interfaces share one request record. **The kernel owns that
record for as long as the host may touch it**, which is past a cancelled await.
There is no timer import: the kernel owns the timer queue, so `tick()`'s return
value says when the host must call back.

### 3.5 The screen and the keyboard

```cpp
struct Cell { char32_t ch; u8 fg, bg, attrs, reserved; };   // fg and bg are palette indices

struct Screen {
    u32 magic;                 // 'BSCR', so a mismatched renderer fails loudly (§8.4)
    u32 cols, rows;
    u32 cursor_x, cursor_y;    // cursor_x may equal cols: the wrap is deferred
    u32 cursor_on;
    u32 cells;                 // address of Cell[cols * rows]
};
```

The renderer holds a view over the cell array and blits monospace glyphs to the
canvas. Damage is a dirty rectangle handed to the host once per `tick`. Rows
that scroll off the top are kept in a ring, and a keyboard chord pages a view
over that history by pointing the descriptor at a different block of cells.

**There may be more than one of these, and the host decides how many.** A
*terminal* is a grid and everything sticky about it, plus a console of its own:
a keyboard channel, a pump, a cooked-input pipe, a foreground set and the two
claims below. `resize(term, …)` for a terminal nothing has named yet is what
makes one — the kernel spawns its pump, init starts a `/bin/sh` on it, and every
export and import that touches a grid names which. A page with one canvas
therefore has exactly one shell, a page with two has two
([web/dual.html](../web/dual.html)), and a page with four has four
([web/quad.html](../web/quad.html)), which is `TERM_MAX` and the most there can
be.

**What terminals do not divide is the system.** One scheduler, one heap, one
VFS and one `/home` underneath all of them: a process belongs to a terminal, and
that is what it inherits — which grid it paints by default, which console its
`^C` comes from, and which foreground set it joins. Two *kernels* on a page is
the other arrangement ([web/embed.html](../web/embed.html)), and it divides
everything except the origin's storage, which is the one thing it cannot.

**A process may reach a screen that is not its own.** `Sys::TermOpen` names a
terminal and hands back a *descriptor*, and every terminal operation names one
in its argument — zero being the process's own, so a program that never asks is
the program it was. Write paints text on that grid and Close gives back both
claims, which is what makes one program drive two screens: an emulator with a
console and a panel, an editor started on one screen and painted on another.
What arbitrates two programs on one grid is unchanged — the claims, one holder
per terminal — so opening is free and taking is not. Reading is not a
descriptor's: a terminal's cooked input has one receiver and it is that
terminal's own shell, so keys come through `KeyClaim` and `KeyRead`.

The terminals a page put up, and their sizes, are `/proc/terms` — a machine fact
and therefore a file rather than an operation (§4.3). A page that means a screen
for a program rather than for a shell says `shell: false`, which is
`TERM_NO_SHELL` on `resize`: the pump still runs, since something must hold the
keyboard, and the grid waits for whoever opens it. **Terminal 0's own program is
`/etc/init`'s to name** (§4) — a site whose whole session is one program, with
`shell: false` on the screens that program opens.

**The layout layer over the grid is a library a process binary links**, and the
kernel does not link it at all. A full-screen program paints its own grid in its
own address space and blits the damaged part across with one syscall.

**Input is symmetric.** A normalised `KeyboardEvent` becomes `{code, mods}` and
lands in a channel. **No control characters exist anywhere in the system**: `^C`
is `'c'` with the control modifier set, and the reader decides what that means.
Line editing is a userland coroutine, not a termios state machine.

The one task receiving a terminal's channel is that terminal's console pump,
spawned when the terminal is made and never ending, because something must hold
the keyboard while nothing is running. A program **claims a route through the
pump** rather than taking the keyboard, and each route — raw keys, and the
screen — has **one holder at a time per terminal**. **`^C` cancels the
foreground if there is one, and reaches the claimant if there is not**, which
lets a line editor abandon the line being typed instead of being killed by it.
What the pump does not route it cooks: echo, a line at a time, into the stdin of
whatever is in front. All of that is one terminal's: `^C` on one screen is not
felt on another.

**Selection, copy, paste and the wheel are the page's business, and the kernel
is told nothing.** A drag never reaches wasm, a paste is fed in as a run of
keystrokes, and a wheel notch becomes the scrollback chord. There is no mouse
event in the ABI, because a selection is a *view* over a grid the page can
already read.

### 3.6 Kernel objects

- **`Channel<T>`** — an async MPSC queue with bounded capacity, and **one
  receiver**. This one type is the pipe, the stdin and the IPC.
- **A scheduler job** — a task, a name, a `CancelToken` and the time it was
  spawned, which is all the scheduler keeps. Spawning hands back a pid; killing
  signals the token, and every `co_await` point checks it and unwinds by
  returning, so destructors run. Reading the table back out is what `/proc` is
  made of. There is no `Process` type.

  **Cancellation does not propagate down a tree**, because a job cannot have two
  children parked at once and a pipeline needs exactly that. So a pipeline's
  stages are independent jobs, and the parent-child relationship is put back by
  hand: a destructor in the parent's frame cancels every stage it started. A
  cancelled child does not unwind until the scheduler resumes it, so it must
  touch nothing the parent owns.
- **Filesystem** — an async node tree, not inodes. One interface, split by
  *when* the work can happen rather than by what it does: naming a file may need
  the host and therefore a wake token, an already-open file does not (§5.2).

  ```cpp
  struct Fs {
      virtual Str kind() const;                     // what `mount` prints
      virtual bool writable() const;
      virtual u64 bytes() const;                    // for `df`; 0 when unknown

      virtual Task<Result<Stat>>       stat(Str path);
      virtual Task<Result<Vec<Entry>>> list(Str path);
      virtual Task<Result<u32>>        open(Str path, u32 flags);
      virtual Task<Result<void>>       mkdir(Str path);
      virtual Task<Result<void>>       remove(Str path, bool all);
      virtual Task<Result<void>>       touch(Str path);
      virtual Task<Result<void>>       symlink(Str target, Str path);
      virtual Task<Result<String>>     readlink(Str path);
      virtual Task<Result<void>>       rename(Str from, Str to);

      virtual Result<usize> read(u32 h, u64 off, u8 *buf, usize n);
      virtual Result<usize> write(u32 h, u64 off, const u8 *buf, usize n);
      virtual Result<u64>   size(u32 h);
      virtual Result<void>  truncate(u32 h, u64 n);
      virtual void          close(u32 h);
  };
  ```

  A mount table maps prefix → `Fs`, longest prefix winning, and an open-file
  table above it holds the descriptors. An implementation sees paths already
  relative to its own mount point, so it never has to know where it was mounted.
  A `Stat` is `{kind, size, mtime}`, and a node is a file, a directory or a
  link — no inode, mode, owner or link count. **A store reports a link and
  never resolves one**, because a target may name a path in a different
  filesystem and only the layer above the mount table can see one.
- **Programs are not kernel objects.** Each is a binary of its own (§4).

### 3.7 Holding JS objects

A `Response`, a file handle or a `WebSocket` is held in an **`externref` table**
as a slot index, with no serialisation, wrapped in an RAII handle that frees the
slot in its destructor.

**The table is the kernel's, and JS never indexes it.** The kernel reserves a
slot and publishes the number; the host deposits the object through the `ref`
export; to *use* it, the kernel passes the object back as an argument. JS sees
the object, never the table. An instance's table is part of the instance, so a
process reaches only the objects its own kernel put there.

---

## 4. Process model

**Every program is a binary in its own instance, in a worker of its own.** There
is no in-kernel program, no program registry and no way to write one. A program
gets its own address space, its own capabilities, its own descriptors and a
memory cap the kernel sets, inside a Web Worker holding nothing else — so
`worker.terminate()` ends it without its cooperation. There is nowhere else to
put a process: the build arranges it unasked, and the binary carries no flag
asking for it.

**The shell is not an exception.** `/bin/sh` is a binary that init runs, and
everything a prompt needs it asks for through §4.3. What is left inside the
kernel is the dispatcher those requests arrive at.

**And init is not tied to it.** `/etc/init`, when the boot archive carries one,
is a single line naming the program init runs on terminal 0 instead; absent,
empty or unreadable means `/bin/sh`, which is also what every terminal after the
first runs either way. Nothing else about the arrangement moves: the program is
resolved, entered and respawned by the rules above, it answers to init's own
pid, it is replaced when it *dies*, and the session is over when it *exits*.
Two things do differ. The offer to restore `/bin` and `/etc` from the archive is
the shell's alone — the archive is those two directories, so it is no repair for
a program named from outside them. And **boot stops reporting itself**: the rows
saying what browser, machine and store this is, and the count the unpack came
back with, are braam's own news, and a site that named a program came for the
program. The version line stays, being the record that braam booted at all, and
`/proc/host` answers the rest to anyone who asks. Errors and the upgrade
question are not news and are never withheld. It is a file rather than a mount
option because the JS boundary is fixed (§3.4) and a path is not an enum value.

**A host with no worker to give is waited out, not worked around.** The spawn is
refused with a retryable error and the caller backs off on an ordinary await, so
`^C` abandons it. Instantiating in the kernel's own worker instead would be a
process with no kill switch sharing the kernel's liveness, so a browser that
cannot make a nested worker cannot run Braam.

**A process that loses its worker dies with it, and init replaces the shell**
when it *died* rather than *exited*, bounded so a shell that cannot start does
not loop.

**A command word resolves as function, then builtin, then `PATH`**, and only the
last costs a process. A shell builtin exists for one of exactly two reasons: it
touches the shell *process's own* state, which no syscall shows anyone, or its
whole cost would be the spawn. The clause is closed.

**`PATH` is searched by the kernel, not by the shell.** The search reads one
word out of the environment the spawn carries, so it steers every spawn there is
and not only a typed command. Installed software gets no further clause: a
package manager reaches its programs by putting a directory on `PATH`.

### 4.1 What separate instances buy

- **Address space: isolated, for free.** Two instances have two memories, and no
  instruction reaches outside its own. A wasm pointer is an offset, not an
  address, so there is nothing to forge. Bounds checks enforce it, not page
  tables someone might misconfigure.
- **Capabilities: isolated, if we are careful.** An instance can only call the
  imports supplied at instantiation, and each closure is bound to its pid. A
  process physically cannot issue a syscall as another: it holds no function
  that does so. The same applies to the `externref` table (§3.7).
- **Memory limits: isolated, and a bonus.** A memory's declared maximum is a
  hard ceiling, 1600 pages or 100 MB, and growth simply fails past it. That is an
  rlimit without cgroups. When a process ends, all its memory returns at once.

### 4.2 What they do not buy: CPU time

**`while(1){}` cannot be preempted.** Nothing in the wasm specification allows
it, so address-space isolation and *liveness* isolation are separate problems.
One worker per process makes `worker.terminate()` the kill switch, and it needs
no metering: a step is one more asynchronous host request, so a process that
never answers is a request that never lands. Workers are hired from a small
pool; one that finished its process goes back, and one that was terminated is
gone, which is the point.

### 4.3 The kernel↔process ABI

[src/kernel/sysabi.h](../src/kernel/sysabi.h) is the wire, included by both ends
so neither can drift alone. An ABI number in the binary's custom section makes
amendment safe: a binary whose number is not the kernel's is refused, so a stale
one is a diagnostic rather than a wrong answer.

```
process imports:  env.memory                       // the kernel's, so the cap is the kernel's
                  sys(op, a0, a1, a2) -> i32       // sync ops, immediate result
                  sys_async(op, token, ptr, len)   // async ops, reply via _resume

process exports:  _start(ptr, len) -> i32          // argv then env; 0 = exited, 1 = suspended
                  _resume(token, ptr, len) -> i32  // the same
                  _sig(n)                          // a leaf call: records it, returns
                  _alloc(n) -> ptr, _free(ptr, n)

custom section "braam":  magic, abi, flags, initial_pages, max_pages
```

The coroutine model survives the boundary intact. The process's `co_await`
suspends, control returns out through `_start`/`_resume`, the kernel continues,
and later calls `_resume` with the payload. Reentrant scheduling across an
instance boundary, with no stack switching.

Four rules shape the wire:

- **The kernel never calls a process, and the host never calls one while the
  kernel is on the stack**, or the kernel would run on a heap it is halfway
  through changing. A step is a `postMessage`; a synchronous syscall re-enters
  the kernel at top level instead.
- **A process's pid is written into its import closure, not passed.** That is
  the whole of "a process cannot issue a syscall on behalf of another pid".
- **A process may have several syscalls outstanding**, and the reply says which
  one it answers, so a socket read that never completes cannot starve the
  keystroke behind it.
- **What crosses is bytes, not addresses** (Appendix B).

Two rules keep the operation table from growing on speculation: an operation
must have a caller in the tree, and anything the kernel can publish as a file in
`/proc` gets none. A stream of bytes always comes back as a descriptor, so
`read`, `write` and `close` serve it and nothing is duplicated. The operations
themselves are [System_Calls.md](System_Calls.md).

### 4.4 Cost model

Compilation is expensive; instantiation is cheap. The host caches the compiled
module and instantiates per `exec`, so a binary is compiled once however many
workers run it. Starting a worker is the other cost, and the pool answers it.

**A syscall is the cost that does not go away**: two `postMessage` hops and two
copies, measured at 34–45 µs in three engines. What that leaves on the
interactive path is the *line* rather than the keystroke, which is why it is
affordable.

**The real cost is duplication.** With no dynamic linking, every binary embeds
its own allocator, string types and coroutine runtime. Keep the process-side
runtime minimal and push anything substantial into a syscall, so it lives once
in the kernel rather than once per program.

### 4.5 The shell's language

`/bin/sh` is a Bourne shell, and the reference for every decision in it is v7's.
It is a program under §4 like any other: nothing in it is a kernel concept, and
the syscall table gained no operation for any of it.

Three things v7 has cannot exist here, and each has a substitute rather than a
gap. There is no `fork`, so `( list )` runs in the shell process with its own
mutable state saved and put back, losing only memory isolation. A compound
command cannot be backgrounded, because nothing inside a process can wait for a
sibling task. And `exec cmd` spawns and waits instead of replacing the image.

**Tab completion is the shell's, and needs no operation.** The line editor is a
userland coroutine under §3.5, so it may `co_await` a directory listing between
two keystrokes; the candidates come from the shell's own tables and from `PATH`
through `Sys::List`, which globbing already calls. Nothing about the keyboard
changes: `Tab` is a named key like any other, and what it means is the reader's.

The grammar, the expansions, the builtins and the job control are
[Shell.md](Shell.md).

---

## 5. Storage

Appendix A compares the browser's storage APIs and the durability caveats.

### 5.1 The mount layering

```
OpfsFs     → OPFS                  (/, and therefore everything — the store)
ProcFs     → the scheduler         (/proc, generated at open)
DevFs      → host_random, ChaCha20 (/dev, generated at read, dropped at write)

unbuilt:
  a File System Access Fs          (a real local directory, opt-in — §5.4)
  an Fs over Range requests        (read-only remote trees)
```

**Three mounts, and two of them are generated.** Everything a user can name is
in the one store: `/bin`, `/etc`, `/home`, `/tmp`, `/import` and `/pkg` are
directories in it, not filesystems of their own. There is no `/usr` and no
`/mnt`, because a directory named for mounting would promise a second filesystem
there is no way to have.

`/proc` publishes what the scheduler knows, and `/dev` makes its bytes at the
moment they are read. Publishing kernel state as *files* makes `cat` and `grep`
the introspection tools, with no second interface to keep in step, and it is
part of why the process ABI is as small as it is. A generated file's content is
produced at `open`, so one read cannot describe two different moments.

**Every process has a working directory of its own**, inherited at spawn and
moved only by its own `chdir`, which is the whole of why `cd` is a builtin. What
a process is *not* isolated in is the namespace: there is no per-process root.

### 5.2 OPFS is the primary store

The Origin Private File System is private to the origin, invisible in the user's
regular filesystem, and supported by every current engine. It gives real
directory and file handles, seekable reads and writes, truncate, rename and
remove, which maps onto §3.6's interface almost one-to-one.

The detail that decides the architecture: the high-performance **synchronous**
access handles are exposed **only inside a Web Worker**. The kernel already
lives in one, so the fast path comes free. *Opening* a file is asynchronous and
costs a wake token; the operations on an open handle return immediately, which
is §2.2's second exception.

Two properties of the store shape everything above it:

- **A sync access handle takes an exclusive lock**, so the VFS keeps an
  open-file table, holds one backend handle per file and shares it by reference.
  Offsets live above the VFS, so two opens cannot disturb each other's position.
- **The store has nowhere to put a type or a mode**, so anything the format does
  not offer is Braam's own convention over what it does: a symbolic link is a
  small file the store agrees to read as one. There are no permissions, no
  owners, and no atomic exclusive create.

**The store is unpacked from an archive shipped beside the kernel, and
stamped.** An empty store is filled at boot without asking; after that the stamp
is compared against the running kernel's version, and a mismatch is the user's
decision. **The archive, not the store, is what the system can be recovered
from**, which is what holds up a writable `/bin`.

OPFS is unavailable in Safari private browsing. Capability-detect and **stop**:
with the whole namespace in one store there is nothing to fall back to, and a
memory namespace that looks like a system until the tab is reloaded is worse
than a refusal.

### 5.3 Capability struct, not probing

The kernel asks once, at boot, and keeps the answer:

```cpp
struct StorageBackend {
    bool opfs, sync, fsaccess, persisted;
    u64  quota, usage;
};
```

Callers consult this rather than probing at use time. It arrives as the reply to
one operation rather than through an export of its own, which keeps the boundary
to §3.4's imports and lets `df` ask again for a fresh figure.

`persisted` is the one field a worker cannot obtain, because the API exists only
on the main thread (§A.2). The page calls it during boot and posts the answer
down, and boot waits for it under a bound. Storage semantics are therefore
inspectable from inside the OS instead of being invisible browser behaviour.

### 5.4 The real local filesystem, and the escape hatch

A directory picker yields a handle to an actual folder on disk, read-write,
after an explicit user gesture and permission grant. Its reach is limited
(§A.3), so it is strictly progressive enhancement. **This is unbuilt.** A mount
is an operation on the wire and its caller exists, but the operation refuses:
there is no factory turning a name into a filesystem, and §5.1 leaves open what
a mount by one process should mean to every other.

The universally available escape hatch is the boring one, and it is built: a
file input for import, and a Blob download for export. Both live on the **page**
rather than in the worker, because a picker and a download need the DOM.

---

## 6. Host services

Everything the browser offers that is not storage and not the terminal reaches
the kernel through the one `host_svc` import (§3.4), as an operation on the
shared request record, with the object it acts on passed alongside as an
`externref`. Naming an import per operation is not the style: a new service is
an enum value on each side.

Behind it today: `fetch`, WebSockets, the clipboard, file import and export, the
wall clock, the host's description of itself, signature verification, raw
inflate, and the process operations of §4.3: compiling a binary, instantiating,
stepping and killing it. Two conventions run through all of them:

- **A stream of bytes comes back as a descriptor**, so `read`, `write` and
  `close` serve it and nothing is duplicated. A fetched body is read like a
  file, and a killed process drops all of them with its handle table.
- **Every one is a promise on the host side**, so every one takes a wake token
  and §2.2 is untouched.

Anything needing the DOM is relayed by the page and answered by id, which is
invisible from the kernel: a service operation is a token either way.

Reading the clipboard is the one thing that does not fit. A browser permits it
only inside a user-gesture handler, and a command's request reaches the page
after that handler has returned. The way out is that **a paste is itself the
gesture**, so a refused read becomes a wait for one and the kernel must not eat
the keystroke that produces the event (§3.5).

---

## 7. Repository layout

The kernel is the bottom tier. `braam_fs` and `braam_svc` are siblings above it
and below userland, depending on neither each other nor upwards. `braam_ui` and
`braam_math` are in neither hierarchy, and the kernel does not link them at all,
because the programs that paint and calculate are binaries. `src/proc/` is a
*different binary's* runtime, sharing a few headers with the kernel, not code.

| Path | What is in it |
|---|---|
| `doc/` | this document, the release notes, and the manuals |
| `Makefile`, `CMakeLists.txt`, `cmake/` | the build, and the wasm32 toolchain file |
| `src/kernel/` | allocator, core types, `Task`, scheduler, `Channel`, screen, both ABIs |
| `src/fs/` | the `Fs` interface, paths, the VFS, the filesystems, the storage ABI |
| `src/svc/` | the host services of §6 |
| `src/ui/` | the layout layer over a `Grid` (§3.5) |
| `src/math/` | musl's libm, vendored (§3.2) |
| `src/user/` | exec, the syscall dispatcher, the console, pipes, `/proc`, boot and init |
| `src/proc/` | a process binary's whole runtime: `_start`, syscalls, stdio |
| `src/cmd/` | one file per program; the shell and the package manager are directories |
| `rootfs/` | what the boot archive carries: `/bin`, `/etc`, `/README` |
| `examples/` | the SDK's worked example, and an ordinary build target |
| `test/` | the two suites, the Node driver, and the fakes they run against |
| `web/` | the embedding API, the workers, the host shim, the renderer |
| `tools/` | build scripts, and the ones a package publisher signs with |

---

## 8. Things to get right

### 8.1 Every awaitable is cancellation-aware
Retrofitting cancellation into coroutine code is painful. `CancelToken`
participates in every `await_suspend`, and every awaiter deregisters in its
destructor, which is what makes destroying a suspended frame safe. A parking
awaitable with no destructor is a use-after-free.

### 8.2 Coroutine frame allocation is the hot path
Frames are heap-allocated per call, so the allocator is built with this as its
primary workload. A frame past 512 bytes costs a whole 64 KiB span, so
long-lived state belongs in a heap block the frame points at.

### 8.3 Never let an import return data synchronously
Beyond the sanctioned exceptions (§2.2). One exception is pragmatic; a class of
them is a second ABI.

### 8.4 `memory.grow` detaches the `ArrayBuffer`
Any cached `Uint8Array` view goes dead after a growth. Route JS-side access
through an accessor that re-derives, and make a mismatch fail loudly — the
`Screen` magic word is there for this. A host request may likewise outlive the
coroutine that issued it, so anything whose address crosses to JS is a heap
record the kernel keeps alive past a cancelled await, never a frame buffer.

### 8.5 Safari's 7-day eviction is a real hazard
With cross-site tracking prevention on, an origin that sees no user interaction
for seven days of browser use has all script-created data deleted. Mitigations:
request persistence, encourage Add to Home Screen — installed web apps are
exempt from the timer — and make export easy.

---

## Appendix A — Browser storage APIs

### A.1 The tiers

| API | Shape | Where it runs | Our use |
|---|---|---|---|
| **OPFS** | Real files and directories, origin-private, invisible to the user | Async API anywhere; **sync** handles worker-only | **Primary store** |
| **IndexedDB** | Async key → blob, transactional | Anywhere | Metadata; stashed directory handles |
| **Cache API** | Request → Response pairs | Anywhere | Unused: the boot archive is fetched at most once per version |
| **localStorage** | 5 MB, sync, strings only | Main thread only | Tiny config, nothing else |
| **File System Access** | The *actual* user disk, with a picker | Chromium desktop only | Optional, unbuilt (§5.4) |

### A.2 Durability

Storage is **best-effort by default**, meaning it can be deleted without asking.

- An origin can opt into persistent mode via `navigator.storage.persist()`,
  after which data is evicted only if the user deletes it. **That call is not
  available in Web Workers** — the main thread makes it during boot and passes
  the result down (§5.3).
- Quotas are generous but finite. `navigator.storage.estimate()` is what `df`
  reports.
- **Eviction is all-or-nothing per origin.** OPFS, IndexedDB and Cache go
  together, so there is no point using one as a backup of another.

### A.3 File System Access reach

Firefox and Safari ship only OPFS, and no mobile browser exposes the pickers.
Safari supports none of the picker APIs on macOS, iPadOS or iOS. Hence
progressive enhancement only, never a dependency.

---

## Appendix B — Cross-instance data movement

Instances cannot call each other, so every transfer is a copy through the host.
The kernel cannot be handed a buffer it did not allocate, so the host asks for
one: a synchronous syscall the *host* issues on the process's behalf returns the
address of a staging block. The reverse direction needs no such call, because
`_alloc` is already in the ABI.

A process is a worker away, so the copy is in two halves with a `postMessage`
between them. Both halves must copy rather than take a view: a view is detached
by the next `memory.grow` (§8.4), and a transferred one cannot be re-derived.

**If the kernel itself is ever to do the copy, multi-memory is the tool.** A
module may declare several memories, and `memory.copy` moves bytes between two
of them. Imports are fixed at instantiation, so the trick would be a tiny
per-process bridge module importing both.

---

## Appendix C — Toolchain notes

Verified against a stock clang for `wasm32-unknown-unknown`, which has no
sysroot of its own.

### C.1 libc++'s `<coroutine>` cannot be used freestanding

`<coroutine>` is header-only and compiler-intrinsic, so it ought to work
freestanding as soon as `operator new` exists. That is true of the *language
feature* but not of the header: libc++'s `<coroutine>` reaches `<cstring>` and
`<cmath>` by a chain of its own, and those need libc declarations the bare
`wasm32-unknown-unknown` target has no sysroot for. A distribution carrying a
wasm sysroot carries it per target, and none has an `unknown-unknown` variant.

### C.2 The shim

[src/kernel/coroutine.h](../src/kernel/coroutine.h) declares
`std::coroutine_traits`, `std::coroutine_handle`, `std::suspend_always` and
`std::suspend_never` over the `__builtin_coro_*` intrinsics. `coroutine_traits`
must be *defined*, not merely declared: a forward declaration compiles until the
first coroutine, which then fails to instantiate it.

### C.3 The flags in §3.1

- **`--export-dynamic` is absent**, and adding it back is a regression: it is
  not a reliable way to export. Exports and imports are named individually with
  attributes, never by linker flag. Either changes the ABI, so the expected
  surface in [test/system/abi.mjs](../test/system/abi.mjs) changes in the same
  commit.
- **`--allow-undefined` is absent**, so nothing is left to resolve and an
  accidental libc dependency is a link error rather than a runtime trap. There
  is no compiler-rt for this target, so a needed builtin is a link error too.
- **The wasm features are named, not defaulted**, because which of them the
  default CPU turns on has changed between clang versions.
- **`--no-default-config`** suppresses any config file a distribution ships,
  which is how a sysroot gets injected. **`--stack-first`** puts the shadow
  stack below the data segment, so an overflow traps rather than corrupting
  globals.
- **`-std=gnu++20`** rather than `c++20`, because `TRY()` is a statement
  expression. **`MinSizeRel`** because at `-O0` a freestanding build calls
  libcalls nothing provides.
