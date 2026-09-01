#include "harness.h"
#include "kernel/string.h"
#include "kernel/text.h"
#include "proc/filebuf.h"

namespace {

// A refill: the held bytes go to the front and `s` lands after them.
void refill(FileBuf &b, Str s)
{
    b.compact();
    usize n = s.size() < b.room() ? s.size() : b.room();
    if (n)
        __builtin_memcpy(b.tail(), s.data(), n);
    b.filled(n);
}

// The whole of `s` in one block, as a reader that has everything already.
void load(FileBuf &b, char *block, usize cap, Str s)
{
    b.adopt(block, cap);
    refill(b, s);
}

char32_t one(Str s)
{
    char block[32];
    FileBuf b;
    load(b, block, sizeof(block), s);

    char32_t ch = 0;
    CHECK(b.take(ch) == RuneStep::Got);
    return ch;
}

} // namespace

void test_filebuf()
{
    test_begin("filebuf");

    char block[16];

    // One codepoint of each width, whole.
    CHECK_EQ(one("A"), 'A');
    CHECK_EQ(one("\xc3\xa9"), 0x00e9);          // e acute
    CHECK_EQ(one("\xe2\x82\xac"), 0x20ac);      // euro sign
    CHECK_EQ(one("\xf0\x9f\x98\x80"), 0x1f600); // an emoji

    // A sequence the buffer does not finish is Need, and the same bytes plus
    // the rest of it are Got. Every width straddles, at every offset.
    {
        const char *runes[] = { "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80" };
        const usize lens[]  = { 2, 3, 4 };
        for (usize r = 0; r < 3; r++) {
            for (usize cut = 1; cut < lens[r]; cut++) {
                FileBuf b;
                load(b, block, sizeof(block), Str(runes[r], cut));

                char32_t ch = 0;
                CHECK(b.take(ch) == RuneStep::Need);
                CHECK_EQ(b.size(), cut);

                refill(b, Str(runes[r] + cut, lens[r] - cut));
                CHECK(b.take(ch) == RuneStep::Got);
                CHECK(b.empty());
            }
        }
    }

    // A rune that ends exactly at the last byte of the block, and one that is
    // cut by it: the second must survive the compaction the refill does.
    {
        FileBuf b;
        b.adopt(block, 4);
        refill(b, "ab\xc3\xa9"); // fills the block exactly
        CHECK_EQ(b.room(), 0);

        char32_t ch = 0;
        CHECK(b.take(ch) == RuneStep::Got);
        CHECK_EQ(ch, 'a');
        CHECK(b.take(ch) == RuneStep::Got);
        CHECK_EQ(ch, 'b');
        CHECK(b.take(ch) == RuneStep::Got);
        CHECK_EQ(ch, 0x00e9);
        CHECK(b.empty());
    }
    {
        FileBuf b;
        b.adopt(block, 4);
        refill(b, "abc\xc3"); // the last byte begins a rune the block cut
        char32_t ch = 0;
        for (usize i = 0; i < 3; i++)
            CHECK(b.take(ch) == RuneStep::Got);
        CHECK(b.take(ch) == RuneStep::Need);

        refill(b, "\xa9");
        CHECK(b.take(ch) == RuneStep::Got);
        CHECK_EQ(ch, 0x00e9);
    }

    // What end of input cut short is visible rather than dropped: one byte,
    // one U+FFFD, and the reader goes on.
    {
        FileBuf b;
        load(b, block, sizeof(block), "\xe2\x82");
        char32_t ch = 0;
        CHECK(b.take(ch) == RuneStep::Need);
        CHECK_EQ(b.take_broken(), 0xfffd);
        CHECK_EQ(b.size(), 1);
        CHECK_EQ(b.take_broken(), 0xfffd);
        CHECK(b.empty());
    }

    // unget puts the bytes back, so a reader that is not take() sees them too.
    {
        FileBuf b;
        load(b, block, sizeof(block), "abc");

        char32_t ch = 0;
        CHECK(b.take(ch) == RuneStep::Got);
        CHECK(b.unget('a'));
        CHECK_EQ(b.size(), 3);
        CHECK(b.held() == "abc");

        // A rune wider than the one that was taken still fits: the held bytes
        // move along to make room.
        CHECK(b.take(ch) == RuneStep::Got);
        CHECK(b.unget(0x1f600));
        CHECK_EQ(b.size(), 6);
        CHECK(b.take(ch) == RuneStep::Got);
        CHECK_EQ(ch, 0x1f600);
        CHECK(b.held() == "bc");
    }
    {
        // A full block has nowhere to put one.
        FileBuf b;
        b.adopt(block, 2);
        refill(b, "ab");
        CHECK(!b.unget('x'));
    }

    // Lines: one that spans two fills, and a last one with no newline.
    {
        FileBuf b;
        String out;

        b.adopt(block, sizeof(block));
        refill(b, "one\ntw");
        CHECK(b.take_line(out, false) == LineStep::Done);
        CHECK(out == "one");

        out.clear();
        CHECK(b.take_line(out, false) == LineStep::Need);
        CHECK(out == "tw");
        refill(b, "o\n");
        CHECK(b.take_line(out, false) == LineStep::Done);
        CHECK(out == "two");

        out.clear();
        refill(b, "three");
        CHECK(b.take_line(out, false) == LineStep::Need);
        CHECK(out == "three");
        CHECK(b.empty());

        // keep_nl leaves the newline where it was.
        out.clear();
        refill(b, "four\n");
        CHECK(b.take_line(out, true) == LineStep::Done);
        CHECK(out == "four\n");

        // An empty line is a line.
        out.clear();
        refill(b, "\n");
        CHECK(b.take_line(out, false) == LineStep::Done);
        CHECK(out.empty());
    }

    // has_line is what take_line would answer Done to, and it is what lets
    // File::getline answer from the buffer without entering a coroutine.
    {
        FileBuf b;
        String out;

        CHECK(!b.has_line()); // no block at all
        b.adopt(block, sizeof(block));
        CHECK(!b.has_line());

        refill(b, "frag");
        CHECK(!b.has_line());
        refill(b, "ment\nrest");
        CHECK(b.has_line());
        CHECK(b.take_line(out, false) == LineStep::Done);
        CHECK(out == "fragment");
        CHECK(!b.has_line());
    }

    // Writing: what fits goes in, what does not is refused whole.
    {
        FileBuf b;
        b.adopt(block, 8);
        CHECK(b.append("abc"));
        CHECK_EQ(b.size(), 3);
        CHECK(!b.append("123456"));
        CHECK_EQ(b.size(), 3);
        CHECK(b.append("de"));
        CHECK(b.held() == "abcde");

        // A partial write leaves the rest at the front once it compacts.
        b.consume(2);
        CHECK(b.held() == "cde");
        b.compact();
        CHECK_EQ(b.room(), 5);
        CHECK(b.held() == "cde");

        CHECK_EQ(b.append_rune(0x00e9), 2);
        CHECK_EQ(b.size(), 5);
        CHECK_EQ(b.append_rune(0x1f600), 0); // three left, four wanted
        CHECK_EQ(b.size(), 5);

        b.reset();
        CHECK(b.empty());
        CHECK_EQ(b.room(), 8);
    }

    // The case mapping the rune path is written for.
    CHECK_EQ(rune_lower('A'), 'a');
    CHECK_EQ(rune_upper('z'), 'Z');
    CHECK_EQ(rune_lower('1'), '1');
    CHECK_EQ(rune_lower(0x00c9), 0x00e9); // E acute
    CHECK_EQ(rune_upper(0x00e9), 0x00c9);
    CHECK_EQ(rune_lower(0x00d7), 0x00d7); // multiplication sign, not a letter
    CHECK_EQ(rune_upper(0x00ff), 0x0178); // y diaeresis
    CHECK_EQ(rune_lower(0x0178), 0x00ff);
    CHECK_EQ(rune_lower(0x0100), 0x0101); // A macron
    CHECK_EQ(rune_upper(0x0101), 0x0100);
    CHECK_EQ(rune_lower(0x0139), 0x013a); // L acute
    CHECK_EQ(rune_upper(0x013a), 0x0139);
    CHECK_EQ(rune_lower(0x017d), 0x017e); // Z caron
    CHECK_EQ(rune_upper(0x017e), 0x017d);
    CHECK_EQ(rune_lower(0x0130), 0x0130); // dotted I: not one codepoint for one
    CHECK_EQ(rune_upper(0x0131), 0x0131);
    CHECK_EQ(rune_lower(0x0391), 0x03b1); // alpha
    CHECK_EQ(rune_upper(0x03c9), 0x03a9); // omega
    CHECK_EQ(rune_upper(0x03c2), 0x03a3); // final sigma
    CHECK_EQ(rune_lower(0x0410), 0x0430); // A cyrillic
    CHECK_EQ(rune_upper(0x044f), 0x042f); // ya
    CHECK_EQ(rune_lower(0x0401), 0x0451); // yo
    CHECK_EQ(rune_upper(0x0451), 0x0401);
    CHECK_EQ(rune_lower(0x00df), 0x00df); // sharp s has no single uppercase
}
