#include "chacha.h"

namespace {

constexpr u32 rotl32(u32 x, u32 n)
{
    return (x << n) | (x >> (32 - n));
}

// Byte-wise: no alignment assumed.
constexpr u32 load_le(const u8 *p)
{
    return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24;
}

void store_le(u8 *p, u32 v)
{
    p[0] = u8(v);
    p[1] = u8(v >> 8);
    p[2] = u8(v >> 16);
    p[3] = u8(v >> 24);
}

void quarter(u32 &a, u32 &b, u32 &c, u32 &d)
{
    a += b;
    d ^= a;
    d = rotl32(d, 16);
    c += d;
    b ^= c;
    b = rotl32(b, 12);
    a += b;
    d ^= a;
    d = rotl32(d, 8);
    c += d;
    b ^= c;
    b = rotl32(b, 7);
}

// "expand 32-byte k".
constexpr u32 SIGMA[4] = { 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574 };

} // namespace

void chacha20_block(const u8 key[CHACHA_KEY], u32 counter, const u8 nonce[CHACHA_NONCE],
                    u8 out[CHACHA_BLOCK])
{
    u32 s[16];
    for (usize i = 0; i < 4; i++)
        s[i] = SIGMA[i];
    for (usize i = 0; i < 8; i++)
        s[4 + i] = load_le(key + i * 4);
    s[12] = counter;
    for (usize i = 0; i < 3; i++)
        s[13 + i] = load_le(nonce + i * 4);

    u32 w[16];
    for (usize i = 0; i < 16; i++)
        w[i] = s[i];

    // Twenty rounds, as ten column-and-diagonal pairs.
    for (usize r = 0; r < 10; r++) {
        quarter(w[0], w[4], w[8], w[12]);
        quarter(w[1], w[5], w[9], w[13]);
        quarter(w[2], w[6], w[10], w[14]);
        quarter(w[3], w[7], w[11], w[15]);
        quarter(w[0], w[5], w[10], w[15]);
        quarter(w[1], w[6], w[11], w[12]);
        quarter(w[2], w[7], w[8], w[13]);
        quarter(w[3], w[4], w[9], w[14]);
    }

    for (usize i = 0; i < 16; i++)
        store_le(out + i * 4, w[i] + s[i]);
}

void ChaCha::seed(const u8 key[CHACHA_KEY])
{
    for (usize i = 0; i < CHACHA_KEY; i++)
        key_[i] = key[i];
    counter_ = 0;
    seeded_  = true;
}

void ChaCha::fill(u8 *out, usize n)
{
    constexpr u8 NONCE[CHACHA_NONCE] = {};

    while (n) {
        u8 block[CHACHA_BLOCK];
        chacha20_block(key_, counter_++, NONCE, block);
        for (usize i = 0; i < CHACHA_KEY; i++)
            key_[i] = block[i];

        usize take = n < CHACHA_KEY ? n : CHACHA_KEY;
        for (usize i = 0; i < take; i++)
            out[i] = block[CHACHA_KEY + i];
        out += take;
        n -= take;
    }
}
