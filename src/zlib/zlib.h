// zlib's deflate and inflate, and its two checksums. `braam::zlib`, asked for
// by name:
//
//     braam_add_program(NAME zpipe SOURCES zpipe.cpp LIBS braam::zlib)
//
// A C++ rewrite of zlib 1.3.2.1 by Jean-loup Gailly and Mark Adler, altered
// from the original and under its licence (LICENSE, beside this file). The
// algorithms are theirs, step for step: deflate's output is zlib's, byte for
// byte, for the same level, strategy, window and memory level. A PORT target
// reaches the same code as <zlib.h>, zlib's own API.
//
// Formats: raw deflate (RFC 1951), the zlib wrapper (RFC 1950) and the gzip
// one (RFC 1952). Not here: gzopen() and the rest of gz*, which are files and
// not streams.
//
// Memory, all of it from the heap and none of it in a frame: an Inflater is
// about 7 KiB plus a window of 2^window_bits bytes, made on first need. A
// Deflater is 2^(window_bits+2) + 2^(mem_level+9) bytes plus about 6 KiB --
// 262 KiB at the defaults, 15 and 8.
#pragma once

#include "kernel/result.h"
#include "kernel/span.h"
#include "kernel/str.h"
#include "kernel/string.h"
#include "kernel/types.h"

// The two checks. crc32_update(0, s) is the CRC-32 of s; adler32_update
// starts from 1.
u32 crc32_update(u32 crc, Bytes bytes);
u32 adler32_update(u32 adler, Bytes bytes);

inline u32 crc32_update(u32 crc, Str s)
{
    return crc32_update(crc, Bytes(reinterpret_cast<const u8 *>(s.data()), s.size()));
}

inline u32 adler32_update(u32 adler, Str s)
{
    return adler32_update(adler, Bytes(reinterpret_cast<const u8 *>(s.data()), s.size()));
}

// The check of A followed by B, from the checks of each and B's length.
u32 crc32_combine(u32 crc_a, u32 crc_b, u64 len_b);
u32 adler32_combine(u32 adler_a, u32 adler_b, u64 len_b);

// crc32_combine in two halves, the first depending on the length alone.
u32 crc32_combine_gen(u64 len_b);
u32 crc32_combine_op(u32 crc_a, u32 crc_b, u32 op);

enum class ZFormat : u8 {
    Raw,  // bare deflate, no header and no check
    Zlib, // RFC 1950: two bytes of header, Adler-32 trailer
    Gzip, // RFC 1952: ten bytes and more of header, CRC-32 and length trailer
    Auto, // inflate only: zlib or gzip, by the header
};

// zlib's numbering, so that the port kit converts by cast.
enum class ZFlush : u8 {
    None,    // as much as the buffers allow
    Partial, // what is pending, and an empty fixed block
    Sync,    // what is pending, byte-aligned, and an empty stored block
    Full,    // Sync, and forget the history: inflate may restart here
    Finish,  // the end of the stream
    Block,   // deflate: to the end of this block; inflate: stop at one
    Trees,   // inflate: stop at a block's end or after its header
};

enum class ZStatus : u8 {
    Ok,       // progress; call again
    End,      // the stream is complete
    NeedDict, // inflate: a zlib stream wants set_dictionary() first
    Stuck,    // no progress was possible: more input or more room is wanted
    Corrupt,  // the input is not a stream of this format; why() says how
    NoMemory,
    Misuse, // the call the stream's state forbids
};

// zlib's numbering again.
enum class ZStrategy : u8 {
    Default,
    Filtered, // fewer short matches: for data a filter left mostly small
    Huffman,  // no matches at all
    Rle,      // matches at distance one only
    Fixed,    // no dynamic trees
};

// A gzip header, read by an Inflater and written by a Deflater. zlib's own
// gz_header, field for field, and the caller's: the stream keeps a pointer to
// it. The three buffers are the caller's too, and may be null.
struct ZHeader {
    i32 text   = 0; // the data is probably text
    u32 time   = 0; // modification time, Unix seconds
    i32 xflags = 0; // extra flags; a Deflater writes its own
    i32 os     = 3; // the operating system; 3 is Unix

    u8 *extra     = nullptr; // the extra field
    u32 extra_len = 0;       // its length as the stream has it
    u32 extra_max = 0;       // inflate: room at `extra`

    u8 *name     = nullptr; // NUL-terminated file name
    u32 name_max = 0;       // inflate: room at `name`

    u8 *comment  = nullptr; // NUL-terminated comment
    u32 comm_max = 0;       // inflate: room at `comment`

    i32 hcrc = 0; // a header CRC is, or was, present
    i32 done = 0; // inflate: 1 once the header is read; -1 if not gzip
};

struct InflateState;
struct DeflateState;

// A decompressor. Move-only: its state is one heap block and its window a
// second, both freed with it.
struct Inflater {
    Inflater() = default;
    Inflater(Inflater &&o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    Inflater &operator=(Inflater &&o) noexcept;
    Inflater(const Inflater &)            = delete;
    Inflater &operator=(const Inflater &) = delete;
    ~Inflater();

    // window_bits is 8..15, or 0 for Zlib and Auto to take the header's.
    // Err(Invalid) for a combination zlib refuses, Err(NoMemory).
    Result<void> init(ZFormat format = ZFormat::Zlib, u8 window_bits = 15);

    bool ready() const { return s_ != nullptr; }

    // Consumes from `in` and fills `out`, advancing both past what it used.
    // Stuck with Finish means the stream did not end in what was given.
    ZStatus step(Span<const u8> &in, Span<u8> &out, ZFlush flush = ZFlush::None);

    // Back to the start of a stream, keeping the window's allocation; the
    // second form changes the format and window as well.
    ZStatus reset();
    ZStatus reset(ZFormat format, u8 window_bits);

    // After NeedDict, or before the first step of a Raw stream. Corrupt when
    // it is not the dictionary the stream names.
    ZStatus set_dictionary(Bytes dict);

    // The window so far, most recent last. Copies at most out.size() bytes
    // and returns the whole of its length.
    usize get_dictionary(Span<u8> out) const;

    // Where the gzip header goes, as it is read. The stream keeps the pointer.
    ZStatus set_header(ZHeader *head);

    // Skips to the next full-flush point in `in`. Ok once one is found,
    // Corrupt when `in` ran out first, Stuck when there was nothing to search.
    ZStatus sync(Span<const u8> &in);

    // True at the end of a block made by a Sync or Full flush.
    bool sync_point() const;

    // Inserts bits ahead of the next input, as zlib's inflatePrime; bits < 0
    // empties the bit buffer instead.
    ZStatus prime(i32 bits, i32 value);

    // Whether the check value is compared. On by default.
    ZStatus validate(bool check);

    // zlib's inflateMark: the position within a block, for random access.
    i32 mark() const;

    // Table entries used, for testing: at most 1444.
    u32 codes_used() const;

    // A second stream in the same place, window and all.
    ZStatus copy_from(const Inflater &src);

    u64 total_in() const;
    u64 total_out() const;

    // Adler-32 or CRC-32 of the output so far; a zlib stream's dictionary id
    // after NeedDict.
    u32 check() const;

    // Bits unused in the last byte, plus 64 in the last block, 128 at a
    // block's end, and 256 at a block header's end: zlib's data_type.
    i32 data_type() const;

    // Why the last call went wrong, or empty. A literal: data() is
    // NUL-terminated.
    Str why() const;

private:
    InflateState *s_ = nullptr;
};

// A compressor. Move-only, as an Inflater is.
struct Deflater {
    Deflater() = default;
    Deflater(Deflater &&o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    Deflater &operator=(Deflater &&o) noexcept;
    Deflater(const Deflater &)            = delete;
    Deflater &operator=(const Deflater &) = delete;
    ~Deflater();

    // level 0..9, or -1 for 6; window_bits 8..15, where 8 is taken as 9 and
    // is for Zlib alone; mem_level 1..9. Err(Invalid) past those.
    Result<void> init(int level = 6, ZFormat format = ZFormat::Zlib, u8 window_bits = 15,
                      u8 mem_level = 8, ZStrategy strategy = ZStrategy::Default);

    bool ready() const { return s_ != nullptr; }

    // Consumes from `in` and fills `out`, advancing both. End once Finish has
    // put out the whole stream; until then Ok, with out full.
    ZStatus step(Span<const u8> &in, Span<u8> &out, ZFlush flush);

    // Back to the start of a stream with the same parameters. reset_keep
    // keeps the history and the hash, as zlib's deflateResetKeep.
    ZStatus reset();
    ZStatus reset_keep();

    // A new level or strategy mid-stream. What is buffered is compressed
    // under the old one first, which may need `out`; Stuck when it did not
    // all fit.
    ZStatus params(int level, ZStrategy strategy, Span<const u8> &in, Span<u8> &out);

    // Before the first step, or after a reset; not for Gzip.
    ZStatus set_dictionary(Bytes dict);

    // The history so far, at most out.size() bytes of it; returns its length.
    usize get_dictionary(Span<u8> out) const;

    // The gzip header to write; Misuse for another format. Kept by pointer.
    ZStatus set_header(ZHeader *head);

    // Bytes and bits produced but not yet handed to `out`.
    ZStatus pending(u32 *bytes, i32 *bits) const;

    // Bits used in the last byte of the last complete block.
    i32 used_bits() const;

    // Inserts up to 16 bits into the output, as zlib's deflatePrime.
    ZStatus prime(i32 bits, i32 value);

    // The four matching parameters, as zlib's deflateTune.
    ZStatus tune(u32 good_length, u32 max_lazy, u32 nice_length, u32 max_chain);

    // The most `len` bytes can compress to under these parameters.
    usize bound(usize len) const;

    // The same for any parameters, and any wrapper up to gzip's.
    static usize bound_any(usize len);

    ZStatus copy_from(const Deflater &src);

    // Frees the state now. Corrupt when the stream was not finished, which
    // the destructor does not say.
    ZStatus end();

    u64 total_in() const;
    u64 total_out() const;

    // Adler-32 or CRC-32 of the input so far.
    u32 check() const;

    // 0 binary, 1 text, 2 not yet known: a guess from the first block.
    i32 data_type() const;

    Str why() const;

    // The level, before or after params().
    int level() const;

private:
    DeflateState *s_ = nullptr;
};

// The whole of `bytes`, compressed. Err(NoMemory), or Err(Invalid) for a
// level or format that is not one.
Result<String> zlib_compress(Str bytes, ZFormat format = ZFormat::Zlib, int level = 6);

// The whole of a stream, decompressed, and never more than `limit` bytes of
// it: a small input may claim a large output. Err(Invalid) for a stream that
// is corrupt, truncated, or longer than the limit.
Result<String> zlib_uncompress(Str bytes, ZFormat format, usize limit);
