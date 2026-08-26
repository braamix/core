# Release notes — 0.7

Reasoning, alternatives and trade-offs behind the code released as 0.7.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.7 — The devices a port opens, and the menus the browser already had

`BRAAM_VERSION_BASE` moves to 0.7; the commit count and the hash behind it carry
on unedited. 0.5 was written from the point of view of a program being
*written* and 0.6 from that of a script already *running*. 0.7 is written from
the point of view of a program being *ported*: not what it does, but what it
assumes is underneath it before it does anything at all — somewhere to throw
output away, somewhere to draw a random byte from, a character at a time off a
stream, the size of a descriptor it is already holding, a create that fails if
the name is taken.

**`/dev` is a filesystem of its own, and it holds four devices.** `random` left
`/proc`, where a decimal number produced at `open` had been standing in for a
stream; `urandom` is ChaCha20 with fast key erasure in
[src/fs/chacha.cpp](../../src/fs/chacha.cpp), seeded once and never reseeded,
because the name promises a generator and a second spelling of `random` would
not have been one; and `null` and `zero` cost the VFS two new predicates.
`writable()` had been answering two questions — whether a name may be added here
and whether a file here may be opened for writing — and a device tree is where
those part, so `file_writable()` and `shares_handles()` split them and both are
defaulted, leaving `OpfsFs` and `ProcFs` unedited. §2.2 admits a third
sanctioned exception on the way: `Fs::read` is not a coroutine and entropy is
synchronous, so `host_random` is the kernel's seventh import.

**A program can read a character now.** [src/proc/file.h](../../src/proc/file.h)
is a buffered stream in userland — the thing §4.4 says to push into a syscall
and which cannot go there, since no operation makes a codepoint cheaper than a
round trip. It adds no syscall and does not move `PROC_ABI`, `get()` and `put()`
are awaiters rather than `Task`s so the fast path allocates nothing, and
`--gc-sections` keeps every byte of it out of a program that does not name it.
Ten programs stopped paying a syscall a row for +84,677 bytes, which is 64% of
the size budget and does not move it. Thirty-eight were converted in the first
attempt at +245,231 bytes, and most of that was reverted: a `File` earns its
~7 KB only where several writes coalesce into one syscall, and `/bin/sh` — the
tree's most-used program — turned out to have no syscall anywhere in it to save.

**Two operations went in without moving the number, and one of them refuses.**
`Sys::FStat` is op 33, because `/bin/unzip` had been sizing an archive by path
and then reading it by descriptor, and a zip's directory is at the end of the
file — that number is where the read starts, not a hint. `Sys::Mount` is op 34
and answers `Err(Unsupported)`: §5.4 names three preconditions and there is no
factory turning a special into an `Fs`, so the table now has one entry whose
only observable behaviour is a diagnostic, which is a debt against §5.4 recorded
as one. `SYS_O_EXCL` is the release's one new flag bit, and `SYS_O_ALL` went in
beside it so that the *next* one fails loudly on an old kernel instead of being
silently masked off — the hazard that makes a bit riskier to add than an
opcode.

**`PROC_ABI` moves from 18 to 19, and `Sys::Random` is the whole of it.** The
synchronous half of the table was closed at four and is closed at five: an
operation the kernel cannot answer itself, since a boot clock and a serial
number are not entropy, and `$RANDOM` is the caller §4.3's first rule wants. A
binary stamped for 18 meets `Err(Unsupported)` at `exec`, so every package built
against the 0.6 SDK has to be rebuilt and its repository re-signed. Both
operations added afterwards deliberately did not move it again, for the third
and fourth time running.

**The terminal joined the browser's own menus, and nothing crossed the wasm
boundary to do it.** `Edit → Select All`, `Copy`, `Cut` and `Paste` had been
operating correctly on an empty box; the hidden sink now holds a sentinel and a
mirror of the selection, which answers all three commands with one invariant.
The right button then got the same treatment from the other side — the sink is
stretched over the canvas for the length of a secondary press, so the browser
raises its *text* menu instead of the one it has for an image. No import, no
export, no syscall and no worker message that did not exist, and there is still
no browser harness in this tree, so the suites prove only that this stayed on
the page and the rest was checked by hand.

**The documents moved last, and one of them stopped being this file.**
Concept.md went from 1867 lines to 749 by giving up two things it should never
have been: a second copy of these notes, and a manual. Every section number
stayed where it was, because 466 source comments cite them. TODO.md lost `C1`,
`C2` and `N7` — two measurements nobody had taken and a decision already made —
and its last two sections with them. `test/smoke/` is `test/system/`, since
forty-seven end-to-end cases were never a triage pass. And these notes are one
file per release under [releases/](.), of which this is the first written into
its own file rather than cut out of the trunk afterwards.

**What moved is what a person may assume, not what the table counts.** 0.6's
answer to "what can I write here" was "a program, or the script that calls
four things that did not exist". 0.7's is that a program ported from somewhere
else finds its `/dev`, its `getchar`, its `fstat` and its exclusive create
already here — and that the terminal it prints into behaves like a text control
to every menu the browser has, rather than like a picture of one.

## The specification stopped arguing with itself

Concept.md was 1867 lines and had become two documents wearing one number
scheme. It is now 749, and states the top-level design: the principles, and the
approach that follows from them.

**The first thing it stopped being is a second copy of this file.** Nearly every
rule in it carried the argument that produced it — which caller turned up, what
was rejected, how many milestones something waited. That is what this document
is for, and a passage-by-passage check found all of it already here:
`Mount` refusing on purpose, the `O_EXCL` temp name, the right button, the Edit
menu's empty box, `null` and `zero`, the second device, §2.2's third exception,
the buffer §4.4 says not to write, the number the kernel cannot make, the slot
that waited four milestones, pid reuse, symbolic links, the modification time.
None of it was carried over here, because none of it needed to be.

**The second thing it stopped being is a manual.** It described what each of the
fifty operations carries, what each `/dev` node answers, how the Edit menu
reaches the hidden input, and what lives in each source file. Those have homes:
System_Calls.md derives the ABI in full, Shell.md is the shell's manual, the
Package_\* pair covers packaging, and the source comments that cite a section
say the rest. Concept.md now points at them rather than restating them.

**The section numbers did not move, and that was the constraint.** 466 source
comments cite this scheme, concentrated on §4.3, §7, §5.2, §4, §6, §5.1 and
§3.5. Every heading kept its number and its subject, so every citation still
lands. What changed is that some of them now land on the principle rather than
the rule underneath it — a comment citing §4.3 for `Fg`'s fourth authorisation
clause finds the wire's four shaping rules instead. That is the accepted cost
and not an oversight: the detail lives in the comment doing the citing, in
System_Calls.md, and here.

The architecture diagram, the seven imports and nine exports, the flag line, the
`Task`/`Waiter` sketch, `Cell` and `Screen`, the `Fs` interface, the mount list,
`StorageBackend` and the storage-tier table were kept verbatim. They carry more
of the design per line than any prose that could replace them.

One loose end is recorded rather than repaired. The entry above on `Mount`
quotes a §4.3 sentence — "left unbuilt for four milestones with `vfs_truncate`
wired beneath it precisely because no program wanted it" — that no longer exists
there. This file is append-only and describes the state at the time of writing,
so the quote stays as written.

## Three entries that were questions, not work

`C1`, `C2` and `N7` leave TODO.md, and with them its last two sections. None of
the three named something to build: two were measurements nobody had taken and
one was a decision already made, waiting on a condition that turned out to be
the wrong test. A file whose whole job is "what is left, in the order it earns
its place" was carrying three items that had not earned one.

**`N7`, a batched step protocol, was already decided.** Concept.md §4.4 says so
normatively — *"a batched step protocol stays decided against"* — and it was
decided twice over: T5 measured a round trip at 34–45 µs against an estimate of
"order 0.1 ms" and answered no, and then `SYS_READ_MAX` removed the workload
that had prompted the question at all. Its reopen condition was "a workload
survives `SYS_READ_MAX`", and that is the part worth writing down, because the
condition is mis-specified. What survives a 64 KiB read span is not per-*read*
cost but per-*object* cost: a file costs an open, a read, the empty read that
discovers the end, and a close, whatever the span is, and a directory costs a
`List`. Coalescing the replies the kernel owes one process does nothing about a
floor that is four calls per file. Anyone reaching for the wire should look
first at that floor, at the trailing EOF probe, and at a `List` that recurses.
The upward direction of the protocol is already batched — one step carries a
list of parked calls up and exactly one reply down — so half the theoretical win
was never there to take.

**`C1` and `C2` were measurements, and measurements are not a backlog.** C1
asked for `pkg verify` over a megabyte; C2 for a deep pipeline against the
64 KiB channel slots. Both are worth doing the day someone has a reason to care
what the number is, and neither blocks anything today. The tree keeps what they
would need: `/proc/stat` publishes `syscalls` and `steps`, `net.proc.stats()`
counts round trips on the host, and `test/system/chunk.mjs` already has the
difference-of-two-commands harness that would answer either of them.

One thing C1 was right about and it should not be lost with the entry. The
figure above at "A read is a span, not half a kilobyte" — `pkg verify` going
from some 2,210 reads to "about eighteen" — is derived, and the derivation is
wrong: eighteen is 1.1 MB ÷ 64 KiB, which treats the workload as one contiguous
stream. It is not; it is some thirty files, each paying the four-call floor
above. The true number was never measured and is not measured here either, so
no figure replaces it — only the warning that the old one is arithmetic rather
than observation. That is exactly why `C1` existed, and removing the entry does
not make its point wrong.

## An operation that cannot succeed, on purpose

§4.3's fourth rule is that what the kernel publishes as text needs no operation,
and its own worked example is `mount`: `/proc` is a filesystem, so the mount
table is `/proc/mounts` and `cat` is the tool. That is why `/bin/mount` has been
a reformatter since M5 and why `src/cmd/mount.cpp` carried a comment saying it
asks for no operation of its own.

**The rule is about reading, and it was doing double duty.** Listing the table
is a question a filesystem can answer, and it still answers it — `mount` with no
operands is unchanged, and `sysinfo.mjs` still checks it row for row against
`/proc/mounts`. Changing the table is a write, and a write has nowhere in a
filesystem to go. The rule's text never covered that case; it was simply never
asked, because nothing could mount anything. The shape it wants is the one the
rule already carves out for `chdir`: the state is readable as a file, and the
act is an operation because no file can be asked to perform one. §5.4 had
written down both candidate shapes years before — "a syscall or a `/proc` write
to reach it" — so choosing the syscall settles a question that was left open
rather than overturning one that was closed.

**What is new is that the operation refuses.** `Sys::Mount` is op 34, takes
`Rename`'s two-path payload, resolves both paths against the caller's cwd, and
answers `Err(Unsupported)`. It cannot do otherwise: §5.4 names three
preconditions and this commit satisfies exactly one. There is no factory turning
a special into an `Fs` — `vfs_mount` takes an `Fs *` and nothing builds one from
a path — and §5.1 still records that there is no per-process root, so a mount
would be global the instant it worked and nothing says whether it should be. The
refusal is the honest answer to both, and it is what a user sees:
`mount: /dev/zero: unsupported`.

This is `Truncate`'s history read backwards, and worth naming as such rather
than filed as the same thing. §4.3 tells that one as an operation "left unbuilt
for four milestones with `vfs_truncate` wired beneath it precisely because no
program wanted it" — wired beneath, unbuilt above, waiting for a caller. Here
the caller is the part that exists and the filesystem is the part that does not.
Both halves of §4.3's first rule are still met: the rule asks that an operation
have a caller in `src/cmd/`, not that it have a backend, and `/bin/mount` issues
the call on every run of the two-operand form. What the rule guards against is
growing the table on speculation, and an opcode with a program behind it and a
test asserting its answer is not that — though the honest way to hold it is that
the table now has one entry whose only observable behaviour is a diagnostic, and
that is a debt against §5.4 rather than a feature.

Opcode 34 was taken inside `PROC_ABI` 19, for the third time running: no
existing opcode, reply or flag changed, so nothing built against 19 can tell.
The exposure is `FStat`'s and not `O_EXCL`'s — a program calling `mount_at` on a
kernel predating op 34 gets `Err(Unsupported)` from the dispatcher's default
seed, which is the same answer this kernel gives deliberately. That is the one
case where the silent-masking hazard does not apply, because the fallback and
the intent agree.

One thing moved that is easy to miss: `rootfs/etc/help` gained the operand form,
and `subst.mjs`'s byte count is three copies of that file. 19,836 became 19,944.
The comment there already said a line added would move it; it now says a line
reworded does too.

## The pid in the temp name was never uniqueness

N4 shelved `O_EXCL` on two clauses, and they answer different questions. "`Open`
already refuses a second concurrent writer" is true and is about **who holds a
path now** — `share()` turning away an `O_WRITE|O_TRUNC` opener while anyone has
the file. `O_EXCL` asks whether **a name exists at all**, which no lock here
reports and which the open-file table has nothing to say about. The other
clause — that the only caller would be a `mktemp` using the pid anyway — named a
program nobody has asked for, and the caller was in the tree the whole time.

**B1 is that caller, and it had the same blind spot.** `edit`'s `save()` opened
the real file `O_WRITE|O_CREATE|O_TRUNC` and only then wrote, so a cancel or an
OOM in `write_all` left the file empty with the only copy in a worker that was
going away. B1 prescribed the fix — write `<path>.tmp.<pid>`, rename it — and
added "no randomness needed", which is where it went wrong. **A pid is reused.**
§4.1's rule is that a pid is never reused *while something still names it*, and
that is an instant, not a lifetime. Nothing names a file. So `.tmp.<pid>` is
unique now and not over time: a save killed between the create and the rename
leaves an orphan, and the next process to draw that number and edit that file
would have truncated it without a word — reintroducing the loss B1 exists to
prevent, at longer odds and with no diagnostic. `O_EXCL` is what makes the name
provably the writer's own, and a bounded counter after it is what stops an
orphan making a file permanently unsaveable.

**The flag is as exclusive as `mkdir` is, and says so.** OPFS has no exclusive
create: `getFileHandle` takes `{create}` and with it set an existing name yields
the existing handle. So the implementation is a probe and then a create with an
`await` between — which is exactly what `web/fs.js`'s `mkdir` has shipped since
M5, and the new code is deliberately the same shape beside it rather than a
weaker one invented fresh. The VFS closes the two windows it can see: a name
that resolved is refused before any backend is asked, which is also the only
check `DevFs` and `ProcFs` need so neither changed, and the loser of the open
race is refused in `share()` instead of being folded into a share, which is what
it would silently have become. The window it cannot close is the host's, and two
tabs are one origin and therefore one store with a kernel each, so across them
the flag promises nothing whatever. That is POSIX's guarantee declined, not
approximated, and §5.2 now says so in those words rather than leaving a reader
to assume the usual meaning.

Worth admitting: the race is the interesting case and **no test covers it**. The
unit suite's `run_now` panics on a suspension, so the await window is out of
reach there — `test_vfs` says as much already — and `test/fakefs.mjs` is a
synchronous `Map`, which makes the fake *atomic where OPFS is not*. Passing
proves the flag arrives and that the resolve-time and share-time refusals fire;
it is not evidence that a browser is exclusive. The comment in `fakefs.mjs` says
this so the next reader does not take a green suite for more than it is.

**`vfs_flags` now refuses what it does not know.** This is the part that made a
flag bit riskier to add than an operation, and it is worth writing down because
it cuts against the ABI-19 practice `Truncate` and `FStat` established. A new
*operation* on an old kernel is `Err(Unsupported)` at the call — loud. A new
*flag bit* on an old kernel was silently masked off by a whitelist that dropped
what it did not recognise, so a caller asking for an exclusive create would have
got a plain one and been told it succeeded. Nothing can repair kernels already
built, and the exposure is real only for an out-of-tree program, since
`/bin/edit` ships inside `rootfs.zip` beside the kernel that serves it. What
`SYS_O_ALL` does is close the class going forward: the next flag added on 19
fails loudly on this kernel instead of quietly. `O_EXCL` without `O_CREATE` is
`Err(Invalid)` for the same reason — POSIX leaves it undefined, and undefined is
what a caller reads as permission.

## N3 was closed on the wrong question

`fstat` sat under "Not scheduled" for four milestones, and the note that put it
there is still correct as far as it goes: the only field anyone wants from a
descriptor is its size, `seek_fd(fd, 0, SYS_SEEK_END)` gives it, and `/bin/tail`
does exactly that. When `/bin/truncate` shipped without needing one either, that
looked like confirmation and was written down as such.

The question it never asked is *which file the size was measured on*.
`/bin/unzip` sized the archive with `stat_of` and then opened the same path, and
handed the path's number to an `FdZipSource` reading the descriptor. A zip's
central directory is at the end of the file, so that number is not a hint about
how much there is — it is where the read starts. An archive that shrank between
the stat and the open sent `zip_entries` looking past the end of the file, and
the two calls that bracketed the window cost exactly what one call and no window
costs. That is the caller §4.3's first rule wants, and it had been sitting in
the tree the whole time being counted as a program that did not need one.

**`Sys::FStat` is op 33 and answers what a descriptor knows.** `kind` is always
a file, because nothing else opens. `mtime` is always 0, because OPFS has no
modification time for an open handle: `lastModified` lives on a `File` that only
an `await` produces, and §5.2 does not put a promise on an open file to fetch
one field no caller wants. It is `vfs_size` underneath — the primitive `Fs` has
had since M4 and only `handle_seek` ever called — so no `Fs` virtual, no host
operation, and `web/fs.js` and `test/fakefs.mjs` are untouched. Worth recording
that the fake is *more* capable than the real store here: `FakeStore` keys its
handles on paths and could answer mtime by handle, which is exactly why the
design was settled against `web/fs.js` and not against the thing that would have
let it pass.

**It took an op without moving `PROC_ABI`, which stays at 19.** `Sys::Truncate`
established that in 0.6 and this is the second time: an operation purely added
changes no opcode, no reply and no flag, so no binary built against 19 can tell
the difference, and the number's job is to refuse a *stale* binary rather than
to count the table. Nothing is invalidated — no package in the store, no
installed SDK. The one exposure runs the other way and is worth naming: a
program built against the new `sysabi.h` that called `stat_fd` on a kernel
predating `FStat` would load cleanly and fail at runtime with
`Err(Unsupported)` instead of being refused at `exec` with the message that
names the number. No such program exists — `/bin/unzip` ships inside
`rootfs.zip` alongside the kernel that serves it — and the first out-of-tree
caller is what would change that.

**`vfs_open` refuses a directory now.** Converting unzip to open-then-fstat
would otherwise have cost it a diagnostic: the kind check it did by path was the
whole of its `is a directory`, and `vfs_open` delegated that refusal, so only
DevFs and ProcFs answered `Err(IsDir)` and OPFS failed somewhere inside
`getFileHandle` with something vaguer. The `Stat` was already in hand — the
`vfs_resolve` at the top of the function had it — so the refusal is three lines
where every backend can stop guessing, and `cat`, `wc`, `less` and `grep` say
the same thing about a directory as unzip does. `test_vfs` proves it above the
filesystem rather than in it, by opening the `CountingFs` mount root and
checking that `fs_opens` did not move: that backend hands out a handle for
anything, so a refusal it never saw is the only way the count can hold.

## The right button had nothing under it

The note below made `Select All`, `Copy`, `Cut` and `Paste` work from the
browser's own Edit menu, and left the menu most people actually reach for
untouched: the one the right button raises. Over the canvas it was the menu a
browser has for an image — `Save image as…`, `Copy image`, `Open image in new
tab` — three commands about a picture of a terminal, and not one about the
terminal.

**The menu was never the missing part; the target under the pointer was.** A
`contextmenu` hit-tests where the pointer is, and every command that menu then
offers is chosen from what it hit. It hit a `<canvas>`. Everything wanted was
one element away — the hidden input, holding the sentinel and the mirror the
note below installs, which four editing commands already act on correctly. So
the sink is stretched over the canvas for the length of a secondary press,
released on the next turn, and the browser raises its *text* menu instead. Cut,
Copy, Paste and Select All arrive as the same four events `web/braam.js` has
been handling since that note, and there is nothing new to route.

**A popup of our own was the obvious alternative and is the worse one.** Three
items, drawn and placed and dismissed by hand: a second implementation of a
menu, a styling contract every embedding page would have to satisfy or look
broken, keyboard navigation to write, and — the part that decides it —
`navigator.clipboard.readText()` for `Paste`, which is gated behind a permission
prompt in every engine, where the `paste` event the native menu fires needs no
permission at all and is the route `pbpaste` already depends on (§6). The
browser's menu is worse-looking and carries items we did not choose; it is also
correct in every locale, on every platform, without a line of CSS.

**The caret is the hazard, and both guards are kept.** A secondary press inside
a text control moves the caret when it lands outside the current selection, and
an armed sink covers a whole terminal while its mirror renders in the first few
pixels of one line — so nearly every press lands outside it, collapsing the
range and greying out the very `Copy` the menu was raised for. `mousedown` is
prevented for that press, which stops the move where an engine honours it; and
the range is set again in the `contextmenu` handler, which runs before the menu
is built, for one that does not. Neither is trusted alone: the first is a
default some engines derive the platform menu from rather than the DOM event,
the second assumes menu contents are read after dispatch. Together they have no
engine left to fall through.

**And the caret is not all a press moves.** Where it does not land on a
selection, an engine selects the *word* under it — and the sink is one line
holding a sentinel and a mirror, so the word under a press anywhere over the
terminal is the whole value: a range of exactly `(0, length)`. Which is the one
range that means `Select All` from the menu. So a right-click selected the
entire screen before the menu had even been drawn, on a terminal with nothing
selected at all. The invariant is sound and the reading of it was too narrow:
that range is the command *when it arrives from outside a press*, and the press
is already a state this code holds — the sink is armed for exactly its
duration. So `onSelectAll` declines while armed, and the restore puts the
resting range back along with the geometry, whatever the engine left behind.

**Restoring is a `setTimeout(0)`, and a timer behind it.** Not synchronous,
because the menu is anchored and its commands route to the *focused* element —
which the sink still is, whatever size it has — so one turn later is soon enough
and costs nothing. Not never, because the window in which an invisible input
covers the terminal is a window in which no drag can select: a press that raises
no menu at all, on an engine that suppressed it, would leave the canvas dead.
The grace is 1.5s, and any ordinary press on an armed sink restores it first.

It costs one behaviour elsewhere. **`Ctrl`+click on a Mac is the secondary
click**, so it stops starting a selection drag — which it should never have
done: it wiped the selection at the moment the menu about to appear needed it.

**The gaps are the ones the pointer leaves.** A touch long press has no button
to arm from, so Android keeps the canvas's own menu; iOS raises no `contextmenu`
under `-webkit-touch-callout: none`, and taking that off brings the text
magnifier back over the grid. The key bar and `⌘V` remain what a phone has. And
`mount({menu: false})` gives an embedder the canvas menu back, for a page that
wants `Save image as…` over a terminal.

Nothing crosses the wasm boundary: no import, no export, no syscall, no worker
message that did not exist. There is still no browser harness in this tree, so
the three CTest cases prove only that this stayed on the page, and the rest was
checked by hand at the prompt.

## The Edit menu was talking to an empty box

`Edit → Select All` in the browser's own menu selected nothing, and `Edit →
Copy` beside it was greyed out. Everything those commands want had been built —
a selection over the grid, `Cmd+A` to select all of it, `Cmd+C` to copy it — and
none of it was reachable, because **every route into this terminal started at a
keystroke and a menu command is not one**. There is no `selectall` event to
listen for, and a `copy` event is dispatched only when the browser thinks
something is selected.

What those commands *do* act on is whatever holds the focus, and since M8 that
is a hidden `<textarea>`, the sink. It was deliberately empty — its whole job
was to hold what an input method had just produced and nothing else. An empty
text control is exactly the thing `Select All` cannot select and `Copy` cannot
copy, so the menu was operating correctly on a box with nothing in it.

**So the sink stops being empty.** It holds a sentinel — one no-break space —
and behind it a mirror of what the grid has selected, with the selection range
covering the mirror alone:

```
value = "\u00a0" + selection     range = (1, value.length)
```

That one invariant answers all three commands at once, which is why it was
preferred to three mechanisms:

- **The resting range never reaches column 0.** So the browser's `Select All`
  always changes it, and the `select` it fires is the command arriving —
  including from the state where nothing is selected, which is the reported
  bug. A range of exactly `(0, length)` is the menu and nothing else; the
  handler collapses it back, which makes a duplicate from an engine that fires
  `select` and `selectionchange` both fail the same guard.
- **`Copy` and `Cut` become enabled**, dispatch their event, and have the right
  text under them. The page still writes the clipboard itself
  (`clipboardData.setData`) and clears the selection after, so the menu and the
  chord leave the terminal in the same state — the invariant that keeps the
  *next* `^C` an interrupt rather than a second copy.
- **Typing is unaffected**, because the mirror is *selected*: an insertion
  replaces it, and what is left in front of the typed text is the sentinel,
  which `drain()` strips. The mirror cannot be typed into the shell by accident
  the way a merely-prefilled field could.

`Paste` needed nothing. The `paste` event is dispatched from the menu exactly as
it is from `Cmd+V`, and `web/braam.js` has handled it since M6 — it was included
in the request and verified rather than changed.

### The event this file ruled out is now the right one

"Ctrl+C, twice overloaded" below rejected the `copy` event outright: it "is not
reliably dispatched to a focused canvas with no document selection behind it,
and when it is not, the chord would be swallowed with nothing copied and no
interrupt sent". Both halves of that premise are gone — the focus is a text
control, not a canvas, and there is now a document selection behind it. The
chord's own path is untouched all the same: it prevents its default, so no
`copy` event follows a `Cmd+C`, and the two routes cannot both write. `Cut`
shares the copy handler, because a terminal has nothing to cut and the native
one would take the mirror out of the sink.

### The sentinel is the contingency M8 already costed

"Three things left uncertain on purpose" named this exact trick as the fix for a
soft keyboard that suppresses `beforeinput` on an empty field — "seed the sink
with two no-break spaces, keep the caret between them, and diff on drain — which
costs autocorrect context and makes a screen reader read the sentinel". It
arrives here for a different reason and pays those listed costs, and it is
installed **on every platform** rather than only where there is a menu bar: one
input path is worth more than sparing a character from a screen reader, and the
capability query that would have split them (`any-pointer: coarse`) is wrong for
a tablet with a keyboard attached. One sentinel, not two, since the range rather
than a caret position is what is being read; and a no-break space rather than a
zero-width one, because it is a word boundary an input method composing a word
cannot absorb.

It pays a debt on the way past: a backspace on an empty line now deletes a real
character, so a UA that fires no `beforeinput` for an edit that changes nothing
has something to change.

Two rules keep the sink and the grid in step. `dropSelection()` runs at the top
of `sendKey()` and `typeCodes()` — every route by which input leaves the page —
so the mirror goes when the worker's own `deselect()` goes, rather than a
message turn later; and `resetSink()` refuses to touch the field while an input
method is composing, since a write to `value` mid-word is how an IME is broken.

**Nothing crosses the wasm boundary.** No import, no export, no syscall, no
`Screen` field, no worker message that did not exist: `Select All` from the menu
posts the same `{kind:"selectall"}` the chord does and means the same thing —
the visible screen. The unedited exact-surface assertion in
[test/system/abi.mjs](../../test/system/abi.mjs) is the evidence, as it was for
M8: there is still no browser harness in this tree, so the three CTest cases
prove only that this stayed on the page, and the behaviour was checked by hand
in a browser — both menu and chord, both terminals in `embed.html`, and typing
after each.

## `null` and `zero`, and the two rules `writable()` was keeping at once

`/dev` holds four entries now. `zero` cost what §5.1 said it would — a row in
the table and a `case` in `read` — and `null` cost what §5.1 said it would too,
which was more. Both notes below stand as written; this supersedes them rather
than editing them.

**`writable()` was answering two questions, and `/dev` answers them
differently.** One is whether a name may be added, removed or renamed here: it
gates `deny_readonly`, so `mkdir`, `rm`, `mv`, `touch` and `ln -s` fail before a
path is even walked, and it is what `mount` prints as `rw` or `ro`. The other is
whether a file here may be opened for writing. For every filesystem that existed
those were the same question, so one predicate answered both and nobody noticed.
A device tree is where they part: `/dev`'s table takes no new name and never
will, and its entries take bytes. Flipping the one predicate would have bought
`> /dev/null` at the price of `mount` calling `/dev` writable, `touch /dev/null`
answering `Unsupported` instead of `Perm`, and — worst — `mv /dev/null /home/x`
reaching the cross-mount `Err(Unsupported)` that tells `mv` to copy and remove
instead, so it would have created an empty `/home/x` and *then* failed the
remove. `file_writable()` defaults to `writable()`, is overridden in one place,
and leaves all of that where it was.

**The open-file table's refusal is OPFS's exclusive lock, not a law of the
VFS.** §5.2 shares one backend handle per resolved path and refuses a writer any
other descriptor holds, because a sync access handle would refuse the second
open itself. `DevFs` opens nothing and locks nothing, so the refusal it
inherited was pure cost: `cmd >/dev/null 2>/dev/null` opens the path twice and
holds both, and either stage of `a >/dev/null | b >/dev/null` would have been
told it lacked permission to write to a sink. `shares_handles()` says a backend
has no file to open twice, and such a backend is opened once per descriptor.

The cheaper shape — keep the sharing, relax only `share()`'s refusal — was
rejected. A sharing open returns before `Fs::open` is ever called, so every
per-path decision the backend makes is skipped whenever another descriptor on
that path already exists. `/dev` would have answered `IsDir` and `NotFound`
correctly on a first open and not on a second. Opting out of the table entirely
keeps `DevFs::open` on every path, which is the only way those answers stay
exact.

**A 0-byte read was already the end of input, so `null` needed no new
convention.** `read_chunk` turns an empty reply into `Err(Error::Closed)` and
nothing in the tree loops on a short read, so `cat /dev/null` exits 0 and
`wc /dev/null` says `0 0 0` without a line of special-casing. `size()` still
answers `Err(Unsupported)` for all four rather than 0 — a size of 0 would make
the read path clamp every read of `zero` to nothing — and `truncate()` now
answers `Err(Invalid)` rather than `Perm`, since a mount that takes writes
refusing on grounds of permission would be a lie; `EINVAL` on a character device
is what Linux says and what it means here too.

**`random` and `urandom` discard a write now instead of refusing it.** Linux
lets you write to both, where it stirs the entropy pool. There is no pool here —
`random` is a host draw per read and `urandom` is fast key erasure — so a write
stirs nothing, and the honest choices were to refuse it or to drop it. The
refusal was never a decision anyone took: it fell out of the mount being
read-only, and the moment `file_writable()` made that a per-device question it
had to be answered on purpose. Dropping it makes all four devices one rule, and
it is Linux's behaviour reached from the other direction.

**`cp /dev/zero x` fills the store until it refuses, exactly as on Linux.**
Nothing guards it, and nothing should: both halves are syscalls, so `^C` is the
answer, which is the same answer `cat /dev/random` has always had.

No import, export or `PROC_ABI` moved; the kernel grew 344 bytes. The two new
`Fs` predicates are defaulted, so `OpfsFs` and `ProcFs` needed no edit —
`ProcFs` keeps sharing on purpose, since its content is a snapshot taken at
`open` and two readers of one snapshot is §5.1's rule rather than an accident.

## The suite was never a smoke test

`test/smoke/` is now `test/system/`, and the CTest case with it: the three names
are `system`, `unit` and `size`. Nothing about what runs changed — the same
ordered `CASES` table, the same cumulative session, the same `--kernel` mode of
`test/run.mjs`.

A smoke test is a handful of assertions that the thing starts at all, run before
anyone bothers with the real suite. That is what `smoke` meant here when it was
`boot.mjs` and `abi.mjs`. It is not what forty-seven cases are — booting the
shipping kernel, typing four thousand keystrokes at a shell that has been alive
the whole time, installing and removing packages, killing workers to watch init
replace them. The name promised a triage pass and delivered the end-to-end
suite, so `unit` and `smoke` looked like a scope and a smell test rather than
two halves of one line.

**The obvious rename is the wrong one.** `test/node/` names the harness, and the
harness does not divide the two suites: `run.mjs --tests` drives `tests.wasm`
under Node as well. Naming it after the runner would say the in-wasm suite is
*not* run by Node, which is false, and would leave the pair inconsistent in the
other direction — a runner beside a scope. The axis that does divide them is
Testing.md §2's dividing line, the one the build already enforces: everything
below a program, against everything that needs a program to run. `unit` and
`system` are the two ends of that.

Nothing here is renamed retroactively. Notes below this one say `smoke` because
that is what the suite was called when they were written, and this file is
appended to rather than rewritten.

## The second device is the one the kernel makes itself

The note two below this one said `urandom` was coming and would be "a row". It
was a row and about fifteen hundred bytes of cipher behind it, and correcting
that is the point of this note. A row would have been `urandom` as a second
spelling of `random` — the same `host_random` per read under a name Linux uses
for something else. That is not what the name means anywhere a person has met
it before: on Linux `/dev/random` is the entropy source and `/dev/urandom` is a
generator seeded from it, and a system that ships both names owes the second
one a generator. So `/dev/urandom` has one, in
[src/fs/chacha.h](../../src/fs/chacha.h), and `/dev/random` did not change by a
line.

**ChaCha20 with fast key erasure, because it is what Linux's own CRNG is.** The
name on the device and the thing behind it agree, which is the whole argument
for not reaching for something a tenth the size. A xorshift would have been a
hundred bytes and would have made `urandom` a weaker promise than the one its
name carries everywhere else — and the property that matters here is one no
small non-cryptographic generator offers: after a block is handed out, nothing
the kernel is still holding can reproduce it. Each 64-byte block's first half
replaces the key and only its second half leaves. The old key is gone, and
there is no pool, no entropy accounting and no state to steal but the next 32
bytes.

**Seeded once, lazily, and never reseeded, because there is nothing to reseed
*from*.** Linux reseeds its CRNG from an entropy pool that keeps improving;
this kernel has no pool, only `host_random`, and a caller who wants the host's
bytes has `random` one path component away rather than a reseed policy nobody
can observe. Lazily rather than at mount, so a boot that never touches
`/dev/urandom` never draws: the seed is 32 bytes taken on the first read and
that is the device's entire cost to a system that does not use it.

**The tail of a read's last block is discarded rather than buffered**, which
reads like a shortcut and is the opposite. Keeping the remainder would leave
un-emitted keystream resident in kernel memory between reads, which is the
precise thing key erasure exists to prevent; discarding is the construction. It
costs at most 31 bytes a read, and it buys something testable: output bytes
`[32i, 32i+32)` always come from block *i*, so the stream does not depend on
where the reads were cut, and the unit suite can assert that.

**§2.2 did not have to move, and its third exception got narrower instead.**
That exception's justification said the rejected alternative was "seeding a
generator in the kernel from one asynchronous draw", which now reads as a
prohibition on the thing this release built. It was never that. Both halves of
the objection were about the generator as a *replacement* for the import:
without `host_random` every byte in `/dev` would have been the kernel's
invention, and the seed would still have had to be awaited from `Fs::read`,
which cannot await. Neither survives contact with what went in. `/dev/random`
still hands out the host's own bytes per read, and `urandom`'s seed is one
*synchronous* `host_random` from inside `Fs::read` — the import is what made
the generator possible, not what forbade it. No import, no export and no
`PROC_ABI` moved; `test/smoke/abi.mjs` is untouched, and the kernel grew 1,539
bytes, leaving 75 KiB of its budget free.

**The cipher is a file of its own so that it can be wrong out loud.** The note
below says "a generator in the kernel is a thing to get wrong in a way
`crypto.getRandomValues` is not", and that is still true — the answer to it is
a known-answer test, which needs the primitive reachable from `test/unit/`.
`src/fs/chacha.cpp` is in `braam_fs`, which the suite already links, so RFC
8439 §2.3.2 and §A.1's vectors are checked on every run along with the erasure
construction over them. Buried in `devfs.cpp`'s anonymous namespace there would
have been nothing to check but an opaque stream, which is another way of saying
nothing.

**The generator belongs to the mount, not to a descriptor.** §5.2 keys one
backend handle per physical path, so two `cat /dev/urandom` readers share one,
and a generator that advanced per read serves them both without either seeing
the other's bytes — the same guarantee `random` gets from drawing per read,
arrived at differently. A namespace-scope global would have been the obvious
place and is the wrong one twice over: it breaks §3.2's trivially-destructible
rule, and it would carry a seeded key across a `vfs_reset()` into the next
mount. It is a member of the `DevFs` the mount owns, and the unit suite asserts
that a fresh mount draws a fresh stream.

**`null` and `zero` are still not here.** `null`'s §5.2 exemption — the
open-file table refuses a writer any other descriptor holds, so two stages
redirecting there would collide — is unchanged and still to be argued on its
own, and it would have been just as wrong to smuggle it in behind `urandom` as
behind `random`.

## The decoder let through what the encoder had refused for years

`cat /dev/random` killed the renderer: `RangeError: Invalid code point 1160716`,
out of `String.fromCodePoint` in `web/render.js`. The device was not at fault.
1160716 is 0x11B58C, which is what the bytes `f4 9b 96 8c` decode to, and
`utf8_decode` had been handing that straight to a cell since M2. `cat` of any
binary file did it; a device that is nothing but bytes made it a one-liner.

**The two halves of one file disagreed, and only one of them had a test.**
`utf8_encode` has refused surrogates and values past U+10FFFF from the
beginning, and `test_text.cpp` asserts it. `utf8_decode` checked nothing: not
the continuation bytes — a lead byte swallowed whatever followed it, so `c3 41`
ate the `A` — not the leads that cannot begin a sequence (`f5`–`f7` all match
the four-byte test and every one of them decodes above U+10FFFF), not
overlongs, not surrogates, and not the range. The header even *described* the
behaviour that was missing: "a stray continuation byte consumes one byte and
yields U+FFFD, so bad input is visible rather than silently dropped". One of
the cases was implemented and the sentence was read as though all of them were.

**The fix is four layers, because one of them was never going to be enough.**
Fixing the decoder alone leaves `Sys::ScreenBlit`, which is a `memcpy` of
whatever a process staged: **any program could put any `u32` in a cell** and
kill the renderer, decoder or no decoder. So the rule is now stated as an
invariant — no cell holds a codepoint the host cannot draw — with one
definition of it, `rune_safe` in `src/kernel/text.h`, and every writer going
through it. `utf8_decode` yields U+FFFD for every malformed sequence;
`screen_put` clamps what it is handed; `screen_touch` clamps the rectangle it is
told about; and `web/render.js` guards both its `fromCodePoint` calls anyway.

**`screen_touch` is where the blit is caught, and that is not a coincidence.**
`screen.h` already required every writer filling cells through `screen_cells()`
to call it — it is how such a writer says what it changed. Putting the pass
there makes the invariant structural rather than remembered: cells cannot be
declared changed without being made drawable, and a direct writer added later is
covered without knowing this note exists. Its callers are the blit and
`FullScreen`'s restore, and nothing else: `Pane` writes a *program's* own `Grid`
and reaches the screen through a blit like everything else. The cost is the
clipped rectangle — 1,920 cells for a full 80×24 repaint, against the step round
trip that carried them, and against the alternative of validating cell by cell
on the way in, which would have replaced the row `memcpy` with a loop and still
left the renderer one bad write from the same death.

**A malformed sequence consumes one byte or all of itself, and the difference
matters.** A bad continuation byte consumes *one*, so the next lead byte
resynchronises rather than the parser eating the rest of a line. A sequence
whose shape was right but whose value was not — a surrogate, an overlong,
0x11B58C — consumes all of itself and emits one U+FFFD, because four
replacement characters where one codepoint was meant is noise. And `return 0`
still means "need more input" and nothing else: `FileBuf::take` reads it as
`RuneStep::Need`, so the invalid-lead test has to come before the length test or
a bad byte at the end of a buffer would look like a short read for ever.

**C0 controls are left alone.** `0x07` or `0x1b` in a cell is a valid codepoint
that draws as a box or as nothing, and there are no escape sequences here to
misread (§2.3). They cannot throw, and this was about what throws.

**The regression test is the crash, not a unit of it.** `test/smoke/term.mjs` is
the only case that drives the real `Renderer`, over the live grid with a mocked
2D context, so a file of malformed bytes is `cat`ted and that row is painted and
copied — `fillText` is stubbed but `String.fromCodePoint` still runs, and the
case threw `RangeError: Invalid code point 1160588` before the fix. The unit
suite has the decoder's table and both grid writers, and the smoke suite prints
64 random bytes at a prompt, which is the report as filed.

## The draw becomes a device, and §2.2 admits a third

`/proc/random` was a decimal `u32` per `open`, one release old, and neither the
name nor the content Linux has. On Linux `/dev/random` is a character device
that hands out raw bytes for as long as anything reads. So it moved, and it
became what it is called: `/dev` is a mount of its own, `DevFs`, and its one
entry answers every read in full, for ever.

**A `/proc` file is a snapshot and a device is a stream, and that difference is
the whole of the move.** Everything else in `/proc` is state the kernel is
holding, produced at `open` so that no two lines of one read describe different
moments. Entropy is not state and there is nothing to be consistent about; a
snapshot of it is just a number that goes stale in a way nobody can observe.
Nothing was lost in the move except that `cat /proc/random` printed something
short and readable, which was never the point of it.

**`Fs::read` is not a coroutine, and that is what forced a new import.** §5.2
makes opening asynchronous and everything on an open descriptor synchronous, so
that an OPFS sync access handle can serve a redirection with a plain call. A
device that draws per read therefore cannot await, and `SvcOp::Random` — a
promise on the host side — is out of reach from exactly the place the bytes are
needed. Three ways out were on the table. Draw a block at `open` and serve reads
from it: simple, and not a device, because it ends. Seed a ChaCha20 in the
kernel from one asynchronous draw and expand it: endless, and the answer to "is
every byte the host's?" becomes no — the kernel would be inventing them, and a
generator in the kernel is a thing to get wrong in a way
`crypto.getRandomValues` is not. Or make entropy synchronous, which it is.

**`host_random(ptr, len)` is §2.2's third sanctioned exception, and the previous
note in this file said there would be no seventh import.** It said so about the
callers that existed: the kernel's draw was an operation on `host_svc` and a
process's is made in its own worker, and neither crossed the line. What changed
is a caller that cannot await. The test §2.2 states has not moved — no promise
is involved at any point, and `crypto.getRandomValues` fills the array it is
given and returns — and the bar for a fourth is written up a notch in the same
edit. A few pragmatic exceptions are fine; a class of them is a second calling
convention, and then there are two ABIs and no invariant.

**`SvcOp::Random` is gone, one release after it arrived.** It was the last enum
value, so nothing renumbered, and it had no caller left: `/proc/random` was the
only one, and this tree does not keep a host operation nobody makes. The
kernel's entropy is now one import instead of one operation on another, which is
also a round trip less per draw — a service call is a record, a token and a
resumption, and this is a call.

**`size()` refuses rather than answering 0, and that is what makes the stream
endless.** The read syscall clamps a request to what a file has left *only when
the filesystem gives a size* (`src/user/syscall.cpp`), so a device that reported
0 would answer every read with EOF and `cat /dev/random` would print nothing at
all. `Err(Unsupported)` means there is nothing to clamp against, and no line of
the read path changed. `SEEK_END` is the price and the only one: it is the sole
operation that needs a size, and on a device it fails.

**The offset is ignored, which settles a sharing problem instead of creating
one.** §5.2 keys one backend handle per physical path, so two `cat /dev/random`
readers share a handle while both are open. Had reads been a function of the
offset, the two would have been handed identical bytes; drawing per read, they
cannot be. `stat` still says 0, as Linux does for a character device — and the
old argument that measuring a file must not spend entropy now costs nothing to
honour, since nothing but a read touches the device.

**`head` grew `-c`, because otherwise nothing could ask for eight bytes.**
`head` was lines only, there is no `od`, no `xxd` and no `dd`, and `wc` reads to
the end of an input that has no end. `cat /dev/random` was therefore stoppable
only by `^C`, and the smoke suite — which drives a real shell — could not read
the device at all without hanging. `head -c 8 /dev/random` both terminates and
is the idiom somebody arriving from Linux would type. Whether two draws differ
is the unit suite's to say: it is the half that can compare bytes without
putting them on a screen.

**`/dev` holds one entry, and `urandom`, `null` and `zero` are coming.** The
entries are a table rather than a chain of name comparisons, so the first two
are a row each. `null` is not: it is a writer, `DevFs` would have to become
writable, and the open-file table refuses a writer any other descriptor holds —
so two pipeline stages redirecting there would collide. That exemption is a §5.2
change to argue on its own, and smuggling it in behind `random` would have been
the wrong way to make it.

**Nothing about a process moved.** `$RANDOM` is still one `Sys::Random` answered
in the shell's own worker, `PROC_ABI` is untouched, and `exec_sys` still has no
case for `Sys::Random` — but the reason in the comment is now that answering
would give one operation two servers, rather than that the kernel has no
entropy, which stopped being true. In the suites the new import is the fixed
xorshift stream the fake service used to serve, moved to where the harness
builds its imports: a run still repeats, and two shells still disagree.

## Ten programs stopped paying a syscall a row

`/bin/cat` was `proc/file.h`'s only caller. What every other program wrote
through `write_all` was *rows*, one syscall each: `ls` one per listing line
twice over, `ps` one per process, `df` and `mount` one per filesystem, `vmstat`
one per counter and one per sampled row, `grep`, `head` and `tail` one per line,
`uname -a` one per line of `/proc/host`. `/bin/echo` was the worst in
proportion, at **2n+1 syscalls for n words** — the shell's `echo` builtin had
fixed exactly that years earlier by accumulating into a `String`, and the
program never did.

Those ten are converted, for **+84,677 bytes**; the `rootfs/` tree goes
1,250,874 → 1,335,551, which is 64% of the 2 MB in `tools/size_budget.txt`. The
bound does not move.

**The first attempt converted thirty-eight programs and most of it was wrong.**
It cost +245,231 bytes, and the criterion it failed to apply is one sentence
long: *a `File` earns its ~7 KB only where several writes coalesce into one
syscall.* A program that writes once already costs one syscall, and buffering it
costs one syscall — plus a `tty_of` probe, which made several of them
fractionally **worse**. `wc` was the clearest case at +12,471 bytes for a
program whose I/O was already one read per 64 KiB and one write at the end.
`basename`, `dirname`, `pwd`, `date`, `pbpaste`, `hog` and `spin` were the same
mistake more cheaply. Those reverted.

So did four where the conversion was not merely idle but actively worse:

- **`wc`, `pbcopy`, `fexport`** read a whole stream. `Input::read()` hands over
  a 64 KiB `String` per syscall; a `File` copies it out through the caller's
  span instead. Same round trips, an extra copy, +9–12 KB each.
- **`chat`** has two tasks writing one stdout, so they would share one
  `FileBuf`. Each message already costs one syscall and would still, so the
  reward for taking on a concurrency hazard was nothing at all.
- **`watch`** flushes per round by necessity — its loop never returns — so
  nothing coalesced.
- **`env`, `timeout`, `/bin/pkg`** spawn a child that inherits stdout, which
  means a flush before every spawn. `pkg`'s only loop-write is its usage table.

**`/bin/sh` was converted and then reverted, and the measurement is the reason
to record it.** `test/smoke/term.mjs` asserts with `!==` that a keystroke is two
round trips and Enter to the next prompt is five — Concept.md §4.4's cost model.
It passed unchanged: `Buffering::Auto`'s probe fires on the first flush, which
happens while the shell is starting, long before the keystroke term.mjs
measures. **§4.4's numbers were never at risk.** What killed the conversion was
the other half: sh's builtins already accumulate into a `String` and write once
(`builtin.h`'s rule), `job.cpp` writes to descriptors that are often a pipe or a
redirect and must not be buffered at all, and `shell.cpp`'s per-prompt newline
has to be flushed immediately because the prompt follows it. There was no
syscall anywhere in the shell to save, so the tree's most-used program keeps
`write_all` and its `read` builtin keeps the hand-rolled wind-back that
`redirect.mjs` asserts.

### What the ten needed beyond a search and replace

- **`ls` sets its own buffering.** It already calls `tty_of(SYS_STDOUT)` to
  choose columns, so it answers `Buffering::Auto`'s question from what it knows
  and skips the probe.
- **`echo` says `Buffering::Full`** for the opposite reason: its whole output is
  one flush, so the buffering question is not worth a `tty_of` to answer. It is
  also the most-invoked program in the system, and the probe would have been
  half its cost.
- **`vmstat` flushes per sampled row.** With an interval and no count its loop
  never returns, so `proc_at_exit` never runs; what coalesces there is the
  header with the row beneath it, and `-s`'s fifteen counter lines into one.
- Everything else is `write_all(SYS_STDOUT, x)` becoming
  `File::stdout().write(x)` and one checked `flush()` on the way out.

`File::getline` replaced the `LineReader` in `grep`, `head` and `tail`;
`LineReader` still has callers in `cp` and `mv`, so it stays.

### `/bin/mount` had no test at all

It was the one program in the tree **no smoke case ran** — it appeared only
inside a comment. Rather than a case of its own it went into `sysinfo.mjs`,
whose opening comment already described `mount` as the arrangement over
`/proc/mounts` that `uname` has over `/proc/host`. Its output is asserted row
for row against `/proc/mounts` itself, so what is checked is the reformatting
rather than a fixed string.

## A buffer in the program, which §4.4 says not to write

`src/proc/file.h` is a buffered stream — `File`, with `get()` a rune, `put()`,
`read()`, `getline()`, a sticky error and a flush. It adds **no syscall**,
`PROC_ABI` does not move, and `--gc-sections` keeps every byte of it out of the
thirty-five programs that do not name it. `/bin/cat` is the one that does.

**It exists because there was no way to write a port's inner loop.** The
request that started it was `while ((c = getchar()) != EOF) putchar(tolower(c))`
— four lines of C that had no expressible form here. `read_some(fd, 1)` is one
syscall per byte at 34–45 µs, which is forty seconds to a megabyte; `Input` and
`LineReader` are chunks and lines and neither yields a character; and nothing
anywhere decoded UTF-8 off a stream, so `getchar` could only have been a byte,
which on a console that is UTF-8 (§2.3) is the wrong answer rather than a
limited one.

**§4.4 says to push anything substantial into a syscall, and this one cannot
go.** System_Calls.md's §"Read semantics" already argued the opposite direction
and was right: the kernel keeps what a short read left *because* a buffer in a
program outlives the descriptor number it was keyed to. That argument bounds
where a userland buffer may be used; it does not make one avoidable, because
there is no operation that would make a codepoint cheaper than a round trip —
`Sys::Read` already clamps to `SYS_READ_MAX` and the cost is the hop, not the
size. So the exception is stated in Concept.md §4.4 rather than smuggled, and it
is paid for with two rules `File` states and does not enforce: a buffered `File`
owns its stream until `close()` or `detach()`, and its destructor does not
flush. `/bin/sh`'s `read` builtin, the caller that must take a line off a pipe
and not the next one, therefore keeps its own hand-rolled loop and is not
converted — which is the rule demonstrating itself.

**The sticky error is what makes a port readable, and it is the one idea taken
from stdio rather than from here.** Every other API in this tree returns
`Result` and expects it checked at the call. Per character that is unwritable:
the C loop has no error check because `ferror` is checked once at the end, and
a translation that puts three lines inside the loop is not a translation. So
`get()` both returns the error and latches it, `failed()` is `ferror()`, and
the `while` condition is `Result`'s `explicit operator bool` — which means
`Err(Closed)` stays exactly the end-of-input convention every other reader here
already uses and nothing had to be invented for EOF.

**`get()` and `put()` are awaiters and not `Task`s, which is the whole of the
machinery.** A `Task<Result<char32_t>> get()` would allocate a coroutine frame
per character — 30–40 ms a megabyte, small beside the syscall it replaces but
pure waste — and, worse, would hand every port the null-frame problem: awaiting
a `Task` whose frame did not allocate **panics** (`task.h`), which is why every
call site in `src/cmd/` writes `Task<...> t = f(); if (!t) …` and why no port
would. As an awaiter, `await_ready()` is true whenever the buffer can answer, so
the fast path allocates nothing and suspends nothing; when it cannot, the slow
path's `Task` is built *into the awaiter*, which lives in the awaiting
coroutine's own frame, and transferred into by the same symmetric transfer
`Task::Awaiter` already performs. A frame that will not allocate is then
`Err(NoMemory)` on a stream, not a trap. `FileBuf` — the bookkeeping, the rune
boundaries, `unget`, the newline scan — has no syscall in it at all and is
compiled straight into `tests.wasm`, where a rune straddling a buffer boundary
can be tested at every offset and every width.

**`unget` puts bytes back rather than keeping a rune aside.** The obvious
implementation is a one-slot `char32_t` that only `get()` consults, and it is
wrong twice over: `read()` and `getline()` would step over it, and a pushback of
a *different* rune than the one taken — which `ungetc` allows — has nowhere to
live. Encoding it back in front of the held bytes costs a `memmove` in the case
where the last take did not leave room, and after it there is no second path
through the buffer for any reader to miss.

**Buffering is decided by one `tty_of`, not by a rule.** The tempting answer was
a fixed default — line-buffer everything, and let a bulk program opt out — which
would have cost `cat` a syscall per line on a 64 KiB file. The other tempting
answer, full buffering, breaks interactive `cat`: the smoke suite types a line
into it and expects to see it echoed twice *before* `^D`. Both are real, so
`stdout` starts as `Buffering::Auto` and resolves itself on its first flush,
which is one round trip once per process and only for a program that uses
`File`. `stderr` is unbuffered and allocates nothing at all.

**The block is 512 bytes because the allocator says so**, and the 64 KiB span is
opt-in through `reserve()`. `cat` asks for `SYS_READ_MAX`, so it still costs a
round trip per span rather than per half-kilobyte, and the manual says out loud
that a `char buf[4096]` on a coroutine frame is the trap it looks like.

**Flushing at exit is a function pointer, not a wrapper.** The root task in
`rt.cpp` awaits `proc_at_exit`'s hook after `proc_main` returns, and
`File::stdout()` installs it on first use. A `proc_root` that referenced the
flush directly would have put `File` into every binary in the tree and undone
the one property that makes this affordable. `Sys::Exit` takes effect when the
root task returns, so a program that exits mid-stream still flushes.

`rune_lower` and `rune_upper` went into `kernel/text.h` beside the two UTF-8
functions rather than into `file.h`: they are the other half of what "a
codepoint, not a byte" means, and the case ranges are algorithmic, so ASCII
through Cyrillic is thirty lines and no table. What is not one codepoint for one
— ß, the ligatures, the Turkish i — comes back unchanged and is documented as
doing so, because a table is a table and nothing has asked for one yet.

## The one number the kernel cannot make

`Sys::Random` is op 5, and it is the first operation here that exists because
the kernel is *unable* to answer rather than because a program had nowhere to
ask. `sched_now()` is a millisecond counter that starts at zero every boot; a
pid is a serial number handed out in order. Neither is entropy, and a mixer over
the two is a sequence anybody with a stopwatch reproduces. So `exec_sys` has no
case for it at all and falls to `-Unsupported`: a refusal is diagnosable, and a
plausible-looking number is not.

**TODO.md's N5 argued against this ABI, and was right for four milestones.** It
proposed a shell-local PRNG seeded from `Sys::Now ^ Sys::GetPid` — no operation,
no bump, no host code — on the ground that nothing in the tree generated a
nonce, a key or a session id. What changed is not that one of those appeared; it
is that `$RANDOM` is *itself* the caller §4.3's first rule wants, and once a
shell variable depends on a value, seeding it from a boot clock is a lie told in
the documentation rather than a simplification. The PRNG did not survive either:
with a synchronous operation there is nothing left for it to do.

**The synchronous half is closed at five, and was closed at four.**
System_Calls.md said four *permanently*, and gave the reason in the same breath:
each of the four is answerable inside the process's own worker with no kernel to
ask. The reason was right and the number was a census.
`crypto.getRandomValues` is on `WorkerGlobalScope`, fills its array and returns;
it passed the stated test the whole time. Writing a census down as though it
were the rule is a mistake this codebase can make cheaply and should correct
cheaply — and the correction is not to open the set but to say what else has to
be true.

**Three things, at once, which is what shuts it again.** The worker must answer
with nothing to ask. Nothing else may already answer the same question, or a
program gets two answers and has to know which one it asked for. And the whole
answer must fit the one `i32` `sys` returns, because the synchronous wire has no
payload direction at all — `Sys::Stage` exists so the *host* can find somewhere
to copy into, and a process cannot receive through it. Randomness passes all
three, and it is the limiting case of the second: every bit pattern is a valid
draw, so there is no answer for a second source to contradict. The wall clock,
the obvious next candidate, fails two of the three, and neither failure is about
how synchronous `Date.now()` is: `Sys::Now` already answers "what time is it"
there, and what `Sys::Clock` carries is a `u64` of epoch milliseconds *and* an
`i32` of timezone offset, which does not fit 32 bits and never will.

**Each realm draws for itself, and that is not two answers to one question.** A
process draws in its own worker through `Sys::Random`; the kernel draws through
`SvcOp::Random` and `host_svc`, which is how `/proc/random` is served. Two
mechanisms for one idea would normally be a smell, and the second clause above
is exactly why it is not one here: there is no agreement to preserve between
them, because there is no right answer to disagree about. It also settles a
misreading the first attempt at this made: §2.2 governs the *kernel's* six
imports of `host`, and a draw made in a process's worker never crosses that
boundary at all. There is no seventh import either way.

**`/proc/random` is a decimal `u32` per `open`, and its `stat` says 0.**
Measuring a file must not spend entropy: `ls -l /proc` would otherwise make a
host round trip per listing and `test -s` one per test, purely to learn a width.
Linux answers 0 for every `/proc` size for a related reason, and this tree
already argued that a `/proc` size is a snapshot which need not agree with the
next read. `open` is the only one of the three calls asked to produce the text,
so it is the only one that awaits, and `generate()` stays synchronous — which
also keeps `ProcFs::stat` and `ProcFs::list` reachable from `run_now()` in the
unit suite.

**`$RANDOM` is drawn per reference, and it is a CSPRNG.** The constraint that
forced the first design has not moved: `cb_look` in `src/cmd/sh/var.cpp` is a
plain `bool`, word expansion cannot await, and `src/cmd/sh/expand.cpp` is
compiled straight into `tests.wasm` where a syscall is a link error. What moved
is that a *synchronous* syscall does not need to be awaited. The counter, its
seed, `var_seed_random` and the seeding call in `shell.cpp` are all gone, and
with them one host round trip per `/bin/sh` start — every `sh -c` in a pipeline
included. There is no longer a degraded mode to document either: the old design
fell back to the pid and the boot clock when the host would not answer, and
nothing falls back now.

**The wire has no argument and no error channel**, which is what makes it a
synchronous operation rather than a synchronous version of an asynchronous one.
It takes nothing and returns 32 bits. There is no count to validate, no pointer
for the host to write through, no `SYS_RANDOM_MAX`, and no status: every bit
pattern is a draw, all zeros included, and one of `0xffffffff` arrives looking
like `-1` and is a number. An operation that had to say "invalid" would have had
to say it somewhere, and the only place left is the return value carrying the
answer.

**`PROC_ABI` stays 19.** The operation moved from 59 to 5 before either number
reached main, which is what a branch is for. A new operation is additive and
needs no bump at all; this one arrived alongside a bump that was already being
made, and moving it between halves of the table on the same branch costs nothing
further.

`${RANDOM-x}` draws twice and shows the second: `Walk::braced` asks `is_set`
before it asks `named`, and both go through `Vars::look`. Nothing can tell the
difference — every draw is a number nobody predicted, which is now literally
rather than approximately true — and every fix costs either mutable state in a
file that is pure by design or a shell-specific name list inside an expander
that deliberately does not know what a shell is. It is documented in Shell.md §8
and left alone. bash has the same wart.
