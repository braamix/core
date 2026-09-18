// zpipe for bzip2: stdin compressed to stdout in the bzip2 format, or with
// -d decompressed back, several streams one after another included. The
// SDK's worked example of LIBS braam::bzip2; doc/Programming_Manual.md walks
// through it.
#include "bzip2/bzip2.h"
#include "kernel/alloc.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    bzpipe [-d] <in >out\n";

constexpr usize CHUNK = 16384;

// On the heap, not in the frame: a frame past 32 KiB costs whole spans.
struct Buffers {
    char in[CHUNK];
    u8 out[CHUNK];
};

Task<i32> fail(Str why)
{
    co_await write_err("bzpipe: ");
    co_await write_err(why);
    co_await write_err("\n");
    co_return 1;
}

Str made(const Buffers &b, Span<u8> room)
{
    return Str(reinterpret_cast<const char *>(b.out), CHUNK - room.size());
}

// Read a chunk; each one stepped until the output stops filling.
Task<i32> compress(Buffers &b)
{
    BzCompressor c;
    if (c.init().is_err())
        co_return co_await fail("out of memory");

    for (;;) {
        Result<usize> n = co_await File::stdin().read(Span<char>(b.in, CHUNK));
        bool eof        = n.is_err();
        if (eof && n.error() != Error::Closed)
            co_return co_await fail(error_name(n.error()));

        Span<const u8> src(reinterpret_cast<const u8 *>(b.in), eof ? 0 : n.value());
        BzAction action = eof ? BzAction::Finish : BzAction::Run;
        BzStatus st;
        do {
            Span<u8> dst(b.out, CHUNK);
            st = c.step(src, dst, action);
            if (st == BzStatus::Misuse || st == BzStatus::NoMemory)
                co_return co_await fail("out of memory");
            if ((co_await File::stdout().write(made(b, dst))).is_err())
                co_return 1;
        } while (eof ? st != BzStatus::End : !src.empty());

        if (eof)
            co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
    }
}

Task<i32> decompress(Buffers &b)
{
    BzDecompressor d;
    bool ended = true; // between streams
    for (;;) {
        Result<usize> n = co_await File::stdin().read(Span<char>(b.in, CHUNK));
        bool eof        = n.is_err();
        if (eof && n.error() != Error::Closed)
            co_return co_await fail(error_name(n.error()));

        Span<const u8> src(reinterpret_cast<const u8 *>(b.in), eof ? 0 : n.value());
        Span<u8> dst;
        do {
            if (ended && !src.empty()) {
                if (d.init().is_err())
                    co_return co_await fail("out of memory");
                ended = false;
            }
            dst         = Span<u8>(b.out, CHUNK);
            BzStatus st = ended ? BzStatus::Stuck : d.step(src, dst);
            if (st == BzStatus::Corrupt || st == BzStatus::NotBzip2)
                co_return co_await fail(d.why());
            if (st == BzStatus::NoMemory || st == BzStatus::Misuse)
                co_return co_await fail("out of memory");
            if ((co_await File::stdout().write(made(b, dst))).is_err())
                co_return 1;
            if (st == BzStatus::End)
                ended = true;
        } while (!src.empty() || dst.empty());

        if (eof) {
            if (!ended)
                co_return co_await fail("the stream is cut short");
            co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
        }
    }
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);
    bool decompressing = args.size() == 2 && args[1] == "-d";
    if (args.size() != 1 && !decompressing)
        co_return co_await usage_error(USAGE);

    Buffers *b = heap_new<Buffers>();
    if (!b)
        co_return co_await fail("out of memory");
    i32 rc = co_await (decompressing ? decompress(*b) : compress(*b));
    heap_delete(b);
    co_return rc;
}
