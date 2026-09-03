# Release notes

Reasoning, alternatives and trade-offs behind the code. Comments in the source
say *what* a thing is; this file says *why* it is that way.
[Concept.md](Concept.md) remains the specification — where this document and the
spec disagree about intent, the spec wins and one of the two needs amending.

The release after 0.9 is being written, and starts below. New sections are
appended under it, and the whole moves to [releases/](releases/) when the
release is cut.

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

