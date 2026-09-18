// Private to src/bzip2/, after bzlib_private.h: the two stream states, and
// what the files share. Not API; bzip2/bzip2.h is.
#pragma once

#include "bzip2/bzip2.h"

constexpr u8 BZ_HDR_B = 0x42; // 'B'
constexpr u8 BZ_HDR_Z = 0x5a; // 'Z'
constexpr u8 BZ_HDR_H = 0x68; // 'h'
constexpr u8 BZ_HDR_0 = 0x30; // '0'

constexpr i32 BZ_MAX_ALPHA_SIZE = 258;
constexpr i32 BZ_MAX_CODE_LEN   = 23;

constexpr i32 BZ_RUNA = 0;
constexpr i32 BZ_RUNB = 1;

constexpr i32 BZ_N_GROUPS = 6;
constexpr i32 BZ_G_SIZE   = 50;
constexpr i32 BZ_N_ITERS  = 4;

constexpr i32 BZ_MAX_SELECTORS = 2 + (900000 / BZ_G_SIZE);

constexpr i32 BZ_N_RADIX     = 2;
constexpr i32 BZ_N_QSORT     = 12;
constexpr i32 BZ_N_SHELL     = 18;
constexpr i32 BZ_N_OVERSHOOT = BZ_N_RADIX + BZ_N_QSORT + BZ_N_SHELL + 2;

constexpr i32 BZ_MTFA_SIZE = 4096;
constexpr i32 BZ_MTFL_SIZE = 16;

// bzlib.h's return codes, which the files pass among themselves.
enum BzRc {
    BZ_RC_OK         = 0,
    BZ_RC_RUN_OK     = 1,
    BZ_RC_FLUSH_OK   = 2,
    BZ_RC_FINISH_OK  = 3,
    BZ_RC_STREAM_END = 4,
    BZ_RC_SEQUENCE   = -1,
    BZ_RC_PARAM      = -2,
    BZ_RC_MEM        = -3,
    BZ_RC_DATA       = -4,
    BZ_RC_MAGIC      = -5,
    BZ_RC_BUG        = -100, // an internal check failed: upstream's AssertH
};

// randtable.c and crctable.c.
struct BzCrcTable {
    u32 v[256];
};

extern const i32 BZ_RAND_NUMS[512];
extern const BzCrcTable BZ_CRC_TABLE;

inline void bz_crc_byte(u32 &crc, u8 c)
{
    crc = (crc << 8) ^ BZ_CRC_TABLE.v[(crc >> 24) ^ c];
}

// The compressor's state, after EState.
struct BzEncodeState {
    enum Mode : i32 { IDLE = 1, RUNNING, FLUSHING, FINISHING };
    enum Phase : i32 { OUTPUT = 1, INPUT };

    const u8 *next_in;
    usize avail_in;
    u8 *next_out;
    usize avail_out;
    u64 total_in;
    u64 total_out;

    Mode mode;
    Phase state;
    bool bug; // an internal check failed; the stream is dead

    usize avail_in_expect; // avail_in when a flush or finish was asked for

    // The block sort.
    u32 *arr1;
    u32 *arr2;
    u32 *ftab;
    i32 orig_ptr;

    // Aliases of arr1 and arr2.
    u32 *ptr;
    u8 *block;
    u16 *mtfv;
    u8 *zbits;

    i32 work_factor;

    // Run-length encoding of the input.
    u32 state_in_ch;
    i32 state_in_len;

    i32 nblock;
    i32 nblock_max;
    i32 num_z;
    i32 state_out_pos;

    i32 n_in_use;
    bool in_use[256];
    u8 unseq_to_seq[256];

    u32 bs_buff;
    i32 bs_live;

    u32 block_crc;
    u32 combined_crc;

    i32 block_no;
    i32 block_size_100k;

    // Coding the MTF values.
    i32 n_mtf;
    i32 mtf_freq[BZ_MAX_ALPHA_SIZE];
    u8 selector[BZ_MAX_SELECTORS];
    u8 selector_mtf[BZ_MAX_SELECTORS];

    u8 len[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
    i32 code[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
    i32 rfreq[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
    u32 len_pack[BZ_MAX_ALPHA_SIZE][4]; // three used; four indexes faster
};

// The decompressor's state, after DState.
struct BzDecodeState {
    enum Phase : i32 {
        IDLE = 1,
        OUTPUT,
        MAGIC_1 = 10,
        MAGIC_2,
        MAGIC_3,
        MAGIC_4,
        BLKHDR_1,
        BLKHDR_2,
        BLKHDR_3,
        BLKHDR_4,
        BLKHDR_5,
        BLKHDR_6,
        BCRC_1,
        BCRC_2,
        BCRC_3,
        BCRC_4,
        RANDBIT,
        ORIGPTR_1,
        ORIGPTR_2,
        ORIGPTR_3,
        MAPPING_1,
        MAPPING_2,
        SELECTOR_1,
        SELECTOR_2,
        SELECTOR_3,
        CODING_1,
        CODING_2,
        CODING_3,
        MTF_1,
        MTF_2,
        MTF_3,
        MTF_4,
        MTF_5,
        MTF_6,
        ENDHDR_2,
        ENDHDR_3,
        ENDHDR_4,
        ENDHDR_5,
        ENDHDR_6,
        CCRC_1,
        CCRC_2,
        CCRC_3,
        CCRC_4,
    };

    const u8 *next_in;
    usize avail_in;
    u8 *next_out;
    usize avail_out;
    u64 total_in;
    u64 total_out;

    Phase state;
    i32 failed;      // the BzRc that ended the stream, or 0
    const char *why; // what the Corrupt or NotBzip2 found, or null

    // The final run-length decoding.
    u8 state_out_ch;
    i32 state_out_len;
    bool block_randomised;
    i32 r_n_to_go;
    i32 r_t_pos;

    u32 bs_buff;
    i32 bs_live;

    i32 block_size_100k;
    bool small_decompress;
    i32 curr_block_no;

    // Undoing the Burrows-Wheeler transform.
    i32 orig_ptr;
    u32 t_pos;
    i32 k0;
    i32 unzftab[256];
    i32 nblock_used;
    i32 cftab[257];
    i32 cftab_copy[257];

    u32 *tt; // fast

    u16 *ll16; // small
    u8 *ll4;

    u32 stored_block_crc;
    u32 stored_combined_crc;
    u32 calculated_block_crc;
    u32 calculated_combined_crc;

    i32 n_in_use;
    bool in_use[256];
    bool in_use16[16];
    u8 seq_to_unseq[256];

    // Decoding the MTF values.
    u8 mtfa[BZ_MTFA_SIZE];
    i32 mtfbase[256 / BZ_MTFL_SIZE];
    u8 selector[BZ_MAX_SELECTORS];
    u8 selector_mtf[BZ_MAX_SELECTORS];
    u8 len[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];

    i32 limit[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
    i32 base[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
    i32 perm[BZ_N_GROUPS][BZ_MAX_ALPHA_SIZE];
    i32 min_lens[BZ_N_GROUPS];

    // bz_decompress's locals, across a return for more input.
    i32 save_i;
    i32 save_j;
    i32 save_t;
    i32 save_alpha_size;
    i32 save_n_groups;
    i32 save_n_selectors;
    i32 save_eob;
    i32 save_group_no;
    i32 save_group_pos;
    i32 save_next_sym;
    i32 save_nblock_max;
    i32 save_nblock;
    i32 save_es;
    i32 save_n;
    i32 save_curr;
    i32 save_zt;
    i32 save_zn;
    i32 save_zvec;
    i32 save_zj;
    i32 save_g_sel;
    i32 save_g_minlen;
    i32 *save_g_limit;
    i32 *save_g_base;
    i32 *save_g_perm;
};

// blocksort.cpp: arr1 in sorted order. Sets s->bug when a check fails.
void bz_block_sort(BzEncodeState *s);

// huffman.cpp. bz_hb_make_code_lengths is false when a check fails.
bool bz_hb_make_code_lengths(u8 *len, const i32 *freq, i32 alpha_size, i32 max_len);
void bz_hb_assign_codes(i32 *code, const u8 *length, i32 min_len, i32 max_len, i32 alpha_size);
void bz_hb_create_decode_tables(i32 *limit, i32 *base, i32 *perm, const u8 *length, i32 min_len,
                                i32 max_len, i32 alpha_size);
