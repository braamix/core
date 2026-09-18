// braam::zlib (src/zlib/). zlib.data holds what the host's own zlib made of
// eight inputs across the levels, strategies, windows and memory levels, and
// deflate must make the same bytes; the rest is what a byte-identical deflate
// does not prove -- the checksums' vectors, inflate fed and drained a byte at
// a time, the flushes, dictionaries, gzip headers, and every way a stream can
// be refused.
#include "harness.h"
#include "kernel/alloc.h"
#include "kernel/string.h"
#include "zlib/zlib.h"

namespace {

struct ZlibInputSum {
    u32 size;
    u32 crc;
};

struct ZlibCase {
    u8 input;
    i8 level;
    u8 strategy;
    i8 wbits; // zlib's windowBits: negative raw, past 15 gzip
    u8 mem_level;
    u32 size;
    u32 crc;
};

#include "zlib.data"

// ------------------------------------------------ mkzlibdata.py's inputs

const char *const WORDS[] = { "the",    "of",     "and",  "deflate", "window",   "a",
                              "stream", "to",     "in",   "block",   "Huffman",  "is",
                              "that",   "match",  "code", "for",     "length",   "distance",
                              "with",   "as",     "literal", "tree", "bits",     "on",
                              "by",     "hash",   "chain", "lazy",   "it",       "be",
                              "zlib",   "at" };

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

void mixed(String &out, u32 seed, usize n)
{
    for (u32 i = 0; out.size() < n; i++) {
        if (i % 2 == 0)
            text(out, seed + i, 3000);
        else
            noise(out, seed + i, 1000);
    }
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
        text(s, 2, 150000);
        break;
    case 5:
        noise(s, 3, 20000);
        break;
    case 6:
        runs(s, 4, 40000);
        break;
    case 7:
        mixed(s, 5, 90000);
        break;
    }
    return s;
}

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

ZFormat format_of(int wbits)
{
    return wbits < 0 ? ZFormat::Raw : wbits > 15 ? ZFormat::Gzip : ZFormat::Zlib;
}

u8 bits_of(int wbits)
{
    return u8(wbits < 0 ? -wbits : wbits > 15 ? wbits - 16 : wbits);
}

// The whole input in one step, into room for all of it: how level 0's blocks
// come out as mkzlibdata.py recorded them.
bool deflate_whole(Str in, int level, ZFormat f, u8 wbits, u8 mem, ZStrategy st, String &out)
{
    Deflater d;
    if (!d.init(level, f, wbits, mem, st))
        return false;
    usize room = d.bound(in.size());
    u8 *buf    = static_cast<u8 *>(heap_alloc(room ? room : 1));
    if (!buf)
        return false;
    Span<const u8> src = bytes_of(in);
    Span<u8> dst(buf, room);
    ZStatus s = d.step(src, dst, ZFlush::Finish);
    bool ok   = s == ZStatus::End && src.empty();
    out.clear();
    out.append(Str(reinterpret_cast<const char *>(buf), room - dst.size()));
    heap_free(buf);
    return ok;
}

// Inflates `in` feeding `in_step` bytes and offering `out_step` at a time.
ZStatus inflate_by(Str in, ZFormat f, usize in_step, usize out_step, String &out)
{
    Inflater inf;
    if (!inf.init(f, f == ZFormat::Raw ? 15 : 0))
        return ZStatus::Misuse;
    out.clear();
    Scratch buf_(4096);
    u8 *buf = buf_.p;
    usize at = 0;
    for (;;) {
        usize n              = in.size() - at < in_step ? in.size() - at : in_step;
        Span<const u8> src   = bytes_of(in.substr(at, n));
        Span<u8> dst(buf, out_step < buf_.n ? out_step : buf_.n);
        usize room = dst.size();
        ZStatus s  = inf.step(src, dst);
        at += n - src.size();
        out.append(Str(reinterpret_cast<const char *>(buf), room - dst.size()));
        if (s != ZStatus::Ok && !(s == ZStatus::Stuck && at < in.size()))
            return s;
    }
}

// Deflates feeding `in_step` bytes and offering `out_step` at a time.
bool deflate_by(Str in, int level, ZFormat f, ZStrategy st, usize in_step, usize out_step,
                String &out)
{
    Deflater d;
    if (!d.init(level, f, 15, 8, st))
        return false;
    out.clear();
    Scratch buf_(4096);
    u8 *buf = buf_.p;
    usize at = 0;
    for (;;) {
        usize n            = in.size() - at < in_step ? in.size() - at : in_step;
        bool last          = at + n == in.size();
        Span<const u8> src = bytes_of(in.substr(at, n));
        Span<u8> dst(buf, out_step < buf_.n ? out_step : buf_.n);
        usize room = dst.size();
        ZStatus s  = d.step(src, dst, last ? ZFlush::Finish : ZFlush::None);
        at += n - src.size();
        out.append(Str(reinterpret_cast<const char *>(buf), room - dst.size()));
        if (s == ZStatus::End)
            return at == in.size();
        if (s != ZStatus::Ok && s != ZStatus::Stuck)
            return false;
    }
}

bool round_trip(Str in, int level, ZFormat f, ZStrategy st)
{
    String packed;
    if (!deflate_whole(in, level, f, 15, 8, st, packed))
        return false;
    Result<String> back = zlib_uncompress(packed.str(), f, in.size());
    return back.is_ok() && back.value().str() == in;
}

// The message of the Corrupt a stream inflates to, or "" if it did not.
String refusal(Str stream, ZFormat f)
{
    Inflater inf;
    String why;
    if (!inf.init(f, f == ZFormat::Raw ? 15 : 0))
        return why;
    Scratch buf_(1024);
    u8 *buf = buf_.p;
    Span<const u8> src = bytes_of(stream);
    for (;;) {
        Span<u8> dst(buf, buf_.n);
        ZStatus s = inf.step(src, dst);
        if (s == ZStatus::Corrupt) {
            why.append(inf.why());
            return why;
        }
        if (s != ZStatus::Ok)
            return why;
    }
}

} // namespace

void test_zlib()
{
    test_begin("zlib");

    usize in_use = heap_stats().bytes_in_use;

    // The checksums against their published vectors.
    CHECK_EQ(crc32_update(0, Str("123456789")), 0xcbf43926);
    CHECK_EQ(crc32_update(0, Str("")), 0);
    CHECK_EQ(crc32_update(0, Str("The quick brown fox jumps over the lazy dog")), 0x414fa339);
    CHECK_EQ(adler32_update(1, Str("Wikipedia")), 0x11e60398);
    CHECK_EQ(adler32_update(1, Str("")), 1);

    // Byte at a time, at every alignment, against the whole: the CRC's four
    // tables and its tail, and Adler-32 past NMAX.
    {
        String s;
        noise(s, 9, 20000);
        u32 c = 0, a = 1;
        for (usize i = 0; i < s.size(); i++) {
            c = crc32_update(c, s.str().substr(i, 1));
            a = adler32_update(a, s.str().substr(i, 1));
        }
        CHECK_EQ(c, crc32_update(0, s.str()));
        CHECK_EQ(a, adler32_update(1, s.str()));
        for (usize off = 0; off < 4; off++)
            CHECK_EQ(crc32_update(crc32_update(0, s.str().substr(0, off)), s.str().substr(off)),
                     c);

        const usize cuts[] = { 0, 1, 5551, 5552, 13000 };
        for (usize cut : cuts) {
            Str x = s.str().substr(0, cut);
            Str y = s.str().substr(cut);
            CHECK_EQ(crc32_combine(crc32_update(0, x), crc32_update(0, y), y.size()), c);
            CHECK_EQ(adler32_combine(adler32_update(1, x), adler32_update(1, y), y.size()), a);
        }
    }

    // The inputs are the ones mkzlibdata.py made.
    for (u32 i = 0; i < sizeof INPUT_SUMS / sizeof INPUT_SUMS[0]; i++) {
        String in = make_input(i);
        CHECK_EQ(in.size(), INPUT_SUMS[i].size);
        CHECK_EQ(crc32_update(0, in.str()), INPUT_SUMS[i].crc);
    }

    // The host's streams, byte for byte, and each inflated back.
    {
        String inputs[sizeof INPUT_SUMS / sizeof INPUT_SUMS[0]];
        for (u32 i = 0; i < sizeof INPUT_SUMS / sizeof INPUT_SUMS[0]; i++)
            inputs[i] = make_input(i);
        u32 same = 0, back = 0;
        for (const ZlibCase &c : ZLIB_CASES) {
            Str in = inputs[c.input].str();
            ZFormat f = format_of(c.wbits);
            String packed;
            bool made = deflate_whole(in, c.level, f, bits_of(c.wbits), c.mem_level,
                                      ZStrategy(c.strategy), packed);
            bool ok = made && packed.size() == c.size && crc32_update(0, packed.str()) == c.crc;
            test_check(ok, "deflate made the host's stream", __FILE_NAME__, __LINE__);
            if (!ok) {
                CHECK_EQ(u32(&c - ZLIB_CASES), ~0U); // which case
                CHECK_EQ(packed.size(), c.size);
            }
            same += ok;
            Result<String> r = zlib_uncompress(packed.str(), f, in.size());
            back += r.is_ok() && r.value().str() == in;
        }
        CHECK_EQ(same, sizeof ZLIB_CASES / sizeof ZLIB_CASES[0]);
        CHECK_EQ(back, sizeof ZLIB_CASES / sizeof ZLIB_CASES[0]);
    }

    {
    String text12k = make_input(3);
    String mix     = make_input(7);

    // Every format, level and strategy, round trip.
    const ZFormat formats[] = { ZFormat::Raw, ZFormat::Zlib, ZFormat::Gzip };
    for (ZFormat f : formats)
        for (int level = 0; level <= 9; level++)
            for (u8 st = 0; st <= 4; st++)
                CHECK(round_trip(mix.str(), level, f, ZStrategy(st)));

    // A byte in and a byte out, each way, and every other pairing of small
    // steps: every state must resume where it stopped.
    {
        String packed, back;
        CHECK(deflate_by(text12k.str(), 6, ZFormat::Zlib, ZStrategy::Default, 1, 1, packed));
        CHECK(inflate_by(packed.str(), ZFormat::Zlib, 1, 1, back) == ZStatus::End);
        CHECK(back.str() == text12k.str());

        CHECK(deflate_by(mix.str(), 0, ZFormat::Gzip, ZStrategy::Default, 7, 3, packed));
        CHECK(inflate_by(packed.str(), ZFormat::Auto, 3, 7, back) == ZStatus::End);
        CHECK(back.str() == mix.str());

        CHECK(deflate_by(mix.str(), 1, ZFormat::Raw, ZStrategy::Rle, 1000, 1, packed));
        CHECK(inflate_by(packed.str(), ZFormat::Raw, 1, 4096, back) == ZStatus::End);
        CHECK(back.str() == mix.str());

        CHECK(deflate_by(mix.str(), 9, ZFormat::Zlib, ZStrategy::Huffman, 1, 4096, packed));
        CHECK(inflate_by(packed.str(), ZFormat::Zlib, 4096, 1, back) == ZStatus::End);
        CHECK(back.str() == mix.str());

        // Levels 1..9 do not depend on how the input arrives.
        String whole;
        CHECK(deflate_whole(mix.str(), 6, ZFormat::Zlib, 15, 8, ZStrategy::Default, whole));
        CHECK(deflate_by(mix.str(), 6, ZFormat::Zlib, ZStrategy::Default, 1, 1, packed));
        CHECK(packed.str() == whole.str());
    }

    // Auto takes zlib and gzip alike; Raw of a zlib stream is refused.
    {
        String z, g, back;
        CHECK(deflate_whole(text12k.str(), 6, ZFormat::Zlib, 15, 8, ZStrategy::Default, z));
        CHECK(deflate_whole(text12k.str(), 6, ZFormat::Gzip, 15, 8, ZStrategy::Default, g));
        CHECK(inflate_by(z.str(), ZFormat::Auto, 4096, 4096, back) == ZStatus::End);
        CHECK(back.str() == text12k.str());
        CHECK(inflate_by(g.str(), ZFormat::Auto, 4096, 4096, back) == ZStatus::End);
        CHECK(back.str() == text12k.str());
        CHECK(inflate_by(g.str(), ZFormat::Zlib, 4096, 4096, back) == ZStatus::Corrupt);
        CHECK(inflate_by(z.str(), ZFormat::Gzip, 4096, 4096, back) == ZStatus::Corrupt);
    }

    // A sync flush makes everything so far inflatable, and a full flush is a
    // place inflate may start from cold.
    {
        Deflater d;
        CHECK(d.init(6, ZFormat::Zlib).is_ok());
        Scratch buf_(32768);
        u8 *buf = buf_.p;
        Str a = text12k.str().substr(0, 5000);
        Str b = text12k.str().substr(5000);

        Span<const u8> src = bytes_of(a);
        Span<u8> dst(buf, buf_.n);
        CHECK(d.step(src, dst, ZFlush::Sync) == ZStatus::Ok);
        usize cut = buf_.n - dst.size();
        CHECK(cut >= 4 && buf[cut - 4] == 0 && buf[cut - 3] == 0 && buf[cut - 2] == 0xff &&
              buf[cut - 1] == 0xff);

        Inflater inf;
        CHECK(inf.init().is_ok());
        Scratch got_(8192);
        u8 *got = got_.p;
        Span<const u8> isrc(buf, cut);
        Span<u8> idst(got, got_.n);
        CHECK(inf.step(isrc, idst) == ZStatus::Ok);
        CHECK(Str(reinterpret_cast<char *>(got), got_.n - idst.size()) == a);

        CHECK(d.step(src, dst, ZFlush::Sync) == ZStatus::Stuck); // nothing new to flush
        CHECK(d.step(src, dst, ZFlush::Full) == ZStatus::Ok);
        usize full = buf_.n - dst.size();
        CHECK(full > cut && buf[full - 2] == 0xff && buf[full - 1] == 0xff);
        src = bytes_of(b);
        CHECK(d.step(src, dst, ZFlush::Finish) == ZStatus::End);
        usize end = buf_.n - dst.size();

        Inflater cold;
        CHECK(cold.init().is_ok());
        Span<const u8> from(buf + full - 4, end - (full - 4));
        CHECK(cold.sync(from) == ZStatus::Ok);
        String tail;
        Scratch out_(8192);
        u8 *out = out_.p;
        for (;;) {
            Span<u8> o(out, out_.n);
            ZStatus s = cold.step(from, o);
            tail.append(Str(reinterpret_cast<char *>(out), out_.n - o.size()));
            if (s != ZStatus::Ok)
                break;
        }
        CHECK(tail.str() == b);
    }

    // A preset dictionary: NeedDict with its Adler-32, the wrong one refused.
    {
        Str dict = "deflate window stream block Huffman match distance literal";
        Str in   = "the deflate window and the stream, a Huffman block of literal match";
        Deflater d;
        CHECK(d.init(9, ZFormat::Zlib).is_ok());
        CHECK(d.set_dictionary(bytes_of(dict)) == ZStatus::Ok);
        Scratch buf_(512);
        u8 *buf = buf_.p;
        Span<const u8> src = bytes_of(in);
        Span<u8> dst(buf, buf_.n);
        CHECK(d.step(src, dst, ZFlush::Finish) == ZStatus::End);
        Str stream(reinterpret_cast<char *>(buf), buf_.n - dst.size());

        String plain;
        CHECK(deflate_whole(in, 9, ZFormat::Zlib, 15, 8, ZStrategy::Default, plain));
        CHECK(stream.size() < plain.size());

        Inflater inf;
        CHECK(inf.init().is_ok());
        Scratch out_(512);
        u8 *out = out_.p;
        Span<const u8> isrc = bytes_of(stream);
        Span<u8> idst(out, out_.n);
        CHECK(inf.step(isrc, idst) == ZStatus::NeedDict);
        CHECK_EQ(inf.check(), adler32_update(1, dict));
        CHECK(inf.set_dictionary(bytes_of("not it")) == ZStatus::Corrupt);
        CHECK(inf.set_dictionary(bytes_of(dict)) == ZStatus::Ok);
        CHECK(inf.step(isrc, idst) == ZStatus::End);
        CHECK(Str(reinterpret_cast<char *>(out), out_.n - idst.size()) == in);

        // Raw: the same dictionary, set before the first step on both sides;
        // without it, a match reaches back before the stream began.
        Deflater rd;
        CHECK(rd.init(9, ZFormat::Raw).is_ok());
        CHECK(rd.set_dictionary(bytes_of(dict)) == ZStatus::Ok);
        src = bytes_of(in);
        dst = Span<u8>(buf, buf_.n);
        CHECK(rd.step(src, dst, ZFlush::Finish) == ZStatus::End);
        Str raw(reinterpret_cast<char *>(buf), buf_.n - dst.size());

        Inflater ri;
        CHECK(ri.init(ZFormat::Raw).is_ok());
        CHECK(ri.set_dictionary(bytes_of(dict)) == ZStatus::Ok);
        isrc = bytes_of(raw);
        idst = Span<u8>(out, out_.n);
        CHECK(ri.step(isrc, idst) == ZStatus::End);
        CHECK(Str(reinterpret_cast<char *>(out), out_.n - idst.size()) == in);
        CHECK(refusal(raw, ZFormat::Raw) == "invalid distance too far back");

        u8 back[128];
        CHECK_EQ(ri.get_dictionary(Span<u8>(back)), dict.size() + in.size());
        CHECK_EQ(rd.get_dictionary(Span<u8>(back)), dict.size() + in.size());
        CHECK(Str(reinterpret_cast<char *>(back), 16) == dict.substr(0, 16));
    }

    // A new level mid-stream.
    {
        Deflater d;
        CHECK(d.init(1, ZFormat::Zlib).is_ok());
        String packed;
        Scratch buf_(65536);
        u8 *buf = buf_.p;
        Span<const u8> src = bytes_of(mix.str().substr(0, 40000));
        Span<u8> dst(buf, buf_.n);
        CHECK(d.step(src, dst, ZFlush::None) == ZStatus::Ok);
        CHECK(d.params(9, ZStrategy::Filtered, src, dst) == ZStatus::Ok);
        CHECK_EQ(d.level(), 9);
        src = bytes_of(mix.str().substr(40000));
        while (d.step(src, dst, ZFlush::Finish) == ZStatus::Ok) {
            packed.append(Str(reinterpret_cast<char *>(buf), buf_.n - dst.size()));
            dst = Span<u8>(buf, buf_.n);
        }
        packed.append(Str(reinterpret_cast<char *>(buf), buf_.n - dst.size()));
        Result<String> r = zlib_uncompress(packed.str(), ZFormat::Zlib, mix.size());
        CHECK(r.is_ok() && r.value().str() == mix.str());
    }

    // A gzip header's fields, written and read back, with its own CRC.
    {
        u8 extra[] = { 'B', 'r', 4, 0, 1, 2, 3, 4 };
        u8 name[]  = "hello.txt";
        u8 note[]  = "a comment";
        ZHeader h;
        h.text      = 1;
        h.time      = 1234567890;
        h.extra     = extra;
        h.extra_len = sizeof extra;
        h.name      = name;
        h.comment   = note;
        h.hcrc      = 1;

        Deflater d;
        CHECK(d.init(6, ZFormat::Gzip).is_ok());
        CHECK(d.set_header(&h) == ZStatus::Ok);
        Scratch buf_(32768);
        u8 *buf = buf_.p;
        Span<const u8> src = bytes_of(text12k.str());
        Span<u8> dst(buf, buf_.n);
        CHECK(d.step(src, dst, ZFlush::Finish) == ZStatus::End);
        usize len = buf_.n - dst.size();
        CHECK(len <= d.bound(text12k.size()));
        CHECK_EQ(d.data_type(), 1);
        CHECK_EQ(buf[3], 0x1f); // FTEXT FHCRC FEXTRA FNAME FCOMMENT

        u8 rextra[4], rname[32], rnote[4];
        ZHeader r;
        r.extra     = rextra;
        r.extra_max = sizeof rextra;
        r.name      = rname;
        r.name_max  = sizeof rname;
        r.comment   = rnote;
        r.comm_max  = sizeof rnote;
        Inflater inf;
        CHECK(inf.init(ZFormat::Gzip).is_ok());
        CHECK(inf.set_header(&r) == ZStatus::Ok);
        String back;
        Scratch out_(16384);
        u8 *out = out_.p;
        Span<const u8> isrc(buf, len);
        Span<u8> idst(out, out_.n);
        CHECK(inf.step(isrc, idst) == ZStatus::End);
        CHECK(Str(reinterpret_cast<char *>(out), out_.n - idst.size()) == text12k.str());
        CHECK_EQ(r.done, 1);
        CHECK_EQ(r.text, 1);
        CHECK_EQ(r.time, 1234567890);
        CHECK_EQ(r.os, 3);
        CHECK_EQ(r.hcrc, 1);
        CHECK_EQ(r.extra_len, 8);
        CHECK(Str(reinterpret_cast<char *>(rextra), 4) == "Br\4\0"_s);
        CHECK(Str(reinterpret_cast<char *>(rname), 10) == "hello.txt\0"_s);
        CHECK(Str(reinterpret_cast<char *>(rnote), 4) == "a co");

        // The header CRC is checked: one flipped bit in the name.
        buf[20] ^= 1;
        CHECK(refusal(Str(reinterpret_cast<char *>(buf), len), ZFormat::Gzip) ==
              "header crc mismatch");
    }

    // Refusals, each with zlib's message.
    {
        CHECK(refusal("\x78\x9d\x01"_s, ZFormat::Zlib) == "incorrect header check");
        CHECK(refusal("\x79\x18"_s, ZFormat::Zlib) == "unknown compression method");
        CHECK(refusal("\x07"_s, ZFormat::Raw) == "invalid block type");
        CHECK(refusal("\x01\x05\x00\x00\x00"_s, ZFormat::Raw) == "invalid stored block lengths");
        CHECK(refusal("\x1f\x8b\x07\x00"_s, ZFormat::Gzip) == "unknown compression method");

        String z, g;
        CHECK(deflate_whole(text12k.str(), 6, ZFormat::Zlib, 15, 8, ZStrategy::Default, z));
        CHECK(deflate_whole(text12k.str(), 6, ZFormat::Gzip, 15, 8, ZStrategy::Default, g));
        z[z.size() - 1] ^= 1;
        CHECK(refusal(z.str(), ZFormat::Zlib) == "incorrect data check");
        g[g.size() - 5] ^= 1;
        CHECK(refusal(g.str(), ZFormat::Gzip) == "incorrect data check");
        g[g.size() - 5] ^= 1;
        g[g.size() - 1] ^= 1;
        CHECK(refusal(g.str(), ZFormat::Gzip) == "incorrect length check");

        // Unchecked, the same stream is taken.
        Inflater lax;
        CHECK(lax.init(ZFormat::Zlib).is_ok());
        CHECK(lax.validate(false) == ZStatus::Ok);
        z[z.size() - 1] ^= 1;
        z[z.size() - 2] ^= 1;
        Scratch out_(16384);
        u8 *out = out_.p;
        Span<const u8> src = bytes_of(z.str());
        Span<u8> dst(out, out_.n);
        CHECK(lax.step(src, dst, ZFlush::Finish) == ZStatus::End);

        // Truncated: Stuck under Finish, never End, and an error one-shot.
        z[z.size() - 2] ^= 1;
        Str cut = z.str().substr(0, z.size() - 3);
        Inflater t;
        CHECK(t.init().is_ok());
        src = bytes_of(cut);
        dst = Span<u8>(out, out_.n);
        CHECK(t.step(src, dst, ZFlush::Finish) == ZStatus::Stuck);
        CHECK(zlib_uncompress(cut, ZFormat::Zlib, 1 << 20).is_err());
    }

    // The limit: a stream that inflates to one byte more is refused.
    {
        String a;
        for (int i = 0; i < 100000; i++)
            a.push('a');
        Result<String> z = zlib_compress(a.str(), ZFormat::Zlib, 9);
        CHECK(z.is_ok() && z.value().size() < 200);
        CHECK(zlib_uncompress(z.value().str(), ZFormat::Zlib, 99999).is_err());
        Result<String> r = zlib_uncompress(z.value().str(), ZFormat::Zlib, 100000);
        CHECK(r.is_ok() && r.value().str() == a.str());
        CHECK(zlib_compress(a.str(), ZFormat::Auto).is_err());
        CHECK(zlib_compress(a.str(), ZFormat::Zlib, 10).is_err());
    }

    // The bounds hold, for every level, on data that does not compress.
    {
        String n;
        noise(n, 11, 70000);
        for (int level = 0; level <= 9; level++) {
            String p;
            CHECK(deflate_whole(n.str(), level, ZFormat::Gzip, 15, 8, ZStrategy::Default, p));
            CHECK(p.size() <= Deflater::bound_any(n.size()));
            CHECK(deflate_whole(n.str(), level, ZFormat::Zlib, 9, 1, ZStrategy::Default, p));
            Deflater d;
            CHECK(d.init(level, ZFormat::Zlib, 9, 1).is_ok());
            CHECK(p.size() <= d.bound(n.size()));
        }
    }

    // A copy mid-stream carries on as the original does.
    {
        Deflater d;
        CHECK(d.init(6, ZFormat::Gzip).is_ok());
        Scratch b1_(65536);
        u8 *b1 = b1_.p;
        Scratch b2_(65536);
        u8 *b2 = b2_.p;
        Span<const u8> src = bytes_of(mix.str().substr(0, 50000));
        Span<u8> dst(b1, b1_.n);
        CHECK(d.step(src, dst, ZFlush::None) == ZStatus::Ok);
        usize head = b1_.n - dst.size();
        __builtin_memcpy(b2, b1, head);
        Deflater e;
        CHECK(e.copy_from(d) == ZStatus::Ok);
        Span<const u8> s1 = bytes_of(mix.str().substr(50000)), s2 = s1;
        Span<u8> d1(b1 + head, b1_.n - head), d2(b2 + head, b2_.n - head);
        CHECK(d.step(s1, d1, ZFlush::Finish) == ZStatus::End);
        CHECK(e.step(s2, d2, ZFlush::Finish) == ZStatus::End);
        CHECK(d1.size() == d2.size());
        usize len = b1_.n - d1.size();
        CHECK(Str(reinterpret_cast<char *>(b1), len) == Str(reinterpret_cast<char *>(b2), len));
        CHECK(e.end() == ZStatus::Ok);

        Inflater i;
        CHECK(i.init(ZFormat::Gzip).is_ok());
        String o1, o2;
        Scratch out_(30000);
        u8 *out = out_.p;
        Span<const u8> rest(b1, len);
        Span<u8> od(out, out_.n);
        CHECK(i.step(rest, od) == ZStatus::Ok);
        CHECK(od.empty());
        o1.append(Str(reinterpret_cast<char *>(out), out_.n));
        o2.append(o1.str());
        Inflater j;
        CHECK(j.copy_from(i) == ZStatus::Ok);
        Inflater *both[] = { &i, &j };
        String *outs[]   = { &o1, &o2 };
        for (int k = 0; k < 2; k++) {
            Span<const u8> r = rest;
            for (;;) {
                Span<u8> o(out, out_.n);
                ZStatus st = both[k]->step(r, o);
                outs[k]->append(Str(reinterpret_cast<char *>(out), out_.n - o.size()));
                if (st != ZStatus::Ok)
                    break;
            }
        }
        CHECK(o1.str() == mix.str());
        CHECK(o2.str() == mix.str());

        Deflater unfinished;
        CHECK(unfinished.init().is_ok());
        src = bytes_of(mix.str());
        dst = Span<u8>(b1, 16);
        CHECK(unfinished.step(src, dst, ZFlush::None) == ZStatus::Ok);
        CHECK(unfinished.end() == ZStatus::Corrupt);
    }

    // Parameters zlib refuses.
    {
        Deflater d;
        Inflater i;
        CHECK(d.init(10).is_err());
        CHECK(d.init(6, ZFormat::Zlib, 16).is_err());
        CHECK(d.init(6, ZFormat::Raw, 8).is_err());
        CHECK(d.init(6, ZFormat::Zlib, 15, 10).is_err());
        CHECK(d.init(6, ZFormat::Auto).is_err());
        CHECK(i.init(ZFormat::Zlib, 7).is_err());
        CHECK(i.init(ZFormat::Raw, 0).is_err());
        CHECK(!i.ready());
        Span<const u8> src;
        Span<u8> dst;
        CHECK(i.step(src, dst) == ZStatus::Misuse);
        CHECK(d.init(6, ZFormat::Zlib, 8).is_ok());
    }
    }

    CHECK_EQ(heap_stats().bytes_in_use, in_use);
}
