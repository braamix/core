#include "fs/chacha.h"
#include "harness.h"

namespace {

bool same(const u8 *a, const u8 *b, usize n)
{
    for (usize i = 0; i < n; i++)
        if (a[i] != b[i])
            return false;
    return true;
}

// RFC 8439 §2.3.2: key 00..1f, nonce 00 00 00 09 00 00 00 4a 00 00 00 00,
// counter 1.
constexpr u8 RFC_KEY[CHACHA_KEY] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
constexpr u8 RFC_NONCE[CHACHA_NONCE] = {
    0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x4a, 0x00, 0x00, 0x00, 0x00,
};
constexpr u8 RFC_OUT[CHACHA_BLOCK] = {
    0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4,
    0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03, 0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e,
    0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa, 0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
    0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e,
};

// RFC 8439 §A.1 vector 1: everything zero, counter 0.
constexpr u8 ZERO_KEY[CHACHA_KEY]     = {};
constexpr u8 ZERO_NONCE[CHACHA_NONCE] = {};
constexpr u8 ZERO_OUT[CHACHA_BLOCK]   = {
    0x76, 0xb8, 0xe0, 0xad, 0xa0, 0xf1, 0x3d, 0x90, 0x40, 0x5d, 0x6a, 0xe5, 0x53, 0x86, 0xbd, 0x28,
    0xbd, 0xd2, 0x19, 0xb8, 0xa0, 0x8d, 0xed, 0x1a, 0xa8, 0x36, 0xef, 0xcc, 0x8b, 0x77, 0x0d, 0xc7,
    0xda, 0x41, 0x59, 0x7c, 0x51, 0x57, 0x48, 0x8d, 0x77, 0x24, 0xe0, 0x3f, 0xb8, 0xd8, 0x4a, 0x37,
    0x6a, 0x43, 0xb8, 0xf4, 0x15, 0x18, 0xa1, 0x1c, 0xc3, 0x87, 0xb6, 0x69, 0xb2, 0xee, 0x65, 0x86,
};

} // namespace

void test_chacha()
{
    test_begin("chacha");

    // The two published vectors: one with the key set and one with nothing set,
    // which between them catch a mistyped sigma and a swapped counter/nonce.
    u8 block[CHACHA_BLOCK];
    chacha20_block(RFC_KEY, 1, RFC_NONCE, block);
    CHECK(same(block, RFC_OUT, sizeof block));

    chacha20_block(ZERO_KEY, 0, ZERO_NONCE, block);
    CHECK(same(block, ZERO_OUT, sizeof block));

    // The counter is state, not decoration.
    u8 next[CHACHA_BLOCK];
    chacha20_block(ZERO_KEY, 1, ZERO_NONCE, next);
    CHECK(!same(block, next, sizeof block));

    // The generator is exactly erasure over that block function: block i's
    // first half becomes the key and its second half is what leaves.
    u8 b0[CHACHA_BLOCK], b1[CHACHA_BLOCK];
    chacha20_block(RFC_KEY, 0, ZERO_NONCE, b0);
    chacha20_block(b0, 1, ZERO_NONCE, b1);

    ChaCha g;
    CHECK(!g.seeded());
    g.seed(RFC_KEY);
    CHECK(g.seeded());

    u8 out[96];
    g.fill(out, CHACHA_KEY);
    CHECK(same(out, b0 + CHACHA_KEY, CHACHA_KEY));
    g.fill(out, CHACHA_KEY);
    CHECK(same(out, b1 + CHACHA_KEY, CHACHA_KEY));

    // One key, one stream.
    ChaCha h;
    h.seed(RFC_KEY);
    u8 wide[96], narrow[96];
    h.fill(wide, sizeof wide);
    ChaCha k;
    k.seed(RFC_KEY);
    k.fill(narrow, 40);

    // And the stream does not depend on where the reads were cut: byte 32i
    // always comes from block i, because the tail is dropped rather than kept.
    CHECK(same(wide, narrow, 40));

    // A different key is a different stream.
    ChaCha other;
    other.seed(ZERO_KEY);
    other.fill(narrow, 40);
    CHECK(!same(wide, narrow, 40));

    // Nought writes nothing and advances nothing.
    ChaCha idle;
    idle.seed(RFC_KEY);
    idle.fill(out, 0);
    idle.fill(out, CHACHA_KEY);
    CHECK(same(out, b0 + CHACHA_KEY, CHACHA_KEY));

    // Every count is met exactly, on both sides of a block boundary.
    constexpr usize SIZES[] = { 1, 31, 32, 33, 65 };
    for (usize n : SIZES) {
        u8 pad[96 + 2];
        for (usize i = 0; i < sizeof pad; i++)
            pad[i] = 0xa5;

        ChaCha one;
        one.seed(RFC_KEY);
        one.fill(pad + 1, n);

        CHECK(pad[0] == 0xa5);
        CHECK(pad[n + 1] == 0xa5);
        CHECK(same(pad + 1, wide, n));
    }
}
