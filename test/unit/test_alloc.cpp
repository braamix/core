#include "harness.h"
#include "kernel/alloc.h"

void test_alloc()
{
    test_begin("alloc");

    // Size classes round up as documented, a middle request to 16 bytes, and
    // large requests take whole spans.
    CHECK_EQ(heap_block_size(1), 16);
    CHECK_EQ(heap_block_size(16), 16);
    CHECK_EQ(heap_block_size(17), 32);
    CHECK_EQ(heap_block_size(33), 64);
    CHECK_EQ(heap_block_size(100), 128);
    CHECK_EQ(heap_block_size(200), 256);
    CHECK_EQ(heap_block_size(385), 512);
    CHECK_EQ(heap_block_size(512), 512);
    CHECK_EQ(heap_block_size(513), 528);
    CHECK_EQ(heap_block_size(1000), 1008);
    CHECK_EQ(heap_block_size(32768), 32768);
    CHECK_EQ(heap_block_size(32769), 65536);
    CHECK_EQ(heap_block_size(65537), 131072);

    // Middle blocks share a span: a thousand of a kilobyte are sixteen spans,
    // not a thousand, and they give all but one back when freed.
    usize held = heap_stats().bytes_reserved;
    void **kb  = static_cast<void **>(heap_alloc(1000 * sizeof(void *)));
    CHECK(kb != nullptr);
    for (usize i = 0; i < 1000; i++) {
        kb[i] = heap_alloc(1000);
        CHECK(kb[i] != nullptr);
        CHECK_EQ(reinterpret_cast<usize>(kb[i]) & 15u, 0);
        CHECK(heap_usable_size(kb[i]) >= 1000);
        for (usize j = 0; j < 1000; j++)
            static_cast<u8 *>(kb[i])[j] = u8(i);
    }
    CHECK(heap_stats().bytes_reserved - held <= 20 * 65536);
    bool whole = true;
    for (usize i = 0; i < 1000; i += 2) // evens first, so odds merge both ways
        for (usize j = 0; j < 1000; j++)
            if (static_cast<u8 *>(kb[i])[j] != u8(i))
                whole = false;
    CHECK(whole);
    for (usize i = 0; i < 1000; i += 2)
        heap_free(kb[i]);
    for (usize i = 1; i < 1000; i += 2)
        heap_free(kb[i]);
    heap_free(kb);
    usize grown = heap_stats().bytes_reserved;
    void *after = heap_alloc(12 * 65536); // fits only in the spans given back
    CHECK(after != nullptr);
    CHECK_EQ(heap_stats().bytes_reserved, grown);
    heap_free(after);

    // A freed middle block is reused by a smaller request, and the rest of it
    // stays free for another.
    u8 *m1 = static_cast<u8 *>(heap_alloc(4000));
    u8 *m2 = static_cast<u8 *>(heap_alloc(4000));
    heap_free(m1);
    u8 *m3 = static_cast<u8 *>(heap_alloc(1000));
    u8 *m4 = static_cast<u8 *>(heap_alloc(1000));
    CHECK(m3 == m1);
    CHECK(m4 > m3 && m4 < m2);
    heap_free(m3);
    heap_free(m4);
    heap_free(m2);

    // Every block is 16-aligned, and distinct.
    void *p[64];
    for (usize i = 0; i < 64; i++) {
        p[i] = heap_alloc(i * 7 + 1);
        CHECK(p[i] != nullptr);
        CHECK_EQ(reinterpret_cast<usize>(p[i]) & 15u, 0);
    }
    for (usize i = 1; i < 64; i++)
        CHECK(p[i] != p[i - 1]);
    for (usize i = 0; i < 64; i++)
        heap_free(p[i]);

    // A freed block of the same class comes straight back.
    void *a = heap_alloc(100);
    heap_free(a);
    void *b = heap_alloc(100);
    CHECK(a == b);
    heap_free(b);

    // bytes_in_use returns to its starting point across an alloc/free cycle.
    usize before = heap_stats().bytes_in_use;
    void *big    = heap_alloc(200000);
    CHECK(big != nullptr);
    CHECK_EQ(reinterpret_cast<usize>(big) & 0xFFFFu, 0); // span-aligned
    CHECK_EQ(heap_stats().bytes_in_use, before + 4 * 65536);
    heap_free(big);
    CHECK_EQ(heap_stats().bytes_in_use, before);

    // Adjacent large runs coalesce, so the space is reusable as one block.
    void *r1 = heap_alloc(100000); // 2 spans
    void *r2 = heap_alloc(100000); // 2 spans
    CHECK(r1 != nullptr);
    CHECK(r2 != nullptr);
    heap_free(r1);
    heap_free(r2);
    void *r3 = heap_alloc(200000); // 4 spans, only fits if the runs merged
    CHECK(r3 == r1);
    heap_free(r3);

    // Writing to a large block does not disturb its neighbour.
    u8 *w1 = static_cast<u8 *>(heap_alloc(70000));
    u8 *w2 = static_cast<u8 *>(heap_alloc(70000));
    for (usize i = 0; i < 70000; i++)
        w1[i] = u8(i);
    for (usize i = 0; i < 70000; i++)
        w2[i] = u8(i ^ 0xFF);
    bool intact = true;
    for (usize i = 0; i < 70000; i++)
        if (w1[i] != u8(i))
            intact = false;
    CHECK(intact);
    heap_free(w1);
    heap_free(w2);

    // A null free is a no-op.
    heap_free(nullptr);
}
