# Release notes — 0.8

Reasoning, alternatives and trade-offs behind the code released as 0.8.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.8 — The screens a page can hold, and the libc it never linked

`BRAAM_VERSION_BASE` moves to 0.8; the commit count and the hash behind it carry
on unedited. 0.6 was written from the point of view of a script already
*running* and 0.7 from that of a program being *ported*. 0.8 is written from the
point of view of the **page**: how many terminals it may put up, and what a
program it did not build may link before it starts. The two halves meet in the
same place — neither of them reaches the kernel.

**`PROC_ABI` does not move, and that is the headline for anyone holding a
binary.** It is 19, as it was in 0.7: nothing here adds an operation or a flag
bit. A package built against the 0.7 SDK runs on this kernel unchanged and no
repository needs re-signing. The wasm surface is unmoved as well — seven imports
and nine exports — but it did grow an *argument*, so `test/system/abi.mjs` now
asserts each export's arity as well as its name. A matching list of names would
not have caught the thing that actually drifted.

**A page decides how many terminals there are.** `web/dual.html` is two of them
and `web/quad.html` four, each with a grid, a console, a `^C` and a `/bin/sh` of
its own, and all of them belonging to **one** kernel: one scheduler, one heap,
one VFS, one open-file table, one `/home` and one `/tmp`. Two kernels on one
page were the alternative and were rejected on storage rather than on layout —
both mount the origin's OPFS as `/`, and a boot that misreads `/etc/version`
unpacks the archive over a running sibling's `/bin`. A terminal is made by the
first `resize()` that names it, so a page with one canvas has exactly one shell
as it always did, and `TERM_MAX` is 4 — an id-space bound, now reached rather
than merely stated.

There is no "current terminal" anywhere, and adding one is the bug this shape
exists to prevent. `Proc` carries a terminal and a spawn inherits it, exactly as
`cwd` is inherited, because a syscall server awaits and a global set before an
await is a bug waiting for two shells to be busy at once. `Sys::Tty`, `Sys::Fg`,
`Sys::Cursor`, `Sys::Style` and `Sys::Echo` keep their wire format and resolve
the caller's terminal from its `Proc`, so nothing in `src/cmd/` or `src/proc/`
had to change at all.

Two screens found two things one screen had hidden. The VFS shares a backend
handle per path but published the record only *after* the await, so two shells
opening `/bin/sh` in the same tick raced and the loser was told `permission
denied`; an in-flight open is now published before the await, which is why `less
/etc/help` on both screens at once works. And `/proc/host` carried a `screen`
field, which was a machine fact only while there was one machine-wide grid — it
is the caller's, so the file stops at `system`, `release` and `machine`, and
`uname -g` is the geometry on its own.

**A ported program may link a C library; the system still links none.**
`braam::compat` ([doc/Compat.md](../Compat.md)) exists because seven ported
packages each wrote one by hand — ~4,000 lines of it, in which `le` and `uemacs`
shared 381 identical lines and six `printf` engines disagreed about `%ld`,
precision, `%*d` and the return value. Four of those disagreements were wrong
answers rather than differences of taste, and none of them could be tested,
because all of it lived outside this tree.

`PORT` on `braam_add_program` is the whole of the opt-in: without it,
`#include <string.h>` is still "file not found", verified in the tree and
against an installed SDK, and nothing in this tree names it. The kit adds no
operation and imports nothing, and it is stratified rather than pretending to be
a libc — Group A is pure computation with exact C signatures, and Group B's
blocking names ship declared `unavailable` with the replacement in the message,
so a port gets its list of `co_await` sites out of one build instead of by hand.
`malloc` lost the 8-byte header all five copies carried, because `alloc.cpp` has
kept the capacity in a side table since the start and merely never exposed it.

**The other shim went with it.** `src/math/musl/shim/` was eight headers and 137
lines, predating the kit and overlapping it; what replaces it is one
force-included [src/math/musl_prologue.h](../../src/math/musl_prologue.h) and
clang's freestanding headers, which are part of the compiler and pull in no
runtime — `<stdio.h>` still does not resolve, and that is the guarantee the rule
was protecting. The review found `FLT_EVAL_METHOD` defined nowhere and read as 0
at fifteen `#if` sites, and `predict_false` defined twice incompatibly. All 149
vendored objects were compiled both ways and compared with `cmp`:
byte-identical. The cost is recorded as a table, since a musl re-sync is now a
copy plus eleven deleted `#include` lines.

**Every program says what it takes, and asking is not a mistake.** All
forty-four binaries carry a `Usage:` / `Options:` block, and a bare invocation
writes it to **stdout** and exits **0** — the shape `/bin/pkg` has had since 0.4
— while a wrong option still goes to stderr with 2. `usage_asked` and
`usage_error` in [src/proc/usage.cpp](../../src/proc/usage.cpp) are two
functions rather than two open-coded lines because what is factored is a
*policy*, which stream and which status, and that is exactly what drifts across
sixty sites. `-h` counts only as the whole command line, so `grep -h f` still
searches for a literal `-h` and `rm -h x` still removes a file called `-h`. The
accepted cost is that a bare `rm` now succeeds.

**Two smaller repairs.** Esc had been leaving the terminal deaf: Chrome cancels
a composition without firing `compositionend`, so the guard on the hidden sink
stayed up for good and a paste was the only way back. Esc now ends a composition
whatever the engine says, and the focus is taken back a turn later. And `less`
and `edit` paint their bottom line black on cyan rather than on the white
ordinary text is already written in, so the one row that belongs to the program
is the one row no other output can be confused with.

**One entry below records something that was built and reverted.** D2 asked for
typed `put`s on a `File` in place of a `Buf` and a write; converting `df` whole
cost 27% of the binary, about 375 bytes per added suspension point, and a shared
`fmt_u64` in `kernel/text.h` grew every binary that formats a number. It is
written down because the four duplicated digit loops look like an obvious
cleanup and are duplication the optimiser is exploiting.

**What moved is what a page may put on the screen, and what a program beside the
system may link.** Neither of them reached the kernel: it imports nothing new,
links nothing new, and answers no operation it did not answer in 0.7.

## Typed output over a `File`, measured and put back

D2 wanted `out.put(pid)` with the padding instead of a `Buf` and a write. It was
built and reverted, and the numbers are the reason.

**The conversion.** `df` was converted whole — every column, output verified
byte-identical and the alignment checked — and grew from 27,529 to **35,023
bytes, 27%**. Its `co_await` sites went from 8 to 28, so the cost is about
**375 bytes per added suspension point**: each one is another state in the
caller's coroutine state machine. A `Buf` and one `write` batch a whole row into
a single suspension; typed `put`s cannot, and that is inherent to the shape
rather than to this implementation of it. What it buys back is 256 bytes of
frame, and `df`'s frame was never shown to be near §8.2's 512-byte cliff where
that would matter.

**The primitive lost too, and separately.** A shared `fmt_u64`/`fmt_i64` in
`kernel/text.h` — the mirror of the `scan_*` family beside it — was meant to be
the durable half, removing three digit loops from `Buf` and four copies of a
zero-padded `put2` from `date`, `ls` and `pkg/query`. Every binary that formats
a number grew 430–550 bytes instead. Marking the wrappers `inline` recovered
100 of them, so it is not a cross-translation-unit call; and routing `date`'s
`put2` through the primitive — the case that should have paid it back — grew
`date` further still, 13,811 to 13,851. One general width/pad/sign routine
cannot beat a tight digit loop the optimiser specialises per call site. The four
"duplicated" loops are duplication the optimiser was exploiting, which is worth
recording because they look like an obvious cleanup and are not.

**D2's own first line was already the answer.** "A write per field is a syscall
per field" stopped being true when the stream buffered — `FileWrite::await_ready`
answers from the buffer without allocating a frame, and a row ending in `\n`
flushes once however it was assembled. With the syscall argument spent and the
frame argument worth 256 bytes against 7,494, there was nothing left to win.
D2 now reads like D4: recorded so it is not re-derived.

**One thing came out of it.** Checking the conversion needed a way to compare
column alignment independent of the values in it, and that turned up a real gap:
every `ls -l` case in the suite lists a fixture whose sizes are one or two
digits, so the size column's measured width had only ever been exercised at 1
or 2. `test/system/columns.mjs` lists `/bin` — fifteen rows of four, five and
six digits — and asserts every row agrees on where its fields end, looking at no
value. A width pass capped at three digits passes every other case in the suite
and fails this one.

## The other shim, and eleven lines of vendored diff

`src/math/musl/shim/` predated the port kit and overlapped it: eight headers,
137 lines, answering `<stdint.h>`, `<float.h>`, `<math.h>`, `<endian.h>`,
`<features.h>`, `<fenv.h>`, `<limits.h>` and `"atomic.h"` for the 148 vendored
sources, which were compiled `-nostdinc` against it. Reviewing it against what
clang and `braam::compat` now provide, it divided four ways.

**Two headers were pure duplication.** clang's freestanding `<stdint.h>` and
`<limits.h>` answer correctly, and the shim's `intptr_t`/`uintptr_t` were
actually *wrong* against clang's — `unsigned int` where clang says
`unsigned long`, the same width but a different type. Nothing used them, so the
conflict never fired.

**One was a deliberate lie, and load-bearing.** `float.h` aliased `LDBL_*` to
the `__DBL_*__` builtins, which is what put `libm.h:10` and `cvt/floatscan.c:6`
on their `LDBL_MANT_DIG == 53` paths. Unshadowed, wasm32's `long double` is
113-bit quad and `libm.h:30` would emit a `union ldshape` over it. The lie had
to survive, and — this is the part worth stating — it must **not** reach the
port kit, where a ported program needs clang's truth.

**Three carried content a *port* wants** and could not get: `#include <math.h>`
simply failed for a ported program. `<math.h>`, `<fenv.h>` and the byte-order
family are now the kit's, each its own copy, because `braam_compat_pure` links
`braam_math` and sharing would invert the dependency.

**Two were musl's internals** — `hidden`, `weak_alias`, `a_clz_64` — which are
toolchain macros, not libc, and have no business in a kit a program links.

What replaces the directory is one force-included
[src/math/musl_prologue.h](../../src/math/musl_prologue.h) holding only what is
neither clang's nor `math/math.h`'s, plus `-I src/math` so `<math.h>` resolves
to the real header under its own name. `double_t`/`float_t` moved into
`math/math.h`, where they always belonged.

**The review turned up two latent defects.** `FLT_EVAL_METHOD` is named at
fifteen `#if` sites across eight files and was defined nowhere — neither by the
shim nor by clang's predefines — so every one of them read an undefined
identifier as 0. That is the right answer for wasm32, but by luck rather than by
statement, and `-Wundef` would have said so. And `predict_false` was defined
twice incompatibly, `__builtin_expect(!!(x), 0)` in the shim against
`__builtin_expect(x, 0)` in `libm.h:97`, a `-Wmacro-redefined` diagnostic
silenced by the `-w` those sources carry.

**The cost, recorded so a re-sync knows.** Eliminating the directory rather than
shrinking it means eleven vendored files each lose one `#include` line, so a
musl re-sync is no longer a plain copy. It is a copy plus this patch:

| file | line to delete after a re-sync |
| --- | --- |
| `musl/libm.h` | `#include <endian.h>` |
| `musl/fma.c` | `#include "atomic.h"` |
| `musl/fmaf.c`, `musl/lrint.c` | `#include <fenv.h>` |
| `musl/{exp,exp2f,log,log2,log2f,logf,pow}_data.h` | `#include <features.h>` |

`cvt/cvt.h` also gained `#include <stddef.h>` — the shim non-standardly put
`size_t` in `<stdint.h>` and cvt leaned on it — but `cvt/` is ours, derived
rather than verbatim, so that is not a vendored edit.

**What makes editing vendored code defensible here is that it changed nothing.**
Every one of the 149 C objects was compiled both ways, same flags, and compared
with `cmp`: all 149 are byte-identical. The `math` unit case, which measures
against `test/unit/math.data` generated from the host's own libm, reports the
same worst error it did before — 15 ulp, in `lgamma`.

**A note on what "no libc headers" ever meant.** Dropping `-nostdinc` makes the
tree use clang's freestanding headers, and four documents said it did not. They
are amended rather than worked around: a freestanding header is part of the
*compiler*, declares no functions and pulls in no runtime. There is still no
sysroot, so `<stdio.h>` does not resolve for anything that has not asked for the
port kit, and that is the guarantee the rule was protecting. One of those four
sentences had already gone stale when `compat/include/limits.h` started reaching
clang's `<limits.h>` with `#include_next`.

Clang 23 also ships a freestanding `<endian.h>` that derives the byte order from
`__BYTE_ORDER__` and carries the whole `htobe`/`letoh` family. The kit's own
first draft hardcoded little-endian; clang's is better, so the kit does not
supply one and keeps only the BSD spelling and the `htonl` four.

## A libc for the programs beside the system, and none inside it

Seven ported packages each wrote a `braam.h`/`braam.cpp` by hand, and two grew a
directory of fake system headers besides. That was ~4,000 lines, and it was one
problem solved seven times rather than seven problems: `le` and `uemacs` shared
381 identical lines — 95% of uemacs's file — `zip` and `iconv` 350, and `uemacs`
and `vi` 228, which is 98% of vi's.

Duplication alone would only have been waste. What made it worth fixing is that
the seven copies **disagreed**, and four of the disagreements were wrong
answers rather than differences of taste:

- `iconv`'s `printf` swallowed `l` and `z` and then read an `int`, so every
  `%ld` and `%zu` misread a 64-bit argument.
- `zip`'s parsed a precision and discarded it, and returned the bytes it wrote
  where C returns the bytes it would have written — which is the idiom for
  sizing a buffer.
- `uemacs`'s took a negative `%*d` width and lost the left-justify.
- `iconv`'s `strtoul("-1")` was 0 where C gives `ULONG_MAX`; `zip`'s `strtol`
  accepted a bare `"0x"` and returned 0 past the `x`; none of the three
  saturated or set `ERANGE`.

None of that could be tested, because all of it lived outside this tree.

**Why this is not the libc §1 rules out.** The non-goal was, and stays, a libc
*under* the system: the kernel imports nothing new, `src/cmd/` links nothing
new, `--allow-undefined` is still absent, and an accidental libc dependency is
still a link error everywhere it was one before. `braam::compat` is an archive
nothing links unless it names it, whose system header names are reachable only
from a `PORT` target — verified both ways, in the tree and against an installed
SDK: without `PORT`, `#include <string.h>` is still "file not found". It adds no
operation and moves no `PROC_ABI`, so §4.3's first rule, which governs the
operation table, is untouched. A program that does not ask for it is byte-for-
byte what it was.

**A port is still a rewrite.** That is the honest half, and it is why the kit is
stratified rather than pretending to be a libc. Group A — string, memory, ctype,
conversions, `qsort`, `malloc`, `snprintf` — is pure computation and gets exact
C signatures. Group B cannot: `int fgetc(FILE *)` is unimplementable here, since
everything blocking is a `co_await` and there is no Asyncify, no JSPI and no
stack switching. `zip` carries 1208 `co_await`s, `le` 627, `uemacs` 401, `vi`
307, and every one of those was found by hand. So the blocking names ship in
`<stdio.h>` declared `unavailable` with the replacement in the message, and the
compiler now produces that list in one build. That diagnostic is the kit's best
idea, and none of the seven had it.

**What was decided, where the copies differed.** `errno` takes musl's numbers,
the dialect already vendored in `src/math/musl/`, rather than `int(Error::…)`:
`Error` has fifteen values and cannot name `ERANGE`, `EILSEQ` or `ENAMETOOLONG`,
and freezing an internal enum into a public C ABI would make `kernel/result.h`
un-amendable. `qsort` is heapsort, not the O(n²) insertion sort three ports
shared — O(1) extra memory, so it cannot fail where there are no exceptions, and
iterative, so it cannot overflow the 128 KiB shadow stack. `strerror` returns
the POSIX *name*, mirroring `error_name()`.

**`malloc` lost its header.** All five copies carried an 8-byte `struct Head`
with the same comment, because `heap_block_size` takes a request size and cannot
recover a block's capacity from a pointer. But `alloc.cpp` has kept a side table
since the start — `span_class[ptr >> 16]`, which is how `free` needs no header —
so the capacity was there and merely unexposed. `heap_usable_size` exposes it,
and the header goes: 8 bytes back per allocation, 16-byte alignment restored
(the header had made every block 8-mod-16), and the divergence all five shared —
never rewriting `h->cap` on the in-place grow, so capacity was never reclaimed —
is now structurally impossible, since the capacity is read rather than
remembered.

**The tests found one immediately.** The unit case is differential: it asserts
exactly the inputs the seven engines disagreed about. On its first run `%zu`
failed — the new engine read `unsigned long long` for `z`, and `size_t` is
32 bits on wasm32. That is the same bug as iconv's, in the replacement written
to fix it, caught before it shipped. The lengths now name `ptrdiff_t` and
`size_t` themselves rather than a guess at their width.

**What it costs.** Against `examples/hello` at 7,769 bytes: `portlet`, using
`qsort`, `strtol`, `strdup`, `ctype` and `malloc`, is 10,491; `snprintf` with
integers and strings is 11,255; with the float conversions, 16,622. The float
arm is 5,367 of those bytes and a port pays it for `%d` alone, because the
engine is one function and `--gc-sections` works at function granularity. The
obvious fix — a translation unit of its own behind a weak reference — is *not*
taken: a weak reference does not pull an archive member, so `%f` would silently
print nothing in a port that forgot to name it, and a wrong answer is worse than
5 KB. TODO.md P5 carries the shape a real fix needs.

## Esc is not a composition, and the keyboard does not end with one

Typing at the shell and then pressing Esc left the terminal deaf: nothing typed
after it arrived, and the way back was a copy and a paste, or a click on the
canvas. Two separate faults, both in the hidden textarea `web/braam.js` reads
the keyboard through, and both about an input method that is not there.

The first is a composition that never closes. The sink guards *both* key routes
on one — `onKeyDown` returned on `event.isComposing`, and `onInput` returned on
the `composing` flag `compositionstart` sets — because an IME's keystrokes
arrive twice otherwise, once as a key and again as the text it composed. Chrome
cancels a composition on Esc **without firing `compositionend`**, so the flag
stayed up; worse, every `keydown` after it still reported `isComposing`, so the
browser's own flag was no more trustworthy than ours. Both routes were then shut
for good, and `resetSink()` — guarded on the same flag — stopped restoring the
sentinel, so the sink silently accumulated the text nobody was reading. A paste
survived because `onPaste` has no such guard and the browser's insertion closes
the composition, which is why copy-and-paste appeared to repair a keyboard.

So Esc now ends a composition whatever the engine says (`endComposing`), and the
guard reads what the composition *events* said rather than `event.isComposing`.
The cost is that an Esc which cancelled a composition also reaches the program.
For a terminal that is the wanted answer — vi needs the key more than an input
method needs it swallowed — and braam has no IME integration of its own to
consult; where `compositionend` does fire it fires first, and the key is guarded
out as it always was.

The second is the focus. Cancelling a composition blurs the sink on some
engines, and a blurred sink reads no keys at all — hence the click. `keepFocus`
takes the focus back a turn after an Esc, a turn because the blur follows the
handler rather than preceding it. It only ever takes it *back*: a sink that
still holds the focus is left alone, so nothing is stolen from the rest of the
page. Neither fault reproduces in headless Chrome without driving
`Input.imeSetComposition` by hand, which is why the guard was written the way it
was and why the repair is defensive rather than a condition to detect.

## Every program says what it takes, and asking is not a mistake

The entry below this one gave `unzip` a `Usage:` / `Options:` block and then
drew a line: "the rest of `src/cmd/` keeps its one-liners… a judgement per
program rather than a rule to apply across the tree." That was wrong, and this
entry supersedes it. What changed the answer is `-h`: once a program can be
*asked* for its usage, the message stops being what a reader gets after a
mistake and becomes documentation, and documentation that is a block in one
program and a line in the next is worse than either shape applied everywhere.
So all forty-four binaries carry the block now, and the ones with no options
carry the heading and the synopsis alone.

The status is the larger half. `unzip` with no arguments printed its usage and
exited 2, which says the user got something wrong. They did not — they asked a
question, and 2 is the wrong answer to it. `/bin/pkg` has had the right one
since 0.4: a bare `pkg` writes the block to **stdout** and returns **0**, and
only a wrong option or an unknown command goes to stderr with 2. That split now
holds for every program. It is two functions in
[src/proc/usage.cpp](../../src/proc/usage.cpp) — `usage_asked` and
`usage_error` — rather than the two open-coded lines each site had, because
sixty sites sharing a *policy* is not the same as two sites sharing a
convenience: what is factored here is which stream and which status, and that is
exactly what would drift.

`-h` and `--help` are recognised as the whole command line and nowhere else —
`help_asked` in [src/proc/opt.h](../../src/proc/opt.h), which is where a command
line is already read, and which is in the suite's syscall-free half, so it is a
unit test rather than a system one. The narrowness is deliberate twice over.
Requiring it to be `argv[1]` means a valued option's argument can never be
mistaken for it: `basename -s -h x` strips `-h` as a suffix, as it always did,
and nothing in `OptParse` or in any hand-rolled flag loop ever reads a value out
of `argv[1]`. Requiring it to be the *only* word means `grep -h f` still
searches for the literal `-h`, and `rm -h x` still removes a file called `-h`.
What is lost is `-h` as a program's sole operand — `./-h` still reaches it.

Five programs are left out, each because `-h` already means something there.
`ls -h` and `df -h` scale their sizes, so only `--help` asks; `sh` is a shell
when run bare, so it takes `--help` alone as well. `echo` must print its
arguments verbatim, `test` reads `-h` as a file primary, and `true` and `false`
ignore arguments by definition — those four take neither spelling. The first
four also ship as shell builtins, and a builtin and its `/bin` twin cannot
disagree.

The cost is real and is accepted rather than overlooked: a bare `rm`, `cp`,
`mkdir` or `touch` now succeeds. `rm $files` with an empty, unquoted `$files`
was a status 2 and is now a silent 0. Nothing in this tree does that — the only
script in `rootfs/` is `/bin/help` — and it is the same trade `/bin/pkg` took
four releases ago. Treating a bare call as a question is not compatible with
treating it as an error, and of the two the question is what people actually
type.

`/bin/pkg` itself is untouched, subcommand usage lines included: it was already
this shape, and its rule differs in reading the command word after `-v` is
stripped. The shell's twenty-six builtins keep their one-liners, which
[doc/Shell.md](../Shell.md) still describes correctly — a builtin has no `-h` to
give, because a builtin that took one would shadow the name at a prompt and
nowhere else.

## Four screens, and the ceiling reached

`web/quad.html` is a 2×2 of four terminals of one kernel, each with a grid, a
console, a `^C` and a `/bin/sh` of its own. It is a page and nothing else — no
kernel change, no JS change, not a line of `web/braam.js` or `web/worker.js` —
and that is the whole of what it demonstrates. `mount({ screens: [ … ] })`
already maps its array index to a terminal id, `worker.js` already grows its
`screens[]` on demand, and `resize(term, …)` for a terminal nothing has named
already makes one. Two screens were the first plural; four are the proof that
the plural has no arithmetic in it.

**The ceiling is reached now, not merely bounded.** `TERM_MAX` has been 4 since
dual.html and nothing had ever asked for the fourth. That is the awkward kind of
constant: it is an *id-space* bound rather than a resource one — the arrays it
sizes are a `Term`, a `Channel<Key>`, a `Con` and a `Claims` each, and four of
each cost nothing worth counting — so the number was never load-bearing except
at its own edge, which no caller had touched. `test/system/quad.mjs` touches it:
it pins the refusal at id 4 rather than at `dual`'s id 9, which is the first id
that does not fit rather than one comfortably past it, and the difference is
whether an off-by-one in `term_open` would have been caught.

Four shells alive at once needed the suite rearranged, since it is one
cumulative session and a shell that exits is not replaced. `dual` used to retire
terminal 1's shell before it returned; it now hands it over, and `quad` makes
terminals 2 and 3, runs with four, and exits all three above terminal 0. The
count `language` starts from is unmoved, which was the reason `dual` cleaned up
in the first place — the cleanup moved, not the rule.

**Four grounds, because only one screen wears a ring.** The focus border tells
you where your keystrokes go; it cannot tell three unfocused screens apart from
each other, and with two screens dual.html did not have to. So each screen has a
background of its own — and *only* that: entry 0 of the palette, the one
`web/render.js` paints the default background with, and the fifteen other
colours shared from one array. A screen that recoloured its text would stop
being the same system in a different corner and start being a different system,
which is what `web/embed.html`'s second kernel is for. The three tints are
permutations of one set of dark components, so no pane reads as brighter or more
important than the others.

**What the layout costs.** Two columns instead of two rows, `#status` spanning
both so the four panes sit above one status line rather than beside it, and
everything dual.html says about `overflow: hidden` and `min-height: 0` on both
the row and the canvas still true and still load-bearing — a canvas's intrinsic
300×150 box floors a grid row, and four of them on a short window would push the
page past the viewport into a scrollbar that narrows the canvases that made it
appear. Four panes make that failure four times as easy to reach, which is the
only sense in which the page is harder than two.

## A status line that is not the colour of text

`less` and `edit` painted their bottom line black on `COLOR_WHITE`, which is the
white ordinary text is already written in and the white the boot banner uses.
The bar was in the right place and the right shape, and it still read as a row
of inverted text rather than as a band belonging to the program. It is now black
on `COLOR_CYAN` — index 6, a background nothing else in the tree paints on — so
the one row that is the program's own is the one row no other output can be
confused with.

Nothing but the two `Pane::style` calls changed. The bar is sticky style plus
`fill_row`, so the padding past the end of the text carries the colour without
being told, and the two programs keep the four-line idiom each already had
rather than gaining a shared helper in `src/ui/`: two call sites is not yet a
pattern, and factoring one would fix the palette for every full-screen program
before there is a second kind of bar to weigh it against. The shell prompt and
the boot banner are deliberately untouched — neither is the bottom line of a
program that owns the screen.

## A usage message that says what the options do

`unzip`'s usage was one line naming three options and explaining none of them,
which is the shape thirty-odd programs in `src/cmd/` print. It is enough where
the option letters are the familiar ones — `ls -l`, `rm -r`, `cp -i` — and it is
not enough here: `-p` is not "print", it is "extract to a pipe and say nothing
else", and a reader with only the line in front of them cannot tell that from
`-l`. So it grew a block, `Usage:` then `Options:` with a row per letter.

The shape is not new. `/bin/pkg` has printed it since 0.4
([src/cmd/pkg/pkg.cpp](../../src/cmd/pkg/pkg.cpp)), where a table of twelve
subcommands left no choice, and taking it for `unzip` costs a longer string
constant and nothing else — the same one write to stderr, the same status 2.
The rest of `src/cmd/` keeps its one-liners. What decides between them is
whether an option's name gives its meaning away, not how many there are, and
that is a judgement per program rather than a rule to apply across the tree.

## Two screens of one kernel

`web/dual.html` splits a window between two terminals: the upper two thirds and
the lower third, each with a `/bin/sh` of its own. The question it settles is
which of the two arrangements a second terminal should be.

**Why not two kernels.** `web/embed.html` has mounted two instances on one page
since 0.2, and doing it again with a different layout would have been an
afternoon's work and no new code. It is the wrong answer, and the reason is
storage. Two kernels are two of everything *except* the origin's OPFS, which
they both mount as `/`. A sync access handle takes an exclusive lock
(`web/fs.js`, `createSyncAccessHandle`), the loser's rejection reaches the
kernel as `E.PERM` (`web/abi.js`) with no retry, and there is no open-file table
spanning two instances to prevent the collision — the VFS's own table is
per-kernel and knows nothing of a sibling.

Contention would be a nuisance. What makes it unacceptable is *misreading*: at
boot each kernel reads `/etc/version` to decide whether to unpack, a failed read
is indistinguishable from an absent file, and absent means unpack **without
asking** — which removes every top-level directory the archive carries, out from
under a kernel that is already running out of it. Two shells on one page would
have had a race that occasionally deletes `/bin`. That is not a trade-off worth
making for a layout.

So: one kernel, two screens. One scheduler, one heap, one VFS, one open-file
table, one `/home` and one `/tmp` — everything the two shells share, they share
through the code that already arbitrates it.

**The ABI grew an argument, and that is the cost.** `key` and `resize` take a
terminal id, and `host_present` carries one; the surface is still nine exports
and seven imports, and no process names a screen — `Sys::Tty`, `Sys::Fg`,
`Sys::Cursor`, `Sys::Style` and `Sys::Echo` keep their wire format and resolve
the caller's terminal from its `Proc`, so `PROC_ABI` does not move and nothing
in `src/cmd/` or `src/proc/` changed at all. Since a matching list of *names*
would not have caught an added argument, `test/system/abi.mjs` now asserts each
export's arity as well: a wasm export reaches JS as a function whose `length` is
its parameter count, which costs one table and pins the thing that actually
drifted here.

**The host decides how many, not the kernel.** A fixed pair would have meant a
second shell running invisibly on `index.html`. Instead a terminal is made by
the first `resize()` that names it: the kernel spawns its pump, init starts a
shell on it, and a page with one canvas has exactly one shell as it always did.
`TERM_MAX` is 4 and bounds an id arriving from JS; the id space is the only
reason for a limit at all.

**A process belongs to a terminal.** `Proc` carries it and a spawn inherits it,
exactly as `cwd` and `depth` are inherited — so the shell, its pipeline and
everything below reach one grid, one keyboard channel and one foreground set.
The alternative, a "current terminal" the dispatcher sets on entry, was rejected
outright: a syscall server awaits, and any global set before an await is a bug
waiting for two shells to be busy at once.

**The bug two terminals found.** Both shells start within a tick of each other
and both `exec_resolve("/bin/sh")`, which opens one file twice. The VFS shares a
backend handle per path and already recovered from a lost race — but only if the
winner had *registered* its record by the time the loser resumed, and when the
loser's rejection arrives first there is nothing to find. The store refuses the
second handle, and `/bin/sh: permission denied` is what the lower screen said.

The fix is not a retry: an in-flight open is now published before the await, so
a second opener waits for the record the first is about to register instead of
asking the store for a handle it is already taking. It costs a zero-delay tick
per waiter and removes the failed handle entirely. This was always a race — two
terminals only made it routine, and it is why `less /etc/help` on both screens
at once works, which is the thing two kernels could never have done.

**The screen left `/proc/host`.** The second thing two terminals found: `uname
-a` printed `screen 124x28` on both, because the file it reformats built that
line from `screen(*term_open(0))`. The comment above it argued the case — the
file is the machine and not the caller, and a process asks `Sys::Tty` for its
own — and the argument was sound while there was one screen and wrong the moment
there were two. A machine with up to `TERM_MAX` grids has no geometry of its
own, so the field was not per-caller, it was *not a machine fact at all*, and
`/proc/host` now stops at `system`, `release`, `machine` and what the host said
at boot.

Three other shapes were considered and rejected. Plumbing the reader into
procfs: `/proc` is generated at open and cannot know who is reading — that is
why `cwd` is per-pid rather than the kernel's — and threading a caller through
the VFS for one line is a large change to buy a small one. Keeping the line as
terminal 0's and letting `uname` override it: `cat /proc/host` would still
answer the wrong number on the lower screen, and a file that is only right when
reformatted is worse than no field. Listing every terminal: it makes `uname -a`
answer a question nobody asked, and `vmstat` still could not tell which of the
list it was printing into.

So `uname -a` prints the terminal it is running on after `machine`, where the
field always was, and a one-screen page's output is byte-identical to what it
was. Taking the field out did expose a second thing: the stored description
carries a blank line, `cat /proc/host` had been showing it as a gap all along,
and with nothing after `machine` it read as the hole the screen had left. It is
not a row of the table — it is `banner_half`'s marker for how far down the boot
grid the fields go — so procfs drops it on the way out and boot keeps reading it
in the string it was always in.

`uname -g` is the geometry on its own, the shape `-s`, `-r` and `-m` already
had for the fields that are in the file, and the only one of the five that does
not read `/proc/host` at all. It is what a script wants — `$(uname -g)` rather
than `uname -a | grep screen` — and it is the answer to a question the file
could not have held even before this: a size that is the caller's, not the
machine's. Off the grid it prints nothing and exits 1, which is what a field
that is not there already did. `vmstat`'s header repeat moved the same
way and is more correct for it: it was sizing its output to terminal 0 and
re-reading terminal 0 on every `SIG_WINCH`, and now follows the window the rows
actually land in. Down a pipe both print no geometry, since `Sys::Tty` answers
zero and a pipe has no width — the rule `ls` has always run by.

**What is not here.** No way to move a process between terminals, no `chvt`, and
no terminal that outlives the page. A second terminal's shell that exits is not
replaced — the same rule terminal 0 has always had — and the page is what
decides a terminal exists at all.
