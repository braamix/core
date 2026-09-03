// The screen: a grid of cells in linear memory, which the renderer reads
// directly (Concept.md §2.3, §3.5). Colours are fields and cursor addressing is
// indexing; an escape sequence is one encoding into that grid rather than the
// model, parsed by ansi.h on the way in (doc/ANSI_Escape_Codes.md).
//
// A Term is one grid and everything sticky about it: the style, the damage
// rectangle, the scrolling region, the scrollback ring and the view over it,
// and the parser's state. There is one per terminal, and every call below names
// the one it acts on.
#pragma once

#include "str.h"
#include "types.h"

struct Cell {
    char32_t ch; // 0 is blank
    u8 fg, bg;   // indices into the renderer's palette
    u8 attrs;    // ATTR_*
    u8 reserved; // pads to the stride the renderer assumes
};

static_assert(sizeof(Cell) == 8, "the renderer strides by 8 bytes");

enum : u8 {
    ATTR_BOLD      = 1,
    ATTR_UNDERLINE = 2,
    ATTR_REVERSE   = 4,
};

enum : u8 {
    COLOR_BLACK = 0,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE,
    COLOR_BRIGHT = 8, // added to any of the above
};

// The grid is clamped to this, so a bad geometry from the host cannot overflow
// the size computation or exhaust the heap. 1 MiB of cells at the maximum.
enum : u32 {
    SCREEN_MAX_COLS = 512,
    SCREEN_MAX_ROWS = 256,
};

// Rows of scrollback. A row costs cols * 8 bytes; the ring is allocated on the
// first scroll, and without it there is simply no scrollback.
enum : u32 { SCREEN_SCROLLBACK = 512 };

// The kernel writes 'BSCR' here; a renderer that reads anything else is looking
// at the wrong build, and should say so rather than draw noise (Concept.md §8.4).
enum : u32 { SCREEN_MAGIC = 0x42534352 };

// What resize() hands back to the host: everything the renderer needs to find
// and interpret the grid. Plain u32s, because JS reads it as a Uint32Array.
struct Screen {
    u32 magic;
    u32 cols, rows;
    u32 cursor_x, cursor_y; // cursor_x may equal cols: the wrap is deferred
    u32 cursor_on;
    u32 cells; // address of Cell[cols * rows]
};

// Terminals there may be; the host makes one by resizing it, so this bounds an
// id arriving from JS.
enum : u32 { TERM_MAX = 4 };

// resize()'s flags. A bare terminal gets its pump but no shell, so its grid
// and keyboard are free for whoever opens it (Sys::TermOpen).
enum : u32 { TERM_NO_SHELL = 1 };

// A terminal's grid and its sticky state. Opaque: reached through the calls
// below, as the file's statics were when there was one.
struct Term;

// The terminal with this id, or null past TERM_MAX or before anything made it.
Term *term_at(u32 id);

// The same, making it if it is not there. A new terminal has no grid until it
// is resized.
Term *term_open(u32 id);

// Which terminal this is: what /proc reports and what a damage rectangle names.
u32 term_id(const Term &t);

// Reallocates the grid, keeping the rows in use and dropping from the top when
// they no longer fit, and clamps the geometry to the limits above — so the host
// reads cols and rows back out of the descriptor rather than assuming it got
// what it asked for. Returns the descriptor's address, or 0 if the grid could
// not be allocated, in which case the existing grid is left untouched.
u32 screen_resize(Term &t, u32 cols, u32 rows);

const Screen &screen(const Term &t);

Cell *screen_cells(Term &t);

// Rows the grid has moved up, ever — a scroll's, and a resize's drop from the
// top. Nothing else counts them, so a writer holding an anchor row takes the
// difference across whatever it did to learn how far the anchor went.
u64 screen_scrolled(const Term &t);

// The scrollback view. While one is up the live grid is untouched underneath
// and `cells` points at a composed block instead, so that address moves when a
// view opens or closes. Negative pages back, clamped to the history there is;
// returns the resulting offset.
u32 screen_view_scroll(Term &t, i32 delta);

// Back to the live screen, and nothing when already there.
void screen_view_home(Term &t);

// Forgets the scrollback, bringing a view down first.
void screen_history_drop(Term &t);

// Rows the view sits above the live screen, and rows there are to page over.
u32 screen_view(const Term &t);
u32 screen_history(const Term &t);

// The cursor as the live screen has it; a view hides it.
bool screen_cursor_on(const Term &t);

void screen_style(Term &t, u8 fg, u8 bg, u8 attrs);

void screen_put(Term &t, char32_t ch);
void screen_write(Term &t, Str utf8);
void screen_newline(Term &t);
void screen_return(Term &t);

// One column back, erasing nothing; screen_backspace also blanks the cell.
void screen_left(Term &t);
void screen_backspace(Term &t);

void screen_move(Term &t, u32 x, u32 y);
void screen_cursor(Term &t, bool on);

void screen_clear(Term &t);

// Performs the deferred wrap if one is pending.
void screen_wrap(Term &t);

// The style the next put paints with.
void screen_style_get(const Term &t, u8 &fg, u8 &bg, u8 &attrs);

// ------------------------------------------------------- the scrolling region
//
// Rows top..bot, 0-origin and inclusive. Everything that scrolls moves rows
// inside it and leaves the rest of the grid alone. Inverted or out-of-range
// margins are refused, and a resize puts the whole screen back.

void screen_region(Term &t, u32 top, u32 bot);
u32 screen_region_top(const Term &t);
u32 screen_region_bot(const Term &t);

// Down one row, scrolling the region up at the bottom margin; up one row,
// scrolling it down at the top. The column is unchanged.
void screen_index(Term &t);
void screen_reverse_index(Term &t);

// The region's rows, n at a time. Rows leaving the top of a whole-screen region
// become scrollback and count in screen_scrolled(); a partial region's do
// neither. The cursor does not move.
void screen_scroll_up(Term &t, u32 n);
void screen_scroll_down(Term &t, u32 n);

// Rows at the cursor, within the region; nothing when the cursor is outside it.
void screen_insert_rows(Term &t, u32 n);
void screen_delete_rows(Term &t, u32 n);

// ------------------------------------------------------- cells and rows
//
// Every one of these writes blanks in the current background colour.

// Cells at the cursor, within its row; the row's tail falls off the end.
void screen_insert_cells(Term &t, u32 n);
void screen_delete_cells(Term &t, u32 n);

// n cells blank from the cursor, moving nothing.
void screen_erase_cells(Term &t, u32 n);

// 0 to the end of the row, 1 from its start, 2 the whole row.
void screen_erase_line(Term &t, u32 mode);

// 0 to the end of the screen, 1 from its start, 2 all of it. The cursor stays
// where it is, which is what tells this from screen_clear.
void screen_erase_display(Term &t, u32 mode);

struct Rect {
    u32 x, y, w, h;
};

// The cells changed since the last flush; w is 0 when none have. The cursor's
// own move is folded in by screen_flush, so it is not counted here.
Rect screen_damage(const Term &t);

// Marks a rectangle damaged, clipped to the grid. For a writer that fills cells
// through screen_cells() rather than through screen_put — the layout layer.
void screen_touch(Term &t, u32 x, u32 y, u32 w, u32 h);

// Sends every terminal's accumulated damage to the host, once each, and forgets
// it. One call at the end of a tick.
void screen_flush();

// Margins, modes, tab stops and the saved cursor, as ESC c leaves them; the
// grid and the style are untouched. A FullScreen claim takes it at both ends,
// and Sys::ScreenClear with it.
void screen_ansi_reset(Term &t);

// Drops the grid. For tests, so a case starts from nothing.
void screen_reset(Term &t);
