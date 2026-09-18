// CRC-32 and Adler-32, after zlib's crc32.c and adler32.c. The CRC is
// slicing-by-4 over four tables built at compile time.
#include "zlib/zlib.h"

namespace {

constexpr u32 POLY = 0xedb88320; // p(x) reflected, x^32 implied

struct CrcTables {
    u32 t[4][256];
};

constexpr CrcTables make_crc_tables()
{
    CrcTables c{};
    for (u32 n = 0; n < 256; n++) {
        u32 p = n;
        for (int k = 0; k < 8; k++)
            p = p & 1 ? (p >> 1) ^ POLY : p >> 1;
        c.t[0][n] = p;
    }
    for (u32 n = 0; n < 256; n++)
        for (int k = 1; k < 4; k++)
            c.t[k][n] = (c.t[k - 1][n] >> 8) ^ c.t[0][c.t[k - 1][n] & 0xff];
    return c;
}

constexpr CrcTables CRC = make_crc_tables();

// a(x) * b(x) mod p(x), reflected; a must not be zero.
constexpr u32 multmodp(u32 a, u32 b)
{
    u32 m = u32(1) << 31;
    u32 p = 0;
    for (;;) {
        if (a & m) {
            p ^= b;
            if ((a & (m - 1)) == 0)
                break;
        }
        m >>= 1;
        b = b & 1 ? (b >> 1) ^ POLY : b >> 1;
    }
    return p;
}

struct X2n {
    u32 t[32];
};

constexpr X2n make_x2n()
{
    X2n x{};
    u32 p  = u32(1) << 30; // x^1
    x.t[0] = p;
    for (int n = 1; n < 32; n++)
        x.t[n] = p = multmodp(p, p);
    return x;
}

constexpr X2n X2N = make_x2n();

// x^(n * 2^k) mod p(x).
u32 x2nmodp(u64 n, u32 k)
{
    u32 p = u32(1) << 31; // x^0
    while (n) {
        if (n & 1)
            p = multmodp(X2N.t[k & 31], p);
        n >>= 1;
        k++;
    }
    return p;
}

constexpr u32 BASE = 65521; // largest prime below 65536
constexpr u32 NMAX = 5552;  // most bytes before a sum can pass 2^32

} // namespace

u32 crc32_update(u32 crc, Bytes bytes)
{
    const u8 *p = bytes.data();
    usize n     = bytes.size();

    crc = ~crc;
    while (n && (usize(p) & 3)) {
        crc = (crc >> 8) ^ CRC.t[0][(crc ^ *p++) & 0xff];
        n--;
    }
    while (n >= 4) {
        u32 w;
        __builtin_memcpy(&w, p, 4);
        crc ^= w;
        crc = CRC.t[3][crc & 0xff] ^ CRC.t[2][(crc >> 8) & 0xff] ^
              CRC.t[1][(crc >> 16) & 0xff] ^ CRC.t[0][crc >> 24];
        p += 4;
        n -= 4;
    }
    while (n--)
        crc = (crc >> 8) ^ CRC.t[0][(crc ^ *p++) & 0xff];
    return ~crc;
}

u32 adler32_update(u32 adler, Bytes bytes)
{
    const u8 *p = bytes.data();
    usize len   = bytes.size();
    u32 sum2    = (adler >> 16) & 0xffff;
    adler &= 0xffff;

    if (len == 1) {
        adler += p[0];
        if (adler >= BASE)
            adler -= BASE;
        sum2 += adler;
        if (sum2 >= BASE)
            sum2 -= BASE;
        return adler | (sum2 << 16);
    }

    if (len < 16) {
        while (len--) {
            adler += *p++;
            sum2 += adler;
        }
        if (adler >= BASE)
            adler -= BASE;
        sum2 %= BASE;
        return adler | (sum2 << 16);
    }

    while (len >= NMAX) {
        len -= NMAX;
        for (u32 n = NMAX; n; n--) {
            adler += *p++;
            sum2 += adler;
        }
        adler %= BASE;
        sum2 %= BASE;
    }
    if (len) {
        while (len--) {
            adler += *p++;
            sum2 += adler;
        }
        adler %= BASE;
        sum2 %= BASE;
    }
    return adler | (sum2 << 16);
}

u32 crc32_combine_gen(u64 len_b)
{
    return x2nmodp(len_b, 3);
}

u32 crc32_combine_op(u32 crc_a, u32 crc_b, u32 op)
{
    return multmodp(op, crc_a) ^ crc_b;
}

u32 crc32_combine(u32 crc_a, u32 crc_b, u64 len_b)
{
    return crc32_combine_op(crc_a, crc_b, crc32_combine_gen(len_b));
}

u32 adler32_combine(u32 adler_a, u32 adler_b, u64 len_b)
{
    u32 rem  = u32(len_b % BASE);
    u32 sum1 = adler_a & 0xffff;
    u32 sum2 = u32((u64(rem) * sum1) % BASE);
    sum1 += (adler_b & 0xffff) + BASE - 1;
    sum2 += ((adler_a >> 16) & 0xffff) + ((adler_b >> 16) & 0xffff) + BASE - rem;
    if (sum1 >= BASE)
        sum1 -= BASE;
    if (sum1 >= BASE)
        sum1 -= BASE;
    if (sum2 >= (BASE << 1))
        sum2 -= (BASE << 1);
    if (sum2 >= BASE)
        sum2 -= BASE;
    return sum1 | (sum2 << 16);
}
