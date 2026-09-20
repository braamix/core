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

## 1. Asking for it

```cmake
braam_add_program(NAME zip SOURCES zip.cpp ... PORT)
```

`PORT` is the whole of it. It links the kit, puts the system header names on
**this target's** include path and no other's, and applies `-fno-builtin` once —
replacing the 26-, 30-, 40- and 45-entry `-fno-builtin-<name>` lists four
packages carry. It also links `braam::portflags`, which silences the seven
warnings upstream code trips that this tree's own code does not.

`PORT NOFLOAT` is the one modifier: it drops `snprintf`'s float conversions,
about 5 KB a port that formats only integers and strings would otherwise pay.
`%f` in such a program **traps** with a message naming the flag — it does not
print nothing. `zip`, `uemacs` and `iconv` are built that way; `le`, `duremark`
and `simbesm` format floats and are not. §5 has the numbers.

Without `PORT`, `#include <string.h>` is still "file not found". That is the
guard, and it is tested.

## 2. Three groups

**Group A — pure computation.** Exact C signatures and semantics, drop-in:
`mem*`, `str*`, `ctype`, `malloc`/`calloc`/`realloc`/`free`, the `strtol` and
`strtod` families, `qsort`/`mergesort`/`bsearch`,
`snprintf`/`vsnprintf`/`sprintf`, `errno`, `strerror`, `getenv`, the calendar
(`<time.h>`), the wide half (`<wchar.h>`, `<wctype.h>`), `fnmatch`,
`<sys/queue.h>`, `<regex.h>`, `<zlib.h>`, `<bzlib.h>`, `<lzma.h>` and
`<zstd.h>`. Group A has no syscall, which is why `braam_compat_pure` links into
`tests.wasm` the way `braam_math` does: a syscall in it is a link error.

Group A does reach the tree's *pure* primitives and leaves them undefined in the
archive, for the final link to answer: `cenv_intern.cpp` calls `heap_alloc`,
`ctime.cpp` calls `civil`/`civil_secs`, `cwchar.cpp` the UTF-8 codec and
`cwctype.cpp` `rune_lower`/`rune_upper`. A `PORT` binary resolves all four
through `braam::proc`, and `tests.wasm` through `braam_core` and the
compiled-in `proc/time.cpp`.

`getenv` is the one member with a foot outside: the environment block is
`braam_proc`'s. So it is split — the interning, which is what is worth testing,
is `braam_compat_pure`'s `cenv_intern.cpp`, and the four lines over `proc_env`
are `braam_compat_proc`, which `braam::compat` links and `tests.wasm` does not.

**Group B — blocking.** Streams, descriptors and directories. These get **no C
signatures**, because on this system a C signature cannot block: everything is a
coroutine, and there is no Asyncify, no JSPI and no stack switching. They get a
`b_` prefix and an awaitable return, with C's *error conventions* kept so the
surrounding code does not change:

```c
    if ((c = fgetc(f)) == EOF)          →  if ((c = co_await b_fgetc(f)) == EOF)
```

`<stdio.h>`, `<sys/stat.h>`, `<fcntl.h>`, `<unistd.h>`, `<dirent.h>` and
`<poll.h>` declare the blocking names `__attribute__((unavailable(…)))`, so one
build hands a porter every call site with its replacement named. `zip` found
its 1208 `co_await`s by hand. §4 is the family itself.

`<sys/cdefs.h>` carries the three macros those six headers diagnose with, so
there is one copy rather than six. Beside `BRAAM_BLOCKS`, `BRAAM_RENAMED` is
for what the buffer or the listing already answers — `feof`, `ungetc`,
`readdir` — which keeps C's shape and only moves its name, and `BRAAM_ABSENT`
is what the kit has decided not to supply: `sscanf` and `vsscanf`, which name
`kernel/text.h`'s scanners, `<time.h>`'s `time`, `clock`, `localtime` and
`ctime`, which name `clock_now()` and `proc_now()`, and `<unistd.h>`'s `fork`,
`pipe`, `exec*`, `dup2`, `getpid`, `sleep` and `sbrk`, which name the
`proc/io.h` call that does answer them. Each was declared and defined nowhere
before, so a caller met a link error naming a mangled symbol; now the compiler
answers at the call site.

**Group C — absent.** `setjmp`/`longjmp` (no restorable value stack; `vi`'s
`error()` unwinding one frame at a time is the recipe), `fork`, `setenv`
(argued and rejected — Release_Notes.md, and its absence is what lets `getenv`
intern an answer once and never revisit it), `pthread_create`, `dlopen` (a
static module table: `iconv`'s `citrus_module.cpp`), `mmap` (read into a heap
block: `citrus_mmap.cpp`), signal handlers, `utime`, `chmod`, µs and CPU clocks.
`exit()` and `abort()` trap: a coroutine cannot exit through a return, so return
a status from `proc_main`. `sscanf` is here too, argued below.

Beside those, three headers exist because a port asks for them by name and the
answer is not the kit's own code: `<math.h>` is `braam::math` under the name C
uses — a `PORT` target links it through the kit, so `sqrt()` needs nothing
further asked for; `<fenv.h>` is a degenerate stub, honestly so, since wasm has
no floating-point environment; `<sys/endian.h>` and `<arpa/inet.h>` carry the
BSD spelling and the `htonl` four. **`<stdint.h>`, `<stddef.h>`, `<stdarg.h>`
and `<float.h>` are clang's** freestanding headers and the kit does not shadow
them — `<float.h>` least of all, since `src/math/` overrides `LDBL_*` for its
own vendored sources and a port must not inherit that lie. `<endian.h>` and
`<limits.h>` are wrappers of the same shape: `#include_next` when the compiler
has one, the names derived from its predefines when it does not, since a
freestanding `<endian.h>` arrived only in clang 23.

## 3. Where this differs from C, deliberately

- **`strtol` takes `0b` in base 0.** A GNU extension, kept so `strtol` and
  `File::scan_i64` do not disagree about the same string.
- **`qsort` is not stable.** Heapsort: O(n log n) worst case and O(1) extra
  memory, so it cannot fail where there are no exceptions, and iterative, so it
  cannot overflow the 128 KiB shadow stack. glibc's is stable in practice, and
  a port leaning on that changes the call to **`mergesort`**, BSD's name for
  the stable one — bottom-up, so it is iterative too, and with BSD's error
  convention: `0`, or `-1` with `errno` `EINVAL`/`ENOMEM`. It allocates a
  block of `n * size`, which is why it is a separate name rather than a
  stabler `qsort`: a caller has to be able to see it fail. `--gc-sections`
  keeps it out of a binary that does not name it.
- **`getenv` interns per name**, in a heap block that outlives every later
  call. Two live results never alias and a long value is never truncated —
  five ports each answered out of one `static char val[512]` and shared both
  bugs.
- **`time_t` is 64-bit**, as musl's is on a 32-bit target. `zip` had picked
  `i64` and `le` `long`; the kit takes the one that does not end in 2038.
- **`mktime` is `timegm` plus `tm_gmtoff`.** There is no local zone in a pure
  group: the offset comes from `clock_now()`, and that is a coroutine. So
  `struct tm` carries BSD's `tm_gmtoff` and `tm_zone`, `mktime` reads the
  fields through the first of them, and `time`, `clock`, `localtime` and
  `ctime` are `unavailable` rather than lies. `tm_isdst` is always 0.
- **`sscanf` is not supplied.** Every conversion the ports actually used has a
  function of its own in `kernel/text.h` and `proc/file.h` — `scan_i64`,
  `scan_u64`, `scan_token`, `scan_until` — and a format string defeats every
  check the compiler could make. Fifteen former call sites across `le` and
  `zip` are already rewritten that way, and `zip`'s own note says it: a general
  one would be the rest of stdio for two call sites.
- **`mbstate_t` holds a split sequence**, `{ unsigned char buf[4]; unsigned
  char len; }`, which is `iconv`'s and not `le`'s placeholder: Citrus keeps one
  per conversion in `sc_mbstate` and feeds it a batch at a time.
- **A malformed sequence is `EILSEQ`, never U+FFFD.** `utf8_decode` answers
  U+FFFD for every malformed form, which is right for a screen and wrong for a
  codec, so `mbrtowc` and `mbtowc` re-encode what came back and compare bytes.
  A real U+FFFD in the input therefore survives as a rune — the distinction
  `le` approximated with a test on the lead byte.
- **`wcwidth` is Markus Kuhn's, and the grid is not.** It reports Unicode's
  width, 2 for East Asian Wide and Fullwidth; the terminal is one `Cell` per
  rune (`kernel/screen.h`), so a port doing column arithmetic with it will
  disagree with the screen about a wide character. It is the one place the kit
  answers for C rather than for this system, and `src/compat/cwidth.cpp` is
  vendored data beside `src/math/` for that reason.
- **`iswalpha` and its family are `rune_lower`/`rune_upper`'s coverage** —
  ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic have case — plus a short
  table of the letter blocks that have none. Not full Unicode, and `<wctype.h>`
  says so rather than implying otherwise.
- **`fnmatch` honours `\`**, as C does with flags 0 and as `le`'s own copy did
  not. It carries the whole flag set including the POSIX character classes,
  because `[[:digit:]]` parsed as an ordinary bracket is a silently wrong
  answer. It is not `sh`'s `glob_match`: that one is `braam_sh`'s and takes the
  expander's quoting mask where `fnmatch` takes flags and a backslash.
- **`<regex.h>` is `braam::regex`** under the name C uses, which the kit carries
  so a `PORT` target asks for nothing: `regcomp`, `regexec`, `regerror` and
  `regfree`, leftmost-longest, ERE and BRE, with `\1` in both. It is the
  system's own library and not a Group A translation unit — the flag surface,
  the two departures from POSIX and what it costs are in
  Programming_Manual.md §6.
- **`<zlib.h>` is zlib 1.3.2.1's C API over `braam::zlib`**, and deflate's
  output is zlib's, byte for byte. The API includes:
  - `z_stream`, `deflateInit2`, `inflateInit2` and every flush;
  - dictionaries, `gz_header`, `deflateParams`, `inflateSync` and the copies;
  - `compress2` and `uncompress2`, with zlib's return codes;
  - `crc32` and `adler32` with their `_combine` forms.

  `czlib.cpp` is an adapter only. A `z_stream`'s state holds the library's
  `Deflater` or `Inflater`, and the stream's fields are copied in before each
  call and out after. Four things differ from zlib:
  - `zalloc`, `zfree` and `opaque` are accepted and never called; the heap
    serves every allocation.
  - The whole of `gz*` is `BRAAM_ABSENT`, a compile error at the call. A gzip
    file is `inflate()` with `windowBits` 31 over bytes `b_read` gave, or 47 to
    take zlib too.
  - `inflateBack*`, `inflateResetKeep` and `get_crc_table` are absent as well.
  - `zlibCompileFlags()` sets bit 16, "no gz* compression".
- **`<bzlib.h>` is libbzip2 1.0.8's C API over `braam::bzip2`**, and its output
  is libbzip2's, byte for byte. The API includes `bz_stream` with
  `BZ2_bzCompress` and `BZ2_bzDecompress` and their `Init` and `End`, and the
  two `BuffToBuff` one-shots, all with libbzip2's return codes.

  `cbzlib.cpp` is an adapter only, as `czlib.cpp` is, and three things differ
  from libbzip2:
  - `bzalloc`, `bzfree` and `opaque` are accepted and never called.
  - `verbosity` is accepted and prints nothing.
  - `BZFILE` and everything taking one, `BZ2_bzRead` to `BZ2_bzerror`, is
    `BRAAM_ABSENT`. A `.bz2` file is `BZ2_bzDecompress()` over bytes `b_read`
    gave. After `BZ_STREAM_END`, while bytes remain, it is `End` and `Init`
    again, since `bzip2` may write one stream after another.
- **`<lzma.h>` is liblzma 5.8.4's own header**, over `braam::lzma`, which is
  liblzma vendored verbatim rather than rewritten. There is no adapter: the
  header forwards to `lzma/lzma.h`, and every call is liblzma's. The whole API
  is there, including `lzma_stream`, the easy, raw and buffer encoders and
  decoders, filter chains and `lzma_str_to_filters`, the index, and `.lzma`
  and `.lz`. Three things differ from a threaded build:
  - `lzma_stream_encoder_mt`, `lzma_stream_encoder_mt_memusage` and
    `lzma_stream_decoder_mt` are each a compile error at the call. The
    single-threaded `lzma_stream_encoder` and `lzma_stream_decoder` make the
    same streams, less the block sizes the threaded encoder records.
  - `lzma_physmem()` and `lzma_cputhreads()` answer 0, liblzma's own answer on
    a system it cannot ask.
  - A null `lzma_allocator` means the heap, so a buffer liblzma hands back, as
    `lzma_str_from_filters` does, goes to the kit's `free()` as usual.
- **`<zstd.h>` and `<zstd_errors.h>` are libzstd 1.6.0's own headers**, over
  `braam::zstd`, vendored verbatim as liblzma is. There is no adapter, and the
  whole API is there: the one-shots, `ZSTD_compressStream2` and
  `ZSTD_decompressStream`, every parameter, prepared dictionaries, skippable
  frames, and the static-linking half behind `ZSTD_STATIC_LINKING_ONLY`. What
  a full build has and this does not:
  - `ZSTD_c_nbWorkers` accepts 0 alone, as in any build without
    `ZSTD_MULTITHREAD`.
  - `<zdict.h>` is absent: there is no dictionary builder, though a dictionary
    built elsewhere loads as usual.
  - Frames from before 1.0 (magic numbers `0xFD2FB521` to `0xFD2FB527`) are
    refused as `prefix_unknown`.
  - A null `ZSTD_customMem` means the heap. libzstd frees what it allocates
    itself and hands back nothing for `free()`.
- **`strerror` returns the POSIX *name***, `"ENOENT"`. Every byte of English
  prose a Unix libc spends here stays unspent. It is **not** `error_name()` in
  `kernel/result.h`, which answers prose — `"not found"` — and is what the rest
  of the system prints; a port that wants a diagnostic to read like this
  system's writes `error_name(error_of(errno))` over `compat/cerr.h`'s bridge,
  as `vi`'s `syserror()` does.
- **`errno` numbers are musl's**, the dialect already vendored in
  `src/math/musl/`. `errno_of`/`error_of` in `compat/cerr.h` are the one bridge
  to `Error`. `Error::Cancelled` and `Error::Intr` both map to `EINTR`: both mean
  "abandoned by a signal", and the difference is not expressible in errno.
  `EFTYPE` is the one number musl has none for, so it sits at 200, past Linux's
  highest.
- **`malloc(0)` and `realloc(p, 0)` return a real block**, never null, so a port
  that reads null as failure cannot mistake success for it.
- **`realloc` never shrinks.** Capacity comes from `heap_usable_size`, so it is
  never stale, but a 1 MB block realloc'd to 1 KB keeps its spans.
- **`PATH_MAX` is 512** — `FS_BLOCK`, `FILE_BUF` and one of the allocator's
  size classes, so a `char[PATH_MAX]` is exactly one block. The number is not
  the constraint; the placement is. Put one in a heap block, never in a
  coroutine frame.
- **No `long double`.** It is 113-bit quad here and every operation on one is a
  compiler-rt link error. `%Lf` is accepted and read as `double`.

## 4. The `b_*` family

`compat/cio.h` is the whole of Group B, and `examples/portio` is the worked
example. Every name is C's with a `b_` in front, taking C's arguments and
answering C's failure — `-1`, `0`, `EOF` or null — with `errno` set through
`compat/cerr.h`. **An end of input is not a failure and never reaches `errno`.**

```
FILE          b_fopen b_fdopen b_freopen b_fclose b_fflush b_fgetc b_fputc
              b_ungetc b_fgets b_fputs b_puts b_fread b_fwrite b_fseek b_ftell
              b_fseeko b_ftello
              b_rewind b_printf b_fprintf b_vfprintf b_perror b_feof b_ferror
              b_clearerr b_fileno b_setvbuf, and b_stdin/b_stdout/b_stderr,
              which `stdin`, `stdout` and `stderr` are macros over
descriptors   b_open b_creat b_close b_read b_write b_poll b_lseek b_ftruncate
              b_dup b_isatty b_unlink b_rmdir b_mkdir b_remove b_rename
              b_chdir b_getcwd b_access b_symlink b_readlink
metadata      b_stat b_lstat b_fstat
directories   b_opendir b_readdir b_closedir b_rewinddir b_telldir b_seekdir
```

What a port has to know beyond the prefix:

- **`b_poll` refuses the call where POSIX marks the entry.** `<poll.h>` is the
  kit's, `struct pollfd` and the `POLL*` constants are Linux's numbers, and
  `POLLIN` and `POLLOUT` are what may be asked for — `POLLPRI` has no meaning
  here and `POLLHUP` comes back beside either. The kernel holds every
  descriptor named for the length of the call (System_Calls.md §8), so a
  descriptor another task of this process is using is `EBUSY` and one that
  cannot be polled at all — a socket, a fetch body — is `ENOSYS`, in both cases
  for the whole call. **`POLLNVAL` is therefore never set**: a bad descriptor
  is `EINVAL`, and a port that reads `revents` to find which one was wrong has
  to be changed. At most `SYS_POLL_MAX` descriptors, beyond which `EINVAL`. A
  negative timeout waits for ever, as POSIX says, and `^C` is `EINTR`.
- **`b_fgetc` and `b_fputc` are awaiters, not `Task`s**, over `FileRead` and
  `FileWrite`: a `Task` per byte would put a coroutine frame on the shadow
  stack per byte (Concept.md §3.3). Their one byte lives in the `FILE`, so two
  streams may be read at once — `zip`'s copy kept it in a file-scope global and
  could not.
- **`b_readdir`, `b_closedir`, `b_rewinddir`, `b_telldir`, `b_seekdir`,
  `b_ungetc`, `b_feof`, `b_ferror`, `b_clearerr`, `b_fileno` and `b_setvbuf`
  do not block**, and are called without a `co_await`. `Sys::List` answers with
  the whole listing, so `b_opendir` is where the syscall is; the rest is the
  buffer's.
- **`b_printf` and `b_fprintf` are not coroutines** — a variadic function
  cannot be one, and the caller's arguments are gone by the time a suspended
  body would read them. They format into a heap block first and the `Task` only
  writes it, so there is no shared format buffer for two of `PROC_TASKS`' eight
  tasks to collide over.
- **`b_write` and `b_fwrite` are all-or-nothing.** A short write is not
  representable here.
- **`b_fseek` clears the end-of-input indicator**, as C requires, and drops the
  pushback. `b_ftell` costs the read-ahead: it is `File::seek(0, SEEK_CUR)`,
  which answers the logical position and leaves the descriptor there.
- **`b_fseeko` and `b_ftello` are the ones that reach past 2 GiB.** `long` is
  32 bits on this target and `off_t` is 64, so C's own pair truncates: they are
  the bodies, `b_fseek` and `b_ftell` are wrappers, and `b_ftell` answers -1
  with `EOVERFLOW` where the position will not fit. `zip` is the caller —
  zip64 is exactly a file a `long` cannot address.
- **`b_ungetc` takes one byte**, which is all C promises. It is the kit's own
  slot and not `File::unget`, which puts back a *rune*.
- **`struct stat` has no permissions, no owner and no links**, because the
  system has none. `st_mode` is a kind plus a constant — `S_ISDIR`, `S_ISLNK`
  and `S_ISREG` are what it answers — `st_dev` is 1, `st_nlink` 1, `st_uid` and
  `st_gid` 0, and `st_atime` and `st_ctime` are `st_mtime`, which is seconds
  and 0 wherever the store keeps none. `st_ino` is **a hash of the path**, so
  that two names compare unequal: `le` compares device and inode to notice a
  file changed under the editor, and a constant makes every file look like
  every other. `b_fstat` has no path, so its `st_ino` is 0.
- **`compat/cio.h` does not include `<stdio.h>`.** A port may have a `printf`
  of its own that does not block — `vi`'s accumulates into one buffer that a
  single `exflush()` drains — and pulling `<stdio.h>` in would collide with it,
  and with a `BUFSIZ` the port has redefined. `cio.h` declares `FILE` itself,
  identically, and guards `EOF`; a port that wants `<stdio.h>` includes it.

## 5. What it costs

Measured, `MinSizeRel`, against `examples/hello` at 7,769 bytes:

| | bytes | over hello |
| --- | --- | --- |
| `examples/portlet` — `qsort`, `strtol`, `strdup`, `ctype`, `malloc` | 10,491 | +2,722 |
| `snprintf`, integers and strings only | 11,255 | +3,486 |
| `snprintf` including the float conversions | 16,622 | +8,853 |

The float arm is **5,367 of those bytes**, and a port used to pay it even for
`%d` alone, because the engine was called from the same function as the
integers and `--gc-sections` works at function granularity. `PORT NOFLOAT`
drops it. The split is two archives rather than one translation unit behind a
weak reference: a weak reference pulls no archive member, so that shape would
make `%f` silently print nothing in a port that forgot to ask, and a wrong
answer is worse than 5 KB. `compat/cfmt.h` is the seam, `braam_compat_float`
is musl's engine and `braam_compat_nofloat` traps, and a program naming neither
is a link error.

Measured over the three ports that took it, and `portio` built both ways:

| | float | NOFLOAT |
| --- | --- | --- |
| `zip` | 418,542 | 413,452 |
| `zipnote` | 196,147 | 191,049 |
| `zipsplit` | 201,850 | 196,752 |
| `zipcloak` | 224,688 | 219,590 |
| `em` | 290,881 | 285,790 |
| `iconv` | 219,873 | 214,783 |
| `examples/portio` | 75,181 | 70,075 |

The split costs **80 bytes** where the arm is kept — one call that was inlined
and is not any more. A binary that never names `snprintf` at all pays nothing
either way and gains nothing from the flag: `vi` and `ex` have a `printf` of
their own and are byte-identical with it and without, which is why they do not
carry it.

Group A's remainder, measured the same way but as a delta over a program that
does nothing else, at 7,353 bytes — each of these is what naming *only* that
arm costs, since `--gc-sections` drops the rest:

| | over the empty program |
| --- | --- |
| `<sys/queue.h>` — a `TAILQ` built and walked | +107 |
| `fnmatch` | +1,957 |
| `mbrtowc`, `wcwidth`, `iswalpha` | +3,463 |
| `gmtime_r`, `strftime` | +4,337 |
| `strtod` | +6,898 |
| `regcomp`, `regexec` and `regerror` | +16,865 |
| `crc32` | +4,564 |
| `uncompress` | +22,632 |
| `compress2` | +31,247 |
| `compress2` and `uncompress` | +48,525 |
| `BZ2_bzBuffToBuffCompress` | +19,072 |
| `BZ2_bzBuffToBuffDecompress` | +21,397 |
| both | +39,380 |
| `lzma_crc32` | +8,611 |
| `lzma_stream_buffer_decode` | +56,874 |
| `lzma_auto_decoder` and `lzma_code` | +61,133 |
| `lzma_easy_buffer_encode` | +72,023 |
| `lzma_easy_buffer_encode` and `lzma_stream_buffer_decode` | +100,929 |
| `ZSTD_decompress` | +56,727 |
| `ZSTD_createDCtx` and `ZSTD_decompressStream` | +61,130 |
| `ZSTD_compress` | +320,385 |
| `ZSTD_compress` and `ZSTD_decompress` | +368,952 |

`<sys/queue.h>` is macros, so its 107 bytes are the caller's own loop. `fnmatch`
carries the twelve `ctype` predicates because a POSIX character class names them
through a table, which `--gc-sections` cannot see past. `strtod` is the largest
by far — musl's `__floatscan` — which is why `cstrtod.cpp` is a translation
unit of its own: a port naming only `strtol` does not pay it. `ZSTD_compress`
is the outlier among the libraries: the level is chosen at run time, so every
strategy's match finders come with it, and a port that only reads `.zst` should
name the decompressor alone.

Group B, the same way, over a `PORT` program that does nothing at all — no
write, so **5,603 bytes** rather than the 7,353 above. Its arms are much larger
than Group A's because each one reaches `proc/file.h` or `proc/io.h`, and the
first to do so brings the buffered stream, the UTF-8 codec and the allocator
with it:

| | over the empty program |
| --- | --- |
| `b_unlink` | +2,502 |
| `b_stat` | +3,953 |
| `b_opendir`, `b_readdir` | +5,530 |
| `b_open`, `b_read`, `b_write`, `b_lseek`, `b_close` | +10,149 |
| `b_printf` | +20,199 |
| the `FILE` family without `b_printf` | +30,817 |

`examples/portio`, which names all of it, is **75,181**. `b_printf` is the
`snprintf` engine above with a stream under it, so a port that has both pays
for the engine once — measured on `duremark`, which already had the engine,
`b_printf` in place of a buffer it writes itself is +10,697 rather than
+20,199. The `FILE` family's 30 KB is `File` itself: `File::fill_`
reaches `Input::read`, which reaches `errln`, so a port opening one stream
carries the multi-file reader whether or not it names one — that is
`proc/file.h`'s shape, not the kit's.

A program that does not name `braam::compat` pays **zero** — no archive, no
header, no flag.

## 6. Never link it

`benchmarks/dhrystone` defines its own `strcpy`/`strcmp`/`strlen` on purpose:
they are what it *measures*, and `dhry_lib.cpp` keeps them in a translation unit
of their own so the call is what it was when it came from libc. Linking the kit
would shadow them and invalidate the benchmark.

`converters/iconv`'s `citrus_bcs_strtol.cpp` is citrus's own private scanner,
not the C library, and stays upstream.

## 7. A port links the kit or keeps its headers, never both

`converters/iconv/include/` and `editors/le/cinc/` once answered the same names
the kit does. Which `-I` wins is silent, so a migration deletes the package's
own header set in the same commit as it adds `PORT`. Both directories are gone,
and so is every private copy that outlived them: `le`'s `lewchar.h`,
`lewchar.cpp`, `wcwidth.c` and its `fnmatch`, `iconv`'s wide half and its whole
`sys/queue.h`, `zip`'s `time_t` and the `days_from_civil` that `timegm()`
answers.

The rule covers a stream layer as well as a header set — which one a call
reaches is equally silent, and the errno each keeps is a different dialect.
`vi`'s `ex_file.cpp` was the first to go, and `zip`'s `z*` family, `le`'s
`leio.cpp`/`lefile.cpp` and `uemacs`'s `fileio.cpp` went with P4. **No package
in `braam-apps` keeps one now.**

Two things survive the rule deliberately, and both are written down where they
live. `iconv` `#undef`s `PATH_MAX` to 256 and `LINE_MAX` to 256 against the
kit's 512 and 2048, because citrus builds paths in coroutine locals and the
kit's numbers are filesystem answers where these are frame-budget ones; the
kit's own archive is compiled against 512, so what makes the divergence safe is
that no kit function is ever handed one of those buffers with an implied size.
And `le`'s config parsers reach the `File` inside the kit's `FILE` —
`f->at->scan_i64()` — because `kernel/text.h`'s scanners are what stands in for
the `sscanf` §3 declines to supply, and because `File::unget` is the pushback
`b_fgetc` shares.
