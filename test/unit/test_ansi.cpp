#include "harness.h"
#include "kernel/screen.h"
#include "kernel/str.h"

namespace {

const Screen &g()
{
    return screen(t0());
}

const Cell &cell(u32 x, u32 y)
{
    return screen_cells(t0())[y * g().cols + x];
}

char32_t at(u32 x, u32 y)
{
    return cell(x, y).ch;
}

void w(Str text)
{
    screen_write(t0(), text);
}

// A blank grid with the parser at its defaults.
void fresh(u32 cols, u32 rows)
{
    screen_reset(t0());
    screen_resize(t0(), cols, rows);
}

} // namespace

void test_ansi()
{
    test_begin("ansi");

    // The state is the terminal's, so a sequence split by a buffer boundary is
    // one sequence (doc/ANSI_Escape_Codes.md §3, rule 1).
    fresh(8, 3);
    w("a\033[");
    w("31mb");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(1, 0), 'b');
    CHECK_EQ(cell(0, 0).fg, COLOR_WHITE);
    CHECK_EQ(cell(1, 0).fg, COLOR_RED);

    // A rune split the same way is held and finished, not dropped.
    fresh(8, 2);
    w(Str("\xd0", 1)); // the lead byte of П
    w("\x9f!");
    CHECK_EQ(at(0, 0), 0x41f);
    CHECK_EQ(at(1, 0), '!');

    // An unrecognised sequence vanishes rather than appearing as text.
    fresh(8, 2);
    w("\033[?12l");
    CHECK_EQ(at(0, 0), 0);
    CHECK_EQ(g().cursor_x, 0);
    w("\033[1yX"); // a final byte the table does not list
    CHECK_EQ(at(0, 0), 'X');

    // A string keeps a window title off the screen, ended either way.
    fresh(8, 2);
    w("\033]0;title\007X");
    CHECK_EQ(at(0, 0), 'X');
    CHECK_EQ(g().cursor_x, 1);
    fresh(8, 2);
    w("\033]0;t\033\\X");
    CHECK_EQ(at(0, 0), 'X');

    // A character-set designation is three bytes and does nothing.
    fresh(8, 2);
    w("\033(BA");
    CHECK_EQ(at(0, 0), 'A');
    CHECK_EQ(g().cursor_x, 1);

    // Past sixteen the parameters are dropped, and a runaway is abandoned;
    // neither is an error and neither paints the terminator.
    fresh(8, 2);
    w("\033[1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;31mX");
    CHECK_EQ(at(0, 0), 'X');
    CHECK_EQ(cell(0, 0).fg, COLOR_WHITE);
    fresh(8, 2);
    w("\033[1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;1;31mX");
    CHECK_EQ(at(0, 0), 'X');
    CHECK_EQ(cell(0, 0).fg, COLOR_WHITE);

    // 0x9B is a UTF-8 continuation byte, never a CSI (§6.3).
    fresh(8, 2);
    w("\x9b"
      "31mZ");
    CHECK_EQ(at(0, 0), 0xfffd);
    CHECK_EQ(at(1, 0), '3');
    CHECK_EQ(at(4, 0), 'Z');

    // ---------------------------------------------------------------- §4.1

    // A tab moves to the next stop and paints no cell.
    fresh(16, 2);
    w("a\tb");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(1, 0), 0);
    CHECK_EQ(at(8, 0), 'b');

    // At the deferred-wrap column it stops at the margin rather than wrapping.
    fresh(4, 2);
    w("abcd");
    CHECK_EQ(g().cursor_x, 4);
    w("\t");
    CHECK_EQ(g().cursor_x, 3);
    CHECK_EQ(g().cursor_y, 0);

    // HTS sets one, TBC takes them away.
    fresh(16, 2);
    w("\033[1;4H\033H\033[1;1Ha\tb");
    CHECK_EQ(at(3, 0), 'b');
    w("\033[3g\033[1;1Hx\ty");
    CHECK_EQ(at(15, 0), 'y');

    // BEL, DEL and the rest of C0 are ignored, not drawn.
    fresh(8, 2);
    w("a\007\001\177b");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(1, 0), 'b');
    CHECK_EQ(g().cursor_x, 2);

    // LNM is set at boot, so LF is a full new line; reset, it is an index and
    // keeps the column (§6.1).
    fresh(8, 3);
    w("ab\033[20l\ncd");
    CHECK_EQ(g().cursor_y, 1);
    CHECK_EQ(at(2, 1), 'c');
    w("\033[20h\nef");
    CHECK_EQ(at(0, 2), 'e');

    // ---------------------------------------------------------------- §4.3

    fresh(8, 4);
    w("\033[2;3Hx");
    CHECK_EQ(at(2, 1), 'x');
    w("\033[Hy"); // no parameters is home
    CHECK_EQ(at(0, 0), 'y');

    // Erasing in the line, all three ways, and none of them moves the cursor.
    fresh(8, 2);
    w("abcd\033[2D\033[K");
    CHECK_EQ(at(1, 0), 'b');
    CHECK_EQ(at(2, 0), 0);
    fresh(8, 2);
    w("abcd\033[1;3H\033[1K"); // from the start, the cursor cell included
    CHECK_EQ(at(0, 0), 0);
    CHECK_EQ(at(2, 0), 0);
    CHECK_EQ(at(3, 0), 'd');
    fresh(8, 2);
    w("abcd\033[2K");
    CHECK_EQ(at(0, 0), 0);
    CHECK_EQ(at(3, 0), 0);
    CHECK_EQ(g().cursor_x, 4);

    // ED 2 blanks the screen and leaves the cursor, which is what tells it
    // from screen_clear.
    fresh(4, 3);
    w("ab\ncd\033[2J");
    CHECK_EQ(at(0, 0), 0);
    CHECK_EQ(at(0, 1), 0);
    CHECK_EQ(g().cursor_x, 2);
    CHECK_EQ(g().cursor_y, 1);

    fresh(4, 3);
    w("abc\ndef\nghi\033[2;2H\033[J");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(0, 1), 'd');
    CHECK_EQ(at(1, 1), 0);
    CHECK_EQ(at(0, 2), 0);

    fresh(4, 3);
    w("abc\ndef\nghi\033[2;2H\033[1J");
    CHECK_EQ(at(0, 0), 0);
    CHECK_EQ(at(1, 1), 0);
    CHECK_EQ(at(2, 1), 'f');
    CHECK_EQ(at(0, 2), 'g');

    // ED 3 takes the scrollback with it, and brings a view down first.
    fresh(4, 2);
    w("a\nb\nc\nd");
    CHECK(screen_history(t0()) != 0);
    CHECK(screen_view_scroll(t0(), -1) != 0);
    w("\033[3J");
    CHECK_EQ(screen_history(t0()), 0u);
    CHECK_EQ(screen_view(t0()), 0u);
    CHECK_EQ(g().cells, u32(reinterpret_cast<usize>(screen_cells(t0()))));

    // ECH blanks in place; ICH and DCH shift the rest of the line.
    fresh(8, 2);
    w("abcd\033[1;2H\033[2X");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(1, 0), 0);
    CHECK_EQ(at(2, 0), 0);
    CHECK_EQ(at(3, 0), 'd');
    CHECK_EQ(g().cursor_x, 1);

    fresh(4, 2);
    w("abcd\033[1;2H\033[2@");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(1, 0), 0);
    CHECK_EQ(at(3, 0), 'b'); // c and d fell off the end

    fresh(4, 2);
    w("abcd\033[1;2H\033[2P");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(1, 0), 'd');
    CHECK_EQ(at(3, 0), 0);

    // Insert mode pushes the line right instead of overwriting it.
    fresh(8, 2);
    w("abcd\033[1;1H\033[4hXY");
    CHECK_EQ(at(0, 0), 'X');
    CHECK_EQ(at(1, 0), 'Y');
    CHECK_EQ(at(2, 0), 'a');

    // And at the deferred-wrap column it wraps first, so the row it is leaving
    // is not the one shifted.
    fresh(4, 3);
    w("\033[4habcd");
    CHECK_EQ(g().cursor_x, 4);
    w("e");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(3, 0), 'd');
    CHECK_EQ(at(0, 1), 'e');
    CHECK_EQ(g().cursor_y, 1);

    // ---------------------------------------------------- the scroll region

    fresh(4, 6);
    w("\033[2;4r");
    CHECK_EQ(screen_region_top(t0()), 1u);
    CHECK_EQ(screen_region_bot(t0()), 3u);
    CHECK_EQ(g().cursor_x, 0); // setting it homes the cursor
    CHECK_EQ(g().cursor_y, 0);

    // A newline at the bottom margin moves the region's rows and nothing else,
    // and a partial region is neither scrollback nor a grid that moved up.
    fresh(4, 6);
    w("a\nb\nc\nd\ne\nf\033[2;4r");
    u64 was  = screen_scrolled(t0());
    u32 hist = screen_history(t0());
    w("\033[4;1H\n");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(0, 1), 'c');
    CHECK_EQ(at(0, 2), 'd');
    CHECK_EQ(at(0, 3), 0);
    CHECK_EQ(at(0, 4), 'e');
    CHECK_EQ(screen_scrolled(t0()), was);
    CHECK_EQ(screen_history(t0()), hist);

    // RI at the top margin scrolls it the other way.
    fresh(4, 6);
    w("a\nb\nc\nd\ne\nf\033[2;4r\033[2;1H\033M");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(0, 1), 0);
    CHECK_EQ(at(0, 2), 'b');
    CHECK_EQ(at(0, 3), 'c');
    CHECK_EQ(at(0, 4), 'e');

    // Rows are inserted and deleted only with the cursor inside the region.
    fresh(4, 6);
    w("a\nb\nc\nd\ne\nf\033[2;4r\033[1;1H\033[2M");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(0, 1), 'b');
    fresh(4, 6);
    w("a\nb\nc\nd\ne\nf\033[2;4r\033[2;1H\033[1L");
    CHECK_EQ(at(0, 1), 0);
    CHECK_EQ(at(0, 2), 'b');
    CHECK_EQ(at(0, 3), 'c');
    CHECK_EQ(at(0, 4), 'e'); // d fell off the region's bottom

    // SU and SD leave the cursor alone, and a count past the region is capped.
    fresh(4, 4);
    w("a\nb\nc\nd\033[2;2H\033[1S");
    CHECK_EQ(g().cursor_x, 1);
    CHECK_EQ(g().cursor_y, 1);
    fresh(4, 6);
    w("a\nb\nc\nd\ne\nf\033[2;4r\033[100S");
    CHECK_EQ(at(0, 0), 'a');
    CHECK_EQ(at(0, 1), 0);
    CHECK_EQ(at(0, 3), 0);
    CHECK_EQ(at(0, 4), 'e');

    // A region that is the whole screen is today's behaviour exactly.
    fresh(4, 3);
    w("\033[1;3r");
    was = screen_scrolled(t0());
    w("a\nb\nc\nd");
    CHECK_EQ(screen_scrolled(t0()), was + 1);
    CHECK(screen_history(t0()) != 0);

    // No parameters puts the whole screen back, and so does a resize.
    fresh(4, 6);
    w("\033[2;4r\033[r");
    CHECK_EQ(screen_region_top(t0()), 0u);
    CHECK_EQ(screen_region_bot(t0()), 5u);
    w("\033[2;4r");
    CHECK(screen_resize(t0(), 4, 3) != 0);
    CHECK_EQ(screen_region_top(t0()), 0u);
    CHECK_EQ(screen_region_bot(t0()), 2u);

    // Autowrap off: a glyph at the last column overwrites it.
    fresh(4, 3);
    w("\033[?7labcdef");
    CHECK_EQ(g().cursor_y, 0);
    CHECK_EQ(at(3, 0), 'f');
    CHECK_EQ(at(0, 1), 0);
    w("\033[?7h");

    // ---------------------------------------------------------------- §4.4

    fresh(8, 2);
    w("\033[31;1mX\033[mY");
    CHECK_EQ(cell(0, 0).fg, COLOR_RED);
    CHECK_EQ(cell(0, 0).attrs, ATTR_BOLD);
    CHECK_EQ(cell(1, 0).fg, COLOR_WHITE); // an empty parameter list is 0
    CHECK_EQ(cell(1, 0).attrs, 0);

    fresh(8, 2);
    w("\033[94;101;4;7mX");
    CHECK_EQ(cell(0, 0).fg, COLOR_BLUE | COLOR_BRIGHT);
    CHECK_EQ(cell(0, 0).bg, COLOR_RED | COLOR_BRIGHT);
    CHECK_EQ(cell(0, 0).attrs, ATTR_UNDERLINE | ATTR_REVERSE);

    // Italic is cyan, and the colour it replaced comes back: a second 3m keeps
    // the first shadow, an explicit colour forgets it, and 0m resets both.
    fresh(8, 2);
    w("\033[34m\033[3mA\033[3mB\033[23mC");
    CHECK_EQ(cell(0, 0).fg, COLOR_CYAN);
    CHECK_EQ(cell(1, 0).fg, COLOR_CYAN);
    CHECK_EQ(cell(2, 0).fg, COLOR_BLUE);
    w("\033[34m\033[3m\033[31m\033[23mD");
    CHECK_EQ(cell(3, 0).fg, COLOR_RED);
    w("\033[3m\033[0mE");
    CHECK_EQ(cell(4, 0).fg, COLOR_WHITE);

    // 256 colours and 24-bit colour, quantised to the sixteen.
    fresh(8, 2);
    w("\033[38;5;9mA\033[38;5;196mB\033[48;2;0;0;255mC\033[38;2;0;128;0mD");
    CHECK_EQ(cell(0, 0).fg, COLOR_RED | COLOR_BRIGHT);
    CHECK_EQ(cell(1, 0).fg, COLOR_RED | COLOR_BRIGHT);
    CHECK_EQ(cell(2, 0).bg, COLOR_BLUE | COLOR_BRIGHT);
    CHECK_EQ(cell(3, 0).fg, COLOR_GREEN);

    // The style is read back rather than shadowed, so a Sys::Style between two
    // sequences stands.
    fresh(8, 2);
    screen_style(t0(), COLOR_GREEN, COLOR_BLACK, 0);
    w("\033[1mX");
    CHECK_EQ(cell(0, 0).fg, COLOR_GREEN);
    CHECK_EQ(cell(0, 0).attrs, ATTR_BOLD);

    // Erasing writes blanks in the current background.
    fresh(4, 2);
    w("\033[41m\033[2J");
    CHECK_EQ(cell(0, 0).bg, COLOR_RED);
    CHECK_EQ(cell(3, 1).bg, COLOR_RED);

    // ------------------------------------------------- saving and resetting

    fresh(8, 4);
    w("\033[3;3H\033[31m\0337\033[H\033[32mX\0338Y");
    CHECK_EQ(at(0, 0), 'X');
    CHECK_EQ(cell(0, 0).fg, COLOR_GREEN);
    CHECK_EQ(at(2, 2), 'Y');
    CHECK_EQ(cell(2, 2).fg, COLOR_RED);

    fresh(8, 4);
    w("\033[2;2H\033[s\033[H\033[uZ");
    CHECK_EQ(at(1, 1), 'Z');

    fresh(8, 2);
    w("\033[?25h");
    CHECK(screen_cursor_on(t0()));
    w("\033[?25l");
    CHECK(!screen_cursor_on(t0()));

    // RIS takes the margins, the modes, the style and the screen.
    fresh(8, 4);
    w("\033[2;3r\033[31m\033[4h\033[?7l\033[?25l\033cX");
    CHECK_EQ(screen_region_top(t0()), 0u);
    CHECK_EQ(screen_region_bot(t0()), 3u);
    CHECK_EQ(at(0, 0), 'X');
    CHECK_EQ(cell(0, 0).fg, COLOR_WHITE);
    CHECK(screen_cursor_on(t0()));

    screen_reset(t0());
}
