// Huffman code lengths, codes and decoding tables, after huffman.c.
#include "bzip2/bzlib_private.h"

namespace {

constexpr i32 weight_of(i32 w)
{
    return w & i32(0xffffff00);
}

constexpr i32 depth_of(i32 w)
{
    return w & 0xff;
}

constexpr i32 add_weights(i32 a, i32 b)
{
    i32 da = depth_of(a), db = depth_of(b);
    return (weight_of(a) + weight_of(b)) | (1 + (da > db ? da : db));
}

void upheap(i32 *heap, const i32 *weight, i32 z)
{
    i32 zz = z, tmp = heap[zz];
    while (weight[tmp] < weight[heap[zz >> 1]]) {
        heap[zz] = heap[zz >> 1];
        zz >>= 1;
    }
    heap[zz] = tmp;
}

void downheap(i32 *heap, const i32 *weight, i32 n_heap, i32 z)
{
    i32 zz = z, tmp = heap[zz];
    for (;;) {
        i32 yy = zz << 1;
        if (yy > n_heap)
            break;
        if (yy < n_heap && weight[heap[yy + 1]] < weight[heap[yy]])
            yy++;
        if (weight[tmp] < weight[heap[yy]])
            break;
        heap[zz] = heap[yy];
        zz       = yy;
    }
    heap[zz] = tmp;
}

} // namespace

bool bz_hb_make_code_lengths(u8 *len, const i32 *freq, i32 alpha_size, i32 max_len)
{
    // Nodes and heap entries run from 1; entry 0 of each is a sentinel.
    i32 heap[BZ_MAX_ALPHA_SIZE + 2];
    i32 weight[BZ_MAX_ALPHA_SIZE * 2];
    i32 parent[BZ_MAX_ALPHA_SIZE * 2];

    for (i32 i = 0; i < alpha_size; i++)
        weight[i + 1] = (freq[i] == 0 ? 1 : freq[i]) << 8;

    for (;;) {
        i32 n_nodes = alpha_size;
        i32 n_heap  = 0;

        heap[0]   = 0;
        weight[0] = 0;
        parent[0] = -2;

        for (i32 i = 1; i <= alpha_size; i++) {
            parent[i] = -1;
            n_heap++;
            heap[n_heap] = i;
            upheap(heap, weight, n_heap);
        }

        if (n_heap >= BZ_MAX_ALPHA_SIZE + 2)
            return false;

        while (n_heap > 1) {
            i32 n1  = heap[1];
            heap[1] = heap[n_heap];
            n_heap--;
            downheap(heap, weight, n_heap, 1);
            i32 n2  = heap[1];
            heap[1] = heap[n_heap];
            n_heap--;
            downheap(heap, weight, n_heap, 1);
            n_nodes++;
            parent[n1] = parent[n2] = n_nodes;
            weight[n_nodes]         = add_weights(weight[n1], weight[n2]);
            parent[n_nodes]         = -1;
            n_heap++;
            heap[n_heap] = n_nodes;
            upheap(heap, weight, n_heap);
        }

        if (n_nodes >= BZ_MAX_ALPHA_SIZE * 2)
            return false;

        bool too_long = false;
        for (i32 i = 1; i <= alpha_size; i++) {
            i32 j = 0;
            i32 k = i;
            while (parent[k] >= 0) {
                k = parent[k];
                j++;
            }
            len[i - 1] = u8(j);
            if (j > max_len)
                too_long = true;
        }

        if (!too_long)
            return true;

        // A code past max_len: flatten the frequencies and build again.
        for (i32 i = 1; i <= alpha_size; i++) {
            i32 j     = weight[i] >> 8;
            j         = 1 + (j / 2);
            weight[i] = j << 8;
        }
    }
}

void bz_hb_assign_codes(i32 *code, const u8 *length, i32 min_len, i32 max_len, i32 alpha_size)
{
    i32 vec = 0;
    for (i32 n = min_len; n <= max_len; n++) {
        for (i32 i = 0; i < alpha_size; i++)
            if (length[i] == n)
                code[i] = vec++;
        vec <<= 1;
    }
}

void bz_hb_create_decode_tables(i32 *limit, i32 *base, i32 *perm, const u8 *length, i32 min_len,
                                i32 max_len, i32 alpha_size)
{
    i32 pp = 0;
    for (i32 i = min_len; i <= max_len; i++)
        for (i32 j = 0; j < alpha_size; j++)
            if (length[j] == i)
                perm[pp++] = j;

    for (i32 i = 0; i < BZ_MAX_CODE_LEN; i++)
        base[i] = 0;
    for (i32 i = 0; i < alpha_size; i++)
        base[length[i] + 1]++;

    for (i32 i = 1; i < BZ_MAX_CODE_LEN; i++)
        base[i] += base[i - 1];

    for (i32 i = 0; i < BZ_MAX_CODE_LEN; i++)
        limit[i] = 0;
    i32 vec = 0;

    for (i32 i = min_len; i <= max_len; i++) {
        vec += (base[i + 1] - base[i]);
        limit[i] = vec - 1;
        vec <<= 1;
    }
    for (i32 i = min_len + 1; i <= max_len; i++)
        base[i] = ((limit[i - 1] + 1) << 1) - base[i];
}
