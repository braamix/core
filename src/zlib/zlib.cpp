// The one-shot calls, after zlib's compress.c and uncompr.c.
#include "kernel/alloc.h"
#include "zlib/zlib.h"

namespace {

constexpr usize CHUNK = 32768;

// A heap buffer for the length of a call.
struct Chunk {
    u8 *p = static_cast<u8 *>(heap_alloc(CHUNK));
    ~Chunk() { heap_free(p); }
};

bool append(String &out, const u8 *p, usize n)
{
    return out.append(Str(reinterpret_cast<const char *>(p), n));
}

} // namespace

Result<String> zlib_compress(Str bytes, ZFormat format, int level)
{
    Deflater d;
    TRY_VOID(d.init(level, format));
    Chunk chunk;
    if (!chunk.p)
        return Err(Error::NoMemory);

    String out;
    if (!out.reserve(d.bound(bytes.size())))
        return Err(Error::NoMemory);
    Span<const u8> in(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
    for (;;) {
        Span<u8> room(chunk.p, CHUNK);
        ZStatus st = d.step(in, room, ZFlush::Finish);
        if (!append(out, chunk.p, CHUNK - room.size()))
            return Err(Error::NoMemory);
        if (st == ZStatus::End)
            return out;
        if (st != ZStatus::Ok)
            return Err(st == ZStatus::NoMemory ? Error::NoMemory : Error::Invalid);
    }
}

Result<String> zlib_uncompress(Str bytes, ZFormat format, usize limit)
{
    Inflater inf;
    TRY_VOID(inf.init(format, format == ZFormat::Raw ? 15 : 0));
    Chunk chunk;
    if (!chunk.p)
        return Err(Error::NoMemory);

    String out;
    Span<const u8> in(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
    for (;;) {
        Span<u8> room(chunk.p, CHUNK);
        ZStatus st = inf.step(in, room);
        usize got  = CHUNK - room.size();
        if (got > limit - out.size())
            return Err(Error::Invalid);
        if (!append(out, chunk.p, got))
            return Err(Error::NoMemory);
        if (st == ZStatus::End)
            return out;
        if (st == ZStatus::NoMemory)
            return Err(Error::NoMemory);
        if (st != ZStatus::Ok)
            return Err(Error::Invalid);
    }
}
