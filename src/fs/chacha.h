// ChaCha20 (RFC 8439 §2.3) and the fast-key-erasure generator over it, which is
// what /dev/urandom hands out (Concept.md §5.1).
#pragma once

#include "kernel/types.h"

constexpr usize CHACHA_KEY   = 32;
constexpr usize CHACHA_NONCE = 12;
constexpr usize CHACHA_BLOCK = 64;

// One block of keystream. `counter` is state word 12.
void chacha20_block(const u8 key[CHACHA_KEY], u32 counter, const u8 nonce[CHACHA_NONCE],
                    u8 out[CHACHA_BLOCK]);

// Fast key erasure: each block's first 32 bytes replace the key and only the
// second 32 leave.
struct ChaCha {
    void seed(const u8 key[CHACHA_KEY]);

    bool seeded() const { return seeded_; }

    // Fills `n` bytes. The last block's unused tail is dropped, not kept.
    void fill(u8 *out, usize n);

private:
    u8 key_[CHACHA_KEY] = {};
    u32 counter_        = 0;
    bool seeded_        = false;
};
