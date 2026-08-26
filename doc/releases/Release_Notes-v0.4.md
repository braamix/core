# Release notes — 0.4

Reasoning, alternatives and trade-offs behind the code released as 0.4.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.4 — A system that can install software it was not built with

`BRAAM_VERSION_BASE` moves to 0.4; the commit count and the hash behind it carry
on unedited. 0.3 was a shell grown into a language over a filesystem that
answered about what it held — everything it could run, it had been built with.
0.4 adds the other half: `/bin/pkg`, twelve subcommands over signed
repositories, an Ed25519 anchor, an index whose command names the publisher
never writes down, a dependency solver, and a transaction that rolls back. A
store the system did not compile is now a place software arrives from.

**The move is the assertion, not the count.** Nothing between 0.3 and here forced
a new base: the commits are a package format frozen before a parser existed, a
verifier passed in rather than reached for, five stanza files read by one
reader, and the four commands that finished the set. Taken one at a time they
are a program being written. Taken together they change what the system *is* —
0.3's answer to "where does a new program come from" was "the tree it was built
from", and 0.4's is "a repository, if it is signed by something the anchor
names".

## Three files that were living in the wrong place

`/version` is now `/etc/version`, `/etc/pkg/anchor` is `/etc/anchor`, and
`/pkg/repositories` is `/etc/repositories`. The previous commit argued that
`/etc` is where this system's shipped configuration goes; these three were left
outside it, each for a different bad reason, and moving them makes `/` one name
shorter, `/etc` a flat directory of five files, and `/pkg` entirely
machine-owned.

**The stamp was at the root because nothing else was there yet.** `/version` is
one line the host writes at the end of an unpack and boot reads before the
shell. It is not something a user opens, and the root is the first thing anyone
types `ls` on. Two characters of a top-level namespace is a high price for a
file that exists to be compared against a constant.

**Moving it changes what an interrupted unpack leaves behind, for the better.**
`installOps` removes each top-level directory the archive carries before
rewriting it, and writes the stamp last. At the root, the stamp was outside
everything the unpack touched, so an unpack that died halfway left the *old*
version string standing over a half-written `/bin` — and the next boot read a
mismatch, printed `the stored image is X and this kernel is Y`, and offered a
choice. Declining kept an image that was not any version at all. Inside `/etc`
the stamp is removed with everything else and rewritten only if the unpack
finishes, so a half-written image leaves no stamp, and `unpack_if_stale` reads
an absent stamp as an empty store and finishes the job without asking. The
prompt now appears only where it means something: a complete image of a
different version. This was a consequence of the move rather than its motive,
and it is the sort of thing worth noticing before shipping rather than after.

**`/etc/pkg/` was a directory holding one file.** The anchor is the only thing
that was ever going to be in it. The extra component was there because the path
was written when `pkg` looked like it would bring several files with it, and it
did — but they went to `/pkg`, where the state belongs, and the one file left
behind kept a directory to itself. `/etc/anchor` says the same thing in four
fewer characters and one fewer `ls`.

**The repository list was in the store because that is where `pkg` writes.**
`/pkg` is deliberately the directory the archive does not carry, so that an
installed program survives a release (Package_Management.md §11). That argument
is about `pkg`'s *record* — the store, the database, the generations — which
can only be rebuilt by reinstalling and re-checking. It was never an argument
about the one line naming where to fetch from. That line is configuration: a
human types it, nothing accumulates in it, and it sits beside the anchor it is
checked against.

**So the URL is now re-pinned by a release, and that is the point rather than
the cost.** `/etc` is replaced wholesale at every version change, which is the
property §6 relies on for the anchor: it cannot be poisoned in the store for
good. The repository URL and the keys that vouch for its index are two halves
of one decision, and having a release restore one but not the other was the odd
arrangement. A user who points the system somewhere else has made an edit a
version change undoes — the same bargain as a locally trusted key, and §11
already documents it. What is gained is that `rootfs/etc/repositories` ships a
working URL, so `pkg update` needs no seeding on a fresh store, and a publisher
building their own braam sets it in the same step as the anchor.

**The tests moved with the paths, and two numbers moved with them.**
`test/unit/test_zip.cpp` compares the archive entry for entry against
`web/fs.js`'s reading of the same bytes and asserts the count: 44 became 45,
because `etc/repositories` is a new entry. `test/smoke/subst.mjs` asserts what
`wc` prints over three copies of `/etc/help`, so editing that document moved
`25548` to `25884` — the same hostage the previous commit collected, and the
comment beside it still says so. `ls /etc` stopped fitting on one row: five
names in columns down the grid rather than three across it, so the case now
compares a sorted set of names instead of an exact row, and the check that a
directory prints a trailing slash moved to `ls /`, which still has some.

## The directory was never shared with anybody

`/share` is now `/etc`. It is one of the two top-level directories `rootfs.zip`
carries, and it holds three things: `help`, which is the whole of `/bin/help`;
`motd`, which boot prints; and `pkg/anchor`, the root keys every repository
index is checked against. The name was borrowed from `/usr/share`, and on a
Unix that name means something precise — data a package installs that is
independent of the architecture, so that several architectures may *share* one
copy of it. None of that is true here. There is one architecture, wasm32; there
is no `/usr` to hang it under (§5.1 says why); and nothing shares anything,
because there is one store and one system in it. What is actually in the
directory is this system's shipped configuration and its one document, which is
what `/etc` has always meant.

**The shorter name is worth two characters six times over.** `rootfs/etc/help`
is written to 78 columns and read through `less` on an 80-column grid, and four
of its lines carried the old path. `/etc/pkg/anchor` also lets the `More` table
at the end of that document keep its column while the entries under it get
narrower. The document lost eight bytes, which is visible:
`test/smoke/subst.mjs` concatenates three copies of it and asserts what `wc`
prints, so `25572` became `25548`. That case is not about the help text at all —
it is about a pipe with eight slots and a drain that has to be running before
the wait — and the number is a hostage to a file it never mentions. It has been
one since the case was written and the comment beside it says so; renaming a
directory is just the first thing that ever collected.

**Two other `share`s in the tree are deliberately untouched.** The SDK installs
into `share/braam/examples` and `share/doc/braam` on the developer's real
machine, where `/usr/local/share` means exactly what it says on a Unix and this
rename has no jurisdiction. And a *package* may carry a `share/` subtree of its
own — `/pkg/store/<stem>/share/…`, which Package_Formats.md §10's tutorial
writes and the `g:` trigger globs in `test/unit/repo.data` match against. That
is the publisher's layout inside their own zip, not ours; a package built for
this system is free to call its data directory whatever a package on any other
system would. Renaming it would also have meant regenerating a signed fixture to
say nothing new.

**A store written before this commit keeps a `/share` nobody deletes.** The
unpack in `web/fs.js` removes each top-level directory *the archive carries*
before rewriting it, which is the property Package_Management.md §6 leans on —
the anchor cannot be poisoned in the store for good, because every version
change re-pins it. The same rule is why a directory the archive stops carrying
is never removed: `installOps` derives its removal list from the entries, and
there are no `share/` entries any more. An upgraded store therefore shows both
`/etc` and a stale `/share` in `ls /` until someone types `rm -r /share`. Adding
a hardcoded removal to boot was considered and dropped: it would be a line
naming a directory that has no other reason to exist in the source, kept for
ever against a case that stops occurring after one boot, and the alternative is
a command the user can already type. There is no upgrade machinery here and
adding one for a rename would be the wrong first customer for it.

## What the tab asked for, and what came back

A repository that would not answer gave three lines and no way to tell which of
several things had gone wrong:

```
$ pkg update
https://pub.sergev.org/braam
pkg: fetch: permission denied
```

Everything the browser knew was thrown away before it reached the screen.
`fetch_url` hands a process a `Fetched{ status, headers, body }`, `ProcHost::
open` kept the status and dropped the headers, and `fetch_capped` turns every
non-200 into `Error::NotFound` — so a 403, a 404 and a directory listing all
read alike, and a refusal by the browser reads as none of them. `pkg -v` prints
the two sides curl prints: `> GET <url>`, then `< <status>`, the response
headers a line each, and the body's size as it was counted off the wire.

**The trace lives in `ProcHost`, not in `update`.** Every fetch this program
makes — the index, and every package `install` and `upgrade` download — goes
through the three `PkgHost` methods `open`, `read` and `close`, so tracing them
there covers all of it and duplicates nothing. It also keeps the printing out of
`index.cpp` and `trust.cpp`, which compile into `tests.wasm` and may not make a
syscall; `host.cpp` is already the file that stays out. `PkgHost` did not have
to grow a parameter for the headers, because the layer that *drops* them is the
same layer that now prints them.

**Both lists are shorter than curl's, and that is the browser's doing.** A page
cannot see the request headers it sent — `fetch` composes them and reports
nothing back — so `> GET <url>` is the whole request, and it is honest: those
are the only headers `pkg` asks for. A cross-origin response exposes only the
CORS-safelisted headers to script, so `<` prints what the browser allowed
through rather than what the server sent. Saying so in `/share/help` costs two
sentences and saves a publisher an afternoon.

**Two hints that should not need a flag.** `curl` has said for some time that
`Err(Perm)` means *"the server did not grant cross-origin access"* and
`Err(Io)` means *"no answer"*; `pkg` printed neither, though it is the program
whose whole job is talking to a server somebody else configured. It prints them
now, unconditionally, in curl's words — a diagnostic nobody thinks to turn on
is a diagnostic nobody reads. The claim is earned rather than guessed:
`web/svc.js` reaches `E.PERM` only through a deliberate `mode: "no-cors"` retry
that the origin answered.

**Gated on the step, not on the error.** The first cut printed the cross-origin
sentence whenever a `Perm` surfaced, and the smoke suite caught it at once: a
package whose bytes do not match its recorded digest is `Perm` as well, and
telling that publisher about CORS would be a confident lie. So `pkg_net_hint`
takes the `IndexStep` and says nothing unless it is one of the two that reach
the network, `Fetch` or `Package`.

**`-v` is `pkg`'s, wherever it is typed.** `OptParse` stops at the first
operand by design, which would have accepted `pkg -v update` and rejected `pkg
update -v` — the spelling actually typed in the report. So `pkg_run` walks the
words once and filters the flag out, and what is left is the command's own
argv, unchanged operand checks and all. The same pass gives `pkg` the message
it was missing for a mistyped flag: `pkg -x` was `unknown command: -x` and is
now `unknown option: -x`. Nothing is lost to the filter, because a §6 token
cannot begin with `-`.

The fake service can model a status, a header block, a CORS refusal and a dead
network, so the smoke suite asserts all four through a pipe — `grep` rather
than the grid, since the trace is wider than sixty columns. What it cannot
model is a redirect: its routes are a `Map` with no `Location`. The real
repository in the report serves a 308, so that part of the story is still only
reachable with a browser.

## A table that says what its rows are for

`pkg` with no command printed its own name and then eleven more, run together
on one wrapped line. Every one of them was true and none of them was any use:
`files` and `list` and `verify` are not words that say what they do to a store,
and nothing in the line said which of them take an operand or what an operand
is. The descriptions existed — `/share/help`'s `Packages` section has had one
per command since each landed — but they are a page in another program, reached
by knowing that `help` exists, and the moment somebody needs them is the moment
they typed `pkg` and got told off.

**So the descriptions move into the table, which reverses a decision made when
the table was written.** The old comment said what each command is for belongs
to `/share/help` and not to a string in the binary, and the fear behind it was
two lists drifting apart. That fear was right about lists and wrong about
fields: `args` and `help` are columns of the same row as `run`, so a command
cannot be added without them, and there is no second array to forget. What
`/share/help` keeps is what it was always better at — the prose around the
commands, the store, the anchor, and what a generation is — and the binary
keeps the one line each that a mistake needs answered.

**The rows are printed in `/share/help`'s order rather than the alphabet.**
`update`, then the two that ask, then the four that change the store, then the
three that report, then `clean`: that is the order somebody meets them in, and
it is the order the manual already uses, so the two lists can be compared by
eye. Nothing depended on the sorting — `find()` is a linear scan over twelve
rows — and the "explicit" half of the old comment, which is about
`--gc-sections` never extracting an unreferenced archive member, is untouched.

**The column width is computed from the table, not written down.** A row whose
name and operands are longer than `install <package>...` moves every
description right; a shorter one costs nothing. This is the same reason the old
code built its line from the table rather than holding a second string, kept
through a change of shape.

**The block is written a row at a time.** It is around seven hundred bytes,
which is past what a coroutine frame may hold — a frame over 512 bytes costs a
whole 64 KiB span — and the alternative, a heap `String` the way `pkg list`
builds its output, would put an allocation failure on the path whose entire job
is to report a mistake. Twelve writes of a `Buf<96>` cost twelve syscalls,
which is twelve more than before and only ever on a path where somebody is
already reading the screen. The loop stops at the first write that fails, so a
`pkg | head -n 1` that closes the pipe ends the block rather than fighting it.

**`help` is a row, and `-h` and `--help` are that row's other spellings.** A
program that lists its commands should list the one that does the listing, and
a row is the only way it stays listed. The two flags are handled by rewriting
the first word before the table is searched, not by an `Opts` parse: `pkg` has
no options and this does not give it any — the first word is a command, and
these are two more ways of writing one of them. Being asked is not a mistake,
so all four of them — `pkg help`, `-h`, `--help`, and `pkg` with nothing after
it — print to stdout and exit 0. **A bare `pkg` is the fourth spelling of
asking, not a usage error.** Typing a program's name and nothing else is how
somebody finds out what it does; there is no wrong argument to report, nothing
was refused, and a shell script that would like the list can have it down a
pipe. What stays a mistake is a word the table does not carry, which prints
`unknown command` and the block on stderr and exits 2.

**`Usage:` is a heading now, and `pkg` alone in the tree capitalises it.** The
block has two of them — `Usage:` over the command line, `Commands:` over the
rows — because it is a page rather than a sentence, and a page wants headings
its eye can find. The eleven single-line usages the subcommands print followed:
`Usage: pkg install <package>...` and the ten beside it, so that whichever of
the twelve messages a mistake produces, it is recognisably the same program
talking. Every other program in `/bin` still prints a lowercase `usage:`, which
is the older and more Unix spelling, and the divergence is deliberate rather
than a migration half-done — `pkg` is the one program here with a surface big
enough to need a table of contents. What it cost is one sentence in
`/share/help`, which quoted `usage: …` as what a usage error prints and now
says what it prints instead of spelling it, since the two spellings differ.

**The smoke case had to stop reading the screen.** The block is fifteen lines
on a sixteen-row grid, so the usage line and the first commands have scrolled
off by the time the prompt is back, and an assertion on what is visible would
have been an assertion about the grid. The case now pipes — `head -n 2` for the
command line under the `Usage:` heading, `grep "install <package>"` for a row in
full — and reads the status from the unpiped form, since a pipeline's status is
its last stage's. That is length-independent, which the old assertion was not:
it named `autoremove` as the first command, and the order has just changed.

`subst.mjs`'s byte counts moved too, because they are three copies of
`/share/help` and it gained two commands' worth of lines. They are meant to
move; the comment beside them says so.

## Four documents, and the one sentence that was wrong in three of them

P28, and the end of `src/cmd/pkg/TODO.md` — which is deleted with it, the plan
having run from P1 to P28. What P28 asked for was `rootfs/README` and
`rootfs/share/help`; what auditing the four documents found was worth more than
what it wrote.

**The two user-facing documents had drifted in opposite directions.**
`rootfs/share/help` was current — a `Packages` section with all eleven
subcommands, written as each landed, and its two checked lists naming every one
of the forty `/bin` entries and all twenty-six builtins. `rootfs/README` was
untouched since before `pkg` existed and said *"Nothing is sent anywhere unless
you ask for it. **Two commands** do"*, which three now do. The difference is
that help has a test and README has almost none: `test/smoke/help.mjs` fails on
a builtin or a binary that is not named, in both directions, so help cannot go
stale in the one way anybody checked. Nothing checks whether a sentence is true.

**One wrong sentence had been copied into three files.** `README.md`,
`rootfs/share/help` and `CLAUDE.md` all said that `test`, `[`, `:`, `echo`,
`true` and `false` keep a file in `/bin` because their whole cost is the spawn.
Four of them do. `[` and `:` are punctuation nothing spawns and have never been
in `BRAAM_BIN_LIST`. The claim is right about *why* — a builtin shadows the name
at a prompt and not everywhere — and wrong about *which*, and it propagated
because each document was written by reading the last one.

**`README.md`'s filesystem paragraph was wrong three times in five lines.** It
said `/` lives in memory (it is `OpfsFs`), that `/home` is the only place files
survive a reload (everything does but `/tmp`), and that without OPFS the system
boots with memory only and says so — it says so and *stops*, since there is
nowhere to run. All three predate the package manager. A front page is the
document nobody re-reads while working, which is exactly why it rots.

**Two specifications still called `/bin/pkg` unbuilt.** Concept.md in two
places, and Package_Management.md in a bolded opening line. The rule is that the
spec is amended in the same commit as the code, and P26 and P27 both missed
these. Package_Management.md's line was worth keeping in substance rather than
deleting: that the policy was written before the code is the interesting fact
about it, and the sentence now says that instead of saying the code is absent.

**A `wc` in an unrelated test is what makes help's length somebody's problem.**
`test/smoke/subst.mjs` reads three concatenated copies of `/share/help` through
a command substitution and asserts `wc` over the result, because the size is
what makes it a many-writes case — forty-six chunks against an eight-slot pipe,
which hangs rather than fails without drain-before-wait. So editing the document
moves three numbers in a test about pipes, and it also outgrew the 120-row grid
`test/smoke/term.mjs` pages it on. Both are real couplings and both are
commented where they bite; a test that generated its own long file instead would
lose the property that the file is one a person maintains.

## One session, told in thirty-nine cases

`test/run.mjs` had reached 4,651 lines: 535 assertions driven through 427 shell
submissions, all inside a single `if (mode === "--kernel")` block 4,380 lines
long at one indent level, with no inner functions. Nothing else in the tree is
close — `test/unit/test_vfs.cpp` is 693 lines and nothing in `web/` passes 631 —
and the C++ suite sitting beside it had been 47 topic files behind one ordered
call list in `main.cpp` since M0. The smoke suite now has the same shape:
`run.mjs` is the `CASES` table and nothing else, `test/smoke/harness.mjs` owns
the kernel, the grid and the tracked cwd, and thirty-nine topic files hold the
assertions. [Testing.md](../Testing.md) is the document that was missing with
it.

**The obvious split is the wrong one.** A file per topic suggests a CTest case
per topic, and cost is no argument against it: the whole suite runs in under a
second, no real workers are spawned, and a fresh boot costs about 150 ms. The
argument against it is that *the suite is one cumulative session, and that is
the point of it*. A shell that has been running for four thousand keystrokes is
the thing under test. `/home/notes` is written two thousand lines before
`persist` reads it back; `pkg-install` leaves `/pkg` broken on purpose because
`pkg-remove` starts from that; `store.unpacks` is asserted as an absolute
running total at four different moments; `respawn` needs its blocks a second
apart or the kernel's own crash-loop guard fires. Isolating the cases would mean
rewriting the assertions rather than moving them, and would delete the coverage
that comes from long-lived state. So: many files, one process, one boot, one
list that fixes the order — and the order's dependencies written down beside the
entries, the way `test/unit/main.cpp` has always written them down.

`--upto=<case>` is named for what it does rather than for what would be nicer.
It is a prefix, not a filter, because by the paragraph above there is no state
from which a single case could start. It earns its place anyway: iterating on
one case without the output of the thirty after it.

**The move had to be provably behaviour-preserving**, and 67 lines of boot log
was too weak a signal for relocating 4,400 lines. The check used while it was
done was a temporary hook in `submit` that printed each command, its timestamp
and a digest of the resulting screen — 1,430 lines of fingerprint, deterministic
across runs once the boot banner's elapsed microseconds are stripped. Every step
was made to reproduce it exactly. The only deliberate divergence was four clock
cursors: `vt` and `gt` were each one running variable spanning several of the
new files, and the four chunks downstream of `gt` were re-based into the gap it
never reached. Nothing else about those cases changed, and the final output is
byte-identical to the output before the split.

The nine near-identical `*shows` helpers — `gshows`, `cshows`, `fshows`,
`tshows`, `rshows`, `sshows`, `ishows` were byte-identical bar the clock — are
one `shows(base, step)` factory in the harness. That is the only code that
changed rather than moved, and it is why the total is 5,185 lines across 42
files where a pure cut would have been longer.

## Nine attacks, and the two that had nowhere to be refused

P27, the end of Phase G. §3's table has been true since it was written and
mostly tested since P15; what this adds is the three cases that were not, and a
sentence about the four that read alike.

**Two fixtures had been sitting unused since the day they were made.**
`index.data`'s `@stranger` — an index signed by a key the anchor does not name —
and the whole of `anchor.data`, which `run.mjs` had never opened. The C suite
reads both, and what it proves there is that the *function* refuses. What only
`run.mjs` can prove is that the refusal reaches a person: that `pkg update`
prints it, exits 1, and records nothing. Those are different claims, and P27 is
the one that wanted the second.

**The counting rule is proven by a pair, not by a case.** `@repeat` meets
`H:root 2` with one key's signature written twice. On its own, a test that it is
refused proves only that *some* anchor with two `Y:` lines is refused — the file
is unfamiliar in several ways at once. `@short` is one signature under the same
threshold, and `@extra` is three signatures of which two count: that one is
*accepted*, and the refusal moves on to `pkg: signature:` because `anchor.data`
holds no key that signed `index.data`'s index. Three runs, three different
outcomes, and the difference between them is where the rule lives. Swapping
`@repeat` for `@extra` moves the message from `anchor:` to `signature:`, which
is the check that the case is load-bearing.

**Four of the nine attacks print the same line, and that is not a defect.**
`trust_meet` returns a bare bool, so a wrong signature, a key the anchor does
not name, a repeated signature and no signature at all are all
`pkg: signature: permission denied`. §7 step 4 is one check; the step name is
what says which check failed, and it does. Telling an attacker which way a
forgery failed buys nothing, and a reason code would be a second thing to keep
true. What keeps the tests honest is the pairing above rather than the wording.

**A dying tab needed a request that never happens.** `store.defer` was the
nearest thing and is the wrong shape: it performs the operation and withholds
the *reply*, so a deferred rename still renames. `store.stall` is the other
half — a predicate consulted before `perform`, so the matching request is
neither performed nor answered. Stall the rename of `/pkg/active.new`, then
throw the kernel away with `reopen()` and `instantiate()`, and the store is left
exactly as a tab that died there leaves it. It is ten lines, and it is the only
way to test the one step §8.3 calls the commit.

**What the dead tab leaves is a working generation, and `pkg clean` keeps it.**
The rename is the last operation, so everything above it — the store
directories, the records, `/pkg/world`, the generation directory and its whole
link farm — had already been written. After the reload nothing names it, so
nothing is installed and `hi` is `not found`; the retry then builds generation
*2*, because numbering runs past what is on disk rather than reusing a number.
`pkg clean` then keeps generation 1: it is the highest below the active one,
which is what a rollback swings back to, and nothing can distinguish a
generation abandoned mid-commit from one that was superseded. That is the right
answer rather than a missed collection, and the test proves it by swinging
`/pkg/active` back and running the command out of it. §8.3's "rolling back is
swinging the link back" now has a case where the thing rolled back to was never
committed in the first place.

## An anchor somebody can sign again

The rest of P26, and the end of it. `rootfs/share/pkg/anchor` names four keys
that exist: three root, two of them needed, and one index key, signed on a
machine this tree has never seen. The placeholder P13 shipped is gone with the
private halves it never had.

**`G:2`, and no chain leads to it.** §4's walk adopts an anchor when a threshold
of the *previous* anchor's roots signed it, and the previous anchor's roots were
destroyed the day it was made — so nothing can sign the step from 1 to 2. That
is not a gap: §6 says the anchor is re-pinned from `rootfs.zip` wholesale at
every version change, so replacing it *is* cutting a release, which is what §10
of Package_Management.md calls the out-of-band path and what it says the worst
case costs. The number still had to rise, because a client that walks anchors
compares `G` and nothing else orders them.

**Nothing about the build changed.** The anchor is a file in `rootfs/`, copied
by `copy_directory` and packed by `pack.py` like the other four, and no build
step reads a key or signs anything — `pip3 install cryptography` remains a
publisher's problem and not a builder's. Swapping the file was a swap.

**The expiry is 2029-01-01, and `run.mjs` is still the only thing watching it.**
Two years and a bit, which §9 asks be a period somebody can actually keep. The
smoke test reads the archive's `E:` against a real clock and fails the build
once it passes; every other test carries its own clock so that the fixtures do
not rot. It is a build-time alarm for a run-time expiry, and it is now armed
over a date somebody can act on.

## A name the publisher does not get to write down

P26 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), less the anchor. §6.1
of Package_Formats.md had been true since it was written and produced by
nothing: `tools/mkindex.py` read `.PKGINFO` and copied `p:` through, and the
fixture made up the difference with a hand-written `p:cmd:hi`.

**The derivation belongs to the publisher, not to `pkg` and not to `mkpkg`.**
Three places could compute a `cmd:` name. `pkg` is wrong because the index
stanza is what `/pkg/db` is written from (§8.1), so a name derived at install
time would exist in a solve against the index and not in one against the
installed set. `mkpkg` is wrong because `.PKGINFO` is the package's own claim
about itself, and §6.1's whole point is that a package *cannot get these
wrong*. `mkindex` opens every zip already, to hash it; the directory it needs
is beside the entry it was reading.

**A hand-written `p:` merges rather than losing.** `p` is one §6 list and §1
makes a repeated lowercase letter malformed, so the merge is a join, not a
second line — the declared value first, the derived names sorted after it. The
tool's field dictionary is one entry per letter, and that is the one place it
had to stop being one.

**An entry that cannot be written as a dependency token stops the index.** A
`bin/` name carrying a space or one of `< > = ~` would produce a `p:` line that
§6's reader splits somewhere else, and a silently wrong provide is worse than a
package that will not publish. It is the same refusal a `.PKGINFO` with no `P`
already gets.

**Dropping `p:cmd:hi` from the fixture is what made the test possible.** It was
unversioned, and §6.1's version clause is exactly that an unversioned provide
is never selected on its own — `is_provider_auto_selectable`. So `pkg install
cmd:hi` failed with `cmd:hi (virtual)` for as long as the fixture wrote the
name by hand, and a clause the solver had implemented all along had no case
over it. Now the name arrives with `=1.0-r0` on it and the install picks
`hello`, which pulls `libz`. No C++ moved: `dep_parse` never knew about the
prefix, `index_provides` walks provides already, and the solver compares the
provide's version. §6.1's last paragraph asked for exactly that, and the way to
confirm it was to add nothing.

**The key generator went into `ed25519.py` rather than beside it.** §10's first
step is "make a key" and nothing could be typed to do it: `generate()` was a
library call `mkrepo.py` reached into, and `openssl genpkey` writes PKCS#8
where `load()` wants thirty-two raw bytes. A `main()` in the file that already
holds every key operation keeps the count at one; a `tools/mkkey.py` would have
been a second file whose whole content is an import. It refuses to write over a
path that exists, since the failure mode of a key generator is a key replaced.

**§10 is a tutorial in a grammar document, deliberately.** The rest of the file
says what `pkg` reads; §10 is the only part addressed to a person, and it is
appended rather than inserted so that §9's number — cited from that document's
own preamble and from this file — stays where it is.

## What a transaction touched, and who wanted to know

P25, the last of Phase F. P24 left `.trigger` almost nothing to build — it was
already unpacked into the store directory and recorded, being a dot-entry that
is not `.PKGINFO`, and `script_run` already spawned a script by kind. What was
missing was the firing rule, and neither format document said a word of it: §3.2
had one row reading "trigger globs, space-separated" and §5.1 one line. So this
landed a rule as much as it landed code, and the rule is §5.1.1.

apk's `fire_triggers` is ported whole. The interesting part of porting it was
deciding what a *directory the transaction modified* means in a system that
deletes nothing.

**`/pkg/bin` is what a removal changes.** apk marks a directory modified when it
deletes files from it, so removing a font package wakes the font cache. Here a
removal writes nothing at all — the bytes stay in the store for a rollback, and
the generation simply stops naming them. The one thing that did change is
§8.3's link farm, and `/pkg/bin` resolves through `/pkg/active` to new contents.
Putting it in the modified set is not an extra rule bolted on; it is the same
rule pointed at the place where this system records that the installed set
moved. Without it a removal could never wake anything, and "uninstall the
package, the cache still lists it" would have no expressible fix.

**The `+` prefix was ported rather than dropped into §9**, and it is worth
writing down what it actually does, because the obvious reading is wrong. A `+`
glob does not mean "only fire on a change". It means *only hand over changed
directories* — a `+` glob matching an unmodified directory still wakes the
trigger, and merely withholds that directory from argv. So a freshly installed
package whose globs are all `+` runs its trigger with an empty argv rather than
not running. Two of the unit cases exist for exactly that, because it is the
kind of thing that gets quietly implemented backwards.

**The matcher had to learn about slashes.** `glob_match` is the shell's
per-component matcher and its `*` crosses anything, because `glob.cpp` walks the
components and never asks it to. A trigger glob is matched against a whole path,
so `/pkg/store/*` would have named every directory under the store rather than
one package. `trigger_match` splits both sides on `/` and matches component by
component, which is apk's `FNM_PATHNAME` and is what lets `*` stand for the
stem and nothing more.

**The rule is a pure file, not a loop inside `settle`.** `trigger.cpp` takes a
glob list, a fresh flag and a list of directories-with-a-modified-bit, and
answers whether the trigger fires and what it is handed. Three inputs, no
syscall, so it compiles into `tests.wasm` and the whole of apk's semantics —
first-glob-wins, `+`, absolute-only, fresh-versus-modified — is a table of cases
rather than something only an end-to-end test could reach. `install.cpp` is left
gathering the two directory sets and spawning.

The second set — every directory of every installed package — is read back from
the §8.1 records rather than remembered, and only when some package about to be
installed carries a `g:` at all. A transaction with no triggers in it does no
extra reads and asks no extra questions, which is the same shape as P24's stat:
the common case is a package that only places files, and it must keep costing
nothing.

## Six scripts, and the moment they run around

P24 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). Package_Formats.md §5.1
has named `.pre-install` and its five relatives since P14, and `zip_meta` has
returned a kind for each of them, but `unpack` wrote only `ZipMeta::Payload` —
so a package carrying scripts installed with them silently dropped on the floor.
They run now. Package_Management.md §11 needed nothing said that it had not
already said, which is what a policy document written before the code is for.

**A script is an ordinary file of the package, kept in the store directory.**
apk puts them beside the record, in `/lib/apk/db/<pkg>.<script>`; here they go
into `/pkg/store/<name>-<version>/` under their own dot-names with an
`F`/`R`/`Z` row apiece. They have to be kept somewhere — `pre-deinstall` runs at
a removal, when the archive is long gone — and the store directory is the one
place where keeping them costs nothing else. `pkg verify` re-hashes them, `pkg
files` lists them, `store_drop` and `pkg clean` collect them with the package,
and not one of those four learned a rule. The alternative would have taught `pkg
clean`'s stem scan about a naming convention, given `pkg verify` a blind spot,
and left the files unhashed.

The rule is every dot-entry but `.PKGINFO`, not the six by name. `.PKGINFO` is
excluded because §8.1's record supersedes it — that is §5.1's own reason for it
existing. The generalisation is what leaves P25 nothing to store: `.trigger` is
already unpacked and recorded, and only the firing rule is missing.

**The commit is the line between `pre-` and `post-`.** apk draws it at
extraction, and that boundary does not exist here. Nothing is extracted into
place: the unpack writes into a store directory nothing yet names, and §8.3's
rename of `/pkg/active` is the single moment anything outside the transaction
can observe. So every `pre-` script runs after every package is fetched, checked
and unpacked, and every `post-` script runs after the rename. The consequence is
worth stating: a `post-` script can run what was just installed, because
`/pkg/bin` points at the new generation, and a `pre-` script cannot. Drawing the
line where apk draws it would have marked a moment at which nothing happens.

**A failing script does not abort.** §11 already required that, and gave the
reason: it is what leaves `pkg verify` something to find. So a non-zero exit is
`pkg: <stem>: post-upgrade failed (1)` on stderr, a `b:` in the record, and a
transaction that carries on to its commit. The mark is written by reading the
record back and writing it again rather than folded into the staged text,
because `unpack` serialises that text while the archive is still alive and a
`post-` failure arrives long after — one path for both halves beats two.

`b` is lowercase, and §1 is why. An unknown uppercase letter makes a record
unusable; an unknown lowercase one is ignored. A future reader that does not
understand "broken" loses a warning, which is bad. One that refused the record
would lose `pkg list`, `pkg files` and the installed set `read_installed` hands
the solver, which is worse. Fail-closed is not automatically the safer choice —
it is only safer when what closes is smaller than what breaks.

The fixture is a package called `noisy` carrying all six scripts, each echoing
its own name and arguments, whose 1.1 `post-upgrade` exits 1 — one package for
the happy path and the broken one. It lives in two indexes of its own rather
than joining the existing pair, so every assertion written before it is
undisturbed by a package that exists to make noise. Its scripts carry no `#!`,
which is not an oversight: they are spawned as `/bin/sh <file>`, and the fixture
is where that stops being a claim.

## A namespace with no code in it

P23 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), which turned out to be
a paragraph rather than a patch. `cmd:awk` has worked everywhere it is *read*
since P14: `dep_parse` takes it as an ordinary name, `index_provides` finds it
under `p:`, and the solver resolves it through the provider machinery apk's own
fixtures exercise. What was missing was the rule saying where such a name comes
from — the whole of it was one clause of Package_Formats.md §6, "an ordinary
name whose providers ship an `awk`", which says nothing about which files
produce one, with what version, or who writes them. §6.1 is now that rule, and
the task is deleted having added no code at all.

**`bin/` was chosen so that one set serves twice.** `cmd:x` names an entry of
the package's `bin/`, flat, which is exactly what §8.3's link farm carries —
`store_commands` lists that directory and skips subdirectories, and `gen_ops`
makes one link per entry. So `cmd:x` holds precisely when typing `x` runs this
package's file. Any other rule would have been a second definition of "a
command this package ships", and two definitions of one thing drift.

That is also why the rule does not ask whether the entry is a *program*
(Concept.md §4). It could: a scanner can look for the `braam` section or a `#!`.
But the farm does not ask, so a `bin/` entry that is not a program is already on
`PATH` and already answers 126, and a `cmd:` rule that skipped it would make the
two sets differ in exactly the case where a package is already wrong. The
namespace should describe what the farm does, not correct it.

**The version is the difference between a name you can install and one you can
only depend on.** A name whose providers are all unversioned is virtual: there
is nothing for the solver to choose between, and `pkg install cmd:awk` refuses.
Emitting `cmd:hi=1.0-r0` — apk's answer, and what
[test/unit/solve.data](../../test/unit/solve.data)'s ported cases already assume
— makes the name selectable and makes `cmd:awk>=1.2` mean the providing
package's version. One `=` decides which of two commands the namespace is.

**The publisher derives them, not the packager.** A package cannot be trusted
to describe itself and does not have to be: the index is what vouches, and
§5.1 already requires only `P` and `V` to agree between `.PKGINFO` and the
stanza that signed for it. Since `/pkg/db` is written from the index stanza, an
installed package carries the derived names too, so a solve against the
installed set sees what a solve against the index saw — without a line of code
arranging it.

**The grammar landed a task ahead of the tool on purpose.** `tools/mkindex.py`
still copies `p:` verbatim and P26 still owns changing that. Writing the
definition first is the cheaper order: a producer with no written rule is how an
implementation quietly becomes the specification, and the two `cmd:` strings in
the tree are hand-typed into a fixture precisely because nobody had had to say
what they meant.

The one thing that did move is a row in
[test/unit/test_dep.cpp](../../test/unit/test_dep.cpp): §6.1 now mandates a
string form, `cmd:awk=1.2-r0`, and the table only covered the bare name.

## What a clean must not collect

P22 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), the last row of `pkg`'s
table, and the end of Phase E. `pkg clean` drops the download cache, the store
directories no kept generation names, and the generations below the one a
rollback swings back to. Most of it is bookkeeping. The part worth writing down
is what it *keeps*, because both rules were already decided elsewhere and this
is where they finally have to be obeyed.

**A generation survives when it is the active one, when it is above the active
one, or when it is the highest below it.** The first two clauses are one
sentence in `install.cpp` that has been waiting for this task: `next_gen`
counts from the highest generation on disk rather than from the live one,
because "a rollback leaves higher generations standing and P22 keeps them".
After rolling back from 5 to 4, generation 5 is the roll-*forward* target, and
a collector that ate it would make rollback one-way — which is most of what
P18 was built to provide. The third clause is the TODO's own: the generation
before the live one is what rollback swings *to*.

`gen_keep` is pure, in `db.cpp`, and has ten rows in
[test/unit/test_db.cpp](../../test/unit/test_db.cpp) rather than a walk through
the code, because the interesting cases are sparse numbering and a rollback,
and a table says which is which.

**No `/pkg/active` keeps everything, and that needs no branch.** With an active
of zero, every generation is above the active one, so the general rule already
answers "collect nothing" for a tree mid-rollback or one whose link somebody
removed by hand. A special case would have had to be right; this one cannot be
wrong.

**The db record goes with the store directory**, and not for tidiness.
`stem_state` compares the record's digest *before* it looks for the directory,
so a record left behind after its bytes were collected answers `Have::Other` —
"installed at a different digest" — for a package that is not installed at any
digest at all. The repository republishes that name-version, and an install
that should refetch refuses instead, permanently. The two files are one fact
about one package and they are collected as one.

**The cache goes whole**, which Package_Formats.md §8 already required, and §8's
own reason is why it costs nothing: a cached archive "is re-hashed against the
index every time it is used and never believed for being on disk", so what a
clean throws away is a download and never a check.

`Dropping` rather than `plan_verb`'s `Purging`: purging means a package left
the installed set, and nothing `clean` touches is installed — that is the
entire criterion for touching it. The name comes back out of the directory
name with `pkg_stem_split`, `pkg_stem`'s inverse, which splits at the first `-`
whose tail is a valid §7 version rather than at the first `-`; `version_valid`
was already there to ask. A directory nothing built keeps its own name in the
report instead of being silently mis-split.

Every row of the table is a command now, so `pkg <name> is not built yet` has
nothing left to answer for and its `smoke` case is gone. The branch stays: a
null `run` is no longer a state the table is in, but it is still what a row
added without its function would be, and the check costs one comparison in a
program that has just made a syscall.

## A digest read back, and what it still does not prove

P21 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). Every install since P18
has written a SHA-256 per file into `/pkg/db/<name>-<version>` — §8.1's `Z`
beside its `R` — and nothing had ever read one. `pkg files` prints the paths
that record names and `pkg verify` hashes the bytes behind them again,
reporting **missing**, **modified** and **extra**. `src/cmd/pkg/verify.cpp` is
both, and it is a file of its own because `query.cpp` says at the top that
nothing in it fetches, checks or writes. Re-hashing the store is a check.

**A clean `verify` prints nothing.** Output means trouble, which is `dpkg -V`'s
shape and `rpm -V`'s, and it is what makes the command usable from a script
without a parser: the status is the answer and the lines are the detail. `pkg
search` with no match already behaved this way, so this is the existing rule
rather than a new one.

**The paths are absolute.** apk prints a package's files relative to a root,
because on apk's system that root is `/` and the relative form is the useful
one. Here a package's files live under `/pkg/store/<name>-<version>/` and are
reached through a symlink farm; `bin/hi` would name nothing a reader could
open. So `files` prints what exists, which is also what `verify` names in its
own report, so a line from one can be pasted into the other.

**Extras come from a walk, not from a second record.** There is nothing to
compare a directory listing against except the record itself — which is exactly
the point, since a file that the record does not name is by construction one
nothing accounted for. Only files are reported: an unrecorded empty directory
is not evidence of anything. The walk is iterative, over a worklist of
directories rather than a coroutine per level, because a coroutine per level is
a frame per level and §2's rule about frames does not stop applying because the
recursion is shallow in practice. `file_matches` streams the file a chunk at a
time into the running `Sha256` for the same reason P6 put SHA-256 in wasm at
all: nothing large has to exist anywhere at once.

**The disclaimer had to reach the help text.** Package_Management.md §11 has
said from the start that the store is not tamper-evident: checking happens once,
at install, there are no file permissions here, every mount but `/proc` is the
one read-write store, and `rm /bin/pkg` already works. A command named `verify`
invites precisely the reading §11 denies, and a caveat that lives only in a
design document is not a caveat anybody reads. So `/share/help` gained a
`Packages` section — `pkg`'s subcommands, which
[pkg.cpp](../../src/cmd/pkg/pkg.cpp)'s table comment had been promising to that
file for six tasks — and the sentence sits under the command it is about.

What `verify` is worth, stated positively: it answers *did what I installed
change*, over an interrupted write, a store a `#!` script scribbled on, or a
generation someone edited by hand. It does not answer *can I trust what is
there*. The first question has an answer here and the second needs a privilege
boundary this system does not have.

`clean` is the one row of the table `/share/help` does not list, since a manual
that names a command answering "not built yet" is worse than one that waits.
P22 adds the line with the code.

Two functions moved down into `db.cpp` on the way: `installed_version`, which
was a file-local in `query.cpp` and is now what both readers of a generation
call, and `db_join`, `db_split`'s inverse. The rule that placed them is the one
that placed everything else in `src/cmd/pkg/`: pure goes down where
`tests.wasm` can reach it, and both have cases in
[test/unit/test_db.cpp](../../test/unit/test_db.cpp) — `db_join` tested by round
trip, since an inverse that is asserted rather than exercised is two chances to
be wrong instead of one.

## One transaction, because the signature covers one file

P20 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the last of Phase
E's commands that change the installed set. `pkg upgrade` is thirty lines of
arity check, `begin`, `SOLVE_UPGRADE`, `decide`, `settle`. The work was the
fixture.

**It is one solve and one generation, and that is a requirement rather than a
convenience.** §6's "the whole package set, signed as one file" is what stops
an attacker combining packages that never went together, and a loop of installs
would re-solve between each one and hand back exactly what the signed set was
protecting. The shape P18 built happens to be the shape that preserves it: one
`solve()` over all of world, one `store_perform`, one rename.

**`SOLVE_UPGRADE` and nothing else.** apk's upgrade branch sets the run-wide
flag and, given operands, clears it again and flags the named packages instead.
There are no operands here — TODO's table says `pkg upgrade` with no argument,
and "commit one generation for the lot" reads that way — so an argument is a
usage error. `-a/--available`, `-l/--latest` and `--prune` are not built
either. The flag rides on the `Txn` now and `decide` passes it, which is the
whole of the plumbing.

**An upgrade's changes are installs, so nothing new performs them.** They carry
a `new_pkg` and a real verb, so `settle`'s `realise` loop fetches, checks the
digest and unpacks each one, and a provider swap that arrives as a `Purging`
beside an `Installing` in the same changeset needs no case of its own. The new
version unpacks into a **new** store directory — `<name>-<new version>` — so
nothing the running generation is executing out of is touched while the
transaction is still able to fail.

**The version it came from stays.** Its store directory and its `/pkg/db`
record are what the previous generation names, and that generation is what a
rollback swings back to; `pkg clean` collects them at P22. Same rule as a
removal's, and the smoke test asserts both are still there after an upgrade.

**P18's different-digest refusal is out of reach while the record and the index
agree about a version** — which is the only state a working repository
produces. A bare upgrade sets no `SOLVE_AVAILABLE`, so a repository that
republishes an installed version under new bytes loses: `compare_providers`
skips its early `ipkg` rung under `SOLVE_UPGRADE`, but its last rung still
prefers the installed package once the versions compare equal. No `Replacing`,
so `stem_state` is never asked. That is what apk's `-a` exists to override, and
not building it is what keeps the store's immutability from being tested here.

It is worth saying what that does *not* cover, since the first draft of this
paragraph claimed more. Three rungs sit **above** the version comparison —
`selectable`, `deps_used` and `conflicts` — and they can differ between two
copies of one name-version when the *local record* and the index disagree about
metadata: an installed stanza whose own `D:` nothing satisfies is disqualified
by `disqualify_package`, and the index's copy then wins at an equal version.
That is a `Replacing`, and it does reach the refusal. The right answer is still
a refusal — §8 makes a store directory immutable and a rollback target may be
executing out of it — but it is reachable, and by exactly the hand-doctored
record P18's own suite plants.

**Nor does `pkg upgrade` fetch an index first**, for the reason `pkg install`
does not: §7's checks belong to the command that fetches one, and a command
that quietly refreshed the index would choose what to install from bytes nobody
asked for.

**The fixture gained a second index rather than a second version of
everything.** `tools/mkrepo.py` now signs `@index` (G:1, `libz-1.0-r0` and
`hello-1.0-r0`) and `@index2` (G:2, the same libz and `hello-1.1-r0`), so the
smoke test can watch an upgrade move one package and leave the other exactly
where it was — a set-wide bump would have proved less. `hello-1.1-r0`'s
`bin/hi` prints different text, so the link farm is checked by *running* it:
`hi` says the new thing without anything having been told a generation changed.

One line of that suite is worth more than it looks: **`pkg install hello`
against the new index does nothing.** Without `SOLVE_UPGRADE` the solver keeps
what is installed whatever the index now offers, which is the difference
between the two commands stated as a test.

`/bin/pkg` is 181,883 bytes, from 180,174; the staging tree 983,973 of 2 MiB.

## The purge was already written

P19 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). `pkg remove` takes a
name out of `/pkg/world` and re-solves; `pkg autoremove` re-solves without
taking anything out. Both commit a generation the way P18 does, and between them
they are about eighty lines, because the work is somewhere else.

**The solver already drops what world does not reach, so neither command walks
the graph.** `generate_changeset`'s three sweeps
([solve.cpp:1116-1144](../../src/cmd/pkg/solve.cpp#L1116)) emit a removal for
every installed package whose name has no requirer and no chosen provider, and
`cset_track_deps_removed` recurses into each dependency as its last requirer
goes. Fixture `basic14` — `del a` with world `a` — expects `Purging a` *and*
`Purging b`, and `basic4`, the same shape with `b` in world, expects only
`Purging a`. So reachability from world is the whole of what "explicitly
installed" means here, and `autoremove` is the re-solve with nothing else
attached. Writing a reachability pass would have been writing the solver twice.

Nor is there an uninstall step. `plan_installed` builds a generation from the
changes that carry a `new_pkg`, so **a removal is expressed as absence** — the
new `packages` and the new `bin/` simply do not name it. `realise` skips a
change with no `new_pkg`, which is exactly what a `Purging` is, so P18's commit
path took a removal without a line of change.

**A removal solves against the installed set alone, and that is a property
rather than an economy.** `SolveInput::repo` is left empty, which
[solve.cpp:427](../../src/cmd/pkg/solve.cpp#L427) makes harmless — `p.selectable
= p.available || … || p.ipkg`, so what is installed stays selectable with no
repository at all. What it buys: no change can name a package that is not
already unpacked, so **a removal can never fetch**, never consults
`/pkg/index`, and works with no network and no `pkg update` behind it. apk's
`del` re-solves against the repositories and may swap in a different provider,
which would have needed a fetch, a digest check and an unpack on a code path
whose whole point is that it destroys nothing.

The cost is one refusal that reads oddly: a world entry nothing installed
satisfies stops a removal with `ghost (no such package)`, which under a full
index means "the index does not list it" and here means "nothing installed
provides it". That is reachable by hand-editing world, so it has a test rather
than a fix; `pkg install` is what repairs a world the store cannot satisfy.

**`SOLVE_REMOVE` unseats a preference; it does not uninstall.** It suppresses
the installed-package rung of `compare_providers`
([solve.cpp:769](../../src/cmd/pkg/solve.cpp#L769)) so that a name something
else still needs is not re-picked out of habit — but if that name is still
required, its provider is still chosen. So `pkg remove libz` while `hello` needs
it changes nothing, and printing `generation 3, unchanged` alone would have
looked like success. It prints `pkg: libz: still needed by hello` first, before
the commit, which is apk's order.

That report can be computed at all because the changeset carries the
*unchanged* packages too — `cset_gen_name_change` records `(old, new)` even
when they are the same package, and `plan_verb` then returns an empty verb. So
the changes with a non-null `new_pkg` are the complete surviving set even when
nothing is printed, which is what makes both `plan_installed` and `survives`
correct.

**Exit 1, where `apk del` exits 0.** apk prints its not-removed report and
commits regardless, and the status says only whether the commit worked. Here a
named operand that could not be honoured is 1, which is what `pkg info
nonesuch` and `pkg install nonesuch` already answer. The pair does read oddly —
`pkg remove nosuch`, a name that was never installed, is a quiet 0, while
`pkg remove libz`, a name that is, is 1 — and that is the right way round: the
first did what was asked, the second did not. **World is rewritten either way**,
so the message says `; world updated` when it was: a name that stayed has
stopped being explicit, and would go at the next `autoremove` without it.

**A removal drops no bytes.** `/pkg/store/<stem>` and `/pkg/db/<stem>` stay
where they are, because the previous generation still names them and that is
what rolling back means. `pkg clean` collects them (P22), and P18's rule —
never `store_drop` on a removal — is what makes the rollback in the test
meaningful.

**Only an install builds `/pkg`.** `pkg_tree_ops` moved behind the same flag
that loads the index, because a removal writes nothing a generation has not
already created — and without the flag, `pkg remove nonesuch` on a machine that
had never installed anything would have created `/pkg`, four directories and a
dangling `/pkg/bin` in the course of doing nothing. On that tree the summary
also had to change: `store_active()` answers 0, and there is no generation 0,
so it says `nothing installed`.

**`world_drop` erases every line naming a package, not the first.** World is a
file people edit, and a name written twice would otherwise leave a line behind
that makes the package permanently unremovable — reported forever as still
needed, by nobody.

**`pkg install`'s head and tail became `begin`, `decide` and `settle`**, and
the record it carries is a `Txn` now rather than an `Install`, since three
commands share it. P20 is the fourth and should be a dozen lines. `/bin/pkg`
is 180,174 bytes, from 171,351 — two commands for nine kilobytes, most of it
their text — and the staging tree 982,264 of the 2 MiB the last entry raised
it to.

## One rename, and everything above it is a check

P18 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). `pkg install` is
Package_Management.md §7's steps 8 to 10 and then §8.3's commit: fetch each
package capped at the size the index gave, hash what arrived, and only then
unzip into `/pkg/store/`, build `/pkg/gen/<N>/` whole and swing `/pkg/active`
onto it. `solve.cpp`, `gen_ops`, `zip_read` and `store_commands` all get their
first caller.

**§7's crossing is a function, not a comment.** The rule — *nothing is
unzipped, written to the store, or run before its hash matches a hash from a
signed index* — is the whole point of the task, so it is `index_fetch` in
[index.cpp](../../src/cmd/pkg/index.cpp): it derives the URL from the header's
`N`, caps the body at `S` through the `ZipSink` the index fetch already used,
and returns `Err` unless `package_check` agrees on both the size and the
digest. It lives beside steps 1 to 7 rather than in `install.cpp` because
`index.cpp` takes a `PkgHost` and is compiled into `tests.wasm`, so the two
refusals that matter most — a byte changed after signing, a body longer than
`S` — are driven by `ctest -R unit` and not only by the browser harness. What
crosses the line in `install.cpp` is then two statements with the rule written
between them.

**The record is what a reinstall re-checks, so it decides three ways.** §7 says
`pkg` keeps its own record of which index version vouched for what, *so that a
reinstall re-checks rather than believing the disk*. `/pkg/db/<stem>` is that
record and `stem_state` reads it before any fetch: no record means rebuild, a
record whose `C` is the one the index gives now means there is nothing to do,
and a record whose `C` differs is a **refusal**. The third row is the one that
matters. A store directory is keyed by `<name>-<version>` and §8 makes it
immutable once written, so a repository that republishes a version under new
bytes would otherwise have `pkg` write over files a live generation is
executing out of. Refusing costs a rebuild nobody asked for and keeps the
property the task exists for.

**Rebuilding is safe because records are written last.** A `Remove` of a store
directory is only reachable when there is no db record for it, and a record is
only written at the commit — so no committed generation can name a directory
this run is willing to destroy. That one ordering is what lets a dead tab's
half-unpack be cleared without a rule about which generations are live.

**Nothing under `/pkg/db` is written before every package has unpacked.** Each
record is serialised into memory as its package finishes, because a `DbFile`
views the archive and the archive is released next, and the texts go out with
the commit. The commit is then one list — a `Write` per record, `/pkg/world`,
then `gen_ops` — performed by one `store_perform` that ends in the rename. A
failure before it leaves store directories nothing refers to, which is
`pkg clean`'s to collect, and `/pkg/active` where it was.

**`^C` is the tab dying, and that is not a gap.** System_Calls.md §8:
`Cancelled` never reaches a process, because the instance is dropped and
[rt.h](../../src/proc/rt.h) says a killed process never unwinds. So no cleanup
of `pkg`'s runs on `^C`, and the `Error::Cancelled → 130` mapping is convention
rather than machinery. The property the task asks for does not come from a
handler: it comes from the ordering, and it holds for a killed process, a closed
tab and a crashed one alike.

**World runs ahead of the generation, never behind.** `/pkg/world` is written
in the same list as the commit but before the rename, so a tab that dies
between them leaves a wish nothing fulfils — which the same command run again
fixes. The other order would leave a package installed that nothing explicitly
wants, and only `pkg autoremove` would ever find it. For the same reason an
install with nothing to do still writes world: making an implicit package
explicit must not be silent.

**The generation number counts directories, not the active link.** `N` is one
past the highest `/pkg/gen/<n>`. After a rollback — which §8.3 says is swinging
the link back — the active generation is not the highest one, and
`store_active() + 1` would have `gen_ops`'s leading `Remove` destroy exactly
the generation P22 is told to keep.

**The cache is re-hashed, not believed.** `/pkg/cache/<name>-<version>.zip` is
written *after* the digest matched, so no unverified bytes ever reach the disk,
and a later install uses it only when it hashes to what the index says now.
That departs from the task's wording, which had the download streamed into the
cache as it arrived; the archive is held whole for `zip_entries` either way, so
streaming bought no memory and cost the crossing its strictness. What it buys
instead is real: install, roll back, install again costs no network.

**`I` stops being decoration.** `S` bounds the compressed archive and says
nothing about what it unpacks to. `I` is inside the signed body and therefore
as trusted as `C`, so the payload entries' declared sizes must sum to no more
than it, and to no more than `UNPACK_MAX` when a stanza carries none — which
bounds a single entry too. `PACKAGE_MAX` bounds `S` itself at four megabytes,
since the archive is held whole and a process has sixteen.

**`.PKGINFO` cannot be a §3.2 stanza, and the format now says so.** `C` and `S`
name the archive and cannot be inside it, so `package_read` — which requires
both — would refuse every well-formed one. Package_Formats.md §5.1 gains the
sentence: `.PKGINFO` is required, carries §3.2's letters less those two, is
read field by field, and `P` and `V` are what must agree with the index, since
they are what choose the store directory and the generation's line. Comparing
the list fields as well would refuse a good package over a space.

**The refusal names the step, the way `pkg update`'s does.** `pkg:
libz-1.0-r0: digest: permission denied` and `pkg: libz-1.0-r0: package:
invalid` are one `Error` and two answers, and the word between the colons is
the difference — `IndexStep` gains `Package` and `Digest` for exactly that.

**`install.cpp` keeps its own printer.** apk's verbs are what a changeset
reads as, and `plan.cpp` holds them, but `test/unit/test_solve.cpp`'s
`render()` is left alone rather than rewired through the product: the fixture
format is a fixture format, and coupling 72 cases to a command's output would
make improving one of them break the other. `plan_verb` returning an empty
verb doubles as the test for whether a change is work, so the plan that is
printed and the work that is done come off one function and cannot drift.

**The installed set has to come out of `/pkg/db`, and that is not tidiness.**
`solve()` keys package identity on the digest, so stanzas synthesised from a
generation's name-and-version lines would all carry the same thirty-two zero
bytes and collapse into one package. A generation line with no readable record
is therefore a refusal naming the file — a store the database does not
describe is broken, and P21 is where that gets diagnosed.

**Four of P26's tools arrived early, because a signed happy path needs them.**
`test/unit/index.data`'s keys were destroyed when it was signed, so no package
could be added to it and no fixture could be signed under it. Rather than
regenerate that file — which would have meant re-deriving its eight attack
variants and every literal digest asserted over them — `tools/mkrepo.py`
writes a second fixture, `test/unit/repo.data`: its own anchor, one signed
index and two small packages, over throwaway keys generated in a temporary
directory and destroyed before it returns. §9 stays true, and `index.data` and
every refusal tested against it keep the bytes they always had.
`tools/ed25519.py` is the only thing in the tree that imports `cryptography`,
and only signing needs it — what is checked in was signed once, and verifying
is `Sys::Verify`.

**The packages are shell scripts, so the fixture is two kilobytes.** A `#!`
file is a program here, which `exec_resolve` chases, so `hello`'s `bin/hi` is
three lines and the smoke test can prove the whole of activation by typing
`hi` at a prompt: `/pkg/bin` is the second component of the default search
list, and nothing had to be told a generation appeared.

**`test/fakesvc.mjs` learns to answer with bytes.** A route body was
`TextEncoder`-ed, and a zip does not survive that. One line, in the shared
fake, so a package can be served at all.

**`/bin/pkg` is 171 KB, from 86.** `solve.cpp` and the newly reachable bodies
of `zip.cpp`, `unzip.cpp` and `db.cpp` are most of it. The staging tree is
973,441 bytes, which fitted the old 1 MiB budget with 75 KB to spare — and P19
to P22 all land on this one binary, so the next task would have broken the
build rather than reported anything.

**So the staging budget is 2 MiB.** Raising it is a deliberate act, and this is
the entry that says why: `/bin/pkg` is a package manager, and a package manager
is a solver, a version grammar, a stanza reader, a zip reader and a digest.
That is not §4.4's duplication, which is what the number exists to bound — the
duplication is the process runtime every binary carries, and it did not move.
One program grew, once, for a reason that will not repeat. The bound stays a
bound: 973,441 against 2 MiB is not room to stop watching, it is room for the
four commands that finish the one program this was raised for. `kernel.wasm`
keeps its 262,144 unchanged, which is the number that would mean something had
gone wrong.

## A solver with no way back, and 72 fixtures that say so

P17 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). `solve.cpp` is apk's
solver ported: discovery, unit propagation, one greedy decision at a time, and
a changeset in dependency order. It is pure — the index and the installed set
arrive as stanzas the caller holds — so it compiles into `tests.wasm` beside
`index.cpp` and a syscall in it is a link error. Nothing calls it yet; P18 is
its first caller, and `/bin/pkg` is byte-for-byte the size it was, because
`--gc-sections` never extracts an archive member nobody references.

**There is no backtracking, and `conflicts` is what replaces it.** Every
constraint that a package fails bumps a counter that only ever increases, so a
package disqualified once is disqualified for good and no decision ever has to
be unwound. What that buys is a solver with no search stack and no exponential
corner; what it costs is that a contradiction is *reported* rather than
retried. The fixtures are full of contradictions, which is the point: apk's
`error*.test` cases exist to pin down what a refusal says.

**The fixtures are the specification, so they came in as a file rather than as
prose.** `tools/mkfixtures.py` derives `test/unit/solve.data` from
`apk-tools/test/solver/` — 72 cases over 36 index and installed-db fixtures,
23 KB — the way `version.data` already carries apk's version suite.
Keeping the converter in the tree rather than hand-editing the output is what
makes the derivation auditable: every re-spelling below is one function in it,
and re-running it against a newer apk is a command rather than a merge.

**`@EXPECT` keeps apk's verbs and loses its printer.** The `(N/M)` counter is a
progress meter and `OK: 2 B in 2 packages` is a `du`; neither is a decision the
solver makes. What is left — `Installing b (2)`, `Upgrading app (1 -> 2)`,
`Purging libold (1)`, in apk's order — is exactly the changeset, and the order
is most of what is being asserted. An error block keeps its `ERROR:` header and
its package labels and drops the `breaks:`/`satisfies:` prose beneath them,
which would have meant porting apk's reporter, its second reachability pass and
its greedy wrap at column fifty for no gain in what is being proved.

**A label is a package that breaks a constraint, not one that failed.** The
first attempt marked every package the solver could not keep, and `error1`
answered with three labels where apk prints one. The reason is that apk's
reporter re-derives what breaks from the graph: the solver stops applying a
constraint once the name behind it has no options left, so `d-2.0`'s counter
never records the `d<2.0` that condemns it. `breaks_something` walks world and
the chosen set at report time instead, which is what makes `error1` one label
and `error3` two.

**Two re-spellings, and one of them was the whole of the last four failures.**
apk accumulates a repeated `D:`, `p:` or `i:`; Package_Formats.md §1 calls that
malformed and §6 makes the list space-separated, so the converter folds the
lines into the one spelling this grammar defines. And `C:` is apk's Q1, which
is SHA-1, where §1.1 takes SHA-256 alone — so it is restamped by hashing the
old digest, which keeps equal equal and distinct distinct. It has to hash the
**decoded bytes**, not the text. `installif1.repo` spells one digest two ways —
`…yysF=` and `…yysE=` differ only in the padding bits base64 discards — so
`libiif` collides with `bar` in apk's package table and never becomes a package
at all. Hashing the text separated them, and a package Alpine has never had
appeared in four expected outputs. That is also why `solve()` keys its own
table on the digest across the whole index and not merely across the index and
the installed set.

**`so:` turns out not to exist.** P17 said to drop the cases that only exercise
pinning or `so:`. Pinning is real and fifteen cases go with it. `so:` is not:
apk's sources contain no occurrence of the string, and `so:libfoo.so.1`,
`cmd:sh`, `pc:zlib` and `/bin/sh` are ordinary names whose colons and slashes
Braam's `dep_parse` already accepts, its name scan stopping only at `< > = ~`.
So nothing was dropped for it and all twenty-two `provides*` cases stand.

**The 47 dropped, each needing something a solver is not:** fifteen for
repository pinning; eight for apk upgrading itself; six for the `fix` applet;
four for `-t .virtual`, a package built on the command line; three for
`--no-network` or a cache index, which is availability masking; three for
`-v`'s verbose summary and its no-longer-available note; three for world
dependency spellings rejected before the solver is reached; two for
`--force-broken-world`; one for arch; one for an index dependency carrying
`@tag`; and one for `apk del`'s not-removed report. The generated file's header
lists them by name, so the drop list cannot drift from the code that applies
it.

## Three rows that only read

P16 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). `pkg search`, `pkg
info` and `pkg list` are what makes an update's result inspectable: two of them
read `/pkg/index` as P15 left it, the third reads the active generation. Nothing
in `query.cpp` fetches, checks or writes, and the solver lands next against a
tree where "what does the index actually say" is one word rather than `cat` and
a grammar.

**The stored index is read, not re-checked.** `index_read` is `signed_split`,
the signature block, §3.1's header and the packages — and none of §7's steps 4
to 6. That looks like a gap and is the opposite: the signature was checked when
the file arrived, and checking it again would say only that the file still says
what it said. §11 is explicit that an installed file carries no lasting
guarantee, and a `pkg info` that verified a signature would be claiming a
property the store does not have. The one check that survives is §3.1's `X`,
because a grammar this is not is a file that cannot be read at all rather than a
file that might be wrong.

**The pattern's shape picks the matcher.** A live `*`, `?` or `[…]` globs the
name and the description whole; anything else is a substring that ignores case.
Two behaviours behind one argument is usually a smell, but the alternative is
worse in both directions: glob-only means `pkg search awk` finds nothing until
you type `'*awk*'`, and substring-only means the metacharacters a user has typed
are matched literally, which nobody intends. apk resolves it the same way, and
the shell's `glob_meta` is exactly the question being asked, so the rule is one
call rather than a heuristic. Case folding applies only to the substring half —
a glob is the shell's matcher and the shell's matcher is case-sensitive, and
making it otherwise here would be a second dialect of one pattern language.

**A search that found nothing is 0, and an `info` that found nothing is 1.**
They look like the same event and are not. `pkg search nonesuch` asked a
question the index answered: no packages match, which is a result. `pkg info
nonesuch` named a package, and §7 step 7 is that a name the index does not list
does not exist and is not looked for anywhere else — so the answer is a refusal,
with the sentence spelled out rather than left to `errln`, which would have said
"no such file" about something that is not a file.

**`list` reads the generation, and only the generation.** Not the index, which
describes a repository rather than this machine, and not `/pkg/world`, which is
what was *asked for* rather than what is *there*. `store_active` follows
`/pkg/active` and `packages_read` parses what it points at, so the command
inherits §8.3's property for free: whatever the link names is a generation that
was committed whole. `info`'s `installed` row comes from the same two calls, and
is absent rather than "no" when nothing matches — an absent field is how every
other row in that listing behaves.

**The matcher is compiled into `braam_pkg` rather than linked from
`braam_sh`.** `match.cpp` is `Str` and `Span` and nothing else, which is what
already lets `tests.wasm` compile it; linking the shell for it would drag a
parser, an expander and a job table into `/bin/pkg`. `src/cmd/CMakeLists.txt`
had set the precedent for `/bin/test` and `cond.cpp`, and the exact-import
assertion is what keeps the shortcut honest — a pure file compiled twice cannot
move a binary's import list, and `smoke` fails if it does.

## One word, and the first row of the table filled

P15 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the first of Phase
E. `pkg update` reads `/pkg/repositories`, runs `index_check` over the one line
it finds, and writes the checked file to `/pkg/index`. Two commits' worth of
machinery had no caller; now it has one, and typing the word is the whole of
what a user has to do.

**The record is P15's because a refusal must leave nothing behind.**
`index_check` writes nothing at any of its seven steps, which is what makes "any
failure abandons the whole operation" free rather than a thing to unwind. So the
`Vec<StoreOp>` is built *after* it returns Ok, and `store_perform` is reached on
no other path. `StoreOpKind::Write` truncates in place, so a half-written
`/pkg/index` would be a floor nobody could read — and §7 step 5 now refuses one
of those, which closes the loop the two tasks make between them.

**A second repository is refused rather than ignored.** The format has always
said "one URL per line; today, one line", and the code says why the second half
of that is load-bearing: `/pkg/index` is one file and the rollback floor is one
number, so a second repository's index would be checked against the first's `G`
and whichever wrote last would become the floor for both. Taking only the first
line would hide that; refusing states it. The day there is somewhere to put a
second index, the refusal is the thing to delete, and Package_Formats.md §8 now
carries the reason so nobody has to rediscover it.

**A trailing slash is stripped, which is the one place normalising wins.** `pkg`
refuses rather than normalises everywhere else — base64 spellings, key names,
`X` versions — because two spellings of one *name* are two ways to be trusted.
A repositories line is not a name: `https://…/braam/` and `https://…/braam` are
one repository however they are typed, and the unstripped form fetches
`//index`, which answers 404 with nothing to explain it. Stripping turns a typo
into the obvious intent and cannot conflate two repositories into one.

**The step is the diagnostic.** `Err(Perm)` is a bad signature, an index for
another repository, a rollback and an expired index — four refusals, one error
value, because `Error` has fourteen members and §7 has seven steps. So the line
is `errln("pkg", index_step_name(step), err)`: `pkg: signature: permission
denied` and `pkg: expiry: permission denied` are the same error and different
answers, and the word between the colons is the whole difference. `Cancelled` is
130 and silent, the convention every blocking program in `src/cmd` follows.

**The smoke test plants an anchor over the shipped one, and that is the honest
way round.** The release anchor's private keys were destroyed the moment it was
signed (P13), so nothing can ever be signed *for* it — which is the correct
state for a system with no repository yet, and a wall for a test that wants a
happy path. `test/run.mjs` writes the unit suite's throwaway-key anchor into the
store after boot, routes the indexes already signed under it through `FakeNet`,
and puts the release's bytes back afterwards. What that proves is the pipeline,
the record and the refusals; what it does not prove is anything about the keys
in the archive, and the two should not be confused. The fixtures are
`test/unit/index.data` read straight from `run.mjs` — one set of bytes checked
by both suites, rather than a second set that could drift.

**`pkg update` is what makes the `/pkg` tree.** `pkg_tree_ops` was written for
P18 and is idempotent, so the first command a user runs is where the directories
come into being. `/pkg/bin` dangles until something is installed, which the
activation work already proved harmless: a miss through it is an ordinary 127.

## Seven steps in one screen, over a host it does not have

P14 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the end of Phase D.
`index_check` is Package_Management.md §7's steps 1 to 7 in one function and in
order: the time once, the anchor, the index fetched under a cap, the signatures,
the header, the version, the expiry, the packages. `IndexStep` names the step a
refusal stopped at, and every one of them has a test.

**One `PkgHost` replaced a function pointer written a commit earlier.** P13
injected its verifier because the split by purity did not fall where the split
by testability did. P14 needs five more of the same kind — a clock, a local
file, a fetch, a read, a close — and six parallel `using` declarations would
have been six ways of saying one thing. So `TrustVerify` is gone and
`trust_meet`, `trust_self`, `trust_step`, `trust_walk` and `anchor_load` all
take `PkgHost &`, which is `src/fs/`'s `Fs` pattern applied one layer up.
`anchor.cpp` became `host.cpp` and holds the concrete one; `anchor_load` moved
into `trust.cpp`, where the suite can reach it, and step 2's cases are the first
tests it has had. Rewriting a one-commit-old interface is cheaper than carrying
two.

**The fetch is `open`/`read`/`close` and not `fetch(url, cap)`, and the cap is
the whole reason.** A one-call fetch would have put the counting loop on each
side of the interface: the real one in `host.cpp`, unreachable from the suite,
and a fake one in the test that proves only itself. §3's endless-data row is
stopped by that loop, so it is the loop that has to be tested. Handing back an
opaque token — an fd for `/bin/pkg`, a table index for the suite — puts the loop
in `index.cpp` where the test drives it, including the two cases that matter:
exactly `INDEX_MAX` passes the fetch, one byte more does not, and the body is
closed either way. `ZipSink` was already this: `take` refuses a chunk that would
pass the declared size, which is a cap when `complete()` is not asked.

**512 KiB, because `Sys::Verify` stages the signed bytes whole.** The ceiling is
`SYS_STAGE_MAX` less the key, the signature and two length words — 1,048,472
bytes — and an index that cannot be staged cannot be checked at all. Half of it
leaves a margin nobody has to compute, and is some thousands of package stanzas.
"Well below" was the instruction; a number that needs arithmetic to see is not
well below anything.

**A `/pkg/index` that is present and does not parse refuses the run.** Absent is
a floor of zero, which is §8.2's "a file that is not there reads as an empty
one" and is what lets a `/pkg` that has never been written to need no seeding.
Unreadable is not the same thing: treating it as zero would mean anyone who can
write one byte into `/pkg` erases the rollback check, and §11 already concedes
that anyone can. A refusal there costs a `pkg update` that says why; the
alternative costs the property step 5 exists for.

**`X` and `N` are checked with the header, between steps 4 and 5.** §3.1 said
when `G` and `E` are checked and said nothing about the other two, and they
cannot wait for step 7: the version and the expiry are fields of a header that
has to be parsed to reach them. Package_Formats.md §3.1 now says so.

**The pipeline writes nothing.** §7's steps stop at reading the index; recording
it is the word "record" in P15's "fetch, check, record". Keeping `index_check`
read-only is what makes "any failure abandons the whole operation" free —
there is nothing to undo, at any step, because nothing was done.

**§1's two scopes survive into step 7.** A line that is not a field takes the
whole index down; a stanza with an unknown uppercase letter takes only itself
and the rest are read. That is the stanza grammar's rule rather than a decision
of the pipeline's, and the test asserts both halves so it stays that way.

## The anchor, and a verifier that arrives as an argument

P13 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the first half of
Phase D. `rootfs/share/pkg/anchor` ships in the archive, `trust.cpp` is what
checks one and `anchor.cpp` what reads the shipped one. Nothing fetches yet, so
this lands with no subcommand behind it — the way `Sys::Verify` did, and for the
same reason: the check is worth reviewing before anything depends on it.

**The verifier is a parameter, because the split by purity does not fall where
the split by testability does.** Every other pure piece of `pkg` is pure
outright, so `test/CMakeLists.txt` compiles it in and a syscall in it is a link
error. Threshold counting is not: it is arithmetic with one `co_await` in the
middle, and that `co_await` is `verify_sig` in a process and `svc_verify` in the
kernel — different names, different result types, the same question. Splitting
the module at that call would have put the loop in `braam_pkg`, where the unit
suite cannot reach it, and the "Done when" list is entirely about the loop. So
`TrustVerify` is a function pointer, `/bin/pkg` passes one adapter and
`test_trust.cpp` the other, and `fakesvc.mjs` answers Ed25519 for real. The cost
is one indirect call per signature. The alternative was a second copy of the
counting, which is the thing §7 step 4 says is easiest to get wrong.

**The loop is keys-outer, and the `break` is the rule.** For each key the anchor
names under a use, the signatures are scanned for one that verifies, and the
first that does ends that key's turn. Written the other way round — signatures
outer, keys inner — "at most one signature per key" becomes a set of names seen
so far, which is a second structure to keep correct and a place for a bug to
live. This way the property is the control flow: a key contributes once because
there is no path on which it contributes twice, and a signature repeated is a
key already counted. §2's "a `Y:` naming a key the anchor does not carry counts
for nothing" falls out of the same shape, since a signature nothing matches is
never reached.

**A key's name is recomputed, never read.** The `Y:` line's keyid is a hint for
a human; what the count uses is the SHA-256 of `<algorithm> <base64 key>` taken
from the anchor's own `K:`. That is what makes P6's strict base64 load-bearing
rather than fastidious — a decoder that accepted two spellings would give one
key two names and the threshold two ways to be met by one holder.

**Three things §4 did not say, and now does.** A higher `X` refuses the whole
anchor, which §3.1 had said only of an index. `E` is checked, against the time
§7 step 1 fixes, since an expiry nothing enforces is a comment. And every
anchor — the archive-pinned one included — must meet its own root threshold.
That last one proves nothing cryptographically: whoever edits the file edits the
keys with it. It was taken anyway because it is *one* code path with the chain
step rather than two, and because it refuses an anchor amended by hand after
signing, which is the accident a release process actually has. §6's "trust stops
at the anchor" is about *which keys*, and that is still believed without proof.

**`G` orders the chain and does not have to be contiguous.** §10's walk reads as
1 → 2 → 3, and the obvious implementation demands each step be exactly one. But
the step that matters is the signature: anchor 3 is adopted only when a
threshold of anchor 2's root keys signed it. Withholding 2 is refused by the
signature that is missing, not by the number that is — and requiring +1 would
additionally forbid a publisher who numbered 1 and 3, which nothing asks for.
So the rule is that `G` increases, and the test that anchor 3 is not reachable
from anchor 1 passes without it.

**A `K` of another algorithm is ignored; an `ed25519` one that is not 32 bytes
is fatal.** §8 keeps an algorithm name on every key precisely so a second one
can be added, so a `K:root ecdsa-p256 …` must be skipped rather than refused.
A key that claims the algorithm this reader implements and then is not that
algorithm's size is a different thing: it is malformed, and fail-closed is the
only reading. A malformed *signature* stays in the first camp, counting for
nothing, since a signature block is attacker-supplied on a chain step and
refusing the file would hand an attacker a way to stop a rotation.

**The shipped anchor's private keys were destroyed the moment it was signed.**
There is no repository yet, so its four keys are throwaway: three root, one
index, generated once outside the tree, the anchor signed two-of-three, and the
private halves gone. §9's "no private key in the git tree, in anything built
from it, or inside `rootfs.zip`" is then not a rule anyone has to keep. What
ships is a well-formed anchor naming keys nobody holds, so the first `pkg
update` to meet a repository will refuse it — which is the correct answer until
`tools/mkanchor.py` (P26) signs a real one. The file cannot be edited by hand
either, since the self-check is a real check; amending it means regenerating it.

**Its expiry is watched by `run.mjs` and by nothing else.** The anchor stops
working a year out, and no test with a fixed clock would ever say so — the unit
fixtures carry their own `E` and their own `now` on purpose, so that they do not
rot. The smoke test runs under Node against a real clock, so that is where the
archive's anchor is read and its `E:` compared against `Date.now()`. It is a
build-time alarm for a run-time expiry, which is the only place the two meet.

**`/share` gained a directory, and two counted things moved.** `ls /share` is
three names now, `pkg/` marked as a directory, and `rootfs.zip` is 44 entries
rather than 43. Both were assertions rather than incidental — which is what they
are for.

## Activation is one constant

P12 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the end of Phase C.
`SYS_PATH_DEFAULT` is `/bin:/pkg/bin`, init plants the same list, and a
`static_assert` holds the two together. That is the whole of it: `exec_resolve`
is untouched, no clause was added to command resolution, and the kernel still
knows none of `/pkg`'s file formats.

Concept.md §4 argued this before there was anything to activate, so what is new
here is that it is done — and the rest of this entry is what the argument turned
out to be worth once it was.

**The four ways it can be broken all cost nothing.** A missing `/pkg`, a
dangling `/pkg/active`, a farm entry pointing into a store directory that is not
there, and a name no generation lists are each a `PATH` component that finds
nothing. `exec.cpp` already continued on `NotFound`, `NotDir` and `IsDir`,
already answered `Err(NotFound)` and 127, and already had a test saying so. The
smoke test now walks all four and gets one 127 each. **Not one of them is a new
failure path**, and that is the property the fourth clause would have spent: a
clause reading `/pkg/active` and a generation file on every failed lookup has
four ways to be half-installed, and each of them has to be turned back into an
ordinary "command not found" by hand.

**Boot does not create `/pkg`.** `make_dirs` still makes `/home` and `/import`
and nothing else. A system that never installs anything never grows a `/pkg`,
and the tree is P11's `pkg_tree_ops` to build on the first write. Creating it at
boot would have put an empty directory and a dangling `/pkg/bin` on every
machine to buy a component that already costs nothing when it is absent.

**`init`'s `PATH` and the kernel's default were two literals.** `base_env` in
`boot.cpp` spelled `PATH=/bin` out rather than deriving it, so the two could
drift and nothing would have said which was right — a shell entered with one
list while `env -i` searched another. `Str::substr` and `operator==` are both
`constexpr`, so one `static_assert` on the word after `PATH=` makes that a
compile error. It is checked: reverting the constant alone fails the build in
`boot.cpp` rather than passing the tests.

**The smoke test builds a generation the way `gen_ops` emits one** — absolute
targets, `/pkg/bin` to `/pkg/active/bin` to `/pkg/gen/1/bin` — and plants a real
binary in the store rather than a fourth symlink, since a store directory holds
a program. What it then proves is that the resolution is the kernel's: `hi` runs
with no `PATH` set by hand, and so do `timeout hi` and `sh -c 'hi'`. It also
pins the two rules that make the arrangement safe rather than merely working.
`/bin` wins for a name in both, checked with `wc` rather than `echo`, which is a
builtin and would have shadowed the pair of them. And **`PATH` is a default and
not a floor**: `PATH=/home hi` is 127, so a process handed a `PATH` searches
that list alone, installed programs included — which is what a `PATH` that
steers resolution has to mean.

**`/pkg` survives a version change**, beside the `/bin/keepme` that proves the
opposite for `/bin`. The unpack names only the top-level directories the archive
carries, and Package_Management.md §6 already leans on that from the other end
to say the trust anchor is re-pinned at every release. Read forwards it says a
release replaces the system and leaves what `pkg` installed standing.

## A generation, as a list of steps

P11 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). `db.cpp` is
Package_Formats.md §8 — the paths under `/pkg`, §8.2's two text files, and what
committing a generation *is*. `store.cpp` is the half that goes and does it.

**The split is the only way any of it could be tested.** `tests.wasm` cannot
supply `sys`/`sys_async` and is not allowed to: `braam_proc` is deliberately not
linked, so a syscall in a source compiled into the suite is a link error. A
`db.cpp` written as a coroutine that walks and writes would have put the commit
order — the one part of this that must not be got wrong — where nothing could
look at it until P18. So the ordering is a value: `gen_ops` returns the eight
steps, `store_perform` runs them, and the test compares the list against the one
§8.3 now spells out.

**Two things in the tree already work this way**, which is why it is not an
invention. `installOps` in `web/fs.js` is "what installing an archive *is*, as
operations for the caller to perform: OPFS awaits each one and the test fake
does them synchronously, and neither has its own idea of what the archive
means". And `test` is cut the same way for the same reason — `cond_probes`
names every file primary in one walk, `cond_eval` reads the answers back, and
`condrun.cpp` is the half that has to go and look. `db.cpp`/`store.cpp` is that
pair with the halves named for what they are rather than for the syscall.

**What is left unproven, and by how much.** `store.cpp` has no test, because no
subcommand reaches the store yet and the in-wasm suite cannot run a program.
What makes that tolerable is that it is a switch with nothing in it: each op is
one `proc/io.h` call the smoke test already covers through a program of its own
— `make_dir_all` through `mkdir -p`, `remove_path` through `rm -r`, `make_link`
through `ln -s`, `rename_path` through `mv`, and the open/write/close through
`edit`. There is no logic left in `store.cpp` to be wrong about, and P18 is what
gives it a caller and `test/run.mjs` a way in.

**The links are absolute, and the reader takes either.** A relative target would
buy a `/pkg` that could be moved, and nothing will move it: `/pkg` is a fixed
top-level name in Concept.md §5.1, and the kernel's default `PATH` names it by
that name at P12. What absolute buys instead is that `ls -l` shows the same
string the code wrote, which is worth more in a directory nobody can debug with
a package manager. `gen_of` is liberal anyway and reads `gen/2` as readily as
`/pkg/gen/2`, because P12's test builds that link by hand and a link put there
by a person is still a link.

**`Err(Unsupported)` from the commit rename is a failure, not an instruction.**
`rename_path`'s contract says the store may refuse to move a directory or a name
across mounts and that the caller should copy instead — `mv` is built on that.
It does not apply here: `/pkg/active.new` is a symlink beside its own
destination, so a refusal is a broken store rather than a hint, and treating it
as one would turn the single atomic step this whole arrangement exists for into
a copy that can be interrupted half-way.

**A missing file reads as an empty one.** `world` and `repositories` are absent
until something writes them, and the alternative — seeding them when the tree is
made — puts a `/pkg/repositories` with nothing in it on disk to mean what its
absence already meant. §8.2 gained the sentence. The same paragraph settles
that a last line without a newline is still a line, which is §9's rule about
apk's dropped final stanza applied to a second file format.

**The writer sorts.** §8.2 says `packages` is sorted by name, and P9 settled
that order is the writer's concern so that a round trip is defined; so
`packages_write` sorts rather than trusting a caller who will be handed the
solver's output in dependency order. It is an insertion sort over indices, which
is the right algorithm for a list as long as the number of packages a person has
installed.

## A second reader of one format, and the ceiling only it can see

P10 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the last Phase C
primitive. `zip.cpp` is Package_Formats.md §5.2 — the end record found behind
its comment, the central directory walked, each entry's data found by re-reading
its
*local* header — and §5.1's dot-entry split. `unzip.cpp` is the twenty lines
that put an entry through `Sys::Inflate`.

**The module splits in two so that the rules can be tested at all.**
`test/CMakeLists.txt` compiles pkg's pure sources straight into `tests.wasm` and
does not link `braam_pkg`; a syscall in one of them is a link error, and that
link error is the assertion. Everything in §5.2 is arithmetic over a byte
buffer, so `zip.cpp` joins that list beside `stanza.cpp`, and only the
`inflate()` loop is exiled to `unzip.cpp`. Had it been one file, every rule the
task exists to pin down would have lived where the unit suite cannot reach it,
and the test would have re-implemented them — the same two-readers failure one
level down.

**The ceiling is on the pure side for the same reason.** `Sys::Inflate` caps its
input at `SYS_STAGE_MAX` and not its output, and answers with a descriptor
rather than a buffer, precisely so a bomb can be abandoned part way — but
nothing below `pkg` knows how big the entry claimed to be. So `ZipSink` holds
the declared size and refuses the chunk that would pass it, and a clean end of
stream short of it is `Err(Invalid)` and never a short read. It is thirty lines
in `zip.cpp`, tested directly, and the two loops that feed it — `unzip.cpp`'s
over `read_chunk`, the test's over `stream_read` — carry no policy between them.
The two halves even spell the end of a stream differently, `Err(Closed)` against
an empty chunk, which is a second reason not to have one loop pretending to
serve both.

**The declared size is trusted and the stream is not.** That sentence is the
whole of the rule, and it holds because the size comes out of an archive whose
digest has already been checked against a signed index (Package_Management.md
§7) while the inflated bytes come out of a decompressor. §5.2 gained it, along
with the `SYS_STAGE_MAX` ceiling on an entry's *compressed* size, which is a
real limit: an entry `pkg` cannot stage is one it cannot read, and refusing is
the only honest answer.

**`web/fs.js` did not move, and the document says why.** `parseZip` never reads
the uncompressed size at `+24`, and three lines would have made the two readers
identical. It stays as it is: `DecompressionStream` hands back a buffer, so the
check could only run once the bomb had been materialised, defending nothing —
and the one archive it reads is the release's own, packed by `tools/pack.py`
three lines away. The package reader reads archives it did not build. §5.2 now
records the asymmetry as an exception with its reasoning, so the next person to
diff the two does not "fix" one of them.

**§5.2's bullets became an order rather than a list.** Writing the second reader
turned up two places where the prose did not say enough to reproduce the first:
`parseZip` skips a name ending in `/` *before* it checks the name, so `../` is
skipped and not refused, and it judges the method *after* the local header has
been found. Both were accidents of the order the JS happens to be written in,
and both are now normative — a reader that reorders them refuses archives the
other accepts, which is exactly the class of disagreement §5.2 exists to
prevent.

**The archive comes along to the unit suite now.** `rootfs.zip` was already
passed to `smoke` so the packer and the unpacker check each other; it is passed
to `unit` for the same reason one level up. `test/run.mjs` plants the raw bytes
and a `<name> <size> <sha256>` manifest built from `parseZip`'s own output into
the fake store, and `test_zip` mounts `OpfsFs`, parses the same bytes in wasm,
inflates all 43 entries through the kernel's own service and compares every
digest. A single wrong byte in one entry fails it by name. The manifest is
digests rather than bytes so that 812 KiB of archive is never held twice.

The rest is fixtures: a zip builder in C++ with the fields a refusal needs to
lie about, since `tools/pack.py` always deflates, writes no directory entry and
no extra field — so **the local-header re-read, the bug §5.2 calls the classic
one, is invisible in `rootfs.zip`** and provable only against an archive built
to expose it. An entry whose local header carries seven bytes of extra field
that the central record does not is the whole case, and a reader that trusted
the central offset lands seven bytes into the data.

**No new command, so nothing in `/share/help` moved**, and `bin/pkg` did not
grow: nothing in `pkg.cpp` references `zip_read` yet, so `--gc-sections` drops
both members and the staging tree is where it was. P18 is what will link them.

## Size stops being a headline

The budgets stay; the noise around them goes. `tools/size_budget.txt` still
names 262,144 bytes for `kernel.wasm` and 1 MiB for the staging tree, the
kernel's `POST_BUILD` still checks the first, and the `size` case still checks
both. What changed is everything written *around* that check.

**CLAUDE.md led with it.** "Four things must never regress" named the two
numbers before it named the wasm ABI, which puts a byte count above the
interface every part of the system is written against. It names two things now.
The `size` bullet lost "raising a number is a deliberate act" — the budget
file's own comment says that, and saying it twice made it a rule about conduct
rather than a line in a config.

**§4.4 was arithmetic with a shelf life.** The duplication argument is the
durable part: no dynamic linking, so every binary carries its own allocator,
string types and coroutine runtime, so keep the process-side runtime minimal and
push anything substantial into a syscall. "~710 KiB over thirty-six binaries,
and `sh.wasm` is 214 KiB of it" was true when it was written and is not now. So
went README.md's "the kernel is 148 KB" in both places, Programming_Manual.md's
6 KB hello, and System_Calls.md's "~400 KB where four binaries once cost 47 KB".

This finishes an argument the file has already made twice. M3 raised the ceiling
from 32 KiB to 256 KiB and recorded that "a ceiling that has to be raised every
milestone measures nothing". Writing System_Calls.md took the exact counts out
of README.md and CLAUDE.md because "a pinned figure in a living document is
stale by the next one" — and replaced them with *rounder* pinned figures, which
went stale on schedule. The remedy is not a better number; it is no number.

**P28 of the `pkg` TODO lost its first half.** It told whoever writes `pkg` to
measure it against the budget and to justify raising the `rootfs/` line in this
file. If the staging tree ever exceeds 1 MiB the `size` case says so at the
moment it happens, and nobody needs telling in advance to worry about it. P28 is
documentation now.

**What did not move.** The 512-byte coroutine frame and the 64 KiB span behind
it are mechanism rather than budget — cited from some two dozen source comments,
and the reason `FS_BLOCK` and `SYS_CHUNK` are what they are. `SYS_STAGE_MAX`,
the 16 MB process cap, the package and index caps in the `pkg` documents and the
runtime storage quota are protocol limits. CI still reports every binary into
the job summary under `--report`, measured and not bounded. And the milestone
entries below stand as written: the figures they quote are a dated snapshot,
which is what this file is for.

## One reader, five files — and a `Field` that was already taken

P9 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the end of Phase C.
`stanza.cpp` is Package_Formats.md §1: a line is a letter, a colon and a value;
an empty line or end of file ends a stanza. Over it sit the typed records §2,
§3, §4 and §8.1 define, and the writer that puts a `/pkg/db` record back
byte-identically.

**The typed records land together rather than with their consumers.** P13 wants
the anchor, P14 the index header, P18 a package stanza, P11 the installed-db
record — four tasks, and writing four readers is exactly what §1 was written to
prevent. What makes one reader possible is that a letter means one thing in
every file, so the only per-file thing is *which* letters are allowed. That is
one `Str` of known letters passed to the reader, and the five of them sit
together in `stanza.h` where they can be read down a column.

**`Unusable` and `Malformed` are different because their scopes are.** §1 says
an unknown uppercase letter makes the *record* unusable — that record, not the
file — and scopes nothing else. The reader takes the silence seriously: a
repeated letter outside the accumulating six, and a line that is not a field at
all, take the file down, because a file whose framing cannot be trusted has no
records to salvage. §1 gained the case the code could not avoid answering: a
*known* letter whose value does not parse, or a required field that is absent,
is the record's problem and not the file's, on the same reasoning.

**§3.3 said two things that could not both be true**, and one of them is now
gone: the writer emits `C P V S I T o t k g D p i`, `C` first, and the document
also said a reader requires only that `P` come first. Order is the writer's
concern, so that a round trip is defined; a reader requires none. §8.1 gained
the same sentence, since it had no order written down at all and its round trip
is the criterion.

**The example digest in §1.1 was not a digest.** It was 43 base64 characters
where a padded 32-byte value needs 44, so §1.1's own rule refused the example
under it. It is now the `Q2` digest of the word `braam`, which decodes. Nothing
had noticed because nothing had ever parsed one.

### The bug that cost the most: two `Field`s

`StanzaField` is called that because `src/cmd/sh/expand.h` already has a
`struct Field` — an expanded shell word, 28 bytes, with a `String` and a `Vec`
in it. A stanza field is 12 bytes, a `char` and a `Str`, trivially copyable.
Both were at global scope, so **`Vec<Field>` was one mangled symbol over two
layouts**, `Vec<Field>::reserve` a weak definition the linker picked one of, and
the shell's — element size 28, non-trivial move, non-trivial destroy — is what
ran over `pkg`'s 12-byte elements. One element per growth came out with a
mangled pointer.

It presented as anything but an ODR violation. It vanished at `-O2` and
appeared at `-Os`, because the two levels inline `reserve` differently and
whether the shared symbol is called at all depends on that. It vanished when
the vector was reserved up front, because then nothing grows. The allocator
was provably not handing out overlapping blocks. The assembly for
`StanzaReader::next`, for the same loop written by hand, and for
`Vec<Field>::reserve` in `stanza.cpp` all read as correct — because they were:
the wrong one was in another object file.

**The rule this leaves: a type name at namespace scope must be unique across
the whole tree.** There are no namespaces here, `Vec<T>` and `Task<T>` mangle
their argument's name into a weak symbol, and `--gc-sections` with comdat
resolves the collision silently. `grep -rn "^struct <Name>" src/` before adding
one costs nothing; not doing it cost a session.

## `mkdir -p`, and the walk it is made of

`Sys::MkDir` creates one level and refuses a leaf that already exists, so until
now nothing in the system could build a path from nothing: `boot.cpp`'s
`make_dirs` is a fixed list of two names, `web/fs.js`'s `installOps` open-codes
the walk on the host side, and P11 of [pkg's TODO](../../src/cmd/pkg/TODO.md)
was going to have to write a third copy for `/pkg/gen/<N>/…`. `-p` on
`/bin/mkdir` is the same walk, so it is written once, as `make_dir_all` in
`src/proc/io.cpp`, and `pkg` is its second caller rather than its author.

**The walk is userland, not a flag on the syscall.** `Sys::MkDir` has a flags
word going spare and a recursive bit would have been two lines in `vfs_mkdir`,
which is the argument against it: the kernel would then own a loop that can fail
half-way, and the partial tree it left behind would be a kernel decision rather
than a program's. Nothing about the walk needs privilege, the components are
independent syscalls either way, and the ABI does not move. A program that wants
different behaviour on a component that already stands — refusing, or asking —
writes its own loop over `make_dir`, which is still there.

**The prefixes are textual, on the path as written.** No `cwd_get`, no
`path_resolve`: a prefix of a relative path is relative and resolves against the
same cwd, `.` drops on the way past, and `..` reaches the VFS which already
normalises it. That is one syscall per component and not one more, and it is why
`mkdir -p r/s/t` needs nothing that `mkdir -p /a/b/c` does not.

**`Error::Exists` is tolerated rather than pre-empted.** Statting each component
first would double the syscalls on the common path — most of a `/pkg/gen/<N>`
already exists — to learn what the `mkdir` is about to say anyway. The cost of
try-then-tolerate is that `Exists` does not say *what* stands there, and for an
intermediate that does not matter: a file part-way along fails the component
below it, with that component named. It matters for the leaf, where `mkdir -p f`
over a regular file would otherwise report success, so the leaf alone is
statted, and only when the whole path turned out to be there already. One extra
syscall in the case that did no work at all.

## A dependency is a name and a bitfield

P8 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the last Phase C
primitive. `dep.cpp` is Package_Formats.md §6: `[!]name[[op]ver]`, and the
space- or newline-separated list of them that `D:`, `p:`, `i:` and `/pkg/world`
carry. It is `apk_dep_parse` and `apk_blob_pull_dep` with the database interning
taken out — a name here is a `Str` into whoever owns the text, the way `Args`
views argv.

**Nothing in it knows how many operators there are.** `<`, `>`, `=` and `~` each
contribute bits to a mask, a comparison answers exactly one bit, and a
dependency is satisfied when that bit is in the mask. So `foo<=1.2` needs no
case, `foo>~1.2` needs no case, and `!foo` is the same machinery with an
inversion at the end. The whole parser is: strip a `!`, take the name up to the
first operator character, take the maximal run of them, and the rest is a
version. Nine spellings, one code path, and the count nine appears nowhere.

**That is also why `><` needed no decision.** apk reads that spelling as a
checksum comparison and §9 dropped it. Dropping it took no code: `>` and `<`
contribute their bits, the mask means LESS or GREATER, and `foo><1.2` reads as
"any version but this one". A parser with nine cases would have had to grow a
tenth to say what happens; a bitfield has nowhere for a spelling to be missing
from.

**Broken and malformed are different answers because they have different
consequences.** §6 already said an unparseable version marks the dependency
broken rather than failing the file — the stanza becomes an uninstallable
package and every other stanza still reads. It said nothing about a token with
no name at all, or an operator with nothing after it. Those are not broken
dependencies; they are a field that is not a dependency list, and a reader that
treated `=1.2` as "a dependency on the empty name that nothing satisfies" would
turn a corrupt file into a package that merely cannot be installed. So
`dep_parse` answers `Ok`, `Broken` or `Malformed`, and §6 gained the sentence
that says which is which. apk makes the same split, as `-APKE_PKGVERSION_FORMAT`
against `-APKE_DEPENDENCY_FORMAT`.

`Dep::broken` is a field *as well as* a return value, which is apk's shape and
looks redundant. It is what makes `dep_satisfied` safe: a reader that kept a
broken dependency to report it later can still ask whether a candidate
satisfies it and get `false`, without every call site remembering to check the
parse result first.

`VER_CONFLICT` lands in `version.h` rather than `dep.h`, and the inversion
happens inside `version_match`, because that is where apk puts it and because
the alternative — inverting in `dep_satisfied` — would leave a mask that means
one thing to one caller and another to the next. The one thing it forced was
rewriting `version_match`'s early `return true` for `VER_ANY` into a value the
inversion applies to; without that, `!foo` would have matched everything.

## Versions, ported rather than designed

P7 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). `version.cpp` is apk's
`src/version.c` — the token grammar of Package_Formats.md §7, the suffix table
with `none` as its pivot, and a comparison that answers a bit rather than a
sign. `test/unit/version.data` is apk's fixture file and
`test/unit/test_version.cpp` is its loop; all 785 cases pass.

**The interesting decision is that there was no decision.** A version
comparison looks like the sort of thing to write freshly and simply — split on
dots, compare numbers — and every such attempt is wrong in the same places.
`1.07 < 1.1` because a run with a leading zero sorts as a string, and it is a
*string* even when the other side has no leading zero. `1.1_alpha1 < 1.1`
because a pre-release suffix makes the longer version the smaller one, which no
lexicographic or field-wise rule produces. `2.3.0b` orders after `2.3.0` but
before `2.3.1`. Package_Formats.md §9 already froze the grammar for the sake of
this file and `test/solver/`'s fixtures; P7 is what that freeze was for.

**The token-type ordering is the whole algorithm.** The nine token kinds are an
enum whose *numeric order* is load-bearing twice over: `token_next` validates a
transition by comparing the previous token against a bound (a letter after a
suffix is invalid because `LETTER` is not greater than `DIGIT`), and the tail of
the comparison, once the shared prefix matches, decides by which side's next
token has the higher number — the higher number being the *lesser* version,
because `END` is 7 and everything that can still follow is below it. Reordering
that enum would silently change what a version means.

**Two behaviours are apk's and are kept although they look like bugs.** A digit
run is pulled as a `u64` and wraps rather than failing, and two versions that
both go invalid at the same token index compare *equal*. Both fall out of the C
the fixtures were written against. A port that "fixed" either would pass fewer
of them, and the fixtures are the specification.

**The fixture is data, not a rewritten table.** `version.data` is byte-for-byte
apk's with `R"DATA(` prepended and `)DATA"` appended — two lines, so a reader
can diff it against upstream and the test compiles it in without a build step.
`tests.wasm` cannot open a host file, and a generated header would have been a
script, a custom command and a dependency edge for the same 12 KB of text. What
the wrapper buys is that nobody retypes 785 comparisons: a rewritten table is a
table with new mistakes in it.

`version_mask` and `version_match` land here rather than in P8 because the
fixture's line format is `ver1 op ver2` and running it needs them. The mask is
also the shape P8 was written around — an operator is a set of acceptable
results, so nine spellings collapse into one `match()` and a bitfield. apk's
conflict bit waits for the dependency that carries it.

## A digest that streams, and two decoders that refuse

P6 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md). `sha256.cpp` is FIPS
180-4 with an init/update/finish shape, `encode.cpp` is hex and base64 in both
directions, and both are pure enough to compile straight into `tests.wasm`
beside the shell's grammar — `braam_pkg` is not linked there, so a syscall in
either would be a link error.

**The digest streams because the host's cannot.** `crypto.subtle.digest` is
one-shot: it takes a whole buffer and answers once. Asking the host for a
package's hash would mean staging the package through `SYS_STAGE_MAX`, which
caps a package at a megabyte and puts the whole of it across the ABI at the
moment `pkg` is least sure of it — a body it has not yet checked. Concept.md §6
already refused it on those grounds; what P6 adds is the shape that makes the
refusal cheap. `update()` takes whatever came off the fetch descriptor, so
P18's step 2 hashes and writes the same chunk and nothing accumulates.

**The context is 112 bytes and says so in its header.** A `Sha256` in a
coroutine frame is a fifth of the 512-byte budget a frame has before it costs a
whole 64 KiB span, and the coroutine that wants one is the loop reading a body,
which is exactly the long-lived frame. So the header says where it goes: the
heap record the read already needs, not the frame.

**The encoders write into the caller's buffer.** Every value `pkg` encodes has
a size known before it starts — 32 bytes of digest, 32 of public key, 64 of
signature — so `hex_size`/`base64_size` are `constexpr`, a caller declares an
array of exactly that, and the module allocates nothing. Returning a `String`
would have been shorter at each call site and would have put a failure that
cannot happen (a heap that cannot find 44 bytes) on the path where a digest is
compared.

**Both decoders refuse rather than normalise, and the tail-bit rule is the
point.** A base64 group that yields fewer than three bytes has bits no byte
carries. A decoder that ignores them accepts `Zg==` and `Zh==` as the same
byte — and Package_Formats.md §2 matches a key's name by *recomputing* it, so
two spellings of one name is two ways to be the key the anchor trusts. The
check is one mask and it is the difference between a name and a label.
Whitespace is not skipped and `=` is not tolerated anywhere but the end for the
same reason: every relaxation is another spelling. Nothing partial comes back —
a rejection is `None`, never a count with garbage behind it.

**Hex is here although no format uses it.** Package_Formats.md is base64
throughout. Hex is what a person types when they pin a key by fingerprint
(Package_Management.md §6), and what a test vector is written in — which is
immediately its second job: `test_svc.cpp` had a private `unhex` for the RFC
8032 vectors, and it is now the same decoder, under the same rule about two
readers of one encoding that P10 states about the zip.

## `/bin/pkg`, which knows its own names and nothing else

P5 of [src/cmd/pkg/TODO.md](../../src/cmd/pkg/TODO.md), and the first Phase C
task. `pkg` is a binary in the archive, `src/cmd/pkg/` is a directory beside
`src/cmd/sh/`, and `braam_pkg` is a static library the binary links with
`main.cpp` outside it. It carries a table of its eleven subcommand names and a
dispatch over it. Nothing behind any of them: no network, no digest, no `/pkg`
tree, and the default `PATH` is still `/bin`.

**A directory before there is anything to put in it.** Every other program in
`src/cmd/` is one translation unit, and the shell earned its directory by being
a grammar, an expander, a line editor and twenty-six builtins. `pkg` has none of
that yet, so the shape is being chosen ahead of the code rather than discovered
by it — which is the point. What P6 to P28 add is a digest, a version grammar,
a stanza reader, a zip reader, a signature pipeline and a solver, each with a
test of its own, and every one of them wants to be a translation unit that
`main.cpp` does not see. Starting one-file and splitting later would have meant
one commit that moves everything and no test able to name a piece before then.
The library also settles what `pkg` may reach: it links `braam_proc`, so nothing
in it can touch a kernel header that pulls in the scheduler, the same fence
`braam_sh` lives behind.

**The table is explicit and carries no prose.** Explicit for
`builtin/table.cpp`'s reason — `--gc-sections` never extracts an unreferenced
archive member, so anything self-registering would be dropped without a word.
No prose for the other of that file's reasons: what a command is *for* belongs
in `/share/help`, and a description compiled into the binary is a second copy to
go stale. The usage text's list of command names is generated from the table for
the same reason, so a row added by P15 cannot drift from what `pkg` prints.

**An unbuilt command answers 1, an unknown one answers 2.** Both could have been
a usage error, and collapsing them would have been simpler by a branch. They are
kept apart because they are different facts: `pkg nonesuch` is a typo and the
usage line is the useful reply, while `pkg install` is a correctly spelled
command this build does not have yet, which is not a usage error and printing
usage for it would say the word was wrong. It also gives `test/run.mjs` a way to
assert that the table is real rather than that the binary refuses everything —
`pkg install braam` reporting 1 and `pkg nonesuch` reporting 2 is the whole
difference between a dispatch and a stub. Each later task turns one null into a
function and the exit code changes with it.

**`/share/help` describes what `pkg` will do, not what it does.** The line is
`install and remove packages`, phrased for the finished command, because the
smoke test requires every shipped binary to be named there exactly once and a
line that has to be rewritten as the commands land is a line that will be
forgotten. What the build actually has is what `pkg` itself says when run.

## A decompressor that hands back a stream, not a buffer

`Sys::Inflate` is op 58 and `SvcOp::Inflate` is 20; `PROC_ABI` moves to 16. Raw
deflate goes in as one staged payload and a descriptor comes back, so `Read` and
`Close` serve it as they serve a fetched body. With `Verify` before it, Phase B
of `src/cmd/pkg/TODO.md` is done and `/bin/pkg` has everything it needs from the
host.

**The host hands back a reader, and `OP.READ` did not change at all.**
`web/svc.js` already builds `{reader, left, done}` for a fetch body and
`readBody` pulls chunks from it; a `DecompressionStream` gives a reader with the
same contract, so the new arm is three lines and `OP.READ` and `OP.DROP` are
untouched — `DROP` already cancels a reader that never reached its end. The
alternative was to inflate eagerly into one JS buffer, which is what
`web/fs.js` does for the boot archive and would have been fewer lines here too.
**It is the wrong shape for a package manager.** A megabyte of compressed input
is allowed to become a gigabyte, and a caller that streams can stop when the
zip's own header says it has had enough, where a caller handed a finished buffer
is told about the bomb only after it has gone off. The bound belongs where the
caller can enforce it.

**`Handle::Kind::Inflate` shares `Kind::Body`'s storage, which is not the
compromise it looks like.** `Handle` holds every payload as a plain member
rather than a union, so a `JsHandle` of its own would have grown every
descriptor in the system to serve one kind; reusing `HttpResponse` costs
nothing and leaves `status` and `headers` zero. Reusing `Kind::Body` *itself*
would have cost even less — no new arms anywhere — and was rejected because the
tag would then be lying about what the descriptor is, which is the sort of thing
that reads fine until someone prints it. What did get renamed is `http_read`,
now `stream_read`: it never named anything but `SvcOp::Read` on a slot, and the
HTTP was in the name alone.

**Two failure modes are asymmetric, and one of them is worth knowing about.**
`Handle::shut()`'s switch is over a scoped enum with no `default`, so a kind
without an arm is a compile error. `Sys::Read`'s chain starts at
`r = Err(Error::Perm)` and falls through, so a kind without a branch is a silent
runtime refusal — which is why the read path is the one the tests actually walk.

**Where a truncated stream fails is deliberately unspecified.** A browser
delivers the chunks it already had and fails the read that reaches the damage;
`test/fakesvc.mjs`, whose `perform` must answer synchronously and so uses
`node:zlib`'s `inflateRawSync`, fails the `Inflate` itself. Both are errors and
neither is a clean end of stream, so that is what the guarantee says and what
the test asserts. Pinning the timing would have written the fake's shape into
the contract.

**The tests are two vectors and a cut.** One inflates to 65 bytes and fits a
single `SYS_CHUNK`; one inflates to 1500 and takes three, which is the only
thing that proves the read loop runs — a single-chunk bug returns 512 and looks
like success otherwise. Both were checked by sabotage: returning the input
uninflated fails them with 10 and 19 bytes, and deleting the fake's arm fails
them with none. `kernel.wasm` grew 1,318 bytes, to 175,061 of 262,144.

## A signature check, and a refusal that cannot be mistaken for a pass

`Sys::Verify` is op 57 and `SvcOp::Verify` is 19: Ed25519 over
`crypto.subtle.verify`, an enum value on each side of the one `host_svc` import
rather than an import of its own. `PROC_ABI` moves to 15. It is the first code
`/bin/pkg` needs and the load-bearing one — every other check in
[Package_Management.md](../Package_Management.md) §7 assumes a signature was
checked first.

**The boolean lives in one place, and everything below it refuses.** A bad
signature is `-Error::Perm` on the wire, a failed `Result` in the kernel, and a
`false` only once it reaches `verify_sig` in `src/proc/io.h`. The alternative
was a status of 1 for good and 0 for bad, which reads better at a call site and
fails in the wrong direction: a caller that ignored the value would take a bad
signature for a good one. This way the slip is `CO_TRY_VOID` on something that
returns a value — visibly wrong — and the default behaviour of every layer that
does not know about `Verify` is to refuse. **A program still gets a `bool`**,
because the layer that has to tell "this signature is bad, try the next `Y:`
line" from "this browser has no Ed25519, stop" is the one that would otherwise
have to special-case `Perm` by hand at every call.

**Where the algorithm is missing, `Err(Unsupported)` must not look like a check
that failed.** `web/svc.js` catches a `NotSupportedError` and rethrows it as
`E.UNSUPPORTED`, because `statusOf` would otherwise map an unrecognised
exception to `E.IO` — an error either way, but not the one §8 attaches a rule
to. The property was tested by deletion rather than asserted: removing the arm
from `test/fakesvc.mjs` makes every vector fail, the good ones because they no
longer verify and the bad ones because the error is the wrong kind. A suite that
went green with the crypto gone would have proved nothing.

**The fake verifies for real, and synchronously.** `test/fakesvc.mjs` answers
from inside the import, which is what lets a unit test drive a whole `co_await`
chain in one `sched_tick` — there is no drain loop in the unit suite, so an arm
that parked could never be answered at all. `crypto.subtle` is a promise, so the
fake uses `node:crypto`'s synchronous `verify` over the raw key wrapped in
Ed25519's twelve-byte SPKI header. Two implementations of one check is the
opposite of what this codebase usually wants, and it is right here: the fake is
not a stand-in for the real verifier, it *is* a verifier, so RFC 8032's vectors
are checked rather than a canned answer being replayed.

**The lengths are refused before the host is asked.** 32 bytes of key and 64 of
signature, from `SYS_ED25519_KEY` and `SYS_ED25519_SIG` — constants rather than
something the payload states, since §8 is one algorithm and no negotiation.
Anything else is `Err(Invalid)`, which also keeps a malformed anchor from
reaching WebCrypto at all.

**Being one staged payload caps what can be signed at `SYS_STAGE_MAX`.** A
signature over bytes the kernel fetched separately would be a signature over
whichever bytes arrived last, so key, signature and message are staged together
and the 1 MiB ceiling follows. That is a real bound on how large a signed index
may be, and it belongs in the record now rather than being discovered when the
fetch cap is written.

**57 landed with no caller in `src/cmd/`.** The note written two commits ago
said `Sys` would gain each reserved number with its caller, and that has been
rewritten rather than left standing: §4.3's rule bars *growing* the table on
speculation, and a row specified, reviewed and committed before a line of it was
written is not a guess. `Cursor` and `Style` have no caller either, and the
reference has said so for some time. `kernel.wasm` grew 1,402 bytes, to 173,743
of 262,144.

## Five formats, frozen before a parser exists

[Package_Formats.md](../Package_Formats.md) defines one stanza grammar and the
five files over it. It is a reference and reads like one; this is what it leaves
out.

**A document of its own, not a section of the policy.** Package_Management.md's
§7 to §11 are cited by number all through `src/cmd/pkg/TODO.md`, so a new
numbered section would have renumbered four of them, and an appendix would have
put the grammar behind eleven sections of argument about keys. The split is also
the honest one: the policy says what must be true, the format says what the
bytes are, and the two change for different reasons.

**apk's field letters are kept unchanged, and that is the whole design
constraint.** `test/unit/version.data` is 788 comparison cases and
`test/solver/` is 119 test files over 29 repository files, all in plain APKINDEX
text. They port as *data* while the letters and the version grammar match and
become a rewriting job the moment they do not — and a rewritten table is a table
with new mistakes in it. Every temptation to improve a letter was measured
against that and lost. What is dropped is only what has nothing to name here:
`A` because there is one architecture, `so:` because every binary is statically
linked, `@tag` because there is one repository, `><` because the index already
names a package by its hash.

**One letter means one thing across all five files.** apk reuses them freely —
`I` is an installed size in the index and unused in the installed database, `R`
is a regular file in one and absent from the other — and gets away with it
because a single switch statement reads both. Assigning them globally costs a
few less mnemonic choices and buys a table that can be checked by reading down
one column, which is the kind of checking that actually gets done.

**A signature is inline, and the signed region is one rule.** The block is the
first stanza of the file and **the signed bytes are everything after the first
empty line**. A detached `.sig` would have made the region plainer still — it is
the whole file — but at the cost of a second fetch and a second thing to
withhold, and "the whole package set, signed as one file" reads more literally
when it is one file. The risk taken is the parser differential: a signer and a
checker that compute that offset differently produce a signature over bytes
nobody agreed on. It is one rule, stated once, and `tools/signindex.py` is
required to compute it the way `pkg` does — which is why P26 now says so.

**The index's header stanza is an invention, and the document says so.** apk has
no index version and no expiry at all: staleness there is a client-side check of
the cached file's mtime against a four-hour default, and `DESCRIPTION` is a
free-form blob capped at 160 bytes with no keys in it. Package_Management.md §7
requires both a version that only goes up and an expiry, so there was nothing
upstream to copy and this is the part with no second opinion available. `N`, the
repository's own URL inside the signed region, is the piece that is easy to
forget: without it an index published for one repository is a valid index for
every other.

**A package's URL is derived, never carried.** §4 of the policy already says a
redirect is invisible and the URL a package came from proves nothing — a package
is named by its hash. A field naming a URL could then only be a second place for
the same fact to be wrong, and it would be the field an attacker edits.

**Metadata is a top-level dot-entry, because a zip has no order to rely on.**
apk's control section works by being an ordered prefix of a tar; a zip is read
through its central directory, which promises no order at all, so the split had
to be a name. Making it a *path* — a name with no slash in it, beginning with a
dot — also makes it structurally impossible for a package to install a file
where its own metadata lives. **An unknown dot-entry refuses the package**,
where apk ignores unknown control files: a package carrying an instruction this
`pkg` cannot read must not be half-installed, and that is the uppercase rule
applied to an entry name.

**Two departures are about the absence of things.** The installed database keeps
`F:`/`R:`/`Z:` and drops `M:` and `a:`, which carry uid, gid, mode and an xattr
digest — a field that could only ever be written `0:0:644` invites someone to
believe it means something. And end of file commits a stanza, where apk drops a
last stanza that has no trailing blank line. That is silent data loss at the one
place these files are most likely to be edited by hand, and it costs a line to
avoid.

**One contradiction of P1's is fixed here.** `/pkg/gen/<N>` was described both
as a text file holding the installed set and as a directory holding the symlink
farm. It is a directory: `packages` is the text and `bin/` the farm, which is
what lets one rename of `/pkg/active` commit the two together.

## The decisions `/bin/pkg` cannot take back

[Package_Management.md](../Package_Management.md) was written before the package
manager because a key made on a networked machine is never afterwards an offline
key. The development plan under `src/cmd/pkg/` opens with the same move for a
different reason: three of its decisions stop being decisions the moment code
embodies them, and one of them had already changed under it. This is that first
step — five documents amended, no code — and what follows is why each says what
it now says.

**Activation is symbolic links on `PATH`, and the kernel gains nothing.** The
plan was written when `/bin` was the whole of command resolution, and it
proposed a fourth clause in `exec_resolve`: on a miss, read `/pkg`'s record of
which generation is live, read that generation, take the line naming the
command. Three commits since — symbolic links, `Sys::Rename`, and `PATH` read
out of the environment a spawn carries — have between them made the clause pure
cost. A generation materialised as a directory of links into the store, named by
a `/pkg/bin` that the default search list mentions after `/bin`, is the same
activation with no kernel code at all, and the property that mattered survives
unchanged: `/bin` is searched first, so nothing installed can shadow the system.
What the clause would have bought instead is worth naming, because it is what
was given up: the kernel learning two of `/pkg`'s file formats, two file reads
on every failed lookup at a prompt, and four distinct ways of being
half-installed that each had to degrade into an ordinary "command not found"
rather than a boot that would not finish. **A rename is already the commit point
a generation wants**, which is the part that makes the trade lopsided rather
than merely favourable. Writing the new generation whole and then swinging one
link over it is Nix's arrangement, and a tab that dies before the swing has left
rubbish a `pkg clean` collects rather than a system half-upgraded.

**`/pkg` is a top-level directory the archive does not carry, which is §6 read
backwards.** The unpack replaces `bin` and `share` and names no other directory,
and Package_Management.md §6 leans on exactly that to guarantee the trust anchor
cannot be poisoned in the store for good: it is re-pinned from the archive at
every version change. The same sentence, read from the other end, says that
anything *not* in the archive survives a release — so the store, the
generations, and the links that activate one all keep. `PATH` reaching them
keeps too, because the default is a constant in the kernel and not a file
anybody can lose. A version change therefore replaces the system and leaves what
`pkg` installed standing, which is the arrangement that makes "a wipe is fixed
by reinstalling" true of `/bin` without being true of everything.

**SHA-256 moved into wasm; the signature check did not.** The two look like one
decision and are not. A signature check is a promise on the host side, which is
Concept.md §2.2's convention already, and the host is inside the trusted base
unconditionally — a host willing to lie about `crypto.subtle.verify` could
simply hand over a different `pkg.wasm`, so a verifier there widens nothing. A
digest is a different shape: `crypto.subtle.digest` wants the whole message, so
taking a package's hash on the host would mean staging every byte of it through
`SYS_STAGE_MAX`, and a megabyte cap on how large a package may be would have
been imposed by the shape of a syscall rather than by anything true about
packages. Hashing in wasm, off the fetch descriptor, a chunk at a time, costs a
few hundred bytes of `pkg` and nothing large ever crosses the boundary. **The
rule that a stream of bytes comes back as a descriptor cuts both ways**: what a
program can already read a chunk at a time, it can already digest a chunk at a
time, and the operation that would have been added was fighting that.

**`Inflate` answers with a descriptor because its output has no size to
declare.** Its input is one staged payload and is bounded by `SYS_STAGE_MAX`;
its output is bounded by nothing, since a compressed entry decides how much it
becomes. Capping the input and not the output is the asymmetry that makes the
operation worth having rather than a flaw in it — a bound is placed where the
caller can honour it and nowhere the format would get to choose. `Read` and
`Close` then serve the result exactly as they serve a fetched body, so no
operation is duplicated and a hundred-to-one entry costs one reply per chunk.
An entry too large to stage is refused and the bound is documented, which is the
alternative to a chunked `Inflate` that would have needed a session and a second
operation to end it.

**Two operation numbers are reserved in the reference before their code, and the
rule they bend is stated where they sit.** Concept.md §4.3 says every operation
has a caller in `src/cmd/`, and calls that a rule against growing the table on
speculation. `Verify` and `Inflate` are in System_Calls.md's table at 57 and 58,
and in neither `Sys` nor `SvcOp`, precisely so the rule keeps its teeth: nothing
in the tree can issue them, `PROC_ABI` is still 14, and no stamped binary has
been invalidated by a document. What the reservation buys is that the shape of
the two crossings a package manager can get wrong quietly — what a signature is
taken over, and what a decompressor is allowed to hand back — is settled and
reviewable before anything is written against it. The numbers themselves are
free real estate: the host-services group runs 48 to 56 and the terminal group
starts at 64, and sparse numbering was put there for this.

**§11's ban on install scripts was rewritten rather than kept, because it
forbade what nothing could enforce.** The old paragraph argued that a package
running code at install time is a package whose signature authorises arbitrary
execution rather than file contents. That argument is sound and its conclusion
still does not follow, for the reason §11's own first paragraph gives: there is
no privilege boundary here to put a script behind. OPFS stores no per-file mode,
every mount but `/proc` is the one read-write store, and `rm /bin/sh` already
works from a prompt. A fence drawn around a script would have been a drawing,
and the section that exists to disclaim guarantees the system does not have is
the last place to add one. So the paragraph now says what a script *is* — an
ordinary `/bin/sh` process with exactly the authority of whoever typed `pkg
install` — and what that costs, which is the whole of §11 brought within reach
of a signed package rather than only a mistaken one. **What it does not cost is
the rule the document was written for.** A script still runs only after its
package's hash matched a hash from a signed index, so the code that runs is the
publisher's and never the network's; the check moves nothing, it is what the
script's authority is traced back to. A repository that can rewrite a package
still cannot make one run. And a package that only places files runs nothing at
all, since `pkg` unpacks those itself from bytes it hashed, so the common case
keeps the stronger property.
