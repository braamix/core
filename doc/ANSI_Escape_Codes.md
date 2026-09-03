# ANSI escape codes in Braam

Braam's terminal is a grid of cells, not a byte stream: colours are struct
fields and the cursor is an index. That does not change. What changes is that
the grid gains a **second way in** — a program may drive it by writing the
escape sequences a VT100 or an xterm understands, and the kernel translates them
into the calls it already has.

This exists for programs that were not written for Braam: `simbesm`, whose guest
Unix speaks escapes to its console, and an `ssh` client later, whose remote host
does the same. Nothing native needs it.

There are two halves, and they are not symmetric:

- **The screen parses.** `screen_write` understands the sequences in §4 and
  swallows the rest ([src/kernel/screen.h](../src/kernel/screen.h)).
- **A program encodes.** The kernel never turns a keystroke into bytes. A key
  reaches a program as `Key{code, mods}` ([src/kernel/key.h](../src/kernel/key.h)),
  and a program that must feed a guest or a remote host encodes it itself, using
  the table in §5.

> **Notation.** ESC is the byte `0x1B`. **CSI** is the two bytes `ESC [`;
> **SS3** is `ESC O`. A space between bytes in a table is for the reader and is
> never transmitted: `ESC [ 5 ~` is four bytes. `<n>`, `<r>`, `<c>` are decimal
> numbers written in ASCII digits.

## Table of contents

1. [What changes, and what does not](#1-what-changes-and-what-does-not)
2. [The grid an escape maps onto](#2-the-grid-an-escape-maps-onto)
3. [The grammar](#3-the-grammar)
4. [Table 1: what the screen understands](#4-table-1-what-the-screen-understands)
5. [Table 2: what a program sends for a key](#5-table-2-what-a-program-sends-for-a-key)
6. [Five traps](#6-five-traps)
7. [Where it lands](#7-where-it-lands)
8. [See also](#8-see-also)

---

## 1. What changes, and what does not

**The grid stays the model.** A native program still paints by filling cells and
blitting them across (`Sys::ScreenBlit`), still sets colour with `Sys::Style`,
still moves the cursor with `Sys::Cursor` and clears with `Sys::ScreenClear`.
None of that is deprecated and none of it goes through a parser. `sh`'s line
editor, `less`, `edit` and `clear` are untouched.

**ANSI becomes one encoding *into* the grid**, parsed where bytes arrive. The
one visible change to existing behaviour is that **ESC is no longer a glyph**:
today `screen_write` handles `\n`, `\r` and `\b` and paints everything else,
including ESC and every other control byte. After this, ESC begins a sequence
and the other control bytes are ignored rather than drawn.

This amended a stated invariant — "no ANSI escapes, no VT100" in
[CLAUDE.md](../CLAUDE.md), [doc/Concept.md](Concept.md) §1 and §2.3, and
[README.md](../README.md). The amendment is that the *grid* is the model and
ANSI is an encoding into it, not that the grid is a stream.

**The key half is nobody's but the program's.** The console pump
([src/user/console.cpp](../src/user/console.cpp)) is unchanged: it still cooks
lines, still routes raw `Key` structs to a claimant, and still knows no control
characters. §5 is a contract for `simbesm` and for whatever comes after it — a
shared encoder they link, not kernel behaviour.

---

## 2. The grid an escape maps onto

Everything in §4 has to end up here. An implementer should read this first,
because half of the entries in §4 are "the nearest thing this grid can do".

| | |
|---|---|
| Geometry | up to 512 × 256 cells; the host sets it with `resize` |
| A cell | `Cell{ char32_t ch; u8 fg, bg, attrs; }` — `ch == 0` is blank |
| Colours | **16**: `COLOR_BLACK`…`COLOR_WHITE` (0–7), plus `COLOR_BRIGHT` (8) |
| Attributes | **3 bits**: `ATTR_BOLD`, `ATTR_UNDERLINE`, `ATTR_REVERSE` |
| Wrap | deferred — `cursor_x` may equal `cols`, and the wrap happens on the next glyph |
| Scrollback | 512 rows, filled by rows falling off the top; the page pages it |
| Cursor | one, visible or not (`screen_cursor`) |

Four things the grid cannot represent, and what the parser does instead:

- **Italic** — there is no attribute bit. `ESC [ 3 m` sets the foreground to
  **cyan** and `ESC [ 23 m` puts back the colour that was in force (§4.4). A
  manual page's italic is then visible rather than lost.
- **Blink, faint, conceal, strikethrough** — no bits, and no substitute worth
  making. The parameters are accepted and do nothing.
- **256 colours and 24-bit colour** — `fg` and `bg` are one-byte palette
  indices. `ESC [ 38 ; 5 ; <n> m` and `ESC [ 38 ; 2 ; <r> ; <g> ; <b> m` are
  **quantised to the nearest of the sixteen**.
- **A reply** — nothing can send bytes back toward a program's input. See
  trap 2.

There is also **no mouse anywhere in the ABI** ([doc/Concept.md](Concept.md)
§3.5): selection and the wheel are the page's business, so every mouse-reporting
mode is swallowed and stays swallowed.

---

## 3. The grammar

Four states. This is the whole parser.

- **Ground** — bytes are decoded as UTF-8 and painted, except the C0 bytes in
  §4.1. `ESC` moves to *Escape*.
- **Escape** — the next byte decides. `[` moves to *CSI*; `]`, `P`, `^` and `_`
  move to *String*; anything else is a two-byte sequence and ends here (§4.2).
- **CSI** — parameter bytes `0`–`9` and `;`, an optional leading `?`, then a
  **final byte in `@` (0x40) … `~` (0x7E)**, which ends the sequence. Bytes in
  `0x20`–`0x2F` are intermediates: collected and ignored.
- **String** — everything up to `BEL` (0x07) or `ST` (`ESC \`), discarded. This
  is what keeps a window title from being painted across the screen.

Five rules that bite:

1. **The state survives across calls.** A sequence may be split by any buffer
   boundary, so the parser's state belongs to the `Term`, not to a local in
   `screen_write`.
2. **An omitted parameter is 0.** `ESC [ m` is a full attribute reset and
   `ESC [ H` is home. A parser that requires a digit gets both wrong.
3. **A parameter that means a count defaults to 1**, not 0: `ESC [ A` is up one
   row. A count of 0 is also treated as 1.
4. **Bound everything.** At most 16 parameters, each clamped; extra parameters
   are dropped, not an error. A sequence that runs past a sane length is
   abandoned and the parser returns to ground.
5. **An unrecognised sequence is swallowed, never painted.** A guest will send
   things this table does not list; a stray `ESC [ ? 12 l` must vanish rather
   than appear as `[?12l`.

---

## 4. Table 1: what the screen understands

### 4.1 Control bytes

| Byte | Name | Effect |
|---|---|---|
| `0x07` | BEL | ignored — there is no bell |
| `0x08` | BS | cursor one column left, erasing nothing (`screen_left`) |
| `0x09` | HT | to the next tab stop, or the right margin; never wraps |
| `0x0A` | LF | new line — column 0 and down one row (**see trap 1**) |
| `0x0D` | CR | column 0 |
| `0x1B` | ESC | begins a sequence (§3) |
| others | — | every other C0 byte, and DEL `0x7F`, is ignored |

Tab stops start every 8 columns and are changed by `HTS` and `TBC` below.

### 4.2 Two-byte sequences

| Sequence | Name | Effect |
|---|---|---|
| `ESC 7` | DECSC | save the cursor position **and the current style** |
| `ESC 8` | DECRC | restore both |
| `ESC D` | IND | down one row; at the bottom margin, scroll up |
| `ESC E` | NEL | column 0, then IND |
| `ESC M` | RI | up one row; at the top margin, scroll down |
| `ESC H` | HTS | set a tab stop at the cursor column |
| `ESC c` | RIS | full reset: clear, home, default style, margins and modes |
| `ESC \` | ST | ends a string (§3) |
| `ESC ( x`, `ESC ) x` | — | character-set designation: swallowed, three bytes |
| `ESC =`, `ESC >` | DECKPAM/DECKPNM | swallowed — key encoding is not ours (§5) |

### 4.3 CSI sequences

`<n>` defaults to 1 in every count below.

| Sequence | Name | Effect |
|---|---|---|
| `ESC [ <n> @` | ICH | insert `<n>` blanks at the cursor, shifting the rest of the line right |
| `ESC [ <n> A` | CUU | up `<n>` rows, stopping at the top margin |
| `ESC [ <n> B` | CUD | down `<n>` rows, stopping at the bottom margin |
| `ESC [ <n> C` | CUF | right `<n>` columns, stopping at the last |
| `ESC [ <n> D` | CUB | left `<n>` columns, stopping at the first |
| `ESC [ <n> E` | CNL | down `<n>` rows, column 0 |
| `ESC [ <n> F` | CPL | up `<n>` rows, column 0 |
| `` ESC [ <n> G ``, `` ESC [ <n> ` `` | CHA, HPA | to column `<n>`, **1-origin** |
| `ESC [ <r> ; <c> H`, `… f` | CUP, HVP | to row `<r>`, column `<c>`, **1-origin**; `ESC [ H` is home |
| `ESC [ <n> I` | CHT | forward `<n>` tab stops |
| `ESC [ <n> J` | ED | erase: `0` cursor to end of screen, `1` start to cursor, `2` all, `3` all and drop the scrollback |
| `ESC [ <n> K` | EL | erase in the line: `0` to the end, `1` from the start, `2` the whole row |
| `ESC [ <n> L` | IL | insert `<n>` blank rows at the cursor, within the scroll region |
| `ESC [ <n> M` | DL | delete `<n>` rows at the cursor, within the scroll region |
| `ESC [ <n> P` | DCH | delete `<n>` characters, shifting the rest of the line left |
| `ESC [ <n> S` | SU | scroll the region up `<n>` rows |
| `ESC [ <n> T` | SD | scroll the region down `<n>` rows |
| `ESC [ <n> X` | ECH | blank `<n>` cells from the cursor, moving nothing |
| `ESC [ <n> Z` | CBT | back `<n>` tab stops |
| `ESC [ <n> d` | VPA | to row `<n>`, **1-origin**, column unchanged |
| `ESC [ <n> g` | TBC | `0` clear the stop at the cursor, `3` clear every stop |
| `ESC [ <n> m` | SGR | §4.4 |
| `ESC [ <t> ; <b> r` | DECSTBM | scroll region, rows `<t>`…`<b>` **1-origin**; no parameters resets it to the whole screen, and setting it homes the cursor |
| `ESC [ s`, `ESC [ u` | — | accepted as DECSC and DECRC |
| `ESC [ <n> n` | DSR | **swallowed, and nothing answers** (trap 2) |
| `ESC [ <n> c` | DA | **swallowed, and nothing answers** (trap 2) |
| `ESC [ <n> t` | — | window operations: swallowed |
| `ESC [ <n> q` | DECSCUSR | cursor shape: swallowed |

Erasing, inserting and deleting all write blanks **in the current background
colour**, which is what makes a coloured pane clear correctly.

### 4.4 SGR — character attributes

`ESC [ <a> ; <b> ; … m`. Each parameter is applied in turn; an empty parameter
list means `0`.

| Parameter | Effect |
|---|---|
| `0` | reset: default colours, no attributes |
| `1` / `22` | bold on / off (`ATTR_BOLD`) |
| `3` / `23` | **foreground to cyan / back to what it was** — italic's stand-in |
| `4` / `24` | underline on / off (`ATTR_UNDERLINE`) |
| `7` / `27` | reverse on / off (`ATTR_REVERSE`) |
| `2`, `5`, `8`, `9` | faint, blink, conceal, strike: accepted, no effect |
| `30`–`37` | foreground `COLOR_BLACK`…`COLOR_WHITE` |
| `39` | default foreground (`COLOR_WHITE`) |
| `40`–`47` | background `COLOR_BLACK`…`COLOR_WHITE` |
| `49` | default background (`COLOR_BLACK`) |
| `90`–`97` | bright foreground (the same plus `COLOR_BRIGHT`) |
| `100`–`107` | bright background |
| `38 ; 5 ; <n>` | 256-colour foreground, quantised to the nearest of the sixteen |
| `48 ; 5 ; <n>` | the same for the background |
| `38 ; 2 ; <r> ; <g> ; <b>` | 24-bit foreground, quantised likewise; `48 ; 2 ; …` for the background |

**The italic rule in full**, because it is the one invention here. `ESC [ 3 m`
remembers the foreground in force and sets `COLOR_CYAN`. `ESC [ 23 m` puts the
remembered one back. Any explicit foreground change while italic is on wins and
forgets the remembered colour; `ESC [ 0 m` resets both. So `ESC [ 3 m` twice in
a row does not lose the original, and italic nested inside a colour comes back
to that colour.

### 4.5 Modes

`ESC [ <n> h` sets, `ESC [ <n> l` resets.

| Mode | Name | Effect |
|---|---|---|
| `4` | IRM | insert mode: a glyph pushes the rest of the line right instead of overwriting |
| `20` | LNM | LF is a full new line. **Set at boot** — see trap 1 |
| `? 7` | DECAWM | autowrap. Set at boot; reset makes a glyph at the last column overwrite it |
| `? 25` | DECTCEM | cursor visible (`screen_cursor`) |
| `? 47`, `? 1047`, `? 1049` | — | alternate screen: **swallowed**. `FullScreen` ([src/user/tty.h](../src/user/tty.h)) already saves and restores the grid, at the claim rather than at the byte — a program takes the screen by asking for it |
| `? 1` | DECCKM | cursor-key application mode: **swallowed** (§5) |
| `? 12` | — | cursor blink: swallowed |
| `? 1000`–`? 1006` | — | mouse reporting: swallowed, permanently (§2) |
| `? 2004` | — | bracketed paste: swallowed; a paste is keystrokes here |

---

## 5. Table 2: what a program sends for a key

This half is **not** the kernel's. A program holding a raw keyboard claim
(`KeyInput` in [src/user/tty.h](../src/user/tty.h)) receives `Key{code, mods}`
and, if it is feeding a guest or a remote host, encodes it with this table.

### 5.1 Keys that are one or two bytes

| Key | Bytes |
|---|---|
| any printable key | its codepoint in UTF-8 |
| `KEY_ENTER` | `0x0D` (CR) |
| `KEY_TAB` | `0x09` (HT) |
| `KEY_BACKSPACE` | `0x7F` (DEL) |
| `KEY_ESCAPE` | `0x1B` (ESC) |

### 5.2 Named keys

| Key | Bytes |
|---|---|
| `KEY_UP`, `KEY_DOWN` | `ESC [ A`, `ESC [ B` |
| `KEY_RIGHT`, `KEY_LEFT` | `ESC [ C`, `ESC [ D` |
| `KEY_HOME`, `KEY_END` | `ESC [ H`, `ESC [ F` |
| `KEY_INSERT`, `KEY_DELETE` | `ESC [ 2 ~`, `ESC [ 3 ~` |
| `KEY_PAGE_UP`, `KEY_PAGE_DOWN` | `ESC [ 5 ~`, `ESC [ 6 ~` |
| `KEY_F1`…`KEY_F4` | `ESC O P`, `ESC O Q`, `ESC O R`, `ESC O S` |
| `KEY_F5`…`KEY_F8` | `ESC [ 15 ~`, `ESC [ 17 ~`, `ESC [ 18 ~`, `ESC [ 19 ~` |
| `KEY_F9`…`KEY_F12` | `ESC [ 20 ~`, `ESC [ 21 ~`, `ESC [ 23 ~`, `ESC [ 24 ~` |

**Always the CSI spelling for the arrows and Home/End, never SS3.** A real
xterm sends `ESC O A` only after being switched into application mode, and
nothing here tracks that switch: the screen swallows `ESC [ ? 1 h` (§4.5), so a
program has no way to know it was asked for. The CSI forms are what a terminal
in normal mode sends, and every decoder accepts them.

### 5.3 Modifiers

| Modifier | Encoding |
|---|---|
| `MOD_CTRL` + `a`…`z` | `0x01`…`0x1A` |
| `MOD_CTRL` + `@`, space | `0x00` |
| `MOD_CTRL` + `[ \ ] ^ _` | `0x1B`…`0x1F` |
| `MOD_CTRL` + `?` | `0x7F` |
| `MOD_ALT` or `MOD_META` | `ESC`, then the key's own bytes |
| a named key with any modifier | the parameterised form below |
| `MOD_SHIFT` on a printable key | **nothing extra** — the shift is already in the codepoint |

The parameterised form puts a modifier number `<m>` into the sequence:

```
m = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0) + (meta ? 8 : 0)

ESC [ A     ->  ESC [ 1 ; <m> A      the arrows, Home and End
ESC [ 5 ~   ->  ESC [ 5 ; <m> ~      the tilde family
ESC O P     ->  ESC [ 1 ; <m> P      F1-F4 lose SS3 when modified
```

`<m>` is never 1: an unmodified key uses the plain form from §5.2.

---

## 6. Five traps

### 6.1 LF is a new line here, not an index

On a real terminal `LF` moves down and leaves the column alone; only `CR LF`
returns to the margin. Braam's `screen_newline` has always done both, and the
whole tree writes `"\n"` expecting it.

So **Braam boots with LNM set** (§4.5), which is exactly that behaviour, and
`ESC [ 20 l` turns it off for a program that wants the strict thing. `IND`
(`ESC D`) is always index-only whatever LNM says, and `NEL` (`ESC E`) is always
both — a program that needs one specific behaviour should use those and not
depend on the mode.

### 6.2 There is nothing to answer with

`ESC [ 6 n` asks for the cursor position and `ESC [ c` asks what kind of
terminal this is. **Neither is answered**, because no path exists from the
screen back toward a process's input: the screen is a grid a program writes
into, and the keyboard is a separate channel with its own claim. Both sequences
are parsed and discarded.

A program that needs to know where the cursor is must track it, or read it back
with `Sys::Cursor`. A guest that *waits* for a reply will hang, and that is the
guest program's problem to work around — do not invent a reply path in the
kernel to fix it.

### 6.3 `0x9B` is never a CSI

`0x9B` is the single-byte CSI of the eight-bit control set, and it is
**forbidden here**, both in the parser and in what a program sends. Braam's
streams are UTF-8, and `0x9B` is a continuation byte — the second byte of
Cyrillic Л (U+041B), and of Ы, Ю and much of lower case. A parser that honours
it eats the next keystroke every time someone types one of those letters.

**Send `ESC [`, two bytes, always.**

### 6.4 Four chords never reach a program

Shift+PageUp, Shift+PageDown, Shift+Up and Shift+Down are the scrollback
gesture. The console pump takes them before any claim sees them
([src/user/console.cpp](../src/user/console.cpp)), so an encoder will never be
handed one and a guest cannot bind them.

### 6.5 A bare Escape is unambiguous on this side

An xterm cannot tell the Escape *key* from the start of a sequence without a
timeout. Braam can: a `Key` arrives whole, and `KEY_ESCAPE` is a code, not a
byte. The encoder therefore needs no timer and must not invent one.

The ambiguity does not disappear — it moves to the far end of the pipe, where a
guest reading `0x1B` off a byte stream has the usual problem. That is the
guest's, and always was.

---

## 7. Where it lands

**§4 has landed. §5 has not** — it is a program's, and nothing links it yet.

- **The parser** is [src/kernel/ansi.cpp](../src/kernel/ansi.cpp) over an
  `Ansi` ([src/kernel/ansi.h](../src/kernel/ansi.h)) held by the `Term`, and it
  drives the grid through the `screen_*` calls and nothing else. `screen_write`
  is its entry.
- **The grid primitives** it needed are in
  [src/kernel/screen.h](../src/kernel/screen.h): a scrolling region, index and
  reverse index, scroll by `<n>` either way, insert and delete rows and cells,
  erase over a range, a line and a display, and the style read back. The tab
  stops are the parser's, since nothing below it has one.
- **Sticky state does not outlive a program**: a `FullScreen` claim resets the
  parser at both ends, and so does `Sys::ScreenClear`, which makes `clear` the
  `reset` there is.
- **No ABI change.** `Cell` keeps its three attribute bits and its 8-byte
  stride, [web/render.js](../web/render.js) is untouched, and there is no new
  import or export — so [test/system/abi.mjs](../test/system/abi.mjs) did not
  move.
- **Two cases**: [test/unit/test_ansi.cpp](../test/unit/test_ansi.cpp) for the
  grammar and the table, and [test/system/escape.mjs](../test/system/escape.mjs)
  for the path from a program's write to the cells, where `tr` supplies the ESC
  byte a prompt cannot type (§6.5).
- **The key encoder** is still to write: a program-side helper under
  `src/proc/`, a pure function of `Key{code, mods}`, linked by `simbesm` and by
  whatever needs it next.

---

## 8. See also

- [doc/Concept.md](Concept.md) §3.5 — the screen and the keyboard as they are.
- [doc/System_Calls.md](System_Calls.md) — `Sys::Style`, `Sys::Cursor`,
  `Sys::ScreenBlit`, `Sys::Echo`: the native way in, which this does not
  replace.
- [src/kernel/screen.h](../src/kernel/screen.h) — the grid, the cell and the
  operations §4 maps onto.
- [src/kernel/key.h](../src/kernel/key.h) — `Key`, `KEY_*` and `MOD_*`, the
  input to §5.
- [src/user/tty.h](../src/user/tty.h) — `KeyInput` and `FullScreen`, the claims
  a program takes before it needs any of this.
