// Compression, after compress.c and the first half of bzlib.c: the input
// run-length coded into a block, the block sorted, move-to-front coded and
// Huffman coded, a block at a time.
#include "bzip2/bzlib_private.h"
#include "kernel/alloc.h"

namespace {

using S = BzEncodeState;

// ---------------------------------------------------------- bit output

void bs_init_write(S *s)
{
    s->bs_live = 0;
    s->bs_buff = 0;
}

void bs_finish_write(S *s)
{
    while (s->bs_live > 0) {
        s->zbits[s->num_z] = u8(s->bs_buff >> 24);
        s->num_z++;
        s->bs_buff <<= 8;
        s->bs_live -= 8;
    }
}

inline void bs_w(S *s, i32 n, u32 v)
{
    while (s->bs_live >= 8) {
        s->zbits[s->num_z] = u8(s->bs_buff >> 24);
        s->num_z++;
        s->bs_buff <<= 8;
        s->bs_live -= 8;
    }
    s->bs_buff |= (v << (32 - s->bs_live - n));
    s->bs_live += n;
}

void bs_put_u32(S *s, u32 u)
{
    bs_w(s, 8, (u >> 24) & 0xff);
    bs_w(s, 8, (u >> 16) & 0xff);
    bs_w(s, 8, (u >> 8) & 0xff);
    bs_w(s, 8, u & 0xff);
}

void bs_put_u8(S *s, u8 c)
{
    bs_w(s, 8, c);
}

// ---------------------------------------------------------- the back end

void make_maps_e(S *s)
{
    s->n_in_use = 0;
    for (i32 i = 0; i < 256; i++)
        if (s->in_use[i]) {
            s->unseq_to_seq[i] = u8(s->n_in_use);
            s->n_in_use++;
        }
}

// arr1 holds the sorted order and arr2's bytes the block. The MTF values
// go into arr1 as u16s, behind the order they are made from.
void generate_mtf_values(S *s)
{
    u8 yy[256];
    u32 *ptr  = s->ptr;
    u8 *block = s->block;
    u16 *mtfv = s->mtfv;

    make_maps_e(s);
    i32 eob = s->n_in_use + 1;

    for (i32 i = 0; i <= eob; i++)
        s->mtf_freq[i] = 0;

    i32 wr     = 0;
    i32 z_pend = 0;
    for (i32 i = 0; i < s->n_in_use; i++)
        yy[i] = u8(i);

    // A run of zeroes as RUNA and RUNB digits, bijective base 2.
    auto flush_zeroes = [&] {
        z_pend--;
        for (;;) {
            if (z_pend & 1) {
                mtfv[wr++] = BZ_RUNB;
                s->mtf_freq[BZ_RUNB]++;
            } else {
                mtfv[wr++] = BZ_RUNA;
                s->mtf_freq[BZ_RUNA]++;
            }
            if (z_pend < 2)
                break;
            z_pend = (z_pend - 2) / 2;
        }
        z_pend = 0;
    };

    for (i32 i = 0; i < s->nblock; i++) {
        i32 j = i32(ptr[i]) - 1;
        if (j < 0)
            j += s->nblock;
        u8 ll_i = s->unseq_to_seq[block[j]];

        if (yy[0] == ll_i) {
            z_pend++;
        } else {
            if (z_pend > 0)
                flush_zeroes();
            u8 rtmp   = yy[1];
            yy[1]     = yy[0];
            u8 *ryy_j = &yy[1];
            while (ll_i != rtmp) {
                ryy_j++;
                u8 rtmp2 = rtmp;
                rtmp     = *ryy_j;
                *ryy_j   = rtmp2;
            }
            yy[0]      = rtmp;
            j          = i32(ryy_j - &yy[0]);
            mtfv[wr++] = u16(j + 1);
            s->mtf_freq[j + 1]++;
        }
    }

    if (z_pend > 0)
        flush_zeroes();

    mtfv[wr++] = u16(eob);
    s->mtf_freq[eob]++;

    s->n_mtf = wr;
}

constexpr u8 BZ_LESSER_ICOST  = 0;
constexpr u8 BZ_GREATER_ICOST = 15;

// False when an internal check fails.
bool send_mtf_values(S *s)
{
    u16 cost[BZ_N_GROUPS];
    i32 fave[BZ_N_GROUPS];
    u16 *mtfv = s->mtfv;
    i32 n_groups, n_selectors = 0;
    i32 gs, ge;

    i32 alpha_size = s->n_in_use + 2;
    for (i32 t = 0; t < BZ_N_GROUPS; t++)
        for (i32 v = 0; v < alpha_size; v++)
            s->len[t][v] = BZ_GREATER_ICOST;

    // How many coding tables.
    if (s->n_mtf <= 0)
        return false;
    if (s->n_mtf < 200)
        n_groups = 2;
    else if (s->n_mtf < 600)
        n_groups = 3;
    else if (s->n_mtf < 1200)
        n_groups = 4;
    else if (s->n_mtf < 2400)
        n_groups = 5;
    else
        n_groups = 6;

    // An initial set of tables, each over a range of symbols.
    {
        i32 n_part = n_groups;
        i32 rem_f  = s->n_mtf;
        gs         = 0;
        while (n_part > 0) {
            i32 t_freq = rem_f / n_part;
            ge         = gs - 1;
            i32 a_freq = 0;
            while (a_freq < t_freq && ge < alpha_size - 1) {
                ge++;
                a_freq += s->mtf_freq[ge];
            }

            if (ge > gs && n_part != n_groups && n_part != 1 && ((n_groups - n_part) % 2 == 1)) {
                a_freq -= s->mtf_freq[ge];
                ge--;
            }

            for (i32 v = 0; v < alpha_size; v++)
                s->len[n_part - 1][v] = v >= gs && v <= ge ? BZ_LESSER_ICOST : BZ_GREATER_ICOST;

            n_part--;
            gs = ge + 1;
            rem_f -= a_freq;
        }
    }

    // Improve the tables, BZ_N_ITERS times.
    for (i32 iter = 0; iter < BZ_N_ITERS; iter++) {
        for (i32 t = 0; t < n_groups; t++)
            fave[t] = 0;

        for (i32 t = 0; t < n_groups; t++)
            for (i32 v = 0; v < alpha_size; v++)
                s->rfreq[t][v] = 0;

        // Six tables' lengths packed in threes, for the common case.
        if (n_groups == 6) {
            for (i32 v = 0; v < alpha_size; v++) {
                s->len_pack[v][0] = u32(s->len[1][v] << 16) | s->len[0][v];
                s->len_pack[v][1] = u32(s->len[3][v] << 16) | s->len[2][v];
                s->len_pack[v][2] = u32(s->len[5][v] << 16) | s->len[4][v];
            }
        }

        n_selectors = 0;
        gs          = 0;
        for (;;) {
            // The group [gs, ge].
            if (gs >= s->n_mtf)
                break;
            ge = gs + BZ_G_SIZE - 1;
            if (ge >= s->n_mtf)
                ge = s->n_mtf - 1;

            // What the group costs under each table.
            for (i32 t = 0; t < n_groups; t++)
                cost[t] = 0;

            if (n_groups == 6 && 50 == ge - gs + 1) {
                u32 cost01 = 0, cost23 = 0, cost45 = 0;
                for (i32 nn = 0; nn < 50; nn++) {
                    u16 icv = mtfv[gs + nn];
                    cost01 += s->len_pack[icv][0];
                    cost23 += s->len_pack[icv][1];
                    cost45 += s->len_pack[icv][2];
                }
                cost[0] = u16(cost01 & 0xffff);
                cost[1] = u16(cost01 >> 16);
                cost[2] = u16(cost23 & 0xffff);
                cost[3] = u16(cost23 >> 16);
                cost[4] = u16(cost45 & 0xffff);
                cost[5] = u16(cost45 >> 16);
            } else {
                for (i32 i = gs; i <= ge; i++) {
                    u16 icv = mtfv[i];
                    for (i32 t = 0; t < n_groups; t++)
                        cost[t] = u16(cost[t] + s->len[t][icv]);
                }
            }

            // The cheapest table, as the group's selector.
            i32 bc = 999999999, bt = -1;
            for (i32 t = 0; t < n_groups; t++)
                if (cost[t] < bc) {
                    bc = cost[t];
                    bt = t;
                }
            fave[bt]++;
            s->selector[n_selectors] = u8(bt);
            n_selectors++;

            // Its symbol frequencies, to the selected table.
            for (i32 i = gs; i <= ge; i++)
                s->rfreq[bt][mtfv[i]]++;

            gs = ge + 1;
        }

        // New tables from the frequencies, no code longer than 17 bits.
        for (i32 t = 0; t < n_groups; t++)
            if (!bz_hb_make_code_lengths(&s->len[t][0], &s->rfreq[t][0], alpha_size, 17))
                return false;
    }

    if (!(n_groups < 8) || !(n_selectors < 32768 && n_selectors <= BZ_MAX_SELECTORS))
        return false;

    // The selectors, move-to-front coded.
    {
        u8 pos[BZ_N_GROUPS];
        for (i32 i = 0; i < n_groups; i++)
            pos[i] = u8(i);
        for (i32 i = 0; i < n_selectors; i++) {
            u8 ll_i = s->selector[i];
            i32 j   = 0;
            u8 tmp  = pos[j];
            while (ll_i != tmp) {
                j++;
                u8 tmp2 = tmp;
                tmp     = pos[j];
                pos[j]  = tmp2;
            }
            pos[0]             = tmp;
            s->selector_mtf[i] = u8(j);
        }
    }

    // The codes of each table.
    for (i32 t = 0; t < n_groups; t++) {
        i32 min_len = 32;
        i32 max_len = 0;
        for (i32 i = 0; i < alpha_size; i++) {
            if (s->len[t][i] > max_len)
                max_len = s->len[t][i];
            if (s->len[t][i] < min_len)
                min_len = s->len[t][i];
        }
        if (max_len > 17 || min_len < 1)
            return false;
        bz_hb_assign_codes(&s->code[t][0], &s->len[t][0], min_len, max_len, alpha_size);
    }

    // The mapping table: which of sixteen ranges are used, then which bytes
    // of those.
    {
        bool in_use16[16];
        for (i32 i = 0; i < 16; i++) {
            in_use16[i] = false;
            for (i32 j = 0; j < 16; j++)
                if (s->in_use[i * 16 + j])
                    in_use16[i] = true;
        }

        for (i32 i = 0; i < 16; i++)
            bs_w(s, 1, in_use16[i] ? 1 : 0);

        for (i32 i = 0; i < 16; i++)
            if (in_use16[i])
                for (i32 j = 0; j < 16; j++)
                    bs_w(s, 1, s->in_use[i * 16 + j] ? 1 : 0);
    }

    // The selectors.
    bs_w(s, 3, u32(n_groups));
    bs_w(s, 15, u32(n_selectors));
    for (i32 i = 0; i < n_selectors; i++) {
        for (i32 j = 0; j < s->selector_mtf[i]; j++)
            bs_w(s, 1, 1);
        bs_w(s, 1, 0);
    }

    // The coding tables, each length a delta from the one before.
    for (i32 t = 0; t < n_groups; t++) {
        i32 curr = s->len[t][0];
        bs_w(s, 5, u32(curr));
        for (i32 i = 0; i < alpha_size; i++) {
            while (curr < s->len[t][i]) {
                bs_w(s, 2, 2);
                curr++;
            }
            while (curr > s->len[t][i]) {
                bs_w(s, 2, 3);
                curr--;
            }
            bs_w(s, 1, 0);
        }
    }

    // The block itself.
    i32 sel_ctr = 0;
    gs          = 0;
    for (;;) {
        if (gs >= s->n_mtf)
            break;
        ge = gs + BZ_G_SIZE - 1;
        if (ge >= s->n_mtf)
            ge = s->n_mtf - 1;
        if (s->selector[sel_ctr] >= n_groups)
            return false;

        const u8 *len   = &s->len[s->selector[sel_ctr]][0];
        const i32 *code = &s->code[s->selector[sel_ctr]][0];
        for (i32 i = gs; i <= ge; i++)
            bs_w(s, len[mtfv[i]], u32(code[mtfv[i]]));

        gs = ge + 1;
        sel_ctr++;
    }
    return sel_ctr == n_selectors;
}

void compress_block(S *s, bool is_last_block)
{
    if (s->nblock > 0) {
        s->block_crc    = ~s->block_crc;
        s->combined_crc = (s->combined_crc << 1) | (s->combined_crc >> 31);
        s->combined_crc ^= s->block_crc;
        if (s->block_no > 1)
            s->num_z = 0;

        bz_block_sort(s);
        if (s->bug)
            return;
    }

    s->zbits = reinterpret_cast<u8 *>(s->arr2) + s->nblock;

    // The stream header, before the first block.
    if (s->block_no == 1) {
        bs_init_write(s);
        bs_put_u8(s, BZ_HDR_B);
        bs_put_u8(s, BZ_HDR_Z);
        bs_put_u8(s, BZ_HDR_H);
        bs_put_u8(s, u8(BZ_HDR_0 + s->block_size_100k));
    }

    if (s->nblock > 0) {
        // The block magic, pi in BCD, then its CRC.
        bs_put_u8(s, 0x31);
        bs_put_u8(s, 0x41);
        bs_put_u8(s, 0x59);
        bs_put_u8(s, 0x26);
        bs_put_u8(s, 0x53);
        bs_put_u8(s, 0x59);
        bs_put_u32(s, s->block_crc);

        // The randomised bit, never set.
        bs_w(s, 1, 0);

        bs_w(s, 24, u32(s->orig_ptr));
        generate_mtf_values(s);
        if (!send_mtf_values(s)) {
            s->bug = true;
            return;
        }
    }

    // The stream trailer, sqrt(pi) in BCD, after the last block.
    if (is_last_block) {
        bs_put_u8(s, 0x17);
        bs_put_u8(s, 0x72);
        bs_put_u8(s, 0x45);
        bs_put_u8(s, 0x38);
        bs_put_u8(s, 0x50);
        bs_put_u8(s, 0x90);
        bs_put_u32(s, s->combined_crc);
        bs_finish_write(s);
    }
}

// ---------------------------------------------------------- the stream

void prepare_new_block(S *s)
{
    s->nblock        = 0;
    s->num_z         = 0;
    s->state_out_pos = 0;
    s->block_crc     = 0xffffffff;
    for (i32 i = 0; i < 256; i++)
        s->in_use[i] = false;
    s->block_no++;
}

void init_rl(S *s)
{
    s->state_in_ch  = 256;
    s->state_in_len = 0;
}

bool isempty_rl(const S *s)
{
    return !(s->state_in_ch < 256 && s->state_in_len > 0);
}

void add_pair_to_block(S *s)
{
    u8 ch = u8(s->state_in_ch);
    for (i32 i = 0; i < s->state_in_len; i++)
        bz_crc_byte(s->block_crc, ch);
    s->in_use[s->state_in_ch] = true;
    switch (s->state_in_len) {
    case 1:
        s->block[s->nblock++] = ch;
        break;
    case 2:
        s->block[s->nblock++] = ch;
        s->block[s->nblock++] = ch;
        break;
    case 3:
        s->block[s->nblock++] = ch;
        s->block[s->nblock++] = ch;
        s->block[s->nblock++] = ch;
        break;
    default:
        s->in_use[s->state_in_len - 4] = true;
        s->block[s->nblock++]          = ch;
        s->block[s->nblock++]          = ch;
        s->block[s->nblock++]          = ch;
        s->block[s->nblock++]          = ch;
        s->block[s->nblock++]          = u8(s->state_in_len - 4);
        break;
    }
}

void flush_rl(S *s)
{
    if (s->state_in_ch < 256)
        add_pair_to_block(s);
    init_rl(s);
}

inline void add_char_to_block(S *s, u32 zchh)
{
    if (zchh != s->state_in_ch && s->state_in_len == 1) {
        // The common case: a byte unlike the one before.
        u8 ch = u8(s->state_in_ch);
        bz_crc_byte(s->block_crc, ch);
        s->in_use[s->state_in_ch] = true;
        s->block[s->nblock++]     = ch;
        s->state_in_ch            = zchh;
    } else if (zchh != s->state_in_ch || s->state_in_len == 255) {
        if (s->state_in_ch < 256)
            add_pair_to_block(s);
        s->state_in_ch  = zchh;
        s->state_in_len = 1;
    } else {
        s->state_in_len++;
    }
}

bool copy_input_until_stop(S *s)
{
    bool progress_in = false;

    if (s->mode == S::RUNNING) {
        for (;;) {
            if (s->nblock >= s->nblock_max)
                break;
            if (s->avail_in == 0)
                break;
            progress_in = true;
            add_char_to_block(s, *s->next_in);
            s->next_in++;
            s->avail_in--;
            s->total_in++;
        }
    } else {
        for (;;) {
            if (s->nblock >= s->nblock_max)
                break;
            if (s->avail_in == 0)
                break;
            if (s->avail_in_expect == 0)
                break;
            progress_in = true;
            add_char_to_block(s, *s->next_in);
            s->next_in++;
            s->avail_in--;
            s->total_in++;
            s->avail_in_expect--;
        }
    }
    return progress_in;
}

bool copy_output_until_stop(S *s)
{
    bool progress_out = false;

    for (;;) {
        if (s->avail_out == 0)
            break;
        if (s->state_out_pos >= s->num_z)
            break;
        progress_out = true;
        *s->next_out = s->zbits[s->state_out_pos];
        s->state_out_pos++;
        s->avail_out--;
        s->next_out++;
        s->total_out++;
    }
    return progress_out;
}

bool handle_compress(S *s)
{
    bool progress_in  = false;
    bool progress_out = false;

    for (;;) {
        if (s->state == S::OUTPUT) {
            progress_out |= copy_output_until_stop(s);
            if (s->state_out_pos < s->num_z)
                break;
            if (s->mode == S::FINISHING && s->avail_in_expect == 0 && isempty_rl(s))
                break;
            prepare_new_block(s);
            s->state = S::INPUT;
            if (s->mode == S::FLUSHING && s->avail_in_expect == 0 && isempty_rl(s))
                break;
        }

        if (s->state == S::INPUT) {
            progress_in |= copy_input_until_stop(s);
            if (s->mode != S::RUNNING && s->avail_in_expect == 0) {
                flush_rl(s);
                compress_block(s, s->mode == S::FINISHING);
                if (s->bug)
                    break;
                s->state = S::OUTPUT;
            } else if (s->nblock >= s->nblock_max) {
                compress_block(s, false);
                if (s->bug)
                    break;
                s->state = S::OUTPUT;
            } else if (s->avail_in == 0) {
                break;
            }
        }
    }

    return progress_in || progress_out;
}

// After BZ2_bzCompress, with its return codes but for one: BZ_RC_PARAM is
// a call that could not move, which upstream calls a sequence error when
// finishing.
int compress_run(S *s, BzAction action)
{
    for (;;) {
        bool progress;
        switch (s->mode) {
        case S::IDLE:
            return BZ_RC_SEQUENCE;

        case S::RUNNING:
            if (action == BzAction::Run) {
                progress = handle_compress(s);
                if (s->bug)
                    return BZ_RC_BUG;
                return progress ? BZ_RC_RUN_OK : BZ_RC_PARAM;
            }
            s->avail_in_expect = s->avail_in;
            s->mode            = action == BzAction::Flush ? S::FLUSHING : S::FINISHING;
            continue;

        case S::FLUSHING:
            if (action != BzAction::Flush || s->avail_in_expect != s->avail_in)
                return BZ_RC_SEQUENCE;
            handle_compress(s);
            if (s->bug)
                return BZ_RC_BUG;
            if (s->avail_in_expect > 0 || !isempty_rl(s) || s->state_out_pos < s->num_z)
                return BZ_RC_FLUSH_OK;
            s->mode = S::RUNNING;
            return BZ_RC_RUN_OK;

        case S::FINISHING:
            if (action != BzAction::Finish || s->avail_in_expect != s->avail_in)
                return BZ_RC_SEQUENCE;
            progress = handle_compress(s);
            if (s->bug)
                return BZ_RC_BUG;
            if (!progress)
                return BZ_RC_PARAM;
            if (s->avail_in_expect > 0 || !isempty_rl(s) || s->state_out_pos < s->num_z)
                return BZ_RC_FINISH_OK;
            s->mode = S::IDLE;
            return BZ_RC_STREAM_END;
        }
        return BZ_RC_SEQUENCE;
    }
}

void free_state(S *s)
{
    if (!s)
        return;
    heap_free(s->arr1);
    heap_free(s->arr2);
    heap_free(s->ftab);
    heap_free(s);
}

} // namespace

BzCompressor &BzCompressor::operator=(BzCompressor &&o) noexcept
{
    if (this != &o) {
        free_state(s_);
        s_   = o.s_;
        o.s_ = nullptr;
    }
    return *this;
}

BzCompressor::~BzCompressor()
{
    free_state(s_);
}

Result<void> BzCompressor::init(int block_size_100k, int work_factor)
{
    free_state(s_);
    s_ = nullptr;
    if (block_size_100k < 1 || block_size_100k > 9 || work_factor < 0 || work_factor > 250)
        return Err(Error::Invalid);
    if (work_factor == 0)
        work_factor = 30;

    S *s = static_cast<S *>(heap_alloc(sizeof(S)));
    if (s == nullptr)
        return Err(Error::NoMemory);
    __builtin_memset(static_cast<void *>(s), 0, sizeof(S));

    usize n = usize(100000) * usize(block_size_100k);
    s->arr1 = static_cast<u32 *>(heap_alloc(n * sizeof(u32)));
    s->arr2 = static_cast<u32 *>(heap_alloc((n + BZ_N_OVERSHOOT) * sizeof(u32)));
    s->ftab = static_cast<u32 *>(heap_alloc(65537 * sizeof(u32)));
    if (s->arr1 == nullptr || s->arr2 == nullptr || s->ftab == nullptr) {
        free_state(s);
        return Err(Error::NoMemory);
    }

    s->block_no        = 0;
    s->state           = S::INPUT;
    s->mode            = S::RUNNING;
    s->combined_crc    = 0;
    s->block_size_100k = block_size_100k;
    s->nblock_max      = 100000 * block_size_100k - 19;
    s->work_factor     = work_factor;

    s->block = reinterpret_cast<u8 *>(s->arr2);
    s->mtfv  = reinterpret_cast<u16 *>(s->arr1);
    s->zbits = nullptr;
    s->ptr   = s->arr1;

    init_rl(s);
    prepare_new_block(s);
    s_ = s;
    return {};
}

BzStatus BzCompressor::step(Span<const u8> &in, Span<u8> &out, BzAction action)
{
    if (!s_)
        return BzStatus::Misuse;
    s_->next_in   = in.data();
    s_->avail_in  = in.size();
    s_->next_out  = out.data();
    s_->avail_out = out.size();
    int rc        = compress_run(s_, action);
    in            = Span<const u8>(s_->next_in, s_->avail_in);
    out           = Span<u8>(s_->next_out, s_->avail_out);

    switch (rc) {
    case BZ_RC_RUN_OK:
        return BzStatus::Ok;
    case BZ_RC_FLUSH_OK:
    case BZ_RC_FINISH_OK:
        return BzStatus::More;
    case BZ_RC_STREAM_END:
        return BzStatus::End;
    case BZ_RC_PARAM:
        return BzStatus::Stuck;
    case BZ_RC_SEQUENCE:
        return BzStatus::Misuse;
    }
    s_->mode = S::IDLE;
    return BzStatus::Misuse;
}

u64 BzCompressor::total_in() const
{
    return s_ ? s_->total_in : 0;
}

u64 BzCompressor::total_out() const
{
    return s_ ? s_->total_out : 0;
}

usize BzCompressor::bound(usize len)
{
    return len + len / 100 + 600;
}
