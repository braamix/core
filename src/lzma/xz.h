// The .xz and .lzma formats, and .lz to read. `braam::lzma`, asked for by name:
//
//     braam_add_program(NAME xzpipe SOURCES xzpipe.cpp LIBS braam::lzma)
//
// A wrapper over liblzma 5.8.4 by Lasse Collin and others, vendored verbatim
// beside this file under the BSD Zero Clause License (COPYING.0BSD). The
// output is liblzma's, byte for byte, because it is liblzma. Filter chains,
// the index, raw streams and the rest are lzma/lzma.h, liblzma's own API,
// which a PORT target also reaches as <lzma.h>.
//
// Memory, all of it from the heap: a coder is one heap block here plus what
// liblzma allocates. The preset rules the encoder's share -- 2.7 MiB at 0,
// 93 MiB at 6, 673 MiB at 9 -- and a decoder needs about the dictionary,
// 256 KiB to 64 MiB. None of it is in a frame.
#pragma once

#include "kernel/result.h"
#include "kernel/span.h"
#include "kernel/str.h"
#include "kernel/string.h"
#include "kernel/types.h"

// OR-ed into a preset: slower, a little smaller, the same memory.
constexpr u32 XZ_PRESET_EXTREME = 0x80000000u;

enum class XzFormat : u8 {
    Xz,   // .xz: blocks, an index and a check
    Lzma, // .lzma, LZMA_Alone: one bare LZMA1 stream and a thirteen-byte header
    Lzip, // decode only: .lz, lzip's format, version 0 and 1
    Auto, // decode only: any of the three, by the first bytes
};

// liblzma's numbering, so that it converts by cast.
enum class XzCheck : u8 {
    None   = 0,
    Crc32  = 1,
    Crc64  = 4,
    Sha256 = 10,
};

// liblzma's numbering again.
enum class XzAction : u8 {
    Run,         // as much as the buffers allow
    SyncFlush,   // .xz: what is pending, readable so far; the block goes on
    FullFlush,   // .xz: SyncFlush, and the block ends; a decoder may start here
    Finish,      // the end of the stream
    FullBarrier, // .xz: FullFlush, and the next block's options may change
};

enum class XzStatus : u8 {
    Ok,          // progress; call again. A flush that has finished is Ok too
    More,        // encode: a flush or Finish is under way: call again with more
                 // room, the same action, and the input as the last call left it
    End,         // the stream is complete
    Stuck,       // no progress was possible: more input or more room is wanted
    Corrupt,     // the input is damaged, or cut short under `finish`
    NotXz,       // the first bytes are not the format asked for
    Unsupported, // valid, but asks for what this build cannot: a filter,
                 // a check, or an option out of range
    MemLimit,    // decode: the stream wants more memory than the limit allows
    NoMemory,
    Misuse, // the call the coder's state forbids
};

struct XzState;

// An encoder. Move-only: its state is one heap block, and liblzma's are freed
// with it.
struct XzEncoder {
    XzEncoder() = default;
    XzEncoder(XzEncoder &&o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    XzEncoder &operator=(XzEncoder &&o) noexcept;
    XzEncoder(const XzEncoder &)            = delete;
    XzEncoder &operator=(const XzEncoder &) = delete;
    ~XzEncoder();

    // preset 0..9, optionally | XZ_PRESET_EXTREME. `check` is .xz's alone;
    // format is Xz or Lzma. Err(Invalid) past those, Err(NoMemory).
    Result<void> init(u32 preset = 6, XzCheck check = XzCheck::Crc64,
                      XzFormat format = XzFormat::Xz);

    bool ready() const { return s_ != nullptr; }

    // Consumes from `in` and fills `out`, advancing both. Run is Ok or Stuck;
    // a flush is More until it is out, then Ok; Finish is More until the
    // stream is out, then End. Misuse for a flush or Finish whose input
    // changed between calls, a different action before one is done, or any
    // call after End.
    XzStatus step(Span<const u8> &in, Span<u8> &out, XzAction action);

    u64 total_in() const;
    u64 total_out() const;

    // Bytes liblzma holds for this coder, as liblzma reckons them.
    u64 memusage() const;

    // Why the last call went wrong, or empty. A literal.
    Str why() const;

    // The most `len` bytes can take as one .xz stream, from a buffer at once.
    static usize bound(usize len);

private:
    XzState *s_ = nullptr;
};

// A decoder. Move-only, as an encoder is.
struct XzDecoder {
    XzDecoder() = default;
    XzDecoder(XzDecoder &&o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    XzDecoder &operator=(XzDecoder &&o) noexcept;
    XzDecoder(const XzDecoder &)            = delete;
    XzDecoder &operator=(const XzDecoder &) = delete;
    ~XzDecoder();

    // memlimit 0 means none. .xz and .lz are read as xz and lzip read them,
    // streams one after another as one output; .lzma is one stream. Err(NoMemory).
    Result<void> init(XzFormat format = XzFormat::Auto, u64 memlimit = 0);

    bool ready() const { return s_ != nullptr; }

    // Consumes from `in` and fills `out`, advancing both. `finish` says no
    // input follows what `in` holds: .xz and .lz end at End only then, since
    // another stream might. Corrupt, NotXz, Unsupported and NoMemory are for
    // good; MemLimit is not, after set_memlimit(); Misuse after End.
    XzStatus step(Span<const u8> &in, Span<u8> &out, bool finish);

    u64 total_in() const;
    u64 total_out() const;

    u64 memusage() const;
    // The limit, raised or lowered. Err(Invalid) below what is in use.
    Result<void> set_memlimit(u64 memlimit);

    Str why() const;

private:
    XzState *s_ = nullptr;
};

// The whole of `bytes` as one .xz stream. Err(NoMemory), or Err(Invalid) for
// a preset that is not one.
Result<String> xz_compress(Str bytes, u32 preset = 6, XzCheck check = XzCheck::Crc64);

// The whole of `bytes`, decoded as Auto does, and never more than `limit`
// bytes of it: a small input may claim a large output. Err(Invalid) for a
// stream that is corrupt, truncated, longer than the limit, or not one of the
// three; Err(NoMemory).
Result<String> xz_uncompress(Str bytes, usize limit);
