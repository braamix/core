# Release notes

Reasoning, alternatives and trade-offs behind the code. Comments in the source
say *what* a thing is; this file says *why* it is that way.
[Concept.md](Concept.md) remains the specification — where this document and the
spec disagree about intent, the spec wins and one of the two needs amending.

What follows the index below is the release after 0.11, under headings
appended in order; the whole moves to [releases/](releases/) when the release
is cut.

Releases before this one are one file each in [releases/](releases/), newest
first:

- [0.11](releases/Release_Notes-v0.11.md) — the exit plain C makes, and the
  line a script continues
- [0.10](releases/Release_Notes-v0.10.md) — the libraries a port links, and
  the escapes a guest speaks
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

## The archive read in memory

`zip_entries` walks the central directory and reads each entry's local header
through the `ZipSource`. Over a `MemZipSource` that read never parks, and an
await that never parks is not free: the awaiter resumes on its own stack, so a
loop of them grows the native stack an entry at a time (§3.3). A few hundred
entries were enough to overflow Safari's, which is smaller than Chrome's or
Node's, and so neither suite saw it. An archive already in memory is now read
plainly through `ZipSource::bytes()`, and one in the store keeps the await,
since its read does park and returns through the trampoline.
