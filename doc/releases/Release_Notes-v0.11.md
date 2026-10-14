# Release notes — 0.11

Reasoning, alternatives and trade-offs behind the code released as 0.11.
Comments in the source say *what* a thing is; this file says *why* it is that
way. [Concept.md](../Concept.md) remains the specification — where this document
and the spec disagree about intent, the spec wins. The current release's notes,
and the index of the ones before it, are in
[Release_Notes.md](../Release_Notes.md).

---

## 0.11 — The exit plain C makes, and the line a script continues

`BRAAM_VERSION_BASE` moves to 0.11; the commit count and the hash behind it
carry on unedited. 0.10 gave a program arriving from elsewhere the libraries it
links. 0.11 is smaller, and is what a **C compiler hosted here** asked of the
system once one existed: a C program's `exit()` has to work from anywhere and
take its output with it, the shell has to read the scripts such a toolchain
comes with, and the one library a port is likeliest to want different has to be
something it can rebuild.

**`PROC_ABI` does not move.** Nothing here adds an operation or an error value,
so a package built against the 0.10 SDK runs unchanged and no repository is
re-signed.

**A trap after `Sys::Exit` is that exit.** A program with no coroutine to return
from records its status and traps, and the kernel no longer calls that a crash:
the status is the one it recorded, and the writes it posted in its last step are
performed, in order, before it is reaped. A trap with no `Sys::Exit` before it
is still 132 and `crashed`.

**A backslash at the end of a line continues it.** Outside quotes and inside
`"…"`, a backslash-newline is now nothing at all, as POSIX 2.2.1 has it; inside
`'…'` it is still two bytes. Two corners — an operator or a reserved word split
across the line — are not POSIX's, and the section below says why they are left.

**The SDK carries libm's sources.** `share/braam/src/math/` builds
`libbraam_math.a` on its own against the installed SDK, byte-identical to the
shipped archive, and musl's `COPYRIGHT` is in `share/doc/braam/` beside the
other libraries' licences. `cvt/fmtfp.c` also stopped shadowing a name, so
`vcc`, which refuses a shadowed local, takes the file as it is.

## The exit plain code can make, and the buffers it takes with it

A process ends when its root task returns (§4.3). That is the whole of it for a
program written here — `proc_main` returns a status, `src/proc/`'s runtime
reports it on the way out of `_start` — and it is unreachable for plain C
compiled by something that is not making coroutines. `exit(2)` can be called ten
frames deep from code with no awaitable anywhere in it, and there is no way from
there back to the export. Such a program had exactly one way to stop: record a
status with `Sys::Exit` and trap. The kernel read the trap as what a trap has
always been — `"<name>: crashed"` on stderr and status 132 — and threw away
whatever the program had just written.

`devel/cc`'s libc in the applications tree is what asked. A C compiler hosted
here compiles ordinary C, whose `exit()` has to work from anywhere and whose
stdio buffers have to come out with it; the ports in that tree get away without
this because each of them *unwinds* — `vi`'s `ex_exit` records a status and
leaves by the route an error already took, and every `ziperr()` in `zip` became
a return, which upstream's never had to be — which is a rewrite of the
program's control flow, and not something a compiler can do to its input.

**A trap after `Sys::Exit` is that exit, and one bit tells the two apart.**
`Proc::exited` is set by the synchronous `Sys::Exit` arm, and `exec_process`
treats `Trapped` with that bit as `Exited`. A trap with no `Sys::Exit` before it
is still a crash: still 132, still the diagnostic, still `died` left alone for
init. Nothing has to trust the trap itself — the process said what it was ending
with *before* it stopped, through the one operation that cannot park and cannot
fail.

**The writes queued in that last step are performed, which is the half that
makes it usable.** A C `exit()` cannot await its buffers, so the libc *posts*
them — `sys_async` with a token nobody will ever wait on — and exits in the same
step. Nothing had been serving those calls: a step that ends the process returns
before the stepper spawns their server jobs, so the bytes sat in a staging block
and were freed with the record. `drain()` performs them in the order they were
issued, before the process is reaped. The ordinary path drains too, and was
losing the same bytes: a program that posts a write and returns from `_start` in
one step is not a strange thing to be, it is simply one that did not wait for a
byte count it had no use for.

**Writes, and nothing else.** `drain()` skips every call that is not a
`Sys::Write` and every one a server has already taken. The rest either have an
answer with nobody left to receive it or an effect that lands in the wrong
place: a `Read` would take bytes off a pipe and drop them, an `Open` would bind
a descriptor into a table about to be freed. A write is the one operation whose
whole point is its side effect, and whose reply the dead do not need.

**A drained write parks like any other.** It goes through `proc_syscall` with
the process's stdio still alive and its handles still held, so a full pipe
suspends the exit until the reader drains it — exactly the wait the write would
have had a step earlier. `^C` still reaches it, because the drain is awaited in
`exec_process`'s own frame, and the queue it walks is no longer than one a step
could already leave behind.

**The case assembles a program rather than shipping one.** Nothing in this tree
exits this way, so there is nothing in `/bin` to point at — and a program added
to `/bin` for it would have nothing to demonstrate to a user. The case in
`test/system/process.mjs` builds the wasm module by hand instead: post a `Write`
of `hi\n`, call `Sys::Exit(7)`, `unreachable`, stamped with `/bin/true`'s own
`braam` section so `exec` accepts it. It asserts that the output arrives, that
the status is 7 rather than 132, and that the same program down a pipe sends
three bytes to `wc`. Hand-assembled wasm in a test is not free to read, which is
why it is worth saying what it buys: the behaviour is the kernel's answer to a
shape no binary here has, and the alternative was to trust it until a package
hit it.

No ABI change, and nothing in `src/cmd/` or `src/proc/` moved; this went out as
0.10.1.

## The backslash at the end of a line

POSIX 2.2.1 makes a backslash-newline outside single quotes a line continued:
both bytes are removed, and the line goes on as if they had never been typed.
The shell had half of that. A text ending in a backslash already failed to
parse with `more` set, so the prompt already asked for the rest under `PS2` and
joined the two lines with a newline — and then the lexer and the expander read
`\` followed by a newline as what any other escape is, the next byte taken
literally. `echo one \` then `two` printed `one` and a second argument that
began with a newline; `true && \` then `echo yes` looked for a command named
newline-`echo` and found none. The continuation came back to a prompt, and did
the wrong thing with what it had collected.

**It is removed in two places, because a word keeps its text as written.** The
lexer skips a backslash-newline where it skips blanks, so between tokens the
pair separates no more than the blanks around it do — which is what makes
`a &&\` at the end of a line work. Inside a word it is left alone, as quotes
are: the word the parser sees is the raw text, which is how any quoting at all
defeats a reserved word, and the expander takes the pair out at the same point
it takes the quotes out. That is also why `"…"` gets it for nothing and `'…'`
does not: the expander's walk over a double-quoted span sees the backslash, and
its walk over a single-quoted one sees no escapes at all.

**Two consequences are not POSIX's, and are left.** POSIX removes the pair
before tokenizing, so `&\`, newline, `&` is `&&` and `do\`, newline, `ne` is the
reserved word `done`. Here the first is two `&`s and the second is a word that
spells nothing reserved, because the operator was lexed and the word was
compared before anything removed the pair. Doing it POSIX's way means a pass
that rewrites the text before the lexer sees it, and that pass cannot be blind:
the body of a here-document lies in the same text, and in the body of a quoted
one, `<<'EOF'`, a backslash-newline is two bytes that must survive. Where a body
begins is the parser's knowledge, found from the lexer's position after the line
that names it, so the pass would need the parse it runs before. Nobody splits an
operator or a keyword across lines, and a line ending between tokens or inside
an argument is every continuation anyone writes.

No ABI change; this went out as 0.10.2.

## The libm a user can rebuild

The SDK now carries `src/math/` whole, as `share/braam/src/math/`, and its
`CMakeLists.txt` builds two ways: inside this tree it is one more
`add_subdirectory`, and on its own it finds the installed SDK and links
`braam::flags` instead of `braam_flags` — the shape `examples/hello` already
has, and for the same reason, that the file a user is given is the one the
build compiles. The archive it makes is byte-identical to the shipped one.

libm is the library a port is likeliest to want different: another rounding of
a transcendental, a function musl has and `native.c` replaced, a flag the
toolchain file does not pass. Without the sources that is a clone of this tree
and a build of all of it, for one archive with no dependency above
`braam::flags`. The other five libraries stay as they are — zlib, bzip2, liblzma
and zstd have upstreams a user can fetch, and `regex` is one file. musl's
`COPYRIGHT` now goes into `share/doc/braam/` as well, which it should have done
from the first SDK that shipped its code.

No ABI change; this went out as 0.10.3.
