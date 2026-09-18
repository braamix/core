// bzip2's compressor and decompressor. `braam::bzip2`, asked for by name:
//
//     braam_add_program(NAME bzpipe SOURCES bzpipe.cpp LIBS braam::bzip2)
//
// A C++ rewrite of libbzip2 1.0.8 by Julian Seward, altered from the original
// and under its licence (LICENSE, beside this file). The algorithms are his,
// step for step: the output is libbzip2's, byte for byte, for the same block
// size. A PORT target reaches the same code as <bzlib.h>, libbzip2's own API.
//
// Not here: BZ2_bzopen, BZ2_bzRead and the rest of the stdio half, which are
// files and not streams; and verbosity, which printed to stderr.
//
// Memory, all of it from the heap and none of it in a frame: a compressor is
// 800,000 bytes per unit of block size plus 384 KiB, 7.6 MB at block size 9.
// A decompressor is 400,000 bytes per unit plus 64 KiB, 3.7 MB at 9, or
// 250,000 per unit in small mode, 2.4 MB, which is half as fast.
#pragma once

#include "kernel/result.h"
#include "kernel/span.h"
#include "kernel/str.h"
#include "kernel/string.h"
#include "kernel/types.h"

// bzip2's CRC-32, most significant bit first, which is not zlib's.
// bz_crc_update(0, s) is the CRC of s.
u32 bz_crc_update(u32 crc, Bytes bytes);

inline u32 bz_crc_update(u32 crc, Str s)
{
    return bz_crc_update(crc, Bytes(reinterpret_cast<const u8 *>(s.data()), s.size()));
}

// libbzip2's numbering, so that the port kit converts by cast.
enum class BzAction : u8 {
    Run,    // as much as the buffers allow
    Flush,  // end the block here; not a sync point, its last bits wait for the next
    Finish, // the end of the stream
};

enum class BzStatus : u8 {
    Ok,       // progress; call again. A Flush that has finished is Ok too
    More,     // a Flush or Finish is under way: call again with more room, the
              // same action, and the input as the last call left it
    End,      // the stream is complete
    Stuck,    // no progress was possible: more input or more room is wanted
    Corrupt,  // the input is not a bzip2 stream after all; why() says how
    NotBzip2, // the first four bytes are not "BZh1" to "BZh9"
    NoMemory,
    Misuse, // the call the stream's state forbids, or an internal check failed
};

struct BzEncodeState;
struct BzDecodeState;

// A compressor. Move-only: its state is one heap block and its sort arrays
// three more, all freed with it.
struct BzCompressor {
    BzCompressor() = default;
    BzCompressor(BzCompressor &&o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    BzCompressor &operator=(BzCompressor &&o) noexcept;
    BzCompressor(const BzCompressor &)            = delete;
    BzCompressor &operator=(const BzCompressor &) = delete;
    ~BzCompressor();

    // block_size_100k 1..9, the block in units of 100,000 bytes. work_factor
    // 0..250, 0 meaning 30: how hard the fast sort tries before it gives way
    // to the slow one. The output is the same either way. Err(Invalid) past
    // those, Err(NoMemory).
    Result<void> init(int block_size_100k = 9, int work_factor = 30);

    bool ready() const { return s_ != nullptr; }

    // Consumes from `in` and fills `out`, advancing both. Run is Ok or Stuck;
    // Flush is More until the block is out, then Ok; Finish is More until
    // the stream is out, then End. Misuse for a Flush or Finish whose input
    // changed between calls, a different action before one is done, or any
    // call after End.
    BzStatus step(Span<const u8> &in, Span<u8> &out, BzAction action);

    u64 total_in() const;
    u64 total_out() const;

    // The most `len` bytes can compress to, stream header and trailer
    // included.
    static usize bound(usize len);

private:
    BzEncodeState *s_ = nullptr;
};

// A decompressor. Move-only, as a compressor is.
struct BzDecompressor {
    BzDecompressor() = default;
    BzDecompressor(BzDecompressor &&o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    BzDecompressor &operator=(BzDecompressor &&o) noexcept;
    BzDecompressor(const BzDecompressor &)            = delete;
    BzDecompressor &operator=(const BzDecompressor &) = delete;
    ~BzDecompressor();

    // A second init starts a new stream. `small` trades speed for memory.
    // Err(NoMemory).
    Result<void> init(bool small = false);

    bool ready() const { return s_ != nullptr; }

    // Consumes from `in` and fills `out`, advancing both. End at the end of
    // one stream, with `in` just past it: bzip2 files are often several,
    // and init() starts the next. Corrupt, NotBzip2 and NoMemory are for
    // good; Misuse after End.
    BzStatus step(Span<const u8> &in, Span<u8> &out);

    u64 total_in() const;
    u64 total_out() const;

    // Why the last call went wrong, or empty. A literal: data() is
    // NUL-terminated.
    Str why() const;

private:
    BzDecodeState *s_ = nullptr;
};

// The whole of `bytes`, compressed. Err(NoMemory), or Err(Invalid) for a
// block size that is not one.
Result<String> bzip2_compress(Str bytes, int block_size_100k = 9);

// The whole of `bytes`, decompressed, and never more than `limit` bytes of
// it: a small input may claim a large output. Streams one after another are
// one output, as bunzip2 takes them. Err(Invalid) for a stream that is
// corrupt, truncated, longer than the limit, or followed by anything but
// another stream.
Result<String> bzip2_uncompress(Str bytes, usize limit);
