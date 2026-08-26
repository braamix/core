# Release notes — 0.5

Reasoning, alternatives and trade-offs behind the code released as 0.5.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.5 — A system a program can be written for, not only in

`BRAAM_VERSION_BASE` moves to 0.5; the commit count and the hash behind it carry
on unedited. 0.4 was a system that could install software it was not built with
— a repository, an anchor, an index and a transaction, all of it about how a
program *arrives*. 0.5 is about what a program may assume once it is here: a
libm, vendored from musl and checked against a real oracle, so that arithmetic
on a float is no longer a link error; signals delivered through a third export,
so a program can be told something happened without being told it is dead;
`/bin/unzip` for archives that make no claims; and the publisher tools in the
installed SDK, so a package can be built outside this tree by whoever wrote the
program.

**The move is the assertion, not the count.** No single commit between 0.4 and
here forced a new base: a vendored libm, a leaf-call signal export, a
subcommand that lists the index, a boot that says why it went nowhere. Taken one
at a time they are a system being finished. Taken together they change who the
system is for — 0.4's answer to "what can I run here" was "whatever a repository
signs", and 0.5's is "whatever you write, against a floating-point library, a
signal, and an SDK that packages it".

## Signals, and the two facts cancellation was conflating

`sched_cancel` is one sticky `bool` on a `CancelState`, checked at every await
point, and until now it was the only thing `^C` could do. That works, but it
says two things at once that Unix keeps apart: *something happened to you* and
*you are dead*. Three consequences, each of which had been written down as a
deliberate absence and each of which was really the same absence:

- `trap '' 2` was refused outright, because once the flag is set nothing can
  decline it, and `trap … 2` in a **script** could never run at all — the
  script's own process was the thing cancelled.
- A resize reached a program only on its next keystroke. Geometry rides on
  every terminal reply, so `less` handled a resize *perfectly* once a key
  arrived; between the resize and that key the screen was visibly wrong, and a
  shrink could fail the next `ScreenBlit` and exit the pager with status 1.
  `vmstat` had a comment saying it needed a signal to notice, which it did.
- `Error::Cancelled` was unrepresentable on the wire on purpose, so "your
  syscall was interrupted" could not be said.

**The delivery mechanism is a third export, and its whole safety argument is
that it is a leaf call.** `_sig(n)` records the number in a word and returns:
no allocation, no syscall, no coroutine resumed. The alternative considered
first was a parked `Sys::SigWait` that a dedicated task blocks on, which needs
no host change at all — but it burns one of eight task slots, and worse, it
only reaches a process that has *already asked*, so a default disposition
delivered to a process that is merely running has nowhere to land. A third
entry point is more machinery and the right machinery.

What makes a new entry point safe is the contract, not the export. A worker is
single-threaded, so the message carrying a signal is handled between two steps
and never inside one. If `_sig` could allocate, syscall, or resume a coroutine,
it would be doing those things beside a kernel that believes this process is
parked. So it may not, and the rule is written where the export is. Nothing is
lost: a process spinning inside a step was reachable only by `terminate()`
before signals and still is.

**Waking is deliberately not the same act as telling.** `_sig` sets a bit and
starts nothing; the kernel separately abandons the calls the process is parked
on, and each answers the new `Error::Intr`. Both a dying process and an
interrupted one arrive at `serve()` as a cancelled server task, and the two are
told apart by `Proc::dead`, which `~End` sets before it cancels anything. That
one test is the entire difference between "interrupted" and "dead".

**Only five calls may be abandoned** — `Read`, `KeyRead`, `Sleep`, `Wait`,
`ClipRead`. This is `SA_RESTART`'s question, and the answer is a list rather
than a flag because `Err(Intr)` has to mean *nothing happened*: an interrupted
`Write` has lost how many bytes went, which is a worse thing to hand a program
than a wait it did not want. Everything else runs to completion and the process
reads its mask at the next suspension. Interrupting `KeyRead` is not optional:
the kernel-side server parks on `p.keys->next()`, and leaving it there while the
process issued another read would put two receivers on one `Channel`, which is a
kernel invariant a user program must not be able to reach.

**Two dispositions, not three.** Unix has Default / Ignore / Handler; here a bit
set in `Proc::caught` means delivered and a bit clear means the default action
runs. A program that catches a signal and does nothing has ignored it — which
is exactly what `trap '' 2` wanted to say and had no way to. That removes a
whole table and a whole syscall's worth of API surface.

**The mask starts empty**, and that is the property that made this landable.
Every binary in the tree behaves exactly as it did until it opts in: `^C` still
cancels, `WINCH` still does nothing. The three suites passed with the entire
mechanism in place and nothing using it, which is a much stronger check than any
individual assertion.

**A signal travels the channel a reply travels.** `CancelState::waiting` is a
single slot, so the stepper, parked on `p->done.recv()`, cannot also park on a
signal. Rather than give `Waiter` the intrusive queue links a second wait would
need, a signal is pushed into `done` as a record with `sig` set, and the stepper
pops it and posts rather than steps. A full box means the signal is dropped and
the default action stands, which for `SIG_INT` is the old behaviour and the safe
way to fail.

**The mask is a payload because `SIG_WINCH` is bit 28** and the op word's
argument is 24 bits. That was caught by writing the numbers down, not by a test:
Unix's numbering is worth keeping — `128 + n` is already a status here, and
people type `kill -9` — and it does not fit where masks usually go.

**`SIG_WINCH` goes to the foreground**, not only to whoever holds a terminal
route. The first version signalled the two claim holders, which is wrong for
`vmstat`: it writes to stdout and claims nothing, so it would never have been
told. The foreground set is already the answer to "who is the terminal for", and
it is what `^C` uses.

`Sys::Kill` grew a payload rather than an 86th operation, since sending a signal
is what killing already was; an empty payload still means `SIG_KILL`, so nothing
that predates signals changed. `SIG_TSTP` and `SIG_CONT` have numbers and no
sender, and are deliberately **not** in `SIG_CATCHABLE`: a number nothing
delivers is an ABI nothing tests. `^Z` remains a milestone rather than a
command — but the shape it needs is now visible, and it is not the resume-side
twin of `CancelToken` that Shell.md promised it would be. Stopping is the
*stepper* holding off the next step, which suspends no coroutine at an arbitrary
point and needs nothing of every awaitable.

## A libm, and it is not ours

Nothing in Braam could compute a square root. `f64` existed in `kernel/types.h`
and was used for one thing, milliseconds crossing the host boundary; the only
arithmetic on a float anywhere in the tree was four lines of `sched.cpp`
rounding a timer deadline up. `df` and `ps` faked one decimal place with
`(rem * 10) / 1024`. That is the wall a Unix port hits, and because
`--allow-undefined` is deliberately absent (M0) it is a hard wall: `wasm-ld:
error: undefined symbol: sqrt`, and no way forward from there.

**The decision worth writing down is that this code is vendored.** Every other
line in the tree is ours. A libm is the wrong place to start being original:
correctness here is measured in units of the last bit, the algorithms are forty
years of accumulated argument reduction and minimax fitting, and a bug in one is
a wrong answer rather than a crash. The source is musl's, as
wasi-libc/wasix carries it, MIT licensed, with the wasm decisions already made
upstream — `arch/wasm32/fp_arch.h` says the machine has no floating-point
exceptions and no alternate rounding modes, so `fp_barrier` and `fp_force_eval`
are identity functions and `WANT_ROUNDING` is 0.

So `src/math/musl/` is byte-identical to upstream and a re-sync is a clean diff.
Which settles three things that would otherwise be arguments: those sources
compile with `-w` rather than this tree's `-Wall -Wextra -Wshadow -Werror`, they
carry a `.clang-format` with `DisableFormat`, and they are **C**. The last is
not a preference — 47 of them do not compile as C++, all for the same reason:
`libm.h`'s `asuint64` and `asdouble` are compound-literal unions, which C
rejects nothing about and C++ rejects entirely. Enabling C cost one word in
`project(braam LANGUAGES CXX C)`; the toolchain file already set
`CMAKE_C_COMPILER`, `CMAKE_C_COMPILER_TARGET` and `CMAKE_C_FLAGS_INIT`.

### What was measured before any of it was written

157 of the 163 candidate sources compiled first try against a header shim of
about 150 lines. The six that did not were gaps in the shim — a `weak` macro,
`FP_ILOGB0`, `a_clz_64` — not in musl. The archive that came out had no
undefined symbol at all beyond `__stack_pointer`: no compiler-rt, no libc,
nothing for `--allow-undefined` to have hidden.

The cost, linked with `--gc-sections`, which never extracts an unreferenced
archive member:

| a program that calls | .wasm bytes |
| --- | --- |
| `sqrt` | 309 |
| `fmod` | 864 |
| `atan2` | 1,410 |
| `exp` | 3,149 |
| `log` | 5,261 |
| `sin` and `cos` | 5,340 |
| `pow` | 8,319 |
| `tgamma` | 10,402 |
| twelve transcendentals at once | 23,924 |

`sqrt` is 309 bytes because it is `f64.sqrt` and the member is never pulled.
`rootfs/` did not move at all — no program in `src/cmd/` links this — and
`kernel.wasm` is byte-identical, since the kernel does not link it either.

### long double is not a slow path here, it is a link error

`long double` on wasm32 is 113-bit quad. Every operation on one is a
compiler-rt call — `__addtf3`, `__multf3`, `__trunctfdf2` — and there is no
compiler-rt for this target. So the `l`-suffixed half of `<math.h>` cannot
exist: every `*l.c` is left upstream, and so are `nexttoward.c` and
`nexttowardf.c`, which take a `long double` argument and were the only two
non-`l` files to reach for it.

The private `float.h` in `musl/shim/` declares `LDBL_MANT_DIG` to be
`DBL_MANT_DIG`, which is what steers `libm.h` and both text engines onto their
53-bit paths. Nothing is compiled with a real quad in it.

The `float` half is real single-precision code — `sinf`, `expf`, `powf` with
their own data tables — rather than rounded doubles. Rounding wrappers were the
plan until it turned out musl's kernels cost nothing extra to compile and are
more accurate.

### errno never came up

The question of what a domain error should be in a system whose errors are
`Result<T, E>` had an answer before it was asked: musl's libm does not use
`errno`. `__math_invalid`, `__math_oflow` and `__math_uflow` are pure IEEE
computations — `(x-x)/(x-x)`, a multiply that overflows — and what comes back
is a NaN or an infinity. `math_errhandling` is 0 and there is nothing to
report. A `Result<f64, Error>` return would have been unusable to a port anyway.

### Eight are one instruction, and four that look like it are not

wasm has `f64.sqrt`, `f64.abs`, `f64.floor`, `f64.ceil`, `f64.trunc`,
`f64.nearest` and `f64.copysign`. `src/math/native.c` defines those eight (with
`nearbyint` as `rint`, there being no environment to differ about) over the
builtins, and musl's software versions — including `sqrt_data.c`'s table — are
not vendored. They are *defined* and not merely declared because a caller
reaching one through a pointer, or built with `-fno-builtin`, still needs the
symbol.

`round`, `fmin`, `fmax` and `fma` look like they belong in that list and do
not. `__builtin_round`, `__builtin_fmin`, `__builtin_fmax` and `__builtin_fma`
each emit an undefined symbol on this target — probed, not assumed — so musl's
implementations are load-bearing. `f64.min` and `f64.max` exist but are not
`fmin` and `fmax`: they propagate a NaN where C says to return the other
operand, which the unit suite checks.

### The text half was the only real work

`printf("%f")` blocks more ports than the transcendentals do, and musl's two
engines were not a lift. `src/internal/floatscan.c` and `fmt_fp` inside
`src/stdio/vfprintf.c` are both parameterised on `LDBL_MANT_DIG` and both run
through a `FILE` and the `shgetc` scanner, neither of which exists here. So
`src/math/cvt/` holds them derived rather than verbatim: `long double` becomes
`double`, `frexpl` and the one L-suffixed literal go with it, and the FILE
becomes `cvt.h`'s `Cur`, a cursor over a bounded string, and a `Sink`, a
truncating buffer.

One bug came out of that, and it is worth recording because it was invisible:
`isspace` and `isdigit` were written as macros, and musl calls them as
`isspace((c = shgetc(f)))` — so the macro read the stream **twice per test**,
and the parser was one character behind. `parse_f64("-1")` returned 1.0 and
`parse_f64(".5")` returned 5.0. They are `static inline` functions now, which is
what they are in a real libc, and the comment beside them says why.

What that buys is a correctly-rounded `strtod` and an exact `%f`/`%e`/`%g`, the
same ones every musl program gets. The unit suite checks the three classic
misses — `0.1`, `2.2250738585072011e-308`, `8.98846567431158e307` — which a
digit-at-a-time accumulation gets wrong.

`fmt_f64_shortest` is a search rather than a Ryu implementation: ask `fmt_fp`
for one significant digit, then two, and stop at the first that parses back
bit-for-bit. With an exact engine underneath that is genuinely the shortest
round trip, for a dozen lines. The one subtlety is that `%g` chooses exponent
form below its precision, so `100` at one digit is `1e+02` — correct, and not
what anyone wants — so the final conversion asks for `e + 1` digits when the
plain form exists, and `%g` drops the trailing zeros it does not need.

`df` and `ps` keep their integer-tenth fakes. Converting them would make two
core binaries link a libm for one decimal place each, and their comments already
say the truncation is deliberate.

### The suite has a real oracle, which the others do not

`test/unit/test_math.cpp` is the first case in the tree that can check an answer
against something other than itself: `tools/mkmathdata.py` — hand-run, the
seventh tool in `tools/` — asks the host's own libm through Python and writes
`test/unit/math.data` as raw f64 bits. The budget is two ulp for everything,
which absorbs the oracle's own error as well as ours, and the case prints the
worst it saw.

It sees **15 ulp, in `lgamma`**, and that is the only budget raised. `lgamma`
has zeros at 1 and 2; near one of them the cancellation costs digits that no
implementation gets back, and 15 ulp of a value near −0.12 is not a defect.
`tgamma` gets 4, `sqrt`, `fmod` and `remainder` get 0 because they are exact.
Everything else is within two.

The rest of the case is what a table cannot say: NaN propagation through every
kernel, `±Inf` arguments, `copysign(1, -0.0)` being −1 and `1/(-0.0)` being
−Inf, subnormals through `frexp`/`ldexp`/`nextafter`, `round` being away from
zero where `rint` is to even, and `fma` keeping the low half of a product.

`tests` links `braam_math` rather than compiling its sources in. The compile-in
convention exists so that a syscall in `sh/` or `pkg/` is a link error; this
library links `braam_flags` alone and has no syscall to hide, exactly as
`braam_ui` does not.

### Three libraries in the SDK, for the same reason there were two

`braam_math` joins `braam_proc` and `braam_ui`. The rule that kept the other
four out has not changed — `braam_core`, `braam_fs`, `braam_svc` and
`braam_user` each carry a host import no binary may have — and this one carries
none, which `test/smoke/abi.mjs` still checks over every binary. Only the count
moved.

`src/math/musl/` and `src/math/cvt/` are excluded from the header install, and
that exclusion is not tidiness: those directories answer `<stdint.h>`,
`<float.h>` and `<math.h>` for the vendored sources and define `errno` and the
character classes as they need them. A `stdint.h` under `include/braam/` would
be a trap. The first install put them there, and an out-of-tree build caught it.

---

## A boot that goes nowhere says so

A black screen was the one failure braam had no words for. Reported against
Microsoft Edge: the page loaded, the title was right, the canvas stayed black,
and nothing appeared in the status line or the console. It was not Edge. A
script-blocking extension in the profile — "Script Defender" — had left
`javascript: block` for `http://*` and `https://*` in the profile's content
settings, with an allow rule for one origin. Chromium applies those from the
stored profile at startup, and it does so **even though the extension itself was
switched off**, which is why the same profile boots on a second launch and not
on a cold one. The document parsed to a DOM, the canvas element existed at its
untouched 300x150 default, and not one line of script ran.

Nothing in braam could report that, because reporting it was itself script. The
whole diagnostic path — `index.html`'s try/catch, `onError`, the status line —
lives below the point where execution had already stopped. So the first of three
guards is a `<noscript>`, the only thing in the page that speaks without
scripting, and it names the cause rather than the symptom: a blocker can hold
scripting off while appearing disabled, which otherwise reads as a browser that
cannot run braam at all.

The other two cover the neighbouring silence, since a stalled fetch throws
nothing and an unresolved promise is indistinguishable from work in progress.
Above the module sits a classic `<script>` — deliberately classic, so it needs
no fetch of its own and runs when the module's never arrives — holding a five
second watchdog that the module disarms on entry. After that `mount` owns the
question, and the worker now names each boot step it starts (`kind: "stage"`,
recorded and shown only if the watchdog fires) so the message is "stuck fetching
kernel.wasm" rather than "stuck". The stages are messages the page never
displays in the ordinary case, which is the point: the cost of a boot that works
is three postMessages.

Five seconds is long enough that a slow network does not trip it and short
enough to beat the reflex to reload. A watchdog that fires on a merely slow page
teaches people to ignore it; every condition here is permanent rather than slow,
so the fetch is not late, it is never coming.

## `pkg list` lists the repository, and `-i` what is installed

The index was reachable only through `pkg search <pattern>`: a person who wanted
to know what a repository offers had to invent a pattern for it, and `pkg search
'*'` is not something anyone guesses. Meanwhile `pkg list` printed the installed
set — the one listing that is also `ls /pkg/gen/<N>` away, and the one a person
asks for second, after deciding what to install.

So the default is flipped. `pkg list` prints the stored index in `pkg search`'s
three columns, and `pkg list -i` prints what `pkg list` used to. That is a
change of meaning for an existing command rather than an addition, which is
worth stating plainly: a script that ran `pkg list` for the installed set now
gets the repository. The flag is the whole of the migration, and it is the
letter apk and dpkg both spell `-i` for the same question.

The listing deliberately does **not** mark rows that are already installed. It
would need the active generation as well as the index, so the command would
start failing in situations where only one of the two is readable, and `pkg
info` already answers "is this installed" for a name, with an `installed` row
that a listing has no room for.

### The two callers are one function

`pkg search` and the new listing differ by a predicate, so the index is loaded,
filtered and printed by one coroutine that takes `all` — column widths still
come off the rows actually printed, and a stanza with no description still ends
its row at the version. The installed path keeps its own coroutine rather than
becoming a branch of `pkg_list`: one frame would then hold a `CheckedIndex`
pointer *and* the generation's two `String`s, and §2's 512-byte frame rule is
not a suggestion.

### An option after a command word belongs to the command

`take_flags` rejected any word beginning `-` that was not `-v`, `--verbose`,
`-h` or `--help`, wherever it appeared, so `pkg list -i` would have died as
`pkg: unknown option: -i` before `pkg_list` ran. It now rejects only what stands
*before* the command word; after it, an unrecognised flag is passed through as
that command's argv, which is how `-i` reaches `pkg list` and how any later
subcommand option will reach its own. `-v` is still taken wherever it is typed —
`pkg -v list -i` and `pkg list -i -v` are one thing — and `pkg -x update` still
says `unknown option: -x`, because `-x` is in front. What changed is the message
for a flag *behind* the command: `pkg update -x` now fails through `update`'s
own operand check, its usage line and 2, rather than pkg's. Same status, better
address.

The flag itself is parsed by `OptParse`, the parser `ls`, `mkdir` and `mv`
already use, rather than a hand-written comparison — `pkg list -i` is the first
pkg subcommand with an option of its own and will not be the last.

---

## Two ways to name a package, and `/bin/unzip`

`pkg install` took package names, and every name had to be in a signed index.
Nothing in the system could open a zip at all: `curl` could fetch one and there
it stopped. So a person who had an archive — built themselves, handed to them,
sitting on a mirror — had no way in, in a system whose whole point is that the
person at the keyboard owns the machine.

The obvious objection is that Package_Management.md §7 said *"nothing is
unzipped, written to the store, or run before its digest matches a digest from a
signed index"*, and closed with *"there is no `--force`, `--insecure` or
`--no-verify` in any form"*. Taking a path as an operand is that flag, spelled
worse — silently, with nothing on the command line saying so.

What resolves it is noticing what §7 actually protects against. §3's attacker is
**the repository and the network in front of it** — never the operator. And §11
already said `pkg` has no privileges to have: OPFS keeps no mode, `/bin` is
writable, *"anything may overwrite `/bin/pkg` itself"*. So the guarantee was
never "only checked code runs"; it was **"a repository never chooses the
bytes"**, and a sideload does not touch that. §11's headline is reworded to say
so, because the old phrasing — "`pkg` installs only what it checked" — invited
exactly the confusion that made this look like a weakening.

So there are now two ways to name bytes and only two: a digest in a signed
index, or a path or URL a person typed. §7's rule is restated over both, and the
first is untouched — there is still no way to make a package the index named
skip a step.

### What keeps the first way intact

Two rules, and they are the whole of why this is safe to add.

**The index owns every name-version it lists.** An archive whose `.PKGINFO`
calls itself `hello-1.0-r0` at bytes the index did not give is refused at step
`index`. Without this a path could quietly stand in for a repository's package,
which is §3's *wrong package* attack wearing a local disguise. It is the one
refusal in the smoke case with a comment saying it is the one that matters.

**Identity is the digest, not the operand.** `solve()` already keyed packages by
digest, so the sideload's stanza is simply appended to the solver's universe and
an archive the index *does* list collapses into the index's entry with no code
at all. That gives the offline case for free: a package carried in on a file or
fetched from a mirror is checked exactly as §7 checks one, keeps its `G`, and
skips only the download. `acquire()` looks up the held archive by digest for the
same reason, so the two cases are one branch.

### The bug this found

`package_read` checked only that `P` and `V` were non-empty, and §6 said in so
many words that *"a name is any token"* — while `pkg_stem` built
`/pkg/store/<P>-<V>/` and `/pkg/db/<P>-<V>` out of them. A `.PKGINFO` saying
`P: ../../bin` would have written outside the store. Nobody had to care before:
`P` arrived from a signed index, and a publisher who corrupts their own store is
their own problem. Making `.PKGINFO` the stanza turns it into untrusted input to
a path, so `stanza_component` now holds `P` and `V` to a path component — no
`/`, no `\`, no `..`, no control byte.

It is applied in `package_read` rather than at the sideload's call site, so the
index's own stanzas meet it too. That is stricter than §3.2 was, and §3.2 and §6
are amended to say so. No legitimate name is affected, and a publisher whose
tooling emits a `P` with a slash in it now finds out at the client instead of
after it has scattered files across `/pkg`.

### The smaller decisions

**`G: 0`** is §8.1's record of "nothing vouched". It is chosen on the *digest*,
not on how the archive arrived, which is what makes the offline case above
record a real `G`. `pkg verify` reports such a record as `unvouched` and
deliberately does **not** fail over it — it is how the package arrived, not
something that went wrong, and a deliberate sideload must not leave `pkg verify`
permanently red. `pkg info` gained a `vouched` row and a fallback to the
`/pkg/db` record, without which a sideloaded package would have been invisible
to it: `info` asks the index, and the index has never heard of one.

**§6.1's `cmd:` names are derived from the archive's flat `bin/`.** The index
gets them from `mkindex.py`, and §6.1 says outright that `.PKGINFO` need carry
none — so a sideload with no derivation would silently lose every `cmd:` name,
and §6.1's invariant that a solve against the installed set sees what a solve
against the index saw would quietly stop holding. `pkg` computes them itself
from the same rule.

**Scripts run.** §11 used to say a signature authorises execution. Refusing to
run a sideload's `.post-install` would be theatre: the payload lands in
`/pkg/bin` and on `PATH` regardless, so the archive runs code the moment
anything invokes it. What authorises execution is now whatever named the bytes —
a signature for a repository's package, a typed path for a sideload.

**The operand syntax is `://` for a URL and a trailing `.zip` for a file.** Both
were usage errors before, so nothing changes meaning. The alternative — treating
anything `dep_parse` calls malformed as a path — would turn a mistyped package
name into a silent file lookup instead of a clear usage error. The subcommand
table still reads `install <package>...`: a file and a URL are ways of *naming*
a package, and spelling all three there widened the aligned description column
past sixty and wrapped every row on the terminal.

### `/bin/unzip`

The other half, and the one that needs no policy argument at all. The zip reader
already existed as `src/cmd/pkg/zip.cpp` and `unzip.cpp`, already checked
against `web/fs.js` over the same bytes by the unit suite. `/bin/unzip` names
those two sources outright rather than linking `braam_pkg`, exactly as
`/bin/test` names the shell's `cond.cpp` instead of dragging in the whole shell.

It exists **separately** on purpose. Arbitrary archives belong to a tool that
makes no claims, so that `pkg` keeps a narrow guarantee it can actually make. It
also inherits §5.2's path rules — an absolute, backslashed or climbing name is
refused by the parser before extraction sees it — so it has no path check of its
own, which is the good kind of not-writing-code.
