# Release notes

Reasoning, alternatives and trade-offs behind the code. Comments in the source
say *what* a thing is; this file says *why* it is that way.
[Concept.md](Concept.md) remains the specification — where this document and the
spec disagree about intent, the spec wins and one of the two needs amending.

What has been written for the release after 0.8 is below, under a heading of its
own; the next goes above it, and all of them move to [releases/](releases/) when
the release is cut.

Releases before this one are one file each in [releases/](releases/), newest
first:

- [0.8](releases/Release_Notes-v0.8.md) — the screens a page can hold, and the
  libc it never linked
- [0.7](releases/Release_Notes-v0.7.md) — the devices a port opens, and the
  menus the browser already had
- [0.6](releases/Release_Notes-v0.6.md) — the programs a script assumed were
  there
- [0.5](releases/Release_Notes-v0.5.md) — a system a program can be written
  for, not only in
- [0.4](releases/Release_Notes-v0.4.md) — a system that can install software it
  was not built with
- [0.3](releases/Release_Notes-v0.3.md) — a shell with a language, and files
  with names of their own
- [0.2](releases/Release_Notes-v0.2.md) — a version that names the commit, and
  one program model
- [0.1.0](releases/Release_Notes-v0.1.md) — packaging, and M0–M9 with the
  criteria they were accepted against

---

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
[TODO.md](TODO.md)'s **P1** rather than something to half-do here.

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

[TODO.md](TODO.md)'s **A7**, ported from FreeBSD's `usr.bin/xargs`. It needed
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

[TODO.md](TODO.md)'s **A6** — `tee`, `cut`, `tr` and `seq` — and with `tr`, its
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
them. [System_Calls.md](System_Calls.md) §10 said so in as many words — "no call
here names a screen, and that is deliberate" — and the reasoning behind it was
sound for what it was answering: two shells on two grids need no way to reach
each other's, and not building one kept `PROC_ABI` still. It was never an
argument that a *program* should not paint two grids; nobody had asked.

An emulator asks. A BESM-6 has a console line and a second Consul line, and the
second one is a telnet listener — which this system has no socket for, so its
only possible home in a browser tab is a second screen of the same process.
An editor started on one screen and painted on another is the same shape, and is
what [src/cmd/edit.cpp](../src/cmd/edit.cpp)'s `-S` is: the caller §4.3's first
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
[Shell.md](Shell.md) already states there is no `/dev/tty`. **Two processes and
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
kernel export's arity, which [test/system/abi.mjs](../test/system/abi.mjs)
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

So the header is the kit's, in [limits.h](../src/compat/include/limits.h)'s
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

`/bin/find` is [TODO.md](TODO.md)'s A3, and the port it was read against is
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
[proc/io.h](../src/proc/io.h) beside the copy helpers, `next(path, entry)`
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

`-name` is the shell's own matcher, [sh/match.h](../src/cmd/sh/match.h), linked
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

[TODO.md](TODO.md)'s A4, read against v7 by way of `v7besm/cmd/sort/` and
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
[TODO.md](TODO.md)'s new B3, since the fix belongs in `getline` rather than in
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
[Testing.md](Testing.md) §5's ninth rule, since the failure is silent and reads
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

`/bin/du` is [TODO.md](TODO.md)'s A5 and `TreeWalk`'s third caller, which is
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
