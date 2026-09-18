// braam::bzip2 (src/bzip2/). bzip2.data holds what the host's own libbzip2
// made of ten inputs at every block size, and the compressor must make the
// same bytes; bzip2 itself wrote SAMPLE3, and RANDOMISED is a block of the
// kind only 0.9.0 wrote. The rest is what a byte-identical compressor does
// not prove -- the CRC's vector, both decoders fed and drained a byte at a
// time, the flush, several streams in a row, and every way a stream can be
// refused.
#include "bzip2/bzip2.h"
#include "harness.h"
#include "kernel/alloc.h"
#include "kernel/string.h"

namespace {

struct Bzip2InputSum {
    u32 size;
    u32 crc;
};

struct Bzip2Case {
    u8 input;
    u8 level;
    u32 size;
    u32 crc;
};

#include "bzip2.data"

// ---------------------------------------------- mkbzip2data.py's inputs

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
constexpr u32 N_CASES  = sizeof BZIP2_CASES / sizeof BZIP2_CASES[0];

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

// The whole input in one Finish, into room for all of it.
bool compress_whole(Str in, int level, int work_factor, String &out)
{
    BzCompressor c;
    if (!c.init(level, work_factor))
        return false;
    Scratch buf(BzCompressor::bound(in.size()));
    if (!buf.p)
        return false;
    Span<const u8> src = bytes_of(in);
    Span<u8> dst(buf.p, buf.n);
    BzStatus s = c.step(src, dst, BzAction::Finish);
    out.clear();
    out.append(str_of(buf.p, buf.n - dst.size()));
    return s == BzStatus::End && src.empty() && c.total_in() == in.size() &&
           c.total_out() == out.size();
}

// Compresses feeding `in_step` bytes and offering `out_step` at a time.
bool compress_by(Str in, int level, usize in_step, usize out_step, String &out)
{
    BzCompressor c;
    if (!c.init(level))
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
        BzStatus s = c.step(src, dst, last ? BzAction::Finish : BzAction::Run);
        at += n - src.size();
        out.append(str_of(buf.p, room - dst.size()));
        if (s == BzStatus::End)
            return at == in.size();
        if (s != BzStatus::Ok && s != BzStatus::More && s != BzStatus::Stuck)
            return false;
    }
}

// Decompresses one stream feeding `in_step` bytes and offering `out_step`
// at a time. The status it ended on.
BzStatus decompress_by(Str in, bool small, usize in_step, usize out_step, String &out)
{
    BzDecompressor d;
    if (!d.init(small))
        return BzStatus::NoMemory;
    out.clear();
    Scratch buf(4096);
    usize at = 0;
    for (;;) {
        usize n            = in.size() - at < in_step ? in.size() - at : in_step;
        Span<const u8> src = bytes_of(in.substr(at, n));
        Span<u8> dst(buf.p, out_step < buf.n ? out_step : buf.n);
        usize room = dst.size();
        BzStatus s = d.step(src, dst);
        at += n - src.size();
        out.append(str_of(buf.p, room - dst.size()));
        if (s != BzStatus::Ok && !(s == BzStatus::Stuck && at < in.size()))
            return s;
    }
}

// What a stream decompresses to: the status, and why() if Corrupt.
String refusal(Str stream, bool small = false)
{
    BzDecompressor d;
    String why;
    if (!d.init(small))
        return why;
    Scratch buf(65536);
    Span<const u8> src = bytes_of(stream);
    for (;;) {
        Span<u8> dst(buf.p, buf.n);
        BzStatus s = d.step(src, dst);
        if (s == BzStatus::Corrupt || s == BzStatus::NotBzip2) {
            why.append(d.why());
            return why;
        }
        if (s != BzStatus::Ok) {
            why.append(s == BzStatus::End ? "end" : "stuck");
            return why;
        }
    }
}

} // namespace

void test_bzip2()
{
    test_begin("bzip2");

    usize in_use = heap_stats().bytes_in_use;

    // The CRC against its published vector (CRC-32/BZIP2), and a byte at a
    // time against the whole.
    CHECK_EQ(bz_crc_update(0, Str("123456789")), 0xfc891918);
    CHECK_EQ(bz_crc_update(0, Str("")), 0);
    {
        String s;
        noise(s, 9, 5000);
        u32 c = 0;
        for (usize i = 0; i < s.size(); i++)
            c = bz_crc_update(c, s.str().substr(i, 1));
        CHECK_EQ(c, bz_crc_update(0, s.str()));
    }

    {
        // The inputs are the ones mkbzip2data.py made.
        String inputs[N_INPUTS];
        for (u32 i = 0; i < N_INPUTS; i++) {
            inputs[i] = make_input(i);
            CHECK_EQ(inputs[i].size(), INPUT_SUMS[i].size);
            CHECK_EQ(bz_crc_update(0, inputs[i].str()), INPUT_SUMS[i].crc);
        }

        // The host's streams, byte for byte, and each decompressed back by one
        // decoder or the other.
        {
            u32 same = 0, back = 0;
            for (const Bzip2Case &c : BZIP2_CASES) {
                Str in = inputs[c.input].str();
                String packed;
                bool made = compress_whole(in, c.level, 0, packed);
                bool ok =
                    made && packed.size() == c.size && bz_crc_update(0, packed.str()) == c.crc;
                test_check(ok, "compress made the host's stream", __FILE_NAME__, __LINE__);
                if (!ok) {
                    CHECK_EQ(u32(&c - BZIP2_CASES), ~0U); // which case
                    CHECK_EQ(packed.size(), c.size);
                }
                same += ok;
                String out;
                bool small = (&c - BZIP2_CASES) % 2;
                back +=
                    decompress_by(packed.str(), small, packed.size(), 4096, out) == BzStatus::End &&
                    out.str() == in;
            }
            CHECK_EQ(same, N_CASES);
            CHECK_EQ(back, N_CASES);
        }

        // The work factor decides which sort runs, never what it gives: the
        // fallback sort against the main one.
        {
            const u32 which[]   = { 3, 4, 7, 8 };
            const int factors[] = { 1, 2, 30, 100, 250 };
            for (u32 i : which) {
                const Bzip2Case &c = BZIP2_CASES[i * 9 + 8]; // block size 9
                CHECK(c.input == i && c.level == 9);
                for (int wf : factors) {
                    String packed;
                    CHECK(compress_whole(inputs[i].str(), 9, wf, packed));
                    CHECK(packed.size() == c.size && bz_crc_update(0, packed.str()) == c.crc);
                }
            }
        }

        // A byte in and a byte out, each way, and other small steps: every state
        // must resume where it stopped, and the stream not depend on the steps.
        {
            Str rl   = inputs[6].str();
            Str text = inputs[3].str();
            String whole, packed, back;
            CHECK(compress_whole(rl, 3, 0, whole));
            CHECK(compress_by(rl, 3, 1, 1, packed));
            CHECK(packed.str() == whole.str());
            CHECK(decompress_by(packed.str(), false, 1, 1, back) == BzStatus::End);
            CHECK(back.str() == rl);
            CHECK(decompress_by(packed.str(), true, 1, 1, back) == BzStatus::End);
            CHECK(back.str() == rl);
            CHECK(compress_whole(text, 1, 0, whole));
            CHECK(compress_by(text, 1, 7, 3, packed));
            CHECK(packed.str() == whole.str());
            CHECK(decompress_by(packed.str(), true, 3, 7, back) == BzStatus::End);
            CHECK(back.str() == text);
            CHECK(decompress_by(packed.str(), false, 4096, 1, back) == BzStatus::End);
            CHECK(back.str() == text);
        }

        // bzip2 itself wrote this.
        {
            Str s3 = str_of(SAMPLE3, sizeof SAMPLE3);
            String out;
            for (int small = 0; small < 2; small++) {
                CHECK(decompress_by(s3, small, s3.size(), 4096, out) == BzStatus::End);
                CHECK_EQ(out.size(), SAMPLE3_PLAIN.size);
                CHECK_EQ(bz_crc_update(0, out.str()), SAMPLE3_PLAIN.crc);
            }
            CHECK(decompress_by(s3, true, 1, 1, out) == BzStatus::End);
            CHECK_EQ(bz_crc_update(0, out.str()), SAMPLE3_PLAIN.crc);
        }

        // A randomised block, as 0.9.0 wrote them, in both decoders.
        {
            Str r = str_of(RANDOMISED, sizeof RANDOMISED);
            CHECK_EQ(r[14] & 0x80, 0x80);
            String want, out;
            text(want, RANDOMISED_SEED, RANDOMISED_SIZE);
            CHECK(decompress_by(r, false, r.size(), 4096, out) == BzStatus::End);
            CHECK(out.str() == want.str());
            CHECK(decompress_by(r, true, 5, 3, out) == BzStatus::End);
            CHECK(out.str() == want.str());
        }

        // A flush ends the block where it is, and the whole is still one
        // stream: two blocks where there would have been one.
        {
            Str a = inputs[3].str().substr(0, 5000);
            Str b = inputs[3].str().substr(5000);
            BzCompressor c;
            CHECK(c.init(9).is_ok());
            Scratch buf(32768);
            Span<const u8> src = bytes_of(a);
            Span<u8> dst(buf.p, 16);
            CHECK(c.step(src, dst, BzAction::Run) == BzStatus::Ok);
            CHECK(src.empty() && dst.size() == 16); // all in the block, none out
            CHECK(c.step(src, dst, BzAction::Flush) == BzStatus::More);
            Span<const u8> other = bytes_of(b);
            CHECK(c.step(other, dst, BzAction::Flush) == BzStatus::Misuse); // input changed
            dst = Span<u8>(buf.p + 16, buf.n - 16);
            CHECK(c.step(src, dst, BzAction::Flush) == BzStatus::Ok);

            src = bytes_of(b);
            CHECK(c.step(src, dst, BzAction::Finish) == BzStatus::End);
            CHECK(c.step(src, dst, BzAction::Finish) == BzStatus::Misuse); // after End
            usize end = buf.n - dst.size();
            CHECK_EQ(c.total_in(), inputs[3].size());
            CHECK_EQ(c.total_out(), end);
            String out, one;
            CHECK(decompress_by(str_of(buf.p, end), true, end, 4096, out) == BzStatus::End);
            CHECK(out.str() == inputs[3].str());
            CHECK(compress_whole(inputs[3].str(), 9, 0, one));
            CHECK(end > one.size());
        }

        // Finish in pieces: More until the room suffices, and the input may not
        // change meanwhile.
        {
            BzCompressor c;
            CHECK(c.init(1).is_ok());
            Scratch buf(8192);
            Span<const u8> src = bytes_of(inputs[3].str());
            Span<u8> dst(buf.p, 100);
            CHECK(c.step(src, dst, BzAction::Finish) == BzStatus::More);
            CHECK(dst.empty());
            CHECK(c.step(src, dst, BzAction::Finish) == BzStatus::Stuck); // no room
            CHECK(c.step(src, dst, BzAction::Run) == BzStatus::Misuse);
            dst = Span<u8>(buf.p + 100, buf.n - 100);
            CHECK(c.step(src, dst, BzAction::Finish) == BzStatus::End);
            Result<String> r = bzip2_uncompress(str_of(buf.p, buf.n - dst.size()), 12000);
            CHECK(r.is_ok() && r.value().str() == inputs[3].str());
        }

        // The one-shots: streams in a row are one output, and what is not
        // another stream, a truncation, or more than the limit is refused.
        {
            Result<String> a = bzip2_compress(inputs[2].str(), 1);
            Result<String> b = bzip2_compress(inputs[9].str());
            CHECK(a.is_ok() && b.is_ok());
            String both, want;
            both.append(a.value().str());
            both.append(b.value().str());
            want.append(inputs[2].str());
            want.append(inputs[9].str());
            Result<String> r = bzip2_uncompress(both.str(), want.size());
            CHECK(r.is_ok() && r.value().str() == want.str());
            CHECK(bzip2_uncompress(both.str(), want.size() - 1).is_err());
            CHECK(bzip2_uncompress(both.str().substr(0, both.size() - 1), want.size()).is_err());
            both.append("x");
            CHECK(bzip2_uncompress(both.str(), want.size()).is_err());
            CHECK(bzip2_uncompress("", 100).is_err());
            CHECK(bzip2_compress("abc", 0).is_err());
            CHECK(bzip2_compress("abc", 10).is_err());

            Result<String> e = bzip2_compress("");
            CHECK(e.is_ok() && e.value().size() == 14);
            Result<String> back = bzip2_uncompress(e.value().str(), 0);
            CHECK(back.is_ok() && back.value().size() == 0);
        }

        // Every way a stream can be refused.
        {
            String good;
            CHECK(compress_whole(inputs[3].str(), 1, 0, good));
            CHECK(refusal(good.str()) == "end");

            CHECK(refusal("BZh0") == "not a bzip2 stream");
            CHECK(refusal("BZx9") == "not a bzip2 stream");
            CHECK(refusal("PK\3\4") == "not a bzip2 stream");
            CHECK(refusal("BZh9") == "stuck");

            String bad;
            bad.append(good.str());
            bad.data()[4] ^= 1; // the block magic
            CHECK(refusal(bad.str()) == "incorrect block header");

            bad.clear();
            bad.append(good.str());
            bad.data()[11] ^= 0x10; // the block CRC
            CHECK(refusal(bad.str()) == "incorrect block check");
            CHECK(refusal(bad.str(), true) == "incorrect block check");

            bad.clear();
            bad.append(good.str());
            bad.data()[bad.size() - 2] ^= 0x08; // the stream CRC
            CHECK(refusal(bad.str()) == "incorrect stream check");

            bad.clear();
            bad.append(good.str());
            bad.data()[15] = char(0xff); // the origin pointer, past the block
            CHECK(refusal(bad.str()) == "invalid origin pointer");

            // Somewhere in the tables or the codes: never taken for the stream,
            // whether refused or waiting on bits that never come.
            u32 refused = 0;
            for (usize at = 40; at < 400; at += 9) {
                bad.clear();
                bad.append(good.str());
                bad.data()[at] ^= 0x5a;
                refused += refusal(bad.str(), at % 2).str() != "end";
            }
            CHECK_EQ(refused, 40);

            // A stream refused stays refused.
            BzDecompressor d;
            CHECK(d.init().is_ok());
            Span<const u8> src = bytes_of("BZh0");
            Scratch buf(64);
            Span<u8> dst(buf.p, buf.n);
            CHECK(d.step(src, dst) == BzStatus::NotBzip2);
            src = bytes_of(good.str());
            CHECK(d.step(src, dst) == BzStatus::NotBzip2);
            CHECK(d.why() == "not a bzip2 stream");
        }

        // Parameters libbzip2 refuses, and calls with no stream.
        {
            BzCompressor c;
            BzDecompressor d;
            CHECK(c.init(0).is_err());
            CHECK(c.init(10).is_err());
            CHECK(c.init(9, -1).is_err());
            CHECK(c.init(9, 251).is_err());
            CHECK(!c.ready());
            Span<const u8> src;
            Span<u8> dst;
            CHECK(c.step(src, dst, BzAction::Run) == BzStatus::Misuse);
            CHECK(d.step(src, dst) == BzStatus::Misuse);
            CHECK(c.init(1, 0).is_ok());
            CHECK(c.step(src, dst, BzAction::Run) == BzStatus::Stuck);
            CHECK(d.init(true).is_ok());
            CHECK(d.step(src, dst) == BzStatus::Stuck);
        }
    }

    CHECK_EQ(heap_stats().bytes_in_use, in_use);
}
