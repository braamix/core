// braam::lzma (src/lzma/). xz.data holds what the host's own liblzma made of
// ten inputs at presets 0 to 6, in both formats, under every check and through
// four filter chains, and this build must make the same bytes; and what the
// host's decoder made of each file of xz's own test corpus, which this one
// must match. The rest is the wrapper: both coders fed and drained a byte at
// a time, flushes, the memory limit, the one-shots, and every refusal.
#include "harness.h"
#include "kernel/alloc.h"
#include "kernel/string.h"
#include "lzma/lzma.h"
#include "lzma/xz.h"

namespace {

struct XzInputSum {
    u32 size;
    u32 crc;
};

struct XzCase {
    u8 input;
    u32 preset;
    u8 check;
    u8 format; // 0 .xz, 1 .lzma
    i8 chain;  // XZ_CHAINS, or -1 for the preset
    u32 size;
    u32 crc;
};

struct XzFile {
    const char *name;
    const u8 *data;
    u32 size;
    u8 ret; // the host's last lzma_ret
    u32 out_size;
    u32 out_crc;
    u8 stepped_ret; // the same, fed and drained a byte at a time
};

#include "xz.data"

// ------------------------------------------------ mkxzdata.py's inputs

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
    }
    return s;
}

constexpr u32 N_INPUTS = sizeof INPUT_SUMS / sizeof INPUT_SUMS[0];
constexpr u32 N_CASES  = sizeof XZ_CASES / sizeof XZ_CASES[0];
constexpr u32 N_FILES  = sizeof XZ_FILES / sizeof XZ_FILES[0];

// A heap buffer: the stack is too small for these.
struct Scratch {
    explicit Scratch(usize n) : p(static_cast<u8 *>(heap_alloc(n))), n(n) {}
    ~Scratch() { heap_free(p); }

    u8 *p;
    usize n;
};

Bytes bytes_of(Str s)
{
    return Bytes(reinterpret_cast<const u8 *>(s.data()), s.size());
}

Str str_of(const u8 *p, usize n)
{
    return Str(reinterpret_cast<const char *>(p), n);
}

u32 crc_of(Str s)
{
    return lzma_crc32(reinterpret_cast<const u8 *>(s.data()), s.size(), 0);
}

// liblzma's allocator over the heap, for what it hands back to be freed.
void *heap_nmemb(void *, size_t nmemb, size_t size)
{
    return heap_alloc(nmemb * size);
}

void heap_release(void *, void *p)
{
    heap_free(p);
}

const lzma_allocator HEAP = { heap_nmemb, heap_release, nullptr };

// The whole input under Finish, into chunks of room.
bool encode_whole(Str in, u32 preset, XzCheck check, XzFormat format, String &out)
{
    XzEncoder e;
    if (!e.init(preset, check, format))
        return false;
    Scratch buf(65536);
    if (!buf.p)
        return false;
    Span<const u8> src = bytes_of(in);
    out.clear();
    XzStatus s;
    do {
        Span<u8> dst(buf.p, buf.n);
        s = e.step(src, dst, XzAction::Finish);
        out.append(str_of(buf.p, buf.n - dst.size()));
    } while (s == XzStatus::More);
    return s == XzStatus::End && src.empty() && e.total_in() == in.size() &&
           e.total_out() == out.size();
}

// Through a filter chain spelled as lzma_str_to_filters takes it, streamed as
// the host's was: lzma_stream_buffer_encode would record the sizes.
bool encode_chain(Str in, const char *chain, String &out)
{
    lzma_filter filters[LZMA_FILTERS_MAX + 1];
    int at = 0;
    if (lzma_str_to_filters(chain, &at, filters, 0, &HEAP))
        return false;
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret     = lzma_stream_encoder(&strm, filters, LZMA_CHECK_CRC64);
    lzma_filters_free(filters, &HEAP);
    Scratch buf(65536);
    out.clear();
    strm.next_in  = reinterpret_cast<const u8 *>(in.data());
    strm.avail_in = in.size();
    while (ret == LZMA_OK) {
        strm.next_out  = buf.p;
        strm.avail_out = buf.n;
        ret            = lzma_code(&strm, LZMA_FINISH);
        out.append(str_of(buf.p, buf.n - strm.avail_out));
    }
    lzma_end(&strm);
    return ret == LZMA_STREAM_END;
}

// Encodes feeding `in_step` bytes and offering `out_step` at a time.
bool encode_by(Str in, u32 preset, usize in_step, usize out_step, String &out)
{
    XzEncoder e;
    if (!e.init(preset))
        return false;
    out.clear();
    Scratch buf(4096);
    usize at = 0;
    for (;;) {
        usize n            = in.size() - at < in_step ? in.size() - at : in_step;
        bool last          = at + n == in.size();
        Span<const u8> src = bytes_of(in.substr(at, n));
        Span<u8> dst(buf.p, out_step < buf.n ? out_step : buf.n);
        usize room = dst.size();
        XzStatus s = e.step(src, dst, last ? XzAction::Finish : XzAction::Run);
        at += n - src.size();
        out.append(str_of(buf.p, room - dst.size()));
        if (s == XzStatus::End)
            return at == in.size();
        if (s != XzStatus::Ok && s != XzStatus::More && s != XzStatus::Stuck)
            return false;
    }
}

// Decodes feeding `in_step` bytes and offering `out_step` at a time, with
// `finish` once the last of the input is handed over. The status it ended on.
XzStatus decode_by(Str in, XzFormat format, usize in_step, usize out_step, String &out)
{
    XzDecoder d;
    if (!d.init(format))
        return XzStatus::NoMemory;
    out.clear();
    Scratch buf(4096);
    usize at = 0;
    for (;;) {
        usize n            = in.size() - at < in_step ? in.size() - at : in_step;
        bool last          = at + n == in.size();
        Span<const u8> src = bytes_of(in.substr(at, n));
        Span<u8> dst(buf.p, out_step < buf.n ? out_step : buf.n);
        usize room = dst.size();
        XzStatus s = d.step(src, dst, last);
        at += n - src.size();
        out.append(str_of(buf.p, room - dst.size()));
        if (s != XzStatus::Ok && !(s == XzStatus::Stuck && !last))
            return s;
    }
}

// What the host's decoder was driven as, in mkxzdata.py: the last lzma_ret.
lzma_ret decode_as_host(Str in, String &out)
{
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret     = lzma_auto_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK)
        return ret;
    Scratch buf(65536);
    out.clear();
    strm.next_in  = reinterpret_cast<const u8 *>(in.data());
    strm.avail_in = in.size();
    do {
        strm.next_out  = buf.p;
        strm.avail_out = buf.n;
        ret            = lzma_code(&strm, LZMA_FINISH);
        out.append(str_of(buf.p, buf.n - strm.avail_out));
    } while (ret == LZMA_OK);
    lzma_end(&strm);
    return ret;
}

// The wrapper's status for what the host's decoder returned under FINISH.
XzStatus status_for(u8 ret)
{
    switch (ret) {
    case LZMA_STREAM_END:
        return XzStatus::End;
    case LZMA_FORMAT_ERROR:
        return XzStatus::NotXz;
    case LZMA_OPTIONS_ERROR:
        return XzStatus::Unsupported;
    case LZMA_DATA_ERROR:
    case LZMA_BUF_ERROR:
        return XzStatus::Corrupt;
    default:
        return XzStatus::Misuse;
    }
}

} // namespace

void test_lzma()
{
    test_begin("lzma");

    usize in_use = heap_stats().bytes_in_use;

    // The checks against their published vectors, and the version vendored.
    CHECK_EQ(lzma_crc32(reinterpret_cast<const u8 *>("123456789"), 9, 0), 0xcbf43926);
    {
        u64 c = lzma_crc64(reinterpret_cast<const u8 *>("123456789"), 9, 0);
        CHECK_EQ(u32(c >> 32), 0x995dc9bb);
        CHECK_EQ(u32(c), 0xdf1939fa);
    }
    CHECK_EQ(lzma_version_number(), 50080042);
    CHECK(lzma_check_is_supported(LZMA_CHECK_SHA256));
    CHECK(!lzma_check_is_supported(lzma_check(2)));

    {
        // The inputs are the ones mkxzdata.py made.
        String inputs[N_INPUTS];
        for (u32 i = 0; i < N_INPUTS; i++) {
            inputs[i] = make_input(i);
            CHECK_EQ(inputs[i].size(), INPUT_SUMS[i].size);
            CHECK_EQ(crc_of(inputs[i].str()), INPUT_SUMS[i].crc);
        }

        // The host's streams, byte for byte, and each decoded back.
        {
            u32 same = 0, back = 0;
            for (const XzCase &c : XZ_CASES) {
                Str in = inputs[c.input].str();
                String packed;
                bool made = c.chain >= 0
                                ? encode_chain(in, XZ_CHAINS[c.chain], packed)
                                : encode_whole(in, c.preset, XzCheck(c.check),
                                               c.format ? XzFormat::Lzma : XzFormat::Xz, packed);
                bool ok   = made && packed.size() == c.size && crc_of(packed.str()) == c.crc;
                test_check(ok, "encode made the host's stream", __FILE_NAME__, __LINE__);
                if (!ok) {
                    CHECK_EQ(u32(&c - XZ_CASES), ~0U); // which case
                    CHECK_EQ(packed.size(), c.size);
                }
                same += ok;
                Result<String> r = xz_uncompress(packed.str(), in.size());
                back += r.is_ok() && r.value().str() == in;
            }
            CHECK_EQ(same, N_CASES);
            CHECK_EQ(back, N_CASES);
        }

        // A filter chain spelled back is the same chain.
        {
            for (const char *chain : XZ_CHAINS) {
                lzma_filter filters[LZMA_FILTERS_MAX + 1];
                int at = 0;
                CHECK(lzma_str_to_filters(chain, &at, filters, 0, &HEAP) == nullptr);
                char *spelled = nullptr;
                CHECK(lzma_str_from_filters(&spelled, filters, LZMA_STR_ENCODER, &HEAP) == LZMA_OK);
                lzma_filters_free(filters, &HEAP);
                String a, b;
                CHECK(encode_chain(inputs[3].str(), chain, a));
                CHECK(spelled && encode_chain(inputs[3].str(), spelled, b));
                CHECK(a.str() == b.str());
                heap_free(spelled);
            }
            lzma_filter filters[LZMA_FILTERS_MAX + 1];
            int at = 0;
            CHECK(lzma_str_to_filters("lzma2:bogus=1", &at, filters, 0, &HEAP) != nullptr);
            CHECK_EQ(at, 6);
        }

        // A byte in and a byte out, each way, and other small steps: the stream
        // must not depend on the steps.
        {
            Str text = inputs[3].str();
            String whole, packed, back;
            CHECK(encode_whole(text, 1, XzCheck::Crc64, XzFormat::Xz, whole));
            CHECK(encode_by(text, 1, 1, 1, packed));
            CHECK(packed.str() == whole.str());
            CHECK(decode_by(packed.str(), XzFormat::Xz, 1, 1, back) == XzStatus::End);
            CHECK(back.str() == text);
            CHECK(encode_by(text, 1, 7, 3, packed));
            CHECK(packed.str() == whole.str());
            CHECK(decode_by(packed.str(), XzFormat::Auto, 3, 7, back) == XzStatus::End);
            CHECK(back.str() == text);
            CHECK(encode_whole(text, 2, XzCheck::None, XzFormat::Lzma, whole));
            CHECK(decode_by(whole.str(), XzFormat::Lzma, 1, 1, back) == XzStatus::End);
            CHECK(back.str() == text);
        }

        // A sync flush makes what came before readable while the block goes on;
        // a full flush ends it, so the whole is longer than one block.
        {
            Str a                    = inputs[3].str().substr(0, 5000);
            Str b                    = inputs[3].str().substr(5000);
            const XzAction FLUSHES[] = { XzAction::SyncFlush, XzAction::FullFlush };
            usize lengths[2]         = {};
            for (u32 k = 0; k < 2; k++) {
                XzAction flush = FLUSHES[k];
                XzEncoder e;
                CHECK(e.init(1).is_ok());
                Scratch buf(32768);
                Span<const u8> src = bytes_of(a);
                Span<u8> dst(buf.p, 16);
                CHECK(e.step(src, dst, XzAction::Run) == XzStatus::Ok);
                dst = Span<u8>(dst.data(), 0);
                CHECK(e.step(src, dst, flush) == XzStatus::More);
                Span<const u8> other = bytes_of(b);
                CHECK(e.step(other, dst, flush) == XzStatus::Misuse); // input changed
                dst = Span<u8>(dst.data(), usize(buf.p + buf.n - dst.data()));
                CHECK(e.step(src, dst, flush) == XzStatus::Ok);
                CHECK(src.empty());
                usize flushed = buf.n - dst.size();

                // What is out so far gives back all of a, and asks for more.
                XzDecoder d;
                CHECK(d.init(XzFormat::Xz).is_ok());
                Scratch got(8192);
                Span<const u8> part(buf.p, flushed);
                Span<u8> room(got.p, got.n);
                CHECK(d.step(part, room, false) == XzStatus::Ok);
                CHECK(str_of(got.p, got.n - room.size()) == a);

                src = bytes_of(b);
                CHECK(e.step(src, dst, XzAction::Finish) == XzStatus::End);
                CHECK(e.step(src, dst, XzAction::Finish) == XzStatus::Misuse); // after End
                usize end = buf.n - dst.size();
                CHECK_EQ(e.total_in(), inputs[3].size());
                CHECK_EQ(e.total_out(), end);
                Result<String> r = xz_uncompress(str_of(buf.p, end), 12000);
                CHECK(r.is_ok() && r.value().str() == inputs[3].str());
                lengths[k] = end;
            }
            String one;
            CHECK(encode_whole(inputs[3].str(), 1, XzCheck::Crc64, XzFormat::Xz, one));
            CHECK(lengths[0] > one.size());
            CHECK(lengths[1] > lengths[0]); // a second block, its header and index entry
        }

        // Finish in pieces: More until the room suffices, and the action may
        // not change meanwhile.
        {
            XzEncoder e;
            CHECK(e.init(0).is_ok());
            CHECK(e.memusage() > 0);
            Scratch buf(8192);
            Span<const u8> src = bytes_of(inputs[3].str());
            Span<u8> dst(buf.p, 100);
            CHECK(e.step(src, dst, XzAction::Finish) == XzStatus::More);
            CHECK(dst.empty());
            CHECK(e.step(src, dst, XzAction::Run) == XzStatus::Misuse);
            dst = Span<u8>(buf.p + 100, buf.n - 100);
            XzStatus s;
            do
                s = e.step(src, dst, XzAction::Finish);
            while (s == XzStatus::More && !dst.empty());
            CHECK(s == XzStatus::End);
            Result<String> r = xz_uncompress(str_of(buf.p, buf.n - dst.size()), 12000);
            CHECK(r.is_ok() && r.value().str() == inputs[3].str());
        }

        // The memory limit: refused, raised, and the stream carries on.
        {
            Result<String> packed = xz_compress(inputs[9].str(), 6);
            CHECK(packed.is_ok());
            XzDecoder d;
            CHECK(d.init(XzFormat::Xz, 1 << 20).is_ok());
            Scratch buf(8192);
            Span<const u8> src = bytes_of(packed.value().str());
            Span<u8> dst(buf.p, buf.n);
            CHECK(d.step(src, dst, true) == XzStatus::MemLimit);
            CHECK(d.why() == "the memory limit is too low for this stream");
            CHECK(d.set_memlimit(0).is_ok());
            CHECK(d.step(src, dst, true) == XzStatus::End);
            CHECK(str_of(buf.p, buf.n - dst.size()) == inputs[9].str());
            CHECK(d.memusage() > (u64(8) << 20)); // preset 6's dictionary
            CHECK_EQ(d.total_out(), inputs[9].size());
        }

        // The one-shots: streams in a row are one output, and what is not
        // another stream, a truncation, or more than the limit is refused.
        {
            Result<String> a = xz_compress(inputs[2].str(), 0, XzCheck::Sha256);
            Result<String> b = xz_compress(inputs[9].str());
            CHECK(a.is_ok() && b.is_ok());
            String both, want;
            both.append(a.value().str());
            both.append(b.value().str());
            want.append(inputs[2].str());
            want.append(inputs[9].str());
            Result<String> r = xz_uncompress(both.str(), want.size());
            CHECK(r.is_ok() && r.value().str() == want.str());
            CHECK(xz_uncompress(both.str(), want.size() - 1).is_err());
            CHECK(xz_uncompress(both.str().substr(0, both.size() - 1), want.size()).is_err());
            both.append(Str("\0\0\0\0", 4)); // stream padding
            r = xz_uncompress(both.str(), want.size());
            CHECK(r.is_ok() && r.value().str() == want.str());
            both.append("x");
            CHECK(xz_uncompress(both.str(), want.size()).is_err());
            CHECK(xz_uncompress("", 100).is_err());
            CHECK(xz_compress("abc", 10).is_err());
            CHECK(xz_compress("abc", 6, XzCheck(2)).is_err());

            // .lzma is one stream, and nothing may follow it.
            String alone;
            CHECK(encode_whole(inputs[2].str(), 0, XzCheck::None, XzFormat::Lzma, alone));
            r = xz_uncompress(alone.str(), 100);
            CHECK(r.is_ok() && r.value().str() == inputs[2].str());
            alone.append("x");
            CHECK(xz_uncompress(alone.str(), 100).is_err());

            Result<String> e = xz_compress("");
            CHECK(e.is_ok() && e.value().size() == 32);
            Result<String> back = xz_uncompress(e.value().str(), 0);
            CHECK(back.is_ok() && back.value().size() == 0);
        }

        // Refusals: the wrong format asked for, damage, and a cut.
        {
            String good;
            CHECK(encode_whole(inputs[3].str(), 1, XzCheck::Crc64, XzFormat::Xz, good));
            String out;
            CHECK(decode_by(good.str(), XzFormat::Lzip, good.size(), 4096, out) == XzStatus::NotXz);
            CHECK(decode_by("PK\3\4", XzFormat::Auto, 4, 4096, out) == XzStatus::NotXz);

            String bad;
            bad.append(good.str());
            bad.data()[bad.size() / 2] ^= 0x5a;
            CHECK(decode_by(bad.str(), XzFormat::Xz, bad.size(), 4096, out) == XzStatus::Corrupt);
            {
                XzDecoder d;
                CHECK(d.init().is_ok());
                Scratch buf(65536);
                Span<const u8> src = bytes_of(bad.str());
                Span<u8> dst(buf.p, buf.n);
                XzStatus s;
                do
                    s = d.step(src, dst, true);
                while (s == XzStatus::Ok);
                CHECK(s == XzStatus::Corrupt);
                CHECK(d.why() == "the compressed data is corrupt");
            }

            Str cut = good.str().substr(0, good.size() - 7);
            CHECK(decode_by(cut, XzFormat::Xz, cut.size(), 4096, out) == XzStatus::Corrupt);
            CHECK(decode_by(cut, XzFormat::Xz, 1, 1, out) == XzStatus::Corrupt);
        }

        // Parameters the wrapper refuses, and calls with no coder.
        {
            XzEncoder e;
            XzDecoder d;
            CHECK(e.init(10).is_err());
            CHECK(e.init(6 | XZ_PRESET_EXTREME | 0x100).is_err());
            CHECK(e.init(6, XzCheck::Crc64, XzFormat::Lzip).is_err());
            CHECK(e.init(6, XzCheck::Crc64, XzFormat::Auto).is_err());
            CHECK(!e.ready());
            Span<const u8> src;
            Span<u8> dst;
            CHECK(e.step(src, dst, XzAction::Run) == XzStatus::Misuse);
            CHECK(d.step(src, dst, false) == XzStatus::Misuse);
            CHECK(d.set_memlimit(0).is_err());
            CHECK(e.init(0).is_ok());
            CHECK(e.step(src, dst, XzAction::Run) == XzStatus::Ok);
            CHECK(e.step(src, dst, XzAction::Run) == XzStatus::Stuck);
            CHECK(d.init().is_ok());
            CHECK(d.step(src, dst, false) == XzStatus::Ok);
            CHECK(d.step(src, dst, false) == XzStatus::Stuck);
            XzDecoder moved(static_cast<XzDecoder &&>(d));
            CHECK(!d.ready() && moved.ready());
        }
    }

    // xz's own corpus, as the host's decoder read it, then through the wrapper
    // a byte at a time.
    {
        u32 same = 0, stepped = 0;
        for (const XzFile &f : XZ_FILES) {
            Str in = str_of(f.data, f.size);
            String out;
            lzma_ret ret = decode_as_host(in, out);
            bool ok = ret == f.ret && out.size() == f.out_size && crc_of(out.str()) == f.out_crc;
            test_check(ok, f.name, __FILE_NAME__, __LINE__);
            same += ok;

            XzStatus s = decode_by(in, XzFormat::Auto, 1, 1, out);
            ok         = s == status_for(f.stepped_ret) &&
                         (s != XzStatus::End ||
                          (out.size() == f.out_size && crc_of(out.str()) == f.out_crc));
            test_check(ok, f.name, __FILE_NAME__, __LINE__);
            if (!ok) {
                CHECK_EQ(u32(s), u32(status_for(f.stepped_ret)));
                CHECK_EQ(out.size(), f.out_size);
            }
            stepped += ok;
        }
        CHECK_EQ(same, N_FILES);
        CHECK_EQ(stepped, N_FILES);
    }

    CHECK_EQ(heap_stats().bytes_in_use, in_use);
}
