#include "ansi.h"

#include "text.h"
#include "traits.h"

static_assert(is_trivially_destructible<Ansi>, "a global must not need atexit");

namespace {

// A sequence longer than this is abandoned; a string is discarded past this.
enum : u32 { SEQ_MAX = 64, STR_MAX = 4096 };

u32 cols(const Term &t)
{
    return screen(t).cols;
}

u32 rows(const Term &t)
{
    return screen(t).rows;
}

// The cursor's column, never the parked one a deferred wrap leaves.
u32 cur_x(const Term &t)
{
    return min(screen(t).cursor_x, cols(t) - 1);
}

u32 cur_y(const Term &t)
{
    return screen(t).cursor_y;
}

// An absent parameter is 0; one that means a count reads 0 as 1.
u32 par(const Ansi &a, u32 i)
{
    return i <= a.nparam && i < ANSI_PARAMS ? a.param[i] : 0;
}

u32 cnt(const Ansi &a, u32 i)
{
    u32 v = par(a, i);
    return v ? v : 1;
}

bool tab_at(const Ansi &a, u32 x)
{
    return x < SCREEN_MAX_COLS && (a.tabs[x / 32] >> (x % 32)) & 1;
}

void tab_set(Ansi &a, u32 x, bool on)
{
    if (x >= SCREEN_MAX_COLS)
        return;
    if (on)
        a.tabs[x / 32] |= 1u << (x % 32);
    else
        a.tabs[x / 32] &= ~(1u << (x % 32));
}

// The next stop past x, or the right margin; never a wrap.
u32 tab_next(const Ansi &a, u32 x, u32 w)
{
    for (u32 i = x + 1; i < w; i++)
        if (tab_at(a, i))
            return i;
    return w - 1;
}

u32 tab_prev(const Ansi &a, u32 x)
{
    for (u32 i = x; i-- > 0;)
        if (tab_at(a, i))
            return i;
    return 0;
}

// The nearest of the sixteen. The palette is r + 2g + 4b, so a bit per channel
// against half the brightest; a near-grey goes by brightness instead.
u8 quantise(u32 r, u32 g, u32 b)
{
    r      = min(r, 255u);
    g      = min(g, 255u);
    b      = min(b, 255u);
    u32 mx = max(r, max(g, b)), mn = min(r, min(g, b));
    if (mx - mn < 40)
        return mx < 64    ? u8(COLOR_BLACK)
               : mx < 160 ? u8(COLOR_BLACK | COLOR_BRIGHT)
               : mx < 224 ? u8(COLOR_WHITE)
                          : u8(COLOR_WHITE | COLOR_BRIGHT);
    u32 mid = mx / 2;
    u8 c    = u8((r > mid ? 1 : 0) | (g > mid ? 2 : 0) | (b > mid ? 4 : 0));
    return mx >= 200 ? u8(c | COLOR_BRIGHT) : c;
}

// 0-15 are the palette itself; then the 6x6x6 cube, then the grey ramp.
u8 xterm256(u32 n)
{
    if (n < 16)
        return u8(n);
    if (n < 232) {
        u32 i = n - 16;
        return quantise((i / 36) * 51, (i / 6 % 6) * 51, (i % 6) * 51);
    }
    if (n < 256) {
        u32 v = 8 + (n - 232) * 10;
        return quantise(v, v, v);
    }
    return COLOR_WHITE;
}

void save_cursor(Term &t, Ansi &a)
{
    a.save_x = screen(t).cursor_x;
    a.save_y = screen(t).cursor_y;
    screen_style_get(t, a.save_fg, a.save_bg, a.save_attrs);
    a.save_italic    = a.italic;
    a.save_italic_fg = a.italic_fg;
}

void restore_cursor(Term &t, Ansi &a)
{
    screen_move(t, a.save_x, a.save_y);
    screen_style(t, a.save_fg, a.save_bg, a.save_attrs);
    a.italic    = a.save_italic;
    a.italic_fg = a.save_italic_fg;
}

// ESC c.
void full_reset(Term &t, Ansi &a)
{
    ansi_reset(a);
    screen_region(t, 0, rows(t) ? rows(t) - 1 : 0);
    screen_style(t, COLOR_WHITE, COLOR_BLACK, 0);
    screen_cursor(t, true);
    screen_clear(t);
}

// §4.4. Read, apply, write back: a Sys::Style between two sequences stands.
void sgr(Term &t, Ansi &a)
{
    u8 fg, bg, attrs;
    screen_style_get(t, fg, bg, attrs);

    for (u32 i = 0; i <= a.nparam; i++) {
        u32 p = par(a, i);
        switch (p) {
        case 0:
            fg          = COLOR_WHITE;
            bg          = COLOR_BLACK;
            attrs       = 0;
            a.italic    = false;
            a.italic_fg = COLOR_WHITE;
            break;
        case 1:
            attrs |= ATTR_BOLD;
            break;
        case 22:
            attrs = u8(attrs & ~ATTR_BOLD);
            break;
        case 4:
            attrs |= ATTR_UNDERLINE;
            break;
        case 24:
            attrs = u8(attrs & ~ATTR_UNDERLINE);
            break;
        case 7:
            attrs |= ATTR_REVERSE;
            break;
        case 27:
            attrs = u8(attrs & ~ATTR_REVERSE);
            break;
        // Italic is cyan, and the colour it replaced comes back. A second 3
        // keeps the first shadow; any explicit foreground forgets it.
        case 3:
            if (!a.italic) {
                a.italic_fg = fg;
                a.italic    = true;
            }
            fg = COLOR_CYAN;
            break;
        case 23:
            if (a.italic) {
                fg       = a.italic_fg;
                a.italic = false;
            }
            break;
        case 39:
            fg       = COLOR_WHITE;
            a.italic = false;
            break;
        case 49:
            bg = COLOR_BLACK;
            break;
        case 38:
        case 48: {
            u8 c;
            if (par(a, i + 1) == 5 && i + 2 <= a.nparam) {
                c = xterm256(par(a, i + 2));
                i += 2;
            } else if (par(a, i + 1) == 2 && i + 4 <= a.nparam) {
                c = quantise(par(a, i + 2), par(a, i + 3), par(a, i + 4));
                i += 4;
            } else {
                i = a.nparam; // short of parameters
                break;
            }
            if (p == 38) {
                fg       = c;
                a.italic = false;
            } else {
                bg = c;
            }
            break;
        }
        default:
            if (p >= 30 && p <= 37) {
                fg       = u8(p - 30);
                a.italic = false;
            } else if (p >= 40 && p <= 47) {
                bg = u8(p - 40);
            } else if (p >= 90 && p <= 97) {
                fg       = u8(p - 90 + COLOR_BRIGHT);
                a.italic = false;
            } else if (p >= 100 && p <= 107) {
                bg = u8(p - 100 + COLOR_BRIGHT);
            }
            break;
        }
    }
    screen_style(t, fg, bg, attrs);
}

// §4.5. Every parameter in the list.
void mode(Term &t, Ansi &a, bool set)
{
    for (u32 i = 0; i <= a.nparam; i++) {
        u32 p = par(a, i);
        if (a.priv) {
            if (p == 7)
                a.awm = set;
            else if (p == 25)
                screen_cursor(t, set);
        } else if (p == 4) {
            a.irm = set;
        } else if (p == 20) {
            a.lnm = set;
        }
    }
}

// §4.3. A private final other than h or l is swallowed.
void csi(Term &t, Ansi &a, u8 fin)
{
    if (a.priv) {
        if (fin == 'h' || fin == 'l')
            mode(t, a, fin == 'h');
        return;
    }

    u32 n = cnt(a, 0), sel = par(a, 0);
    u32 x = cur_x(t), y = cur_y(t);
    u32 top = screen_region_top(t), bot = screen_region_bot(t);

    switch (fin) {
    case '@':
        screen_insert_cells(t, n);
        break;
    case 'A':
    case 'F': {
        u32 lim = y >= top ? top : 0;
        screen_move(t, fin == 'A' ? x : 0, y - min(n, y - lim));
        break;
    }
    case 'B':
    case 'E': {
        u32 lim = y <= bot ? bot : rows(t) - 1;
        screen_move(t, fin == 'B' ? x : 0, y + min(n, lim - y));
        break;
    }
    case 'C':
        screen_move(t, min(x + n, cols(t) - 1), y);
        break;
    case 'D':
        screen_move(t, x - min(n, x), y);
        break;
    case 'G':
    case '`':
        screen_move(t, min(n, cols(t)) - 1, y);
        break;
    case 'H':
    case 'f':
        screen_move(t, cnt(a, 1) - 1, n - 1);
        break;
    case 'I':
        for (u32 i = 0; i < n; i++)
            x = tab_next(a, x, cols(t));
        screen_move(t, x, y);
        break;
    case 'Z':
        for (u32 i = 0; i < n; i++)
            x = tab_prev(a, x);
        screen_move(t, x, y);
        break;
    case 'J':
        screen_erase_display(t, sel);
        if (sel == 3)
            screen_history_drop(t);
        break;
    case 'K':
        screen_erase_line(t, sel);
        break;
    case 'L':
        screen_insert_rows(t, n);
        break;
    case 'M':
        screen_delete_rows(t, n);
        break;
    case 'P':
        screen_delete_cells(t, n);
        break;
    case 'S':
        screen_scroll_up(t, n);
        break;
    case 'T':
        screen_scroll_down(t, n);
        break;
    case 'X':
        screen_erase_cells(t, n);
        break;
    case 'd':
        screen_move(t, x, min(n, rows(t)) - 1);
        break;
    case 'g':
        if (sel == 0)
            tab_set(a, x, false);
        else if (sel == 3)
            for (u32 i = 0; i < SCREEN_MAX_COLS / 32; i++)
                a.tabs[i] = 0;
        break;
    case 'h':
    case 'l':
        mode(t, a, fin == 'h');
        break;
    case 'm':
        sgr(t, a);
        break;
    case 'r': {
        u32 bottom = par(a, 1) ? par(a, 1) : rows(t);
        if (n < bottom && bottom <= rows(t)) {
            screen_region(t, n - 1, bottom - 1);
            screen_move(t, 0, 0);
        }
        break;
    }
    case 's':
        save_cursor(t, a);
        break;
    case 'u':
        restore_cursor(t, a);
        break;
    // DSR, DA, window operations and the cursor's shape: nothing answers.
    default:
        break;
    }
}

// §4.2, once the sequence has no intermediate byte.
void esc(Term &t, Ansi &a, u8 fin)
{
    switch (fin) {
    case '7':
        save_cursor(t, a);
        break;
    case '8':
        restore_cursor(t, a);
        break;
    case 'D':
        screen_index(t);
        break;
    case 'E': // always both motions, whatever LNM says
        screen_return(t);
        screen_index(t);
        break;
    case 'M':
        screen_reverse_index(t);
        break;
    case 'H':
        tab_set(a, cur_x(t), true);
        break;
    case 'c':
        full_reset(t, a);
        break;
    default:
        break;
    }
}

void csi_begin(Ansi &a)
{
    a.state  = ANSI_CSI;
    a.nparam = 0;
    a.priv = a.inter = a.bad = false;
    a.len                    = 0;
    for (u32 i = 0; i < ANSI_PARAMS; i++)
        a.param[i] = 0;
}

void to_esc(Ansi &a)
{
    a.state = ANSI_ESC;
    a.len   = 0;
    a.inter = false;
}

// §4.1.
void c0(Term &t, Ansi &a, u8 b)
{
    switch (b) {
    case 0x08:
        screen_left(t);
        break;
    case 0x09:
        screen_move(t, tab_next(a, cur_x(t), cols(t)), cur_y(t));
        break;
    case 0x0a:
        if (a.lnm)
            screen_newline(t);
        else
            screen_index(t);
        break;
    case 0x0d:
        screen_return(t);
        break;
    case 0x1b:
        to_esc(a);
        break;
    default: // BEL, DEL and every other C0 byte
        break;
    }
}

void glyph(Term &t, Ansi &a, char32_t ch)
{
    if (a.awm)
        screen_wrap(t);
    else if (screen(t).cursor_x >= cols(t))
        screen_move(t, cols(t) - 1, cur_y(t));
    if (a.irm)
        screen_insert_cells(t, 1);
    screen_put(t, ch);
}

// One byte, in any state but Ground.
void step(Term &t, Ansi &a, u8 b)
{
    switch (a.state) {
    case ANSI_ESC:
        if (b == 0x1b)
            return;
        if (b == '[') {
            csi_begin(a);
            return;
        }
        if (b == ']' || b == 'P' || b == '^' || b == '_' || b == 'X') {
            a.state = ANSI_STR;
            a.len   = 0;
            return;
        }
        if (b >= 0x20 && b <= 0x2f) { // a designation: ESC ( B and its kin
            a.inter = true;
            if (++a.len > SEQ_MAX)
                a.state = ANSI_GROUND;
            return;
        }
        a.state = ANSI_GROUND;
        if (!a.inter)
            esc(t, a, b);
        return;

    case ANSI_CSI:
        if (b == 0x1b) {
            to_esc(a);
            return;
        }
        if (++a.len > SEQ_MAX)
            a.bad = true;
        if (b >= '0' && b <= '9') {
            if (!a.inter)
                a.param[a.nparam] = u16(min(u32(a.param[a.nparam]) * 10 + u32(b - '0'), 65535u));
            return;
        }
        if (b == ';' || b == ':') { // a colon reads as a semicolon
            if (a.nparam + 1 < ANSI_PARAMS)
                a.param[++a.nparam] = 0;
            return;
        }
        if (a.len == 1 && (b == '?' || b == '<' || b == '=' || b == '>')) {
            a.priv = true;
            return;
        }
        if (b >= 0x20 && b <= 0x2f) {
            a.inter = true;
            return;
        }
        if (b >= 0x40 && b <= 0x7e) {
            a.state = ANSI_GROUND;
            if (!a.bad && !a.inter)
                csi(t, a, b);
            return;
        }
        a.bad = true;
        return;

    case ANSI_STR:
        if (b == 0x07)
            a.state = ANSI_GROUND;
        else if (b == 0x1b)
            a.state = ANSI_STR_ESC;
        else if (++a.len > STR_MAX)
            a.state = ANSI_GROUND;
        return;

    default: // ANSI_STR_ESC
        if (b == '\\')
            a.state = ANSI_GROUND;
        else if (b != 0x1b)
            a.state = ANSI_STR;
        return;
    }
}

// Completes a rune held from the last write. Returns the bytes taken from it.
usize pending(Term &t, Ansi &a, Str utf8)
{
    char buf[8];
    u32 n    = a.pend_n;
    u32 take = u32(min(utf8.size(), usize(4)));
    for (u32 k = 0; k < n; k++)
        buf[k] = char(a.pend[k]);
    for (u32 k = 0; k < take; k++)
        buf[n + k] = utf8[k];

    char32_t ch;
    usize len = utf8_decode(Str(buf, n + take), 0, ch);
    if (!len) { // still short, and this write is over
        a.pend_n = u8(min(n + take, 4u));
        for (u32 k = n; k < a.pend_n; k++)
            a.pend[k] = u8(buf[k]);
        return take;
    }
    a.pend_n = 0;
    glyph(t, a, ch);
    return len > n ? len - n : 0;
}

} // namespace

void ansi_reset(Ansi &a)
{
    a                = Ansi{};
    a.lnm            = true;
    a.awm            = true;
    a.italic_fg      = COLOR_WHITE;
    a.save_fg        = COLOR_WHITE;
    a.save_bg        = COLOR_BLACK;
    a.save_italic_fg = COLOR_WHITE;
    for (u32 x = 0; x < SCREEN_MAX_COLS; x += 8)
        a.tabs[x / 32] |= 1u << (x % 32);
}

void ansi_write(Term &t, Ansi &a, Str utf8)
{
    if (!screen(t).cols || !screen(t).rows)
        return;

    usize i = 0;
    if (a.pend_n)
        i = pending(t, a, utf8);

    while (i < utf8.size()) {
        u8 b = u8(utf8[i]);
        if (a.state != ANSI_GROUND) {
            i++;
            step(t, a, b);
            continue;
        }
        if (b < 0x20 || b == 0x7f) {
            i++;
            c0(t, a, b);
            continue;
        }

        char32_t ch;
        usize len = utf8_decode(utf8, i, ch);
        if (!len) { // a rune the next write finishes
            a.pend_n = u8(min(utf8.size() - i, usize(4)));
            for (u32 k = 0; k < a.pend_n; k++)
                a.pend[k] = u8(utf8[i + k]);
            return;
        }
        i += len;
        glyph(t, a, ch);
    }
}
