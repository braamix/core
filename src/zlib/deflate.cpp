// Deflate, after zlib's deflate.c and trees.c (Jean-loup Gailly). Every
// choice is theirs -- the hash, the chains, the lazy match, the block split --
// so that the output is zlib's to the byte; the static trees and the code
// maps that trees.h carries are built at compile time instead.
#include "kernel/alloc.h"
#include "zlib/zlib.h"

// A tree node: a frequency, then a code, in fc; a parent, then a length, in dl.
struct DeflateNode {
    u16 fc;
    u16 dl;
};

struct DeflateStaticDesc {
    const DeflateNode *static_tree; // or null
    const int *extra_bits;          // per code, or null
    int extra_base;                 // first code with extra bits
    int elems;                      // most elements in the tree
    int max_length;                 // longest code
};

struct DeflateTreeDesc {
    DeflateNode *dyn_tree;
    int max_code; // largest code of non-zero frequency
    const DeflateStaticDesc *stat_desc;
};

namespace {

enum Rc {
    RC_OK     = 0,
    RC_END    = 1,
    RC_STREAM = -2,
    RC_DATA   = -3,
    RC_MEM    = -4,
    RC_BUF    = -5,
};

enum {
    F_NONE    = 0,
    F_PARTIAL = 1,
    F_SYNC    = 2,
    F_FULL    = 3,
    F_FINISH  = 4,
    F_BLOCK   = 5,
};

enum {
    ST_DEFAULT  = 0,
    ST_FILTERED = 1,
    ST_HUFFMAN  = 2,
    ST_RLE      = 3,
    ST_FIXED    = 4,
};

enum { DT_BINARY = 0, DT_TEXT = 1, DT_UNKNOWN = 2 };

constexpr int LENGTH_CODES = 29;
constexpr int LITERALS     = 256;
constexpr int L_CODES      = LITERALS + 1 + LENGTH_CODES;
constexpr int D_CODES      = 30;
constexpr int BL_CODES     = 19;
constexpr int HEAP_SIZE    = 2 * L_CODES + 1;
constexpr int MAX_BITS     = 15;
constexpr int MAX_BL_BITS  = 7;
constexpr int BUF_SIZE     = 16;
constexpr int END_BLOCK    = 256;
constexpr int REP_3_6      = 16;
constexpr int REPZ_3_10    = 17;
constexpr int REPZ_11_138  = 18;
constexpr int MIN_MATCH    = 3;
constexpr int MAX_MATCH    = 258;
constexpr u32 MIN_LOOKAHEAD = MAX_MATCH + MIN_MATCH + 1;
constexpr u32 WIN_INIT      = MAX_MATCH;
constexpr int MAX_MEM_LEVEL = 9;
constexpr u32 TOO_FAR       = 4096;
constexpr u32 MAX_STORED    = 65535;
constexpr int DIST_CODE_LEN = 512;
constexpr u32 NIL           = 0;
constexpr u32 PRESET_DICT   = 0x20;
constexpr u32 DEFLATED      = 8;
constexpr u8 OS_CODE        = 3; // Unix

constexpr int STORED_BLOCK = 0;
constexpr int STATIC_TREES = 1;
constexpr int DYN_TREES    = 2;

constexpr int INIT_STATE    = 42;
constexpr int GZIP_STATE    = 57;
constexpr int EXTRA_STATE   = 69;
constexpr int NAME_STATE    = 73;
constexpr int COMMENT_STATE = 91;
constexpr int HCRC_STATE    = 103;
constexpr int BUSY_STATE    = 113;
constexpr int FINISH_STATE  = 666;

constexpr int EXTRA_LBITS[LENGTH_CODES] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                            2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
constexpr int EXTRA_DBITS[D_CODES] = { 0, 0, 0, 0, 1, 1, 2,  2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
constexpr int EXTRA_BLBITS[BL_CODES] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 2, 3, 7 };
constexpr u8 BL_ORDER[BL_CODES] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

constexpr unsigned bi_reverse(unsigned code, int len)
{
    unsigned res = 0;
    do {
        res |= code & 1;
        code >>= 1, res <<= 1;
    } while (--len > 0);
    return res >> 1;
}

// Assigns each code of `tree` its bits from the counts of each length.
constexpr void gen_codes(DeflateNode *tree, int max_code, const u16 *bl_count)
{
    u16 next_code[MAX_BITS + 1]{};
    unsigned code = 0;
    for (int bits = 1; bits <= MAX_BITS; bits++) {
        code            = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = u16(code);
    }
    for (int n = 0; n <= max_code; n++) {
        int len = tree[n].dl;
        if (len == 0)
            continue;
        tree[n].fc = u16(bi_reverse(next_code[len]++, len));
    }
}

struct StaticTrees {
    DeflateNode ltree[L_CODES + 2];
    DeflateNode dtree[D_CODES];
    u8 dist_code[DIST_CODE_LEN];
    u8 length_code[MAX_MATCH - MIN_MATCH + 1];
    int base_length[LENGTH_CODES];
    int base_dist[D_CODES];
};

// trees.c's tr_static_init, run by the compiler.
constexpr StaticTrees make_trees()
{
    StaticTrees t{};
    int n, code, length = 0, dist = 0;
    u16 bl_count[MAX_BITS + 1]{};

    for (code = 0; code < LENGTH_CODES - 1; code++) {
        t.base_length[code] = length;
        for (n = 0; n < (1 << EXTRA_LBITS[code]); n++)
            t.length_code[length++] = u8(code);
    }
    t.length_code[length - 1] = u8(code);

    for (code = 0; code < 16; code++) {
        t.base_dist[code] = dist;
        for (n = 0; n < (1 << EXTRA_DBITS[code]); n++)
            t.dist_code[dist++] = u8(code);
    }
    dist >>= 7;
    for (; code < D_CODES; code++) {
        t.base_dist[code] = dist << 7;
        for (n = 0; n < (1 << (EXTRA_DBITS[code] - 7)); n++)
            t.dist_code[256 + dist++] = u8(code);
    }

    n = 0;
    while (n <= 143)
        t.ltree[n++].dl = 8, bl_count[8]++;
    while (n <= 255)
        t.ltree[n++].dl = 9, bl_count[9]++;
    while (n <= 279)
        t.ltree[n++].dl = 7, bl_count[7]++;
    while (n <= 287)
        t.ltree[n++].dl = 8, bl_count[8]++;
    gen_codes(t.ltree, L_CODES + 1, bl_count);

    for (n = 0; n < D_CODES; n++) {
        t.dtree[n].dl = 5;
        t.dtree[n].fc = u16(bi_reverse(unsigned(n), 5));
    }
    return t;
}

constexpr StaticTrees TREES = make_trees();

constexpr DeflateStaticDesc STATIC_L_DESC  = { TREES.ltree, EXTRA_LBITS, LITERALS + 1, L_CODES,
                                               MAX_BITS };
constexpr DeflateStaticDesc STATIC_D_DESC  = { TREES.dtree, EXTRA_DBITS, 0, D_CODES, MAX_BITS };
constexpr DeflateStaticDesc STATIC_BL_DESC = { nullptr, EXTRA_BLBITS, 0, BL_CODES, MAX_BL_BITS };

enum Func : u8 { STORED, FAST, SLOW };

struct Config {
    u16 good_length; // reduce lazy search above this match length
    u16 max_lazy;    // do not perform lazy search above this match length
    u16 nice_length; // quit search above this match length
    u16 max_chain;
    Func func;
};

constexpr Config CONFIGURATION[10] = {
    { 0, 0, 0, 0, STORED },         // 0: store only
    { 4, 4, 8, 4, FAST },           // 1: max speed, no lazy matches
    { 4, 5, 16, 8, FAST },          //
    { 4, 6, 32, 32, FAST },         //
    { 4, 4, 16, 16, SLOW },         // 4: lazy matches
    { 8, 16, 32, 32, SLOW },        //
    { 8, 16, 128, 128, SLOW },      //
    { 8, 32, 128, 256, SLOW },      //
    { 32, 128, 258, 1024, SLOW },   //
    { 32, 258, 258, 4096, SLOW },   // 9: max compression
};

// Ranks Block between None and Partial.
constexpr int rank(int f)
{
    return (f * 2) - (f > 4 ? 9 : 0);
}

enum BlockState { NEED_MORE, BLOCK_DONE, FINISH_STARTED, FINISH_DONE };

} // namespace

struct DeflateState {
    // The stream, as a z_stream carries it.
    const u8 *next_in;
    u32 avail_in;
    u64 total_in;
    u8 *next_out;
    u32 avail_out;
    u64 total_out;
    const char *msg;
    u32 adler;
    i32 data_type;

    int status;
    u8 *pending_buf;
    u32 pending_buf_size;
    u8 *pending_out; // next pending byte to output
    u32 pending;     // bytes in the pending buffer
    int wrap;        // bit 0 zlib, bit 1 gzip; negative once the trailer is out
    ZHeader *gzhead;
    u32 gzindex; // where in extra, name, or comment
    int last_flush;

    u32 w_size; // LZ77 window size
    u32 w_bits;
    u32 w_mask;

    // Input is read into the second half and moved to the first, so that a
    // match may reach back w_size less MIN_LOOKAHEAD.
    u8 *window;
    u32 window_size; // 2 * w_size

    u16 *prev; // older string with the same hash, for the last w_size strings
    u16 *head; // heads of the hash chains, or NIL

    u32 ins_h; // hash of the string to insert
    u32 hash_size;
    u32 hash_bits;
    u32 hash_mask;
    u32 hash_shift; // after MIN_MATCH steps the oldest byte is out of the hash

    i32 block_start; // window position of the current block; negative once slid

    u32 match_length;
    u32 prev_match;
    int match_available;
    u32 strstart;
    u32 match_start;
    u32 lookahead;
    u32 prev_length;      // best match at the previous step
    u32 max_chain_length; // hash chains are never searched beyond this
    u32 max_lazy_match;   // levels 4..9; max_insert_length for 1..3
    int level;
    int strategy;
    u32 good_match; // search faster above this
    int nice_match; // stop searching above this

    DeflateNode dyn_ltree[HEAP_SIZE];
    DeflateNode dyn_dtree[2 * D_CODES + 1];
    DeflateNode bl_tree[2 * BL_CODES + 1];

    DeflateTreeDesc l_desc;
    DeflateTreeDesc d_desc;
    DeflateTreeDesc bl_desc;

    u16 bl_count[MAX_BITS + 1];
    int heap[2 * L_CODES + 1];
    int heap_len;
    int heap_max;
    u8 depth[2 * L_CODES + 1];

    u8 *sym_buf; // distances and literals/lengths, three bytes a symbol
    u32 lit_bufsize;
    u32 sym_next;
    u32 sym_end;

    u32 opt_len;    // bits of the current block with optimal trees
    u32 static_len; // bits of the current block with static trees
    u32 matches;    // stored: pending slide_hash calls
    u32 insert;     // bytes at the end of the window left to insert

    u16 bi_buf;   // bits go in at the bottom
    int bi_valid; // valid bits in bi_buf
    int bi_used;  // bits used in the last byte at a byte boundary

    u32 high_water; // window bytes past this have never been written
    int slid;       // the hash has been slid since it was cleared
};

namespace {

using D = DeflateState;

const char *const MSG_STREAM = "stream error";
const char *const MSG_BUF    = "buffer error";
const char *const MSG_MEM    = "insufficient memory";

// ------------------------------------------------------------ trees.c

inline void put_byte(D *s, unsigned c)
{
    s->pending_buf[s->pending++] = u8(c);
}

inline void put_short(D *s, unsigned w)
{
    put_byte(s, w & 0xff);
    put_byte(s, u16(w) >> 8);
}

inline void send_bits(D *s, int value, int length)
{
    if (s->bi_valid > BUF_SIZE - length) {
        s->bi_buf |= u16(u16(value) << s->bi_valid);
        put_short(s, s->bi_buf);
        s->bi_buf = u16(u16(value) >> (BUF_SIZE - s->bi_valid));
        s->bi_valid += length - BUF_SIZE;
    } else {
        s->bi_buf |= u16(u16(value) << s->bi_valid);
        s->bi_valid += length;
    }
}

inline void send_code(D *s, int c, const DeflateNode *tree)
{
    send_bits(s, tree[c].fc, tree[c].dl);
}

inline u8 d_code(unsigned dist)
{
    return dist < 256 ? TREES.dist_code[dist] : TREES.dist_code[256 + (dist >> 7)];
}

void bi_flush(D *s)
{
    if (s->bi_valid == 16) {
        put_short(s, s->bi_buf);
        s->bi_buf   = 0;
        s->bi_valid = 0;
    } else if (s->bi_valid >= 8) {
        put_byte(s, u8(s->bi_buf));
        s->bi_buf >>= 8;
        s->bi_valid -= 8;
    }
}

void bi_windup(D *s)
{
    if (s->bi_valid > 8)
        put_short(s, s->bi_buf);
    else if (s->bi_valid > 0)
        put_byte(s, u8(s->bi_buf));
    s->bi_used  = ((s->bi_valid - 1) & 7) + 1;
    s->bi_buf   = 0;
    s->bi_valid = 0;
}

void init_block(D *s)
{
    for (int n = 0; n < L_CODES; n++)
        s->dyn_ltree[n].fc = 0;
    for (int n = 0; n < D_CODES; n++)
        s->dyn_dtree[n].fc = 0;
    for (int n = 0; n < BL_CODES; n++)
        s->bl_tree[n].fc = 0;

    s->dyn_ltree[END_BLOCK].fc = 1;
    s->opt_len = s->static_len = 0;
    s->sym_next = s->matches = 0;
}

void tr_init(D *s)
{
    s->l_desc.dyn_tree   = s->dyn_ltree;
    s->l_desc.stat_desc  = &STATIC_L_DESC;
    s->d_desc.dyn_tree   = s->dyn_dtree;
    s->d_desc.stat_desc  = &STATIC_D_DESC;
    s->bl_desc.dyn_tree  = s->bl_tree;
    s->bl_desc.stat_desc = &STATIC_BL_DESC;

    s->bi_buf   = 0;
    s->bi_valid = 0;
    s->bi_used  = 0;
    init_block(s);
}

constexpr int SMALLEST = 1;

inline bool smaller(const DeflateNode *tree, int n, int m, const u8 *depth)
{
    return tree[n].fc < tree[m].fc || (tree[n].fc == tree[m].fc && depth[n] <= depth[m]);
}

// Restores the heap property below k.
void pqdownheap(D *s, const DeflateNode *tree, int k)
{
    int v = s->heap[k];
    int j = k << 1;
    while (j <= s->heap_len) {
        if (j < s->heap_len && smaller(tree, s->heap[j + 1], s->heap[j], s->depth))
            j++;
        if (smaller(tree, v, s->heap[j], s->depth))
            break;
        s->heap[k] = s->heap[j];
        k          = j;
        j <<= 1;
    }
    s->heap[k] = v;
}

// The optimal bit lengths, limited to max_length; opt_len and static_len.
void gen_bitlen(D *s, DeflateTreeDesc *desc)
{
    DeflateNode *tree        = desc->dyn_tree;
    int max_code             = desc->max_code;
    const DeflateNode *stree = desc->stat_desc->static_tree;
    const int *extra         = desc->stat_desc->extra_bits;
    int base                 = desc->stat_desc->extra_base;
    int max_length           = desc->stat_desc->max_length;
    int h, n, m, bits, xbits;
    u16 f;
    int overflow = 0;

    for (bits = 0; bits <= MAX_BITS; bits++)
        s->bl_count[bits] = 0;

    tree[s->heap[s->heap_max]].dl = 0;

    for (h = s->heap_max + 1; h < HEAP_SIZE; h++) {
        n    = s->heap[h];
        bits = tree[tree[n].dl].dl + 1;
        if (bits > max_length)
            bits = max_length, overflow++;
        tree[n].dl = u16(bits);

        if (n > max_code)
            continue;

        s->bl_count[bits]++;
        xbits = 0;
        if (n >= base)
            xbits = extra[n - base];
        f = tree[n].fc;
        s->opt_len += u32(f) * unsigned(bits + xbits);
        if (stree)
            s->static_len += u32(f) * unsigned(stree[n].dl + xbits);
    }
    if (overflow == 0)
        return;

    do {
        bits = max_length - 1;
        while (s->bl_count[bits] == 0)
            bits--;
        s->bl_count[bits]--;
        s->bl_count[bits + 1] += 2;
        s->bl_count[max_length]--;
        overflow -= 2;
    } while (overflow > 0);

    for (bits = max_length; bits != 0; bits--) {
        n = s->bl_count[bits];
        while (n != 0) {
            m = s->heap[--h];
            if (m > max_code)
                continue;
            if (unsigned(tree[m].dl) != unsigned(bits)) {
                s->opt_len += (u32(bits) - tree[m].dl) * tree[m].fc;
                tree[m].dl = u16(bits);
            }
            n--;
        }
    }
}

// A Huffman tree for desc's frequencies: its lengths and its codes.
void build_tree(D *s, DeflateTreeDesc *desc)
{
    DeflateNode *tree        = desc->dyn_tree;
    const DeflateNode *stree = desc->stat_desc->static_tree;
    int elems                = desc->stat_desc->elems;
    int n, m;
    int max_code = -1;
    int node;

    s->heap_len = 0, s->heap_max = HEAP_SIZE;

    for (n = 0; n < elems; n++) {
        if (tree[n].fc != 0) {
            s->heap[++(s->heap_len)] = max_code = n;
            s->depth[n]                         = 0;
        } else {
            tree[n].dl = 0;
        }
    }

    // At least two codes of non-zero frequency, so that one bit is sent.
    while (s->heap_len < 2) {
        node = s->heap[++(s->heap_len)] = (max_code < 2 ? ++max_code : 0);
        tree[node].fc                   = 1;
        s->depth[node]                  = 0;
        s->opt_len--;
        if (stree)
            s->static_len -= stree[node].dl;
    }
    desc->max_code = max_code;

    for (n = s->heap_len / 2; n >= 1; n--)
        pqdownheap(s, tree, n);

    node = elems;
    do {
        n                    = s->heap[SMALLEST];
        s->heap[SMALLEST]    = s->heap[s->heap_len--];
        pqdownheap(s, tree, SMALLEST);
        m = s->heap[SMALLEST];

        s->heap[--(s->heap_max)] = n;
        s->heap[--(s->heap_max)] = m;

        tree[node].fc  = u16(tree[n].fc + tree[m].fc);
        s->depth[node] = u8((s->depth[n] >= s->depth[m] ? s->depth[n] : s->depth[m]) + 1);
        tree[n].dl = tree[m].dl = u16(node);

        s->heap[SMALLEST] = node++;
        pqdownheap(s, tree, SMALLEST);
    } while (s->heap_len >= 2);

    s->heap[--(s->heap_max)] = s->heap[SMALLEST];

    gen_bitlen(s, desc);
    gen_codes(tree, max_code, s->bl_count);
}

// Counts the bit-length codes a tree's lengths will need.
void scan_tree(D *s, DeflateNode *tree, int max_code)
{
    int prevlen   = -1;
    int curlen;
    int nextlen   = tree[0].dl;
    int count     = 0;
    int max_count = 7;
    int min_count = 4;

    if (nextlen == 0)
        max_count = 138, min_count = 3;
    tree[max_code + 1].dl = u16(0xffff);

    for (int n = 0; n <= max_code; n++) {
        curlen  = nextlen;
        nextlen = tree[n + 1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            s->bl_tree[curlen].fc = u16(s->bl_tree[curlen].fc + count);
        } else if (curlen != 0) {
            if (curlen != prevlen)
                s->bl_tree[curlen].fc++;
            s->bl_tree[REP_3_6].fc++;
        } else if (count <= 10) {
            s->bl_tree[REPZ_3_10].fc++;
        } else {
            s->bl_tree[REPZ_11_138].fc++;
        }
        count   = 0;
        prevlen = curlen;
        if (nextlen == 0)
            max_count = 138, min_count = 3;
        else if (curlen == nextlen)
            max_count = 6, min_count = 3;
        else
            max_count = 7, min_count = 4;
    }
}

// Sends a tree's lengths in the bit-length code.
void send_tree(D *s, const DeflateNode *tree, int max_code)
{
    int prevlen   = -1;
    int curlen;
    int nextlen   = tree[0].dl;
    int count     = 0;
    int max_count = 7;
    int min_count = 4;

    if (nextlen == 0)
        max_count = 138, min_count = 3;

    for (int n = 0; n <= max_code; n++) {
        curlen  = nextlen;
        nextlen = tree[n + 1].dl;
        if (++count < max_count && curlen == nextlen) {
            continue;
        } else if (count < min_count) {
            do {
                send_code(s, curlen, s->bl_tree);
            } while (--count != 0);
        } else if (curlen != 0) {
            if (curlen != prevlen) {
                send_code(s, curlen, s->bl_tree);
                count--;
            }
            send_code(s, REP_3_6, s->bl_tree);
            send_bits(s, count - 3, 2);
        } else if (count <= 10) {
            send_code(s, REPZ_3_10, s->bl_tree);
            send_bits(s, count - 3, 3);
        } else {
            send_code(s, REPZ_11_138, s->bl_tree);
            send_bits(s, count - 11, 7);
        }
        count   = 0;
        prevlen = curlen;
        if (nextlen == 0)
            max_count = 138, min_count = 3;
        else if (curlen == nextlen)
            max_count = 6, min_count = 3;
        else
            max_count = 7, min_count = 4;
    }
}

// The bit-length tree; returns the index in BL_ORDER of the last code sent.
int build_bl_tree(D *s)
{
    int max_blindex;

    scan_tree(s, s->dyn_ltree, s->l_desc.max_code);
    scan_tree(s, s->dyn_dtree, s->d_desc.max_code);
    build_tree(s, &s->bl_desc);

    // At least four bit-length codes are sent.
    for (max_blindex = BL_CODES - 1; max_blindex >= 3; max_blindex--)
        if (s->bl_tree[BL_ORDER[max_blindex]].dl != 0)
            break;
    s->opt_len += 3 * (u32(max_blindex) + 1) + 5 + 5 + 4;
    return max_blindex;
}

void send_all_trees(D *s, int lcodes, int dcodes, int blcodes)
{
    send_bits(s, lcodes - 257, 5);
    send_bits(s, dcodes - 1, 5);
    send_bits(s, blcodes - 4, 4);
    for (int rank = 0; rank < blcodes; rank++)
        send_bits(s, s->bl_tree[BL_ORDER[rank]].dl, 3);
    send_tree(s, s->dyn_ltree, lcodes - 1);
    send_tree(s, s->dyn_dtree, dcodes - 1);
}

void tr_stored_block(D *s, const u8 *buf, u32 stored_len, int last)
{
    send_bits(s, (STORED_BLOCK << 1) + last, 3);
    bi_windup(s);
    put_short(s, u16(stored_len));
    put_short(s, u16(~stored_len));
    if (stored_len)
        __builtin_memcpy(s->pending_buf + s->pending, buf, stored_len);
    s->pending += stored_len;
}

void tr_flush_bits(D *s)
{
    bi_flush(s);
}

void tr_align(D *s)
{
    send_bits(s, STATIC_TREES << 1, 3);
    send_code(s, END_BLOCK, TREES.ltree);
    bi_flush(s);
}

void compress_block(D *s, const DeflateNode *ltree, const DeflateNode *dtree)
{
    unsigned dist;
    int lc;
    unsigned sx = 0;
    unsigned code;
    int extra;

    if (s->sym_next != 0)
        do {
            dist = s->sym_buf[sx++] & 0xff;
            dist += unsigned(s->sym_buf[sx++] & 0xff) << 8;
            lc = s->sym_buf[sx++];
            if (dist == 0) {
                send_code(s, lc, ltree);
            } else {
                code = TREES.length_code[lc];
                send_code(s, int(code) + LITERALS + 1, ltree);
                extra = EXTRA_LBITS[code];
                if (extra != 0) {
                    lc -= TREES.base_length[code];
                    send_bits(s, lc, extra);
                }
                dist--;
                code = d_code(dist);
                send_code(s, int(code), dtree);
                extra = EXTRA_DBITS[code];
                if (extra != 0) {
                    dist -= unsigned(TREES.base_dist[code]);
                    send_bits(s, int(dist), extra);
                }
            }
        } while (sx < s->sym_next);

    send_code(s, END_BLOCK, ltree);
}

// Binary if any byte of 0..6, 14..25 or 28..31 occurs; text if 9, 10, 13 or
// 32..255 does; binary otherwise.
int detect_data_type(D *s)
{
    u32 block_mask = 0xf3ffc07fU;
    int n;

    for (n = 0; n <= 31; n++, block_mask >>= 1)
        if ((block_mask & 1) && (s->dyn_ltree[n].fc != 0))
            return DT_BINARY;

    if (s->dyn_ltree[9].fc != 0 || s->dyn_ltree[10].fc != 0 || s->dyn_ltree[13].fc != 0)
        return DT_TEXT;
    for (n = 32; n < LITERALS; n++)
        if (s->dyn_ltree[n].fc != 0)
            return DT_TEXT;
    return DT_BINARY;
}

// The current block, stored, fixed or dynamic, whichever is smallest.
void tr_flush_block(D *s, const u8 *buf, u32 stored_len, int last)
{
    u32 opt_lenb, static_lenb;
    int max_blindex = 0;

    if (s->level > 0) {
        if (s->data_type == DT_UNKNOWN)
            s->data_type = detect_data_type(s);

        build_tree(s, &s->l_desc);
        build_tree(s, &s->d_desc);
        max_blindex = build_bl_tree(s);

        opt_lenb    = (s->opt_len + 3 + 7) >> 3;
        static_lenb = (s->static_len + 3 + 7) >> 3;

        if (static_lenb <= opt_lenb || s->strategy == ST_FIXED)
            opt_lenb = static_lenb;
    } else {
        opt_lenb = static_lenb = stored_len + 5;
    }

    if (stored_len + 4 <= opt_lenb && buf != nullptr) {
        tr_stored_block(s, buf, stored_len, last);
    } else if (static_lenb == opt_lenb) {
        send_bits(s, (STATIC_TREES << 1) + last, 3);
        compress_block(s, TREES.ltree, TREES.dtree);
    } else {
        send_bits(s, (DYN_TREES << 1) + last, 3);
        send_all_trees(s, s->l_desc.max_code + 1, s->d_desc.max_code + 1, max_blindex + 1);
        compress_block(s, s->dyn_ltree, s->dyn_dtree);
    }
    init_block(s);

    if (last)
        bi_windup(s);
}

inline bool tally_lit(D *s, u8 c)
{
    s->sym_buf[s->sym_next++] = 0;
    s->sym_buf[s->sym_next++] = 0;
    s->sym_buf[s->sym_next++] = c;
    s->dyn_ltree[c].fc++;
    return s->sym_next == s->sym_end;
}

inline bool tally_dist(D *s, unsigned distance, unsigned length)
{
    u8 len   = u8(length);
    u16 dist = u16(distance);
    s->sym_buf[s->sym_next++] = u8(dist);
    s->sym_buf[s->sym_next++] = u8(dist >> 8);
    s->sym_buf[s->sym_next++] = len;
    dist--;
    s->dyn_ltree[TREES.length_code[len] + LITERALS + 1].fc++;
    s->dyn_dtree[d_code(dist)].fc++;
    return s->sym_next == s->sym_end;
}

// ---------------------------------------------------------- deflate.c

inline void update_hash(D *s, u32 &h, u8 c)
{
    h = ((h << s->hash_shift) ^ c) & s->hash_mask;
}

// Inserts the string at str into the hash; returns the previous head.
inline u32 insert_string(D *s, u32 str)
{
    update_hash(s, s->ins_h, s->window[str + (MIN_MATCH - 1)]);
    u32 match_head = s->prev[str & s->w_mask] = s->head[s->ins_h];
    s->head[s->ins_h]                         = u16(str);
    return match_head;
}

void clear_hash(D *s)
{
    s->head[s->hash_size - 1] = NIL;
    __builtin_memset(s->head, 0, (s->hash_size - 1) * sizeof(*s->head));
    s->slid = 0;
}

// Slides the hash down with the window.
void slide_hash(D *s)
{
    unsigned n, m;
    u16 *p;
    u32 wsize = s->w_size;

    n = s->hash_size;
    p = &s->head[n];
    do {
        m  = *--p;
        *p = u16(m >= wsize ? m - wsize : NIL);
    } while (--n);
    n = wsize;
    p = &s->prev[n];
    do {
        m  = *--p;
        *p = u16(m >= wsize ? m - wsize : NIL);
    } while (--n);
    s->slid = 1;
}

// Reads input, updating the check and the count.
unsigned read_buf(D *s, u8 *buf, unsigned size)
{
    unsigned len = s->avail_in;
    if (len > size)
        len = size;
    if (len == 0)
        return 0;

    s->avail_in -= len;
    __builtin_memcpy(buf, s->next_in, len);
    if (s->wrap == 1)
        s->adler = adler32_update(s->adler, Bytes(buf, len));
    else if (s->wrap == 2)
        s->adler = crc32_update(s->adler, Bytes(buf, len));
    s->next_in += len;
    s->total_in += len;
    return len;
}

// Fills the window when the lookahead runs short. On entry lookahead is under
// MIN_LOOKAHEAD; on return strstart <= window_size - MIN_LOOKAHEAD.
void fill_window(D *s)
{
    unsigned n;
    unsigned more;
    u32 wsize = s->w_size;

    do {
        more = unsigned(s->window_size - s->lookahead - s->strstart);

        if (s->strstart >= wsize + (s->w_size - MIN_LOOKAHEAD)) {
            __builtin_memcpy(s->window, s->window + wsize, wsize - more);
            s->match_start -= wsize;
            s->strstart -= wsize;
            s->block_start -= i32(wsize);
            if (s->insert > s->strstart)
                s->insert = s->strstart;
            slide_hash(s);
            more += wsize;
        }
        if (s->avail_in == 0)
            break;

        n = read_buf(s, s->window + s->strstart + s->lookahead, more);
        s->lookahead += n;

        if (s->lookahead + s->insert >= u32(MIN_MATCH)) {
            u32 str  = s->strstart - s->insert;
            s->ins_h = s->window[str];
            update_hash(s, s->ins_h, s->window[str + 1]);
            while (s->insert) {
                update_hash(s, s->ins_h, s->window[str + MIN_MATCH - 1]);
                s->prev[str & s->w_mask] = s->head[s->ins_h];
                s->head[s->ins_h]        = u16(str);
                str++;
                s->insert--;
                if (s->lookahead + s->insert < u32(MIN_MATCH))
                    break;
            }
        }
    } while (s->lookahead < MIN_LOOKAHEAD && s->avail_in != 0);

    // Zero WIN_INIT bytes past the data, once, for the match routines' reads.
    if (s->high_water < s->window_size) {
        u32 curr = s->strstart + s->lookahead;
        u32 init;

        if (s->high_water < curr) {
            init = s->window_size - curr;
            if (init > WIN_INIT)
                init = WIN_INIT;
            __builtin_memset(s->window + curr, 0, init);
            s->high_water = curr + init;
        } else if (s->high_water < curr + WIN_INIT) {
            init = curr + WIN_INIT - s->high_water;
            if (init > s->window_size - s->high_water)
                init = s->window_size - s->high_water;
            __builtin_memset(s->window + s->high_water, 0, init);
            s->high_water += init;
        }
    }
}

int reset_keep(D *s)
{
    s->total_in = s->total_out = 0;
    s->msg                     = nullptr;
    s->data_type               = DT_UNKNOWN;

    s->pending     = 0;
    s->pending_out = s->pending_buf;

    if (s->wrap < 0)
        s->wrap = -s->wrap;
    s->status     = s->wrap == 2 ? GZIP_STATE : INIT_STATE;
    s->adler      = s->wrap == 2 ? crc32_update(0, Bytes()) : adler32_update(1, Bytes());
    s->last_flush = -2;

    tr_init(s);
    return RC_OK;
}

void lm_init(D *s)
{
    s->window_size = 2 * s->w_size;
    clear_hash(s);

    s->max_lazy_match   = CONFIGURATION[s->level].max_lazy;
    s->good_match       = CONFIGURATION[s->level].good_length;
    s->nice_match       = CONFIGURATION[s->level].nice_length;
    s->max_chain_length = CONFIGURATION[s->level].max_chain;

    s->strstart        = 0;
    s->block_start     = 0;
    s->lookahead       = 0;
    s->insert          = 0;
    s->match_length = s->prev_length = MIN_MATCH - 1;
    s->match_available               = 0;
    s->ins_h                         = 0;
}

int reset(D *s)
{
    int ret = reset_keep(s);
    if (ret == RC_OK)
        lm_init(s);
    return ret;
}

inline void put_short_msb(D *s, u32 b)
{
    put_byte(s, u8(b >> 8));
    put_byte(s, u8(b & 0xff));
}

// Hands as much pending output to next_out as fits.
void flush_pending(D *s)
{
    tr_flush_bits(s);
    unsigned len = s->pending > s->avail_out ? s->avail_out : s->pending;
    if (len == 0)
        return;

    __builtin_memcpy(s->next_out, s->pending_out, len);
    s->next_out += len;
    s->pending_out += len;
    s->total_out += len;
    s->avail_out -= len;
    s->pending -= len;
    if (s->pending == 0)
        s->pending_out = s->pending_buf;
}

// The longest match at strstart along the chain from cur_match, longer than
// prev_length; sets match_start. Never more than the lookahead.
u32 longest_match(D *s, u32 cur_match)
{
    unsigned chain_length = s->max_chain_length;
    u8 *scan              = s->window + s->strstart;
    u8 *match;
    int len;
    int best_len   = int(s->prev_length);
    int nice_match = s->nice_match;
    u32 max_dist   = s->w_size - MIN_LOOKAHEAD;
    u32 limit      = s->strstart > max_dist ? s->strstart - max_dist : NIL;
    u16 *prev      = s->prev;
    u32 wmask      = s->w_mask;
    u8 *strend     = s->window + s->strstart + MAX_MATCH;
    u8 scan_end1   = scan[best_len - 1];
    u8 scan_end    = scan[best_len];

    if (s->prev_length >= s->good_match)
        chain_length >>= 2;
    if (u32(nice_match) > s->lookahead)
        nice_match = int(s->lookahead);

    do {
        match = s->window + cur_match;

        if (match[best_len] != scan_end || match[best_len - 1] != scan_end1 ||
            *match != *scan || *++match != scan[1])
            continue;

        scan += 2, match++;

        do {
        } while (*++scan == *++match && *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match && *++scan == *++match &&
                 *++scan == *++match && *++scan == *++match && scan < strend);

        len  = MAX_MATCH - int(strend - scan);
        scan = strend - MAX_MATCH;

        if (len > best_len) {
            s->match_start = cur_match;
            best_len       = len;
            if (len >= nice_match)
                break;
            scan_end1 = scan[best_len - 1];
            scan_end  = scan[best_len];
        }
    } while ((cur_match = prev[cur_match & wmask]) > limit && --chain_length != 0);

    if (u32(best_len) <= s->lookahead)
        return u32(best_len);
    return s->lookahead;
}

#define FLUSH_BLOCK_ONLY(s, last)                                                             \
    do {                                                                                      \
        tr_flush_block(s, (s)->block_start >= 0 ? &(s)->window[unsigned((s)->block_start)]    \
                                                : nullptr,                                    \
                       u32(i32((s)->strstart) - (s)->block_start), (last));                   \
        (s)->block_start = i32((s)->strstart);                                                \
        flush_pending(s);                                                                     \
    } while (0)

#define FLUSH_BLOCK(s, last)                                                                  \
    do {                                                                                      \
        FLUSH_BLOCK_ONLY(s, last);                                                            \
        if ((s)->avail_out == 0)                                                              \
            return (last) ? FINISH_STARTED : NEED_MORE;                                       \
    } while (0)

inline u32 min_u32(u32 a, u32 b)
{
    return a > b ? b : a;
}

// Level 0: stored blocks, copied straight from next_in to next_out where the
// buffers allow. s->matches counts the hash slides owed if the level changes.
BlockState deflate_stored(D *s, int flush)
{
    unsigned min_block = min_u32(s->pending_buf_size - 5, s->w_size);
    int last           = 0;
    unsigned len, left, have;
    unsigned used = s->avail_in;
    do {
        len  = MAX_STORED;
        have = (unsigned(s->bi_valid) + 42) >> 3;
        if (s->avail_out < have)
            break;
        have = s->avail_out - have;
        left = unsigned(i32(s->strstart) - s->block_start);
        if (len > left + s->avail_in)
            len = left + s->avail_in;
        if (len > have)
            len = have;

        if (len < min_block &&
            ((len == 0 && flush != F_FINISH) || flush == F_NONE || len != left + s->avail_in))
            break;

        last = flush == F_FINISH && len == left + s->avail_in ? 1 : 0;
        tr_stored_block(s, nullptr, 0, last);

        s->pending_buf[s->pending - 4] = u8(len);
        s->pending_buf[s->pending - 3] = u8(len >> 8);
        s->pending_buf[s->pending - 2] = u8(~len);
        s->pending_buf[s->pending - 1] = u8(~len >> 8);

        flush_pending(s);

        if (left) {
            if (left > len)
                left = len;
            __builtin_memcpy(s->next_out, s->window + s->block_start, left);
            s->next_out += left;
            s->avail_out -= left;
            s->total_out += left;
            s->block_start += i32(left);
            len -= left;
        }

        if (len) {
            read_buf(s, s->next_out, len);
            s->next_out += len;
            s->avail_out -= len;
            s->total_out += len;
        }
    } while (last == 0);

    used -= s->avail_in;
    if (used) {
        if (used >= s->w_size) {
            s->matches = 2;
            __builtin_memcpy(s->window, s->next_in - s->w_size, s->w_size);
            s->strstart = s->w_size;
            s->insert   = s->strstart;
        } else {
            if (s->window_size - s->strstart <= used) {
                s->strstart -= s->w_size;
                __builtin_memcpy(s->window, s->window + s->w_size, s->strstart);
                if (s->matches < 2)
                    s->matches++;
                if (s->insert > s->strstart)
                    s->insert = s->strstart;
            }
            __builtin_memcpy(s->window + s->strstart, s->next_in - used, used);
            s->strstart += used;
            s->insert += min_u32(used, s->w_size - s->insert);
        }
        s->block_start = i32(s->strstart);
    }
    if (s->high_water < s->strstart)
        s->high_water = s->strstart;

    if (last) {
        s->bi_used = 8;
        return FINISH_DONE;
    }

    if (flush != F_NONE && flush != F_FINISH && s->avail_in == 0 &&
        i32(s->strstart) == s->block_start)
        return BLOCK_DONE;

    have = s->window_size - s->strstart;
    if (s->avail_in > have && s->block_start >= i32(s->w_size)) {
        s->block_start -= i32(s->w_size);
        s->strstart -= s->w_size;
        __builtin_memcpy(s->window, s->window + s->w_size, s->strstart);
        if (s->matches < 2)
            s->matches++;
        have += s->w_size;
        if (s->insert > s->strstart)
            s->insert = s->strstart;
    }
    if (have > s->avail_in)
        have = s->avail_in;
    if (have) {
        read_buf(s, s->window + s->strstart, have);
        s->strstart += have;
        s->insert += min_u32(have, s->w_size - s->insert);
    }
    if (s->high_water < s->strstart)
        s->high_water = s->strstart;

    have      = (unsigned(s->bi_valid) + 42) >> 3;
    have      = min_u32(s->pending_buf_size - have, MAX_STORED);
    min_block = min_u32(have, s->w_size);
    left      = unsigned(i32(s->strstart) - s->block_start);
    if (left >= min_block ||
        ((left || flush == F_FINISH) && flush != F_NONE && s->avail_in == 0 && left <= have)) {
        len  = min_u32(left, have);
        last = flush == F_FINISH && s->avail_in == 0 && len == left ? 1 : 0;
        tr_stored_block(s, s->window + s->block_start, len, last);
        s->block_start += i32(len);
        flush_pending(s);
    }

    if (last)
        s->bi_used = 8;
    return last ? FINISH_STARTED : NEED_MORE;
}

// Levels 1..3: no lazy evaluation, and short matches alone go into the hash.
BlockState deflate_fast(D *s, int flush)
{
    u32 hash_head;
    bool bflush;

    for (;;) {
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == F_NONE)
                return NEED_MORE;
            if (s->lookahead == 0)
                break;
        }

        hash_head = NIL;
        if (s->lookahead >= u32(MIN_MATCH))
            hash_head = insert_string(s, s->strstart);

        if (hash_head != NIL && s->strstart - hash_head <= s->w_size - MIN_LOOKAHEAD)
            s->match_length = longest_match(s, hash_head);
        if (s->match_length >= u32(MIN_MATCH)) {
            bflush = tally_dist(s, s->strstart - s->match_start, s->match_length - MIN_MATCH);

            s->lookahead -= s->match_length;

            if (s->match_length <= s->max_lazy_match && s->lookahead >= u32(MIN_MATCH)) {
                s->match_length--;
                do {
                    s->strstart++;
                    insert_string(s, s->strstart);
                } while (--s->match_length != 0);
                s->strstart++;
            } else {
                s->strstart += s->match_length;
                s->match_length = 0;
                s->ins_h        = s->window[s->strstart];
                update_hash(s, s->ins_h, s->window[s->strstart + 1]);
            }
        } else {
            bflush = tally_lit(s, s->window[s->strstart]);
            s->lookahead--;
            s->strstart++;
        }
        if (bflush)
            FLUSH_BLOCK(s, 0);
    }
    s->insert = s->strstart < u32(MIN_MATCH - 1) ? s->strstart : MIN_MATCH - 1;
    if (flush == F_FINISH) {
        FLUSH_BLOCK(s, 1);
        return FINISH_DONE;
    }
    if (s->sym_next)
        FLUSH_BLOCK(s, 0);
    return BLOCK_DONE;
}

// Levels 4..9: a match is taken only if the next position has no better one.
BlockState deflate_slow(D *s, int flush)
{
    u32 hash_head;
    bool bflush;

    for (;;) {
        if (s->lookahead < MIN_LOOKAHEAD) {
            fill_window(s);
            if (s->lookahead < MIN_LOOKAHEAD && flush == F_NONE)
                return NEED_MORE;
            if (s->lookahead == 0)
                break;
        }

        hash_head = NIL;
        if (s->lookahead >= u32(MIN_MATCH))
            hash_head = insert_string(s, s->strstart);

        s->prev_length = s->match_length, s->prev_match = s->match_start;
        s->match_length = MIN_MATCH - 1;

        if (hash_head != NIL && s->prev_length < s->max_lazy_match &&
            s->strstart - hash_head <= s->w_size - MIN_LOOKAHEAD) {
            s->match_length = longest_match(s, hash_head);

            if (s->match_length <= 5 &&
                (s->strategy == ST_FILTERED ||
                 (s->match_length == u32(MIN_MATCH) && s->strstart - s->match_start > TOO_FAR)))
                s->match_length = MIN_MATCH - 1;
        }

        if (s->prev_length >= u32(MIN_MATCH) && s->match_length <= s->prev_length) {
            u32 max_insert = s->strstart + s->lookahead - MIN_MATCH;

            bflush = tally_dist(s, s->strstart - 1 - s->prev_match, s->prev_length - MIN_MATCH);

            s->lookahead -= s->prev_length - 1;
            s->prev_length -= 2;
            do {
                if (++s->strstart <= max_insert)
                    insert_string(s, s->strstart);
            } while (--s->prev_length != 0);
            s->match_available = 0;
            s->match_length    = MIN_MATCH - 1;
            s->strstart++;

            if (bflush)
                FLUSH_BLOCK(s, 0);

        } else if (s->match_available) {
            bflush = tally_lit(s, s->window[s->strstart - 1]);
            if (bflush)
                FLUSH_BLOCK_ONLY(s, 0);
            s->strstart++;
            s->lookahead--;
            if (s->avail_out == 0)
                return NEED_MORE;
        } else {
            s->match_available = 1;
            s->strstart++;
            s->lookahead--;
        }
    }
    if (s->match_available) {
        tally_lit(s, s->window[s->strstart - 1]);
        s->match_available = 0;
    }
    s->insert = s->strstart < u32(MIN_MATCH - 1) ? s->strstart : MIN_MATCH - 1;
    if (flush == F_FINISH) {
        FLUSH_BLOCK(s, 1);
        return FINISH_DONE;
    }
    if (s->sym_next)
        FLUSH_BLOCK(s, 0);
    return BLOCK_DONE;
}

// Rle: runs of one byte, matched at distance one; no hash.
BlockState deflate_rle(D *s, int flush)
{
    bool bflush;
    u32 prev;
    u8 *scan, *strend;

    for (;;) {
        if (s->lookahead <= u32(MAX_MATCH)) {
            fill_window(s);
            if (s->lookahead <= u32(MAX_MATCH) && flush == F_NONE)
                return NEED_MORE;
            if (s->lookahead == 0)
                break;
        }

        s->match_length = 0;
        if (s->lookahead >= u32(MIN_MATCH) && s->strstart > 0) {
            scan = s->window + s->strstart - 1;
            prev = *scan;
            if (prev == *++scan && prev == *++scan && prev == *++scan) {
                strend = s->window + s->strstart + MAX_MATCH;
                do {
                } while (prev == *++scan && prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan && prev == *++scan &&
                         prev == *++scan && prev == *++scan && scan < strend);
                s->match_length = MAX_MATCH - u32(strend - scan);
                if (s->match_length > s->lookahead)
                    s->match_length = s->lookahead;
            }
        }

        if (s->match_length >= u32(MIN_MATCH)) {
            bflush = tally_dist(s, 1, s->match_length - MIN_MATCH);
            s->lookahead -= s->match_length;
            s->strstart += s->match_length;
            s->match_length = 0;
        } else {
            bflush = tally_lit(s, s->window[s->strstart]);
            s->lookahead--;
            s->strstart++;
        }
        if (bflush)
            FLUSH_BLOCK(s, 0);
    }
    s->insert = 0;
    if (flush == F_FINISH) {
        FLUSH_BLOCK(s, 1);
        return FINISH_DONE;
    }
    if (s->sym_next)
        FLUSH_BLOCK(s, 0);
    return BLOCK_DONE;
}

// Huffman only: literals and no matches; no hash.
BlockState deflate_huff(D *s, int flush)
{
    bool bflush;

    for (;;) {
        if (s->lookahead == 0) {
            fill_window(s);
            if (s->lookahead == 0) {
                if (flush == F_NONE)
                    return NEED_MORE;
                break;
            }
        }

        s->match_length = 0;
        bflush          = tally_lit(s, s->window[s->strstart]);
        s->lookahead--;
        s->strstart++;
        if (bflush)
            FLUSH_BLOCK(s, 0);
    }
    s->insert = 0;
    if (flush == F_FINISH) {
        FLUSH_BLOCK(s, 1);
        return FINISH_DONE;
    }
    if (s->sym_next)
        FLUSH_BLOCK(s, 0);
    return BLOCK_DONE;
}

#undef FLUSH_BLOCK
#undef FLUSH_BLOCK_ONLY

BlockState run_func(D *s, Func f, int flush)
{
    switch (f) {
    case STORED:
        return deflate_stored(s, flush);
    case FAST:
        return deflate_fast(s, flush);
    case SLOW:
        break;
    }
    return deflate_slow(s, flush);
}

inline int err_return(D *s, int err)
{
    s->msg = err == RC_STREAM ? MSG_STREAM : err == RC_BUF ? MSG_BUF : MSG_MEM;
    return err;
}

// Adds pending[beg..] to the gzip header's CRC.
inline void hcrc_update(D *s, u32 beg)
{
    if (s->gzhead->hcrc && s->pending > beg)
        s->adler = crc32_update(s->adler, Bytes(s->pending_buf + beg, s->pending - beg));
}

// zlib's deflate(), on zlib's return codes.
int deflate_run(D *s, int flush)
{
    if (flush > F_BLOCK || flush < 0)
        return RC_STREAM;
    if (s->status == FINISH_STATE && flush != F_FINISH)
        return err_return(s, RC_STREAM);
    if (s->avail_out == 0)
        return err_return(s, RC_BUF);

    int old_flush = s->last_flush;
    s->last_flush = flush;

    if (s->pending != 0) {
        flush_pending(s);
        if (s->avail_out == 0) {
            // Called again with more room and perhaps nothing else: not an error.
            s->last_flush = -1;
            return RC_OK;
        }
    } else if (s->avail_in == 0 && rank(flush) <= rank(old_flush) && flush != F_FINISH) {
        return err_return(s, RC_BUF);
    }

    if (s->status == FINISH_STATE && s->avail_in != 0)
        return err_return(s, RC_BUF);

    if (s->status == INIT_STATE && s->wrap == 0)
        s->status = BUSY_STATE;
    if (s->status == INIT_STATE) {
        u32 header = (DEFLATED + ((s->w_bits - 8) << 4)) << 8;
        u32 level_flags;

        if (s->strategy >= ST_HUFFMAN || s->level < 2)
            level_flags = 0;
        else if (s->level < 6)
            level_flags = 1;
        else if (s->level == 6)
            level_flags = 2;
        else
            level_flags = 3;
        header |= (level_flags << 6);
        if (s->strstart != 0)
            header |= PRESET_DICT;
        header += 31 - (header % 31);

        put_short_msb(s, header);

        if (s->strstart != 0) {
            put_short_msb(s, s->adler >> 16);
            put_short_msb(s, s->adler & 0xffff);
        }
        s->adler  = adler32_update(1, Bytes());
        s->status = BUSY_STATE;

        flush_pending(s);
        if (s->pending != 0) {
            s->last_flush = -1;
            return RC_OK;
        }
    }
    if (s->status == GZIP_STATE) {
        s->adler = crc32_update(0, Bytes());
        put_byte(s, 31);
        put_byte(s, 139);
        put_byte(s, 8);
        if (s->gzhead == nullptr) {
            put_byte(s, 0);
            put_byte(s, 0);
            put_byte(s, 0);
            put_byte(s, 0);
            put_byte(s, 0);
            put_byte(s, s->level == 9 ? 2 : (s->strategy >= ST_HUFFMAN || s->level < 2 ? 4 : 0));
            put_byte(s, OS_CODE);
            s->status = BUSY_STATE;

            flush_pending(s);
            if (s->pending != 0) {
                s->last_flush = -1;
                return RC_OK;
            }
        } else {
            put_byte(s, (s->gzhead->text ? 1 : 0) + (s->gzhead->hcrc ? 2 : 0) +
                            (s->gzhead->extra == nullptr ? 0 : 4) +
                            (s->gzhead->name == nullptr ? 0 : 8) +
                            (s->gzhead->comment == nullptr ? 0 : 16));
            put_byte(s, s->gzhead->time & 0xff);
            put_byte(s, (s->gzhead->time >> 8) & 0xff);
            put_byte(s, (s->gzhead->time >> 16) & 0xff);
            put_byte(s, (s->gzhead->time >> 24) & 0xff);
            put_byte(s, s->level == 9 ? 2 : (s->strategy >= ST_HUFFMAN || s->level < 2 ? 4 : 0));
            put_byte(s, unsigned(s->gzhead->os) & 0xff);
            if (s->gzhead->extra != nullptr) {
                put_byte(s, s->gzhead->extra_len & 0xff);
                put_byte(s, (s->gzhead->extra_len >> 8) & 0xff);
            }
            if (s->gzhead->hcrc)
                s->adler = crc32_update(s->adler, Bytes(s->pending_buf, s->pending));
            s->gzindex = 0;
            s->status  = EXTRA_STATE;
        }
    }
    if (s->status == EXTRA_STATE) {
        if (s->gzhead->extra != nullptr) {
            u32 beg  = s->pending;
            u32 left = (s->gzhead->extra_len & 0xffff) - s->gzindex;
            while (s->pending + left > s->pending_buf_size) {
                u32 copy = s->pending_buf_size - s->pending;
                __builtin_memcpy(s->pending_buf + s->pending, s->gzhead->extra + s->gzindex, copy);
                s->pending = s->pending_buf_size;
                hcrc_update(s, beg);
                s->gzindex += copy;
                flush_pending(s);
                if (s->pending != 0) {
                    s->last_flush = -1;
                    return RC_OK;
                }
                beg = 0;
                left -= copy;
            }
            __builtin_memcpy(s->pending_buf + s->pending, s->gzhead->extra + s->gzindex, left);
            s->pending += left;
            hcrc_update(s, beg);
            s->gzindex = 0;
        }
        s->status = NAME_STATE;
    }
    if (s->status == NAME_STATE) {
        if (s->gzhead->name != nullptr) {
            u32 beg = s->pending;
            int val;
            do {
                if (s->pending == s->pending_buf_size) {
                    hcrc_update(s, beg);
                    flush_pending(s);
                    if (s->pending != 0) {
                        s->last_flush = -1;
                        return RC_OK;
                    }
                    beg = 0;
                }
                val = s->gzhead->name[s->gzindex++];
                put_byte(s, unsigned(val));
            } while (val != 0);
            hcrc_update(s, beg);
            s->gzindex = 0;
        }
        s->status = COMMENT_STATE;
    }
    if (s->status == COMMENT_STATE) {
        if (s->gzhead->comment != nullptr) {
            u32 beg = s->pending;
            int val;
            do {
                if (s->pending == s->pending_buf_size) {
                    hcrc_update(s, beg);
                    flush_pending(s);
                    if (s->pending != 0) {
                        s->last_flush = -1;
                        return RC_OK;
                    }
                    beg = 0;
                }
                val = s->gzhead->comment[s->gzindex++];
                put_byte(s, unsigned(val));
            } while (val != 0);
            hcrc_update(s, beg);
        }
        s->status = HCRC_STATE;
    }
    if (s->status == HCRC_STATE) {
        if (s->gzhead->hcrc) {
            if (s->pending + 2 > s->pending_buf_size) {
                flush_pending(s);
                if (s->pending != 0) {
                    s->last_flush = -1;
                    return RC_OK;
                }
            }
            put_byte(s, s->adler & 0xff);
            put_byte(s, (s->adler >> 8) & 0xff);
            s->adler = crc32_update(0, Bytes());
        }
        s->status = BUSY_STATE;

        flush_pending(s);
        if (s->pending != 0) {
            s->last_flush = -1;
            return RC_OK;
        }
    }

    if (s->avail_in != 0 || s->lookahead != 0 || (flush != F_NONE && s->status != FINISH_STATE)) {
        BlockState bstate = s->level == 0                ? deflate_stored(s, flush)
                            : s->strategy == ST_HUFFMAN ? deflate_huff(s, flush)
                            : s->strategy == ST_RLE     ? deflate_rle(s, flush)
                                : run_func(s, CONFIGURATION[s->level].func, flush);

        if (bstate == FINISH_STARTED || bstate == FINISH_DONE)
            s->status = FINISH_STATE;
        if (bstate == NEED_MORE || bstate == FINISH_STARTED) {
            if (s->avail_out == 0)
                s->last_flush = -1;
            return RC_OK;
        }
        if (bstate == BLOCK_DONE) {
            if (flush == F_PARTIAL) {
                tr_align(s);
            } else if (flush != F_BLOCK) {
                tr_stored_block(s, nullptr, 0, 0);
                if (flush == F_FULL) {
                    clear_hash(s);
                    if (s->lookahead == 0) {
                        s->strstart    = 0;
                        s->block_start = 0;
                        s->insert      = 0;
                    }
                }
            }
            flush_pending(s);
            if (s->avail_out == 0) {
                s->last_flush = -1;
                return RC_OK;
            }
        }
    }

    if (flush != F_FINISH)
        return RC_OK;
    if (s->wrap <= 0)
        return RC_END;

    if (s->wrap == 2) {
        put_byte(s, s->adler & 0xff);
        put_byte(s, (s->adler >> 8) & 0xff);
        put_byte(s, (s->adler >> 16) & 0xff);
        put_byte(s, (s->adler >> 24) & 0xff);
        put_byte(s, u32(s->total_in) & 0xff);
        put_byte(s, (u32(s->total_in) >> 8) & 0xff);
        put_byte(s, (u32(s->total_in) >> 16) & 0xff);
        put_byte(s, (u32(s->total_in) >> 24) & 0xff);
    } else {
        put_short_msb(s, s->adler >> 16);
        put_short_msb(s, s->adler & 0xffff);
    }
    flush_pending(s);
    if (s->wrap > 0)
        s->wrap = -s->wrap; // the trailer only once
    return s->pending != 0 ? RC_OK : RC_END;
}

ZStatus status_of(int rc)
{
    switch (rc) {
    case RC_OK:
        return ZStatus::Ok;
    case RC_END:
        return ZStatus::End;
    case RC_DATA:
        return ZStatus::Corrupt;
    case RC_MEM:
        return ZStatus::NoMemory;
    case RC_BUF:
        return ZStatus::Stuck;
    default:
        return ZStatus::Misuse;
    }
}

void free_state(D *s)
{
    if (s) {
        heap_free(s->pending_buf);
        heap_free(s->head);
        heap_free(s->prev);
        heap_free(s->window);
        heap_free(s);
    }
}

// Allocates the four arrays for s's w_size, hash_size and lit_bufsize.
bool alloc_arrays(D *s)
{
    s->window      = static_cast<u8 *>(heap_alloc(s->w_size * 2));
    s->prev        = static_cast<u16 *>(heap_alloc(s->w_size * sizeof(u16)));
    s->head        = static_cast<u16 *>(heap_alloc(s->hash_size * sizeof(u16)));
    s->pending_buf = static_cast<u8 *>(heap_alloc(s->lit_bufsize * 4));
    return s->window && s->prev && s->head && s->pending_buf;
}

void load_io(D *s, Span<const u8> &in, Span<u8> &out)
{
    s->next_in   = in.data();
    s->avail_in  = in.size();
    s->next_out  = out.data();
    s->avail_out = out.size();
}

void store_io(D *s, Span<const u8> &in, Span<u8> &out)
{
    in  = Span<const u8>(s->next_in, s->avail_in);
    out = Span<u8>(s->next_out, s->avail_out);
}

} // namespace

Deflater &Deflater::operator=(Deflater &&o) noexcept
{
    if (this != &o) {
        free_state(s_);
        s_   = o.s_;
        o.s_ = nullptr;
    }
    return *this;
}

Deflater::~Deflater()
{
    free_state(s_);
}

Result<void> Deflater::init(int level, ZFormat format, u8 window_bits, u8 mem_level,
                            ZStrategy strategy)
{
    int wrap;
    switch (format) {
    case ZFormat::Raw:
        wrap = 0;
        break;
    case ZFormat::Zlib:
        wrap = 1;
        break;
    case ZFormat::Gzip:
        wrap = 2;
        break;
    default:
        return Err(Error::Invalid);
    }
    if (level == -1)
        level = 6;
    int wbits = window_bits;
    if (mem_level < 1 || mem_level > MAX_MEM_LEVEL || wbits < 8 || wbits > 15 || level < 0 ||
        level > 9 || u8(strategy) > ST_FIXED || (wbits == 8 && wrap != 1))
        return Err(Error::Invalid);
    if (wbits == 8)
        wbits = 9; // zlib's 256-byte window is not trusted

    free_state(s_);
    s_ = static_cast<D *>(heap_alloc(sizeof(D)));
    if (s_ == nullptr)
        return Err(Error::NoMemory);
    __builtin_memset(static_cast<void *>(s_), 0, sizeof(D));
    D *s      = s_;
    s->status = INIT_STATE;

    s->wrap   = wrap;
    s->gzhead = nullptr;
    s->w_bits = u32(wbits);
    s->w_size = 1U << s->w_bits;
    s->w_mask = s->w_size - 1;

    s->hash_bits  = u32(mem_level) + 7;
    s->hash_size  = 1U << s->hash_bits;
    s->hash_mask  = s->hash_size - 1;
    s->hash_shift = (s->hash_bits + MIN_MATCH - 1) / MIN_MATCH;

    s->high_water  = 0;
    s->lit_bufsize = 1U << (mem_level + 6);

    // pending_buf and sym_buf overlay: a symbol is three bytes and becomes at
    // most 31 bits, so what is written never catches what is still unread.
    if (!alloc_arrays(s)) {
        free_state(s);
        s_ = nullptr;
        return Err(Error::NoMemory);
    }
    s->pending_buf_size = s->lit_bufsize * 4;
    s->sym_buf          = s->pending_buf + s->lit_bufsize;
    s->sym_end          = (s->lit_bufsize - 1) * 3;

    s->level    = level;
    s->strategy = int(strategy);
    ::reset(s);
    return {};
}

ZStatus Deflater::step(Span<const u8> &in, Span<u8> &out, ZFlush flush)
{
    if (!s_)
        return ZStatus::Misuse;
    load_io(s_, in, out);
    int rc = deflate_run(s_, int(flush));
    store_io(s_, in, out);
    return status_of(rc);
}

ZStatus Deflater::reset()
{
    if (!s_)
        return ZStatus::Misuse;
    return status_of(::reset(s_));
}

ZStatus Deflater::reset_keep()
{
    if (!s_)
        return ZStatus::Misuse;
    return status_of(::reset_keep(s_));
}

ZStatus Deflater::params(int level, ZStrategy strategy, Span<const u8> &in, Span<u8> &out)
{
    if (!s_)
        return ZStatus::Misuse;
    D *s = s_;
    if (level == -1)
        level = 6;
    if (level < 0 || level > 9 || u8(strategy) > ST_FIXED)
        return ZStatus::Misuse;
    Func func = CONFIGURATION[s->level].func;

    if ((int(strategy) != s->strategy || func != CONFIGURATION[level].func) &&
        s->last_flush != -2) {
        load_io(s, in, out);
        int err = deflate_run(s, F_BLOCK);
        store_io(s, in, out);
        if (err == RC_STREAM)
            return ZStatus::Misuse;
        if (s->avail_in || (i32(s->strstart) - s->block_start) + i32(s->lookahead))
            return ZStatus::Stuck;
    }
    if (s->level != level) {
        if (s->level == 0 && s->matches != 0) {
            if (s->matches == 1)
                slide_hash(s);
            else
                clear_hash(s);
            s->matches = 0;
        }
        s->level            = level;
        s->max_lazy_match   = CONFIGURATION[level].max_lazy;
        s->good_match       = CONFIGURATION[level].good_length;
        s->nice_match       = CONFIGURATION[level].nice_length;
        s->max_chain_length = CONFIGURATION[level].max_chain;
    }
    s->strategy = int(strategy);
    return ZStatus::Ok;
}

ZStatus Deflater::set_dictionary(Bytes dict)
{
    if (!s_)
        return ZStatus::Misuse;
    D *s     = s_;
    int wrap = s->wrap;
    if (wrap == 2 || (wrap == 1 && s->status != INIT_STATE) || s->lookahead)
        return ZStatus::Misuse;

    const u8 *dictionary = dict.data();
    u32 dict_length      = dict.size();

    if (wrap == 1)
        s->adler = adler32_update(s->adler, dict);
    s->wrap = 0; // no Adler-32 in read_buf

    if (dict_length >= s->w_size) {
        if (wrap == 0) {
            clear_hash(s);
            s->strstart    = 0;
            s->block_start = 0;
            s->insert      = 0;
        }
        dictionary += dict_length - s->w_size;
        dict_length = s->w_size;
    }

    u32 avail      = s->avail_in;
    const u8 *next = s->next_in;
    s->avail_in    = dict_length;
    s->next_in     = dictionary;
    fill_window(s);
    while (s->lookahead >= u32(MIN_MATCH)) {
        u32 str = s->strstart;
        u32 n   = s->lookahead - (MIN_MATCH - 1);
        do {
            update_hash(s, s->ins_h, s->window[str + MIN_MATCH - 1]);
            s->prev[str & s->w_mask] = s->head[s->ins_h];
            s->head[s->ins_h]        = u16(str);
            str++;
        } while (--n);
        s->strstart  = str;
        s->lookahead = MIN_MATCH - 1;
        fill_window(s);
    }
    s->strstart += s->lookahead;
    s->block_start = i32(s->strstart);
    s->insert      = s->lookahead;
    s->lookahead   = 0;
    s->match_length = s->prev_length = MIN_MATCH - 1;
    s->match_available               = 0;
    s->next_in                       = next;
    s->avail_in                      = avail;
    s->wrap                          = wrap;
    return ZStatus::Ok;
}

usize Deflater::get_dictionary(Span<u8> out) const
{
    if (!s_)
        return 0;
    u32 len = s_->strstart + s_->lookahead;
    if (len > s_->w_size)
        len = s_->w_size;
    usize n = len < out.size() ? len : out.size();
    if (n)
        __builtin_memcpy(out.data(), s_->window + s_->strstart + s_->lookahead - len, n);
    return len;
}

ZStatus Deflater::set_header(ZHeader *head)
{
    if (!s_ || s_->wrap != 2)
        return ZStatus::Misuse;
    s_->gzhead = head;
    return ZStatus::Ok;
}

ZStatus Deflater::pending(u32 *bytes, i32 *bits) const
{
    if (!s_)
        return ZStatus::Misuse;
    if (bits != nullptr)
        *bits = s_->bi_valid;
    if (bytes != nullptr)
        *bytes = s_->pending;
    return ZStatus::Ok;
}

i32 Deflater::used_bits() const
{
    return s_ ? s_->bi_used : 0;
}

ZStatus Deflater::prime(i32 bits, i32 value)
{
    if (!s_)
        return ZStatus::Misuse;
    D *s = s_;
    if (bits < 0 || bits > 16 || s->sym_buf < s->pending_out + ((BUF_SIZE + 7) >> 3))
        return ZStatus::Stuck;
    do {
        int put = BUF_SIZE - s->bi_valid;
        if (put > bits)
            put = bits;
        s->bi_buf |= u16((value & ((1 << put) - 1)) << s->bi_valid);
        s->bi_valid += put;
        tr_flush_bits(s);
        value >>= put;
        bits -= put;
    } while (bits);
    return ZStatus::Ok;
}

ZStatus Deflater::tune(u32 good_length, u32 max_lazy, u32 nice_length, u32 max_chain)
{
    if (!s_)
        return ZStatus::Misuse;
    s_->good_match       = good_length;
    s_->max_lazy_match   = max_lazy;
    s_->nice_match       = int(nice_length);
    s_->max_chain_length = max_chain;
    return ZStatus::Ok;
}

namespace {

// Fixed blocks of 9-bit literals, ~13%, and stored blocks of 127 bytes, ~4%:
// the two worst cases, each saturating.
usize fixed_bound(usize len)
{
    usize b = len + (len >> 3) + (len >> 8) + (len >> 9) + 4;
    return b < len ? usize(-1) : b;
}

usize stored_bound(usize len)
{
    usize b = len + (len >> 5) + (len >> 7) + (len >> 11) + 7;
    return b < len ? usize(-1) : b;
}

} // namespace

usize Deflater::bound_any(usize len)
{
    usize f = fixed_bound(len);
    usize s = stored_bound(len);
    usize b = f > s ? f : s;
    return b + 18 < b ? usize(-1) : b + 18;
}

usize Deflater::bound(usize len) const
{
    if (!s_)
        return bound_any(len);
    D *s = s_;
    usize wraplen;
    switch (s->wrap < 0 ? -s->wrap : s->wrap) {
    case 0:
        wraplen = 0;
        break;
    case 1:
        wraplen = 6 + (s->strstart ? 4 : 0);
        break;
    case 2:
        wraplen = 18;
        if (s->gzhead != nullptr) {
            if (s->gzhead->extra != nullptr)
                wraplen += 2 + s->gzhead->extra_len;
            const u8 *str = s->gzhead->name;
            if (str != nullptr)
                do {
                    wraplen++;
                } while (*str++);
            str = s->gzhead->comment;
            if (str != nullptr)
                do {
                    wraplen++;
                } while (*str++);
            if (s->gzhead->hcrc)
                wraplen += 2;
        }
        break;
    default:
        wraplen = 18;
    }

    if (s->w_bits != 15 || s->hash_bits != 8 + 7) {
        usize b = s->w_bits <= s->hash_bits && s->level ? fixed_bound(len) : stored_bound(len);
        return b + wraplen < b ? usize(-1) : b + wraplen;
    }

    usize b = len + (len >> 12) + (len >> 14) + (len >> 25) + 13 - 6 + wraplen;
    return b < len ? usize(-1) : b;
}

ZStatus Deflater::copy_from(const Deflater &src)
{
    const D *ss = src.s_;
    if (!ss || this == &src)
        return ZStatus::Misuse;

    D *ds = static_cast<D *>(heap_alloc(sizeof(D)));
    if (ds == nullptr)
        return ZStatus::NoMemory;
    __builtin_memcpy(static_cast<void *>(ds), ss, sizeof(D));
    if (!alloc_arrays(ds)) {
        free_state(ds);
        return ZStatus::NoMemory;
    }

    __builtin_memcpy(ds->window, ss->window, ss->high_water);
    __builtin_memcpy(ds->prev, ss->prev,
                     (ss->slid || ss->strstart - ss->insert > ds->w_size
                          ? ds->w_size
                          : ss->strstart - ss->insert) *
                         sizeof(u16));
    __builtin_memcpy(ds->head, ss->head, ds->hash_size * sizeof(u16));

    ds->pending_out = ds->pending_buf + (ss->pending_out - ss->pending_buf);
    __builtin_memcpy(ds->pending_out, ss->pending_out, ss->pending);
    ds->sym_buf = ds->pending_buf + ds->lit_bufsize;
    __builtin_memcpy(ds->sym_buf, ss->sym_buf, ss->sym_next);

    ds->l_desc.dyn_tree  = ds->dyn_ltree;
    ds->d_desc.dyn_tree  = ds->dyn_dtree;
    ds->bl_desc.dyn_tree = ds->bl_tree;

    free_state(s_);
    s_ = ds;
    return ZStatus::Ok;
}

ZStatus Deflater::end()
{
    if (!s_)
        return ZStatus::Misuse;
    int status = s_->status;
    free_state(s_);
    s_ = nullptr;
    return status == BUSY_STATE ? ZStatus::Corrupt : ZStatus::Ok;
}

u64 Deflater::total_in() const
{
    return s_ ? s_->total_in : 0;
}

u64 Deflater::total_out() const
{
    return s_ ? s_->total_out : 0;
}

u32 Deflater::check() const
{
    return s_ ? s_->adler : 0;
}

i32 Deflater::data_type() const
{
    return s_ ? s_->data_type : DT_UNKNOWN;
}

Str Deflater::why() const
{
    if (!s_ || !s_->msg)
        return Str();
    usize n = 0;
    while (s_->msg[n])
        n++;
    return Str(s_->msg, n);
}

int Deflater::level() const
{
    return s_ ? s_->level : 0;
}
