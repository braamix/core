#include "screen.h"

#include "alloc.h"
#include "ansi.h"
#include "host.h"
#include "text.h"
#include "traits.h"

// One terminal's grid and everything sticky about it.
struct Term {
    Screen g;
    Cell *cells;

    u32 id;
    bool made;

    u8 fg = COLOR_WHITE;
    u8 bg = COLOR_BLACK;
    u8 attrs;

    // The scrolling region, inclusive; 0 .. rows-1 is the whole screen.
    u32 rtop, rbot;

    // The parser a write goes through (doc/ANSI_Escape_Codes.md).
    Ansi ansi;

    // One damage rectangle, half-open, valid only while dirty (Concept.md §3.5).
    u32 x0, y0, x1, y1;
    bool dirty;

    // Where the renderer last drew the cursor, so flush can repaint the cell it
    // left. Keeping this in one place is what stops a mutation added later from
    // leaving a ghost behind.
    u32 drawn_x, drawn_y;

    // Rows the grid has moved up, ever. Nothing else counts them, and a writer
    // holding an anchor has no other way to learn its row went with them.
    u64 scrolled;

    // The scrollback ring: rows that have left the top, at the grid's own width.
    Cell *hist;
    u32 hist_cols;
    u32 hist_head;  // where the next row goes
    u32 hist_count; // rows held, up to SCREEN_SCROLLBACK

    // `cells` is always the live screen. While view_cells exists it is what
    // g.cells points at, showing the grid from `view` rows further back.
    Cell *view_cells;
    u32 view;
    bool view_dirty;  // recompose at the next flush
    bool cursor_live; // g.cursor_on, which a view forces to 0
};

namespace {

// Constant-initialised, so there is no static initialisation for --no-entry to
// skip. The record cannot fail to exist; only its grid allocates.
Term g_terms[TERM_MAX];

static_assert(is_trivially_destructible<Term>, "a global must not need atexit");

bool sized(const Term &t)
{
    return t.cells && t.g.cols && t.g.rows;
}

Cell blank(const Term &t)
{
    return Cell{ 0, t.fg, t.bg, t.attrs, 0 };
}

void damage(Term &t, u32 x, u32 y)
{
    if (!t.dirty) {
        t.x0    = x;
        t.y0    = y;
        t.x1    = x + 1;
        t.y1    = y + 1;
        t.dirty = true;
        return;
    }
    t.x0 = min(t.x0, x);
    t.y0 = min(t.y0, y);
    t.x1 = max(t.x1, x + 1);
    t.y1 = max(t.y1, y + 1);
}

void damage_all(Term &t)
{
    if (!sized(t))
        return;
    t.x0    = 0;
    t.y0    = 0;
    t.x1    = t.g.cols;
    t.y1    = t.g.rows;
    t.dirty = true;
}

// Rows y0..y1 inclusive, full width: what a partial region's scroll moved.
void damage_rows(Term &t, u32 y0, u32 y1)
{
    if (!sized(t))
        return;
    damage(t, 0, y0);
    damage(t, t.g.cols - 1, y1);
}

// The region, clamped to a grid that may have shrunk under it.
u32 region_top(const Term &t)
{
    return min(t.rtop, t.g.rows ? t.g.rows - 1 : 0);
}

u32 region_bot(const Term &t)
{
    return min(max(t.rbot, region_top(t)), t.g.rows ? t.g.rows - 1 : 0);
}

bool region_whole(const Term &t)
{
    return region_top(t) == 0 && t.g.rows && region_bot(t) == t.g.rows - 1;
}

// The cursor's column, never the one a deferred wrap parks past the last.
u32 cursor_col(const Term &t)
{
    return min(t.g.cursor_x, t.g.cols - 1);
}

void clear_row(const Term &t, Cell *cells, u32 cols, u32 y)
{
    Cell b = blank(t);
    for (u32 x = 0; x < cols; x++)
        cells[y * cols + x] = b;
}

// x0..x1 of one row, half-open.
void erase_span(Term &t, u32 y, u32 x0, u32 x1)
{
    Cell *row = t.cells + usize(y) * t.g.cols;
    Cell b    = blank(t);
    for (u32 i = x0; i < x1; i++)
        row[i] = b;
}

void hist_drop(Term &t)
{
    heap_free(t.hist);
    t.hist       = nullptr;
    t.hist_cols  = 0;
    t.hist_head  = 0;
    t.hist_count = 0;
}

// The k-th newest row, k = 1 being the one that left most recently.
const Cell *hist_row(const Term &t, u32 k)
{
    u32 at = (t.hist_head + SCREEN_SCROLLBACK - k) % SCREEN_SCROLLBACK;
    return t.hist + usize(at) * t.hist_cols;
}

// Keeps a row that is leaving the top, building the ring on first use.
void hist_push(Term &t, const Cell *row)
{
    if (t.hist && t.hist_cols != t.g.cols)
        hist_drop(t); // a width it cannot hold: the rewidth failed
    if (!t.hist) {
        t.hist =
            static_cast<Cell *>(heap_alloc(usize(SCREEN_SCROLLBACK) * t.g.cols * sizeof(Cell)));
        if (!t.hist)
            return; // no scrollback; the screen is otherwise unaffected
        t.hist_cols = t.g.cols;
    }
    __builtin_memcpy(t.hist + usize(t.hist_head) * t.hist_cols, row, t.hist_cols * sizeof(Cell));
    t.hist_head = (t.hist_head + 1) % SCREEN_SCROLLBACK;
    if (t.hist_count < SCREEN_SCROLLBACK)
        t.hist_count++;
}

// Carries the history across a width change, clipping or padding each row the
// way the grid itself is. Dropped if the new ring will not allocate.
void hist_rewidth(Term &t, u32 cols)
{
    if (!t.hist || t.hist_cols == cols)
        return;

    Cell *next = static_cast<Cell *>(heap_alloc(usize(SCREEN_SCROLLBACK) * cols * sizeof(Cell)));
    if (!next) {
        hist_drop(t);
        return;
    }

    u32 keep = min(cols, t.hist_cols);
    for (u32 i = 0; i < t.hist_count; i++) { // oldest first, unwinding the ring
        Cell *dst = next + usize(i) * cols;
        clear_row(t, next, cols, i);
        __builtin_memcpy(dst, hist_row(t, t.hist_count - i), keep * sizeof(Cell));
    }

    heap_free(t.hist);
    t.hist      = next;
    t.hist_cols = cols;
    t.hist_head = t.hist_count % SCREEN_SCROLLBACK;
}

// Fills the block the renderer reads: rows above the live screen out of the
// ring, the rest out of the grid.
void view_compose(Term &t)
{
    if (t.view > (t.hist ? t.hist_count : 0))
        t.view = t.hist ? t.hist_count : 0; // never index past the ring
    for (u32 i = 0; i < t.g.rows; i++) {
        Cell *dst = t.view_cells + usize(i) * t.g.cols;
        if (i < t.view)
            __builtin_memcpy(dst, hist_row(t, t.view - i), t.g.cols * sizeof(Cell));
        else
            __builtin_memcpy(dst, t.cells + usize(i - t.view) * t.g.cols, t.g.cols * sizeof(Cell));
    }
    t.view_dirty = false;
}

// Points the descriptor at a block of its own, leaving the live grid where it
// is for the writers.
bool view_open(Term &t)
{
    if (t.view_cells)
        return true;
    t.view_cells = static_cast<Cell *>(heap_alloc(usize(t.g.cols) * t.g.rows * sizeof(Cell)));
    if (!t.view_cells)
        return false;
    t.cursor_live = t.g.cursor_on;
    t.g.cursor_on = 0; // the cursor is on the live screen, not up here
    t.g.cells     = u32(reinterpret_cast<usize>(t.view_cells));
    return true;
}

// Rows top..bot, n at a time. `whole` sends what leaves the top to the
// scrollback and counts it: only a whole-screen scroll upwards does.
void scroll_span(Term &t, u32 top, u32 bot, u32 n, bool down, bool whole)
{
    if (!sized(t) || !n || top > bot || bot >= t.g.rows)
        return;
    n = min(n, bot - top + 1);

    if (down) {
        for (u32 y = bot + 1; y-- > top + n;)
            __builtin_memcpy(t.cells + usize(y) * t.g.cols, t.cells + usize(y - n) * t.g.cols,
                             t.g.cols * sizeof(Cell));
        for (u32 y = top; y < top + n; y++)
            clear_row(t, t.cells, t.g.cols, y);
    } else {
        if (whole)
            for (u32 i = 0; i < n; i++)
                hist_push(t, t.cells + usize(i) * t.g.cols); // the rows about to go
        for (u32 y = top; y + n <= bot; y++)
            __builtin_memcpy(t.cells + usize(y) * t.g.cols, t.cells + usize(y + n) * t.g.cols,
                             t.g.cols * sizeof(Cell));
        for (u32 y = bot + 1 - n; y <= bot; y++)
            clear_row(t, t.cells, t.g.cols, y);
    }

    if (!whole) {
        if (t.view)
            t.view_dirty = true; // the live rows under it moved
        damage_rows(t, top, bot);
        return;
    }

    t.drawn_y -= min(t.drawn_y, n);
    t.scrolled += n;

    // A view stays put: the offset grows with the live top, until the ring is
    // full and the clamp lets it drift. Composing is the flush's, since this
    // runs once per output line.
    if (t.view) {
        for (u32 i = 0; i < n && t.view < t.hist_count; i++)
            t.view++;
        t.view_dirty = true;
    }
    damage_all(t);
}

void scroll_rows(Term &t, u32 n, bool down)
{
    scroll_span(t, region_top(t), region_bot(t), n, down, region_whole(t) && !down);
}

// Down one row, scrolling the region when there is no row left inside it.
void next_row(Term &t)
{
    if (t.g.cursor_y == region_bot(t))
        scroll_rows(t, 1, false);
    else if (t.g.cursor_y + 1 < t.g.rows)
        t.g.cursor_y++;
}

// Up one row, scrolling the region down at its top margin.
void prev_row(Term &t)
{
    if (t.g.cursor_y == region_top(t))
        scroll_rows(t, 1, true);
    else if (t.g.cursor_y)
        t.g.cursor_y--;
}

// The wrap is deferred: cursor_x sits at cols until the next character is
// written, so filling the last column does not scroll the screen on its own.
void wrap_pending(Term &t)
{
    if (t.g.cursor_x >= t.g.cols) {
        t.g.cursor_x = 0;
        next_row(t);
    }
}

// One terminal's damage to the host, and forgotten.
void flush(Term &t)
{
    // Output has moved the rows under the view: once a tick, not once a row.
    if (t.view_dirty)
        view_compose(t);

    // The cursor is drawn, not stored, so moving it dirties two cells.
    if (sized(t) && (t.g.cursor_x != t.drawn_x || t.g.cursor_y != t.drawn_y)) {
        damage(t, min(t.drawn_x, t.g.cols - 1), min(t.drawn_y, t.g.rows - 1));
        damage(t, min(t.g.cursor_x, t.g.cols - 1), t.g.cursor_y);
        t.drawn_x = t.g.cursor_x;
        t.drawn_y = t.g.cursor_y;
    }
    if (!t.dirty)
        return;
    host_present(t.id, t.x0, t.y0, t.x1 - t.x0, t.y1 - t.y0);
    t.dirty = false;
}

} // namespace

Term *term_at(u32 id)
{
    if (id >= TERM_MAX || !g_terms[id].made)
        return nullptr;
    return &g_terms[id];
}

Term *term_open(u32 id)
{
    if (id >= TERM_MAX)
        return nullptr;
    if (!g_terms[id].made) { // every resize calls this; the parser is made once
        g_terms[id].id   = id;
        g_terms[id].made = true;
        ansi_reset(g_terms[id].ansi);
    }
    return &g_terms[id];
}

u32 term_id(const Term &t)
{
    return t.id;
}

u32 screen_resize(Term &t, u32 cols, u32 rows)
{
    cols = cols ? min(cols, u32(SCREEN_MAX_COLS)) : 1;
    rows = rows ? min(rows, u32(SCREEN_MAX_ROWS)) : 1;

    Cell *next = static_cast<Cell *>(heap_alloc(usize(cols) * rows * sizeof(Cell)));
    if (!next)
        return 0; // the old grid is still whole, and still the one on screen
    for (u32 y = 0; y < rows; y++)
        clear_row(t, next, cols, y);

    // After the bail: a resize that could not allocate leaves the view alone.
    screen_view_home(t);

    // Rows 0..cursor_y are the ones in use. Keep as many of them as fit,
    // dropping from the top, and land them at the top of the new grid — output
    // grows downwards from there, so that is where the eye already is.
    u32 live      = t.g.rows ? min(t.g.cursor_y + 1, t.g.rows) : 0;
    u32 keep_rows = min(rows, live);
    u32 keep_cols = min(cols, t.g.cols);
    if (t.cells && keep_rows && keep_cols) {
        u32 src0 = live - keep_rows;
        for (u32 i = 0; i < src0; i++)
            hist_push(t, t.cells + i * t.g.cols); // dropped from the top, so kept
        for (u32 i = 0; i < keep_rows; i++)
            __builtin_memcpy(next + i * cols, t.cells + (src0 + i) * t.g.cols,
                             keep_cols * sizeof(Cell));

        t.g.cursor_y = t.g.cursor_y - src0; // cursor_y is live - 1, so keep_rows - 1
        t.scrolled += src0;                 // rows off the top, as a scroll's are
        if (t.g.cursor_x > cols)
            t.g.cursor_x = cols;
    } else {
        t.g.cursor_x = t.g.cursor_y = 0;
    }

    heap_free(t.cells);
    hist_rewidth(t, cols); // after the free, so the peak is one grid lower
    t.cells   = next;
    t.g.magic = SCREEN_MAGIC;
    t.g.cols  = cols;
    t.g.rows  = rows;
    t.g.cells = u32(reinterpret_cast<usize>(next));
    t.drawn_x = t.g.cursor_x;
    t.drawn_y = t.g.cursor_y;
    t.rtop    = 0; // margins a shrunk grid cannot hold
    t.rbot    = rows - 1;
    damage_all(t);
    return u32(reinterpret_cast<usize>(&t.g));
}

const Screen &screen(const Term &t)
{
    return t.g;
}

Cell *screen_cells(Term &t)
{
    return t.cells;
}

u64 screen_scrolled(const Term &t)
{
    return t.scrolled;
}

void screen_style(Term &t, u8 fg, u8 bg, u8 attrs)
{
    t.fg    = fg;
    t.bg    = bg;
    t.attrs = attrs;
}

void screen_put(Term &t, char32_t ch)
{
    if (!sized(t))
        return;
    wrap_pending(t);
    t.cells[t.g.cursor_y * t.g.cols + t.g.cursor_x] = Cell{ rune_safe(ch), t.fg, t.bg, t.attrs, 0 };
    damage(t, t.g.cursor_x, t.g.cursor_y);
    t.g.cursor_x++;
}

void screen_write(Term &t, Str utf8)
{
    ansi_write(t, t.ansi, utf8);
}

void screen_newline(Term &t)
{
    if (!sized(t))
        return;
    t.g.cursor_x = 0;
    next_row(t);
}

void screen_return(Term &t)
{
    if (sized(t))
        t.g.cursor_x = 0;
}

// One column back, over a row boundary, erasing nothing: `\b \b' needs the
// first \b to leave the character standing.
void screen_left(Term &t)
{
    if (!sized(t))
        return;
    if (t.g.cursor_x) {
        t.g.cursor_x--;
    } else if (t.g.cursor_y) {
        t.g.cursor_x = t.g.cols - 1;
        t.g.cursor_y--;
    }
}

void screen_backspace(Term &t)
{
    if (!sized(t) || (t.g.cursor_x == 0 && t.g.cursor_y == 0))
        return;
    screen_left(t);
    t.cells[t.g.cursor_y * t.g.cols + t.g.cursor_x] = blank(t);
    damage(t, t.g.cursor_x, t.g.cursor_y);
}

void screen_move(Term &t, u32 x, u32 y)
{
    if (!sized(t))
        return;
    t.g.cursor_x = min(x, t.g.cols - 1);
    t.g.cursor_y = min(y, t.g.rows - 1);
}

void screen_cursor(Term &t, bool on)
{
    t.cursor_live = on;
    if (!t.view_cells) // a view hides it, and gives it back on the way home
        t.g.cursor_on = on;
    if (sized(t))
        damage(t, min(t.g.cursor_x, t.g.cols - 1), t.g.cursor_y);
}

bool screen_cursor_on(const Term &t)
{
    return t.cursor_live;
}

void screen_clear(Term &t)
{
    if (!sized(t))
        return;
    for (u32 y = 0; y < t.g.rows; y++)
        clear_row(t, t.cells, t.g.cols, y);
    t.g.cursor_x = t.g.cursor_y = 0;
    damage_all(t);
}

void screen_wrap(Term &t)
{
    if (sized(t))
        wrap_pending(t);
}

void screen_style_get(const Term &t, u8 &fg, u8 &bg, u8 &attrs)
{
    fg    = t.fg;
    bg    = t.bg;
    attrs = t.attrs;
}

void screen_region(Term &t, u32 top, u32 bot)
{
    if (!sized(t) || top > bot || bot >= t.g.rows)
        return;
    t.rtop = top;
    t.rbot = bot;
}

u32 screen_region_top(const Term &t)
{
    return region_top(t);
}

u32 screen_region_bot(const Term &t)
{
    return region_bot(t);
}

void screen_index(Term &t)
{
    if (sized(t))
        next_row(t);
}

void screen_reverse_index(Term &t)
{
    if (sized(t))
        prev_row(t);
}

void screen_scroll_up(Term &t, u32 n)
{
    scroll_rows(t, n, false);
}

void screen_scroll_down(Term &t, u32 n)
{
    scroll_rows(t, n, true);
}

void screen_insert_rows(Term &t, u32 n)
{
    if (sized(t) && t.g.cursor_y >= region_top(t) && t.g.cursor_y <= region_bot(t))
        scroll_span(t, t.g.cursor_y, region_bot(t), n, true, false);
}

void screen_delete_rows(Term &t, u32 n)
{
    if (sized(t) && t.g.cursor_y >= region_top(t) && t.g.cursor_y <= region_bot(t))
        scroll_span(t, t.g.cursor_y, region_bot(t), n, false, false);
}

void screen_insert_cells(Term &t, u32 n)
{
    if (!sized(t) || !n)
        return;
    u32 x = cursor_col(t), y = t.g.cursor_y;
    n         = min(n, t.g.cols - x);
    Cell *row = t.cells + usize(y) * t.g.cols;
    for (u32 i = t.g.cols; i-- > x + n;)
        row[i] = row[i - n];
    Cell b = blank(t);
    for (u32 i = x; i < x + n; i++)
        row[i] = b;
    damage_rows(t, y, y);
}

void screen_delete_cells(Term &t, u32 n)
{
    if (!sized(t) || !n)
        return;
    u32 x = cursor_col(t), y = t.g.cursor_y;
    n         = min(n, t.g.cols - x);
    Cell *row = t.cells + usize(y) * t.g.cols;
    for (u32 i = x; i + n < t.g.cols; i++)
        row[i] = row[i + n];
    Cell b = blank(t);
    for (u32 i = t.g.cols - n; i < t.g.cols; i++)
        row[i] = b;
    damage_rows(t, y, y);
}

void screen_erase_cells(Term &t, u32 n)
{
    if (!sized(t) || !n)
        return;
    u32 x = cursor_col(t);
    erase_span(t, t.g.cursor_y, x, x + min(n, t.g.cols - x));
    damage_rows(t, t.g.cursor_y, t.g.cursor_y);
}

void screen_erase_line(Term &t, u32 mode)
{
    if (!sized(t))
        return;
    u32 x = cursor_col(t), y = t.g.cursor_y;
    erase_span(t, y, mode == 0 ? x : 0, mode == 1 ? x + 1 : t.g.cols);
    damage_rows(t, y, y);
}

void screen_erase_display(Term &t, u32 mode)
{
    if (!sized(t))
        return;
    u32 x = cursor_col(t), y = t.g.cursor_y;
    if (mode == 0) {
        erase_span(t, y, x, t.g.cols);
        for (u32 i = y + 1; i < t.g.rows; i++)
            clear_row(t, t.cells, t.g.cols, i);
    } else if (mode == 1) {
        for (u32 i = 0; i < y; i++)
            clear_row(t, t.cells, t.g.cols, i);
        erase_span(t, y, 0, x + 1);
    } else {
        for (u32 i = 0; i < t.g.rows; i++)
            clear_row(t, t.cells, t.g.cols, i);
    }
    damage_all(t);
}

void screen_history_drop(Term &t)
{
    screen_view_home(t);
    hist_drop(t);
}

void screen_ansi_reset(Term &t)
{
    ansi_reset(t.ansi);
    t.rtop = 0;
    t.rbot = t.g.rows ? t.g.rows - 1 : 0;
}

Rect screen_damage(const Term &t)
{
    if (!t.dirty)
        return Rect{ 0, 0, 0, 0 };
    return Rect{ t.x0, t.y0, t.x1 - t.x0, t.y1 - t.y0 };
}

void screen_touch(Term &t, u32 x, u32 y, u32 w, u32 h)
{
    if (!sized(t) || !w || !h || x >= t.g.cols || y >= t.g.rows)
        return;
    u32 x1 = min(x + w, t.g.cols);
    u32 y1 = min(y + h, t.g.rows);

    // A blit is a memcpy of whatever a process staged, and this call is the
    // only notice the grid gets of it (Concept.md §2.3).
    for (u32 row = y; row < y1; row++)
        for (u32 col = x; col < x1; col++) {
            Cell &c = t.cells[row * t.g.cols + col];
            c.ch    = rune_safe(c.ch);
        }

    damage(t, x, y);
    damage(t, x1 - 1, y1 - 1);
}

u32 screen_view(const Term &t)
{
    return t.view;
}

u32 screen_history(const Term &t)
{
    return t.hist ? t.hist_count : 0;
}

u32 screen_view_scroll(Term &t, i32 delta)
{
    if (!sized(t))
        return 0;

    i64 want = i64(t.view) - i64(delta); // back is negative, and pages upwards
    if (want < 0)
        want = 0;
    if (want > i64(screen_history(t)))
        want = i64(screen_history(t));

    if (u32(want) == t.view)
        return t.view; // at that end already: damage nothing
    if (want == 0) {
        screen_view_home(t);
        return 0;
    }
    if (!view_open(t))
        return t.view; // nothing to compose into; stay put

    t.view = u32(want);
    view_compose(t);
    damage_all(t);
    return t.view;
}

void screen_view_home(Term &t)
{
    if (!t.view_cells)
        return; // the common case: every keystroke comes through here

    heap_free(t.view_cells);
    t.view_cells  = nullptr;
    t.view        = 0;
    t.view_dirty  = false;
    t.g.cells     = u32(reinterpret_cast<usize>(t.cells));
    t.g.cursor_on = t.cursor_live;
    damage_all(t);
}

void screen_flush()
{
    for (Term &t : g_terms)
        if (t.made)
            flush(t);
}

void screen_reset(Term &t)
{
    heap_free(t.view_cells);
    t.view_cells = nullptr;
    t.view       = 0;
    t.view_dirty = false;
    hist_drop(t);

    heap_free(t.cells);
    u32 id = t.id;

    t.cells       = nullptr;
    t.g           = Screen{};
    t.fg          = COLOR_WHITE;
    t.bg          = COLOR_BLACK;
    t.attrs       = 0;
    t.dirty       = false;
    t.cursor_live = false;
    t.drawn_x = t.drawn_y = 0;
    t.scrolled            = 0;
    t.rtop = t.rbot = 0;
    t.id            = id;
    t.made          = true;
    ansi_reset(t.ansi);
}
