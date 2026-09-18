// The Burrows-Wheeler block sort, after blocksort.c: a three-way radix
// quicksort, and for blocks too repetitive for it, a doubling sort after
// Manber and Myers. Either gives the same order.
#include "bzip2/bzlib_private.h"

namespace {

using S = BzEncodeState;

// ---------------------------------------------------- the fallback sort

void fallback_simple_sort(u32 *fmap, const u32 *eclass, i32 lo, i32 hi)
{
    if (lo == hi)
        return;

    if (hi - lo > 3) {
        for (i32 i = hi - 4; i >= lo; i--) {
            u32 tmp    = fmap[i];
            u32 ec_tmp = eclass[tmp];
            i32 j;
            for (j = i + 4; j <= hi && ec_tmp > eclass[fmap[j]]; j += 4)
                fmap[j - 4] = fmap[j];
            fmap[j - 4] = tmp;
        }
    }

    for (i32 i = hi - 1; i >= lo; i--) {
        u32 tmp    = fmap[i];
        u32 ec_tmp = eclass[tmp];
        i32 j;
        for (j = i + 1; j <= hi && ec_tmp > eclass[fmap[j]]; j++)
            fmap[j - 1] = fmap[j];
        fmap[j - 1] = tmp;
    }
}

void swap_u32(u32 &a, u32 &b)
{
    u32 t = a;
    a     = b;
    b     = t;
}

void vswap(u32 *v, i32 p1, i32 p2, i32 n)
{
    while (n > 0) {
        swap_u32(v[p1], v[p2]);
        p1++;
        p2++;
        n--;
    }
}

constexpr i32 min_i32(i32 a, i32 b)
{
    return a < b ? a : b;
}

constexpr i32 FALLBACK_QSORT_SMALL_THRESH = 10;
constexpr i32 FALLBACK_QSORT_STACK_SIZE   = 100;

void fallback_qsort3(S *s, u32 *fmap, const u32 *eclass, i32 lo_st, i32 hi_st)
{
    i32 stack_lo[FALLBACK_QSORT_STACK_SIZE];
    i32 stack_hi[FALLBACK_QSORT_STACK_SIZE];
    u32 r  = 0;
    i32 sp = 0;

    stack_lo[sp] = lo_st;
    stack_hi[sp] = hi_st;
    sp++;

    while (sp > 0) {
        if (sp >= FALLBACK_QSORT_STACK_SIZE - 1) {
            s->bug = true;
            return;
        }

        sp--;
        i32 lo = stack_lo[sp];
        i32 hi = stack_hi[sp];
        if (hi - lo < FALLBACK_QSORT_SMALL_THRESH) {
            fallback_simple_sort(fmap, eclass, lo, hi);
            continue;
        }

        // Random partitioning, with Sedgewick's constants.
        r      = ((r * 7621) + 1) % 32768;
        u32 r3 = r % 3;
        u32 med;
        if (r3 == 0)
            med = eclass[fmap[lo]];
        else if (r3 == 1)
            med = eclass[fmap[(lo + hi) >> 1]];
        else
            med = eclass[fmap[hi]];

        i32 un_lo = lo, lt_lo = lo;
        i32 un_hi = hi, gt_hi = hi;

        for (;;) {
            for (;;) {
                if (un_lo > un_hi)
                    break;
                i32 n = i32(eclass[fmap[un_lo]]) - i32(med);
                if (n == 0) {
                    swap_u32(fmap[un_lo], fmap[lt_lo]);
                    lt_lo++;
                    un_lo++;
                    continue;
                }
                if (n > 0)
                    break;
                un_lo++;
            }
            for (;;) {
                if (un_lo > un_hi)
                    break;
                i32 n = i32(eclass[fmap[un_hi]]) - i32(med);
                if (n == 0) {
                    swap_u32(fmap[un_hi], fmap[gt_hi]);
                    gt_hi--;
                    un_hi--;
                    continue;
                }
                if (n < 0)
                    break;
                un_hi--;
            }
            if (un_lo > un_hi)
                break;
            swap_u32(fmap[un_lo], fmap[un_hi]);
            un_lo++;
            un_hi--;
        }

        if (gt_hi < lt_lo)
            continue;

        i32 n = min_i32(lt_lo - lo, un_lo - lt_lo);
        vswap(fmap, lo, un_lo - n, n);
        i32 m = min_i32(hi - gt_hi, gt_hi - un_hi);
        vswap(fmap, un_lo, hi - m + 1, m);

        n = lo + un_lo - lt_lo - 1;
        m = hi - (gt_hi - un_hi) + 1;

        if (n - lo > hi - m) {
            stack_lo[sp] = lo;
            stack_hi[sp] = n;
            sp++;
            stack_lo[sp] = m;
            stack_hi[sp] = hi;
            sp++;
        } else {
            stack_lo[sp] = m;
            stack_hi[sp] = hi;
            sp++;
            stack_lo[sp] = lo;
            stack_hi[sp] = n;
            sp++;
        }
    }
}

// The bucket-header bits.
inline void set_bh(u32 *bhtab, i32 zz)
{
    bhtab[zz >> 5] |= u32(1) << (zz & 31);
}

inline void clear_bh(u32 *bhtab, i32 zz)
{
    bhtab[zz >> 5] &= ~(u32(1) << (zz & 31));
}

inline bool isset_bh(const u32 *bhtab, i32 zz)
{
    return bhtab[zz >> 5] & (u32(1) << (zz & 31));
}

inline u32 word_bh(const u32 *bhtab, i32 zz)
{
    return bhtab[zz >> 5];
}

inline bool unaligned_bh(i32 zz)
{
    return zz & 0x1f;
}

// On entry the block is in eclass's bytes [0, nblock); on return it is
// there again, and fmap holds the sorted order. bhtab is destroyed.
void fallback_sort(S *s, u32 *fmap, u32 *eclass, u32 *bhtab, i32 nblock)
{
    i32 ftab[257];
    i32 ftab_copy[256];
    u8 *eclass8 = reinterpret_cast<u8 *>(eclass);

    // A one-byte radix sort for the first fmap and the first header bits.
    for (i32 i = 0; i < 257; i++)
        ftab[i] = 0;
    for (i32 i = 0; i < nblock; i++)
        ftab[eclass8[i]]++;
    for (i32 i = 0; i < 256; i++)
        ftab_copy[i] = ftab[i];
    for (i32 i = 1; i < 257; i++)
        ftab[i] += ftab[i - 1];

    for (i32 i = 0; i < nblock; i++) {
        i32 j   = eclass8[i];
        i32 k   = ftab[j] - 1;
        ftab[j] = k;
        fmap[k] = u32(i);
    }

    i32 n_bhtab = 2 + (nblock / 32);
    for (i32 i = 0; i < n_bhtab; i++)
        bhtab[i] = 0;
    for (i32 i = 0; i < 256; i++)
        set_bh(bhtab, ftab[i]);

    // Sentinel bits past the end of the block.
    for (i32 i = 0; i < 32; i++) {
        set_bh(bhtab, nblock + 2 * i);
        clear_bh(bhtab, nblock + 2 * i + 1);
    }

    // Refine the buckets, doubling the sorted depth each time.
    i32 h = 1;
    for (;;) {
        i32 j = 0;
        for (i32 i = 0; i < nblock; i++) {
            if (isset_bh(bhtab, i))
                j = i;
            i32 k = i32(fmap[i]) - h;
            if (k < 0)
                k += nblock;
            eclass[k] = u32(j);
        }

        i32 n_not_done = 0;
        i32 r          = -1;
        for (;;) {
            // The next bucket of more than one.
            i32 k = r + 1;
            while (isset_bh(bhtab, k) && unaligned_bh(k))
                k++;
            if (isset_bh(bhtab, k)) {
                while (word_bh(bhtab, k) == 0xffffffff)
                    k += 32;
                while (isset_bh(bhtab, k))
                    k++;
            }
            i32 l = k - 1;
            if (l >= nblock)
                break;
            while (!isset_bh(bhtab, k) && unaligned_bh(k))
                k++;
            if (!isset_bh(bhtab, k)) {
                while (word_bh(bhtab, k) == 0x00000000)
                    k += 32;
                while (!isset_bh(bhtab, k))
                    k++;
            }
            r = k - 1;
            if (r >= nblock)
                break;

            // [l, r] is the bucket.
            if (r > l) {
                n_not_done += (r - l + 1);
                fallback_qsort3(s, fmap, eclass, l, r);
                if (s->bug)
                    return;

                i32 cc = -1;
                for (i32 i = l; i <= r; i++) {
                    i32 cc1 = i32(eclass[fmap[i]]);
                    if (cc != cc1) {
                        set_bh(bhtab, i);
                        cc = cc1;
                    }
                }
            }
        }

        h *= 2;
        if (h > nblock || n_not_done == 0)
            break;
    }

    // The block back into eclass8, which the passes above overwrote.
    i32 j = 0;
    for (i32 i = 0; i < nblock; i++) {
        while (ftab_copy[j] == 0)
            j++;
        ftab_copy[j]--;
        eclass8[fmap[i]] = u8(j);
    }
    if (j >= 256)
        s->bug = true;
}

// -------------------------------------------------------- the main sort

// Whether the rotation at i1 sorts after the one at i2.
inline bool main_gtu(u32 i1, u32 i2, const u8 *block, const u16 *quadrant, u32 nblock, i32 *budget)
{
    u8 c1, c2;
    u16 s1, s2;

    for (int n = 0; n < 12; n++) {
        c1 = block[i1];
        c2 = block[i2];
        if (c1 != c2)
            return c1 > c2;
        i1++;
        i2++;
    }

    i32 k = i32(nblock) + 8;

    do {
        for (int n = 0; n < 8; n++) {
            c1 = block[i1];
            c2 = block[i2];
            if (c1 != c2)
                return c1 > c2;
            s1 = quadrant[i1];
            s2 = quadrant[i2];
            if (s1 != s2)
                return s1 > s2;
            i1++;
            i2++;
        }

        if (i1 >= nblock)
            i1 -= nblock;
        if (i2 >= nblock)
            i2 -= nblock;

        k -= 8;
        (*budget)--;
    } while (k >= 0);

    return false;
}

// Knuth's increments.
constexpr i32 INCS[14] = { 1,    4,    13,    40,    121,    364,    1093,
                           3280, 9841, 29524, 88573, 265720, 797161, 2391484 };

void main_simple_sort(u32 *ptr, const u8 *block, const u16 *quadrant, i32 nblock, i32 lo, i32 hi,
                      i32 d, i32 *budget)
{
    i32 big_n = hi - lo + 1;
    if (big_n < 2)
        return;

    i32 hp = 0;
    while (INCS[hp] < big_n)
        hp++;
    hp--;

    for (; hp >= 0; hp--) {
        i32 h = INCS[hp];

        i32 i = lo + h;
        for (;;) {
            for (int copy = 0; copy < 3; copy++) {
                if (i > hi)
                    break;
                u32 v = ptr[i];
                i32 j = i;
                while (main_gtu(ptr[j - h] + u32(d), v + u32(d), block, quadrant, u32(nblock),
                                budget)) {
                    ptr[j] = ptr[j - h];
                    j      = j - h;
                    if (j <= (lo + h - 1))
                        break;
                }
                ptr[j] = v;
                i++;
            }
            if (i > hi)
                break;
            if (*budget < 0)
                return;
        }
    }
}

u8 mmed3(u8 a, u8 b, u8 c)
{
    if (a > b) {
        u8 t = a;
        a    = b;
        b    = t;
    }
    if (b > c) {
        b = c;
        if (a > b)
            b = a;
    }
    return b;
}

constexpr i32 MAIN_QSORT_SMALL_THRESH = 20;
constexpr i32 MAIN_QSORT_DEPTH_THRESH = BZ_N_RADIX + BZ_N_QSORT;
constexpr i32 MAIN_QSORT_STACK_SIZE   = 100;

// Sedgewick and Bentley's three-way quicksort for strings.
void main_qsort3(S *s, u32 *ptr, const u8 *block, const u16 *quadrant, i32 nblock, i32 lo_st,
                 i32 hi_st, i32 d_st, i32 *budget)
{
    i32 stack_lo[MAIN_QSORT_STACK_SIZE];
    i32 stack_hi[MAIN_QSORT_STACK_SIZE];
    i32 stack_d[MAIN_QSORT_STACK_SIZE];

    i32 next_lo[3];
    i32 next_hi[3];
    i32 next_d[3];

    i32 sp    = 0;
    auto push = [&](i32 lz, i32 hz, i32 dz) {
        stack_lo[sp] = lz;
        stack_hi[sp] = hz;
        stack_d[sp]  = dz;
        sp++;
    };
    auto next_size = [&](int a) { return next_hi[a] - next_lo[a]; };
    auto next_swap = [&](int a, int b) {
        i32 t      = next_lo[a];
        next_lo[a] = next_lo[b];
        next_lo[b] = t;
        t          = next_hi[a];
        next_hi[a] = next_hi[b];
        next_hi[b] = t;
        t          = next_d[a];
        next_d[a]  = next_d[b];
        next_d[b]  = t;
    };

    push(lo_st, hi_st, d_st);

    while (sp > 0) {
        if (sp >= MAIN_QSORT_STACK_SIZE - 2) {
            s->bug = true;
            return;
        }

        sp--;
        i32 lo = stack_lo[sp];
        i32 hi = stack_hi[sp];
        i32 d  = stack_d[sp];
        if (hi - lo < MAIN_QSORT_SMALL_THRESH || d > MAIN_QSORT_DEPTH_THRESH) {
            main_simple_sort(ptr, block, quadrant, nblock, lo, hi, d, budget);
            if (*budget < 0)
                return;
            continue;
        }

        i32 med = mmed3(block[ptr[lo] + u32(d)], block[ptr[hi] + u32(d)],
                        block[ptr[(lo + hi) >> 1] + u32(d)]);

        i32 un_lo = lo, lt_lo = lo;
        i32 un_hi = hi, gt_hi = hi;

        for (;;) {
            for (;;) {
                if (un_lo > un_hi)
                    break;
                i32 n = i32(block[ptr[un_lo] + u32(d)]) - med;
                if (n == 0) {
                    swap_u32(ptr[un_lo], ptr[lt_lo]);
                    lt_lo++;
                    un_lo++;
                    continue;
                }
                if (n > 0)
                    break;
                un_lo++;
            }
            for (;;) {
                if (un_lo > un_hi)
                    break;
                i32 n = i32(block[ptr[un_hi] + u32(d)]) - med;
                if (n == 0) {
                    swap_u32(ptr[un_hi], ptr[gt_hi]);
                    gt_hi--;
                    un_hi--;
                    continue;
                }
                if (n < 0)
                    break;
                un_hi--;
            }
            if (un_lo > un_hi)
                break;
            swap_u32(ptr[un_lo], ptr[un_hi]);
            un_lo++;
            un_hi--;
        }

        if (gt_hi < lt_lo) {
            push(lo, hi, d + 1);
            continue;
        }

        i32 n = min_i32(lt_lo - lo, un_lo - lt_lo);
        vswap(ptr, lo, un_lo - n, n);
        i32 m = min_i32(hi - gt_hi, gt_hi - un_hi);
        vswap(ptr, un_lo, hi - m + 1, m);

        n = lo + un_lo - lt_lo - 1;
        m = hi - (gt_hi - un_hi) + 1;

        next_lo[0] = lo;
        next_hi[0] = n;
        next_d[0]  = d;
        next_lo[1] = m;
        next_hi[1] = hi;
        next_d[1]  = d;
        next_lo[2] = n + 1;
        next_hi[2] = m - 1;
        next_d[2]  = d + 1;

        if (next_size(0) < next_size(1))
            next_swap(0, 1);
        if (next_size(1) < next_size(2))
            next_swap(1, 2);
        if (next_size(0) < next_size(1))
            next_swap(0, 1);

        push(next_lo[0], next_hi[0], next_d[0]);
        push(next_lo[1], next_hi[1], next_d[1]);
        push(next_lo[2], next_hi[2], next_d[2]);
    }
}

constexpr u32 SETMASK   = 1U << 21;
constexpr u32 CLEARMASK = ~SETMASK;

// On entry the block is in block[0, nblock) with room for the overshoot
// after it; on return ptr holds the sorted order, unless *budget went below
// zero and the sort was abandoned. ftab is destroyed.
void main_sort(S *s, u32 *ptr, u8 *block, u16 *quadrant, u32 *ftab, i32 nblock, i32 *budget)
{
    i32 running_order[256];
    bool big_done[256];
    i32 copy_start[256];
    i32 copy_end[256];

    auto big_freq = [&](i32 b) { return ftab[(b + 1) << 8] - ftab[b << 8]; };

    // The two-byte frequency table.
    for (i32 i = 65536; i >= 0; i--)
        ftab[i] = 0;

    i32 j = block[0] << 8;
    i32 i = nblock - 1;
    for (; i >= 3; i -= 4) {
        quadrant[i] = 0;
        j           = (j >> 8) | (i32(block[i]) << 8);
        ftab[j]++;
        quadrant[i - 1] = 0;
        j               = (j >> 8) | (i32(block[i - 1]) << 8);
        ftab[j]++;
        quadrant[i - 2] = 0;
        j               = (j >> 8) | (i32(block[i - 2]) << 8);
        ftab[j]++;
        quadrant[i - 3] = 0;
        j               = (j >> 8) | (i32(block[i - 3]) << 8);
        ftab[j]++;
    }
    for (; i >= 0; i--) {
        quadrant[i] = 0;
        j           = (j >> 8) | (i32(block[i]) << 8);
        ftab[j]++;
    }

    for (i = 0; i < BZ_N_OVERSHOOT; i++) {
        block[nblock + i]    = block[i];
        quadrant[nblock + i] = 0;
    }

    // The rest of the initial radix sort.
    for (i = 1; i <= 65536; i++)
        ftab[i] += ftab[i - 1];

    u16 w = u16(block[0] << 8);
    i     = nblock - 1;
    for (; i >= 3; i -= 4) {
        w       = u16((w >> 8) | (block[i] << 8));
        j       = i32(ftab[w]) - 1;
        ftab[w] = u32(j);
        ptr[j]  = u32(i);
        w       = u16((w >> 8) | (block[i - 1] << 8));
        j       = i32(ftab[w]) - 1;
        ftab[w] = u32(j);
        ptr[j]  = u32(i - 1);
        w       = u16((w >> 8) | (block[i - 2] << 8));
        j       = i32(ftab[w]) - 1;
        ftab[w] = u32(j);
        ptr[j]  = u32(i - 2);
        w       = u16((w >> 8) | (block[i - 3] << 8));
        j       = i32(ftab[w]) - 1;
        ftab[w] = u32(j);
        ptr[j]  = u32(i - 3);
    }
    for (; i >= 0; i--) {
        w       = u16((w >> 8) | (block[i] << 8));
        j       = i32(ftab[w]) - 1;
        ftab[w] = u32(j);
        ptr[j]  = u32(i);
    }

    // ftab now holds where each small bucket starts. The big buckets in
    // order, smallest first.
    for (i = 0; i <= 255; i++) {
        big_done[i]      = false;
        running_order[i] = i;
    }

    {
        i32 h = 1;
        do
            h = 3 * h + 1;
        while (h <= 256);
        do {
            h = h / 3;
            for (i = h; i <= 255; i++) {
                i32 vv = running_order[i];
                j      = i;
                while (big_freq(running_order[j - h]) > big_freq(vv)) {
                    running_order[j] = running_order[j - h];
                    j                = j - h;
                    if (j <= (h - 1))
                        break;
                }
                running_order[j] = vv;
            }
        } while (h != 1);
    }

    for (i = 0; i <= 255; i++) {
        i32 ss = running_order[i];

        // Step 1: complete big bucket ss by sorting its small buckets
        // [ss, j] that earlier passes have not already done.
        for (j = 0; j <= 255; j++) {
            if (j != ss) {
                i32 sb = (ss << 8) + j;
                if (!(ftab[sb] & SETMASK)) {
                    i32 lo = i32(ftab[sb] & CLEARMASK);
                    i32 hi = i32(ftab[sb + 1] & CLEARMASK) - 1;
                    if (hi > lo) {
                        main_qsort3(s, ptr, block, quadrant, nblock, lo, hi, BZ_N_RADIX, budget);
                        if (s->bug || *budget < 0)
                            return;
                    }
                }
                ftab[sb] |= SETMASK;
            }
        }

        if (big_done[ss]) {
            s->bug = true;
            return;
        }

        // Step 2: scan it, to order the small buckets [t, ss] for every t,
        // [ss, ss] among them.
        for (j = 0; j <= 255; j++) {
            copy_start[j] = i32(ftab[(j << 8) + ss] & CLEARMASK);
            copy_end[j]   = i32(ftab[(j << 8) + ss + 1] & CLEARMASK) - 1;
        }
        for (j = i32(ftab[ss << 8] & CLEARMASK); j < copy_start[ss]; j++) {
            i32 k = i32(ptr[j]) - 1;
            if (k < 0)
                k += nblock;
            u8 c1 = block[k];
            if (!big_done[c1])
                ptr[copy_start[c1]++] = u32(k);
        }
        for (j = i32(ftab[(ss + 1) << 8] & CLEARMASK) - 1; j > copy_end[ss]; j--) {
            i32 k = i32(ptr[j]) - 1;
            if (k < 0)
                k += nblock;
            u8 c1 = block[k];
            if (!big_done[c1])
                ptr[copy_end[c1]--] = u32(k);
        }

        // The second case is a block that is one byte value throughout.
        if (!((copy_start[ss] - 1 == copy_end[ss]) ||
              (copy_start[ss] == 0 && copy_end[ss] == nblock - 1))) {
            s->bug = true;
            return;
        }

        for (j = 0; j <= 255; j++)
            ftab[(j << 8) + ss] |= SETMASK;

        // Step 3: ss is done. Its order goes into the quadrant descriptors,
        // the overshoot's too, for later comparisons to cut short; the last
        // bucket has no later comparisons.
        big_done[ss] = true;

        if (i < 255) {
            i32 bb_start = i32(ftab[ss << 8] & CLEARMASK);
            i32 bb_size  = i32(ftab[(ss + 1) << 8] & CLEARMASK) - bb_start;
            i32 shifts   = 0;

            while ((bb_size >> shifts) > 65534)
                shifts++;

            for (j = bb_size - 1; j >= 0; j--) {
                i32 a2update       = i32(ptr[bb_start + j]);
                u16 q_val          = u16(j >> shifts);
                quadrant[a2update] = q_val;
                if (a2update < BZ_N_OVERSHOOT)
                    quadrant[a2update + nblock] = q_val;
            }
            if (((bb_size - 1) >> shifts) > 65535) {
                s->bug = true;
                return;
            }
        }
    }
}

} // namespace

// On entry the block is in arr2's bytes [0, nblock); on return it is there
// still, and arr1 [0, nblock) holds the sorted order.
void bz_block_sort(S *s)
{
    u32 *ptr   = s->ptr;
    u8 *block  = s->block;
    u32 *ftab  = s->ftab;
    i32 nblock = s->nblock;
    i32 wfact  = s->work_factor;

    if (nblock < 10000) {
        fallback_sort(s, s->arr1, s->arr2, ftab, nblock);
    } else {
        // The quadrant after the block and its overshoot, 2-aligned.
        i32 i = nblock + BZ_N_OVERSHOOT;
        if (i & 1)
            i++;
        u16 *quadrant = reinterpret_cast<u16 *>(&block[i]);

        // The budget: comparisons the main sort may spend per byte.
        if (wfact < 1)
            wfact = 1;
        if (wfact > 100)
            wfact = 100;
        i32 budget = nblock * ((wfact - 1) / 3);

        main_sort(s, ptr, block, quadrant, ftab, nblock, &budget);
        if (s->bug)
            return;
        if (budget < 0)
            fallback_sort(s, s->arr1, s->arr2, ftab, nblock);
    }
    if (s->bug)
        return;

    s->orig_ptr = -1;
    for (i32 i = 0; i < s->nblock; i++) {
        if (ptr[i] == 0) {
            s->orig_ptr = i;
            break;
        }
    }

    if (s->orig_ptr == -1)
        s->bug = true;
}
