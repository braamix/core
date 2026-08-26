# Release notes — 0.3

Reasoning, alternatives and trade-offs behind the code released as 0.3.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.3 — A shell with a language, and files with names of their own

`BRAAM_VERSION_BASE` moves to 0.3; the commit count and the hash behind it carry
on unedited. The base is the only number anyone chooses, and it moves when what
the system *is* has changed rather than when enough commits have piled up. 0.2
was one program model and a shell that had become an ordinary process. 0.3 is
that shell grown into a language — variables, command substitution, globbing,
functions, `if`, `while`, `until`, `for`, `case`, and a file run by `#!` — over
a filesystem that now answers about what it holds: modification times, symbolic
links, a rename, and `PATH` searched by the kernel rather than by whoever asked.

**Nothing in the numbering forces the move**, which is why it is written down. A
commit count cannot say that the shell stopped reading commands and started
interpreting them, and a hash says less than that. The base is the one place a
difference in kind can be asserted, and 0.2 → 0.3 is that assertion: a script
written against 0.2's shell was a list of commands, and one written against
0.3's may be a program.

## `help` is a document, and `less` is a `cat` off a terminal

`help` was the twenty-seventh builtin: it printed the table with a usage string
carried on every row, then walked `PATH` and decorated each name it found with a
line looked up in `/share/help`. Around a hundred lines of C++ and a `Str` field
per builtin, inside `sh.wasm` — nearly a third of the boot archive — to render a
list that changes when a program is added and at no other time. **What a command
is for is a document, not state**, and the shell had no business holding it: the
first clause of §4.5's rule is about the shell *process's own* state, and a
usage line is not that. So `/share/help` became the whole text, `/bin/help` is
`#!/bin/sh` over `less /share/help`, and `Builtin` lost its `usage` field along
with `builtins()`, which had no other caller. `sh.wasm` fell by 5,877 bytes and
`less.wasm` rose by 1,691 for the copy path below; with the document 2,622
bytes longer for the builtin lines it now carries, the staging tree came out
1,444 bytes smaller at 797,935.

**The document ships as one file, and that is the loss.** The old `help` listed
what the directories on `PATH` actually held, so a program dropped anywhere
appeared in it unasked. The new one lists what the archive ships. That is the
right trade for the system as it stands — the only way a name gets onto `PATH`
today is by hand — but it is `/bin/pkg`'s problem the moment packages install
programs: an installer will have to append to `/share/help`, or the file will
have to gain an "and whatever else is out there" section. Two smaller losses go
with it: `help <arg>` pages rather than answering with a usage line and 2, and a
name that is both a builtin and a file (`echo`, `test`, `[`, `true`, `false`)
now appears in both sections, where the builtin's listing deduplicated it. The
second is arguably better — the two entries say different things, one about what
a prompt runs and one about what a spawn gets.

**`less` had to learn the difference between a terminal and a pipe**, or
`help | grep ls` would have hung with an invisible pager holding the keyboard.
`Sys::Tty` already answers it and `ls` already asks, so the probe is `tty_of`
copied from `ls.cpp`; off a console `less` runs cat.cpp's chunk loop instead and
claims nothing. **It goes before the keyboard claim**, which is the whole of it:
a stage that will not page must not take the keys from one that will. That fixed
`less a | less b` too, which used to be a pager and a refusal and is now a copy
and a pager. The refusal is still worth a test, and `edit … | less` is what
reaches it now — `edit` claims the terminal whatever its output is, and asks
second because it opens its file first.

**The accuracy problem moved rather than vanished.** A generated listing could
not go stale; a document can, and nothing at run time notices. So `test/run.mjs`
checks the shipped `/share/help` against the builtin names in `table.cpp` and
the `bin/` entries of the archive it already unpacks: each named once, and
nothing named that is not there. The archive rather than the source tree,
because what ships is what a reader gets. It is the same discipline as the
exact-import assertion — a list that must not drift, asserted against the thing
it describes.

The worker pool's expected size moved from one to two with this, and that is a
real change rather than a fudged number: `help | cat` is four processes at once
— the shell, the script's interpreter, the pager it runs and `cat` — which is
the widest moment the smoke test ever reaches, and the pool grows to the widest
pipeline it has seen.

## A `/proc` size is a snapshot, and the test asked the wrong file

`test_procfs` checked that `vfs_stat` agrees with a read — what `ls -l`
promises — and asked it of `/proc/meminfo`. A `/proc` file has no stored size:
`ProcFs::stat` generates the text, measures it and throws it away, so the
answer describes the moment of the `stat`, not the moment of the read.
`/proc/meminfo` carries `allocs` and `frees`, which the intervening read moves
— it allocates a `String` and grows it — so the two texts differ in length
whenever a counter gains a digit between them. The check passed for as long as
it did by luck, and failed on CI once the suite's own allocation total drifted
onto a power of ten (71 bytes against 72).

The check is worth keeping, so it moved to `/proc/version`, whose text is a
build constant. Making `meminfo` stable was never an option: live counters are
what the file is for, and the alternative — caching the generated text between
`stat` and `open` — would buy a test a lie, since the two are separate calls
with a tick between them and nothing says the numbers should not have moved.
Linux answers 0 for every `/proc` size for the same reason; Braam answers the
true length of one snapshot instead, which is more useful and no more binding.

## `PATH`

"The environment crosses a spawn" below closed with the reason there was no
`PATH`: *"a `PATH` that did not steer resolution would be a lie a script could
believe."* That set the condition rather than the verdict, and this change meets
it. `/bin` was the only place a program could live, so anything built, unpacked
or fetched had to be copied there or typed with a slash.

**It went where resolution already was, which is the kernel.** The `/bin/`
prefix was a constant inside `exec_resolve` (`src/user/exec.cpp`), not something
the shell built — the shell hands `Sys::Spawn` the word as typed. So the
alternative was a shell that resolved words itself and spawned an absolute path,
and that would have made `PATH` exactly the lie the old paragraph warned about:
`timeout ls`, `watch ls`, a program's own `spawn()` and a `#!` script's children
would all still have looked in `/bin` alone, and the shell would have needed a
second copy of the "is this a program" test to boot. Instead `exec_resolve`
takes the environment blob the spawn is carrying and reads one word out of it.
Every spawn in the system steers by the same list, and `PATH=/x prog` steers the
very spawn its prefix names, because the child's blob is assembled before the
resolve.

**One word, and the kernel still does not read the rest.** `System_Calls.md`
said the kernel does not read the environment at all; that is now true of every
word but `PATH`. `env_value` and `env_path_next` went into `sysabi.h` beside
`argv_at` rather than into `exec.cpp`, because the shell's `command -v` and
`help` need the same two and a second implementation is how a rule drifts.
`proc_env` in `src/proc/rt.cpp` was that lookup already and is now a call to it.

**An empty component is skipped rather than meaning the current directory.**
v7's `::` and its leading `:` are a footgun with nothing to recommend them, and
there is no compatibility to keep here. An absent `PATH` means
`SYS_PATH_DEFAULT`, so `env -i ls` still runs and no existing spawn changed
behaviour; a `PATH` that is *there and empty* names no directories and finds
nothing. Those two had to be distinguishable, which is why `env_value` reports
presence through its return value and leaves the out-parameter alone.

**A file that is not a program does not shadow one behind it.** There are no
permissions and OPFS stores no mode, so "is it a program" is the only
executability test the system has; if a stray `notes` in an early component
stopped the search, `PATH` would be unusable for anything but a curated
directory. The search skips it and remembers, and reports `Err(Invalid)` — 126,
`not executable` — rather than `Err(NotFound)` if it found nothing better. That
is POSIX's found-but-not-executable rule, arrived at from the other direction.
`Err(Unsupported)`, a binary of ours built against another kernel, stops the
search instead: a stale-binary diagnostic is only a diagnostic if it survives.

**The cost is one failed open per miss.** A hit reads the image, which
resolution paid for anyway. Nothing bounds the component count but `SYS_ENV_MAX`
— 8192 bytes of environment — and a cap would be a number to argue about for a
case nobody has.

**The shell exports `PATH` whether or not it is asked to.** `var_set` marks it,
which is the one name in the table with a rule of its own. Without that,
`unset PATH; PATH=/x` would leave a variable that looks right, prints right and
steers nothing, since the kernel reads the *environment*. The mark is the
smallest thing that makes the variable mean what it says. Init plants
`PATH=/bin` beside `HOME` and `SHELL`, so `echo $PATH` answers and a script can
prepend to it.

**`command -v` is the twenty-seventh builtin, and the list is still closed.** It
qualifies under the first clause, not a new one: its answer *is* the shell
process's own state — the function table and the builtin table, which no syscall
shows anyone — so no program could give it. It walks `PATH` with the probe
`test -x` already used, lifted out of `condrun.cpp` as `file_runnable` so that
`test -x`, `command -v` and `help` cannot disagree. That probe checks the wasm
magic rather than the `braam` section, which is a divergence from the kernel
that predates this and is documented where it is.

**Only `-v`.** A bare `command <cmd>` means "run this with function lookup
suppressed", and suppression has to reach the per-stage resolution in
`exec_pipeline`; a builtin runs after that decision, not before it. Refused with
a usage line and 2 rather than half-implemented.

**`help` walks `PATH` too**, first directory naming something winning, because a
listing that still said `/bin` would be answering a question nobody asked any
more. It lists what the directories hold rather than what would run — as it
always did, since checking each entry would cost two round trips a name.

**It cost 971 bytes of kernel and 5,958 of `sh.wasm`** — the search itself is
small; the builtin and the `help` rewrite are most of the second number.

**`vmstat` grew two columns.** `al` and `fr` are adjacent with no gap, six wide,
and a run allocating past a hundred thousand times a second ran them together —
latent, and the new tests were what allocated enough to show it. Seven each; the
row is still inside eighty columns.

---

## `/bin/mv`, and a rename that sometimes cannot

Modelled on v7's `mv`, whose structure is the whole design: try `rename`, and on
`EXDEV` copy and remove instead. What is new here is how wide `EXDEV` had to
become.

**There was no rename anywhere in the stack.** Not in `Sys::`, not in `Fs`, not
in `web/fs.js`'s `OP` table — which is why there was no `mv` either. So the
choice was between doing it all in userland and adding the operation. Userland
alone would have worked: `open`, `read_chunk`, `write_all`, `remove_path` are
all there. It would also have made every rename a rewrite — two syscalls per 512
bytes, so `mv sh.wasm sh.old` is 850 round trips — and, worse, it would have
lost the modification time on every move, because §5.2 has a `touch` that sets
the stamp to *now* and no setter at all. Renaming a file is not editing it, and
a system where it looks edited afterwards is lying about its own files.

**But the operation cannot replace the copy, only get in front of it.** OPFS's
`FileSystemHandle.move()` is implemented for a **file** handle alone, and not in
every engine. A directory move is therefore always the copy path today, on every
browser, and so is any move on an engine without the method. That is why
`Sys::Rename`'s `Err(Unsupported)` is specified as an *instruction* rather than
a failure — "not here, copy instead" — and why `/bin/mv` falls back on that one
error and reports every other. It is `EXDEV` with a wider definition of
"different device": a different mount, a directory, an engine that will not.

`web/fs.js` feature-tests the method rather than naming browsers, so an engine
that gains a directory `move()` starts using it with nothing else changed.

**Policy in the kernel, mechanism in the store.** `vfs_rename` decides what may
replace what — kinds must agree, two directories are `Err(Exists)` rather than a
merge, a mount point is refused, a read-only mount at either end is refused —
and every one of those is answered before the round trip. The store and the two
fakes do nothing but "remove the destination, move the handle". That split is
what keeps the fakes honest: they cannot disagree with OPFS about semantics
because they hold none.

They do have to agree about *capability*, though, and that is deliberate.
`test/fakefs.mjs` answers `UNSUPPORTED` for a directory exactly as OPFS does,
and `TempFs` in the unit suite does the same. A fake that could move a directory
would have hidden the copy path from every test — the path that, in a real
browser, is the only one a directory ever takes. This is the same lesson the
symbolic-link work wrote down about `Err(NotDir)`: a fake that is easier than
the thing it stands for is a fake that tests nothing.

**`mv a a` is where the data loss was.** The fallback removes the destination
before it copies, because a rename replaces. If the two paths name one file,
that removal eats the file about to be moved and the copy then has nothing to
read. `vfs_rename` answers `Ok` having done nothing when the two *resolved
physical* paths are equal — which is `rename(2)`'s answer — and `/bin/mv`
repeats the check on its own two absolute paths, because it is the half that
holds the removal. Resolved and physical, so `mv a ./a` and a move through a
link are the same no-op.

**Two orderings in `vfs_rename` are load-bearing.** A mount point is refused
*before* the cross-mount answer: `/home` cannot be moved by copying either, so
`Unsupported` would send the caller off to copy a whole store and then fail to
remove it. And the kind checks come before it too, so `mv file existingdir`
is `Err(IsDir)` rather than a copy that lands somewhere surprising.

**A source with an open descriptor is `Err(Perm)`.** `OpenShared` is keyed on
the path, so a rename underneath one leaves a record naming a file that is no
longer there — and OPFS holds an open file exclusively anyway, so the move would
have failed in the store with a worse error. Refusing is the honest answer, and
the copy fallback would fail on the same lock.

**What the fallback costs, kept rather than papered over.** It restamps, for the
reason above. It is not atomic: the destination is removed before the copy, so
an interrupted directory move leaves a partial tree and no original destination.
Both are visible from the shell — a moved file keeps its `ls -l` stamp and a
moved directory does not — and the smoke test asserts exactly that, which is how
the two paths are told apart without a probe for which one ran.

**The directory walk is an explicit stack, not recursion.** `ls -R`'s shape, and
for `ls -R`'s reason twice over: a coroutine frame per level would make a deep
tree a deep chain of frames, and descending on `SYS_KIND_DIR` alone means a link
is recreated rather than followed, so no cycle guard is needed. A link moves as
itself throughout — `vfs_rename` follows neither end, as `vfs_remove` follows
neither.

**`-f` and `-i` are v7's, and `-f` still wins.** There are no permissions here
and nothing to override, so `-f`'s only remaining job is the one v7 gives it
regardless of order: it silences `-i`. The prompt goes to stderr and the answer
comes through the `Input`/`LineReader` already in `proc/io.h`, so `mv -i` reads
one line per source whether that is a cooked console line or a script's stdin.

`PROC_ABI` moved from 13 to 14, taking op 29 beside the two link operations.

## Symbolic links

A third node kind, and the first change to the filesystem's type system since
it had one. The motive is `/bin/pkg`: a package manager installs
`/bin/vi -> /share/pkg/vim/bin/vim`, and without links the only alternatives are
copying a binary per name or teaching the shell a second lookup rule.

**The whole design is where resolution lives.** Braam's VFS was purely lexical:
`path_resolve` normalised a string, `vfs_lookup` picked a mount by longest
prefix, and the *whole* remaining path went to one backend, which walked it
itself. Nothing had ever looked at a path one component at a time. A textbook
`namei` would be one host round trip per component on every path operation, in a
system that documents a keystroke's two round trips as a floor worth defending.

So resolution is lazy, and rests on a fact about the store rather than on
bookkeeping: **a link in the middle of a path announces itself as
`Err(NotDir)`.** OPFS walks with `getDirectoryHandle`, which raises
`TypeMismatchError` on a file, and a link *is* a file. So `vfs_resolve` hands
the whole path to the backend exactly as before; a success with a non-link leaf
is the answer, and only `Err(NotDir)` — the one failure a link in the middle can
produce — is worth walking. A path with no links in it costs the round trip it
always did, a leaf that really is a link costs one more, and a plain
`Err(NotFound)` costs nothing extra, because it proves every component above the
leaf was a directory.

That fact had to be *made* true in the two fakes. `test/fakefs.mjs` and the unit
suite's `TempFs` are flat maps keyed by whole path; both would have answered
`NotFound` where OPFS answers `NotDir`, and the walk would never have run under
test. Each grew the same six-line ancestor check. A fake that is easier than the
thing it stands for is a fake that tests nothing.

**A listing never resolves.** `Sys::List` reports `SYS_KIND_LINK` whatever the
link points at. That was chosen for tree walks rather than for fidelity: `ls -R`
and the shell's globber both descend on `SYS_KIND_DIR` alone, so neither can
follow a link out of the tree it is walking, and both stayed correct with no
cycle guard added to either. The `-R` entry above used to justify its unbounded
stack by saying the VFS had no links; it now justifies it by saying a link is
not a directory. Only the globber changed, and only for a trailing-slash
pattern, where a link to a directory *should* match — so it stats the links in a
listing and nothing else.

**`..` stays lexical**, which is `cd -L` and what shells do by default.
Resolving it physically would mean `path_resolve` could no longer pop a
component textually, and that function is not only shared with every process
binary but is what `proc_path` relies on being *synchronous*: the dispatcher
makes a process's path absolute against that process's cwd before its first
await, because another task of the same process may move the cwd underneath it.
Making it a `Task` reopens that race to buy a `..` almost nobody types after a
link. The cwd is stored logically for the same reason, so `cd` through a link
and `pwd` says the link.

**A store has nowhere to put a type**, so a link is a file whose whole contents
are `!<braamlink>` and the target — the reasoning, and the rejected sidecar, are
in Concept.md §5.2. Two things fell out of that rather than being written:
`removeEntry` sees a file, so `rm` and `rm -r` cannot follow a link and needed
no code to stop them; and the magic-plus-target form made the fake and the real
store share one classifier instead of holding two ideas of the format.

**One latent bug surfaced.** The open-file table was keyed on the lexical path.
That is fine when a path names one file, and wrong the moment two do: opening
`/bin/vi` and `/share/pkg/vim/bin/vim` would have asked OPFS for two sync access
handles on one file, and OPFS takes an exclusive lock. It is keyed on the
resolved path now, which is the same rule §5.2 already stated from the other
direction — one backend handle per file — and it is what the unit case about a
reader refusing a writer *through the other name* is guarding.

**What this is not.** There are no hard links: OPFS keeps no link count and one
file has one name, so `ln` without `-s` says so rather than pretending. There is
no `readlink` program — `ls -l` prints the target and `Sys::ReadLink` has its
caller there — and no `-L` on `ls`. And `tools/pack.py` does not carry a link
into `rootfs.zip`: nothing in `rootfs/` is one, so the code would be written
against a case that does not exist and tested against none. `/bin/pkg` writing
links into the store at install time is the case that will ask for it.

**Cost.** `PROC_ABI` 12 → 13, invalidating every stamped binary. Ops 27 and 28
were free — the sparse numbering finally paid for itself, and nothing had to
move. `kernel.wasm` went from 154,751 to 167,373 bytes against its 256 KiB
budget, and the boot tree from ~710 KiB to 754,623 against 1 MiB, of which
`/bin/ln` is 13,595.

---

## A package policy, written before the package manager

`/bin/pkg` will be the first thing in the system that fetches bytes from
somewhere else and writes them into the store. `curl` fetches and prints; the
boot unpack writes, but only what the origin served. Nothing in the tree has
ever had to decide whether to believe a stranger.

**The policy went first because the expensive decisions are the ones that cannot
be taken back.** A key generated on a networked machine is never afterwards an
offline key, and a repository trusted once without a signature cannot be
un-trusted retroactively. Writing `pkg` first and adding signatures afterwards
would have meant a key ceremony performed under pressure, which is the same as
no ceremony. [Package_Management.md](../Package_Management.md) is the result.

**Two roles, not TUF's four.** TUF's *snapshot* role stops a mix of metadata
files that never coexisted and its *timestamp* role bounds replay without
forcing the larger, rarely re-signed files to expire quickly. Both answer
problems a busy multi-writer repository has. There is one index here, published
whole by one writer, and it carries its own version and its own expiry — so both
properties come from the index itself and a second role would sign a restatement
of it. What that costs is written down rather than hidden: there is no
arrangement in which an attacker must steal two independently held online keys,
and one expiry now sets both the re-signing interval and the replay window. PEP
480's per-author keys arrive as a delegation from the index role, which is a
change to what the index carries and not a new trust anchor — that is the
property that makes the reduction reversible, and it is why it was acceptable.

**The verifier is allowed to be the host's, and that is not a concession.**
The first instinct was SHA-256 and Ed25519 in freestanding C++, linked into
`pkg.wasm`: self-contained, no ABI change, no browser variance, and about 15 KiB
against the archive's budget rather than the kernel's. It was rejected on the
observation that **the host is already inside the trusted base without
qualification.** `web/` hands the kernel every byte of `kernel.wasm`, every byte
of `rootfs.zip` and every process image, and only JS can call a process's
exports (§4.3). A host willing to lie about `crypto.subtle.verify` has the
shorter path of supplying a different `pkg.wasm` — so a wasm verifier would be
guarding a door in a wall that is not there, at the price of two cryptographic
implementations in the tree to keep correct. `crypto.subtle` returns promises,
so it is §2.2's convention already: enum values on each side of `host_svc`, and
the six-import surface `test/run.mjs` asserts does not move.

**A missing algorithm refuses rather than degrades.** WebCrypto's Ed25519
arrived much later in Chrome than in Safari or Firefox, so the availability
question is real. The answer is §5.3's: a capability that is absent is reported,
not worked around. A `pkg` that installs without checking is not a weaker `pkg`;
it is `curl` with extra steps, and the system has `curl`. For the same reason
there is no `--force`, `--insecure` or `--no-verify` in any form — a flag that
skips the check is the flag an attacker's instructions tell the user to pass.

**The anchor lives in `rootfs.zip`, which turns an accident into the recovery
path.** Every signature chain terminates in something believed without proof,
and PEP 458's answer is that the package manager ships the root metadata. Here
the archive is already that: built from the tree, packed by `tools/pack.py`,
served from the origin over the same TLS as `kernel.wasm`. Putting the anchor in
`/share/pkg/` means the boot unpack — which deletes each top-level directory the
archive carries before rewriting it — **re-pins it at every version change**, so
it cannot be poisoned in the store for good. §5.2's "the archive, not the store,
is what the system recovers from" turned out to cover the trust anchor too, and
it is what makes the worst case survivable: replacing an anchor out of band
means cutting a release, and releases already exist. The same behaviour is why a
*locally* trusted key under `/share` is wiped, which is recorded as a cost.

**Packages are not signed; the index is.** apk v2 signs each `.apk`
individually. TUF names targets by hash from signed metadata. The second was
chosen for revocation: withdrawing a package costs a re-signed index and no key
operation at all, whereas a per-package signature has to be reasoned about for
as long as a copy of the file exists anywhere. Signing the set rather than each
entry is what makes "these versions, together" the thing attested — otherwise an
attacker can pair a real signature for a new package with a real signature for
an old one, in a combination the publisher never produced.

**A rejected third option was apk's trusted-key directory** — a folder of public
keys, trust meaning presence in it. It is much less work and it was turned down
for what it lacks rather than what it has: no version and no expiry anywhere,
so neither rollback nor freeze is detectable, and an attacker who can answer for
the network can serve last year's index for ever.

**What the document deliberately does not claim.** `/bin` is writable, OPFS
stores no per-file mode, and `rm /bin/sh` already works, so the property on
offer is "`pkg` installs only what it checked" and not "only checked code runs".
The
second needs a privilege boundary the system does not have, and a policy that
implied it would be worse than one that admits it. The other admissions are in
that document's last section: nothing re-checks the store after install, a
version change erases what `pkg` put in `/bin`, the wall clock is the user's,
and a repository that does not send `Access-Control-Allow-Origin` is unreachable
rather than insecure.

Nothing is built. Concept.md §6 gains the service as **unbuilt**, and the next
commit is `pkg`'s design — which should need no decision this policy declined to
make.

## Files have a modification time

The filesystem stored `{kind, size}` and nothing else. The section on `ls`,
below, blamed five of BSD's flags on that and was right to — but the store had
been keeping the missing field all along. `getFile()` yields a `File`, a `File`
carries `lastModified`, and `OpfsStore.stat` and `OpfsStore.list` were already
awaiting one apiece to learn a size. The mtime cost one property read.

**Milliseconds, not seconds.** Seconds is Unix's unit, good to 2106, and would
have fitted the free `aux` word in `HostRequest` — no reply shape would have
moved and no buffer would have been reserved per stat. It was rejected on the
one workload this field exists for. A build step in this system finishes in well
under a second, so second-granularity stamps would routinely give a target and
the source it was made from the same time, and a `make` that cannot tell those
apart is not a `make`. The unit that costs something is the unit that works.

**`FsOp::Stat` answers through the buffer now, as `Info` always has.** Its two
scalar channels were both spoken for — `flags` held the kind, `result` held the
64-bit size — and a second u64 does not fit beside them. Borrowing `aux` would
have worked for seconds and not for milliseconds, and adding a word to
`HostRequest` would grow every request record in the system for one operation.
So `Stat` packs `{kind, size, mtime}` into 20 bytes the way `Info` packs 32.
The width is fixed, so `OpfsFs::stat` reserves exactly it and `List`'s
ask-again round trip cannot arise here. The cost is one small allocation per
stat, against a `FsCall` that already allocates the path.

**0 means the filesystem does not know, and `ls -l` prints a dash.** OPFS has no
timestamp on a directory handle at all, and a `/proc` file is generated at
`open` and has no moment to name. Unix would report 0 as a real time and render
`Jan 01  1970`; that reads as a broken clock on every directory in every
listing, which is the same misreading that moved the release archive's stamps
off 1980. A dash says what is true. Nothing here predates 1970, so the sentinel
can never collide with a stamp.

**`touch` moves an mtime by rewriting the file, and checks that it worked.**
OPFS has no setter — there is no `utimes` to reach for and no prospect of one —
so the only lever is to modify the file and let the browser restamp it: its own
first byte written back, or, for an empty file, a byte written and truncated
away. Whether that restamps is the browser's decision and not ours, so the host
reads `lastModified` back and answers `Unsupported` when it has not moved. A
`touch` that silently did nothing would be exactly the failure this system
refuses elsewhere — a thing that looks like it worked. This is also the one
place in the tree where the store's own behaviour, rather than ours, decides
whether an operation exists, and the fake backend cannot answer the question:
`make serve` in a real browser is the only test of it.

`PROC_ABI` went 11 to 12. `Sys::Stat` and `Sys::List` widened, and `Touch` took
op 24, pushing `Chdir` and `Dup` up one — a renumbering worth doing while the
ABI word was moving anyway, since it keeps the path operations contiguous.

**`civil()` moved out of `date` into `src/proc/time.h`.** It was already the
branch-free `civil_from_days`, and already correct; it was in an anonymous
namespace where `ls` could not reach it. `src/proc/` is where it belongs — pure,
no host import, so it is linkable into `tests.wasm` (which is what
`test_time.cpp` does, over the leap-day cases and the two century years the era
arithmetic exists to get right) and it ships with the SDK unasked. `date`'s
output did not move, which is what the unchanged `date -u` assertion checks.

**`ls -l` gains twelve columns and one syscall.** The two BSD forms —
`Mmm DD HH:MM` inside six months, `Mmm DD  YYYY` outside — rather than ISO,
because `date` already renders in that register and a listing that agrees with
the system's own clock command is worth more than lexical sortability nothing
here sorts on. The one `clock_now()` is read only under `-l`, and supplies both
halves of the decision: the timezone to render in, and the "now" the six months
are measured from. A clock that will not answer costs the recent form and not
the listing.

**`-t` is the flag the gap actually cost**, and it arrives with `-S` folded into
one `order` key rather than beside it as a second flag, so the last of the two
given wins the way the last of `-l`, `-1` and `-C` already does. `-i`, `-s`,
`-o`, `-T` and `-L` remain out of reach, and for the original reason: mode,
owner, link count and inode are still not stored anywhere.

**`test -nt` and `-ot` are the first binary file primaries.** `cond.cpp` had
string and integer comparison and no binary operator that looked at a file, so
this adds a shape rather than a row: `CondProbe` grew an `arg2` and the probe
walk answers both files in one go. Both are false when either file is missing,
which is also what two files with no mtime between them answer — a directory
compares false against everything, including another directory.

The fake store stamps from a clock of its own, seeded behind `fakesvc`'s frozen
wall clock so a stamped file reads as the recent past and the `-l` output is
exact. It advances a second per write, which is what gives `ls -t` something to
sort and lets the suite prove `touch` moved a stamp rather than merely returning
success.

`kernel.wasm` grew 2,581 bytes to 154,751 (59% of budget) and the staging tree
5,174 to 736,180 (70%). `ls` is 2,653 of that, `test` 824, `sh` 870, `touch`
677 and `date` 90 — the calendar is shared now, but every binary that links it
still carries its own copy, which is §4.4 again.

---

## `import` and `save` became `fimport` and `fexport`

The pair was asymmetric because one half of it was never chosen: the shell
builtin owns `export`, so the program that is §5.4's Blob download took the name
that was left ("`/bin/export` became `save`" below). A user who found `import`
had nothing to guess from.

**The `f` is `fopen`'s.** The prefix marks the variant whose plain name is
occupied — `fopen`/`open`, `fstat`/`stat` — and that is exactly the situation
here. It buys guessability in both directions: either half now implies the
other, which neither `import`/`save` nor a one-sided rename would give. The
cost, stated plainly: `fimport` has no occupied sibling to be distinguished
*from*, so its prefix is there for the symmetry rather than for a collision of
its own.

`upload`/`download` was the other candidate and was rejected on register.
Upload and download name a *network* boundary, and this system has one of those
with a program already on it — `curl`. What these two cross is an application
boundary, which is import/export's register. There is also nothing to upload
*to*: the browser calls the picker an upload only because a form posts
somewhere, and here it does not.

**`/import` did not follow.** The directory keeps its name, so `fimport` writes
somewhere not named after it — the one thing given up. Boot creates `/import`
on every run and "/mnt/import became /import" below records that such a rename
migrates nothing; a second orphan directory would leave every existing store's
files stranded in the old one. Weighed against a lost correspondence in a name,
the files win.

**The service was renamed to the bottom, enum spellings included.** `Sys::Save`
is now `Sys::Fexport` (operation 56 — the *number* did not move, so nothing on
the wire changed), `SvcOp::Save` is `SvcOp::Fexport`, `save_file` is
`fexport_file`, the SDK's `save()` is `fexport()`, and the page verb `svc:
"save"` is `svc: "fexport"`. The alternative was renaming the program alone and
leaving a `save` stack under it, which reads as an unfinished rename to whoever
finds it next.

The argument against going this deep, recorded because the decision went the
other way: the `f` prefix exists to dodge a *shell builtin*, and there are no
builtins below the shell, so these layers now carry a marker that means nothing
where they live — an ABI operation named after a workaround from above it. What
carried the day is that a half-renamed stack costs every future reader a
question, while a uniformly-named one costs only this paragraph.

**It is a shipped SDK break.** `save()` is declared in the installed
`include/braam/proc/io.h`, so an out-of-tree program calling it stops compiling
and must say `fexport()`. There is no deprecation shim: the SDK has no
compatibility surface yet and starting one for a two-command system would
outlive its usefulness.

Nothing needed a new test. The M6 criterion already drives both programs
end to end, and it now drives them under their new names; the per-binary ABI
assertions read a glob rather than a list, so they followed the rename without
being touched. One thing the rename *did* surface: `build/web/` is assembled by
`copy_directory`, which never deletes, so the first build after it carried 41
binaries — the two old ones included. `make clean` is the answer and the
CMake section of CLAUDE.md already says so.

---

## `export notes.txt` is refused rather than obeyed

It surfaced out of a naming question — whether `/bin/import` and `/bin/save`
should become `upload`/`download`, or `fimport`/`fexport` — and the answer
turned out not to be a rename at all.

`/bin/import` has no `/bin/export` because the builtin owns that name
("`/bin/export` became `save`" below). So a user who has found `import` and
wants the way back out guesses `export`, and until now the guess *worked*: the
builtin handed its operand straight to `var_mark`, which validates nothing, and
the shell created an exported variable named `notes.txt`, printed nothing and
exited 0. Of the ways that line could have failed, silent success is the worst
one — nothing to search for, nothing in `$?`, and a variable in every child's
environment from then on. `read notes.txt` did the same.

**No rename closes that**, which is the argument for leaving `import` and `save`
alone. A user under `fimport`/`fexport` still guesses `export` the first time;
what they need is for the guess to say no. The asymmetry between `import` and
`save` is a scar with a reason, and it cost exactly one thing, which is now
paid.

**Checked before the value**, so `export 2a=1` neither assigns nor marks. The
alternative — assign, then refuse the mark — would leave a variable behind from
a line that reported failure. The remaining operands are still applied and the
refusals are collected into one write, which is what the readonly refusal
beside it already did.

**`read` validates every name before it reads**, not as it fills them. A usage
error that consumed a line would be a silent data loss inside `while read`, and
the loop would then end on the next iteration for the wrong reason.

**`var_init` is deliberately exempt.** It imports the environment blob the
kernel handed the process, and dropping an entry there would be a silent loss
of something a caller meant to pass — the failure this change exists to remove.
An environment is not a place to enforce the shell's spelling rules.

**The rule was written three times**, all file-local: `is_name` in `parse.cpp`,
a hand-inlined copy of the same character class inside `is_assignment` beside
it, and `is_name_start`/`is_name_char` in `expand.cpp`. None was visible outside
its own translation unit, so a builtin could not ask the question the grammar
had already answered — which is how the hole survived this long. They are now
one header-only `name.h`, included by both grammar files and by the two
builtins. Header-only and `inline` matters: `braam_sh`'s builtins reach a
syscall and the grammar is compiled into `tests.wasm`, which links no process
runtime, so a shared `.cpp` would have had to be pure anyway and a shared
header costs nothing either way.

The predicate is unit-tested; the builtins cannot be, since `write_all` is a
syscall and the unit suite links none, so their behaviour is asserted in
`run.mjs` — including that the refused name reaches no child's environment and
that a refused `read` leaves the line for the next one.

---

## Pids are reused, and syscall servers no longer spend them

This reverses "The pid counter saturates rather than wraps" below. The reasoning
there was sound on its own terms and the terms have changed: the requirement is
now that the system run for as long as the tab is open, and a counter that only
climbs is a lifetime rather than a bound. Two things came out of taking that
seriously.

**The wall was closer than the counter.** `next_pid` is a `u32`, but
`SYS_PID_MAX` was `0xffffff` and `Sys::Spawn` refused any pid above it, so the
system stopped being able to run programs at 16.7M spawns — not 4.3 billion.
That is about two minutes of bulk piping at 150k parked syscalls a second.
`/proc/stat`'s `spawns` was already measuring it; nobody had divided.

**The fire hose was not processes.** 99.99% of the space went to the syscall
server each parked call gets, which is a task `Wait`, `Kill` and `Fg` cannot
name — the op word carries a pid, but all three look it up in the caller's own
children, so a number that names no child of yours is `Err(Perm)` whatever it
is. Spending a nameable resource on unnameable things was the actual defect.
Servers now come from a second table with ids above `SYS_PID_MAX`, and the pid
space is spent only on what can be addressed: at a heroic ten programs a second
it lasts nineteen days, and at a realistic rate, years.

**Two tables rather than one partitioned vector.** A range test on the id would
have served the allocator, and a single `jobs` vector would have kept one sweep,
one teardown order and one gauge loop. Two tables cost a second pass in each of
those and a teardown-ordering argument that used to come free — `anon` is
destroyed first, because a server is spawned after the process it serves and its
frame holds a `ProcRef` and awaitables parked on that process's stdio. What they
buy is that `sched_procs` walks one table and is done: an anonymous job is not
in `/proc` because it is not in the structure `/proc` reads, rather than because
every reader remembers to filter.

**Servers left `/proc`, and that is a real loss.** You can no longer see *which*
syscall a wedged process is stuck in — only how many, from `/proc/<pid>`'s
`calls`. Against it: `ps` during a large pipe was mostly rows for one-syscall
coroutines, and the PID column had grown to six and seven digits reporting how
much I/O the session had done rather than how many commands had been run.
`PROC_MAX` is 64, and a bulk pipeline could push the snapshot past it and
truncate `/proc/tasks` silently; processes alone will not. `exec_proc_state`'s
nested scan over every process × every outstanding call — which existed only to
give a server row a `ppid` — is deleted.

**The gauges still count what the listing hides.** `/proc/stat`'s `tasks`,
`ready`, `on_timer`, `on_host` and `on_park` cover both tables, so `tasks` is
now larger than the number of `/proc/tasks` rows. Hiding a job from a listing is
not hiding it from a count, and `on_host` counting the servers is the half of
that figure worth reading — `vmstat` relies on it, since its own server is
runnable while its stepper is parked.

**Reuse is made safe by reservation, not by the wrap being large.** The old note
is right that skipping *live* pids cannot help, because every dangerous case is
a pid held across the death of the task it named. So those cases hold the pid
explicitly: `sched_pid_hold` is a counted set the allocator skips, taken by an
uncollected `Child` entry and by a foreground entry, which the audit found to be
the whole list. The other holders identify their subject some other way and
needed nothing — the keyboard and screen claims compare pointer identity
(`g_raw == ring_`), and `web/proc.js`'s map entry is deleted by `End::~End`
before the job is reaped. `Call::server` was the near miss: `serve` deletes the
`Call` before returning on every path but one, and that one now clears `server`
so the invariant is flat — it names a live job or it is 0. With the holds in
place, the shell's `alive()` over `/proc/<pid>` needs no change, since the file
existing again can only mean the same task.

**`SYS_PID_MAX` is 999999 and not `0xffffff`.** Six digits keeps `ps`'s PID
column at seven characters, which is what the width-from-the-table work below
was compensating for. It is no longer the op word's limit — the argument is 24
bits and would carry 999999 with room to spare — so the constant's meaning
changed from "the largest the field can carry" to "the largest pid there is",
and `test_sysabi` now asserts both separately: `SYS_PID_MAX + 1` round-trips,
and the field truncates at `1 << 24`.

**`PROC_ABI` did not move.** No wire format, operation, payload or import
changed. A binary built against the old constant differs only in that a stale
`ps` would look for rows that no longer exist, which is cosmetic; bumping would
force every out-of-tree SDK program to be rebuilt for nothing structural.

**`wraps` is a new `/proc/stat` counter.** Laps of the pid space, which after
this change is the event worth knowing about — the anonymous space wraps freely
and is not counted, since nothing names one of its ids past its job.

## The pid counter saturates rather than wraps

`sched_spawn` refuses to spawn once `next_pid` has come back round to 0, instead
of handing that 0 out and carrying on.

Nothing reaches this. The counter is a `u32` and only a spawn advances it; a
page would have to run for days doing nothing but parking syscalls. But it moves
faster than it reads: since each parked syscall gets a scheduler job of its own,
the counter advances at *syscall* rate rather than at process rate, and bulk I/O
through a pipeline burns a pid per `SYS_CHUNK` per stage — about 1,500 for a
quarter of a megabyte through three processes. Fast-path calls (`sysfast`) park
nothing and cost none. What used to be "one pid per program you ran" is now a
number with a plausible growth rate behind it, which is reason enough to say
what happens at the end of it.

The wrap would have been silent and would have handed out the one value the
whole system reads as *nobody*: `sched_spawn`'s failure return,
`tty_keys_owner()`'s "unclaimed", `SYS_WAIT_ANY`, `Fg(0)`, `link.pid = 0`. A
`Proc` whose child was pid 0 would wait on a child it could not name.

Two ways to not wrap. Restart the counter at 1 and skip pids still in use, which
is what a system with a small pid space does — but pids are never reused here,
and a good deal depends on that: a process's exit status is recorded on its
parent's record by a destructor that finds the parent by pid, long after the
parent might have been replaced by a namesake. Or stop. Stopping costs one
comparison, needs no free-pid scan, and lands on a path every caller already
handles, since a spawn can already fail for want of memory. The pid space is a
resource like any other and running out of it is an out-of-resources failure,
not a wrap.

Not sixteen bits with reuse, which is the other way to never wrap. Sixteen bits
is about half a second of piped I/O at the rate above, so recycling would be the
steady state rather than an edge, and three places would then be wrong within
seconds of each other: the shell reaps a background job by asking whether
`/proc/<pid>` still exists (`alive` in `job.cpp`) and would find a stranger and
report its job as still running; `console_fg_has(pid)` compares bare pids, so
`^C` would aim at whatever inherited the number; and the destructor that reports
an exit status calls `proc_find(parent)` deliberately *after* the parent may be
gone, relying on that lookup failing. All three are designs in which a failed
lookup means *gone*, and non-reuse is what makes it mean that. Skipping pids
that are currently live does not help — every one of those is a pid held across
the death of the task it named, which is exactly what skipping cannot see.
Nothing stores a pid narrowly (`sysabi.h` is `u32` throughout) so the narrower
space buys no memory either.

`ps` computes the PID and PPID column widths from the table it just read,
instead of the constant
5. The counter now advances per parked syscall, so a session passes 99,999 in
   about half a minute of piped I/O, and `put_right` writes anything wider than
   its column whole — every row after that would shift right and the table would
   stop being one. The width is the widest value **plus one**: the two columns
   are adjacent with nothing but their padding between them, so at exactly full
   width `100070` and `100068` print as `100070100068`. The old constant hid
   that by being one wider than any pid it ever saw.

## WORKER was the CWD column saying it twice

`ps` has ten columns rather than eleven and `/proc/tasks` twelve fields rather
than thirteen: the worker is gone from both. `/proc/<pid>` keeps its `worker`
line.

**The column carried one bit, and four other columns already carried it.**
`exec_proc_state` fills `worker`, `calls`, `fds`, `pages` and `cwd` together or
not at all, because they all come from the `Proc` the pid does or does not have.
A `Proc`'s cwd is never empty — `exec_process` assigns `vfs_cwd()` when the
spawn named none, and a process whose assignment fails never starts — so `CWD`
is `-` exactly when `WORKER` was, and it is the one of the two that also answers
a question worth asking. What is lost is a *label*: `bound` said "this is a
process" in a word, where the reader now infers it. That is the trade, and seven
columns of an eighty-column row is what it buys.

**`dying` went with it, and was unreachable.** `worker_of` had a third value for
`st.dead`, but `End` sets `p->dead` and calls `proc_remove(p)` three lines later
in the same destructor with no await between, so `proc_find` never returns a
record with it set and no reader ever saw the word. `ProcState::dead` is
deleted; `Proc::dead` stays, since `Sys::Wait`'s parent check reads it — where,
for the same reason, the `!par` beside it is what actually fires.

**`ps` used the field as its own predicate** — `worker != "-"` decided whether
CALLS and FDS print a number or a dash — and now tests the cwd instead. That is
the whole of the program's change beyond the column itself, and it is why the
field could not simply be left in the file unread.

**`/proc/<pid>` is the exception on purpose.** A pid with no process behind it
prints no `cwd` line there at all, and no `fds` and no cap: the file is named
lines rather than a fixed row, so absence says nothing and `worker -` is the
only positive statement that this is a kernel coroutine. Concept.md §5.1 now
says the two files differ in that, and why.

`kernel.wasm` 151,028 → 150,828.

## The `braam:` prefix is gone

Everything the system writes to its own screen used to begin `braam:` — the
shell's `say` and `say2`, `exec`'s `<path>: crashed` and `no worker, retrying`,
and every line boot and init print. It says nothing: there is one system on that
screen and it has already named itself in the banner one row above.
`braam: sh: too many processes` is now `sh: too many processes`, and
`braam: the shell died (status 132)` is `the shell died (status 132)`.

**What the prefix was actually drawing** is the line between the runtime
speaking and a builtin speaking, which Shell.md still records: a builtin says
`<name>: <what>: <why>` and names itself, because several of them can fail the
same way and the name is the only thing that says which one did. The runtime is
not one of several. Where its message names a path or a command word, that word
is what the reader needs and now leads the line; where it does not, the sentence
was always the whole message.

**It is kept where the terminal is not the destination.** `panic()` goes through
`host_log` to the browser console, and `web/braam.js`, `web/render.js` and
`web/worker.js` throw or report to the embedding page — text landing among
everything else that page logs, where the prefix is the only thing saying who
spoke. The rule is the destination, not the severity: on the Braam screen
nothing needs telling, off it everything does.

`sh_source` was the one caller that had to change shape rather than lose a
literal: it printed through `errln("braam", path, err)`, whose first argument is
the speaker, and now passes the path as the speaker with an empty middle — the
form `errln` already had for a program naming a file.

`kernel.wasm` 151,542 → 151,028: seven bytes of literal in a dozen places, and
one `write` per diagnostic in `exec`.

Notes below this one quote the old prefix in their examples. They are what was
true when they were written and are left alone.

## Sixteen deep, and saying so

`SYS_PROC_DEPTH` is 16, from 8, and a spawn refused by either bound now prints
`braam: <name>: too many processes` rather than `not executable`.

**Eight was reached by hand.** Typing `sh` at the prompt costs a level, so the
eighth one was refused: init's shell is depth 0 and the check is on the child's
depth, which makes the bound "processes in a chain, counting the first" — seven
nested shells and then a refusal, on a system where a nested shell is the
ordinary way to try something in another directory. The bound exists for the
fork bomb, and a fork bomb does not care whether it is stopped at 8 or at 16:
the depth cap is the second of two, and `SYS_CHILD_MAX` still holds the width at
16, so the product is what bounds the tree either way. Doubling the one that a
person walks into and leaving the one that a bomb walks into is the whole
change.

**What sixteen costs.** Nothing at rest — depth is a `u32` on `Proc`, not a
table — and at worst sixteen live instances in a chain, each a worker of its own
and each about 900 KiB committed of its 16 MB cap. The host makes workers on
demand (`MAX_IDLE` is the pool's size, not a ceiling on live ones), so a deep
chain hires rather than waiting; there is no new way to deadlock. A chain that
deep is a person nesting shells or a script recursing, and the recursion is
exactly what the bound is for.

**The message named the wrong problem.** Both bounds answer `Err(NoMemory)`, and
the shell spelled out only `NotFound` and `Unsupported`, so a depth refusal fell
through to `not executable` — which points at the binary, sends the reader to
`test -x` and to rebuilding, and is false: `/bin/sh` is a program and had just
run seven times. A new `Error` value would say it exactly and is still far too
expensive for a diagnostic string, for the reason the `#!` notes below give:
`Error` crosses the wire as a negated `i32` in every reply, so it means a
`PROC_ABI` bump and `web/` changes. So the shell reads the value that already
arrives. The cost is that a genuine allocation failure inside `Sys::Spawn` now
says `too many processes` too — the wrong half of one error rather than the
wrong error, and the status stays 126, which is still what "found and would not
run" means.

The test is a script that runs itself: `deep.sh` recurses until the kernel
refuses, and every level above the deepest returns its status, so the error is
printed once and 126 comes back to the prompt. It is also a fork bomb with the
bound taken out, which is the point of running it.

System_Calls.md said `SYS_CHILD_MAX` was 8 in prose and 16 in the table beneath;
the prose was stale and is now the table's number.

## `#!` after all

A text file beginning `#!/` or `#! /` is now a program. `./script.sh` runs, a
bare `script` in `/bin` runs, `Sys::Spawn` on one from any process runs, and
`test -x` agrees. `kernel.wasm` went 149,613 → 151,542 of 262,144 and `sh.wasm`
218,887 → 219,637, the staging tree 727,600 → 729,099 of 1 MiB. **`PROC_ABI` is
still 11**: argv is still argv and the host still receives
`(path, image, ProcMeta)`, so nothing a binary observes moved. A diff touching
`sysabi.h` reads like a bump and this one is not.

**This reverses "`#!` is settled rather than deferred" below**, which argued
that "there is no place to put an interpreter lookup that would not be the
kernel reading file contents to decide what a program is". The premise is right
and the conclusion does not follow: the kernel already reads file contents to
decide what a program is — that is the whole of `exec_meta`, which walks the
section list looking for `braam`. Reading eight bytes further into the same
image, in the same function, against the same contract, adds no capability the
kernel did not have. What the old argument was really protecting was that `exec`
should not *guess*, and that survives intact: a text file with no `#!` is still
`Err(Invalid)`, and there is no sniffing of any kind beyond the two literal
prefixes.

**Three bounds are what make it a rule rather than a search.** The interpreter
is **absolute**, because the `/` is required after `#!` — there is no PATH here
and one directory is not a search path, so a relative interpreter has nothing to
resolve against and is refused rather than guessed at. The lookup is **one level
deep**, which is v7's and Linux's ENOEXEC answer: an interpreter that is itself
a script is `Err(Invalid)`. And the first line must end within
`PROC_SHEBANG_MAX`, so a file is decided by its head rather than by a scan. That
cap is 128 and is `static_assert`ed against `SYS_CHUNK`, because `test -x`
decides from a single `read_chunk`: the two answers can only agree while a whole
`#!` line fits in one. Tying them at the constant is what keeps a later raise
from silently splitting `exec` and `-x` apart.

**`exec_resolve` became a two-round loop rather than a recursive call.** A
nested `Task` would be a second coroutine frame and a second allocation on the
path every command takes, and "one level only" written as a loop bound is a fact
the compiler enforces rather than a convention a later edit can drift past.

**`Executable::path` is the interpreter's now, and it had to be.** It is the key
the host caches the compiled `Module` under and it is what the no-worker backoff
re-reads on each retry; keying either on the script would hand the host a text
file. The cost is that `braam: /bin/sh: crashed` names the interpreter rather
than the script — which is truthful, since the interpreter is what crashed.
Nothing a user navigates by moved: `ps`, `jobs`, `/proc` and `kill %n` all read
the scheduler's job name, which `sched_spawn` takes from the caller's word, so
they still say `./script.sh`. A second `String` holding a display name would
have bought three error messages and cost a field on every `Executable`, one of
which lives on init's coroutine frame.

**A missing interpreter is 126, not 127.** Letting the interpreter's
`Err(NotFound)` through would print `braam: ./s.sh: not found` for a script that
plainly exists, and would spend the one status that means "the command word
names nothing" on a file that does name something. So the second round's
`NotFound` alone collapses to `Invalid` — "not executable" — and every other
error is let through unchanged, which keeps the genuinely useful one: a stale
interpreter still reports `Err(Unsupported)` and prints
`built for another process ABI`, naming the repair. A new `Error` value was the
alternative and is far too expensive for a diagnostic string: `Error` crosses
the wire as a negated `i32` in every reply, so it would mean a `PROC_ABI` bump,
`web/` changes and a System_Calls.md edit.

**A script costs one process, not two.** The resolution happens before any
process exists, so `SYS_PROC_DEPTH` and `SYS_CHILD_MAX` count the interpreter
exactly as they counted a binary, and every script shares `/bin/sh`'s
already-compiled `Module` — the second and later scripts pay the instantiation
and the worker, not the compile.

**The cost, stated rather than hidden: a script's image is read twice.**
`exec_resolve` reads the whole file through the ordinary VFS before it can
discover it is not a module, keeps 128 bytes of it and throws the rest away; the
interpreter then reads the same file again. Avoiding it means splitting
`read_file` into a probe-then-rest read, which is a worse VFS for files that are
small by nature. The `#!` rule lives in `sysabi.h` beside `PROC_SECTION` and
`PROC_MAGIC` for the same reason those are there — it is the exec contract — and
being a header is what lets `src/cmd/sh/condrun.cpp` answer `-x` from the one
statement of it, since `braam_sh` cannot reach `src/user/`. Being `inline` costs
nothing: it is expanded into its single call site in each of the three binaries
that call it and emitted out of line in none of them.

---

## Seconds, and `-m` for the machine's unit

`sleep`, `timeout` and `watch -n` took **milliseconds**, and `vmstat` took
**seconds**. Every duration a user types is now seconds, and all four accept
`-m` to mean the number is milliseconds instead. `sleep 5` waits five seconds,
`sleep -m 5` returns almost at once.

**Why the divergence was there, and why a flag keeps it.** The earlier note
below — "`sleep` takes milliseconds; there is no float parser, the scheduler is
a millisecond machine, and the smoke test needs an exact number to assert
`tick()`'s return value against" — was three arguments, and only the third is
still load-bearing. The float parser is still absent and the argument is still
`parse_u32`, so seconds cost nothing to parse; the scheduler being a millisecond
machine is an implementation fact that `sleep_for(secs * 1000)` hides in one
multiply. But the test harness really does own the clock and really does assert
the exact millisecond `tick()` reports, and a whole second is a clumsy unit for
a virtual clock whose ticks are hand-picked ten milliseconds apart. `-m` is what
preserves that: `test/run.mjs` spells every one of its delays with it, so not a
single tick value or hand-written `now` in that file moved when the default
changed. A seconds-only change would have had to retime four blocks and rewrite
the `[30, 20, -1]` assertion that has stood since M1.

The alternative was a fractional parser — `sleep 0.5` — which reads better than
`sleep -m 500` and was rejected on cost: it is real parsing code in four
binaries that each carry their own copy of `text.cpp`, to express something no
caller in the tree wants. `sleep 0.5` is a usage error, not a rounding.

**This supersedes two paragraphs further down.** vmstat's *"Seconds, which makes
it the one time argument here that is not milliseconds"* now describes the rule
rather than the exception, and its first clause — that a rate per second wants
an interval in seconds — is the argument that won generally. M3's *"`sleep`
takes milliseconds… the divergence from POSIX lives in the usage string"* is
retired; the usage string reads `usage: sleep [-m] <seconds>` and there is no
divergence left to record.

**Nothing below the command line moved.** `Sys::Sleep`'s payload is still
milliseconds (§4.3) and `sleep_for(u32 ms)` is still the SDK's call, so this is
four argument parsers and four usage strings. `vmstat` needed slightly more care
than the others: its columns are rates *per second* whatever `-m` says, and its
"a count without an interval paces itself" default has to stay a whole second
under `-m` too, since a one-millisecond interval would measure nothing but
vmstat.

`watch`'s ad-hoc `args[1] == "-n"` became a leading-option loop in vmstat's
style, so `-m` and `-n` compose in either order. The three ordinary programs cap
a seconds argument at 4,294,967 — as many as convert to milliseconds inside a
`u32` — and refuse anything above it rather than wrapping; under `-m` the
parser's own range is the cap.

**What the tests gained.** `sleep 5` asserting `tick()` returns 5000 is the
whole seconds path in one line, and `watch -n 100` asserting an interval of
100000 is the same check for the one program whose conversion is not at the top
of `proc_main`. The refusals — `sleep 0.5`, `sleep 4294968`, `sleep -m` with no
number, `watch -n 4294968` — are cheap and pin the boundary. Three copies of
`/share/help` are `wc`'d by an unrelated case, so lengthening four help lines
moved a byte count there — the exact number is worth keeping over a looser
assertion precisely because it notices.

## The wheel is a keystroke

Scrollback has been reachable only from the keyboard since it arrived, and the
gesture everyone tries first — a wheel, a two-finger swipe — did nothing at all.
Closing that cost **no import, no export, no message the kernel understands and
no mouse in the ABI**: the page turns a wheel notch into the keystrokes the
chord is already made of.

**Why not a tenth export.** `scroll(lines)` was the obvious shape and would have
been exact: one call, one repaint, no chord to invent. It was rejected because
it puts a pointer gesture into the ABI, which §3.5 spends a paragraph saying it
does not have — the selection, the key bar, the paste and the soft keyboard are
all page-side, and each of them arrives as an ordinary `{code, mods}` or as
nothing at all. The wheel is not a different kind of input; it is a way of
asking for the scroll chord, exactly as the key bar is a way of asking for
`Esc`. So the page sends keys, the kernel cannot tell a wheel-born Shift+Up from
a typed one, and the export list stays at nine.

**The chord grew a row.** Half a screen a notch is unusable, so `console_pump`
now takes Shift+Up and Shift+Down beside Shift+PageUp and Shift+PageDown, one
row instead of `rows / 2`. Nothing else in the tree binds Shift with an arrow,
so no claimant lost a key it was reading, and the keyboard gains a
line-at-a-time scroll it did not have — the feature is worth having on its own,
which is part of the argument for this shape over an export.

**A claimed screen keeps the fall-through, and `less` gets the wheel free.** The
chord is not the pump's while a program holds the screen, so the synthetic key
is delivered raw like any other — and `less` switches on `k.code` without
looking at the modifier, so a wheel over the pager pages it a line at a time
with nothing written for it. `edit` moves its cursor a row, which is the same
bargain and harmless. Swallowing the key while the screen is claimed was the
alternative: more predictable, and it makes the wheel dead inside the two
programs where scrolling is the whole point.

**Pixels cross, rows are computed in the worker.** `deltaY` is device pixels,
lines or pages depending on the platform and the input device, and only the
worker knows `cellH` — it owns the font, which is why it also owns the geometry
(`fit`). So `web/braam.js` posts the delta in device pixels, as a selection drag
already does, and `web/worker.js` divides. The leftover fraction is **carried
between events**: a trackpad delivers a few pixels at a time, and truncating
each one independently means a slow swipe scrolls nothing ever. The residue is
zeroed when the direction reverses (a reversal continues nothing) and in `fit`,
where the row height it is a fraction of changes underneath it. A page-mode
delta is passed to the half-screen chord instead of being converted, since that
is what it already means.

**Fed like a paste.** The run goes through `key()` and stops when it returns
false, which is the back-pressure the ring's return value exists for (§3.5), and
it is clamped to a ring's worth so a fling cannot outrun it. One `pump()` for
the whole run, so *n* rows cost one composition pass and one `host_present`
rather than *n* of each. It deliberately does not touch `key_at` or the sample
array: those measure what a *keystroke* costs, and a wheel event is not one.

**`Ctrl`+wheel is the browser's.** A trackpad pinch arrives as a wheel with
`ctrlKey` set, and zoom belongs to the user. Everything else over the canvas is
prevented and taken, which is the same claim `touch-action: none` already makes
for a drag — an embedding page does not scroll under a terminal the pointer is
over. Touch is still not covered: a one-finger drag selects, and a two-finger
scroll gesture would need a decision about which one wins.

## The environment crosses a spawn

`export` set a bit nothing read. §4.5 listed it as one of six things v7 has that
this cannot, and the reason given was true: `Sys::Spawn` carried three
descriptors, an argv and a cwd, and there was nowhere to put a variable. Closing
that gap cost `PROC_ABI` 10 → 11 and **no new operation, no new import or
export, and not one line of `web/proc.js`.**

**One encoding, two blobs, no length word.** An environment is a word list in
exactly the format `argv_encode` already wrote, its words `NAME=value`, so there
is one codec rather than two and `env` needs no parser of its own. The two blobs
sit back to back — in `_start`'s payload and after `Sys::Spawn`'s three
descriptor words — and the join is found by walking the first (`argv_bytes`)
rather than by a length word in front. A length word was the obvious alternative
and is worse: it would sit where `argc` sits, so `argv_count(payload, len)` and
the existing assertion in `test_sysabi.cpp` would both have had to learn about
it. Walking is free where it matters, because both readers already walk the argv
blob to the end. `argv_bytes` reports 0 for anything it cannot walk, and that is
unambiguous: the smallest well-formed blob is the four bytes of an empty word
list, so "malformed" and "empty" are told apart and the kernel refuses rather
than entering a child with rubbish.

**The kernel holds it, beside the cwd, and inheritance is the default.** The
alternative was to hold nothing and have `spawn()` re-serialise the caller's
environment on every call, which keeps the kernel stateless and puts the truth
on the wire every time. It was rejected for what it does to a program that
starts a program: `timeout 5 env` and `watch ls` would each have had to ask for
their own environment and hand it on, and one that forgot would silently strip
it. So `Proc` gains a `String env`, `Sys::Spawn`'s op-word argument — unused
until now — gains bit 0 meaning "an env blob follows", and a spawn that says
nothing hands the child the caller's. The common case puts zero bytes on the
wire. `SYS_ENV_MAX` bounds it, because a child hands its own on and an unbounded
one would grow down a chain of them.

**There is no `setenv`, and a `Sys::Env` would have failed §4.3's own rule.**
Every operation has a caller in `src/cmd/`, and this one would have had none:
the shell keeps its variable table in its own memory and builds the blob afresh
at each spawn, so it never needs to ask the kernel to change anything. A
process's environment is therefore fixed at spawn — stronger than v7, which is a
fair trade for an operation nothing wanted. It also keeps the synchronous half
of the ABI closed: `getenv` would have had to be asynchronous, since the sync
half is answered inside the worker with no kernel to ask, and a `co_await` to
read a variable is a bad shape.

**The runtime walks the environment rather than indexing it, and `hog` is why.**
The first version built a `Vec<Str>` in `_start` beside the argv one. That is
one heap block in every process that will never use it, and it broke `hog` —
which allocates until the heap refuses, hands one 64 KiB span back, and reports.
The failure was not the sixteen bytes: it was the size *class*. `_alloc(4)` for
the reply the kernel writes back used to land in the span the argv block had
already seeded, and a bigger `_start` block moved that block to another class,
leaving no span able to serve four bytes. Walking on demand removes the
allocation, which is the right answer anyway — `Rt` holds a pointer and a
length, `proc_env` is a linear scan over a handful of words, and a program that
never asks pays nothing. `hog` now hands back two spans instead of one, and says
so: reporting needs coroutine frames *and* a reply block, and those are
different size classes once nothing else is left. That fragility was always
there; the environment is only what exposed it.

**`x=1 prog` finally goes somewhere.** The note under M9 explains why an
assignment prefix on a program stage used to be expanded and then dropped —
applying it to the shell would have leaked it, and there was nowhere else. Now
it goes into that child's environment and nowhere else, which is v7's semantics
exactly. A prefix *replaces* an exported variable of the same name in the words
handed over rather than arriving beside it, because a lookup takes the first
match and a second word would never be reached. Prefixes on builtin and function
stages keep the apply-and-restore they had: those run in the shell's own turn
and have no spawn to ride.

**A nested shell reads its environment back into its table.** `var_init` seeds
from `proc_env_at`, marking each entry exported, so
`export a=1; sh -c 'echo $a'` works and the environment is not swallowed by the
one program most likely to be in the middle of a chain. Without it the feature
would have reached every program *except* another `/bin/sh`.

**Init gives the shell `HOME` and `SHELL` and nothing else.** `PATH` is absent
because there is no search path to describe — a command word resolves as
function, then builtin, then `/bin` — and a `PATH` that did not steer resolution
would be a lie a script could believe. `TERM` is absent because §2.3's terminal
is a cell grid with no escape sequences and no terminfo entry that could
describe it; `Sys::Tty` is how a program asks about the terminal, and a
`COLUMNS` would be a copy taken at spawn that the first resize made wrong. `cd`
with no argument now reads `$HOME`, falling back to the literal `/home`, which
is what makes the one variable init plants mean something rather than being
decoration.

**`/bin/env` is a program, not a builtin.** Neither clause of §4's rule fits it:
it does not touch the shell's own state, and its whole cost is not the spawn —
it *makes* a spawn, which is the thing a builtin cannot do. It is the worked
example for both halves of the new `spawn` signature, and it is what makes the
feature testable end to end from `run.mjs`.

---

## src/sh became src/cmd/sh

The shell is a program (§4), and it was the only program whose code did not live
in `src/cmd/`. `src/sh/` was a top-level tree beside `src/fs/`, `src/svc/` and
`src/user/` — the layers *below* userland — and being there said the shell was
one of those, which stopped being true when it became a binary. It is
`src/cmd/sh/` now, a directory beside the thirty-four one-file programs, and its
entry point is `main.cpp` inside it rather than `src/cmd/sh.cpp` outside.
Nothing else in the tree gains a directory by doing this: a program that needs
one is what the shell is, and `braam_add_program` already takes a source path
rather than assuming `<name>.cpp`.

**The build got shorter, not longer.** `add_subdirectory(src/sh)` left the
top-level `CMakeLists.txt` and became `add_subdirectory(sh)` at the top of
`src/cmd/`'s own, so the shell's library is declared where its binary is.
`/bin/test` names `sh/cond.cpp sh/condrun.cpp` instead of `../sh/cond.cpp` — the
same two files, no longer reached for through a parent.

**`#include "sh/x.h"` became `#include "cmd/sh/x.h"`**, which is the only cost.
Includes are root-relative from `src/`, so the builtins and `src/cmd/test.cpp`
and the five unit tests that reach into the shell all name a directory deeper
than before. Sibling includes inside the shell are unchanged, because a quoted
include finds a sibling first — which is why `main.cpp` says `"shell.h"` where
`src/cmd/sh.cpp` had said `"sh/shell.h"`.

`sh.wasm` is 215,270 bytes before and after, to the byte, and the three CTest
cases pass unchanged. Notes above this one name the old paths where they are
describing what happened at the time; their file *links* were repointed, so none
is dead.

---

## The shell's plan is deleted, and what only it held is here

Last of the stages in `src/sh/TODO.md`, and the one that deletes it. Ten stages
had landed and each has a note of its own below, so what the plan still held was
a to-do list with nothing to do plus four things written nowhere else: **the
grammar and the expansion order**, **the six impossibilities**, **the purity
boundary**, and **the size trajectory**. The first two are now Concept.md §4.5,
which the specification had been missing entirely; the last two are here. **No
behaviour changed**: `sh.wasm` went 215,238 → 215,270, and all thirty-two of
those bytes are one usage string — `unset` had taken `-f` since the functions
stage and its one-line help had never said so. The staging tree is 704,061 of
1,048,576 (67%), 277 KiB as `rootfs.zip`; `kernel.wasm` is 148,604 of 262,144,
byte for byte what S7 left; `PROC_ABI` is still 10.

**Cite the stages by name, not by ordinal.** The ten notes below number
themselves *first* to *tenth*, and those ordinals are **one ahead of the plan's
S-numbers** from the beginning: *"The lexer stopped removing quotes"* took a
note without taking a stage number, so the plan's S1 is the second note, its S7
is *"The ABI changed, once"* and its S9 is *"The shell got a front door"*. The
S-numbers are kept as citations the way the worker plan's T-numbers were —
CLAUDE.md and the commit messages say "S7" and mean the `Sys::Dup` stage — but a
heading is the unambiguous name and this note is the last place the two schemes
have to be reconciled.

**The purity boundary is a standing rule, not a stage's tactic.** `parse.cpp`,
`tokenize.cpp`, `expand.cpp`, `match.cpp` and `cond.cpp` touch nothing but
`Str`, `String` and `Vec`, and `test/CMakeLists.txt` compiling them into
`tests.wasm` **is** the enforcement: a syscall in any of the five is a link
error, not a review comment. It cost real design twice and both times the answer
was a second file rather than a weakened rule — the directory walk could not go
in `expand.cpp` and became `glob.cpp`, and `test`'s file primaries could not go
in `cond.cpp` and became `condrun.cpp`, which answers the probes the pure
evaluator names. That is what lets `test_parse.cpp`'s one-string `shape()`
compare check a whole parse, and `test_cond.cpp` check a whole expression
grammar against a table of canned answers, with no kernel in the loop. Anything
new that reaches `exec_node` goes the same way.

**The trajectory, since it is the only place the arithmetic is recorded.** Per
stage, in bytes of `sh.wasm`: 20,807, 10,644, 12,678, 14,607, 8,699, 13,897,
20,650, 26,270, 3,501. About twenty bytes of wasm per line of C++, which the
arena stages came in under and the coroutine ones over, and the estimate was
calibrated against the binary after S1 rather than guessed per stage — which is
why nine stages landed inside a budget that never moved. S8 also added
`/bin/test`, 19,246 of the tree on its own, because a builtin of the second kind
keeps its file.

**The integration cases are the last thing the per-stage tests could not do.**
Every block in `run.mjs` before this one stays inside one stage, so nothing
checked that the features *compose*. What checks it now is a planted script that
does something real — a function, an EXIT trap, a glob walked by `for`, a `case`
that skips an arm, a `$( )` with a redirection inside it, a here-document and a
nonzero exit — asserted whole, and it is the same script Programming_Manual.md
prints, so the manual's example cannot rot. Beside it are the seams: a function
body that globs with the *call* redirected, `case` inside `while` inside a
function, a `for` over a substitution under `set -e`, a builtin at each end of a
pipeline with a program between, `set -x` tracing inside a compound, and a
construct through `sh -c`. All of them passed the first time they were run,
which is the finding — the stages were composable as they landed, and this
proves rather than fixes it.

**Three instances at the peak, however many turns the loop takes.** The script's
cost is asserted rather than described: this shell, the script's own, and
whichever program is running. A shell that leaked an instance per turn, or
spawned the loop body in parallel, would fail that line — and it is the
counterpart to S8's assertion that a loop of builtins hires no worker at all.

---

## The shell got a front door

Tenth of the stages in `src/sh/TODO.md`: `sh <file> args`, `sh -c cmd`,
`-e -x -u` at startup, `$0` and the positional parameters a shell begins with.
`sh.wasm` went from 211,737 to 215,238 bytes and the staging tree from 700,528
to 704,027 against an unchanged 1 MiB budget — 3,501 bytes over about 150 lines,
the cheapest stage of the ten. **`kernel.wasm` did not move**: 148,604 of
262,144, byte for byte what S7 left. The syscall ABI did not change and
`PROC_ABI` is still 10.

**A script file is read whole and parsed once, because that is what `.` already
did.** The alternative was the loop `sh -s` runs — a `LineReader` over the file,
accumulating until `line_incomplete` says the construct closed — and it is what
v7 does, executing as it parses. Two things decided against it. The smaller is
duplication: `sh_source` already reads a file with `read_file` and hands the
text to one parse, so `sh file` and `. file` are now the same mechanism and
cannot drift apart. The larger is stdin. A shell reading its script off stdin
has that stream open for two purposes at once, which is exactly the fidelity
loss S8 recorded for `read`; a script named on the command line has no such
conflict, and `while read l; do …; done` inside one reads the shell's actual
stdin. The cost is stated rather than hidden: a syntax error anywhere in the
file means none of the file runs, where v7 would have run everything above it.

**`sh -c cmd name args` names `$0` itself, and the arithmetic is v7's.** It is
tempting to make every word after the command string a positional parameter and
leave `$0` as `sh`, and that is wrong in the way that only shows up later:
`options()` and `main()` in the v7 port compute `cmdadr = dolv[0]` uniformly
across all three entry points, so `$0` is the script for a file, the first
operand for `-c`, and argv[0] otherwise. Following it means
`sh -c 'echo $0 $1' a b` prints `a b` here as everywhere else, and `sh -s a b`
puts `a` at `$1` rather than eating it. The implementation is `OptParse` plus
nine lines, since `var_init` plants `$0` and `args_set` carries it over — the
shell's own state was already the right shape and no new storage was added.

**`${x?}` now ends a non-interactive shell, which discharges a deferral.** *"The
shell got variables"* below records that `${x?}` "aborts the line rather than
the shell … because script-entry policy belongs to the stage that adds `sh file`
and `sh -c`". This is that stage, and v7's answer is taken: a script stops where
the parameter is missing rather than running on past a hole, while at a prompt
only the line is abandoned. It reuses `Flow::Exit` and so adds no exit path,
exactly as `set -e` did. Folding the decision in also collapsed four hand-copied
expansion-failure epilogues in [job.cpp](../../src/cmd/sh/job.cpp) into one
`expand_failed`, which is where the `^C`-is-130 distinction now lives once
instead of four times.

**`#!` is settled rather than deferred.** `exec_meta` requires `\0asm` and a
`braam` custom section carrying `PROC_ABI`, so a text file can never be handed
to `Sys::Spawn`; there is no place to put an interpreter lookup that would not
be the kernel reading file contents to decide what a program is. `sh file` and
`sh < file` are the whole of it, and `./script.sh` will not arrive later —
**which it since has: "`#!` after all" above reverses this, and says why.**

---

## The rule grew a second clause

Ninth of the stages in `src/sh/TODO.md`: `test`, `[`, `:`, `read`, `wait`,
`trap` and `set -e -x -u`, plus `echo`, `true` and `false`, which S8 as written
had ruled out. `sh.wasm` went from 185,467 to 211,737 bytes and the staging tree
from 654,945 to 700,528 against an unchanged 1 MiB budget — the extra being
`/bin/test`, a new binary at 19,246. **`kernel.wasm` did not move**: 148,604 of
262,144, byte for byte what S7 left. The syscall ABI did not change either, and
`PROC_ABI` is still 10.

**`test` is a builtin, and the reason is not the one TODO.md wrote down.** The
plan proposed "the shell's own state **or the condition of a loop**", drawn
narrowly enough to admit `test`, `[` and `:` and to exclude `echo` by name. That
wording does not survive the first request to add `echo`, and it was never the
real criterion: what makes `test` worth having inside the process is that a
program costs an instantiation and a worker — roughly a millisecond, §4.4 — and
`test` is two hundred lines of pure logic. The cost of running it *is* the
spawn. So the second clause is that, and it admits exactly the six commands
small enough for the statement to be true: `test`, `[`, `:`, `echo`, `true`,
`false`. A `while [ … ]; do echo …; done` paid two workers a turn and now pays
none, which `run.mjs` asserts by counting links across a five-turn loop.

**The file in `/bin` stays, and that is the other half of the amendment.** A
builtin of the first kind has no file and never will — there is nothing for
`/bin/cd` to do. A builtin of the second kind is a *shortcut*, so its file is
still the thing anything spawning by path gets: `/bin/test` now exists for a
future `find -exec`, and `/bin/echo`, `/bin/true` and `/bin/false` were not
deleted. TODO.md argued against shipping `/bin/test` as duplication for nothing;
the answer is that the duplication is the point, since the alternative is that
adding `find` later means adding `test` back. `help` prints each name once — the
builtin's line, because that is what typing the name runs.

**The expression is pure and the probing is not, and they had to be two files.**
v7's `test` is five mutually recursive functions, and three of its primaries —
`-r -w -x -f -d -s -t` — have to ask the filesystem. Making the recursion
coroutines would have cost a frame per nesting level against an allocator whose
top size class is 512 bytes. Instead [cond.cpp](../../src/cmd/sh/cond.cpp) walks
the expression twice: once naming every file primary, then, with the answers in
hand, once more evaluating. It works because **v7's `-a` and `-o` are the
bitwise `&` and `|`** rather than the short-circuiting pair, so both sides of
every operator always evaluate and the two walks consume the same tokens in the
same order. The indices line up by construction, not by agreement between two
scanners — which is why collecting and evaluating are one function with a flag
rather than two. `cond.cpp` reaches no syscall, so it joins `parse.cpp` and
`expand.cpp` on the purity boundary and `test_cond.cpp` checks the whole grammar
against a table of answers. [condrun.cpp](../../src/cmd/sh/condrun.cpp) is what
goes and looks; it touches no shell state, which is what lets `src/cmd/test.cpp`
build the binary out of the same two files rather than link `braam_sh`.

**Two primaries answer out of the gaps rather than out of a mode word.** There
are no file permissions and OPFS stores none, so `-r` is "it exists": everything
readable that is there. `-w` is v7's `tio(a, 1)` unchanged — open it for writing
and close it again — which asks the one question that has an answer here, since
`vfs_open` refuses `O_WRITE` on a read-only mount. It also inherits v7's answer
for a directory, which is *false*, because opening one for writing fails there
too. TODO.md proposed deriving it from `/proc/mounts` instead; the open probe is
shorter, exact, and needs no path normalisation. `-x` is true for a file whose
first four bytes are `\0asm`, because that is what `exec_meta` requires and
therefore what executable means here.

**`read` is where the shell reads past what it was asked for.** `Sys::Read`
carries no length — the reply is a whole chunk, whatever the writer wrote — so
there is no reading a line and leaving the rest. A file-scope `String` holds
what followed the newline, keyed by the descriptor it came off, and a `read` on
a different one drops it. Without that, `while read l; do …; done < file` would
see one line and stop, since a three-line file arrives in a single chunk. The
cost is that `sh -s` reading a script off stdin has its own `LineReader`
buffering separately, so a `read` inside such a script sees a different position
in the same stream; that is a real fidelity loss and it is the price of a
length-free read.

Interactively `read` needs nothing at all, which is worth recording because it
looks like it should. `exec_pipeline` gives the keyboard back before any builtin
runs and takes it again after, so for exactly the window a builtin is awake the
console pump has no raw claimant and is cooking lines into this shell's stdin —
and doing the echo.

**`wait` puts the job in front, and v7's does not.** There are no signals, so
being in the foreground set is the only way a `^C` can reach anything; a `wait`
that did not would park the shell on a job with no way out. That makes `wait %n`
the same call `fg %n` makes, minus the echo of the command line, and
`builtin_fg` is now the thin one.

**`trap` has two signals because there are two things that can happen.**
`trap … 0` fires from the `shell()` funnel, whatever ended the loop. `trap … 2`
fires from the one place a `^C` becomes a status — `exec_pipeline`'s epilogue,
where 130 already becomes `Flow::Interrupt` — and only in an interactive shell,
where the interrupt went to the stages and this process was never cancelled. In
a script the process itself is cancelled and `CancelState::cancelled` is sticky,
so every await from that point answers `Err(Cancelled)` and a handler could
neither spawn nor write. It is accepted rather than worked around, and
`trap '' 2` is **refused outright** for the same reason: nothing can decline a
cancellation that has already been delivered, and accepting the syntax while
ignoring it would be worse than saying so. A trap is *taken* rather than read
before it runs, so an action cannot fire itself and a `trap` inside one replaces
it.

**`set -e` needed a counter, not a flag.** The rule is not "a nonzero status
ends the shell" but "a nonzero status that nobody asked about ends the shell",
and the four places that ask are an `if` condition, a loop condition, `!`'s
operand and every element of an `&&`/`||` chain but the last. `Ctx` gained a
`cond` depth incremented around exactly those, and the test itself sits at the
end of `exec_node`, where every node's status passes through once. It reuses
`Flow::Exit`, so `set -e` adds no exit path — `exit` already owned that one.

**`set -u` is a field on `Vars`, not a callback.** A lookup cannot tell a bare
`$x` from the one inside `${x-y}`: only the walk knows that the operators asked
first and are exempt. Making it data keeps
[expand.cpp](../../src/cmd/sh/expand.cpp) pure, so the rule is checked in
`test_expand.cpp` with the rest of the expander. The check went into `named` and
`positional` rather than `emit`, because a bare `$x` reaches those two straight
from `dollar()`.

**One gap got wider, and it is the price of the second clause.**
`while true; do echo x; done` used to be interruptible, because `true` and
`echo` were programs and the shell armed them with `Sys::Fg`. Both are builtins
now, so that loop has no child in the foreground set, no `^C` reaches anything,
and `rt.h` is explicit that nothing cancels from inside a process. A loop with a
program still in it — `while true; do sleep 100; done` — is interrupted in one
press, and the escape from the other kind is still killing the shell, which init
replaces. The alternative was leaving `true` a binary and paying a worker a turn
for the one shape of loop that most needs not to; the gap is recorded rather
than traded for.

`$-` came with the letters, and cost `-` its place as an ordinary byte: it is a
special parameter now, so `$-` is the options and no longer a literal `$-`. The
flags are one word, which is what makes them one more field on `( … )`'s
checkpoint — TODO.md's subshell row promised `(set -e; …)` isolation and this is
what pays it.

---

## The ABI changed, once

Eighth of the stages in `src/sh/TODO.md`: here-documents, `>&`, `exec`
redirections, inherited base stdio and `( … )` as a state checkpoint. `sh.wasm`
went from 164,817 to 185,467 bytes and the staging tree from 634,295 to 654,945
against an unchanged 1 MiB budget. **`kernel.wasm` moved for the first time in
this plan** — 148,483 to 148,604 of 262,144 — because this is the stage that had
to add an operation.

**`Sys::Dup`, and it was `exec` rather than `2>&1` that forced it.** The plan of
record promised no syscall changes, and that promise held for seven stages. What
breaks it is not the obvious thing: `cmd 2>&1` already worked, since
`ChildIo{.err = SYS_STDOUT}` is resolved by the kernel against the parent's own
stream, and a builtin merging two streams is one integer assignment. It is
`exec >file`. `Spawn` **moves** a descriptor — that is what closes a pipe's
write end and gives the reader its end of input — so the shell's own base
descriptor would be gone into the first child that ran. Nothing in a
thirty-seven-operation table can stand in for a duplicate.

It cost less than the promise suggested. Ops 25–31 were free before
`Sleep = 32`, so `Dup` renumbers nothing; a new operation is an enum value on
each side rather than a new import, so `smoke`'s exact per-binary import surface
stayed green untouched, which is the whole point of multiplexing `sys_async`.
`PROC_ABI` went 9 → 10 and `exec_meta` refuses anything older, so a stale binary
is a diagnostic.

**One rule had to be relaxed, and it turned out to be a rule that never said
what it meant.** `Spawn` refused to move a handle with `refs > 1`, and its
comment said "nothing this process is inside a syscall on". Those are different
things the moment a second *descriptor* can exist: every dup'd fd would have
been unspawnable. The condition it always meant is recorded on the handle as
`busy_r`/`busy_w`, so the test moved there and the comment finally describes the
code. `Handle` also gained an `fds` count beside `refs`, because closing one of
two descriptors must shut nothing.

**The here-doc cliff did not exist.** TODO.md specified a `/tmp` file with a
unique name, a path tracked on `Run`, a field on `JobEntry` and a reap in
`jobs_report`, all to avoid "a hard cliff at 8 × 512 = 4,096 bytes". That is the
`PIPE_SLOTS` misreading corrected at S5, a second time: `pipe_write` puts a
whole `Str` into **one** slot, and a single `Sys::Write` carries up to
`SYS_STAGE_MAX` = 1 MB. So a here-doc is a pipe — write the body, close the
write end, hand the read end over — and every piece of the temp-file machinery
was deleted before it was written. Two stages in a row have now been simplified
by reading `PIPE_SLOTS` correctly; the comment on it says "chunks, not bytes"
and means writes.

**The body is collected at parse time and nothing else may be.**
`line_incomplete` re-parses the whole accumulated text on every line the user
adds, and `run_line` parses it again — so collection has to be a pure function
of the text. Creating the pipe in the parser would have made one per keystroke
line. The body is stored as the redirection's *target*, because `Tree`'s target
table is an arbitrary byte store rather than a filename table, and the pipe is
made where the other descriptors are.

Two rules fall out of the accumulator's shape rather than from any
specification. The text never ends in a newline — `shell.cpp` joins lines with
one — so a delimiter at the very end of the text must count as terminated, or
the prompt asks forever. And an unterminated body has to fail with `more` set,
which is what puts `PS2` on the screen.

**A here-doc expands but does not unquote**, and that needed no change to the
expander. The body is escaped — every `'`, `"` and `\` gets a backslash — before
`expand_one` sees it, so `$` acts and the quotes come back out as themselves. A
quoted delimiter skips even that.

**Pipes now run before redirections, and that is a fix, not a rearrangement.**
`ls /nope 2>&1 | wc` must send stderr into the pipe, and `2>&1` copies whatever
`out` holds *at that moment* — which was still the terminal, because the pipe
was attached afterwards. The order is now pipes, redirections, capture.
`a > f | b` is unchanged: the redirection displaces the write end and closes it,
so `b` still gets a clean end of input.

**`ours()` is the whole of what changed for base stdio.** It decided what a
stage may close by `fd >= SYS_FD_MIN`, and a base descriptor of 7 breaks that
assumption everywhere at once — the replace-on-second-redirection, the
per-builtin close, the final sweep. It takes the base now, and the three
hard-wired constants in the pipe and capture tests became comparisons against
it. That is the entire mechanism, and it lifts S6's function refusal for free:
`f | wc` and `f > log` work because `call_func` already ran the body on the
caller's `Ctx`, which now carries the stage's descriptors.

**A compound may be redirected but still not piped**, and the split is where the
grammar is. A compound's own redirections ride on a synthetic `Command` named by
the node's spare `d` field, so `{ a; } > f` and `for … done > f` needed a parser
arm and an `exec_node` prologue. Piping one is a different change: a `Pipe`
node's stages are *commands*, and making them nodes is its own work. The refusal
narrowed rather than went.

**`( … )` restores everything but the job table.** cwd through two syscalls, the
positional parameters through the `args_swap` `call_func` already used, the
variables through a new `vars_swap`, and the function table through a snapshot
that holds each body's tree — a redefinition mutates an entry in place rather
than growing the vector, so a mark-and-truncate would have missed it. `(exit 3)`
reports 3 and the shell lives, caught at the boundary the way `call_func`
catches `Flow::Return`.

The job table is deliberately shared: the children are this same process's and
must still be reaped, so restoring it would leak kernel child slots and break
`jobs_report`. And `exec` *is* in the checkpoint, which is not obvious until you
look for the way back — there is no `/dev/tty` and no way to name the stream the
shell was handed, so `exec >file` at the top level is irreversible. Inside
`( … )` it is not, and that is the only place it can be tested.

**`>&-` is refused rather than faked.** Saying "this stream is closed" needs a
value the Spawn payload does not have and a null *sink* the kernel does not have
— `null_source` exists for input only. A gap to record.

**What the tests prove separately.** `test_parse.cpp` renders a here-doc's
collected body as one string compare, including two on one line taken in order,
`<<-`'s tabs, a quoted delimiter, and both unterminated forms asking for more.
`test/run.mjs` has what the tree cannot show: a here-doc typed over three lines
with `PS2` on the screen, `2>&1` reaching a pipe and a file, a compound and a
function redirected, and a subshell putting back a cwd, a variable, the
parameters, a function and a redefinition.

---

## A line could outlive itself

Seventh of the stages in `src/sh/TODO.md`: `name() { … }`, `.`, `eval`, `return`
and `unset -f`. `sh.wasm` went from 150,920 to 164,817 bytes and the staging
tree from 620,398 to 634,295 against an unchanged 1 MiB budget. **`kernel.wasm`
did not move and the syscall ABI did not change.**

**This is the first stage with no reference to port.** The V7 shell has no
functions and no `return` — both arrived with SVR2, five years later — so where
every stage before this one could be checked against
`/Users/vak/Project/Besm-6/v7besm/cmd/sh/`, this one is POSIX by choice. What v7
*does* have is `.` and `eval`, and its shape was worth keeping: one `execexp()`
that pushes an input source and runs it in the current everything. `run_text` is
that function, and `.` and `eval` differ only in where the text comes from.

**A function body outlives its line, and that is the whole of the difficulty.**
Every stage until now could assume a parse dies with the text it came from. The
tree is already heap-allocated — S3 put it there — so the change is a count, not
an allocation. What it could *not* be is a destructor: `Tree`'s move constructor
is implicit, `test_parse.cpp` asserts a frozen tree still moves, and declaring
`~Tree` would delete that move and fall back to the deleted copy. So the count
is a plain `mutable u32 refs` with `tree_hold`/`tree_release` beside it, and a
stack `Tree` — `line_incomplete`, the tests — never touches either.

**The second half of that lifetime is the text, and it is easy to miss.**
`Tree::line_` is a *view* of the line, not a copy, and `text()` over it is what
`jobs` lists. A tree pinned by a function outlives its line, so `own_source()`
copies the text into the tree and repoints the view — called only when
`defines()` says a `FuncDef` was added, which `add_node` records as it goes.
Without it a background job started inside a function would list freed memory,
which is the kind of bug that shows up as a garbled `jobs` months later.

**Telling `f() { … }` from `f arg` needs one token of lookahead the parser did
not have**, and `simple()` adds the name to the store on its first iteration
with no way to take it back. So `pipeline()` eats the name itself and hands it
on: to `func_def` if a `(` follows, to `simple(name, true)` otherwise. Two
orderings are load-bearing. `compound()` keeps first refusal, because **every
keyword is also a name** — without that, `case x in …` had `case` eaten as a
function name and forty-nine tests failed at once. And a `(` with anything but
`)` after it is reported as `syntax error near '('` rather than `expected ')'`,
so `echo (x)` still says what it said before: it was never a definition.

`(` and `)` cost nothing here, having become tokens at S4 for `case`'s arms.
That is twice now that stage has paid for itself.

**The body runs on the caller's `Ctx`, which is a deliberate trade.** It is what
gives the body the caller's capture, keyboard and depth bound for free — so
`x=$(f)` works with nothing added, and a runaway recursion reports
`too deeply nested` rather than overflowing the wasm stack. The price is that a
function is not a scope: `break` inside one reaches a loop outside it, and its
variables are the shell's. There is no fork, so the alternative is the
save-and-restore checkpoint S7 owes `( list )`, and doing it twice is how the
two would drift.

**What the call does save is the positional parameters**, and `var.cpp` gained
one primitive for it: `args_swap` exchanges the whole block and hands back what
was there. `$#` is derived from the vector's size, so restoring one restores the
other.

**`Flow::Return` was almost free.** `loop_step` already passes anything that is
not `Break` or `Continue` straight through, so a `return` unwinds `while`,
`for`, `case`, `if` and `{ }` with no change to any of them; only the call site
notices it and clears it back to `Normal`. `Ctx::frames` counts function calls
and sourced files together, so `return` outside both is a silent no-op — the
rule `break` outside a loop already follows.

**A function is looked up before a builtin and before `/bin`**, which is ksh's
order and POSIX's. It costs three edits, all at gates `Stage` already had. What
it does *not* yet get is a pipe or a redirection: the body would write to the
shell's own stdout, because `exec_pipeline`'s base stdio is still hard-wired, so
`f | wc` is refused with the message `{ a; } | wc` has had since S3. S7 threads
the base stdio and lifts both at once.

**`ShIo` still has no `Ctx`, and did not need one.** `.` and `eval` reach the
walk through `sh_source` and `sh_eval` in `job.h`, over a file-scope pointer to
the walk in progress that `exec_pipeline` saves and restores around each
builtin. That is the shape `loop_break` and `shell_exit` have had all along, and
it kept `Ctx` private to one file rather than publishing a type that two
builtins out of sixteen need.

**One test bug worth recording, because it is a property of the feature.** A
case proving a function shadows `/bin` defined `echo() { true; }` — and a
definition outlives its line, so every later case in the file silently printed
nothing. Both shadowing cases now `unset -f` on the same line. A language where
definitions persist needs its tests to clean up after themselves, which no
earlier stage did.

**What the tests prove separately.** `test_parse.cpp` renders `fn(name){body}`
as one string compare per form — no space needed before `(`, a newline before
the body, any compound as the body — plus the three refusals. `test/run.mjs` has
what the tree cannot show: that `$1` and `$#` are the call's and go back
afterwards, that a redefinition wins, that `return` carries a status out through
a loop, that a function shadows both a builtin and `/bin`, that `unset -f`
really removes one, and that a function defined in a sourced file is still
callable after the `.` returned.

---

## A word could run a command

Sixth of the stages in `src/sh/TODO.md`: `$( )` and the backtick, the pipe the
shell drains itself, and the hook that lets a pure expander reach an impure
command. `sh.wasm` went from 142,221 to 150,920 bytes and the staging tree from
611,699 to 620,398 against an unchanged 1 MiB budget. **`kernel.wasm` did not
move and the syscall ABI did not change** — `Sys::Pipe` has been there since M6,
and until now `watch` was its only caller.

**The expander gives up rather than becoming a coroutine.** Running a command
needs a `co_await`; `expand.cpp` is synchronous, and it is compiled into
`tests.wasm` precisely because it reaches no syscall. So `Vars` took a fifth
callback that hands back what a substitution *printed*, and when nobody has that
yet the walk abandons the whole word with `Err(Again)`, naming the command and
the byte where it began. `job.cpp` runs it, files the output under that offset,
and expands again.

That is only correct because the walk is idempotent, and it is idempotent for
one specific reason: `${x=$(a)}` gives up **inside** the operator's body, before
`v.set` is reached, so the retry finds x still unset and takes the same branch.
Every other operator only reads. The word is walked once per substitution in it
— one or two in practice — and the alternative, collecting every substitution in
a single pass and re-expanding once, is what does *not* work: it would have to
commit a placeholder for `${x=$(a)}`, and the second pass would then see x set
and take the other branch.

The property the TODO asked for falls out with nothing added: `${x-$(cmd)}` runs
the command exactly once and only when the branch is taken, because the
operator's word is only ever walked on the branch that takes it.
`test_expand.cpp` proves it with a call counter rather than by reading the code.

**The offset that keys the memo needs no plumbing.** Every recursion in the
expander — a `"…"` body, a `${x-…}` body — is over a *substring* of the raw
word, so subtracting the two data pointers gives the absolute offset and no base
has to be threaded through `run`, `dollar` and `braced`. The unit tests assert
it through both nestings, which is the only way that trick stays honest.

**TODO.md's reason for `ShIo::capture` was wrong, so the field was not
written.** It said a builtin inside `$( )` would "fill eight chunks with nobody
left to drain them". `PIPE_SLOTS` counts **writes**, not 512-byte chunks:
`pipe_write` puts a whole `Str` into one slot and returns its full size, so a
builtin that buffers and writes once — which is `builtin.h`'s standing rule, and
what all six output builtins do — uses one slot of eight and never parks. The
last stage takes the capture pipe whether it is a builtin or a program,
`builtin.h` and its six call sites did not change, and the rule there gained the
second reason it is now load-bearing for. `builtin.h` also had "eight chunks"
and now says what it counts.

**Drained before the wait, and `watch` had already written the paragraph.** A
child parked in a full pipe has not exited, so collecting statuses first is a
deadlock only `^C` could break. The drain goes between the builtin loop and the
wait loop, which is the one seam where the shell holds the read end and nothing
else: a program stage's write end was *moved* into it by `spawn`, and a
builtin's was closed by `close_stage` the moment its turn ended. `run.mjs` runs
7,077 bytes through it — fourteen chunks against eight slots — which is the case
that hangs rather than fails if the two are ever swapped.

One capture pipe per *pipeline*, not per line. `$(a; b)` makes one for each and
drains each before the next runs, so the outputs concatenate in order and at
most one message is ever queued; one pipe for the whole list could not see
end-of-input until the list was over.

**`"$(echo "a b")"` forced a real fix.** A `$(…)` inside double quotes quotes
independently, and both scanners looked for the closing `"` byte by byte — so
the inner quote ended the outer run and the word broke in the wrong place. The
lexer's scan and the expander's now skip a `$(…)` whole, using the same
`paren_end` they use everywhere else. `brace_end` was already the precedent for
sharing a scan rather than keeping two copies; `paren_end` and `tick_end` join
it in `tokenize.h`.

**A substitution is not a subshell, and cannot be.** There is no fork, so the
command runs in this shell: `$(cd /x)` moves it and `$(y=1)` sets a real
variable. What *is* contained is containable without one — the command is parsed
and run against its own `Tree` and its own `Ctx` rather than through `run_line`,
which would clear a pending `break` and let `$(exit)` end the shell. The inner
`Ctx` carries the outer's depth, so `$($($(…)))` is bounded like any other nest,
and inherits `interactive`, so `^C` reaches the command and comes back as
`Err(Cancelled)` — which the expansion path already turned into 130 at S4. The
rest is in CLAUDE.md's gaps, waiting for the checkpoint S7 has to write for
`( list )` anyway.

**Trailing newlines go, which is not a detail.** `x=$(pwd)` must not put a
newline in x, and every shell since v7 strips them. Everything else about the
output falls out of `put_value`: unquoted it splits against IFS and is left
**unmarked**, so a `*` out of a command globs exactly as one out of a variable
does; inside `"…"` it is marked and unsplit. That is one call, and it is the
whole of the quoting behaviour.

**Nothing bounds the captured output** but the process's 16 MB cap, which is
v7's answer too. `watch` stops appending at 32 KB because it only paints a
screen; a substitution that truncated would corrupt a value with no way to tell.

**What the tests prove separately.** `test_tokenize.cpp` has the balanced scans
— nesting, a `)` inside quotes, a backtick run inside double quotes, and both
unterminated forms reaching the continuation prompt. `test_expand.cpp` has the
hook against canned answers: the splitting, the mark, the stripped newlines, the
call counter, and `Err(Again)` reporting the right offset through a quote and
through an operator body. `run.mjs` has what neither can:
`$(ls /bin | grep less)` through a real pipeline, a builtin down the same pipe,
a nested substitution, `$(pwd)` with no newline, and the 7,077-byte drain.

---

## A word became a pattern

Fifth of the stages in `src/sh/TODO.md`: `*`, `?`, `[a-z]` and `[!…]` over the
real store, and `case`/`esac` with its arms. `sh.wasm` went from 127,614 to
142,221 bytes and the staging tree from 597,092 to 611,699 against an unchanged
1 MiB budget — 58% of it. **`kernel.wasm` did not move and the syscall ABI did
not change**; a directory listing is `Sys::List`, which `ls` has been calling
since M5.

The two features ship together because they are one matcher. A `case` pattern
*is* a glob pattern, and writing the matcher twice is how the two would disagree
about what `[a-` means.

**This is the first reader the quoting mask has, and the mask was right.** S1
built `Field::mark` one byte per byte with these two consumers named in the
header and nothing using it for two stages; `test_expand.cpp` has asserted since
then that `$star` yields an *unmarked* `*` where `'*'`, `"*"` and `\*` yield a
marked one. Globbing needed no change to the expander at all — `glob_match`
takes the mark as its second argument and a marked metacharacter matches itself.
The alternative, deciding at expansion time whether a word "looks like a
pattern", is what shells that have no mask do, and it is why they cannot tell
`$x` holding a star from a typed one.

**The walk could not go where the plan put it.** TODO.md said the directory walk
belongs in `expand.cpp`. It cannot: `list_dir` is a coroutine, and `expand.cpp`
is compiled into `tests.wasm` precisely because it reaches no syscall — the link
is what enforces that, not a promise. So the matcher is `match.cpp`, pure and in
`tests.wasm` beside the parser and the expander, and the walk is `glob.cpp`, in
`braam_sh` alone. The purity boundary is the thing being protected here, and it
survived by splitting the file rather than by weakening the rule. S5's
`substitute`/`glob` pair is the same argument one step further on.

**The matcher does not follow v7's `gmatch`, and the reason is the pattern the
tests name.** V7 recurses once per `*`, and the port this work follows already
turned its other three tail calls into loop iterations. Both forms are
exponential in *time* on `a*a*a*b`: every star re-tries every suffix
independently. A single `*` class is the whole of this pattern language, so one
saved backtrack point — the last star and the offset it was tried at — answers
it, in a loop, with no recursion left to bound. `test_match.cpp` runs `a*a*a*b`
against thirty `a`s, which the recursive form would not finish.

**Three answers where v7 and POSIX differ, decided the way S3 decided its
three.** A leading dot must be asked for, and a *quoted* dot counts: `\.*` lists
dotfiles here where v7 lists nothing, because every shell since has done so and
the rule is shorter to state. An unterminated `[` matches nothing rather than
being taken literally — that is v7's answer, and it costs nothing visible, since
a word that matches nothing is used as it was written. And a component that is
not the last must be a directory, so `echo */` lists directories and `a*/b`
never tries to descend into a file.

**No sort was needed, which the plan did not expect.** `vfs_list` has sorted its
entries in case-free byte order since M5 — `ls` relies on it and says so.
Prefixes are walked in order and each listing arrives ordered, so a
multi-component glob comes out ordered without a comparison anywhere in
`glob.cpp`. TODO.md had budgeted an insertion sort for it.

**`(` and `)` became operators, which is S7's lexer work done early.** A `case`
arm ends in `)`, and until now that was an ordinary word byte — `a)` lexed as
one word. The parser could have stripped the trailing byte inside `case_clause`
and left the lexer alone, but that misreads `case x in a\)` and cannot accept
POSIX's optional leading `(`. So `is_operator` took both, and the price is that
`echo (x)` is now `syntax error near '('` instead of printing `(x)`. Every real
shell says the same thing, and `( list )` — which S7 has to run as a state
checkpoint, there being no fork — now lexes already. `parse()`'s trailing
diagnostic stopped saying "unexpected keyword" for a token that is not a word,
so `;;` alone reports itself rather than being called a keyword.

**Only argv words are globbed.** An assignment value and a redirection target
expand under `Split::One` — one field in, one field out by definition — and
globbing them would break that contract for no gain worth having; `> *.txt` is a
refusal to write into a pattern rather than a silent single match. `for`'s word
list *is* globbed, which cost one call, because S3 put its words through the
command expander for exactly this.

**`expand_all` became a coroutine, and that is the whole of the cost to the job
runtime.** It expands a command's raw words into a scratch vector and hands that
to `glob_fields`, which appends to `Run::words`; `argv0[i]` is still pushed
before the command's words, so the slice arithmetic nothing else knows about did
not change. A `^C` caught mid-listing arrives as `Error::Cancelled` out of the
expansion, which is the one error that must not print "out of memory" — it
reports 130 and abandons the line, through the path a stage reporting 130
already took.

**`case` is not a loop and must not look like one.** Its arms are a flat run of
(patterns, body) pairs in the child list, as `if`'s branches are, for the
third-and-fourth time the same reason: in this executor depth costs coroutine
frames. It never touches `cx.loops`, so a `break` in an arm belongs to whatever
loop is above it, and a `case` that matches no arm reports 0 with an explicit
`var_status(0)` — the rule `if` needed when it took no branch, since nothing ran
and so nothing published a status. v7's literal-compare fallback after a failed
`gmatch` is not reproduced: a marked metacharacter already matches itself, which
is what that fallback was for.

**A generated name carries a mask of its own.** `Field` promises one mark byte
per text byte, and a name off the store has to keep that promise even though
nothing reads it again — it is marked literal throughout, so no later stage can
take a `?` in a filename for a pattern.

**What the tests prove separately.** `test_match.cpp` is the matcher alone
against table literals, including the quoted star, the quoted `-` inside a live
class, the unterminated `[`, and the pathological pattern; `test_parse.cpp`
renders `case` as one string compare per construct, with the empty arm and the
arm whose last `;;` is spared; `test/run.mjs` has what neither can reach — that
`echo /bin/l*` names two real files, that `/home/g/.*` finds the dotfile that
`/home/g/*` must not, that a star out of a variable globs and a quoted one does
not, and that `case` runs a real body. The glob block sits late in `run.mjs` on
purpose: every command spends pids, and the `ps` cases above it assert a column
width that a five-digit pid overruns.

---

## The tree learned to branch

Fourth of the stages in `src/sh/TODO.md`: `if`/`elif`/`else`/`fi`, `while`,
`until`, `for … in`, `break` and `continue`. `sh.wasm` went from 114,936 to
127,614 bytes and the staging tree from 584,414 to 597,092 against an unchanged
1 MiB budget. **`kernel.wasm` did not move and the syscall ABI did not change.**

A correction to the two entries below this one, since it is a measurement and
not a judgement: both were taken on a build tree still holding the `/bin/export`
that S1 renamed to `save`, which `copy_directory` never deletes. Clean, S2 ended
at 584,414 rather than the 599,238 recorded there. CLAUDE.md already warns that
a release is cut from a clean tree; so, it turns out, is a number.

**Almost nothing new had to be invented, which is the point of having built S2
first.** The node arena took four kinds, `Flow` took two values, and
`break`/`continue` went in as builtins through the path `exit` already used — a
flag the builtin sets, turned into a `Flow` by the pipeline it ran in, consumed
by the loop above. `builtin.h`'s rule needed no amendment: what they touch is
this process's own walk.

**`if` is a flat run of (condition, body) pairs where v7 nests.** The V7 shell
desugars `elif` into a nested `TIF` as the else-branch, which is elegant in a
shell that unwinds with `longjmp` and wrong in one whose walk is a chain of
coroutine frames: `elif` nine times would be nine frames deep. It is the same
argument that made `Seq` and the `&&` chain n-ary in the stage before, and it is
now the third time it has come up, so it is worth naming — **in this executor,
depth costs frames, so anything that reads as a chain must be stored as one.**

**Three places where POSIX won over the reference, all deliberate.** An `if`
whose condition fails with no `else` reports 0; v7 reports the *condition's*
status, because its `execute()` zeroes `exitval` only for a non-null tree and
the missing else-branch is a null one. That is a bug every shell since has
fixed, and scripts have assumed the fix for forty years. `for x in; do … done`
is legal with an empty list where v7's `chkword` refuses it. And `continue n`
takes a level, which v7 parses for `break` and ignores for `continue`.

One v7 behaviour was deliberately *not* reproduced: `break 9` inside one loop
leaves its level counter set after the outermost loop exits, and nothing there
ever clears it, so the rest of the script is silently skipped. `run_line` clears
the request on the way out, so it can spill into the rest of a line but never
into the next one.

**A loop lets `Flow::Interrupt` through rather than consuming it**, and that one
line is what makes `while true; do sleep 5; done` come back on a single `^C`
instead of one press per iteration. `Break` and `Continue` are the only two a
loop takes; `Exit` and `Interrupt` pass through to `run_line`. The gap it leaves
is now in CLAUDE.md: a loop whose body is *entirely* builtins has nowhere for a
`^C` to go, because the shell arms its children with `Sys::Fg` and is never in
its own foreground set. Every `while true` has a program in it today, since
`true` is `/bin/true`, so the gap only opens when S8 makes `:` and `test`
builtins — which is the stage that has to argue for them.

**A construct that runs nothing has to say so.** `exec_pipeline` is what
publishes `$?`, so an `if` that took no branch and a loop whose body never ran
would have left the *condition's* status visible even though they report 0. Both
call `var_status` on that path and only that path — the same shape the `!`
operator already needed.

**`for`'s name and word list are a `Command`**, so the expander that serves a
command serves them too and `for i in $files` splits against `IFS` for free,
with globbing to follow at S4 with nothing added. The one catch was that
`add_word` counts a leading `name=value` as an assignment prefix, which would
have hidden `for i in x=1` from `args()`; the fix is a defaulted parameter that
the `for` words pass false. Its items are copied before the loop starts, because
a `set` or `shift` in the body would otherwise move the vector being walked — v7
takes a reference on the parameter block for exactly that reason.

**Reserved words are recognised by position and quoting is handled by doing
nothing.** `ends_list` is consulted only where a command may start, and
`simple()` takes words greedily, so `echo done` prints `done` and
`while true do …` reads `do` as an argument to `true` — which is v7's behaviour
and why the `;` is mandatory. And because a word still carries its quotes at
that point, `'do'` is four bytes and can never match the two-byte keyword.

**Two things cost more time than the feature did.** A conditional expression
choosing between string literals — `w == "then" ? "…" : "…"` — is not a constant
pointer, so `Str`'s `__builtin_strlen` did not fold and the link failed on
`strlen` with no libc to satisfy it. The codebase already had that comment in
`job.cpp`; it now has it twice. And a driver case that types a 63-character
command in one burst overflows the 64-slot key ring and loses its Enter, which
looks exactly like a hung shell. A human types slowly enough never to see it.

**What the tests prove separately.** `test_parse.cpp` renders the four
constructs, so `if` with an `elif` chain, a nested loop and a `for` with and
without its `in` are each one string compare; `test/run.mjs` has what the tree
cannot show — that a loop rebinds its variable, that `break 2` leaves both loops
and nothing else, that `break` outside a loop is silent, and that
`if false; then echo x; fi` reports 0.

---

## A line became a tree

Third of the stages in `src/sh/TODO.md`, and the one it called the largest. `;`,
`&&`, `||`, `&` mid-line, `#` comments, `!`, `{ … ; }`, a newline as a
separator, and a half-typed construct that asks for another line under `PS2`.
`sh.wasm` went from 104,292 to 114,936 bytes and the staging tree from 588,594
to 599,238 against an unchanged 1 MiB budget. **`kernel.wasm` did not move and
the syscall ABI did not change**; `smoke`'s exact import and export surface
stayed green untouched, which is what says so.

**`Pipeline` became `Tree`.** It already owned the store, the words and the
commands; now it owns the nodes above them, and a class named for one of the
kinds it holds would have been read wrong by every stage after this one. The
rename is the whole of `parse.{h,cpp}`, `job.cpp` and `test_parse.cpp`, and it
is cheaper now than at any later point.

**The arena is index links, for the reason `freeze()` already gave.** `nodes_`,
`kids_` and `ops_` all reallocate while the parse grows, so a pointer into any
of them dangles at the next push. Indices also keep a frozen tree trivially
movable, which `test_parse.cpp` asserts and which a function body will depend
on. `nodes_[0]` is always a `Nop`, so an index of 0 can mean "nothing" without a
sentinel of its own — an empty line is a tree whose root is 0.

**`&&` and `||` are one n-ary chain, not a left-leaning pair per link, and that
is a *depth* decision rather than a tidiness one.** `exec_node` recurses, so
`And(And(And(a,b),c),d)` would make `a && b && c && d` four frames deep and a
hundred links a hundred frames. As one chain with a byte of operator per link,
the walk's depth is the *text's nesting* depth — braces, `!`, and later `if`,
`while` and a function — which is what the parser's bound of 16 actually bounds.
`Seq` is n-ary for the same reason, so a hundred-statement script is depth one.

The short circuit is a `continue` and not a `break`, and the difference is
`false && a || b`: the `&&` skips `a`, and the status carries down the chain so
the `||` still reaches `b`. Breaking out would have stopped the whole chain at
the first failure, which is a different language.

**Control transfer is a field, because there are no exceptions.**
`enum class Flow { Normal, Exit, Interrupt }` on a `Ctx`, checked after every
`co_await exec_node(…)` — one branch per node, the same shape as `TRY()`.
`Break`, `Continue` and `Return` join it when there is something to break out
of.

`exit` is the case that shows why a field and not a return value. It was already
a builtin that *asks* rather than acts, setting a flag the prompt loop reads at
the end of a line; with a list, `echo a; exit; echo b` would have run `echo b`
before anyone looked. So `exec_pipeline` consumes the flag where the builtin
ran, turns it into `Flow::Exit`, and `run_line` re-arms it on the way out.
`shell.cpp`'s two loops did not change a line for it.

`Flow::Interrupt` was scheduled for the next stage, with the loops. It came here
instead because `;` is what makes it reachable: without it `sleep 5; echo done`
echoes after a `^C`. A stage reporting 130 stops the rest of the text, and
`job.h` now says why that is the mechanism — there is no signal, and a status is
the only thing that crosses a process boundary. The price is stated there too: a
program that exits 130 of its own accord does the same.

**`Ctx` is its own heap block, and so is the tree.** `Run` held the `Pipeline`
by value, and it could not: the tree outlives every pipeline under it.
`run_line` allocates the tree, `Run` borrows it and shrank by half as a result.
`Ctx` is thirty-odd bytes today and did not have to be on the heap — but it
grows with every stage left (break levels, traps, a scope, the `-e -x -u`
flags), and `run_line`'s frame is the root of a recursion. Putting it there now
freezes that frame instead of letting each later stage push it a little closer
to the 512-byte cliff.

One `Run` per pipeline, `heap_new`'d and freed as each runs, rather than one
cleared and reused: `Vec` has no capacity-preserving `clear()`, ten of them
would have to be got right every time, and command substitution will want two
`Run`s alive at once anyway.

**Multi-line input is re-parsing, and v7's best idea was rejected on its own
merits.** The V7 shell serves `.`, `eval`, traps, `-c`, substitution and
here-documents through one `fileblk` chain — a single character source that can
block. Here that would make the lexer a coroutine and the recursive-descent
parser a stack of them, five frames deep on every word against a 512-byte
budget, and it would drag `sys_async` into `parse.cpp`, which `tests.wasm`
compiles precisely because it touches nothing but `Str`, `String` and `Vec`.

So `parse()` gained a third outcome instead: `ParseErr{ message, more }`, where
`more` means the text ended *inside* something. Both readers accumulate and
re-parse from the top — the prompt under `Prompt{ {}, {}, ps2 }`, which
`LineEditor` needed no change at all to draw, since a `Prompt` was already three
independent pieces; and `sh -s` over `LineReader`, one line at a time, so
`producer | sh -s` still streams rather than blocking to EOF. It is quadratic in
the length of one construct, and a construct is tens of lines against a pure
parser that allocates into a store it throws away. `more` is deliberately *not*
a new `Error` value: that enum is kernel-wide.

`more` is set where the operator was — a trailing `|`, `&&` or `||`, an unclosed
`{`, an unterminated quote or `${` — and never by a command that simply had no
words. `> f` is a syntax error at once; `a &&` waits.

**A newline stopped being whitespace**, which is one asymmetry and no more: the
lexer's leading skip takes a new `is_blank` that excludes `\n`, while the
word-terminating test keeps `is_space`, which already included it. `#` starts a
comment only where a token could start, so `echo a#b` is still one word. `{`,
`}` and `!` stay ordinary word bytes and are reserved by *position* — and
because a word still carries its quotes at that point, `'{'` is three bytes and
can never match the one-byte reserved word, so quoting is handled by doing
nothing.

**Two refusals, both for the same reason.** `a && b &` and `{ … ; } &` are
`cannot run a list in the background`: backgrounding means the shell keeps
running while the rest goes, and nothing in a process can wait for a sibling
task, so there would be nobody left to run `b`. What may be backgrounded is one
pipeline whose stages are all spawned children. And `{ a; } | wc` and
`{ a; } > f` are refused as *not yet* — a group needs the base stdio threaded
through `Ctx`, which is the redirection stage's work, and doing it twice is how
the two would drift.

**`jobs` now lists a pipeline's own text.** The `JobEntry` copied the whole
line, which was the same thing when a line was one pipeline; with
`echo hi; sleep 5 &` it would have filed the lot. Each `Pipe` node carries the
byte span it came from, which is two of the four fields a node has spare, and
the `&` falls outside it — so the listing reads `sleep 5`, which is what it
always should have said.

**What the tests prove separately.** `test_parse.cpp`'s `shape()` grew from a
loop over stages to a walk over the tree, and every construct is one string
compare again: `{a};{b}`, `{a}&&{b}||{c}`, `!{a}`, `{{a};{b}}`, and `?` rather
than `!` in front of a message when the text merely ended early. `test/run.mjs`
has the four things the unit suite cannot see: that `false && a || b` reaches
`b`, that `$?` is the *pipeline's* and not the line's, that a trailing `&&`
leaves `>` on the screen and not a prompt, and that
`echo before; exit 7; echo never` prints one of the two.

---

## The shell got variables

Second of the stages in `src/sh/TODO.md`. `$x`, `${x}`, the `${x-y}` family, the
specials, assignment as a word the parser knows, field splitting against `IFS`,
and `set`, `shift`, `unset`, `export` and `readonly`. `sh.wasm` went from 83,485
to 104,292 bytes and the staging tree from 552,965 to 588,594 against an
unchanged 1 MiB budget. **`kernel.wasm` did not move and the syscall ABI did not
change** — everything here is inside the process binary, and `smoke`'s exact
import and export surface stayed green untouched, which is what says so.

**`Field` needed a flag the mark could not carry.** The last stage put the
quoting mark out of band, one byte per byte, and that was right for everything
the mark will ever be asked — except one question it cannot be asked at all:
*was quoting the reason this field exists?* With x empty, `echo "$x"` must pass
one empty argument and `echo $x` must pass none, and both are a zero-length text
beside a zero-length mark. V7 spends its lone `QESC` on exactly this. So `Field`
gained a `bool quoted`, set by the `'` and `"` arms whether or not they
contribute a byte, and the splitter keeps an empty field only when it is set. It
was harmless while one field was always pushed; the moment a word could expand
to nothing it was the difference.

**Splitting happens during the walk, not after it.** We know whether a byte came
from an unquoted expansion at the moment we append it; after the fact we would
have to consult the mark to find out, and the mark would stop being scratch. So
the splitter is one branch in `put_value`, and a literal byte of the word never
goes through it — which is why `IFS=:` leaves the typed word `a:b` alone and
splits `$path` in two. That distinction is a `bool literal` threaded through the
walk, and it also settles `${x-a b}`: the body of an operator is expansion
output rather than the word's own bytes, so it splits, where the same two
characters typed directly would not.

**The expander took callbacks rather than calling the variable table.**
`var.cpp` is pure enough to have been linked into `tests.wasm` beside
`expand.cpp` and called directly, and that would have been less code. The seam
is there for the two stages that need it: command substitution adds a
`substitute` callback beside the lookup, and a function body will want a table
of its own that is not the shell's. Four function pointers now is the shape
those want, and the unit tests fill them with table literals — which is also
what keeps `expand.cpp` clear of every syscall, a fact the link into
`tests.wasm` enforces rather than promises.

**`x=1 cmd` sets, runs and restores — and a program cannot see it either way.**
There is no environment anywhere in the wasm ABI: `Sys::Spawn` hands a child
three descriptors, an argv and a cwd. So an assignment prefix on a program stage
is expanded and then has nowhere to go. Rather than leak it into the shell or
refuse the idiom, the prefix is applied around the one stage that can see it — a
builtin, and later `read`, `eval` and a function — and the displaced values are
put back after. `x=1` with no command word is the ordinary case and persists.
Per stage, so `x=1 a | y=2 b` leaks nothing.

**`${x?}` aborts the line rather than the shell.** V7 ends a non-interactive
shell there. Braam prints `braam: x: parameter not set` and reports 1 in both
modes, because script-entry policy belongs to the stage that adds `sh file` and
`sh -c` and there is nothing to be gained by deciding it early. Expansion runs
before anything is opened or spawned, so the line leaves nothing behind.

**A command may now have no command word at all**, which two things make
reachable: `x=1` alone, and a word that expands to nothing. `run_line` indexed
`args(i)[0]` unchecked at two sites, correct only because the parser guaranteed
a word and the expander guaranteed a field. Both guarantees are gone. The null
command performs its redirections, keeps its assignments and reports 0, which is
Bourne.

**`${` had to stop ending a word**, or `${x-a b}` would lex as two and
`${x-a|b}` as three. The lexer gained one arm, and it shares `brace_end` with
the expander rather than carrying a copy — unlike the quote arms, which are
duplicated because each needed a different thing from the same rule. `\$` also
became escapable inside double quotes, in both, which changes no word boundary
but makes `echo "\$x"` mean what it says.

**`/bin/export` became `save`.** The builtin and the program wanted one name,
and the builtin's is not negotiable — it is in every script ever written. The
program is the Blob download of Concept.md §5.4 and its name never was: `import`
and `save` are the pair now. The alternative was shipping no `export` builtin at
all, which is defensible only until §5.4's paragraph has to explain why a
standard name is missing.

**What the tests prove separately.** `test_expand.cpp` carries the whole `$`
grammar against a table-literal shell, including the empty quoted word against
the empty unquoted one, `"$@"` with no parameters leaving no field where `""`
leaves an empty one, and the mark showing that an unquoted expansion is
*unmarked* — which is what will let a `*` out of a variable glob at S4.
`test_parse.cpp` renders an assignment prefix in brackets, so `[x=1]{ls}` is one
string compare. `test/run.mjs` has what the unit suite cannot reach: that a
value set at the prompt survives to the next line, that `echo a "$e" b` and
`echo a $e b` differ by a space, and that `x=two echo hi` leaves the shell's x
where it was.

---

## The lexer stopped removing quotes

First of the stages in `src/sh/TODO.md`, which is the plan for making `/bin/sh`
a language rather than a prompt. Nothing a user can see changed: the grammar is
the same one line it was, and the same commands do the same things. What moved
is *when* quotes come off — out of `Lexer::next`, into a new `expand.cpp` that
runs between the parse and the run. `sh.wasm` went from 79,899 to 83,485 bytes,
and the staging tree from 549,379 to 552,965 against an unchanged 1 MiB budget.

**Two things force the move, and neither is a preference.** A loop body is
parsed once and expanded once per iteration, so `for f in *; do echo $f; done`
cannot have its words collapsed at parse time — there is nothing left to
re-expand. And `$` acts inside `"…"` and does not inside `'…'`, a distinction a
one-pass collapse throws away before anything can act on it. Both bite in the
stages after this one, which is exactly why this one is separate: it is the
whole refactor with none of the new behaviour, so a regression here is visible
on its own.

**`parse.h`'s comment was the thing that had to change.** It said words are
owned "because quote removal makes a word something the line does not contain" —
and with quotes left in, a word *is* a substring of the line again. The store
did not become unnecessary, but its justification did: it is now that a parse
outlives the text it came from, which is what a function body and a sourced file
will need. The old reason was true and would have quietly stopped being true.

**The lexer got smaller and stopped allocating.** It already walked the quotes
to find where a word ends; it merely also copied. Now it only walks, and
`next(Str &)` hands back a view. The double-quote arm keeps its escape rule
verbatim — only `\"` and `\\` — because that is not a copying detail but a
*scanning* one: without it a `\"` would close the run early and the word would
end in the wrong place.

**The quoting mark is a parallel byte array, and v7's encoding was rejected on
the reference's own argument.** The V7 shell marks a quoted character in bit
`0200` of the character, and the BESM-6 port had to move that to a `QESC` prefix
byte because its console is UTF-8. Braam's is too, and here it fails twice over:
there is no `int`-wide character space to hide a flag in either. So `Field`
carries `String text` plus a `Vec<u8> mark`, one byte per byte. Nothing reads it
yet — field splitting and globbing are the only two readers there will ever be,
and both are stages away — but it is built now because adding it later means
rewriting every branch of the expander. It is deliberately unconditional rather
than lazily allocated: an unused optimisation nobody has measured is worse than
an allocation nobody has measured.

**The expanded words are a second set, not a rewrite of the first.** `Run` grew
`words`, `argv`, `argv0` and `targets` beside the `Pipeline` it already held,
because one raw word may yield any number of fields and because the same tree
may be run more than once. `Run::args(i)` replaces `Pipeline::args(i)` at the
four sites that build a spawn, find a builtin or name one in a diagnostic. The
views are built only after the last append, which is `freeze()`'s rule one level
up — though the reason differs: a `Vec<Field>` moves its elements by stealing
their pointers, so the bytes never move and the discipline is habit rather than
necessity here. Habit is the point.

**What the tests now prove separately.** `test_tokenize.cpp` and
`test_parse.cpp` render words with their quotes still on and assert only where a
word *ends*; the quote-removal cases they lost are `test_expand.cpp`'s, along
with the mark, rendered as `^` and `.` per byte so one string compare checks
text and mark together. `expand.cpp` joins `parse.cpp` and `tokenize.cpp` in
`tests.wasm` — it touches nothing but `Str`, `String` and `Vec`, and the link is
what enforces that. Two cases went into `test/run.mjs` as well, because the unit
suite cannot prove the words reach a real `argv`: `echo 'a  b'` and
`echo a\ \ b`, both with **two** spaces, since one would survive the word
becoming two arguments and echo joining them back with a single space.

---

## Scrollback, and the renderer that was told nothing

`scroll()` memcpy'd the rows up and overwrote row 0, so everything that reached
the top of the window was gone. Shift+PageUp and Shift+PageDown now page back
over it, half a screen a press, the way the Linux console does. `kernel.wasm`
went from 146,326 to 148,483 bytes against an unchanged 256 KiB budget.

**The history has to be the kernel's**, and that is the one thing not open to
argument. A row is overwritten inside a `tick`, often many of them, and
`host_present` fires once at the end — so by the time the page could look, the
row it would have kept is already gone. There is no capturing it from
`web/render.js` and no ABI that would let the page ask.

**The renderer, though, is told nothing at all.** `struct Screen` gained no
field, there is no new import or export, and `web/render.js` changed by one
comment. The kernel composes the view into a cell array and points `cells` at
it; the renderer paints row *i* at canvas row *i* as it always did. Selection
came free out of that: `all()`, `text()` and `bounds()` index `S_CELLS` freshly
on every call, so a drag over scrollback selects and copies scrollback without a
line of it knowing. The alternative — `view_top` and `history_cells` in the
descriptor, and `render.js` biasing its row reads — would have had to re-teach
the selection, `text()` and select-all what a row means, and would have put the
ring's layout into the ABI where the magic word is the only versioning there is.

**Which buffer moves is the whole design.** The obvious arrangement keeps
`cells` pinned and shadows the live screen while a view is up — and then every
write has to be kept from damaging a rectangle that is no longer on screen,
which means hooks in `damage`, `damage_all`, `screen_touch`, `screen_flush` and
`screen_cursor`, plus a private path for the view's own repaint to bypass them
all. Six edits to the hottest code in the kernel. Inverting it costs one:
`g_cells` stays the live screen, every writer is untouched, and `cells` points
at a composed block instead. A write while scrolled back damages a rectangle the
renderer then repaints out of the composed block, where those cells are
unchanged — wasted paint, never wrong pixels. What it costs is that the address
in the descriptor moves when a view opens or closes; nothing caches it, because
`memory.grow` detaching the buffer (§8.4) already forced everyone to re-derive,
but a test that holds a descriptor across a press is a hazard that did not exist
before, so `test/run.mjs` says so where it reads `cells`.

**The live screen stays live, which is what keeps the anchor arithmetic
honest.** Output goes on into the grid underneath, `screen_scrolled()` goes on
counting, and `LineEditor`'s anchor follows it — none of them can tell there is
a view. Freezing output instead would have meant deciding what `Sys::Echo`
should answer with, and the answer would have been a lie the moment the view
came down.

**Composing is the flush's, not the scroll's.** `scroll()` runs once per output
line. Recomposing there would memcpy the whole grid that often — a megabyte a
line at the widest geometry, thousands of times inside one `tick`, which is a
hang rather than a slowdown. It sets a flag instead, and `screen_flush` composes
once a tick, where `host_present` already is.

**The view stays on the rows being read.** When output scrolls the grid
underneath, the offset grows with it, so the text does not slide. Once the ring
is full the oldest row is evicted and the offset clamps instead, and from there
the view drifts by a row per scroll — correct, since those rows are genuinely
gone, but the kind of thing someone later reads as a bug, so it is asserted.

**Any key comes home, and that is a choice.** The Linux console leaves you where
you are until you page back down. Coming home on the next keystroke is the more
forgiving rule, and it also makes the prompt provably live whenever anything is
typed — so the line editor's anchor can never be pointing into a view, and there
is no interaction to reason about. Paging is therefore the only chord that does
not come home, and `MOD_SHIFT` was free: nothing in the kernel had ever looked
at it.

**The pump owns the chord, but not while a program holds the screen.** `less`
and `edit` page with PageUp and switch on the code alone, and the grid they are
painting is not the one with the history behind it — so the claim gates the
interception, and an unshifted PageUp was never the pump's anyway. `FullScreen`
brings the view home before it snapshots, and that is load-bearing rather than
tidy: `less /README &` claims the screen from a syscall with no keystroke in
between, and without it the renderer would sit on a frozen view while the pager
painted underneath.

**512 rows, allocated on the first scroll.** A row is `cols * 8` bytes, so it is
320 KB at eighty columns and 2 MiB at the widest grid the host can ask for; a
screen that never fills pays nothing, and a heap that will not give it up means
no scrollback rather than no screen. A width change re-widths the ring, clipping
and padding each row exactly as the grid itself is clipped and padded — there is
no line model to re-wrap with, which is the same reason resize has never had
one. Only on a width change: `web/worker.js` calls `resize()` on every viewport
event, and a rows-only change must not copy megabytes per frame.

**`clear` does not clear it**, the way the Linux console does not. The screen
blanks and what scrolled off it is still there to page back to.

**Re-wrapping logical lines is still outstanding.** §3.5 promised it would "land
with whichever milestone needs scrollback"; scrollback landed without it,
because the per-row continuation bit is a property of how a row was *written*,
and history stores rows that were already written. The promise moves rather than
being kept — and the selection that dies on the next keystroke is still waiting
on the same bit.

---

## /mnt/import became /import

The picker's landing directory sat under a `/mnt` that never held anything else.
`/mnt` is where Unix puts *mounted* filesystems, and §5.1's whole point is that
there is one store and one generated tree: `/bin`, `/share`, `/home`, `/tmp` and
the import directory are all directories in the same `OpfsFs`. A `/mnt` with a
single ordinary directory under it promised a second filesystem the system has
no way to give, and cost every user of the escape hatch four extra characters on
a path they type by hand. `/import` says what it is at the root, beside the
rest.

It is a rename with no mechanism behind it: `make_dirs` in `src/user/boot.cpp`
creates one directory instead of two, `import`'s default destination is
`/import`, and the archive still carries neither — `installOps` names only the
top-level directories `rootfs.zip` actually contains, so the tree it does not
carry is still never touched.

**An existing store keeps its old `/mnt/import`, files and all.** Boot does not
migrate it and nothing removes it, because a boot-time rename would have to
decide what to do about a collision with a `/import` already made and would run
on every boot for ever after. `mv /mnt/import/* /import` then `rm -r /mnt` is
the whole of it, and a store that is never touched simply carries a stale
directory — which is what §5.2 means by the archive, not the store, being what
the system recovers from.

---

## ls asks whether it is talking to a terminal

`ls` printed one name per line, always, and understood one flag. Bringing BSD's
`ls` across — `-C` in columns, `-R`, `-S`, `-r`, `-d`, `-h`, a `-l` that lines
up — turned out to need one thing the system did not have, and the rest fell out
of it.

**There is no `isatty`, and §2.3 is why.** The terminal is a cell grid, not a
byte stream, so there is no escape sequence a program could send and no
environment it could read a width out of. `Sys::Cursor` reports the grid's
geometry, but it reports it to a program whose stdout is a pipe just as readily,
so it cannot answer the question that matters. The kernel *can* answer it:
`stdio_console()` installs one sink and `pipe_sink`/`file_sink` install others,
and that function pointer is the whole of the difference. It was simply never
reported. `Sys::Tty` is the operation that reports it — the thirty-seventh, arg
= fd, data = a flags word and the geometry.

Three choices inside it are worth the ink:

- **An explicit predicate in `tty.h`, not `ctx == nullptr` in the dispatcher.**
  The console sink is today the only `Stream` with a null context, so the
  implicit test would work. It would also silently promote the next sink that
  happens to need no context into a terminal. `tty_is_console` says what is
  meant, in the file that owns the fact; `console_is_input` is its twin for fd
  0, so `arg = fd` is honest across its whole domain rather than over two thirds
  of it.
- **Zero geometry for a pipe, not 80.** A caller cannot distinguish an invented
  width from a measured one, and would believe it. The fallback belongs in the
  program, where it is visible: `ls -C` into a pipe formats at eighty because
  `ls` chose eighty.
- **A flags word rather than a bool.** It is what `SYS_STORE_*` bought `Storage`
  — room for a second fact about a terminal without a thirty-eighth operation.

`PROC_ABI` went 8 → 9. The change is purely additive and an old binary would
never call the new opcode, so nothing would actually break; the bump is the
tree's standing rule that the number tracks the table, and it buys a sentence at
exec (`built for another process ABI`) instead of a stale binary meeting
`Err(Unsupported)` somewhere further in. In-tree it costs a rebuild, since
`tools/stamp.py` reads the constant.

**The flag parser is a third header in `src/proc/`, and a translation unit of
its own.** `ls` went from one flag to eight, and every command in `src/cmd/` was
hand-rolling a leading-flag loop that did not accept `-lR`. `proc/opt.h` is not
part of `io.h`, which is one wrapper per syscall, nor of `rt.h`, which is
`_start` and `SysCall`. Being its own `.cpp` is load-bearing: `--gc-sections`
never extracts an unreferenced archive member, so the thirty-three binaries that
do not call it pay nothing — the rule the builtin table exists for, used the
other way round. `ls` is the only adopter here; `head` and `tail` would take
`Opts{"", "n"}` unchanged, and were left alone.

**Column-major, and a shared column width.** A sorted listing is read *down* a
column: filling across would put `ls` and `mkdir` in different columns of the
same row and make the sort useless to the eye. The width is one number for the
whole block, as BSD's is rather than GNU's per-column fitting — with a `total`
in 512-byte blocks under `-l`, `FS_BLOCK` being the system's own unit. The last
cell of a row is deliberately not padded: trailing blanks would leave the cursor
in the deferred-wrap column, and the test harness trims them, so that bug would
have passed the suite and shown up only in a browser.

**Width is codepoints, not bytes.** `screen_write` puts one codepoint in one
cell, so `Buf::put_left`/`put_right` — which pad by `Str::size()` — cannot be
used on a filename at all; `ls` counts with `utf8_decode` and pads a `String`
itself. They are still right for the `-l` size column, which is ASCII digits.

**`-R` is an explicit stack, not a recursive coroutine.** §8.2 prices a
coroutine frame at a whole size class, and a frame per directory level is the
wrong shape for a tree. One heap `Vec<String>` holds what is still to list;
children are pushed in reverse of the order they printed, so they pop in it,
which is BSD's depth-first pre-order for free. Memory is bounded by sibling
counts along the current path rather than by the tree, and there are no cycles
to guard against: symbolic links arrived afterwards and a listing reports one as
`SYS_KIND_LINK` rather than as what it points at, so `-R` — which pushes on
`SYS_KIND_DIR` alone — still cannot walk through one. The entry vector, that
stack, the operand list and the row buffer are one heap block for §8.2's
reason, the same
one `less` has a `Pager` for.

**What could not be brought across at all**: `-t`, `-i`, `-s`, `-o`, `-T` and
`-L`. The filesystem stores `{kind, size}` and nothing else — no mtime, mode,
owner, link count or inode anywhere in `Stat`, `Entry` or the `Sys::List` wire
format. Any of those is a change reaching both storage backends and the ABI, not
a change to `ls`. (`-t` since arrived, by making exactly that change — see
"Files have a modification time", above. `-L` has since become *answerable*, and
is still not there: with links in the system it would mean "follow them", and
what `-l` does instead is decline to follow an *operand*, which is the case
worth having. The other four stand.) `-a`/`-A` were left out for a different
reason: nothing in the tree creates a dotfile and nothing hides one, so the flag
would introduce the concept of a hidden file rather than expose it. `-F` is
already there and unconditional — the trailing slash on a directory is the only
thing distinguishing it from a file in the short form, and under columns it
earns its place twice over.

**vmstat's rate columns went from five wide to six.** `W_RATE = 5` gave a
five-digit rate no separator from the column on its left, so `sy` and `cs` ran
together into one number. The listing work tipped `cs` over 10,000 and exposed
it; the row is 74 columns now and still inside eighty.

The layout tests moved off `/bin` and onto a `/home/t` the suite builds itself.
A listing of `/bin` is a test that fails whenever a program is added, which
makes it a test of the wrong thing; what is left pointing at `/bin` asks only
that `cd` is absent and `timeout` present.

`ls` costs 26,504 bytes against 15,633, and the staged tree 549,403 against
538,499 — 52% of its budget. `kernel.wasm` grew 318 bytes for the new arm, to
146,342. Every `ls` now pays one extra round trip for `Sys::Tty`, 34–45 µs;
against it, a directory listing to the grid is one write per *row* rather than
one per entry, which on `/bin` is six writes instead of thirty-four.

---

## exec.cpp was four things in one file

At 2,211 lines it was a 3.4× outlier over the next largest source in the tree
and more than half of `src/user/`, and the reason it could not shrink was
structural: everything in it sat in one anonymous namespace, so nothing could
leave without first becoming a name another translation unit can see.
`src/user/proctab.h` is that step — the process record, the descriptor behind a
number, the parked call, and the counted references over all three. It is
private to the directory in the only way this tree has ever made a header
private: it is not installed, and it says at the top that `exec.cpp`,
`syscall.cpp` and `proctab.cpp` are the whole of its audience. `exec.h` remains
the surface for everyone else, which is why `boot.cpp`, `procfs.cpp` and
`main.cpp` did not change a line.

The cut is by *what the code is doing*, not by size. `exec.cpp` keeps the life
of a process — resolving a name, waiting for a worker, stepping the instance,
and the four entry points the kernel exports. `syscall.cpp` keeps what a process
asks for: `proc_syscall` and the child a `Sys::Spawn` creates, since `Spawned`
exists only because that operation does. `proctab.cpp` keeps the table and its
accessors, and `exec_proc_state` with them — `/proc`'s view of a process is a
query over that table and nothing else, which is what lets `g_procs` stay a
file-local of the one file that owns it.

**The syscall switch stayed one file.** The `Sys` enum blocks the ABI into
families and the switch follows it, so cutting it four more ways along those
lines was the obvious next move and was decided against: a family would then be
a coroutine of its own, and a coroutine is a frame per outstanding call. The
read of §8.2 that matters here is that a frame past 512 bytes costs a whole 64
KiB span, and the dispatcher's frame is already the union of every arm's live
state — splitting it would have added a second frame to that, not replaced it.
One file of 1,320 lines with four visible sections is the cheaper shape.

**The helper set stops where the frame does.** `CO_CALL` (a Task that would not
allocate is `Err(NoMemory)`, not a crash) and `CO_RETRY` (an `Again` is a stray
wake) are macros for exactly that reason: a function wrapping an `await` is a
coroutine, and these had to stay inline. What is left is plain functions —
`chunk_reply` and `write_status` for the tails a read and a write share,
`handle_bind` for the five arms that open something, `reply_u32` for the twelve
that answered with a fixed header. Nothing was factored that had fewer than
three callers.

**Cancellation folded to one exit.** The switch had twenty-six
`co_return Err(Cancelled)` between its arms, each an early return out of a
coroutine and each carrying its own unwind. They are now a single check after
the switch: `-Error::Cancelled` in `status` means the reply is abandoned,
wherever in the switch it was set. That is the same semantics — no arm ever
reported cancellation *to* the program — and it is where the size went: 147,182
bytes before the split, 146,024 after, against a budget of 262,144. A refactor
that was supposed to cost a little returned a kilobyte.

Two out-of-memory paths were wrong and are fixed in passing, both the same
mistake. `Sys::Fetch` bound its handle into the descriptor table and *then*
released it if copying the headers failed, leaving the table pointing at a freed
block; the release now happens before the bind. `handle_bind` exists so that the
other four arms cannot make the same mistake: it binds, or it closes what it was
given and reports `-NoMemory`, and there is no order left for a caller to get
wrong.

---

## The tree that becomes the root is called rootfs

`bundle/` is now `rootfs/`, and the staging copy `build/bundle/` is
`build/rootfs/`. The name was the last survivor of `bundle.bin`, the packed blob
that `rootfs.zip` replaced: since then the directory, the staging tree, the size
budget's entry and the archive the host fetches were one thing under two names,
and the one nobody sees on disk was the one the code used. The budget line reads
`rootfs/` and the archive beside it `rootfs.zip`, which is the point — the tree
and the file are the same content, compressed or not.

`/bin/version` went at the same time. It printed `BRAAM_VERSION` and nothing
else, which the boot banner, `/proc/version` and `uname -a` each already say; a
whole binary, its worker and its instantiation for a string the kernel prints
unasked. It was also the only program that included `kernel/version.h`, so the
`revision` dependency the build named one target at a time lost an entry rather
than gaining a rule.

---

## What the kernel is doing, as rates

`ps` says what each task *is*. Nothing said what the system was *doing*. Every
counter the kernel kept was a running total on `/proc/meminfo` — `allocs` and
`frees` — and nobody subtracted them, so the figure §8.2 calls the primary
workload of the whole system was invisible as a rate. The costs §4.4 quantifies
were in the same position: two `postMessage` hops per syscall, one syscall per
512-byte chunk, asserted in prose and impossible to watch. `vmstat 1000` during
`cat big | wc -c` is the first thing that shows them.

**One file, because a row is one moment.** The counters could have been columns
added to `/proc/meminfo` and gauges added to `/proc/tasks`, and then a row would
have been two `open`s and two moments — which is exactly the mistake
`/proc/tasks` was introduced to avoid. `/proc/stat` therefore repeats the heap's
figures rather than referring to them, the same duplication `/proc/tasks` has
against `/proc/<pid>`, and for the same reason. Both branches call
`heap_stats()` once so neither can drift.

**`now` is the first line, and it is the whole design.** It is the `sched_now()`
the counters below it were incremented against, so the reader divides by the
file's own elapsed time rather than by how long it asked to sleep. A throttled
background tab, a late timer and a `^C`-shortened sleep all come out right, and
the interval argument degrades to a fallback divisor for the case — reachable,
since the clock is truncated to whole milliseconds — where two reads land inside
the same millisecond. It also makes the since-boot first row fall out for free:
`counter / now` is BSD's `rate()` over `getuptime()`, with no special case.

**A woken token is two different events and is counted as two.** `sched_wake`
has four callers and only one of them is the host: a channel wakes its peer, and
an exiting child wakes a parked parent. Counting them in one column would have
put every byte moved through a pipe in the same figure as an answer from
outside, so `wc a | grep b` would have read as an interrupt storm.
`Waiter::parked` — the flag `/proc` already uses to tell a `park` from a `host`
wait — splits them into `wakes` and `unparks`, which costs one branch. `unparks`
is a number nothing else in the system exposes: pipe traffic has never been
observable.

**`frames` is exact rather than a share of `allocs`.** A `Task`'s promise
declares `get_return_object_on_allocation_failure`, so its frame is allocated
through the nothrow `operator new` and nothing else in the tree reaches that
overload. One increment there is precisely the coroutine-frame count, which is
the number §8.2 is about; `allocs` mixes it with every string and every heap
record. `fails` is on the same principle: every error in the system funnels into
`Err(NoMemory)` and this is the only place the count becomes visible.

**`ready` is a task suspended on nothing, not the depth of the ready queue.**
The tempting definition, `ready.size() - head`, is wrong here for a reason that
is easy to miss: `/proc/stat` is generated inside `open`, inside a syscall
server, inside the drain loop, so that figure would measure whatever happened to
be queued behind the reader in one drain — and would exclude the reader. The
definition used is the one `/proc/tasks` already prints as state, which makes
`ready + on_timer + on_host + on_park == tasks` an invariant a test can assert,
and makes `r` mean what `ps` prints as `R`. The consequence is that `vmstat` is
always in its own row — `r` and `p` are never zero while it is the one asking,
since its syscall server is runnable and its stepper is parked on the reply. BSD
subtracted itself out (`total.t_rq - 1`); that would be wrong here, because
`cat /proc/stat` reads the same file and is not the same shape.

**Counters live in `Sched`, gauges are computed there.** A file-static would
survive `sched_reset()` and accumulate across the fixed-order unit suite; a
member of the object the reset destroys is zeroed with it, which is what a test
starting from a clean scheduler means. The heap's counters are the other way
round — `heap_init` runs once — so `/proc/stat` mixes two zero points. That is
invisible in a running system, which has one `Sched` lifetime, and only shows up
in the test suite.

**No CPU column, and the group that replaced it says so.** Kernel-busy
milliseconds are measurable with two `host_now()` calls around the tick drain,
and were rejected: in a browser tab the answer is zero almost always, and as a
percentage of wall time it would read 99–100% idle — a column bought with a
hot-path cost that says nothing, and a label reading `cpu` would read as
retiring a documented gap (§4.2). `-loop-` is in its place: how often the host
granted the event loop a turn, which does vary, and collapses when the tab is
backgrounded. `-alloc-` similarly stands where BSD's `-page-` was, there being
no paging; allocations are this system's page faults.

**Seconds, which makes it the one time argument here that is not milliseconds.**
`sleep`, `watch -n` and `timeout` all take milliseconds, and consistency with
them was the first choice; BSD's units are the better one. The columns are rates
*per second*, so the interval that divides them wants the same unit, and
`vmstat 1` has to mean the useful thing rather than an interval that would
measure almost nothing but vmstat itself. Memory is in raw KB rather than `ps`'s
scaled units for a related reason: a `vmstat` column is scanned downwards, and a
unit that changes between rows defeats the only thing that makes scanning it
worth doing. BSD's `pgtok` does the same.

**What is deliberately absent.** `-i` would need every wake token tagged with a
source, and a token is opaque on purpose. `-m` is the interesting one —
`domem()` prints kernel malloc by bucket size and the allocator has exactly that
shape, `SIZE_CLASS[]` plus whole-span blocks — but it needs per-class counters
in the allocator's hot path, and it is a separate argument to make. `bi`/`bo`
were dropped because `sy` already exposes that traffic: bulk I/O is one syscall
per `SYS_CHUNK`. `-f`, `-t`, `-M` and `-N` have nothing to report: no fork, no
page-in timing, no core file, no namelist.

**The file is emitted a line at a time, and that is not a style choice.** `Buf`
truncates silently and `overflowed()` is called nowhere in the tree, so a single
buffer for twenty-three lines would have been a latent bug that returned the
moment someone added a counter — and because `stat` and `read` both work off the
same snapshot, they would have agreed on the short answer and no reader could
have told. A per-line `Buf<32>` holds the worst case, eight padded characters
and a space and twenty digits and a newline, and makes the file's total length
unbounded. The counter table is then the one place a counter is added, which is
also why `vmstat -s` prints an unknown name under itself rather than needing an
edit.

**`vmstat` measures itself**, about four syscalls per row: noise at a one-second
interval, and most of the reading at 100 ms. BSD has the same property and does
not correct for it either.

One test needed narrowing rather than fixing: `ls /bin | grep ta` asserted the
match was exactly `tail`, and `vmstat` contains `ta`. It is `grep tai` now.

## MEM is measured, and it rides back on the step

`ps` reported the memory *cap*, and every row read `16M` — one number, the same
for every process, which is a column that costs eleven characters and says
nothing. It reports what the instance has actually committed now.

The figure exists after all. The section below is right that nothing will report
a **worker's** memory — that needs `measureUserAgentSpecificMemory()` and
therefore COOP/COEP — but a process is not only a worker: it is a
`WebAssembly.Memory`, and a Memory says its own size. `buffer.byteLength` is the
pages the instance has grown to, read off the object rather than measured, and
it is exactly the RSS analogue `ps` wanted. Only the worker holds it, so only
the worker can read it — which is why `Memory.pages()` is in `web/host.js`
beside the other accessor that has to re-derive after a `grow`.

**It comes back on the step, not on an operation of its own.** A new `SvcOp` was
the obvious shape and is the wrong one twice over: `/proc` is generated at
`open` with nothing to await (§5.1), so an asynchronous query could not have fed
the file it exists for; and a step already carries a message each way.
`result_hi` in the reply record was unused by `ProcStep`, so the pages travel
there and `proc_step` takes a `u32 *pages` out-param the way `exec_process`
takes `bool *died`. No new operation, no extra round trip, and nothing at all on
the hot path — the kernel reads the field it is already being handed.

So the figure is as of the process's last step, and that is the most current one
there can be: a process grows its memory only while it runs, and it runs only
during a step. A process that has not finished its first step reports
`ProcMeta.initial_pages`, which is what the host sized the Memory to — the truth
then, rather than nought.

`/proc/<pid>` carries both, `mem` and `cap`, since a per-process ceiling is
worth having somewhere even when it is uniform; `/proc/tasks` carries both as
fields and `ps` prints only the first. The column scales like `df`'s, one
decimal below ten, because the numbers now vary: the shell reads `1.2M` and
`cat` `448K` where the cap column only ever said `16M` for both. `ps` asserts in
the suite that what init holds is neither nought nor the whole cap, which is
what proves a host figure arrived rather than a placeholder.

So the shell holds 1,310,720 bytes of its 16 MB and `cat` 458,752, and the
ceiling was never the thing worth watching — the difference between two programs
is.

---

## ps, and what a browser will say about a worker

The question that started this was whether the browser lets us look inside a
worker, so that `ps` could report what each one is doing. It does not, and the
answer is worth writing down because it looks like an oversight and is not one:

- There is no enumeration. No `navigator.workers`, no registry, no parent-side
  list. The only handle to a worker is the `Worker` object that created it,
  which is why `web/proc.js` keeps `procs` and `idle` itself.
- The handle is opaque: `postMessage`, `terminate`, `onmessage`, `onerror`. No
  id, no state, no start time, no exit code. `onerror` is the only involuntary
  signal, which is why `broke()` has to synthesise `STEP.TRAPPED`.
- There is no per-worker CPU time anywhere in the platform.
- The one memory API, `performance.measureUserAgentSpecificMemory()`, requires
  `crossOriginIsolated` — the COOP/COEP headers this system deliberately does
  without (§C.3), so that it hosts anywhere. Even where it is available it
  reports an agent-cluster breakdown, not a worker. `performance.memory` is
  non-standard and main-thread only.

DevTools sees workers through the CDP, which is not reachable from a page. So
*nothing* a browser offers would have helped, and none of it was needed: **the
kernel already keeps a better table than the platform could have given us.** A
worker is exactly a process, so "does this task hold a worker" is
`proc_find(pid)` succeeding, and `ps` costs no host round trip, no new import
and no ABI change at all.

### /proc/tasks, because a row per read describes a row per moment

`ps` reads one file. The alternative — list `/proc` and read `/proc/<pid>` per
row — costs two `postMessage` hops per syscall *and* breaks the rule the same
§5.1 paragraph states: content is produced at `open` so that a read cannot
describe two moments. Twelve rows read one at a time are twelve snapshots.
`/proc/tasks` is generated in a single pass, so the table is coherent.

Its fields are positional like `/proc/mounts`, with the cwd last so nothing
after it needs finding: a path may hold a space, and the shell does quote. A
name may too, so a space in one becomes an underscore — a name is a display
field, and the alternative was a quoting rule in a file two programs parse.

`/proc/<pid>` grew the same facts as named lines and its value column widened
from six to seven to fit `worker`. That is a visible change to a file people
`cat`, and the cheaper alternative was a cryptic four-letter name for the one
fact `ps` puts at the centre of its table.

### The columns, and the two that are honest about what they cannot say

`PID PPID NAME STAT WORKER WAIT CALLS FDS MEM ELAPSED CWD`. STAT is BSD's: a
state letter — `R` ready, `S` waiting, `C` cancelled — then `+` for the
foreground and `k`/`s` for the console claims. So STAT says whether a task is
suspended and WAIT says on what, with no redundancy.

**MEM started as the cap, not the usage** — `ProcMeta.max_pages`, which
`spawn_process` had been computing and discarding — on the reasoning that no
browser API reports what a worker holds. That is true of a *worker* and not of
an instance, and the section below corrects it: MEM is the measured figure now.
**There is no CPU column**: nothing meters one, and a runaway program is killed
rather than measured.

### WAIT needed a bit in the Waiter, because a token cannot say what it is for

WAIT was to come free from the queue an awaiter registers in — the timer queue
or the wake table. It does not. `Channel` parks with `sched_wait_token` and so
does a host call, and the stdio path parks a token of its own through
`park_receiver`. Every blocked task read `host`, including a stage blocked on a
pipe, which is the one distinction the column exists to draw: is this pipeline
waiting for data, or is the host wedged?

So the awaiter says so. `Waiter::parked` is set by `Channel`'s two awaiters and
by `Stream::Write` and `Source::Read`, where a non-null `park_` *is* the fact
that this is a channel. Four bytes per suspended frame against a column that
would otherwise have lied. A `sleep 5000 | wc` now reads `timer` for the
sleeper's server and `park` for the one blocked on the pipe.

### A syscall server is a task, and names the process it serves

Every outstanding syscall is its own scheduler job (§4.3), so they appear in
`ps` — a process named `wc` beside its server named `/bin/wc`. Left alone they
look like orphans, so `exec_proc_state` searches the `Call` records for one
whose `server` is this pid and reports its owner as PPID. It holds only while
the call is outstanding: once answered, the record is erased and what is left is
a coroutine finishing, which is what `/proc` then says about it. Recording the
owner on the job instead would have contradicted §3.3 — a job is a task, a name
and a `CancelToken`, and that is all the scheduler keeps.

`ps` is wide, so the suite widens the grid for it the way it already does for
`help`.

---

## df is a table

`df` printed a four-line key/value block — `backend`, `mode`, `quota`, `used` —
and then one prose line per mount. Every fact §5.3 asked for was in it and none
of it looked like `df`. It is the BSD table now:

```
Filesystem  1K-blocks     Used    Avail Capacity  Mounted on
opfs         10485760      487 10485273     0%    /
procfs              0        0        0      -    /proc
```

**Nothing was added to the ABI, because nothing had to be.** The two sources
`df` already used carry the whole table: `/proc/mounts` names each mount and its
filesystem, and `Sys::Storage` answers with the live quota and usage. A
per-mount `statfs` operation was the obvious shape and is the wrong one — there
is one store behind every writable mount, so the syscall would return the same
pair however many times it was asked, and Concept.md §4.3's table would have
grown an operation to say so. What the row needs from the kernel is which mount
is backed by the store, and the `kind` field has been saying that since M5.

So a mount whose kind is `opfs` reports the origin's figures, and two of them
would report the same ones. That is not a fudge: they *are* one store, and BSD
shows one device mounted twice the same way. Anything else answers from
`Fs::bytes()`, which is still declared and still unimplemented — `/proc` holds
no bytes and says `0 0 0 -`, which is the honest row rather than a missing one.

**The mode moved to the boot banner, and that amends a criterion.** M5's second
was "`df` reports quota, usage, and persistent versus best-effort mode". The
quota and the usage are the table. Durability is not a per-mount fact and had
nowhere to sit in one: it belongs to the origin, beside the quota, on the
`store:` line boot already prints. The backend went with it for a simpler reason
— `boot_filesystem` refuses to start without OPFS and sync handles, so
`backend opfs` was a line that could never read anything else.

The cost is real and is accepted rather than worked around. `persisted` is not
settled when boot reads it: the page sends a provisional best-effort answer
after a 250 ms grace period and the real one when the browser decides, and the
late one corrects `OpfsStore.persisted` in JS. `df` asking again was what made
that correction visible; a banner line is a snapshot, so a store that becomes
persistent a second after boot goes on saying `best-effort` until the next
reload. Putting the mode back in `df` to recover that would put a column in the
table that is the same in every row, which is the thing the table was written to
stop.

**`-h`, because the number is gigabytes.** `10485760` is not a figure anyone
reads; `10G` is. The scaling is a dozen lines in `df.cpp` rather than anything
shared — `text.h` has no formatter and nothing else wants one. It keeps a
decimal only below ten, so three significant digits carry the magnitude: `9.9G`,
`487K`, `0B`.

**Alignment is `Buf`'s now.** Nothing in the tree could right-justify a number:
`ls` does not align at all, `date` hand-rolls a two-digit pad, `/proc` pads its
keys with literal spaces in the string. `put_left`, `put_right(Str)` and
`put_right(u64)` are three members on `Buf`, which is a template — the kernel
links none of them. A value wider than its column is written whole and pushes
the rest of the row, because a truncated size is worse than a ragged line.

The widths are BSD's exactly, including the quirk where the capacity column's
header ends two places past its values. That puts the whole row inside sixty
columns, which matters here more than it does on a terminal: the test grid is
sixty wide and wrapping would have made every assertion a two-line one.

---

## The banner names the host, not the heap

The boot line read
`braam 0.2.65-6e62a37 — heap at 0x00030000, 64 KiB, 1 allocs, up in 200 us`.
Three of its four facts were allocator internals that `/proc/meminfo` already
reports in more detail, printed at the one moment nobody is asking about the
allocator. What was missing is the thing a browser makes genuinely hard to know:
what the system is running *on*. Nothing in `web/` read `navigator.userAgent`,
`hardwareConcurrency`, `deviceMemory` or `userAgentData` at all.

Now the version line carries the version and the boot time, and a block under it
names the browser, the OS, the architecture, the cores, the memory and the
store. The same facts are `/proc/host`, and `uname` reformats that file the way
`mount` reformats `/proc/mounts`.

The grid's own geometry is in `/proc/host` and not in the banner: it is a fact
about the screen it would be printed on, so it is the one line a reader can
already see the answer to. `/proc/host` reads it fresh on every open, which is
what makes it worth having there — it moves with the window, and a boot-time
number would be wrong by the first resize.

**Two constraints decided the shape, and neither was negotiable.** `init()` is a
synchronous export, so nothing in it can `co_await` a service — and it runs
before the host's first `resize()`, so `screen().cols` there is always the
placeholder 80. And `/proc` is generated synchronously: `generate()` returns
`bool` and `stat`/`list`/`open` all call it without awaiting, so a `/proc/host`
that asked the host on every read was never possible. Both point one way: ask
once from the boot coroutine, which runs on the first tick with the real
geometry in place, and keep the answer. A cache is not a compromise here — a
browser does not change under a running tab. The storage figures cost nothing
extra either, because `boot_filesystem` already awaits `storage_info()` as its
first act and has the quota in hand.

**What a browser will not tell you.** There is no `navigator.cpu`. A CPU model
and a clock rate do not exist in any browser API, and the two ways of faking
them were both refused: a microbenchmark measures the JIT and the coarsened
timer as much as the silicon, and the WebGL renderer string names a GPU, not a
processor. They are omitted. Cores come from `hardwareConcurrency`, which is
everywhere; architecture, OS version and full browser version come from
`userAgentData.getHighEntropyValues()`, which is Chromium's alone.

**No user-agent parsing, deliberately.** It would have filled the gap on Safari
and Firefox, and it would have lied: Safari's UA still says
`Intel Mac OS X 10_15_7` on Apple Silicon. A field nothing can answer is left
out, and the raw agent string is shown instead of an interpretation of it. That
is the whole reason the wire format has a blank line in the middle — above it is
what is short enough and useful enough for the boot grid, below it the locale
and the agent string, which wraps a row on its own and only carries information
when the interpreted fields are absent. Boot writes the half above; `/proc/host`
serves all of it. It also means the kernel never parses a field:
`next_line`/`next_field` live in `src/proc/io.h`, the process runtime, which the
kernel cannot reach — so the split is a single scan for `"\n\n"` rather than a
parser in `boot.cpp`.

**Two things `userAgentData` gets wrong if taken literally.** Chrome's `brands`
lists both `Chromium` and `Google Chrome`, and varies the order on purpose to
break sniffers — so taking the first non-GREASE entry names a different browser
on different loads. The specific brand is the one worth printing, so `Chromium`
is chosen only when nothing else is there. And Windows reports a
`platformVersion` that is not a Windows version: Windows 11 says `15.0.0`. That
mapping is published rather than inferred, so it is applied; UA-CH answers `0`
for 7, 8 and 8.1 without distinguishing them, and those get no number at all
rather than a wrong one.

**The formatting is the host's.** `describeHost()` in `web/svc.js` returns
finished `name value` lines, the value at column 9. The colon the boot banner
shows is added as it writes, by `write_labelled` in `boot.cpp`: it is
presentation, and putting it in the stored text would make `/proc/host` a worse
table — `uname` reads a field out of that file with `next_field`, and the name
would carry punctuation. It is also why `timezone`, the one name that fills the
column, gets its separating space from the writer rather than from the padding.
Which fields exist varies by browser, so composing them in the kernel would have
meant teaching it about `userAgentData`; this way `kernel.wasm` grew by the
price of one service wrapper. `HostInfo` sits with the other services rather
than after the process operations, which renumbered `PROC_SPAWN`/`STEP`/`KILL` —
safe because only `web/svc.js` writes those numbers down, the C++ side using
enum names and `test/fakesvc.mjs` importing the table.

**`uname -a` prints the block rather than one packed line.** There is no POSIX
contract here, and four fields is not what is worth reading about a browser.
`-s`, `-r` and `-m` are the classic three, and `-m` is `wasm32` — the one fact
neither the host nor the version supplies.

Storage stayed out of `/proc/host`. Quota and usage are live, `df` exists to ask
for them, and a cached copy would go stale. The banner names the quota alone for
the same reason: the usage is the half that moves, so a figure true only at boot
is the wrong one to leave on the screen all session, while how much room the
origin gave is a fact about the machine like the others around it.

The banner block costs three rows of the grid. On the 60×16 screen
`test/run.mjs` resizes to, boot occupies 13 rows of 16 — which is why the fake's
host string in `test/fakesvc.mjs` is two short lines, and why the motd is seven.
A longer one would push the greeting off the top, and the motd assertion is what
would catch it.

---

## One store, and rootfs.zip

Three filesystems held the namespace: `MemFs` for `/`, `BundleFs` for a
read-only `/bin` and `/share` unpacked from a fetched archive in linear memory,
`OpfsFs` for `/home`. Only `/home` survived a reload, every boot downloaded 491
KB whether or not anything had changed, and the files a user made outside
`/home` — in `/tmp`, in `/mnt/import` — quietly did not exist after one. Now `/`
is one `OpfsFs` and everything else is a directory in it. `kernel.wasm` lost 10
KB with the other two filesystems, but that is not why: three stores meant three
answers to every question about durability, and users only ever wanted one.

**Nothing above the VFS had to change.** `exec_resolve` reads a program image
with `read_file` and boot builds its directories with `vfs_mkdir`, so which
backend a path lands in was never visible from userland. That is the property
the mount layering was built for, and this is the first change that spent it.

**The unpacker moved to JS, and the format became a zip.** `BBND` existed
because a kernel had to parse it: 231 lines of `bundlefs.cpp` reading a table of
offsets into a blob it kept in linear memory, with a matching writer in
`tools/pack.py`. Once the archive is written into OPFS rather than mounted, the
only thing that reads it is `web/fs.js` — and there the browser's own
`DecompressionStream` and Python's `zipfile` do the work, so the hand-rolled
format bought nothing and cost two implementations to keep in step. `rootfs.zip`
is inspectable with `unzip -l`, buildable by hand, and 204 KB where `bundle.bin`
was 491 KB.

Deflating it moved what the size budget means. That entry exists so §4.4's
per-binary duplication "shows in one number", and a compressor hides precisely
that — so `size_budget.py` grew a directory branch and the budget now names the
*staging tree*, at the number `bundle.bin` used to report. The download got
smaller as a side effect rather than as a way of not looking at the total.

**The fetch is lazy, and the stamp is what makes it so.** The host writes
`/version` as the last step of an unpack, with the string the kernel passed —
its own `BRAAM_VERSION`, so the stamp cannot disagree with the kernel that asked
for it. Boot compares, and a match means the archive is never requested: the
steady state costs no download at all. Written last, so an interrupted unpack
leaves a stamp that does not match and is done again rather than believed.

A mismatch asks. The alternative was overwriting on sight, which is what a
service worker would do, but a stored `/bin` is the user's — they may have put
something there — and the prompt costs one line above a prompt they were about
to see anyway. Declining boots on what is stored; if those binaries are stale,
`exec_resolve` already says so by name ("built for another process ABI"), so the
failure explains itself rather than needing the boot path to predict it. The
prompt reads keys through `KeyInput`, which works because init spawns the
console pump before this task — the same claim a full-screen program takes, a
few hundred milliseconds earlier than anything else has ever taken it.

**`/bin` is writable now, and that needed an answer.** It used to be immutable
by construction: a read-only archive mount refused a write below the VFS. One
store means one `writable()`, and `rm /bin/sh` is reachable from the prompt —
with a matching stamp, nothing would ever put it back, and the origin could not
boot again. So `no_shell` offers the unpack a second time. The rule that comes
out of it is worth stating plainly: **the archive, not the store, is what the
system can be recovered from**, which is also why it is never cached into the
store as bytes.

A per-file read-only flag was considered and rejected. OPFS has no such thing —
`FileSystemHandle` carries `kind` and `name` and nothing else, and
`createSyncAccessHandle({mode})` chooses a lock, not a protection — so it would
have to be Braam's own metadata, in a manifest or a sidecar, with no inode layer
to hang a bit on and nothing to keep a user from rewriting whatever held it.
With one user and no privilege boundary it would protect against accident only,
which the restore prompt already does.

**No OPFS is fatal, and that retires a criterion.** M5's third was "with OPFS
unavailable, the system boots on `MemFs` and says so", and it was a good
criterion while `/home` was the only thing at stake. It is not one now: there is
no second store, so a memory namespace would be a system that looks like it
works and loses everything at the reload — the failure mode the line was written
to prevent, arrived at by a different road. Boot says what is wrong and starts
no shell. The criteria list keeps it, struck, the way M8's third is kept.

**What the tests lost, and where it went.** `test_memfs` and `test_bundlefs` are
deleted with their subjects. `test_vfs` needed a writable filesystem that
answers *without suspending* — `run_now` panics on a suspension and `OpfsFs`
awaits the host for everything but a read — so it grew a `TempFs` beside the
`ReadOnlyFs` and `CountingFs` it already had. A fixture rather than a component:
flat, no `df` accounting, and honest about it in a comment.

`test/run.mjs` drives everything synchronously, and there is no synchronous
inflate. Rather than make the driver async — which would reach every assertion
in it — `parseZip` is separated from `installOps`: the parser is async and runs
once during the driver's own setup, and the generator of operations is plain and
runs inside the import. Both stores drive the same generator, so neither has its
own idea of what an archive means. It also made the one check worth writing by
hand cheap: an entry named `../escape` is put in front of the parser and has to
be refused.
