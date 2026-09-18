// Decompression, after decompress.c and the second half of bzlib.c. The
// state machine is theirs, state for state, so that a stream stopped at any
// bit resumes where it stopped.
#include "bzip2/bzlib_private.h"
#include "kernel/alloc.h"

namespace {

using S = BzDecodeState;

// ------------------------------------------------ randomised blocks

void rand_init(S *s)
{
    s->r_n_to_go = 0;
    s->r_t_pos   = 0;
}

void rand_update(S *s)
{
    if (s->r_n_to_go == 0) {
        s->r_n_to_go = BZ_RAND_NUMS[s->r_t_pos];
        s->r_t_pos++;
        if (s->r_t_pos == 512)
            s->r_t_pos = 0;
    }
    s->r_n_to_go--;
}

u8 rand_mask(const S *s)
{
    return s->r_n_to_go == 1 ? 1 : 0;
}

// ------------------------------------------------ small mode's T vector

inline void set_ll4(S *s, i32 i, u32 n)
{
    if ((i & 1) == 0)
        s->ll4[i >> 1] = u8((s->ll4[i >> 1] & 0xf0) | n);
    else
        s->ll4[i >> 1] = u8((s->ll4[i >> 1] & 0x0f) | (n << 4));
}

inline u32 get_ll4(const S *s, u32 i)
{
    return (u32(s->ll4[i >> 1]) >> ((i << 2) & 0x4)) & 0xf;
}

inline void set_ll(S *s, i32 i, u32 n)
{
    s->ll16[i] = u16(n & 0xffff);
    set_ll4(s, i, n >> 16);
}

inline u32 get_ll(const S *s, u32 i)
{
    return u32(s->ll16[i]) | (get_ll4(s, i) << 16);
}

// The byte whose cumulative count brackets indx.
inline i32 index_into_f(i32 indx, const i32 *cftab)
{
    i32 nb = 0, na = 256;
    do {
        i32 mid = (nb + na) >> 1;
        if (indx >= cftab[mid])
            nb = mid;
        else
            na = mid;
    } while (na - nb != 1);
    return nb;
}

// The next byte of the block in each mode, or false past its end.
inline bool get_fast(S *s, u8 &c)
{
    if (s->t_pos >= u32(100000) * u32(s->block_size_100k))
        return false;
    s->t_pos = s->tt[s->t_pos];
    c        = u8(s->t_pos & 0xff);
    s->t_pos >>= 8;
    return true;
}

inline bool get_small(S *s, u8 &c)
{
    if (s->t_pos >= u32(100000) * u32(s->block_size_100k))
        return false;
    c        = u8(index_into_f(i32(s->t_pos), s->cftab));
    s->t_pos = get_ll(s, s->t_pos);
    return true;
}

// ------------------------------------------------------------ decoding

void make_maps_d(S *s)
{
    s->n_in_use = 0;
    for (i32 i = 0; i < 256; i++)
        if (s->in_use[i]) {
            s->seq_to_unseq[s->n_in_use] = u8(i);
            s->n_in_use++;
        }
}

#define RETURN(rrr)                 \
    {                               \
        ret_val = rrr;              \
        goto save_state_and_return; \
    }

#define FAIL(msg)           \
    {                       \
        s->why = msg;       \
        RETURN(BZ_RC_DATA); \
    }

#define GET_BITS(lll, vvv, nnn)                                                 \
    case S::lll:                                                                \
        s->state = S::lll;                                                      \
        for (;;) {                                                              \
            if (s->bs_live >= nnn) {                                            \
                u32 v = (s->bs_buff >> (s->bs_live - nnn)) & ((1U << nnn) - 1); \
                s->bs_live -= nnn;                                              \
                vvv = v;                                                        \
                break;                                                          \
            }                                                                   \
            if (s->avail_in == 0)                                               \
                RETURN(BZ_RC_OK);                                               \
            s->bs_buff = (s->bs_buff << 8) | u32(*s->next_in);                  \
            s->bs_live += 8;                                                    \
            s->next_in++;                                                       \
            s->avail_in--;                                                      \
            s->total_in++;                                                      \
        }

#define GET_UCHAR(lll, uuu) GET_BITS(lll, uuu, 8)
#define GET_BIT(lll, uuu)   GET_BITS(lll, uuu, 1)

#define GET_MTF_VAL(label1, label2, lval)                                    \
    {                                                                        \
        if (group_pos == 0) {                                                \
            group_no++;                                                      \
            if (group_no >= n_selectors)                                     \
                FAIL("invalid selector");                                    \
            group_pos = BZ_G_SIZE;                                           \
            g_sel     = s->selector[group_no];                               \
            g_minlen  = s->min_lens[g_sel];                                  \
            g_limit   = &s->limit[g_sel][0];                                 \
            g_perm    = &s->perm[g_sel][0];                                  \
            g_base    = &s->base[g_sel][0];                                  \
        }                                                                    \
        group_pos--;                                                         \
        zn = g_minlen;                                                       \
        GET_BITS(label1, zvec, zn);                                          \
        for (;;) {                                                           \
            if (zn > 20)                                                     \
                FAIL("invalid code");                                        \
            if (zvec <= g_limit[zn])                                         \
                break;                                                       \
            zn++;                                                            \
            GET_BIT(label2, zj);                                             \
            zvec = (zvec << 1) | zj;                                         \
        }                                                                    \
        if (zvec - g_base[zn] < 0 || zvec - g_base[zn] >= BZ_MAX_ALPHA_SIZE) \
            FAIL("invalid code");                                            \
        lval = g_perm[zvec - g_base[zn]];                                    \
    }

// After BZ2_decompress: headers, tables and one block's symbols, up to the
// point where the block can be put out. BZ_RC_OK when it wants more input
// or has a block ready, s->state saying which.
int decompress_block(S *s)
{
    u8 uc;
    int ret_val;

    if (s->state == S::MAGIC_1) {
        s->save_i           = 0;
        s->save_j           = 0;
        s->save_t           = 0;
        s->save_alpha_size  = 0;
        s->save_n_groups    = 0;
        s->save_n_selectors = 0;
        s->save_eob         = 0;
        s->save_group_no    = 0;
        s->save_group_pos   = 0;
        s->save_next_sym    = 0;
        s->save_nblock_max  = 0;
        s->save_nblock      = 0;
        s->save_es          = 0;
        s->save_n           = 0;
        s->save_curr        = 0;
        s->save_zt          = 0;
        s->save_zn          = 0;
        s->save_zvec        = 0;
        s->save_zj          = 0;
        s->save_g_sel       = 0;
        s->save_g_minlen    = 0;
        s->save_g_limit     = nullptr;
        s->save_g_base      = nullptr;
        s->save_g_perm      = nullptr;
    }

    i32 i           = s->save_i;
    i32 j           = s->save_j;
    i32 t           = s->save_t;
    i32 alpha_size  = s->save_alpha_size;
    i32 n_groups    = s->save_n_groups;
    i32 n_selectors = s->save_n_selectors;
    i32 eob         = s->save_eob;
    i32 group_no    = s->save_group_no;
    i32 group_pos   = s->save_group_pos;
    i32 next_sym    = s->save_next_sym;
    i32 nblock_max  = s->save_nblock_max;
    i32 nblock      = s->save_nblock;
    i32 es          = s->save_es;
    i32 n_run       = s->save_n;
    i32 curr        = s->save_curr;
    i32 zt          = s->save_zt;
    i32 zn          = s->save_zn;
    i32 zvec        = s->save_zvec;
    i32 zj          = s->save_zj;
    i32 g_sel       = s->save_g_sel;
    i32 g_minlen    = s->save_g_minlen;
    i32 *g_limit    = s->save_g_limit;
    i32 *g_base     = s->save_g_base;
    i32 *g_perm     = s->save_g_perm;

    ret_val = BZ_RC_OK;

    switch (s->state) {
        GET_UCHAR(MAGIC_1, uc);
        if (uc != BZ_HDR_B)
            RETURN(BZ_RC_MAGIC);

        GET_UCHAR(MAGIC_2, uc);
        if (uc != BZ_HDR_Z)
            RETURN(BZ_RC_MAGIC);

        GET_UCHAR(MAGIC_3, uc);
        if (uc != BZ_HDR_H)
            RETURN(BZ_RC_MAGIC);

        GET_BITS(MAGIC_4, s->block_size_100k, 8);
        if (s->block_size_100k < (BZ_HDR_0 + 1) || s->block_size_100k > (BZ_HDR_0 + 9))
            RETURN(BZ_RC_MAGIC);
        s->block_size_100k -= BZ_HDR_0;

        if (s->small_decompress) {
            usize n = usize(s->block_size_100k) * 100000;
            s->ll16 = static_cast<u16 *>(heap_alloc(n * sizeof(u16)));
            s->ll4  = static_cast<u8 *>(heap_alloc((1 + n) >> 1));
            if (s->ll16 == nullptr || s->ll4 == nullptr)
                RETURN(BZ_RC_MEM);
        } else {
            s->tt =
                static_cast<u32 *>(heap_alloc(usize(s->block_size_100k) * 100000 * sizeof(u32)));
            if (s->tt == nullptr)
                RETURN(BZ_RC_MEM);
        }

        GET_UCHAR(BLKHDR_1, uc);

        if (uc == 0x17)
            goto endhdr_2;
        if (uc != 0x31)
            FAIL("incorrect block header");
        GET_UCHAR(BLKHDR_2, uc);
        if (uc != 0x41)
            FAIL("incorrect block header");
        GET_UCHAR(BLKHDR_3, uc);
        if (uc != 0x59)
            FAIL("incorrect block header");
        GET_UCHAR(BLKHDR_4, uc);
        if (uc != 0x26)
            FAIL("incorrect block header");
        GET_UCHAR(BLKHDR_5, uc);
        if (uc != 0x53)
            FAIL("incorrect block header");
        GET_UCHAR(BLKHDR_6, uc);
        if (uc != 0x59)
            FAIL("incorrect block header");

        s->curr_block_no++;

        s->stored_block_crc = 0;
        GET_UCHAR(BCRC_1, uc);
        s->stored_block_crc = (s->stored_block_crc << 8) | u32(uc);
        GET_UCHAR(BCRC_2, uc);
        s->stored_block_crc = (s->stored_block_crc << 8) | u32(uc);
        GET_UCHAR(BCRC_3, uc);
        s->stored_block_crc = (s->stored_block_crc << 8) | u32(uc);
        GET_UCHAR(BCRC_4, uc);
        s->stored_block_crc = (s->stored_block_crc << 8) | u32(uc);

        GET_BITS(RANDBIT, s->block_randomised, 1);

        s->orig_ptr = 0;
        GET_UCHAR(ORIGPTR_1, uc);
        s->orig_ptr = (s->orig_ptr << 8) | i32(uc);
        GET_UCHAR(ORIGPTR_2, uc);
        s->orig_ptr = (s->orig_ptr << 8) | i32(uc);
        GET_UCHAR(ORIGPTR_3, uc);
        s->orig_ptr = (s->orig_ptr << 8) | i32(uc);

        if (s->orig_ptr < 0)
            FAIL("invalid origin pointer");
        if (s->orig_ptr > 10 + 100000 * s->block_size_100k)
            FAIL("invalid origin pointer");

        // The mapping table.
        for (i = 0; i < 16; i++) {
            GET_BIT(MAPPING_1, uc);
            s->in_use16[i] = uc == 1;
        }

        for (i = 0; i < 256; i++)
            s->in_use[i] = false;

        for (i = 0; i < 16; i++)
            if (s->in_use16[i])
                for (j = 0; j < 16; j++) {
                    GET_BIT(MAPPING_2, uc);
                    if (uc == 1)
                        s->in_use[i * 16 + j] = true;
                }
        make_maps_d(s);
        if (s->n_in_use == 0)
            FAIL("invalid symbol map");
        alpha_size = s->n_in_use + 2;

        // The selectors.
        GET_BITS(SELECTOR_1, n_groups, 3);
        if (n_groups < 2 || n_groups > BZ_N_GROUPS)
            FAIL("invalid number of tables");
        GET_BITS(SELECTOR_2, n_selectors, 15);
        if (n_selectors < 1)
            FAIL("invalid number of selectors");
        for (i = 0; i < n_selectors; i++) {
            j = 0;
            for (;;) {
                GET_BIT(SELECTOR_3, uc);
                if (uc == 0)
                    break;
                j++;
                if (j >= n_groups)
                    FAIL("invalid selector");
            }
            // Selectors past the most a block can use are read and dropped.
            if (i < BZ_MAX_SELECTORS)
                s->selector_mtf[i] = u8(j);
        }
        if (n_selectors > BZ_MAX_SELECTORS)
            n_selectors = BZ_MAX_SELECTORS;

        // Undo their move-to-front coding.
        {
            u8 pos[BZ_N_GROUPS], tmp, v;
            for (v = 0; v < n_groups; v++)
                pos[v] = v;

            for (i = 0; i < n_selectors; i++) {
                v   = s->selector_mtf[i];
                tmp = pos[v];
                while (v > 0) {
                    pos[v] = pos[v - 1];
                    v--;
                }
                pos[0]         = tmp;
                s->selector[i] = tmp;
            }
        }

        // The coding tables.
        for (t = 0; t < n_groups; t++) {
            GET_BITS(CODING_1, curr, 5);
            for (i = 0; i < alpha_size; i++) {
                for (;;) {
                    if (curr < 1 || curr > 20)
                        FAIL("invalid code lengths");
                    GET_BIT(CODING_2, uc);
                    if (uc == 0)
                        break;
                    GET_BIT(CODING_3, uc);
                    if (uc == 0)
                        curr++;
                    else
                        curr--;
                }
                s->len[t][i] = u8(curr);
            }
        }

        // The Huffman decoding tables.
        for (t = 0; t < n_groups; t++) {
            i32 min_len = 32;
            i32 max_len = 0;
            for (i = 0; i < alpha_size; i++) {
                if (s->len[t][i] > max_len)
                    max_len = s->len[t][i];
                if (s->len[t][i] < min_len)
                    min_len = s->len[t][i];
            }
            bz_hb_create_decode_tables(&s->limit[t][0], &s->base[t][0], &s->perm[t][0],
                                       &s->len[t][0], min_len, max_len, alpha_size);
            s->min_lens[t] = min_len;
        }

        // The MTF values.
        eob        = s->n_in_use + 1;
        nblock_max = 100000 * s->block_size_100k;
        group_no   = -1;
        group_pos  = 0;

        for (i = 0; i <= 255; i++)
            s->unzftab[i] = 0;

        {
            i32 kk = BZ_MTFA_SIZE - 1;
            for (i32 ii = 256 / BZ_MTFL_SIZE - 1; ii >= 0; ii--) {
                for (i32 jj = BZ_MTFL_SIZE - 1; jj >= 0; jj--) {
                    s->mtfa[kk] = u8(ii * BZ_MTFL_SIZE + jj);
                    kk--;
                }
                s->mtfbase[ii] = kk + 1;
            }
        }

        nblock = 0;
        GET_MTF_VAL(MTF_1, MTF_2, next_sym);

        for (;;) {
            if (next_sym == eob)
                break;

            if (next_sym == BZ_RUNA || next_sym == BZ_RUNB) {
                es    = -1;
                n_run = 1;
                do {
                    // A run longer than any block.
                    if (n_run >= 2 * 1024 * 1024)
                        FAIL("invalid run length");
                    if (next_sym == BZ_RUNA)
                        es = es + (0 + 1) * n_run;
                    else if (next_sym == BZ_RUNB)
                        es = es + (1 + 1) * n_run;
                    n_run = n_run * 2;
                    GET_MTF_VAL(MTF_3, MTF_4, next_sym);
                } while (next_sym == BZ_RUNA || next_sym == BZ_RUNB);

                es++;
                uc = s->seq_to_unseq[s->mtfa[s->mtfbase[0]]];
                s->unzftab[uc] += es;

                if (s->small_decompress)
                    while (es > 0) {
                        if (nblock >= nblock_max)
                            FAIL("block too long");
                        s->ll16[nblock] = u16(uc);
                        nblock++;
                        es--;
                    }
                else
                    while (es > 0) {
                        if (nblock >= nblock_max)
                            FAIL("block too long");
                        s->tt[nblock] = u32(uc);
                        nblock++;
                        es--;
                    }

                continue;

            } else {
                if (nblock >= nblock_max)
                    FAIL("block too long");

                // uc = MTF(next_sym - 1)
                {
                    u32 nn = u32(next_sym - 1);

                    if (nn < u32(BZ_MTFL_SIZE)) {
                        i32 pp = s->mtfbase[0];
                        uc     = s->mtfa[pp + i32(nn)];
                        while (nn > 3) {
                            i32 z          = pp + i32(nn);
                            s->mtfa[z]     = s->mtfa[z - 1];
                            s->mtfa[z - 1] = s->mtfa[z - 2];
                            s->mtfa[z - 2] = s->mtfa[z - 3];
                            s->mtfa[z - 3] = s->mtfa[z - 4];
                            nn -= 4;
                        }
                        while (nn > 0) {
                            s->mtfa[pp + i32(nn)] = s->mtfa[pp + i32(nn) - 1];
                            nn--;
                        }
                        s->mtfa[pp] = uc;
                    } else {
                        i32 lno = i32(nn / BZ_MTFL_SIZE);
                        i32 off = i32(nn % BZ_MTFL_SIZE);
                        i32 pp  = s->mtfbase[lno] + off;
                        uc      = s->mtfa[pp];
                        while (pp > s->mtfbase[lno]) {
                            s->mtfa[pp] = s->mtfa[pp - 1];
                            pp--;
                        }
                        s->mtfbase[lno]++;
                        while (lno > 0) {
                            s->mtfbase[lno]--;
                            s->mtfa[s->mtfbase[lno]] =
                                s->mtfa[s->mtfbase[lno - 1] + BZ_MTFL_SIZE - 1];
                            lno--;
                        }
                        s->mtfbase[0]--;
                        s->mtfa[s->mtfbase[0]] = uc;
                        if (s->mtfbase[0] == 0) {
                            i32 kk = BZ_MTFA_SIZE - 1;
                            for (i32 ii = 256 / BZ_MTFL_SIZE - 1; ii >= 0; ii--) {
                                for (i32 jj = BZ_MTFL_SIZE - 1; jj >= 0; jj--) {
                                    s->mtfa[kk] = s->mtfa[s->mtfbase[ii] + jj];
                                    kk--;
                                }
                                s->mtfbase[ii] = kk + 1;
                            }
                        }
                    }
                }

                s->unzftab[s->seq_to_unseq[uc]]++;
                if (s->small_decompress)
                    s->ll16[nblock] = u16(s->seq_to_unseq[uc]);
                else
                    s->tt[nblock] = u32(s->seq_to_unseq[uc]);
                nblock++;

                GET_MTF_VAL(MTF_5, MTF_6, next_sym);
                continue;
            }
        }

        // Now that nblock is known, a closer check of the origin.
        if (s->orig_ptr < 0 || s->orig_ptr >= nblock)
            FAIL("invalid origin pointer");

        // cftab, for T^-1, and its bounds.
        for (i = 0; i <= 255; i++)
            if (s->unzftab[i] < 0 || s->unzftab[i] > nblock)
                FAIL("invalid symbol counts");
        s->cftab[0] = 0;
        for (i = 1; i <= 256; i++)
            s->cftab[i] = s->unzftab[i - 1];
        for (i = 1; i <= 256; i++)
            s->cftab[i] += s->cftab[i - 1];
        for (i = 0; i <= 256; i++)
            if (s->cftab[i] < 0 || s->cftab[i] > nblock)
                FAIL("invalid symbol counts");
        for (i = 1; i <= 256; i++)
            if (s->cftab[i - 1] > s->cftab[i])
                FAIL("invalid symbol counts");

        s->state_out_len        = 0;
        s->state_out_ch         = 0;
        s->calculated_block_crc = 0xffffffff;
        s->state                = S::OUTPUT;

        if (s->small_decompress) {
            for (i = 0; i <= 256; i++)
                s->cftab_copy[i] = s->cftab[i];

            // T.
            for (i = 0; i < nblock; i++) {
                uc = u8(s->ll16[i]);
                set_ll(s, i, u32(s->cftab_copy[uc]));
                s->cftab_copy[uc]++;
            }

            // T^-1, by reversing T's pointers in place.
            i = s->orig_ptr;
            j = i32(get_ll(s, u32(i)));
            do {
                i32 tmp = i32(get_ll(s, u32(j)));
                set_ll(s, j, u32(i));
                i = j;
                j = tmp;
            } while (i != s->orig_ptr);

            s->t_pos       = u32(s->orig_ptr);
            s->nblock_used = 0;
            if (!get_small(s, uc))
                FAIL("invalid block data");
            s->k0 = uc;
            s->nblock_used++;
            if (s->block_randomised) {
                rand_init(s);
                rand_update(s);
                s->k0 ^= rand_mask(s);
            }
        } else {
            // T^-1.
            for (i = 0; i < nblock; i++) {
                uc = u8(s->tt[i] & 0xff);
                s->tt[s->cftab[uc]] |= u32(i << 8);
                s->cftab[uc]++;
            }

            s->t_pos       = s->tt[s->orig_ptr] >> 8;
            s->nblock_used = 0;
            if (!get_fast(s, uc))
                FAIL("invalid block data");
            s->k0 = uc;
            s->nblock_used++;
            if (s->block_randomised) {
                rand_init(s);
                rand_update(s);
                s->k0 ^= rand_mask(s);
            }
        }

        RETURN(BZ_RC_OK);

    endhdr_2:

        GET_UCHAR(ENDHDR_2, uc);
        if (uc != 0x72)
            FAIL("incorrect stream trailer");
        GET_UCHAR(ENDHDR_3, uc);
        if (uc != 0x45)
            FAIL("incorrect stream trailer");
        GET_UCHAR(ENDHDR_4, uc);
        if (uc != 0x38)
            FAIL("incorrect stream trailer");
        GET_UCHAR(ENDHDR_5, uc);
        if (uc != 0x50)
            FAIL("incorrect stream trailer");
        GET_UCHAR(ENDHDR_6, uc);
        if (uc != 0x90)
            FAIL("incorrect stream trailer");

        s->stored_combined_crc = 0;
        GET_UCHAR(CCRC_1, uc);
        s->stored_combined_crc = (s->stored_combined_crc << 8) | u32(uc);
        GET_UCHAR(CCRC_2, uc);
        s->stored_combined_crc = (s->stored_combined_crc << 8) | u32(uc);
        GET_UCHAR(CCRC_3, uc);
        s->stored_combined_crc = (s->stored_combined_crc << 8) | u32(uc);
        GET_UCHAR(CCRC_4, uc);
        s->stored_combined_crc = (s->stored_combined_crc << 8) | u32(uc);

        s->state = S::IDLE;
        RETURN(BZ_RC_STREAM_END);

    default:
        RETURN(BZ_RC_BUG);
    }

save_state_and_return:

    s->save_i           = i;
    s->save_j           = j;
    s->save_t           = t;
    s->save_alpha_size  = alpha_size;
    s->save_n_groups    = n_groups;
    s->save_n_selectors = n_selectors;
    s->save_eob         = eob;
    s->save_group_no    = group_no;
    s->save_group_pos   = group_pos;
    s->save_next_sym    = next_sym;
    s->save_nblock_max  = nblock_max;
    s->save_nblock      = nblock;
    s->save_es          = es;
    s->save_n           = n_run;
    s->save_curr        = curr;
    s->save_zt          = zt;
    s->save_zn          = zn;
    s->save_zvec        = zvec;
    s->save_zj          = zj;
    s->save_g_sel       = g_sel;
    s->save_g_minlen    = g_minlen;
    s->save_g_limit     = g_limit;
    s->save_g_base      = g_base;
    s->save_g_perm      = g_perm;

    return ret_val;
}

#undef RETURN
#undef FAIL
#undef GET_BITS
#undef GET_UCHAR
#undef GET_BIT
#undef GET_MTF_VAL

// -------------------------------------------------------------- output

// One output byte, counted into the block's CRC.
inline void put(S *s, u8 c)
{
    *s->next_out = c;
    bz_crc_byte(s->calculated_block_crc, c);
    s->next_out++;
    s->avail_out--;
    s->total_out++;
}

// The block's run-length coding undone into the output, one byte at a
// time through get(), as far as the room goes. True when the block turns
// out to be corrupt.
template <bool (*get)(S *, u8 &)>
bool unrle_to_output(S *s)
{
    u8 k1;
    bool randomised = s->block_randomised;

    // The next byte, the randomisation undone.
    auto next = [&](u8 &c) {
        if (!get(s, c))
            return false;
        if (randomised) {
            rand_update(s);
            c ^= rand_mask(s);
        }
        s->nblock_used++;
        return true;
    };

    for (;;) {
        // Finish the run under way.
        for (;;) {
            if (s->avail_out == 0)
                return false;
            if (s->state_out_len == 0)
                break;
            put(s, s->state_out_ch);
            s->state_out_len--;
        }

        // Can another start?
        if (s->nblock_used == s->save_nblock + 1)
            return false;

        // Only a corrupt stream gets here.
        if (s->nblock_used > s->save_nblock + 1)
            return true;

        s->state_out_len = 1;
        s->state_out_ch  = u8(s->k0);
        if (!next(k1))
            return true;
        if (s->nblock_used == s->save_nblock + 1)
            continue;
        if (k1 != s->k0) {
            s->k0 = k1;
            continue;
        }

        s->state_out_len = 2;
        if (!next(k1))
            return true;
        if (s->nblock_used == s->save_nblock + 1)
            continue;
        if (k1 != s->k0) {
            s->k0 = k1;
            continue;
        }

        s->state_out_len = 3;
        if (!next(k1))
            return true;
        if (s->nblock_used == s->save_nblock + 1)
            continue;
        if (k1 != s->k0) {
            s->k0 = k1;
            continue;
        }

        if (!next(k1))
            return true;
        s->state_out_len = i32(k1) + 4;
        if (!next(k1))
            return true;
        s->k0 = k1;
    }
}

// After BZ2_bzDecompress, with its return codes.
int decompress_run(S *s)
{
    for (;;) {
        if (s->state == S::IDLE)
            return BZ_RC_SEQUENCE;
        if (s->state == S::OUTPUT) {
            bool corrupt =
                s->small_decompress ? unrle_to_output<get_small>(s) : unrle_to_output<get_fast>(s);
            if (corrupt) {
                s->why = "invalid block data";
                return BZ_RC_DATA;
            }
            if (s->nblock_used == s->save_nblock + 1 && s->state_out_len == 0) {
                s->calculated_block_crc = ~s->calculated_block_crc;
                if (s->calculated_block_crc != s->stored_block_crc) {
                    s->why = "incorrect block check";
                    return BZ_RC_DATA;
                }
                s->calculated_combined_crc =
                    (s->calculated_combined_crc << 1) | (s->calculated_combined_crc >> 31);
                s->calculated_combined_crc ^= s->calculated_block_crc;
                s->state = S::BLKHDR_1;
            } else {
                return BZ_RC_OK;
            }
        }
        if (s->state >= S::MAGIC_1) {
            int r = decompress_block(s);
            if (r == BZ_RC_STREAM_END) {
                if (s->calculated_combined_crc != s->stored_combined_crc) {
                    s->why = "incorrect stream check";
                    return BZ_RC_DATA;
                }
                return r;
            }
            if (s->state != S::OUTPUT)
                return r;
        }
    }
}

void free_state(S *s)
{
    if (!s)
        return;
    heap_free(s->tt);
    heap_free(s->ll16);
    heap_free(s->ll4);
    heap_free(s);
}

BzStatus status_of(int rc, bool progress)
{
    switch (rc) {
    case BZ_RC_OK:
        return progress ? BzStatus::Ok : BzStatus::Stuck;
    case BZ_RC_STREAM_END:
        return BzStatus::End;
    case BZ_RC_DATA:
        return BzStatus::Corrupt;
    case BZ_RC_MAGIC:
        return BzStatus::NotBzip2;
    case BZ_RC_MEM:
        return BzStatus::NoMemory;
    }
    return BzStatus::Misuse;
}

} // namespace

BzDecompressor &BzDecompressor::operator=(BzDecompressor &&o) noexcept
{
    if (this != &o) {
        free_state(s_);
        s_   = o.s_;
        o.s_ = nullptr;
    }
    return *this;
}

BzDecompressor::~BzDecompressor()
{
    free_state(s_);
}

Result<void> BzDecompressor::init(bool small)
{
    free_state(s_);
    s_ = static_cast<S *>(heap_alloc(sizeof(S)));
    if (s_ == nullptr)
        return Err(Error::NoMemory);
    __builtin_memset(static_cast<void *>(s_), 0, sizeof(S));
    s_->state            = S::MAGIC_1;
    s_->small_decompress = small;
    return {};
}

BzStatus BzDecompressor::step(Span<const u8> &in, Span<u8> &out)
{
    if (!s_)
        return BzStatus::Misuse;
    if (s_->failed)
        return status_of(s_->failed, false);
    s_->next_in   = in.data();
    s_->avail_in  = in.size();
    s_->next_out  = out.data();
    s_->avail_out = out.size();
    int rc        = decompress_run(s_);
    bool progress = s_->avail_in != in.size() || s_->avail_out != out.size();
    in            = Span<const u8>(s_->next_in, s_->avail_in);
    out           = Span<u8>(s_->next_out, s_->avail_out);
    if (rc < 0 && rc != BZ_RC_SEQUENCE)
        s_->failed = rc;
    if (rc == BZ_RC_MAGIC)
        s_->why = "not a bzip2 stream";
    return status_of(rc, progress);
}

u64 BzDecompressor::total_in() const
{
    return s_ ? s_->total_in : 0;
}

u64 BzDecompressor::total_out() const
{
    return s_ ? s_->total_out : 0;
}

Str BzDecompressor::why() const
{
    if (!s_ || !s_->why)
        return Str();
    usize n = 0;
    while (s_->why[n])
        n++;
    return Str(s_->why, n);
}
