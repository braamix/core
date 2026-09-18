// Inflate, after zlib's inflate.c, inffast.c and inftrees.c (Mark Adler).
// The state machine is theirs, mode for mode, so that a stream stopped
// anywhere resumes where it stopped; the fixed tables are built at compile
// time by the same inflate_table that builds the dynamic ones.
#include "kernel/alloc.h"
#include "zlib/zlib.h"

// A decoding table entry: a literal, a length or distance base with its extra
// bits, a link to a sub-table, the end of a block, or an invalid code.
struct InflateCode {
    u8 op;   // 0 literal, 0000tttt link, 0001eeee base, 01100000 end, 01000000 bad
    u8 bits; // bits this part of the code takes
    u16 val; // literal, base, or offset to the sub-table
};

namespace {

enum Rc {
    RC_OK        = 0,
    RC_END       = 1,
    RC_NEED_DICT = 2,
    RC_STREAM    = -2,
    RC_DATA      = -3,
    RC_MEM       = -4,
    RC_BUF       = -5,
};

enum {
    F_NONE    = 0,
    F_PARTIAL = 1,
    F_SYNC    = 2,
    F_FULL    = 3,
    F_FINISH  = 4,
    F_BLOCK   = 5,
    F_TREES   = 6,
};

constexpr u32 MAXBITS      = 15;
constexpr u32 ENOUGH_LENS  = 852; // "enough 286 9 15"
constexpr u32 ENOUGH_DISTS = 592; // "enough 30 6 15"
constexpr u32 ENOUGH       = ENOUGH_LENS + ENOUGH_DISTS;
constexpr u32 DEFLATED     = 8;

enum CodeType { CODES, LENS, DISTS };

constexpr u16 LBASE[31] = { 3,  4,  5,  6,  7,  8,  9,  10,  11,  13,  15,  17,  19, 23, 27, 31,
                            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0 };
constexpr u16 LEXT[31]  = { 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 18, 18, 18, 18,
                            19, 19, 19, 19, 20, 20, 20, 20, 21, 21, 21, 21, 16, 68, 193 };
constexpr u16 DBASE[32] = { 1,    2,    3,    4,    5,    7,     9,     13,    17,    25,   33,
                            49,   65,   97,   129,  193,  257,   385,   513,   769,   1025, 1537,
                            2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0,     0 };
constexpr u16 DEXT[32]  = { 16, 16, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22,
                            23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 64, 64 };

// Builds the tables for the canonical code whose lengths are lens[0..codes).
// 0 on success, -1 for an invalid code, 1 when ENOUGH is not enough. *table
// is advanced past what was used; *bits is the root table's index bits.
constexpr int inflate_table(CodeType type, const u16 *lens, u32 codes, InflateCode **table,
                            u32 *bits, u16 *work)
{
    u32 len, sym, min, max, root, curr, drop, used, huff, incr, fill, low, mask;
    int left;
    InflateCode here{};
    InflateCode *next;
    const u16 *base  = nullptr;
    const u16 *extra = nullptr;
    u32 match        = 0;
    u16 count[MAXBITS + 1]{};
    u16 offs[MAXBITS + 1]{};

    for (len = 0; len <= MAXBITS; len++)
        count[len] = 0;
    for (sym = 0; sym < codes; sym++)
        count[lens[sym]]++;

    root = *bits;
    for (max = MAXBITS; max >= 1; max--)
        if (count[max] != 0)
            break;
    if (root > max)
        root = max;
    if (max == 0) {
        here.op    = 64;
        here.bits  = 1;
        here.val   = 0;
        *(*table)++ = here;
        *(*table)++ = here;
        *bits       = 1;
        return 0;
    }
    for (min = 1; min < max; min++)
        if (count[min] != 0)
            break;
    if (root < min)
        root = min;

    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= count[len];
        if (left < 0)
            return -1;
    }
    if (left > 0 && (type == CODES || max != 1))
        return -1;

    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++)
        offs[len + 1] = u16(offs[len] + count[len]);

    for (sym = 0; sym < codes; sym++)
        if (lens[sym] != 0)
            work[offs[lens[sym]]++] = u16(sym);

    switch (type) {
    case CODES:
        match = 20;
        break;
    case LENS:
        base  = LBASE;
        extra = LEXT;
        match = 257;
        break;
    case DISTS:
        base  = DBASE;
        extra = DEXT;
    }

    huff = 0;
    sym  = 0;
    len  = min;
    next = *table;
    curr = root;
    drop = 0;
    low  = u32(-1);
    used = 1U << root;
    mask = used - 1;

    if ((type == LENS && used > ENOUGH_LENS) || (type == DISTS && used > ENOUGH_DISTS))
        return 1;

    for (;;) {
        here.bits = u8(len - drop);
        if (work[sym] + 1U < match) {
            here.op  = 0;
            here.val = work[sym];
        } else if (work[sym] >= match) {
            here.op  = u8(extra[work[sym] - match]);
            here.val = base[work[sym] - match];
        } else {
            here.op  = 32 + 64;
            here.val = 0;
        }

        incr = 1U << (len - drop);
        fill = 1U << curr;
        min  = fill;
        do {
            fill -= incr;
            next[(huff >> drop) + fill] = here;
        } while (fill != 0);

        incr = 1U << (len - 1);
        while (huff & incr)
            incr >>= 1;
        if (incr != 0) {
            huff &= incr - 1;
            huff += incr;
        } else
            huff = 0;

        sym++;
        if (--(count[len]) == 0) {
            if (len == max)
                break;
            len = lens[work[sym]];
        }

        if (len > root && (huff & mask) != low) {
            if (drop == 0)
                drop = root;
            next += min;

            curr = len - drop;
            left = int(1 << curr);
            while (curr + drop < max) {
                left -= count[curr + drop];
                if (left <= 0)
                    break;
                curr++;
                left <<= 1;
            }

            used += 1U << curr;
            if ((type == LENS && used > ENOUGH_LENS) || (type == DISTS && used > ENOUGH_DISTS))
                return 1;

            low               = huff & mask;
            (*table)[low].op   = u8(curr);
            (*table)[low].bits = u8(root);
            (*table)[low].val  = u16(next - *table);
        }
    }

    if (huff != 0) {
        here.op    = 64;
        here.bits  = u8(len - drop);
        here.val   = 0;
        next[huff] = here;
    }

    *table += used;
    *bits = root;
    return 0;
}

struct FixedTables {
    InflateCode len[512];
    InflateCode dist[32];
};

constexpr FixedTables make_fixed()
{
    FixedTables f{};
    u16 lens[288]{};
    u16 work[288]{};
    u32 sym = 0;
    while (sym < 144)
        lens[sym++] = 8;
    while (sym < 256)
        lens[sym++] = 9;
    while (sym < 280)
        lens[sym++] = 7;
    while (sym < 288)
        lens[sym++] = 8;
    InflateCode *next = f.len;
    u32 bits          = 9;
    inflate_table(LENS, lens, 288, &next, &bits, work);

    for (sym = 0; sym < 32; sym++)
        lens[sym] = 5;
    next = f.dist;
    bits = 5;
    inflate_table(DISTS, lens, 32, &next, &bits, work);
    return f;
}

constexpr FixedTables FIXED = make_fixed();

constexpr u16 ORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

constexpr u32 swap32(u32 q)
{
    return ((q >> 24) & 0xff) + ((q >> 8) & 0xff00) + ((q & 0xff00) << 8) + ((q & 0xff) << 24);
}

} // namespace

struct InflateState {
    enum Mode : u16 {
        HEAD = 16180, // waiting for magic header
        FLAGS,        // gzip method and flags
        TIME,         // gzip modification time
        OS,           // gzip extra flags and operating system
        EXLEN,        // gzip extra length
        EXTRA,        // gzip extra bytes
        NAME,         // gzip file name
        COMMENT,      // gzip comment
        HCRC,         // gzip header crc
        DICTID,       // dictionary check value
        DICT,         // waiting for set_dictionary()
        TYPE,         // type bits, including last-flag bit
        TYPEDO,       // same, but skip check to exit on a new block
        STORED,       // stored size, length and complement
        COPY_,        // COPY, first time in
        COPY,         // input or output to copy a stored block
        TABLE,        // dynamic block table lengths
        LENLENS,      // code length code lengths
        CODELENS,     // length/literal and distance code lengths
        LEN_,         // LEN, first time in
        LEN,          // length/literal/end-of-block code
        LENEXT,       // length extra bits
        DIST,         // distance code
        DISTEXT,      // distance extra bits
        MATCH,        // output space to copy a string
        LIT,          // output space for a literal
        CHECK,        // 32-bit check value
        LENGTH,       // 32-bit length (gzip)
        DONE,         // finished check; remain here until reset
        BAD,          // data error; remain here until reset
        MEM,          // memory error; remain here until reset
        SYNC,         // looking for synchronisation bytes
    };

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

    Mode mode;
    int last;     // processing the last block
    int wrap;     // bit 0 zlib, bit 1 gzip, bit 2 validate the check
    int havedict; // dictionary provided
    int flags;    // gzip method and flags, 0 if zlib, -1 if raw or no header yet
    u32 dmax;     // zlib header max distance
    u32 check;    // protected copy of the check value
    u32 total;    // protected copy of the output count
    ZHeader *head;
    // sliding window
    u32 wbits;
    u32 wsize;
    u32 whave;
    u32 wnext;
    u8 *window;
    // bit accumulator
    u32 hold;
    u32 bits;
    // string and stored block copying
    u32 length;
    u32 offset;
    u32 extra;
    // code tables
    const InflateCode *lencode;
    const InflateCode *distcode;
    u32 lenbits;
    u32 distbits;
    // dynamic table building
    u32 ncode;
    u32 nlen;
    u32 ndist;
    u32 have;
    InflateCode *next;
    u16 lens[320];
    u16 work[288];
    InflateCode codes[ENOUGH];
    int sane;
    int back; // bits back of last unprocessed length/lit
    u32 was;  // initial length of match
};

namespace {

using S = InflateState;

int reset_keep(S *s)
{
    s->total_in = s->total_out = s->total = 0;
    s->msg                                = nullptr;
    s->data_type                          = 0;
    if (s->wrap)
        s->adler = u32(s->wrap & 1);
    s->mode     = S::HEAD;
    s->last     = 0;
    s->havedict = 0;
    s->flags    = -1;
    s->dmax     = 32768U;
    s->head     = nullptr;
    s->hold     = 0;
    s->bits     = 0;
    s->lencode = s->distcode = s->next = s->codes;
    s->sane                            = 1;
    s->back                            = -1;
    return RC_OK;
}

int reset(S *s)
{
    s->wsize = 0;
    s->whave = 0;
    s->wnext = 0;
    return reset_keep(s);
}

int reset2(S *s, int window_bits)
{
    int wrap;
    if (window_bits < 0) {
        if (window_bits < -15)
            return RC_STREAM;
        wrap        = 0;
        window_bits = -window_bits;
    } else {
        wrap = (window_bits >> 4) + 5;
        if (window_bits < 48)
            window_bits &= 15;
    }

    if (window_bits && (window_bits < 8 || window_bits > 15))
        return RC_STREAM;
    if (s->window != nullptr && s->wbits != u32(window_bits)) {
        heap_free(s->window);
        s->window = nullptr;
    }

    s->wrap  = wrap;
    s->wbits = u32(window_bits);
    return reset(s);
}

// zlib's windowBits for a format and a window.
int window_bits_of(ZFormat f, u8 bits)
{
    switch (f) {
    case ZFormat::Raw:
        return bits ? -int(bits) : 99;
    case ZFormat::Zlib:
        return bits;
    case ZFormat::Gzip:
        return bits + 16;
    case ZFormat::Auto:
        return bits + 32;
    }
    return 99;
}

// Keeps the last wsize bytes written, making the window on first need.
int update_window(S *s, const u8 *end, u32 copy)
{
    if (s->window == nullptr) {
        s->window = static_cast<u8 *>(heap_alloc(1U << s->wbits));
        if (s->window == nullptr)
            return 1;
    }

    if (s->wsize == 0) {
        s->wsize = 1U << s->wbits;
        s->wnext = 0;
        s->whave = 0;
    }

    if (copy >= s->wsize) {
        __builtin_memcpy(s->window, end - s->wsize, s->wsize);
        s->wnext = 0;
        s->whave = s->wsize;
    } else {
        u32 dist = s->wsize - s->wnext;
        if (dist > copy)
            dist = copy;
        __builtin_memcpy(s->window + s->wnext, end - copy, dist);
        copy -= dist;
        if (copy) {
            __builtin_memcpy(s->window, end - copy, copy);
            s->wnext = copy;
            s->whave = s->wsize;
        } else {
            s->wnext += dist;
            if (s->wnext == s->wsize)
                s->wnext = 0;
            if (s->whave < s->wsize)
                s->whave += dist;
        }
    }
    return 0;
}

u32 update_check(S *s, u32 check, const u8 *buf, u32 len)
{
    Bytes b(buf, len);
    return s->flags ? crc32_update(check, b) : adler32_update(check, b);
}

u32 crc_word(u32 check, u32 word, u32 n)
{
    u8 hbuf[4] = { u8(word), u8(word >> 8), u8(word >> 16), u8(word >> 24) };
    return crc32_update(check, Bytes(hbuf, n));
}

// Decodes until the input or the output runs short, a block ends, or the data
// is bad. On entry: mode LEN, avail_in >= 6, avail_out >= 258, bits < 8.
void inflate_fast(S *s, u32 start)
{
    const u8 *in         = s->next_in;
    const u8 *last       = in + (s->avail_in - 5);
    u8 *out              = s->next_out;
    u8 *beg              = out - (start - s->avail_out);
    u8 *end              = out + (s->avail_out - 257);
    u32 wsize            = s->wsize;
    u32 whave            = s->whave;
    u32 wnext            = s->wnext;
    u8 *window           = s->window;
    u32 hold             = s->hold;
    u32 bits             = s->bits;
    const InflateCode *lcode = s->lencode;
    const InflateCode *dcode = s->distcode;
    u32 lmask            = (1U << s->lenbits) - 1;
    u32 dmask            = (1U << s->distbits) - 1;
    const InflateCode *here;
    u32 op, len, dist;
    u8 *from;

    do {
        if (bits < 15) {
            hold += u32(*in++) << bits;
            bits += 8;
            hold += u32(*in++) << bits;
            bits += 8;
        }
        here = lcode + (hold & lmask);
    dolen:
        op = here->bits;
        hold >>= op;
        bits -= op;
        op = here->op;
        if (op == 0) {
            *out++ = u8(here->val);
        } else if (op & 16) {
            len = here->val;
            op &= 15;
            if (op) {
                if (bits < op) {
                    hold += u32(*in++) << bits;
                    bits += 8;
                }
                len += hold & ((1U << op) - 1);
                hold >>= op;
                bits -= op;
            }
            if (bits < 15) {
                hold += u32(*in++) << bits;
                bits += 8;
                hold += u32(*in++) << bits;
                bits += 8;
            }
            here = dcode + (hold & dmask);
        dodist:
            op = here->bits;
            hold >>= op;
            bits -= op;
            op = here->op;
            if (op & 16) {
                dist = here->val;
                op &= 15;
                if (bits < op) {
                    hold += u32(*in++) << bits;
                    bits += 8;
                    if (bits < op) {
                        hold += u32(*in++) << bits;
                        bits += 8;
                    }
                }
                dist += hold & ((1U << op) - 1);
                hold >>= op;
                bits -= op;
                op = u32(out - beg);
                if (dist > op) {
                    op = dist - op;
                    if (op > whave) {
                        if (s->sane) {
                            s->msg  = "invalid distance too far back";
                            s->mode = S::BAD;
                            break;
                        }
                    }
                    from = window;
                    if (wnext == 0) {
                        from += wsize - op;
                        if (op < len) {
                            len -= op;
                            do {
                                *out++ = *from++;
                            } while (--op);
                            from = out - dist;
                        }
                    } else if (wnext < op) {
                        from += wsize + wnext - op;
                        op -= wnext;
                        if (op < len) {
                            len -= op;
                            do {
                                *out++ = *from++;
                            } while (--op);
                            from = window;
                            if (wnext < len) {
                                op = wnext;
                                len -= op;
                                do {
                                    *out++ = *from++;
                                } while (--op);
                                from = out - dist;
                            }
                        }
                    } else {
                        from += wnext - op;
                        if (op < len) {
                            len -= op;
                            do {
                                *out++ = *from++;
                            } while (--op);
                            from = out - dist;
                        }
                    }
                    while (len > 2) {
                        *out++ = *from++;
                        *out++ = *from++;
                        *out++ = *from++;
                        len -= 3;
                    }
                    if (len) {
                        *out++ = *from++;
                        if (len > 1)
                            *out++ = *from++;
                    }
                } else {
                    from = out - dist;
                    do {
                        *out++ = *from++;
                        *out++ = *from++;
                        *out++ = *from++;
                        len -= 3;
                    } while (len > 2);
                    if (len) {
                        *out++ = *from++;
                        if (len > 1)
                            *out++ = *from++;
                    }
                }
            } else if ((op & 64) == 0) {
                here = dcode + here->val + (hold & ((1U << op) - 1));
                goto dodist;
            } else {
                s->msg  = "invalid distance code";
                s->mode = S::BAD;
                break;
            }
        } else if ((op & 64) == 0) {
            here = lcode + here->val + (hold & ((1U << op) - 1));
            goto dolen;
        } else if (op & 32) {
            s->mode = S::TYPE;
            break;
        } else {
            s->msg  = "invalid literal/length code";
            s->mode = S::BAD;
            break;
        }
    } while (in < last && out < end);

    // Return unused bytes; on entry bits < 8, so in cannot go too far back.
    len = bits >> 3;
    in -= len;
    bits -= len << 3;
    hold &= (1U << bits) - 1;

    s->next_in   = in;
    s->next_out  = out;
    s->avail_in  = u32(in < last ? 5 + (last - in) : 5 - (in - last));
    s->avail_out = u32(out < end ? 257 + (end - out) : 257 - (out - end));
    s->hold      = hold;
    s->bits      = bits;
}

// The state machine: zlib's inflate(), on zlib's return codes.
int inflate_run(S *s, int flush)
{
    const u8 *next;
    u8 *put;
    u32 have, left;
    u32 hold;
    u32 bits;
    u32 in, out;
    u32 copy;
    u8 *from;
    InflateCode here;
    InflateCode last;
    u32 len;
    int ret;

#define LOAD()                \
    do {                      \
        put  = s->next_out;   \
        left = s->avail_out;  \
        next = s->next_in;    \
        have = s->avail_in;   \
        hold = s->hold;       \
        bits = s->bits;       \
    } while (0)

#define RESTORE()             \
    do {                      \
        s->next_out  = put;   \
        s->avail_out = left;  \
        s->next_in   = next;  \
        s->avail_in  = have;  \
        s->hold      = hold;  \
        s->bits      = bits;  \
    } while (0)

#define INITBITS() \
    do {           \
        hold = 0;  \
        bits = 0;  \
    } while (0)

#define PULLBYTE()                     \
    do {                               \
        if (have == 0)                 \
            goto inf_leave;            \
        have--;                        \
        hold += u32(*next++) << bits;  \
        bits += 8;                     \
    } while (0)

#define NEEDBITS(n)              \
    do {                         \
        while (bits < u32(n))    \
            PULLBYTE();          \
    } while (0)

#define BITS(n) (hold & ((1U << (n)) - 1))

#define DROPBITS(n)          \
    do {                     \
        hold >>= (n);        \
        bits -= u32(n);      \
    } while (0)

#define BYTEBITS()           \
    do {                     \
        hold >>= bits & 7;   \
        bits -= bits & 7;    \
    } while (0)

    if (s->mode == S::TYPE)
        s->mode = S::TYPEDO;
    LOAD();
    in  = have;
    out = left;
    ret = RC_OK;
    for (;;)
        switch (s->mode) {
        case S::HEAD:
            if (s->wrap == 0) {
                s->mode = S::TYPEDO;
                break;
            }
            NEEDBITS(16);
            if ((s->wrap & 2) && hold == 0x8b1f) {
                if (s->wbits == 0)
                    s->wbits = 15;
                s->check = crc32_update(0, Bytes());
                s->check = crc_word(s->check, hold, 2);
                INITBITS();
                s->mode = S::FLAGS;
                break;
            }
            if (s->head != nullptr)
                s->head->done = -1;
            if (!(s->wrap & 1) || ((BITS(8) << 8) + (hold >> 8)) % 31) {
                s->msg  = "incorrect header check";
                s->mode = S::BAD;
                break;
            }
            if (BITS(4) != DEFLATED) {
                s->msg  = "unknown compression method";
                s->mode = S::BAD;
                break;
            }
            DROPBITS(4);
            len = BITS(4) + 8;
            if (s->wbits == 0)
                s->wbits = len;
            if (len > 15 || len > s->wbits) {
                s->msg  = "invalid window size";
                s->mode = S::BAD;
                break;
            }
            s->dmax  = 1U << len;
            s->flags = 0;
            s->adler = s->check = adler32_update(1, Bytes());
            s->mode             = hold & 0x200 ? S::DICTID : S::TYPE;
            INITBITS();
            break;
        case S::FLAGS:
            NEEDBITS(16);
            s->flags = int(hold);
            if ((s->flags & 0xff) != int(DEFLATED)) {
                s->msg  = "unknown compression method";
                s->mode = S::BAD;
                break;
            }
            if (s->flags & 0xe000) {
                s->msg  = "unknown header flags set";
                s->mode = S::BAD;
                break;
            }
            if (s->head != nullptr)
                s->head->text = int((hold >> 8) & 1);
            if ((s->flags & 0x0200) && (s->wrap & 4))
                s->check = crc_word(s->check, hold, 2);
            INITBITS();
            s->mode = S::TIME;
            [[fallthrough]];
        case S::TIME:
            NEEDBITS(32);
            if (s->head != nullptr)
                s->head->time = hold;
            if ((s->flags & 0x0200) && (s->wrap & 4))
                s->check = crc_word(s->check, hold, 4);
            INITBITS();
            s->mode = S::OS;
            [[fallthrough]];
        case S::OS:
            NEEDBITS(16);
            if (s->head != nullptr) {
                s->head->xflags = int(hold & 0xff);
                s->head->os     = int(hold >> 8);
            }
            if ((s->flags & 0x0200) && (s->wrap & 4))
                s->check = crc_word(s->check, hold, 2);
            INITBITS();
            s->mode = S::EXLEN;
            [[fallthrough]];
        case S::EXLEN:
            if (s->flags & 0x0400) {
                NEEDBITS(16);
                s->length = hold;
                if (s->head != nullptr)
                    s->head->extra_len = hold;
                if ((s->flags & 0x0200) && (s->wrap & 4))
                    s->check = crc_word(s->check, hold, 2);
                INITBITS();
            } else if (s->head != nullptr)
                s->head->extra = nullptr;
            s->mode = S::EXTRA;
            [[fallthrough]];
        case S::EXTRA:
            if (s->flags & 0x0400) {
                copy = s->length;
                if (copy > have)
                    copy = have;
                if (copy) {
                    if (s->head != nullptr && s->head->extra != nullptr &&
                        (len = s->head->extra_len - s->length) < s->head->extra_max) {
                        __builtin_memcpy(s->head->extra + len, next,
                                         len + copy > s->head->extra_max ? s->head->extra_max - len
                                                                         : copy);
                    }
                    if ((s->flags & 0x0200) && (s->wrap & 4))
                        s->check = crc32_update(s->check, Bytes(next, copy));
                    have -= copy;
                    next += copy;
                    s->length -= copy;
                }
                if (s->length)
                    goto inf_leave;
            }
            s->length = 0;
            s->mode   = S::NAME;
            [[fallthrough]];
        case S::NAME:
            if (s->flags & 0x0800) {
                if (have == 0)
                    goto inf_leave;
                copy = 0;
                do {
                    len = next[copy++];
                    if (s->head != nullptr && s->head->name != nullptr &&
                        s->length < s->head->name_max)
                        s->head->name[s->length++] = u8(len);
                } while (len && copy < have);
                if ((s->flags & 0x0200) && (s->wrap & 4))
                    s->check = crc32_update(s->check, Bytes(next, copy));
                have -= copy;
                next += copy;
                if (len)
                    goto inf_leave;
            } else if (s->head != nullptr)
                s->head->name = nullptr;
            s->length = 0;
            s->mode   = S::COMMENT;
            [[fallthrough]];
        case S::COMMENT:
            if (s->flags & 0x1000) {
                if (have == 0)
                    goto inf_leave;
                copy = 0;
                do {
                    len = next[copy++];
                    if (s->head != nullptr && s->head->comment != nullptr &&
                        s->length < s->head->comm_max)
                        s->head->comment[s->length++] = u8(len);
                } while (len && copy < have);
                if ((s->flags & 0x0200) && (s->wrap & 4))
                    s->check = crc32_update(s->check, Bytes(next, copy));
                have -= copy;
                next += copy;
                if (len)
                    goto inf_leave;
            } else if (s->head != nullptr)
                s->head->comment = nullptr;
            s->mode = S::HCRC;
            [[fallthrough]];
        case S::HCRC:
            if (s->flags & 0x0200) {
                NEEDBITS(16);
                if ((s->wrap & 4) && hold != (s->check & 0xffff)) {
                    s->msg  = "header crc mismatch";
                    s->mode = S::BAD;
                    break;
                }
                INITBITS();
            }
            if (s->head != nullptr) {
                s->head->hcrc = int((s->flags >> 9) & 1);
                s->head->done = 1;
            }
            s->adler = s->check = crc32_update(0, Bytes());
            s->mode             = S::TYPE;
            break;
        case S::DICTID:
            NEEDBITS(32);
            s->adler = s->check = swap32(hold);
            INITBITS();
            s->mode = S::DICT;
            [[fallthrough]];
        case S::DICT:
            if (s->havedict == 0) {
                RESTORE();
                return RC_NEED_DICT;
            }
            s->adler = s->check = adler32_update(1, Bytes());
            s->mode             = S::TYPE;
            [[fallthrough]];
        case S::TYPE:
            if (flush == F_BLOCK || flush == F_TREES)
                goto inf_leave;
            [[fallthrough]];
        case S::TYPEDO:
            if (s->last) {
                BYTEBITS();
                s->mode = S::CHECK;
                break;
            }
            NEEDBITS(3);
            s->last = int(BITS(1));
            DROPBITS(1);
            switch (BITS(2)) {
            case 0:
                s->mode = S::STORED;
                break;
            case 1:
                s->lencode  = FIXED.len;
                s->lenbits  = 9;
                s->distcode = FIXED.dist;
                s->distbits = 5;
                s->mode     = S::LEN_;
                if (flush == F_TREES) {
                    DROPBITS(2);
                    goto inf_leave;
                }
                break;
            case 2:
                s->mode = S::TABLE;
                break;
            default:
                s->msg  = "invalid block type";
                s->mode = S::BAD;
            }
            DROPBITS(2);
            break;
        case S::STORED:
            BYTEBITS();
            NEEDBITS(32);
            if ((hold & 0xffff) != ((hold >> 16) ^ 0xffff)) {
                s->msg  = "invalid stored block lengths";
                s->mode = S::BAD;
                break;
            }
            s->length = hold & 0xffff;
            INITBITS();
            s->mode = S::COPY_;
            if (flush == F_TREES)
                goto inf_leave;
            [[fallthrough]];
        case S::COPY_:
            s->mode = S::COPY;
            [[fallthrough]];
        case S::COPY:
            copy = s->length;
            if (copy) {
                if (copy > have)
                    copy = have;
                if (copy > left)
                    copy = left;
                if (copy == 0)
                    goto inf_leave;
                __builtin_memcpy(put, next, copy);
                have -= copy;
                next += copy;
                left -= copy;
                put += copy;
                s->length -= copy;
                break;
            }
            s->mode = S::TYPE;
            break;
        case S::TABLE:
            NEEDBITS(14);
            s->nlen = BITS(5) + 257;
            DROPBITS(5);
            s->ndist = BITS(5) + 1;
            DROPBITS(5);
            s->ncode = BITS(4) + 4;
            DROPBITS(4);
            if (s->nlen > 286 || s->ndist > 30) {
                s->msg  = "too many length or distance symbols";
                s->mode = S::BAD;
                break;
            }
            s->have = 0;
            s->mode = S::LENLENS;
            [[fallthrough]];
        case S::LENLENS:
            while (s->have < s->ncode) {
                NEEDBITS(3);
                s->lens[ORDER[s->have++]] = u16(BITS(3));
                DROPBITS(3);
            }
            while (s->have < 19)
                s->lens[ORDER[s->have++]] = 0;
            s->next    = s->codes;
            s->lencode = s->distcode = s->next;
            s->lenbits               = 7;
            ret = inflate_table(CODES, s->lens, 19, &s->next, &s->lenbits, s->work);
            if (ret) {
                s->msg  = "invalid code lengths set";
                s->mode = S::BAD;
                break;
            }
            s->have = 0;
            s->mode = S::CODELENS;
            [[fallthrough]];
        case S::CODELENS:
            while (s->have < s->nlen + s->ndist) {
                for (;;) {
                    here = s->lencode[BITS(s->lenbits)];
                    if (u32(here.bits) <= bits)
                        break;
                    PULLBYTE();
                }
                if (here.val < 16) {
                    DROPBITS(here.bits);
                    s->lens[s->have++] = here.val;
                } else {
                    if (here.val == 16) {
                        NEEDBITS(here.bits + 2);
                        DROPBITS(here.bits);
                        if (s->have == 0) {
                            s->msg  = "invalid bit length repeat";
                            s->mode = S::BAD;
                            break;
                        }
                        len  = s->lens[s->have - 1];
                        copy = 3 + BITS(2);
                        DROPBITS(2);
                    } else if (here.val == 17) {
                        NEEDBITS(here.bits + 3);
                        DROPBITS(here.bits);
                        len  = 0;
                        copy = 3 + BITS(3);
                        DROPBITS(3);
                    } else {
                        NEEDBITS(here.bits + 7);
                        DROPBITS(here.bits);
                        len  = 0;
                        copy = 11 + BITS(7);
                        DROPBITS(7);
                    }
                    if (s->have + copy > s->nlen + s->ndist) {
                        s->msg  = "invalid bit length repeat";
                        s->mode = S::BAD;
                        break;
                    }
                    while (copy--)
                        s->lens[s->have++] = u16(len);
                }
            }

            if (s->mode == S::BAD)
                break;

            if (s->lens[256] == 0) {
                s->msg  = "invalid code -- missing end-of-block";
                s->mode = S::BAD;
                break;
            }

            // 9 and 6 are what ENOUGH_LENS and ENOUGH_DISTS were found for.
            s->next    = s->codes;
            s->lencode = s->next;
            s->lenbits = 9;
            ret        = inflate_table(LENS, s->lens, s->nlen, &s->next, &s->lenbits, s->work);
            if (ret) {
                s->msg  = "invalid literal/lengths set";
                s->mode = S::BAD;
                break;
            }
            s->distcode = s->next;
            s->distbits = 6;
            ret = inflate_table(DISTS, s->lens + s->nlen, s->ndist, &s->next, &s->distbits,
                                s->work);
            if (ret) {
                s->msg  = "invalid distances set";
                s->mode = S::BAD;
                break;
            }
            s->mode = S::LEN_;
            if (flush == F_TREES)
                goto inf_leave;
            [[fallthrough]];
        case S::LEN_:
            s->mode = S::LEN;
            [[fallthrough]];
        case S::LEN:
            if (have >= 6 && left >= 258) {
                RESTORE();
                inflate_fast(s, out);
                LOAD();
                if (s->mode == S::TYPE)
                    s->back = -1;
                break;
            }
            s->back = 0;
            for (;;) {
                here = s->lencode[BITS(s->lenbits)];
                if (u32(here.bits) <= bits)
                    break;
                PULLBYTE();
            }
            if (here.op && (here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = s->lencode[last.val + (BITS(last.bits + last.op) >> last.bits)];
                    if (u32(last.bits + here.bits) <= bits)
                        break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                s->back += last.bits;
            }
            DROPBITS(here.bits);
            s->back += here.bits;
            s->length = here.val;
            if (here.op == 0) {
                s->mode = S::LIT;
                break;
            }
            if (here.op & 32) {
                s->back = -1;
                s->mode = S::TYPE;
                break;
            }
            if (here.op & 64) {
                s->msg  = "invalid literal/length code";
                s->mode = S::BAD;
                break;
            }
            s->extra = u32(here.op) & 15;
            s->mode  = S::LENEXT;
            [[fallthrough]];
        case S::LENEXT:
            if (s->extra) {
                NEEDBITS(s->extra);
                s->length += BITS(s->extra);
                DROPBITS(s->extra);
                s->back += int(s->extra);
            }
            s->was  = s->length;
            s->mode = S::DIST;
            [[fallthrough]];
        case S::DIST:
            for (;;) {
                here = s->distcode[BITS(s->distbits)];
                if (u32(here.bits) <= bits)
                    break;
                PULLBYTE();
            }
            if ((here.op & 0xf0) == 0) {
                last = here;
                for (;;) {
                    here = s->distcode[last.val + (BITS(last.bits + last.op) >> last.bits)];
                    if (u32(last.bits + here.bits) <= bits)
                        break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                s->back += last.bits;
            }
            DROPBITS(here.bits);
            s->back += here.bits;
            if (here.op & 64) {
                s->msg  = "invalid distance code";
                s->mode = S::BAD;
                break;
            }
            s->offset = here.val;
            s->extra  = u32(here.op) & 15;
            s->mode   = S::DISTEXT;
            [[fallthrough]];
        case S::DISTEXT:
            if (s->extra) {
                NEEDBITS(s->extra);
                s->offset += BITS(s->extra);
                DROPBITS(s->extra);
                s->back += int(s->extra);
            }
            s->mode = S::MATCH;
            [[fallthrough]];
        case S::MATCH:
            if (left == 0)
                goto inf_leave;
            copy = out - left;
            if (s->offset > copy) {
                copy = s->offset - copy;
                if (copy > s->whave) {
                    if (s->sane) {
                        s->msg  = "invalid distance too far back";
                        s->mode = S::BAD;
                        break;
                    }
                }
                if (copy > s->wnext) {
                    copy -= s->wnext;
                    from = s->window + (s->wsize - copy);
                } else
                    from = s->window + (s->wnext - copy);
                if (copy > s->length)
                    copy = s->length;
            } else {
                from = put - s->offset;
                copy = s->length;
            }
            if (copy > left)
                copy = left;
            left -= copy;
            s->length -= copy;
            do {
                *put++ = *from++;
            } while (--copy);
            if (s->length == 0)
                s->mode = S::LEN;
            break;
        case S::LIT:
            if (left == 0)
                goto inf_leave;
            *put++ = u8(s->length);
            left--;
            s->mode = S::LEN;
            break;
        case S::CHECK:
            if (s->wrap) {
                NEEDBITS(32);
                out -= left;
                s->total_out += out;
                s->total += out;
                if ((s->wrap & 4) && out)
                    s->adler = s->check = update_check(s, s->check, put - out, out);
                out = left;
                if ((s->wrap & 4) && (s->flags ? hold : swap32(hold)) != s->check) {
                    s->msg  = "incorrect data check";
                    s->mode = S::BAD;
                    break;
                }
                INITBITS();
            }
            s->mode = S::LENGTH;
            [[fallthrough]];
        case S::LENGTH:
            if (s->wrap && s->flags) {
                NEEDBITS(32);
                if ((s->wrap & 4) && hold != s->total) {
                    s->msg  = "incorrect length check";
                    s->mode = S::BAD;
                    break;
                }
                INITBITS();
            }
            s->mode = S::DONE;
            [[fallthrough]];
        case S::DONE:
            ret = RC_END;
            goto inf_leave;
        case S::BAD:
            ret = RC_DATA;
            goto inf_leave;
        case S::MEM:
            return RC_MEM;
        case S::SYNC:
        default:
            return RC_STREAM;
        }

inf_leave:
    RESTORE();
    if (s->wsize || (out != s->avail_out && s->mode < S::BAD &&
                     (s->mode < S::CHECK || flush != F_FINISH)))
        if (update_window(s, s->next_out, out - s->avail_out)) {
            s->mode = S::MEM;
            return RC_MEM;
        }
    in -= s->avail_in;
    out -= s->avail_out;
    s->total_in += in;
    s->total_out += out;
    s->total += out;
    if ((s->wrap & 4) && out)
        s->adler = s->check = update_check(s, s->check, s->next_out - out, out);
    s->data_type = int(s->bits) + (s->last ? 64 : 0) + (s->mode == S::TYPE ? 128 : 0) +
                   (s->mode == S::LEN_ || s->mode == S::COPY_ ? 256 : 0);
    if (((in == 0 && out == 0) || flush == F_FINISH) && ret == RC_OK)
        ret = RC_BUF;
    return ret;

#undef LOAD
#undef RESTORE
#undef INITBITS
#undef PULLBYTE
#undef NEEDBITS
#undef BITS
#undef DROPBITS
#undef BYTEBITS
}

// Searches buf for 0, 0, 0xff, 0xff; *have is how much of it was already
// found. Returns the bytes looked at.
u32 sync_search(u32 *have, const u8 *buf, u32 len)
{
    u32 got  = *have;
    u32 next = 0;
    while (next < len && got < 4) {
        if (int(buf[next]) == (got < 2 ? 0 : 0xff))
            got++;
        else if (buf[next])
            got = 0;
        else
            got = 4 - got;
        next++;
    }
    *have = got;
    return next;
}

ZStatus status_of(int rc)
{
    switch (rc) {
    case RC_OK:
        return ZStatus::Ok;
    case RC_END:
        return ZStatus::End;
    case RC_NEED_DICT:
        return ZStatus::NeedDict;
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

void free_state(S *s)
{
    if (s) {
        heap_free(s->window);
        heap_free(s);
    }
}

} // namespace

Inflater &Inflater::operator=(Inflater &&o) noexcept
{
    if (this != &o) {
        free_state(s_);
        s_   = o.s_;
        o.s_ = nullptr;
    }
    return *this;
}

Inflater::~Inflater()
{
    free_state(s_);
}

Result<void> Inflater::init(ZFormat format, u8 window_bits)
{
    free_state(s_);
    s_ = static_cast<S *>(heap_alloc(sizeof(S)));
    if (s_ == nullptr)
        return Err(Error::NoMemory);
    __builtin_memset(static_cast<void *>(s_), 0, sizeof(S));
    s_->window = nullptr;
    s_->mode   = S::HEAD;
    if (reset2(s_, window_bits_of(format, window_bits)) != RC_OK) {
        heap_free(s_);
        s_ = nullptr;
        return Err(Error::Invalid);
    }
    return {};
}

ZStatus Inflater::step(Span<const u8> &in, Span<u8> &out, ZFlush flush)
{
    if (!s_)
        return ZStatus::Misuse;
    s_->next_in   = in.data();
    s_->avail_in  = in.size();
    s_->next_out  = out.data();
    s_->avail_out = out.size();
    int rc        = inflate_run(s_, int(flush));
    in            = Span<const u8>(s_->next_in, s_->avail_in);
    out           = Span<u8>(s_->next_out, s_->avail_out);
    return status_of(rc);
}

ZStatus Inflater::reset()
{
    if (!s_)
        return ZStatus::Misuse;
    return status_of(::reset(s_));
}

ZStatus Inflater::reset(ZFormat format, u8 window_bits)
{
    if (!s_)
        return ZStatus::Misuse;
    return status_of(reset2(s_, window_bits_of(format, window_bits)));
}

ZStatus Inflater::set_dictionary(Bytes dict)
{
    if (!s_)
        return ZStatus::Misuse;
    if (s_->wrap != 0 && s_->mode != S::DICT)
        return ZStatus::Misuse;

    if (s_->mode == S::DICT) {
        u32 dictid = adler32_update(1, dict);
        if (dictid != s_->check)
            return ZStatus::Corrupt;
    }

    if (update_window(s_, dict.data() + dict.size(), dict.size())) {
        s_->mode = S::MEM;
        return ZStatus::NoMemory;
    }
    s_->havedict = 1;
    return ZStatus::Ok;
}

usize Inflater::get_dictionary(Span<u8> out) const
{
    if (!s_)
        return 0;
    if (s_->whave && !out.empty()) {
        u8 *tmp = out.data();
        usize n = out.size();
        // The window is circular: its oldest byte is at wnext.
        usize a = s_->whave - s_->wnext;
        usize k = a < n ? a : n;
        __builtin_memcpy(tmp, s_->window + s_->wnext, k);
        if (n > a) {
            usize b = s_->wnext < n - a ? s_->wnext : n - a;
            __builtin_memcpy(tmp + a, s_->window, b);
        }
    }
    return s_->whave;
}

ZStatus Inflater::set_header(ZHeader *head)
{
    if (!s_ || (s_->wrap & 2) == 0)
        return ZStatus::Misuse;
    s_->head   = head;
    head->done = 0;
    return ZStatus::Ok;
}

ZStatus Inflater::sync(Span<const u8> &in)
{
    if (!s_)
        return ZStatus::Misuse;
    if (in.empty() && s_->bits < 8)
        return ZStatus::Stuck;

    if (s_->mode != S::SYNC) {
        u8 buf[4];
        u32 len  = 0;
        s_->mode = S::SYNC;
        s_->hold >>= s_->bits & 7;
        s_->bits -= s_->bits & 7;
        while (s_->bits >= 8) {
            buf[len++] = u8(s_->hold);
            s_->hold >>= 8;
            s_->bits -= 8;
        }
        s_->have = 0;
        sync_search(&s_->have, buf, len);
    }

    u32 len = sync_search(&s_->have, in.data(), in.size());
    in      = in.subspan(len);
    s_->total_in += len;

    if (s_->have != 4)
        return ZStatus::Corrupt;
    if (s_->flags == -1)
        s_->wrap = 0;
    else
        s_->wrap &= ~4;
    int flags = s_->flags;
    u64 tin   = s_->total_in;
    u64 tout  = s_->total_out;
    ::reset(s_);
    s_->total_in  = tin;
    s_->total_out = tout;
    s_->flags     = flags;
    s_->mode      = S::TYPE;
    return ZStatus::Ok;
}

bool Inflater::sync_point() const
{
    return s_ && s_->mode == S::STORED && s_->bits == 0;
}

ZStatus Inflater::prime(i32 bits, i32 value)
{
    if (!s_)
        return ZStatus::Misuse;
    if (bits == 0)
        return ZStatus::Ok;
    if (bits < 0) {
        s_->hold = 0;
        s_->bits = 0;
        return ZStatus::Ok;
    }
    if (bits > 16 || s_->bits + u32(bits) > 32)
        return ZStatus::Misuse;
    value &= (1 << bits) - 1;
    s_->hold += u32(value) << s_->bits;
    s_->bits += u32(bits);
    return ZStatus::Ok;
}

ZStatus Inflater::validate(bool check)
{
    if (!s_)
        return ZStatus::Misuse;
    if (check && s_->wrap)
        s_->wrap |= 4;
    else
        s_->wrap &= ~4;
    return ZStatus::Ok;
}

i32 Inflater::mark() const
{
    if (!s_)
        return -(1 << 16);
    return i32(u32(s_->back) << 16) +
           i32(s_->mode == S::COPY ? s_->length
                                   : (s_->mode == S::MATCH ? s_->was - s_->length : 0));
}

u32 Inflater::codes_used() const
{
    if (!s_)
        return u32(-1);
    return u32(s_->next - s_->codes);
}

ZStatus Inflater::copy_from(const Inflater &src)
{
    const S *from = src.s_;
    if (!from || this == &src)
        return ZStatus::Misuse;

    S *copy = static_cast<S *>(heap_alloc(sizeof(S)));
    if (copy == nullptr)
        return ZStatus::NoMemory;
    u8 *window = nullptr;
    if (from->window != nullptr) {
        window = static_cast<u8 *>(heap_alloc(1U << from->wbits));
        if (window == nullptr) {
            heap_free(copy);
            return ZStatus::NoMemory;
        }
    }

    __builtin_memcpy(static_cast<void *>(copy), from, sizeof(S));
    if (from->lencode >= from->codes && from->lencode <= from->codes + ENOUGH - 1) {
        copy->lencode  = copy->codes + (from->lencode - from->codes);
        copy->distcode = copy->codes + (from->distcode - from->codes);
    }
    copy->next = copy->codes + (from->next - from->codes);
    if (window != nullptr)
        __builtin_memcpy(window, from->window, from->whave);
    copy->window = window;
    free_state(s_);
    s_ = copy;
    return ZStatus::Ok;
}

u64 Inflater::total_in() const
{
    return s_ ? s_->total_in : 0;
}

u64 Inflater::total_out() const
{
    return s_ ? s_->total_out : 0;
}

u32 Inflater::check() const
{
    return s_ ? s_->adler : 0;
}

i32 Inflater::data_type() const
{
    return s_ ? s_->data_type : 0;
}

Str Inflater::why() const
{
    if (!s_ || !s_->msg)
        return Str();
    usize n = 0;
    while (s_->msg[n])
        n++;
    return Str(s_->msg, n);
}
