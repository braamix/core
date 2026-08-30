# The port kit

`braam::compat` is an opt-in C library for **ported** programs. Nothing in this
tree links it: the kernel has no libc, `src/cmd/` has no libc, and the kit's
system header names are not on `braam::flags`' include path. A program that does
not ask for it is byte-for-byte what it was.

It exists because seven ported packages each wrote one. That was ~4,000 lines,
of which `le` and `uemacs` shared 381 identical lines, five copies carried the
same `malloc` header and the same `getenv` aliasing bug, and six `printf`
engines disagreed about `%ld`, precision, `%*d` and the return value. This
document is what the kit decided instead.

## Asking for it

```cmake
braam_add_program(NAME zip SOURCES zip.cpp ... PORT)
```

`PORT` is the whole of it. It links the kit, puts the system header names on
**this target's** include path and no other's, and applies `-fno-builtin` once —
replacing the 26-, 30-, 40- and 45-entry `-fno-builtin-<name>` lists four
packages carry. It also links `braam::portflags`, which silences the seven
warnings upstream code trips that this tree's own code does not.

Without `PORT`, `#include <string.h>` is still "file not found". That is the
guard, and it is tested.

## Three groups

**Group A — pure computation.** Exact C signatures and semantics, drop-in:
`mem*`, `str*`, `ctype`, `malloc`/`calloc`/`realloc`/`free`, the `strtol`
family, `qsort`/`bsearch`, `snprintf`/`vsnprintf`/`sprintf`, `errno` and
`strerror`. Group A has no syscall, which is why `braam_compat_pure` links into
`tests.wasm` the way `braam_math` does: a syscall in it is a link error.

**Group B — blocking.** Streams, descriptors and directories. These get **no C
signatures**, because on this system a C signature cannot block: everything is a
coroutine, and there is no Asyncify, no JSPI and no stack switching. They get a
`b_` prefix and an awaitable return, with C's *error conventions* kept so the
surrounding code does not change:

```c
    if ((c = fgetc(f)) == EOF)          →  if ((c = co_await b_fgetc(f)) == EOF)
```

`<stdio.h>` declares the blocking names `__attribute__((unavailable(…)))`, so
one build hands a porter every call site with its replacement named. `zip` found
its 1208 `co_await`s by hand.

**Group C — absent.** `setjmp`/`longjmp` (no restorable value stack; `vi`'s
`error()` unwinding one frame at a time is the recipe), `fork`, `setenv` (argued
and rejected — doc/TODO.md), `pthread_create`, `dlopen` (a static module table:
`iconv`'s `citrus_module.cpp`), `mmap` (read into a heap block:
`citrus_mmap.cpp`), signal handlers, `utime`, `chmod`, µs and CPU clocks.
`exit()` and `abort()` trap: a coroutine cannot exit through a return, so return
a status from `proc_main`.

## Where this differs from C, deliberately

- **`strtol` takes `0b` in base 0.** A GNU extension, kept so `strtol` and
  `File::scan_i64` do not disagree about the same string.
- **`qsort` is not stable.** Heapsort: O(n log n) worst case and O(1) extra
  memory, so it cannot fail where there are no exceptions, and iterative, so it
  cannot overflow the 128 KiB shadow stack. glibc's is stable in practice; a
  port leaning on that must be found now.
- **`strerror` returns the POSIX *name***, `"ENOENT"`, mirroring `error_name()`
  in `kernel/result.h`. Every byte of English prose a Unix libc spends here
  stays unspent.
- **`errno` numbers are musl's**, the dialect already vendored in
  `src/math/musl/`. `errno_of`/`error_of` in `compat/cerr.h` are the one bridge
  to `Error`. `Error::Cancelled` and `Error::Intr` both map to `EINTR`: both mean
  "abandoned by a signal", and the difference is not expressible in errno.
- **`malloc(0)` and `realloc(p, 0)` return a real block**, never null, so a port
  that reads null as failure cannot mistake success for it.
- **`realloc` never shrinks.** Capacity comes from `heap_usable_size`, so it is
  never stale, but a 1 MB block realloc'd to 1 KB keeps its spans.
- **`PATH_MAX` is 512** — `FS_BLOCK`, `FILE_BUF` and the allocator's top small
  size class, so a `char[PATH_MAX]` is exactly one block. The number is not the
  constraint; the placement is. Put one in a heap block, never in a coroutine
  frame.
- **No `long double`.** It is 113-bit quad here and every operation on one is a
  compiler-rt link error. `%Lf` is accepted and read as `double`.

## What it costs

Measured, `MinSizeRel`, against `examples/hello` at 7,769 bytes:

| | bytes | over hello |
| --- | --- | --- |
| `examples/portlet` — `qsort`, `strtol`, `strdup`, `ctype`, `malloc` | 10,491 | +2,722 |
| `snprintf`, integers and strings only | 11,255 | +3,486 |
| `snprintf` including the float conversions | 16,622 | +8,853 |

The float arm is **5,367 of those bytes**, and a port pays it even for `%d`
alone, because the engine is one function and `--gc-sections` works at function
granularity. Splitting it into a translation unit of its own is the obvious
saving and is **not done**: a weak reference does not pull an archive member, so
the split would make `%f` silently print nothing in a port that forgot to ask
for it. A wrong answer is worse than 5 KB. `doc/TODO.md` P5 carries the shape a
fix would need.

A program that does not name `braam::compat` pays **zero** — no archive, no
header, no flag.

## Never link it

`benchmarks/dhrystone` defines its own `strcpy`/`strcmp`/`strlen` on purpose:
they are what it *measures*, and `dhry_lib.cpp` keeps them in a translation unit
of their own so the call is what it was when it came from libc. Linking the kit
would shadow them and invalidate the benchmark.

`converters/iconv`'s `citrus_bcs_strtol.cpp` is citrus's own private scanner,
not the C library, and stays upstream.

## A port links the kit or keeps its headers, never both

`converters/iconv/include/` and `editors/le/cinc/` answer the same names. Which
`-I` wins is silent. A migration deletes the package's own header set in the
same commit as it adds `PORT`.
