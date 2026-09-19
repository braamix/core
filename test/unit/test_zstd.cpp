// braam::zstd (src/zstd/). zstd.data holds what src/zstd/lib, built for the
// host, made of twelve inputs at sixteen levels, under six sets of parameters
// and with two dictionaries, and this build must make the same bytes; and what
// that decoder made of each golden file of zstd's own tests, which this one
// must match. The rest is the C API through the paths a program takes:
// streaming a byte at a time, the checksum, prepared dictionaries, the frame
// queries, skippable frames, a custom allocator, and the refusals.
#include "harness.h"
#include "kernel/alloc.h"
#include "kernel/string.h"
#include "zlib/zlib.h"

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/zstd.h"

namespace {

struct ZstdInputSum {
    u32 size;
    u32 crc;
};

struct ZstdParams {
    const char *name;
    u8 count;
    struct {
        i32 param;
        i32 value;
    } set[2];
};

struct ZstdCase {
    u8 input;
    i8 level;
    u8 params;     // ZSTD_PARAMS
    u8 dictionary; // 0 none, 1 input 9's first 2 KiB, 2 ZSTD_GOLDEN_DICT
    u32 size;
    u32 crc;
};

struct ZstdFile {
    const char *name;
    const u8 *data;
    u32 size;
    u8 code; // the ZSTD_ErrorCode it ended on, 0 for none
    u32 out_size;
    u32 out_crc;
};

#include "zstd.data"

// ----------------------------------------------- mkzstddata.py's inputs

const char *const WORDS[] = { "the",  "of",   "and",    "deflate",  "window", "a",     "stream",
                              "to",   "in",   "block",  "Huffman",  "is",     "that",  "match",
                              "code", "for",  "length", "distance", "with",   "as",    "literal",
                              "tree", "bits", "on",     "by",       "hash",   "chain", "lazy",
                              "it",   "be",   "zlib",   "at" };

struct Lcg {
    u32 x;

    u32 next()
    {
        x = x * 1103515245U + 12345U;
        return (x >> 16) & 0x7fff;
    }
};

void text(String &out, u32 seed, usize n)
{
    Lcg r{ seed };
    usize start = out.size();
    while (out.size() - start < n) {
        out.append(WORDS[r.next() % 32]);
        u32 k = r.next() % 16;
        out.append(k == 0 ? "\n" : k == 1 ? ", " : " ");
    }
    out.truncate(start + n);
}

void noise(String &out, u32 seed, usize n)
{
    Lcg r{ seed };
    for (usize i = 0; i < n; i++)
        out.push(char(r.next() & 0xff));
}

void runs(String &out, u32 seed, usize n)
{
    Lcg r{ seed };
    while (out.size() < n) {
        char c    = char(r.next() & 0xff);
        u32 count = 1 + r.next() % 300;
        for (u32 i = 0; i < count; i++)
            out.push(c);
    }
    out.truncate(n);
}

void periodic(String &out, u32 seed, usize period, usize n)
{
    String unit;
    noise(unit, seed, period);
    while (out.size() < n)
        out.append(unit.str());
    out.truncate(n);
}

Str str_of(const u8 *p, usize n)
{
    return Str(reinterpret_cast<const char *>(p), n);
}

String make_input(u32 i)
{
    String s;
    switch (i) {
    case 1:
        s.append("a");
        break;
    case 2:
        s.append("hello, hello, hello world\n");
        break;
    case 3:
        text(s, 1, 12000);
        break;
    case 4:
        text(s, 2, 250000);
        break;
    case 5:
        noise(s, 3, 20000);
        break;
    case 6:
        runs(s, 4, 40000);
        break;
    case 7:
        periodic(s, 6, 13, 60000);
        break;
    case 8:
        for (u32 k = 0; k < 120000; k++)
            s.push('\0');
        break;
    case 9:
        text(s, 5, 5000);
        break;
    case 10:
        s.append(str_of(ZSTD_GOLDEN_INPUT_0, sizeof ZSTD_GOLDEN_INPUT_0));
        break;
    case 11:
        s.append(str_of(ZSTD_GOLDEN_INPUT_1, sizeof ZSTD_GOLDEN_INPUT_1));
        break;
    }
    return s;
}

constexpr u32 N_SUMS   = sizeof INPUT_SUMS / sizeof INPUT_SUMS[0];
constexpr u32 N_INPUTS = N_SUMS + 2;
constexpr u32 N_CASES  = sizeof ZSTD_CASES / sizeof ZSTD_CASES[0];
constexpr u32 N_FILES  = sizeof ZSTD_FILES / sizeof ZSTD_FILES[0];

// A heap buffer: the stack is too small for these.
struct Scratch {
    explicit Scratch(usize n) : p(static_cast<u8 *>(heap_alloc(n))), n(n) {}
    ~Scratch() { heap_free(p); }

    u8 *p;
    usize n;
};

u32 crc_of(Str s)
{
    return crc32_update(0, s);
}

bool failed(size_t r, ZSTD_ErrorCode want)
{
    return ZSTD_isError(r) && ZSTD_getErrorCode(r) == want;
}

// ZSTD_compress2 under a case's level, parameters and dictionary.
bool compress_case(const ZstdCase &c, Str in, Str dict, String &out)
{
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx)
        return false;
    bool ok = !ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, c.level));
    const ZstdParams &ps = ZSTD_PARAMS[c.params];
    for (u8 k = 0; ok && k < ps.count; k++)
        ok = !ZSTD_isError(
            ZSTD_CCtx_setParameter(cctx, ZSTD_cParameter(ps.set[k].param), ps.set[k].value));
    if (ok && !dict.empty())
        ok = !ZSTD_isError(ZSTD_CCtx_loadDictionary(cctx, dict.data(), dict.size()));
    Scratch buf(ZSTD_compressBound(in.size()));
    size_t n = ok ? ZSTD_compress2(cctx, buf.p, buf.n, in.data(), in.size()) : 0;
    ZSTD_freeCCtx(cctx);
    out.clear();
    if (!ok || ZSTD_isError(n))
        return false;
    out.append(str_of(buf.p, n));
    return true;
}

// ZSTD_decompressStream fed `in_step` bytes and offered `out_step` of room at a
// time, as mkzstddata.py drives it when both are whole: until a call ends a
// frame with the input consumed, or consumes it without filling the room. The
// ZSTD_ErrorCode it ended on; srcSize_wrong for a frame left unfinished.
ZSTD_ErrorCode decompress_by(Str in, Str dict, usize in_step, usize out_step, String &out)
{
    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!dctx)
        return ZSTD_error_memory_allocation;
    if (!dict.empty() && ZSTD_isError(ZSTD_DCtx_loadDictionary(dctx, dict.data(), dict.size()))) {
        ZSTD_freeDCtx(dctx);
        return ZSTD_error_dictionary_corrupted;
    }
    out.clear();
    Scratch buf(65536);
    ZSTD_ErrorCode code = ZSTD_error_no_error;
    usize at            = 0;
    for (;;) {
        usize n            = in.size() - at < in_step ? in.size() - at : in_step;
        ZSTD_inBuffer src  = { in.data() + at, n, 0 };
        ZSTD_outBuffer dst = { buf.p, out_step < buf.n ? out_step : buf.n, 0 };
        size_t hint        = ZSTD_decompressStream(dctx, &dst, &src);
        if (ZSTD_isError(hint)) {
            code = ZSTD_getErrorCode(hint);
            break;
        }
        at += src.pos;
        out.append(str_of(buf.p, dst.pos));
        if (at == in.size() && (hint == 0 || dst.pos < dst.size)) {
            if (hint != 0)
                code = ZSTD_error_srcSize_wrong;
            break;
        }
    }
    ZSTD_freeDCtx(dctx);
    return code;
}

// ZSTD_compressStream2 fed `in_step` bytes and offered `out_step` of room at a
// time, the size pledged, as a program with a known length streams.
bool compress_by(Str in, int level, usize in_step, usize out_step, String &out)
{
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx)
        return false;
    bool ok = !ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level)) &&
              !ZSTD_isError(ZSTD_CCtx_setPledgedSrcSize(cctx, in.size()));
    out.clear();
    Scratch buf(4096);
    usize at = 0;
    while (ok) {
        usize n            = in.size() - at < in_step ? in.size() - at : in_step;
        bool last          = at + n == in.size();
        ZSTD_inBuffer src  = { in.data() + at, n, 0 };
        ZSTD_outBuffer dst = { buf.p, out_step < buf.n ? out_step : buf.n, 0 };
        size_t left = ZSTD_compressStream2(cctx, &dst, &src, last ? ZSTD_e_end : ZSTD_e_continue);
        ok          = !ZSTD_isError(left);
        at += src.pos;
        out.append(str_of(buf.p, dst.pos));
        if (ok && last && at == in.size() && left == 0)
            break;
    }
    ZSTD_freeCCtx(cctx);
    return ok;
}

// A ZSTD_customMem that counts what it is asked for.
struct Counts {
    u32 allocs;
    u32 frees;
};

void *counted_alloc(void *opaque, size_t n)
{
    static_cast<Counts *>(opaque)->allocs++;
    return heap_alloc(n);
}

void counted_free(void *opaque, void *p)
{
    if (p)
        static_cast<Counts *>(opaque)->frees++;
    heap_free(p);
}

} // namespace

void test_zstd()
{
    test_begin("zstd");

    usize in_use = heap_stats().bytes_in_use;

    // The version vendored.
    CHECK_EQ(ZSTD_versionNumber(), 10600);
    CHECK(Str(ZSTD_versionString()) == "1.6.0");
    CHECK_EQ(ZSTD_minCLevel() < 0, 1);
    CHECK_EQ(ZSTD_maxCLevel(), 22);
    CHECK_EQ(ZSTD_defaultCLevel(), 3);

    {
        // The inputs are the ones mkzstddata.py made.
        String inputs[N_INPUTS];
        for (u32 i = 0; i < N_INPUTS; i++)
            inputs[i] = make_input(i);
        for (u32 i = 0; i < N_SUMS; i++) {
            CHECK_EQ(inputs[i].size(), INPUT_SUMS[i].size);
            CHECK_EQ(crc_of(inputs[i].str()), INPUT_SUMS[i].crc);
        }
        const Str DICTS[3] = { Str(), inputs[9].str().substr(0, 2048),
                               str_of(ZSTD_GOLDEN_DICT, sizeof ZSTD_GOLDEN_DICT) };

        // The oracle's frames, byte for byte, and each decompressed back.
        {
            u32 same = 0, back = 0;
            for (const ZstdCase &c : ZSTD_CASES) {
                Str in   = inputs[c.input].str();
                Str dict = DICTS[c.dictionary];
                String packed;
                bool ok = compress_case(c, in, dict, packed) && packed.size() == c.size &&
                          crc_of(packed.str()) == c.crc;
                test_check(ok, "compress made the oracle's frame", __FILE_NAME__, __LINE__);
                if (!ok) {
                    CHECK_EQ(u32(&c - ZSTD_CASES), ~0U); // which case
                    CHECK_EQ(packed.size(), c.size);
                }
                same += ok;
                String out;
                back += decompress_by(packed.str(), dict, packed.size(), 65536, out) ==
                            ZSTD_error_no_error &&
                        out.str() == in;
            }
            CHECK_EQ(same, N_CASES);
            CHECK_EQ(back, N_CASES);
        }

        // A byte in and a byte out, each way, and other small steps: with the
        // size pledged, the frame must not depend on the steps.
        {
            Str text = inputs[3].str();
            Scratch whole(ZSTD_compressBound(text.size()));
            size_t n = ZSTD_compress(whole.p, whole.n, text.data(), text.size(), 3);
            CHECK(!ZSTD_isError(n));
            Str one = str_of(whole.p, n);
            String packed, back;
            CHECK(compress_by(text, 3, 1, 1, packed));
            CHECK(packed.str() == one);
            CHECK(compress_by(text, 3, 7, 3, packed));
            CHECK(packed.str() == one);
            CHECK(decompress_by(one, Str(), 1, 1, back) == ZSTD_error_no_error);
            CHECK(back.str() == text);
            CHECK(decompress_by(one, Str(), 3, 7, back) == ZSTD_error_no_error);
            CHECK(back.str() == text);

            // The one-shot, with its size in the header.
            CHECK_EQ(ZSTD_getFrameContentSize(one.data(), one.size()), text.size());
            CHECK_EQ(ZSTD_findFrameCompressedSize(one.data(), one.size()), one.size());
            Scratch out(text.size());
            CHECK_EQ(ZSTD_decompress(out.p, out.n, one.data(), one.size()), text.size());
            CHECK(str_of(out.p, text.size()) == text);
            CHECK(failed(ZSTD_decompress(out.p, out.n - 1, one.data(), one.size()),
                         ZSTD_error_dstSize_tooSmall));
            CHECK(failed(ZSTD_decompress(out.p, out.n, one.data(), one.size() - 1),
                         ZSTD_error_srcSize_wrong));
        }

        // The checksum: written, checked, and a damaged one refused.
        {
            ZstdCase c{ 9, 3, 1, 0, 0, 0 }; // "checksum"
            String packed, out;
            CHECK(compress_case(c, inputs[9].str(), Str(), packed));
            ZSTD_frameHeader h;
            CHECK_EQ(ZSTD_getFrameHeader(&h, packed.data(), packed.size()), 0);
            CHECK(h.checksumFlag);
            CHECK(decompress_by(packed.str(), Str(), packed.size(), 65536, out) ==
                  ZSTD_error_no_error);
            packed.data()[packed.size() - 1] ^= 1;
            CHECK(decompress_by(packed.str(), Str(), packed.size(), 65536, out) ==
                  ZSTD_error_checksum_wrong);
        }

        // Prepared dictionaries: a raw one has no ID, and without it the frame
        // does not come back.
        {
            Str dict        = DICTS[1];
            Str in          = inputs[3].str();
            ZSTD_CDict *cd  = ZSTD_createCDict(dict.data(), dict.size(), 5);
            ZSTD_DDict *dd  = ZSTD_createDDict(dict.data(), dict.size());
            ZSTD_CCtx *cctx = ZSTD_createCCtx();
            ZSTD_DCtx *dctx = ZSTD_createDCtx();
            CHECK(cd && dd && cctx && dctx);
            CHECK_EQ(ZSTD_getDictID_fromCDict(cd), 0);
            CHECK_EQ(ZSTD_getDictID_fromDDict(dd), 0);
            Scratch packed(ZSTD_compressBound(in.size()));
            size_t n = ZSTD_compress_usingCDict(cctx, packed.p, packed.n, in.data(), in.size(), cd);
            CHECK(!ZSTD_isError(n));
            size_t plain = ZSTD_compress(packed.p + n, packed.n - n, in.data(), in.size(), 5);
            CHECK(!ZSTD_isError(plain) && n < plain);
            Scratch out(in.size());
            CHECK_EQ(ZSTD_decompress_usingDDict(dctx, out.p, out.n, packed.p, n, dd), in.size());
            CHECK(str_of(out.p, in.size()) == in);
            size_t r = ZSTD_decompress(out.p, out.n, packed.p, n);
            CHECK(ZSTD_isError(r) || str_of(out.p, in.size()) != in);
            ZSTD_freeCDict(cd);
            ZSTD_freeDDict(dd);
            ZSTD_freeCCtx(cctx);
            ZSTD_freeDCtx(dctx);
        }

        // Frames in a row, with a skippable frame between: one output, and the
        // queries walk them all.
        {
            Str a = inputs[2].str();
            Str b = inputs[9].str();
            Scratch all(ZSTD_compressBound(a.size()) + ZSTD_compressBound(b.size()) + 64);
            size_t na = ZSTD_compress(all.p, all.n, a.data(), a.size(), 1);
            size_t ns = ZSTD_writeSkippableFrame(all.p + na, all.n - na, "meta", 4, 3);
            CHECK(!ZSTD_isError(na) && !ZSTD_isError(ns));
            CHECK_EQ(ns, 12);
            CHECK(ZSTD_isSkippableFrame(all.p + na, ns));
            size_t nb = ZSTD_compress(all.p + na + ns, all.n - na - ns, b.data(), b.size(), 19);
            CHECK(!ZSTD_isError(nb));
            usize total = na + ns + nb;
            CHECK_EQ(ZSTD_findDecompressedSize(all.p, total), a.size() + b.size());
            char meta[4];
            unsigned variant = 0;
            CHECK_EQ(ZSTD_readSkippableFrame(meta, 4, &variant, all.p + na, ns), 4);
            CHECK(Str(meta, 4) == "meta" && variant == 3);
            String out;
            CHECK(decompress_by(str_of(all.p, total), Str(), 5, 11, out) == ZSTD_error_no_error);
            String want;
            want.append(a);
            want.append(b);
            CHECK(out.str() == want.str());
        }

        // Refusals.
        {
            String out;
            CHECK(decompress_by("PK\3\4 not a frame", Str(), 17, 65536, out) ==
                  ZSTD_error_prefix_unknown);
            CHECK(decompress_by(Str("\x27\xb5\x2f\xfd\0\0\0\0", 8), Str(), 8, 65536, out) ==
                  ZSTD_error_prefix_unknown); // v0.7's magic: no legacy formats
            CHECK_EQ(ZSTD_getFrameContentSize("PK\3\4", 4), ZSTD_CONTENTSIZE_ERROR);
            ZstdCase c{ 3, 3, 2, 0, 0, 0 }; // "no content size"
            String packed;
            CHECK(compress_case(c, inputs[3].str(), Str(), packed));
            CHECK_EQ(ZSTD_getFrameContentSize(packed.data(), packed.size()),
                     ZSTD_CONTENTSIZE_UNKNOWN);
            Str cut = packed.str().substr(0, packed.size() - 5);
            CHECK(decompress_by(cut, Str(), cut.size(), 65536, out) == ZSTD_error_srcSize_wrong);
            CHECK(decompress_by(cut, Str(), 1, 1, out) == ZSTD_error_srcSize_wrong);
            String bad;
            bad.append(packed.str());
            bad.data()[bad.size() / 2] ^= 0x5a;
            CHECK(decompress_by(bad.str(), Str(), bad.size(), 65536, out) != ZSTD_error_no_error ||
                  out.str() != inputs[3].str());

            // There are no threads.
            ZSTD_CCtx *cctx = ZSTD_createCCtx();
            CHECK(ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, 1)));
            CHECK_EQ(ZSTD_cParam_getBounds(ZSTD_c_nbWorkers).upperBound, 0);
            CHECK(failed(ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, 40),
                         ZSTD_error_parameter_outOfBound));
            ZSTD_freeCCtx(cctx);
            CHECK(Str(ZSTD_getErrorName(ZSTD_decompress(nullptr, 0, "x", 1))).size() > 0);
        }

        // An allocator of the program's own is the one used, and each block it
        // gave is given back.
        {
            Counts n{};
            ZSTD_customMem mem = { counted_alloc, counted_free, &n };
            ZSTD_CCtx *cctx    = ZSTD_createCCtx_advanced(mem);
            ZSTD_DCtx *dctx    = ZSTD_createDCtx_advanced(mem);
            CHECK(cctx && dctx);
            Str in = inputs[3].str();
            Scratch packed(ZSTD_compressBound(in.size()));
            size_t p = ZSTD_compressCCtx(cctx, packed.p, packed.n, in.data(), in.size(), 9);
            Scratch out(in.size());
            CHECK_EQ(ZSTD_decompressDCtx(dctx, out.p, out.n, packed.p, p), in.size());
            CHECK(ZSTD_sizeof_CCtx(cctx) > 0 && ZSTD_sizeof_DCtx(dctx) > 0);
            ZSTD_freeCCtx(cctx);
            ZSTD_freeDCtx(dctx);
            CHECK(n.allocs >= 2);
            CHECK_EQ(n.frees, n.allocs);
        }
    }

    // zstd's golden files, as the oracle's decoder read them, then a byte at a
    // time.
    {
        u32 same = 0, stepped = 0;
        for (const ZstdFile &f : ZSTD_FILES) {
            Str in = str_of(f.data, f.size);
            String out;
            ZSTD_ErrorCode code = decompress_by(in, Str(), in.size(), 65536, out);
            bool ok = code == f.code && out.size() == f.out_size && crc_of(out.str()) == f.out_crc;
            test_check(ok, f.name, __FILE_NAME__, __LINE__);
            if (!ok) {
                CHECK_EQ(code, f.code);
                CHECK_EQ(out.size(), f.out_size);
            }
            same += ok;

            code = decompress_by(in, Str(), 1, 1, out);
            ok   = code == f.code && (code != ZSTD_error_no_error ||
                                      (out.size() == f.out_size && crc_of(out.str()) == f.out_crc));
            test_check(ok, f.name, __FILE_NAME__, __LINE__);
            stepped += ok;
        }
        CHECK_EQ(same, N_FILES);
        CHECK_EQ(stepped, N_FILES);
    }

    CHECK_EQ(heap_stats().bytes_in_use, in_use);
}
