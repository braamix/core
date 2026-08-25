#include "harness.h"
#include "kernel/screen.h"
#include "kernel/text.h"

namespace {

// Encodes, then reads the codepoint back out of the grid through
// screen_write's decoder — the two halves have to agree.
void round_trip(char32_t ch, usize want_len)
{
    char b[4];
    usize n = utf8_encode(ch, b);
    CHECK_EQ(n, want_len);

    screen_move(0, 0);
    screen_write(Str(b, n));
    CHECK_EQ(u32(screen_cells()[0].ch), u32(ch));
}

// One decode: the codepoint and the bytes it took.
void decodes(Str bytes, char32_t want, usize want_len)
{
    char32_t got = 0;
    CHECK_EQ(utf8_decode(bytes, 0, got), want_len);
    CHECK_EQ(u32(got), u32(want));
}

Str raw(const u8 *b, usize n)
{
    return Str(reinterpret_cast<const char *>(b), n);
}

} // namespace

void test_text()
{
    test_begin("text");

    CHECK(is_space(' '));
    CHECK(is_space('\t'));
    CHECK(is_space('\n'));
    CHECK(!is_space('a'));
    CHECK(!is_space('\0'));
    CHECK(is_digit('0'));
    CHECK(is_digit('9'));
    CHECK(!is_digit('/'));
    CHECK(!is_digit(':'));

    screen_reset();
    CHECK(screen_resize(8, 2));

    round_trip('A', 1);     // 1 byte
    round_trip(0x00e9, 2);  // e acute
    round_trip(0x20ac, 3);  // euro sign
    round_trip(0x1f600, 4); // an emoji

    // A surrogate and an out-of-range value both become U+FFFD.
    {
        char b[4];
        CHECK_EQ(utf8_encode(char32_t(0xd800), b), 3);
        CHECK_EQ(u8(b[0]), 0xef);
        CHECK_EQ(utf8_encode(char32_t(0x110000), b), 3);
        CHECK_EQ(u8(b[0]), 0xef);
    }

    // The decoder's half of that, which used to be missing: every malformed
    // sequence is U+FFFD, and one shaped right but valued wrong takes the whole
    // of itself with it. f4 9b 96 8c is 0x11B58C, which killed the renderer.
    {
        const u8 over[]  = { 0xf4, 0x9b, 0x96, 0x8c };
        const u8 sur[]   = { 0xed, 0xa0, 0x80 };
        const u8 long3[] = { 0xe0, 0x80, 0xaf };
        const u8 lead[]  = { 0xf5, 0x80, 0x80, 0x80 };
        const u8 long2[] = { 0xc0, 0xaf };
        const u8 stray[] = { 0x80 };
        const u8 cut[]   = { 0xc3, 0x41 };
        const u8 euro[]  = { 0xe2, 0x82, 0xac };
        const u8 emoji[] = { 0xf0, 0x9f, 0x98, 0x80 };

        decodes(raw(over, 4), 0xfffd, 4);
        decodes(raw(sur, 3), 0xfffd, 3);
        decodes(raw(long3, 3), 0xfffd, 3);
        decodes(raw(lead, 4), 0xfffd, 1);
        decodes(raw(long2, 2), 0xfffd, 1);
        decodes(raw(stray, 1), 0xfffd, 1);
        decodes(raw(euro, 3), 0x20ac, 3);
        decodes(raw(emoji, 4), 0x1f600, 4);

        // A lead byte does not swallow what follows it: the 'A' is still there.
        decodes(raw(cut, 2), 0xfffd, 1);
        char32_t got = 0;
        CHECK_EQ(utf8_decode(raw(cut, 2), 1, got), 1);
        CHECK_EQ(u32(got), u32('A'));

        // 0 still means "need more", and only that.
        CHECK_EQ(utf8_decode(raw(euro, 2), 0, got), 0);
    }

    // No byte a program can write reaches a cell as something the host cannot
    // render — the invariant web/render.js draws on.
    {
        char all[256];
        for (usize i = 0; i < 256; i++)
            all[i] = char(u8(i));

        screen_reset();
        CHECK(screen_resize(64, 8));
        screen_write(Str(all, 256));
        for (usize i = 0; i < 64 * 8; i++)
            CHECK_EQ(u32(screen_cells()[i].ch), u32(rune_safe(screen_cells()[i].ch)));
    }

    // And a caller that lies outright is clamped at the cell.
    screen_reset();
    CHECK(screen_resize(4, 2));
    screen_put(char32_t(0x11b58c));
    screen_put(char32_t(0xd800));
    CHECK_EQ(u32(screen_cells()[0].ch), 0xfffd);
    CHECK_EQ(u32(screen_cells()[1].ch), 0xfffd);

    screen_reset();

    CHECK(!parse_u32("").has_value());
    CHECK_EQ(parse_u32("0").value(), 0);
    CHECK_EQ(parse_u32("7").value(), 7);
    CHECK_EQ(parse_u32("4294967295").value(), 4294967295u);
    CHECK(!parse_u32("4294967296").has_value());
    CHECK(!parse_u32("99999999999").has_value());
    CHECK(!parse_u32("12a").has_value());
    CHECK(!parse_u32("+1").has_value());
    CHECK(!parse_u32(" 1").has_value());
    CHECK(!parse_u32("-1").has_value());
    CHECK_EQ(parse_u32("0000000030").value(), 30);
}
