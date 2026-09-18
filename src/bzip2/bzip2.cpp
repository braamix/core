// The one-shot calls, after BZ2_bzBuffToBuffCompress and
// BZ2_bzBuffToBuffDecompress.
#include "bzip2/bzip2.h"

#include "kernel/alloc.h"

namespace {

constexpr usize CHUNK = 32768;

// A heap buffer for the length of a call.
struct BzChunk {
    u8 *p = static_cast<u8 *>(heap_alloc(CHUNK));
    ~BzChunk() { heap_free(p); }
};

bool append(String &out, const u8 *p, usize n)
{
    return out.append(Str(reinterpret_cast<const char *>(p), n));
}

} // namespace

Result<String> bzip2_compress(Str bytes, int block_size_100k)
{
    BzCompressor c;
    TRY_VOID(c.init(block_size_100k));
    BzChunk chunk;
    if (!chunk.p)
        return Err(Error::NoMemory);

    String out;
    if (!out.reserve(BzCompressor::bound(bytes.size())))
        return Err(Error::NoMemory);
    Span<const u8> in(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
    for (;;) {
        Span<u8> room(chunk.p, CHUNK);
        BzStatus st = c.step(in, room, BzAction::Finish);
        if (!append(out, chunk.p, CHUNK - room.size()))
            return Err(Error::NoMemory);
        if (st == BzStatus::End)
            return out;
        if (st != BzStatus::More)
            return Err(Error::Invalid);
    }
}

Result<String> bzip2_uncompress(Str bytes, usize limit)
{
    BzChunk chunk;
    if (!chunk.p)
        return Err(Error::NoMemory);

    String out;
    BzDecompressor d;
    Span<const u8> in(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
    do {
        TRY_VOID(d.init());
        for (;;) {
            Span<u8> room(chunk.p, CHUNK);
            BzStatus st = d.step(in, room);
            usize got   = CHUNK - room.size();
            if (got > limit - out.size())
                return Err(Error::Invalid);
            if (!append(out, chunk.p, got))
                return Err(Error::NoMemory);
            if (st == BzStatus::End)
                break;
            if (st == BzStatus::NoMemory)
                return Err(Error::NoMemory);
            if (st != BzStatus::Ok)
                return Err(Error::Invalid);
        }
    } while (!in.empty());
    return out;
}
