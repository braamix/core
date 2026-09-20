# Release notes

Reasoning, alternatives and trade-offs behind the code. Comments in the source
say *what* a thing is; this file says *why* it is that way.
[Concept.md](Concept.md) remains the specification — where this document and the
spec disagree about intent, the spec wins and one of the two needs amending.

The release after 0.9 is being written, and starts below. New sections are
appended under it, and the whole moves to [releases/](releases/) when the
release is cut.

## An editor's regex engine became the system's

`braam::regex` did not start here. It was written for `editors/eh` in the
applications tree, because the port kit had no `<regex.h>` and eh's whole
purpose is searching. It is 1,000 lines: a recursive-descent parser into a node
arena, and a backtracking matcher over an explicit continuation list. When
`/bin/grep` wanted regular expressions the choice was a second engine or this
one, and a second engine is how a tree ends up with `le`'s 6,600-line GNU
`regex.c` *and* a rewrite of it.

**`le` stays on GNU, and that is not indecision.** It calls `re_search_2` across
the two halves of a gap buffer without copying, under Emacs syntax with
`RE_FRUGAL`, and its sixty shipped syntax files are written to those semantics.
Porting it to this engine means a copy of the buffer per search, non-greedy
quantifiers this engine does not have, and rewriting user-facing files to mean
what they already say. What the lift bought there was ~20 KB of a 537 KB binary
— so `le` was left alone, and the one thing it did need was a quoted include:
`<regex.h>` now answers from the kit and would have captured it silently.

**Four things were wrong to ship in an SDK header, and closing them is most of
the work.** `REG_ICASE` and `REG_NOSUB` were declared and never read — eh passed
neither, so nothing noticed. `REG_EXTENDED` was declared and ignored too, which
is worse: a program that meant a BRE got an ERE and no complaint, so there is a
BRE arm now, sharing the matcher and forking only the parser. And there was no
`regerror`, because eh's error handling is one `beep()`; a single `REG_BADPAT`
was enough for that and is not enough for `grep`, so the parser's thirteen
failure sites are now POSIX's codes with messages behind them.

Back-references came with the BRE arm rather than for it: `\1` is POSIX in a BRE
and would have been a gap, and in a backtracking matcher it is one node type.
They are in the ERE too, as GNU's extension, which is the one behaviour change
for eh — `\1` used to be a literal `1`.

**`REG_STARTEND` is what made the engine usable from this system at all.**
`kernel/string.h` says in its first line that nothing there is NUL-terminated,
so without a region the subject `grep` has would need a NUL appended per line.
Replacing the NUL sentinel with an end pointer throughout the matcher is a
smaller change than it sounds — ten comparisons — and it also means a NUL inside
a line is a byte rather than an end.

The rest was the platform. `realloc` became `heap_alloc` with a doubling grow
helper, which the three arenas did not have before: they grew one element at a
time, O(n²) in the pattern. `<ctype.h>` and `<wctype.h>` became
`kernel/text.h`'s new `rune_is_*`, which is where the twelve classes belonged
anyway — `src/compat/cwctype.cpp` had them, so the port kit was the only way to
ask the system what a letter is. They are one implementation now, with
`iswprint`/`iswgraph`/`iswpunct` staying in compat because those three answer a
*width* through `wcwidth` where `rune_is_print` answers the grid's own
question: a rune it can put in a cell.

**`grep` defaults to an ERE and keeps `-F` for a plain string.** That silently
changes what `grep 1.2` means, which is the price of a grep that is a grep. `-F`
is not a compatibility shim — a literal search needs no engine, the substring
loop was already written, and it is the faster answer.

**The retired test is the part worth arguing about.** eh's engine had a native
differential harness: 3,456 cases against the host's own `<regex.h>`, under
ASan and UBSan. An engine that reaches `kernel/alloc.h` cannot be compiled for
the host — `kernel/types.h` asserts `sizeof(usize) == 4` — so that harness could
not come along. Keeping it would have meant an allocation seam and a
classification seam existing only for the test, and a library shaped by its
test. Instead `tools/mkregexdata.py` asks the host the same questions once and
writes the answers down: the corpus survives as `test/unit/regex.data` and is
replayed in wasm, where the engine actually runs. What it cannot record —
back-references in an ERE, `\|`, UTF-8 by the sequence — is asserted by hand,
because those are the three places the hosts disagree with each other.

## The compile cache keys on the image, not on the path

`pkg upgrade` relinked `/pkg/bin/<name>` to the new generation, said so, and
then ran the old binary anyway — for the life of the page, with nothing to
say so. The host's compile cache (§4.4) was a `Map` from path to Module, and a
path is mutable: the same name resolves to different bytes after an upgrade, a
rebuild written over itself, or an `fimport`. The image the kernel had already
sent was ignored on a hit.

Keying on a digest of the image instead makes the cache sound for every writer
rather than for one of them, and it is what the section always meant by
"compiled once however many workers run it" — once per binary, not once per
name. FNV-1a over 128 KB is ~0.2 ms, paid once per distinct image, against a
compile that is far dearer and a syscall that is 34–45 µs on its own. Two
paths holding the same bytes now share a module, which the old key could not
do.

The eight `pkg` cases each assert that the upgraded program's *output* changed
and none of them caught this, because their fixtures are `#!/bin/sh` scripts —
never compiled, so never cached. The `stale` case runs two real binaries at one
path, which is the shape that was missing.

## The screen parses, and the grid is still the model

[ANSI_Escape_Codes.md](ANSI_Escape_Codes.md) was written as a specification with
nothing behind it. Its §4 is now [src/kernel/ansi.cpp](../src/kernel/ansi.cpp);
its §5 — what a program sends for a key — is still nobody's but the program's.

**The invariant that changed is smaller than it looks.** "No ANSI escapes, no
VT100" said two things at once: that the terminal is a grid rather than a byte
stream, and that no byte stream may reach it. The first is untouched and is the
whole point — colours are still struct fields, `Sys::ScreenBlit` still blits,
and `sh`'s line editor, `less`, `edit` and `clear` still paint cells without a
parser between them and the grid. Only the second gave way, and only for guests:
`simbesm`'s BESM-6 speaks escapes to its console because Unix v7 does, and an
`ssh` client after it will have the same problem. Writing a *second* encoding
into the grid is cheaper than teaching every guest ours.

**The parser sits beside the grid, not inside it.** `ansi.cpp` reaches the
screen through the public `screen_*` calls and never sees `Term`'s fields, so
there is exactly one writer to the cells and the operations §4 maps onto are the
same ones a native program gets. The alternative — a state machine folded into
`screen_write` — would have doubled `screen.cpp` and given the parser a private
door to the cells, which is the door the invariant exists to keep shut.

**The scrolling region is the grid's, the modes are the parser's.** `LNM`,
`IRM` and `DECAWM` change what a byte *means* and never outlive the parser, so
they live in `Ansi`. The margins are different: `screen_newline` is called
directly by the console pump and by `boot.cpp`, and a newline at the bottom
margin has to scroll the region for everyone, not only for a writer who came
through the parser. So `Term` holds them, and `scroll_span` is the one place
rows move.

**A partial region is not the grid moving up.** `screen_scrolled()` counts rows
the whole grid lost, and `Sys::Echo` subtracts it from an anchor row the line
editor is holding. Rows above a region's top margin do not move, so counting a
region's scroll would walk that anchor in the wrong direction — and a region
scroll fills no scrollback either, since nothing left the top of the screen. On
the default region, which is the whole screen, every path is what it was.

**Italic is cyan because the grid has three attribute bits and none of them is
italic.** A manual page's italic would otherwise be lost entirely. The rule that
makes it survive nesting is that `ESC [ 3 m` remembers the foreground only if
italic was off, so a second one does not overwrite the shadow with cyan, and any
explicit colour forgets it.

**Nothing answers `ESC [ 6 n` or `ESC [ c`, and nothing will.** There is no path
from the screen back toward a process's input: the grid is what a program writes
into and the keyboard is a separate channel with its own claim. A guest that
waits for a reply hangs, and that is the guest's problem — inventing a reply
path would put a byte stream into the ABI, which is the thing this whole design
does not have.

**The alternate screen is swallowed, for now.** `? 47`, `? 1047` and `? 1049`
ask for exactly what `FullScreen` ([src/user/tty.h](../src/user/tty.h)) already
does: snapshot the grid, blank it, put it back. Doing it a second time at the
byte level means a second heap block of cells and a second lifetime to get
wrong, and a program that wants the screen can ask for it. If a guest turns out
to need the sequence itself, `screen_alt` is a small addition; until one does,
§4.5's row says swallowed.

**Sticky state must not outlive the program that set it.** Margins, insert mode,
autowrap and a hidden cursor are the `Term`'s, and a guest that dies without
`ESC c` would leave the shell painting into a three-row window. Three recovery
points close it: a `FullScreen` claim resets the parser at both ends, so a
program starts from a known terminal and hands one back; and `Sys::ScreenClear`
resets it too, which makes `/bin/clear` the `reset` this system does not
otherwise have. That is a small widening of what `clear` means, and it is worth
it — the alternative is a wedged terminal with no way out but a resize.

**A rune split across two writes is now carried.** `screen_write` used to drop a
truncated UTF-8 tail and start the next call mid-sequence, showing U+FFFD. §3's
first rule already required the parser's state to survive a buffer boundary; the
four bytes that carry a partial rune are the same rule applied one level down,
and a guest streaming through an eight-slot pipe splits runes routinely.

**Two behaviour changes fall out of §4.1.** A tab now moves to a stop instead of
painting a cell, so `cat` of a tabbed file lays out in columns. And `cat` of a
binary file now interprets whatever escapes it contains, which is the feature
and also how a user wedges their terminal — see the paragraph above for the way
out.

**The authorisation surface widened, deliberately.** `Sys::Cursor`, `Sys::Style`
and `Sys::ScreenClear` are refused to a process that does not hold the screen
claim; `Sys::Write` is not claim-checked, and now carries `ESC [ H`,
`ESC [ 3 1 m` and `ESC [ 2 J`. So a writer without the claim can reach all three
through its own stdout. This cannot be gated — `screen_write` has no pid to
check against — and it is not much of a loss: a writer could already scribble
anywhere by writing enough text, and the claims are there to stop two programs
interleaving on one screen, not to confine one to its own rows.

`kernel.wasm` went from 195,260 to 204,369 bytes against a 262,144 budget.

## `/etc/init`: the program a site boots into

0.9 gave a program a second screen ([`Sys::TermOpen`](System_Calls.md)), and
named the caller it was for: an emulator with a second console line. That caller
is [simbesm](https://github.com/besm6/v7besm), a BESM-6 booting Unix v7 with a
getty on each of its two Consul lines — and building the site for it found the
other half missing. The page could say which screens the emulator got, and could
not say that the emulator was what the tab was *for*: init ran `/bin/sh`, a
`constexpr` in [src/user/boot.h](../src/user/boot.h) with a comment saying
outright that nothing configures it. The visitor met a prompt and had to type a
command to reach the thing they came for.

So `/etc/init`: one line, the path of the program init runs on terminal 0.

**A file and not a mount option.** `mount({init: "/bin/besm6"})` reads better on
the page, beside the `shell: false` that arranges the screens — but a string has
to cross the JS boundary to get in, and §3.4 fixes that boundary at nine exports
and seven imports, asserted name by name and arity by arity in
[test/system/abi.mjs](../test/system/abi.mjs). A tenth export was never on. The
service route was: §6's rule is that *a new service is an enum value on each
side*, and `SvcOp::HostInfo` — a host-owned string, asked once at boot and
cached — is the exact precedent. What decided it against was that the *archive*
is where the rest of a site's own content already lives: `/etc/motd`, `/bin`,
and now the one line saying what the tab runs. A site that ships an archive
ships this with it, and nothing has to be threaded through `braam.js`,
`worker.js`, `svc.js` and `fakesvc.mjs` to say it. It costs one `read_file` on
the boot path, beside the motd's.

**Absent, empty and unreadable all mean the shell.** An archive without the file
boots exactly as it did, which is what makes this safe to add to a released
format. The line is trimmed and cut at the first newline; nothing checks the
shape of the path, because `exec_resolve` already has the only rule there is —
a bare word goes through `PATH`, a path does not.

**Terminal 0's alone.** `term_watch` starts `/bin/sh` on every terminal the host
makes later, and that does not change. The page's example is one program with a
panel: the second screen is that program's, opened with `TermOpen` and marked
`shell: false`, not a second copy of it.

**`SHELL=/bin/sh` stays in the environment.** The variable means the user's
shell — what `sh -c` and a `#!`-less script get — not what init happened to run.

**The restore offer is withheld.** A shell that will not resolve is offered an
unpack, because the archive is `/bin` and `/etc` and the shell is in it. A
program `/etc/init` named may be anywhere, so the offer would be a false promise
and the ending says something else: *there is nothing to run*. Every other line
about the program is the shell's with the name swapped in, and `called()` keeps
`/bin/sh` reading as "the shell" so the system suite's existing assertions hold
byte for byte.

**And boot stops reporting itself.** The BESM-6 page put a simulator on the
screen and got seven lines of ours in front of it: the version, then what
browser, machine and store this is, then `unpacked N files` on the first visit.
The first line is the record that braam booted and handed the grid over, and it
stays. The rest is braam talking about itself in front of somebody else's
machine — and it was never the only copy, `/proc/host` holding the same facts
for `uname` and anyone else who asks.

**The archive file doubles as the switch**, rather than an `/etc/quiet` beside
it. A marker file would be orthogonal — a site could have its own init *and* the
banner — but nobody wants that pair, and the cost of the option is a second
concept in the boot format and a second `read_file` on the path. The rule reads
as one sentence instead: *an archive that names its own program owns the grid
from the version line down.* A mount option was out for the reason the init path
was: §3.4's boundary.

**The cost is that the decision moved below the unpack.** `/etc/init` is a file
in the store, and on a first visit the store has nothing in it until the archive
is unpacked — so boot cannot know whose grid it is until after that. The rows
therefore print after the mounts rather than before them, and `unpack_if_stale`
hands its count back for the caller to announce instead of announcing it. Asking
the host still happens early, where it was: it is a round trip, `learn_host`
caches it for `/proc/host` either way, and only the printing waits. A plain boot
prints what it always did, in the same order, except that what happens between
the mounts and the unpack now comes above the rows rather than below them: the
upgrade question, a `/proc` or `/dev` that would not mount, an archive that
would not unpack. That reads at least as well. A root mount that fails prints no
rows at all now, having returned before the decision point — a fatal wants its
own line and not a description of the machine it happened on. Errors and the
question themselves are never withheld: they are interaction and not news.

Covered by [test/system/initprog.mjs](../test/system/initprog.mjs): a named
program runs and no prompt appears, the line is trimmed, a missing one says so
and is not offered an unpack, an empty file is the shell — and the host rows are
absent with a program named and present without one.

## The page that failed silently

`web/embed.html` is the one that demonstrates the *embedding* arrangement — two
kernels on a page, a worker each, sharing nothing but the origin's storage
(§3.5) — so it is what somebody reads before putting a terminal on a site of
their own. It was also the only page of the four that said nothing when it went
wrong.

It had fallen a generation behind. `index.html`, `dual.html` and `quad.html`
each picked up a noscript notice, a boot watchdog and a `#status` pane as those
were written; `embed.html` was not touched again and kept none of them.
Scripting off was a blank page. A `braam.js` fetch a blocking extension held
open was a blank page. A browser without `OffscreenCanvas` was an uncaught throw
in a console. And a boot stuck behind any of that was a black canvas, because
`mount()` was called with no `onError` at all, so its own stall report — the
report written precisely for that case — went to `console.error` where nobody
was looking. The page most likely to be opened by somebody who does not yet know
how braam boots was the page that told them least.

None of this is new work; it is four blocks copied from `dual.html`, which is
the reference for a page with more than one pane. What is new is the one thing
those pages have no need of.

**A diagnostic says which kernel spoke.** `dual.html` and `quad.html` are one
`mount()`, so one boot watch and one voice. `embed.html` is two `mount()` calls
and therefore two independent stall timers, and an unprefixed pane would show
the same "boot is stuck" line twice with nothing to tell the two workers apart —
which is the opposite of what a diagnostic is for. So each mount takes an
`onLog`/`onError` pair that names it, and the two share the one status pane.

**One `try` for the pair.** `mount()` checks `transferControlToOffscreen` on
every spec before it makes a worker, and that is a property of the browser, not
of a canvas: the two mounts fail together or succeed together, so there is no
half-mounted page to unwind. The `catch` disables the dispose button, since a
page that mounted nothing has nothing to dispose.

**The key bars are per kernel.** `braam.js` only appends buttons into the
container a spec names and styles nothing; which bar is visible is the page's
CSS, keyed off a `data-pane` flag the page keeps in step on a pointerdown.
`dispose()` already removes that pane's buttons and its focus ring, so disposing
the right kernel needs one line here — move the flag back to the left, so the
bar still on screen belongs to the kernel still running.

The arrangement itself is unchanged, so §3.5 is untouched. `index.html` is still
short of the focus ring and the `overflow: hidden` the two multi-pane pages
have; that is a separate tidy.

## The other half of the escape: what a program sends for a key

[ANSI_Escape_Codes.md](ANSI_Escape_Codes.md) §5 was a specification with nothing
behind it, as §4 had been. It is now
[src/proc/keyenc.cpp](../src/proc/keyenc.cpp): `key_encode(Key, char[8])`, the
bytes and the count.

**It is not the kernel's, and that is the whole design.** A key reaches a
program as `Key{code, mods}` and there are no control characters anywhere in
the ABI — `^D` is `'d'` with `MOD_CTRL`. Teaching the console pump to emit
bytes would put a byte stream on the *input* side to match the one §4 put on
the output side, and the input side does not need one: nothing native reads
keys as bytes, and the two programs that will are a guest emulator and an ssh
client. So the encoder is in the process runtime, `--gc-sections` keeps it out
of the fifty-odd binaries that never name it, and **nothing in this tree names
it at all.**

That last point is the argument for
[test/unit/test_keyenc.cpp](../test/unit/test_keyenc.cpp). A pure `src/proc/`
half with no in-tree caller would otherwise rot with nothing to notice, so the
case is not a nicety — it is the only thing holding the table true. Compiled
into `tests.wasm` like `proc/opt.cpp` and `proc/time.cpp`, which also makes a
syscall in it a link error.

**Three decisions inside the table.** CSI and never SS3 for the arrows and
Home/End, because nothing tracks DECCKM — the screen swallows `ESC [ ? 1 h`
(§4.5), so a program cannot know it was asked for, and the CSI forms are what a
terminal in normal mode sends and every decoder accepts. No timer for the
Escape key, because §6.5 says the ambiguity does not exist on this side: a
`Key` arrives whole and `KEY_ESCAPE` is a code. And a printable key goes out in
**UTF-8**, which is what makes a guest's eight-bit line carry Cyrillic rather
than dropping it.

`KEY_ENTER`…`KEY_F12` are 26 consecutive codes in
[src/kernel/key.h](../src/kernel/key.h), so §5.2 is one indexed table rather
than a 26-arm switch, and §5.3's parameterised form shares its rows: the final
byte and the tilde parameter are what both need.

**No kernel change, and no ABI change.** `kernel.wasm` did not move.

## The regex test got an oracle that is not a host

`regex.data` was the honest thing to write when the engine arrived: eh's native
differential harness could not come along, so `tools/mkregexdata.py` asked the
host's own `<regex.h>` the same 2,211 questions once and wrote the answers down.
What it records, though, is *one libc's* answers. The tool has to pick
`REG_NEWLINE`'s value by platform because glibc and the BSDs disagree; three
behaviours had to be lifted out of the table entirely and asserted by hand
because the hosts disagree with each other about them; and a regeneration on a
different machine would quietly move the baseline. A table like that can prove
the engine has not changed. It cannot say the engine is right.

AT&T's `testregex` is the thing that can. Glenn Fowler's harness is the
reference conformance driver for POSIX `regex(3)`, and its answers are the
standard's rather than an implementation's — `(a|ab)(c|bcd)(d*)` on `abcd` has
one POSIX-correct subexpression split, and the corpus says which.

**What was adopted is the data, not the driver.** `testregex.c` wants `stdio`,
`setjmp`, `signal` and `alarm`, and a program is not what the in-wasm suite can
run. So the seven `.dat` files are vendored into `test/unit/att/` verbatim —
the diff against upstream is two lines a file, `R"DAT(` and `)DAT"`, which is
the `solve.data` idiom and keeps a re-sync a copy — and `test_attregex.cpp` is
the driver's main loop reduced to what this corpus actually uses. Profiling it
first kept that small: the corpus needs the `B` and `E` dialects, three
modifiers, `SAME`, an `nmatch` override, two `{` `}` blocks and the
categorisation lines, and nothing else. No `RE_DUP_MAX`, no `NIL`, no locales,
no `A`/`S`/`K` dialects. The part lifted exactly is `matchcheck()`, including
the two rules a looser comparison would drop: every slot past the answer must
read `(-1,-1)`, and the slot past `nmatch` is a sentinel that must come back
unwritten.

**The files are not all pass/fail, and treating them as if they were would be
wrong.** `basic`, `forcedassoc`, `nullsubexpr` and `repetition` are the
conformance set. `leftassoc` and `rightassoc` are a pair AT&T does not expect
any implementation to pass both of — glibc passes the first, the BSDs the
second — so what is asserted is the categorisation, not a winner. And
`categorize.dat` is a report: fourteen groups, each naming the category the
engine falls into. Pinning those fourteen answers turns it into an assertion
that names the axis when the engine moves.

**The first run found one plain bug, and it is fixed rather than recorded.**
`a{9876543210}` compiled: the bound was accumulated into an `int` with nothing
watching it, so a long enough run of digits wrapped into whatever it wrapped
into. There is a `DUP_MAX` of 32767 now — testregex.c's own default, and
glibc's — the accumulation stops there rather than wrapping, and past it is
`REG_BADBR`. `test_regex.cpp` carries the bound, the wrap and the unclosed
brace beside it, since the corpus only has the one spelling.

**Thirty-five of 449 cases are deviations, and the entry is an assertion too.**
A listed case that starts *passing* fails the suite as loudly as one that
starts failing, so the list cannot rot in either direction. Two are documented
absences — `[[.x.]]` and `[[=x=]]`, which `regex.h` already says are not here.
The other thirty-three are one property: **POSIX assigns subexpressions by
leftmost-longest applied outward, and a backtracking matcher reports the split
it reached success by.** The whole match is right in all but two of them; what
differs is which group inside it got what, and whether a starred group takes
the empty final iteration POSIX requires. `categorize.dat`'s verdict is
`SUBEXPRESSION=grouping` where the standard wants `precedence`, which is the
same finding in AT&T's vocabulary.

That is a real gap and it is now measured rather than assumed — which was the
point. Closing it is a matcher change, not a test change, and it is not made
here.

## The matcher learned where a subexpression ends

It is made here. Of the deviations above, four runs are the two documented
absences, counted once per dialect; the thirty-one that remained were the one
property — the count of thirty-three above is a line the bound fix left
behind — and they are gone. `basic`, `forcedassoc`,
`nullsubexpr` and `repetition` pass whole — 449 runs, no failures — and
`categorize.dat` now answers `SUBEXPRESSION=precedence` with every one of its
ten bug axes reading `EXPECTED`.

**What the engine was missing was not a rule but a comparison.** `accept()`
already enumerated every match at a start position and kept the longest; among
matches of the *same* length it kept whichever the backtracker reached first,
which is greedy-first and alternation-left-to-right, and that is what POSIX is
not. So the ingredient the engine lacked was a way to tell two equally long
matches apart — and that needs the *shape* of the parse, which nothing kept.

**A capture vector is not enough to compare with, and that is the difficulty.**
`((..)|(.)){2}` on `aaa` has two parses of length 3: turns of `aa`+`a`, and of
`a`+`aa`. POSIX wants the first, but its group 1 ends up at `(2,3)` and the
loser's at `(1,3)` — compare the *reported* captures and the wrong one wins on
leftmost. The rule is about the turns, and only the first turn distinguishes
them. So the matcher now keeps a **trail**: one entry per instance of a group,
of a repeat, and of a turn of a repeat, each with its extent and a parent index,
pushed as the parse is walked and truncated when it is backtracked. It is the
parse tree in preorder, and comparing two of them is POSIX's rule read
literally — settle a node, then its children left to right.

**Four rules, and each one is pinned by a case that would otherwise flip.**
A node settles before what is inside it (`((a*)(b|abc))(c*)` on `abc`), which
includes a repeat settling before its own turns: `(ab|a|c|bcd)*(d*)` on `ababcd`
has the repeat reaching 6 one way and 5 the other, and both parses end the whole
match at 6 because `(d*)` mops up — compare turn by turn first and the shorter
repeat wins on a longer second turn. Two subexpressions that are not the same
one are ordered by which opens earlier in the pattern, before either extent is
looked at, which is what keeps `(a|b)*c|(a|ab)*c` and `(.a|.b).*|.*(.a|.b)` on
their old answers. A repeat takes as few turns as it can — no trailing empty
one, `(a*)*` on `aaaaaa` — except that one empty turn beats none, `(a*)*` on
`x`. Everywhere else, the subexpression that took part wins over one that did
not: `((a|a)|a)` and `(ab)c|abc`.

**A repeat with no group in it is still an extent.** Nothing inside it can move
a capture, so its turns are not recorded — but `.*(.*)` on `ab` reports
`(2,2)` for the group precisely because the `.*` to its left is a subexpression
that settles first, and dropping its entry would report `(0,2)`, which is AT&T's
`BUG=subexpression-first`. `(a|b)?.*` on `b` is the same thing once more.

**Which of those entries to suppress is a property of the pattern, not of the
stack.** The first draft carried a depth counter incremented around an untraced
body, and it was wrong for a reason worth writing down: this is a CPS matcher,
so the continuation — the whole rest of the match — runs *nested inside* the
body's call. A counter around `mrep_simple` swallows everything that follows the
repeat, not the repeat's body. `mark_trace()` walks the tree once at compile
time instead and marks each repeat that sits inside a group-free body.

**Turns had to change as well, and this half is not gated on `nmatch`.** A turn
now clears its body's groups before it runs, so a branch the last turn skipped
reads `(?,?)` rather than keeping an extent nothing set — that alone is the
`iteration` family, and it is what makes `\(a\(b\)*\)*\2` on `abab` the
`NOMATCH` POSIX asks for, `\2` having been cleared by the turn that did not set
it. A turn that consumes nothing is now recorded rather than rolled back, with
the parse that omits it enumerated beside it and the comparison choosing; that
is what moves `\(a*\)*\(x\)\(\1\)` on `ax` from `(1,2)` to `(0,2)`, the only
place in the corpus where subexpression assignment moves the *whole* match.
Recording it changes what a later `\1` can match, so it must happen whether or
not captures were asked for — a `REG_NOSUB` run has to reach the same verdict,
and the harness checks exactly that.

**A bound of `{32767}` is one trail entry, not 32767 of them.** Turns owed to
`min` are taken even when empty — `X(.?){8,}Y` reports `(8,8)` where `{0,}`
reports `(7,8)` — but the remaining ones are the same empty turn in the same
place, so one entry stands for all. The collapse is sound because an empty turn
stops the expansion either way: interior empty turns were never enumerated, so
no parse is lost. `(a*){2000}b` answers rather than spending 2000 frames of
`MAX_DEPTH`.

**Grep pays nothing for any of it.** The trail is recorded only when `regexec`
was asked for captures — `nmatch > 1` and a group in the pattern — and
`/bin/grep` compiles `REG_NOSUB` and passes 1. Where it is on, the comparison is
charged to the same budget the rest of the matcher is, and past 4,096 entries it
is switched off and the incumbent stands, since comparing against a truncated
trail says nothing. Measured either way, the pathological patterns take what
they took before; what grew is the code, by 2,522 bytes, which
[Compat.md](Compat.md)'s cost table now carries.

**The pinned meta-assertions moved, which is what they are there for.** The
engine is cleanly right-associative now — twelve of `rightassoc.dat` and none
of `leftassoc.dat`, where it was right on eight and left on four, that split
having been the same gap under another name. `test_regex.cpp` gained the rules
the corpus states only through its answers, and its 2,211 host-recorded cases
needed no change: the ambiguous ones there, `(a*)*b` and its kin, are cases
where this host and POSIX agree.

## A collating element is a character here

The last two deviations were `[[.NIL.]]` and `[[=aleph=]]`, both wanting
`ECOLLATE` from a parser that had never heard of either bracket. They are
implemented now, and the simplest reading is the correct one: a collating
element is the character it names and an equivalence class is the set of
characters that sort as it, so where every character sorts as itself — what a
system with one locale and no collation table has — `[[.x.]]` and
`[[=x=]]` are both `[x]`. Multi-character names have no meaning to give them,
and that is `REG_ECOLLATE`, a code the header did not have before and now has,
appended rather than slotted into POSIX's numbering so that nothing else moves.

It is one function, `bracket_point()`, because an endpoint is an endpoint:
`[[.a.]-[.c.]]` and `[x[.-.]y]` fall out of writing it once and calling it for
both sides of a range. The name runs to its own terminator rather than to the
first `]`, so `[[.].]]` is a bracket holding `]`.

**The corpus now passes whole — 449 runs, no failures, no deviations.** The
table stays where it is, empty, with `deviation_for()` still walking it: it is
the shape an entry would take, and the assertion that an entry starting to pass
is as loud as a case starting to fail is worth keeping armed.

## Node 22.12, checked at configure time

Both suites import `web/*.js` from Node, and those files are the browser's: ES
modules with no `package.json` to say so, and a zip reader over
`DecompressionStream("deflate-raw")`. Node loads the first as ESM only from
22.12, where syntax detection became the default, and has the second only from
21.2. Under the Node 18 an Ubuntu 24.04 `apt install nodejs` gives, the build
succeeded and `system` and `unit` both died on their first import, which read as
a broken tree. A `package.json` in `web/` would cure the import and not the
decompressor, and would ship with the site; `test/CMakeLists.txt` refuses an
older Node when configuring instead, naming the version it found.

## Size classes are powers of two, up to half a span

The allocator had ten small classes, 16 to 512 in steps of about half, and
everything past 512 bytes took whole 64 KiB spans. That was sized for coroutine
frames (Concept.md §8.2) and was right for them, but a ported program allocates
in the middle: a kilobyte buffer, a 4 KiB node, a hash table of a few thousand
bytes. Each of those cost 64 KiB, so a thousand one-kilobyte blocks held
64 MB of spans — most of a process's 100 MB — for 1 MB of data.

The classes are now the powers of two from 16 to 32 KiB, twelve of them. Half a
span is the ceiling because it is the largest class of which a span still holds
two; one past it takes whole spans as before, where rounding up to 64 KiB wastes
at most half. A thousand one-kilobyte blocks now take sixteen spans, and
`test_alloc` holds it to twenty.

Powers of two make `class_of` a count of leading zeros rather than a loop over a
table, and it runs on every allocation. The price is the classes that went: 48,
96, 192 and 384 now round to 64, 128, 256 and 512, so the worst internal waste
below 512 rises from a third to a half. `FS_BLOCK`, `FILE_BUF` and `PATH_MAX`
stay 512, which is still a class exactly — what changed is only that it is no
longer the top one, and a frame a byte past it now costs 1 KiB instead of 64.

## `PACKAGE_MAX` is 50 MiB, and a fetched archive grows to its size once

`PACKAGE_MAX` was 4 MiB, set at 0.4 when a process had 16 MB and the archive is
held whole. 0.9 raised the process to 100 MB and the stage cap with it, but left
this bound where it was, and a port of an interpreter with its library does not
fit in four. It is now 50 MiB, the same as `UNPACK_MAX`, so the bound on what
arrives no longer refuses a package the bound on what it unpacks to admits.

A 50 MiB archive in a 100 MiB process leaves little room for a second copy.
`ZipSink` gathers the body in a `String`, whose capacity doubles, and at the top
of a doubling the old buffer and the new one are both alive — 32 MiB and 64 MiB
for a 50 MiB archive, which is nearly all of the process before a byte is
hashed. `ZipSink::take` now reserves for itself: it doubles as before, but never
past the declared size, so the last growth lands on exactly `S` and the pair
alive at once is under twice it, where a doubling past it could be three times.
`index.cpp` bounds an index body through the same sink and gets the same.

## zlib, rewritten rather than vendored

`braam::zlib` is the SDK's third library beside `braam::math` and
`braam::regex`. Until now a program could inflate only through `Sys::Inflate`,
which is the host's `DecompressionStream`. It could compress nothing, and it
could not compute a CRC-32 or an Adler-32 without writing its own. A port that
speaks gzip, zlib or PNG had nothing to link.

**It is a rewrite of zlib 1.3.2.1 in C++, not zlib's C with a prologue.**
musl's libm went the vendored way, but zlib does not fit that shape. Its
`zutil.h` wants `<string.h>` and `<stdlib.h>` unless it is built `Z_SOLO`, and
`Z_SOLO` makes `compress()` and a zero `zalloc` fail outright. So vendoring
would have meant either shim headers answering libc names inside the library,
or an API that refuses its own defaults.

The rewrite keeps the algorithms step for step: inflate's modes, the fast path,
the table builder, deflate's hash chains, the lazy match and the block choice.
It changes the rest:

- Its types are the tree's: `Span`, `Str`, `Result` and `heap_alloc`.
- The fixed Huffman tables and trees.c's static trees are built at compile time
  by the same code that builds the dynamic ones, rather than carried as
  generated headers.
- deflate.c and trees.c are one file, so there is no private header for the
  install glob to ship.

**Byte-identity is the oracle, and that is why the translation is so literal.**
A deflate may choose any valid encoding, so a round trip proves only that
inflate undoes deflate. `tools/mkzlibdata.py` therefore records what the host's
zlib makes of eight inputs across every level, strategy, window and memory
level. `test_zlib.cpp` requires the same bytes, which pins every heuristic.

The host's zlib was 1.2.12, and it agreed on all 300 cases. So the two changes
the byte-identity has to allow for were known in advance:

- **The gzip OS byte.** macOS writes 19 and this writes 3, Unix. The tool
  normalises it before taking the CRC.
- **Level 0's stored blocks.** They are cut to whatever output room deflate is
  given, and Python grows that room as it goes. So level 0 is recorded only for
  inputs that fit Python's first output block.

The tool refuses zlib-ng, whose streams are valid but not zlib's.

**The API is Braam's, and zlib's C API is the port kit's.** A native caller
gets two objects, `Deflater` and `Inflater`, stepped over a span in and a span
out. Their status is a `ZStatus` that names zlib's return codes rather than
numbering them. A `PORT` target gets `<zlib.h>`, zlib's own `z_stream` API,
from `czlib.cpp`. That file is an adapter: the stream's fields are copied in
before each call and out after. `gz_header` and `ZHeader` are the same layout,
asserted, so a header pointer passes through unchanged.

`zalloc` and `zfree` are accepted and never called. A port that counts its
allocations through them sees nothing, but none of the ports in view does.

**`Sys::Inflate` stays.** The kernel does not link `braam::zlib`, and a leaf
library that the kernel reached would stop being a leaf. The host's inflate is
also native and already streams out of a descriptor. What changed for `/bin/pkg`
is only the unit suite: `test_zip.cpp` now inflates every entry of `rootfs.zip`
a second time, with this code, and compares the two answers. So Python's
deflate is checked against two independent inflates.

**`gz*` is absent**, each function a compile error that names its replacement.
`gzopen` is a file, and a file here is a coroutine (Compat.md §4). A
synchronous `gzread` over it would be the blocking call the port kit exists to
refuse. A gzip file is `inflate()` with `windowBits` 31 over bytes the program
read. `inflateBack` goes too: it is a second inflate driven by callbacks, and
`inflate()` does the same work.

**Every call is synchronous.** Nothing in zlib waits, so `step` computes and
returns. A caller with megabytes steps a chunk at a time, as `examples/zpipe`
does, and the event loop turns between the chunks. This is the same bargain as
`regexec`.

The zlib licence asks that an altered version say so. `src/zlib/LICENSE` says
it and keeps the original notice. The SDK installs that file as
`share/doc/braam/zlib-LICENSE`, and each source file names the zlib file it was
translated from. `examples/zpipe` is zlib's own example rewritten. It is the
in-tree caller that keeps the SDK side building, and the `sdk` test
round-trips `/etc/help` through it under the installed harness.

## bzip2, rewritten beside zlib

`braam::bzip2` is the SDK's fourth library. A port that reads or writes `.bz2`
had nothing to link, and the host has nothing to lend: `DecompressionStream`
knows gzip and deflate, and not bzip2.

**It is rewritten in C++ for zlib's reason.** `bzlib_private.h` includes
`<stdlib.h>` whatever it is built with. Built `BZ_NO_STDIO`, it wants a
`bz_internal_error` from its caller in place of the `exit(3)` it would have
called. Vendoring would have meant shim headers answering libc names inside the
library, as it would have for zlib.

The algorithms are kept step for step: the run-length front end, both block
sorts, the move-to-front and Huffman coding with its four refining passes, and
the decoder's resumable state machine. The rest changed:

- The types are the tree's: `Span`, `Str`, `Result` and `heap_alloc`.
- `EState` and `DState` became `BzEncodeState` and `BzDecodeState`, since a
  type name must be unique across the tree. The fields are in snake case.
- `crctable.c` is built at compile time. `randtable.c` stays a literal, since
  it has no rule to build it from.
- `bzlib.c` is split between `compress.cpp` and `decompress.cpp`, as its two
  halves share nothing.
- The decoder's four output loops, fast and small each with a randomised twin,
  became one template over the byte source. That includes upstream's hand-cached
  fast path, and the output is the same.

**There is one private header, and it keeps upstream's name.** zlib's rewrite
avoided one by merging deflate.c and trees.c. Here the block sort, the
compressor and the Huffman builder all need the encoder's state, and merging
them would make a 1,700-line file. So `bzlib_private.h` is shared, and the
install rule's new `*_private.h` pattern keeps it out of the SDK. The pattern is
general: any library's private header takes that name.

**An internal check fails the stream, not the process.** Upstream's `AssertH`
prints an appeal to report the bug and calls `exit(3)`. A library that ends its
program is not one a program can hold. Here each check sets `bug`, the call
returns, and every later call on that stream answers `Misuse`. No input reaches
them, so nothing tests them. The one upstream's message says is known to fire,
1007, it blames on unreliable memory.

**Three things the decoder does differently, none visible on a good stream:**

- An error is sticky. Upstream leaves the state wherever it stopped after
  `BZ_DATA_ERROR`, and a further call parses on from there.
- `Stuck` is distinguished from progress. Upstream answers `BZ_OK` whether or
  not the call moved, so the shim maps `Stuck` back to `BZ_OK`.
- The block-start fetch returns `Corrupt` when out of range. Upstream's
  `BZ_GET_FAST` inside `BZ2_decompress` returns `True`, which is `BZ_RUN_OK` by
  number, and skips saving its locals. The checks before it make that
  unreachable; it is a status now so that nothing depends on that.

**Byte-identity is the oracle again.** `tools/mkbzip2data.py` asks Python's
`bz2`, the host's libbzip2 1.0.8, for ten inputs at every block size. All 90
agreed on the first build. It refuses any libbzip2 but 1.0.x, and bzip2's output
has not changed since 1.0.3 capped codes at 17 bits.

One test comes free: the work factor chooses between the main sort and the
fallback, and both build the same order. So `test_bzip2.cpp` compresses at
factors 1 to 250 and requires the host's bytes each time. Blocks under 10,000
bytes always take the fallback, and the periodic input exhausts the main sort's
budget, so both sorts are pinned to libbzip2's output.

**The decoder has two oracles that are not the compressor.** `sample3.bz2` is
from the bzip2 distribution. The other is a randomised block, which bzip2 0.9.0
wrote when its sort was too slow for repetitive data. Every version since 0.9.5
writes none, but a decoder must still read them, and no tool makes one now. The
generator builds one from text with no run of four:

1. XOR the text with 0.9.0's mask.
2. Compress it.
3. Set the block's randomised bit and give both CRCs the text's.
4. Require the host's libbzip2 to give the text back.

Both decoders read it. Without it, `rand_update` would be dead code.

**A flush is not a sync point**, unlike zlib's. It ends a block, but the last
bits of that block wait in the bit buffer for the next one. So the output so far
does not decompress to the input so far. The test checks that the flush makes a
second block, and nothing more.

**The one-shot takes streams in a row and refuses anything else.** `bunzip2`
reads concatenated streams, and `pbzip2` writes nothing but. `bunzip2` warns
about trailing garbage and ignores it. `bzip2_uncompress` refuses it, because
otherwise a truncated second stream would pass as a whole first one.

**The memory is upstream's, rounded to spans.** At block size 9 a compressor is
7.6 MB and a decompressor 3.7 MB, or 2.4 MB in small mode. `ftab` is 65,537
words, four bytes past four spans, so it takes five. It stays at that size,
because `mainSort` indexes entry 65,536.

In the port kit, the compressor's arm is +19,072 bytes, the decompressor's
+21,397, and both +39,380.

**Checked beyond the suite**, before any of it went in:

- A native build under ASan and UBSan compressed 24 files at block sizes 1, 5
  and 9, identical to the host's `bzip2` each time. The files ranged from empty
  to a megabyte of zeros, a 1.5 MB word list and incompressible noise.
- The same build decompressed 1,200 bit-flipped and truncated streams. None
  crashed, and each was refused with a reason.

libbzip2's licence asks that an altered version be plainly marked.
`src/bzip2/bzip2.h` says it is altered, `LICENSE` is the original, and the SDK
installs it as `share/doc/braam/bzip2-LICENSE`. `examples/bzpipe` is `zpipe`'s
twin. The `sdk` test round-trips `/etc/help` through it, and it makes the host
`bzip2 -9`'s bytes on a 1.2 MB file under the kernel.

## liblzma, vendored rather than rewritten

`braam::lzma` is the SDK's fifth library. It gives `.xz`, `.lzma` and, for
reading, lzip's `.lz`. Like bzip2, the host has nothing to lend here:
`DecompressionStream` knows gzip and deflate and nothing else.

**It is liblzma 5.8.4 itself, not a rewrite, because liblzma does not have
zlib's problem.** zlib and libbzip2 were rewritten because each wants
`<stdlib.h>` in a way no build flag turns off. liblzma wants the same headers,
`<stdlib.h>`, `<string.h>` and `<assert.h>`, all through one file,
`sysdefs.h`. But it calls only six functions from them, and its
single-threaded build already steps around `<signal.h>` on a wasm target. So
the answer is musl's: vendor the C byte-identical, and answer the headers
privately. About 20,000 lines of C in 76 files build with no line changed,
and that pays for itself at every release: a re-sync is a copy.

How it is laid out:

- `src/lzma/liblzma/` is xz's `src/liblzma/` and `src/lzma/common/` the nine
  files of xz's `src/common/` it includes, both under `-w` and a `DisableFormat`
  `.clang-format`, as musl is. Every `.c` present is built.
- What was left upstream is listed in `src/lzma/CMakeLists.txt`: the threaded
  coders and their output queue, the table generators, the x86, ARM64 and
  LoongArch CRC code, and the big-endian tables.
- `src/lzma/sys/` answers `<config.h>`, `<stdlib.h>`, `<string.h>` and
  `<assert.h>`. It is on the vendored C's include path alone and is never
  installed.
- `config.h` is a full single-threaded build: every filter, every match finder,
  all four checks, and the lzip decoder. `HAVE_INTTYPES_H` stays undefined,
  because clang's `<inttypes.h>` is an `include_next` into a libc.
  `sysdefs.h` supplies the `PRI*` macros it would have wanted from it.

**The six functions are renamed, not defined.** `sys/string.h` maps `memcpy`,
`memmove` and `memset` to their builtins, which are bulk-memory instructions
here. It maps `memcmp`, `memchr` and `strlen` to `lzma_sys_*` names, and
`sys/stdlib.h` does the same for `malloc`, `calloc` and `free`. `sys.cpp`
defines those six over `heap_alloc` and `heap_free`. Defining the libc names
themselves would work until a port linked the kit, whose `malloc` and
`strlen` are C-linkage definitions of the same names. The archive's only
undefined symbols outside itself are `heap_alloc` and `heap_free`. Since both
sides allocate on the one heap, a port may `free()` what liblzma hands back.

**There are no threads, so the threaded API is a compile error.**
`lzma_stream_encoder_mt`, its `_memusage`, and `lzma_stream_decoder_mt` are
declared in upstream's header, and without `MYTHREAD_*` nothing defines them.
`lzma/lzma.h` redeclares the three `unavailable`, naming the single-threaded
calls. A port finds this out at the call, not at the link, as with zlib's
`gz*`. `lzma_physmem` and `lzma_cputhreads` stay in and answer 0, liblzma's
answer on a system it cannot ask.

**The API is both, and neither restates the other.** zlib's and bzip2's C APIs
are adapters over a native pair. Here the C API is the library, so native code
may include `lzma/lzma.h` exactly as a port includes `<lzma.h>`. That header is
a one-line forward to it. `lzma/xz.h` adds `XzEncoder` and `XzDecoder` in
bzip2's shape, for the common case, with a status that names liblzma's return
codes. Filter chains, raw streams and the index stay in the C API.

The pair differs from liblzma in four places:

- **`More`**, as bzip2 has. liblzma answers `LZMA_OK` both while a flush is
  under way and after a plain run, and `LZMA_STREAM_END` when a flush is done.
  So `step` says `More` during a flush or finish, `Ok` when a flush is done, and
  `End` only for `Finish`.
- **The decoder's `finish`.** Every `.xz` and `.lz` is read concatenated, as
  `xz` and `lzip` read them, so the end of one stream is not the end of the
  input. `LZMA_FINISH` is what says it is, and `step` takes it as a flag.
- **A cut stream is `Corrupt`.** Under `finish`, liblzma reports a truncated
  stream as `LZMA_BUF_ERROR`, the same code as a call that could not move. The
  wrapper calls it `Corrupt` when there was room to write into.
- **The encoder's `memusage()`.** `lzma_memusage` answers only for decoders, so
  the encoder's figure is liblzma's own estimate for the preset, taken at
  `init`.

**Byte-identity is the oracle, and here it is identity with itself.** The host
has the same liblzma. `tools/mkxzdata.py` records 141 streams through Python's
`lzma`:

- ten inputs at presets 0 to 6 and two extremes;
- `.lzma` at three presets;
- every check;
- four filter chains, among them delta, x86 and SPARC.

All agreed on the first build. That proves the build, not the algorithms: the
`config.h`, the stub headers, the 32-bit `size_t` and the unaligned reads.
Those are this port's own contribution, and the easiest place for it to be
wrong. `xzpipe` under the kernel makes what `xz -T1` makes of
`doc/Concept.md` at presets 0, 3 and 6. Plain `xz` records sizes in each block
header, because 5.8 is threaded by default.

**xz's own test corpus is the decoder's oracle.** Upstream ships 96 small files
that are good, bad and unsupported, built by hand to reach each refusal. The
tool stores all of them whole but one 51 KB TIFF. With each it records what the
host's decoder made of the file whole and a byte at a time. It had to record
both, because one file differs between the two, in liblzma and so here too.
`good-known_size-with_eopm.lzma` declares its size and also carries an end
marker. Fed whole with `LZMA_FINISH` it is good. Fed a byte at a time, it is
`LZMA_DATA_ERROR` one byte before its end: the decoder reaches the declared size
before `LZMA_FINISH` has said an end marker may follow. Three lzip files named
`good` are refused as well, since they have trailing data. Upstream's README
says so: they are good only for lzip before 1.20, and liblzma under
`LZMA_CONCATENATED` agrees with lzip 1.20.

**The memory is liblzma's, and the process cap decides the presets.** An
encoder is 2.7 MiB at preset 0, 93 MiB at 6 and 673 MiB at 9. A process may
have 1,600 pages, 100 MiB, so preset 6 (xz's default and `xzpipe`'s) fits with
little to spare, and 7 to 9 do not fit at all. `xzpipe -7` says "out of memory"
and exits 1. The heap's own ceiling is 256 MiB, so the unit suite stops its
oracle at 6 too. A decoder needs the dictionary, which is 8 MiB at 6 and 64 MiB
at 9, so it reads every preset's output.

In the port kit, `lzma_crc32` is +8,611 bytes. That is slicing-by-eight tables
where zlib has slicing-by-four. `lzma_stream_buffer_decode` is +56,874,
`lzma_easy_buffer_encode` +72,023, and both +100,929. `xzpipe` is 142 KB.

liblzma is under the BSD Zero Clause License, which asks nothing. The SDK
installs `COPYING.0BSD` as `share/doc/braam/lzma-LICENSE` anyway, so the SDK
says where the code came from. `examples/xzpipe` is `bzpipe`'s twin, with a
preset flag. The `sdk` test round-trips `/etc/help` through it under the
installed harness and checks the `.xz` magic.

## libzstd, vendored with nothing over it

`braam::zstd` is the SDK's sixth library, libzstd 1.6.0, for Zstandard (RFC
8878). It is the format most archives and packages now choose, and the host
cannot help: `DecompressionStream` knows gzip and deflate and nothing else.

**It is vendored, as liblzma is, and for the same reason.** Its core is 35,000
lines of C, and it already expects to be built without a libc. `zstd_deps.h`
names every libc dependency, and the Linux kernel builds zstd by replacing that
one file. Replacing it here would be an edit to re-apply at every release.
Instead, `src/zstd/sys/` answers `<string.h>` and `<stdlib.h>` for the vendored
sources alone, as `src/lzma/sys/` does, and `zstd_deps.h` includes those as
upstream wrote it. `XXH_NO_STDLIB` keeps xxHash's own `<stdlib.h>` out, and
`DEBUGLEVEL=0` keeps `assert` and stdio out. **No line of upstream is changed.**
`src/zstd/lib/` is upstream's `lib/` less four directories and its build
files, and a re-sync is a copy.

- **Four functions are renamed, not defined.** `memcmp`, `malloc`, `calloc`
  and `free` become `zstd_sys_*`, in `sys.cpp` over `heap_alloc` and
  `heap_free`, for the reason liblzma's are. `memcpy`, `memmove` and `memset`
  are the builtins, and the archive's only undefined symbols outside itself
  are the heap's two.
- **Left upstream: `dictBuilder/`, `legacy/`, `deprecated/` and the amd64
  assembly.** The dictionary builder wants `qsort_r`, `clock()` and `fprintf`,
  and training a dictionary is a job for the machine that publishes the data,
  not the tab that reads it. A dictionary trained elsewhere loads as usual. The
  pre-1.0 formats have not been written since 1.0, in 2016. `ZBUFF_*` is an
  older streaming API that upstream marks deprecated.
- **There are no threads.** `zstdmt_compress.c`, `pool.c` and `threading.c`
  are compiled anyway, because `zstd_compress.c` names `ZSTDMT_*`
  unconditionally. Without `ZSTD_MULTITHREAD` each is its single-threaded
  build, and `ZSTD_c_nbWorkers` has an upper bound of 0.

**The API is libzstd's, with no pair over it.** zlib, bzip2 and liblzma each
got a Braam-shaped pair, because each C API is a struct of pointers and counts
that the caller keeps in step, with return codes that mean different things in
different states. zstd's streaming API is already the pair's shape: two
cursors, `ZSTD_inBuffer` and `ZSTD_outBuffer`, that the call advances, and one
`size_t` answer that is either an error or a hint. A wrapper would restate it.
So native code includes `zstd/zstd.h`, a one-line forward to `lib/zstd.h`, and
a port includes `<zstd.h>`, which forwards to the same place.

Two traps come with using a C API directly, and the manual names both:

- **The hint after a frame ends is the next frame's.** `ZSTD_decompressStream`
  answers 0 when a frame is complete. Called once more with no input, it
  answers the size of a frame header, as if another frame were coming.
  `zstdpipe` did exactly that at end of input and called its good streams cut
  short, before the loop learned to stop on 0. A frame whose output was an
  exact multiple of the buffer would have tripped it anywhere.
- **`ZSTD_getErrorName` answers a C string.** `Str(const char *)` is
  `__builtin_strlen`, which is a call at run time, and a loop counting to the
  NUL is recognised back into one. Nothing defines `strlen` for a program that
  is not a port, so this is a link error. `zstdpipe` counts inside
  `__attribute__((no_builtin("strlen")))`, the narrowest answer that leaves
  `braam_add_program` unchanged.

**The oracle is the same C, built for the host.** The host's libzstd is 1.5.7,
and a different release makes different bytes. The `zstd` command also pledges
the size it knows, so even the same release would disagree with a stream.
`tools/mkzstddata.py` therefore compiles `src/zstd/lib` with the host's `cc`,
under the defines CMake gives it, and records 290 frames through ctypes:

- twelve inputs, among them upstream's two small `golden-compression` files;
- sixteen levels, from −5 to 19, which reach all nine strategies;
- six sets of parameters: the checksum, no content size, a 1 KiB window, long
  distance matching, `btultra2` forced, and the smallest target block size;
- a raw dictionary, and upstream's golden one.

All 290 agreed on the first build. That proves the port, not the algorithms:
`sys/`, the defines, the heap, and 32-bit `size_t` against the host's 64-bit.
The compressor tests `MEM_32bits()` or `MEM_64bits()` thirteen times, and none
of them changed a byte here. The algorithms' oracle is upstream's own golden
files. The tool stores the six small ones whole, three good frames and three
corrupt ones, and records what the decoder made of each, whole and a byte at a
time. `block-128k.zst` and the two large compression inputs are left out for
size.

**Two decoders that are not this one agree with it.** The host's `zstd` 1.5.7
reads what `zstdpipe` wrote at levels 1, 3, 9 and 19, and `zstdpipe -d` reads
what it wrote. That was checked by hand. The `sdk` test hands `zstdpipe`'s
frame of `/etc/help` to Node's own zstd, in `node:zlib` in recent releases
(1.5.7 in the Node this was written with). On a Node without it, the test says
so and skips that half.

**The memory is libzstd's, and level 19 is the last that fits.** A compressing
stream of unknown size needs 3.5 MiB at level 3, 52.7 MiB at 12, 89.5 MiB at 19
and 193.5 MiB at 20, as `ZSTD_estimateCStreamSize` gives them under the
kernel. The cap is 100 MiB, so `zstdpipe` stops at 19. It compresses at 19
under the headless harness, with the harness binding the same memory maximum
the browser does. A decoder is 94 KiB plus the window, 8 MiB for level 19.
libzstd's default ceiling on a window is 128 MiB, which a process cannot have,
so a program reading untrusted frames should set `ZSTD_d_windowLogMax`. Then a
frame that asks for too much fails as `frameParameter_windowTooLarge`, not as
an allocation failure.

**The compressor is 320 KB.** The level is a run-time value, so
`ZSTD_compress` reaches every strategy's match finders, each inlined several
times over. The decompressor is 57 KB, and `zstdpipe` is 415 KB. upstream's
`ZSTD_EXCLUDE_*_BLOCK_COMPRESSOR` would cut the compressor down, but it applies
to the whole library, so a program could no longer ask for the levels it
removes. A program that only reads `.zst` pays for the decompressor alone.

libzstd is dual-licensed, BSD or GPLv2, and this tree takes the BSD licence.
`src/zstd/LICENSE` is upstream's `LICENSE` under a line saying which files are
vendored and that none is altered. The SDK installs it as
`share/doc/braam/zstd-LICENSE`. `examples/zstdpipe` is `xzpipe`'s twin with a
level flag. The `sdk` test round-trips `/etc/help` through it under the
installed harness, checks the magic, and hands the frame to Node.

## Waiting on several descriptors at once

A process could park on exactly one thing. `Read` is the only way to learn that
a pipe has bytes, and it consumes them; `CancelState::waiting` is one slot, so
a task cannot even be parked on two pipes by hand. A program holding a child's
stdout and its stderr — which is every `communicate()` in every language that
has one — had to read one of them to the end before looking at the other, and
deadlocked when the child filled the one it was not reading. `Sys::Poll` is the
missing question: *which of these is ready*, answered without taking anything.

**The scheduler had to be fixed before the syscall could be written.** A
`Waiter` carries `timed` and `listed`, for the timer queue and the wake table,
and nothing had ever set both: `sched_wait_timer` had one caller, `Sleep`, and
`Sleep` registers no token. So `sched_tick` cleared `timed` and left `listed`
alone, `sched_wake` did the reverse, and a waiter in both tables would have been
pushed onto the ready queue twice — the second resume entering a frame that had
moved on, or gone. A poll with a timeout is exactly that waiter, and would have
been the first. Each path now removes the other registration, which also keeps
the host from being armed for a deadline nobody is waiting on any more. The unit
case registers a waiter both ways and fires each path in turn, because no
awaiter in the kernel does this yet and the rule would otherwise rot.

**Descriptors 0, 1 and 2 had no busy guard, and now have the one everything
else has.** `HandleBusy` has allowed one reader and one writer per handle since
pipes existed, because `Channel` displaces a second suspended receiver silently
and panics on a second blocked sender. Descriptors below `SYS_FD_MIN` are not
handles, so nothing carried the flag: two tasks of one process reading its
stdin lost one of them, and two writing a full stdout crashed the kernel from
user code. Nothing in the tree does either — a program's own runtime keeps one
task per stream — which is why it went unnoticed. `Poll` has to hold these
descriptors for the length of the call, so the flags exist now, and `Read` and
`Write` check them too rather than only `Poll`: a guard that one call honours
and two others walk past is not a guard.

**The call is one waiter, one token, armed on every channel named.** A poll
does not read: it asks each descriptor whether the next operation would park,
and `Stream` and `Source` grew a probe that answers that without performing
one — `IO_READY` and `IO_GONE`, null meaning a file or the screen, which never
parks. When nothing is ready the same token goes onto every channel through
`park_receiver` and `park_sender`, the timeout goes on the same waiter, and
the first of them to fire takes the rest of the registrations with it. A
channel that fires afterwards finds nothing listed, which is the late event
`sched_wake` has always answered false to. A wake that leaves nothing ready
re-arms with a fresh token and what is left of the timeout, so the call cannot
report ready for something that is not.

**The alternative was a process-side one, and it cannot be written.** A task
could in principle read each pipe in a task of its own and race them — except
that `CancelState::waiting` is one slot per task tree, and the read that loses
the race has already taken the bytes. Neither is repairable in a library; the
question is about the scheduler's registrations, so it belongs where they are.

**`Error::Busy` is a sixteenth error value, rather than `Perm` again.** A
second `Read` on one descriptor has always been `Perm`, and stays so: it is a
call that is not allowed. A `Poll` naming a descriptor another task holds is a
call to make again in a moment, and a program that cannot tell the two apart
retries the wrong one. Nothing else answers `Busy`.

**What is not pollable is refused, not faked.** A socket, a fetch body, an
inflate stream and a picked file wait on a host request rather than on a
channel; there is nothing to arm, and the only way to know whether one is ready
is to issue the read and keep the answer. That would make a poll consume, which
is the one thing it must not do, so they are `Err(Unsupported)` until something
needs otherwise. A direction a descriptor does not have — `SYS_POLL_OUT` on a
pipe's read end — is `Err(Invalid)` rather than a wait that never ends.

**`/bin/polltest` exists because §4.3 says an operation must have a caller in
the tree.** Nothing else in `/bin` wants two pipes at once: every filter here
reads one stream and writes another. So the caller is a demonstration, in the
manner of `hog` and `spin` — four scenarios, one line each at a prompt, and
the system case drives all four. `polltest two` is the shape a real program would
write: two children, two pipes, and a line printed as it arrives rather than
one pipe drained before the other is looked at.

**`PROC_ABI` moves from 20 to 21, so every binary is rebuilt and every
repository re-signed.** An added operation alone would not have moved it — the
number refuses a stale binary, and one that never issues an operation cannot
tell it is missing. Two things here do. `Error::Busy` is a value an older
process runtime has no name for, and a program built against this SDK that
polls on a 0.9 kernel would be told `Unsupported` in the middle of a wait
rather than refused at exec, where a version mismatch belongs.

**The port kit gets `<poll.h>` and `b_poll`, and one POSIX behaviour is not
reproduced.** `poll(2)` marks a bad descriptor `POLLNVAL` in its own entry and
carries on; this call holds every descriptor it names, so it refuses the whole
call instead — `EINVAL` for a bad one, `EBUSY` for one another task holds,
`ENOSYS` for one that cannot be polled. A port that reads `revents` to find
which descriptor was wrong has to be changed, and that is the sort of thing
Compat.md exists to say out loud rather than leave to be discovered.

Releases before this one are one file each in [releases/](releases/), newest
first:

- [0.9](releases/Release_Notes-v0.9.md) — the programs a pipeline needs, and
  the screen a program opens
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

