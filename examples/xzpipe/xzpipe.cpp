// zpipe for xz: stdin compressed to stdout in the .xz format, or with -d
// decompressed back, from .xz, .lzma or .lz, several streams one after
// another included. The SDK's worked example of LIBS braam::lzma;
// doc/Programming_Manual.md walks through it.
#include "kernel/alloc.h"
#include "lzma/xz.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    xzpipe [-0 ... -9] <in >out\n"
    "    xzpipe -d <in >out\n";

constexpr usize CHUNK = 16384;

// On the heap, not in the frame: a frame past 32 KiB costs whole spans.
struct Buffers {
    char in[CHUNK];
    u8 out[CHUNK];
};

Task<i32> fail(Str why)
{
    co_await write_err("xzpipe: ");
    co_await write_err(why);
    co_await write_err("\n");
    co_return 1;
}

Str made(const Buffers &b, Span<u8> room)
{
    return Str(reinterpret_cast<const char *>(b.out), CHUNK - room.size());
}

// Read a chunk; each one stepped until the output stops filling.
Task<i32> compress(Buffers &b, u32 preset)
{
    XzEncoder e;
    if (e.init(preset).is_err())
        co_return co_await fail("out of memory");

    for (;;) {
        Result<usize> n = co_await File::stdin().read(Span<char>(b.in, CHUNK));
        bool eof        = n.is_err();
        if (eof && n.error() != Error::Closed)
            co_return co_await fail(error_name(n.error()));

        Span<const u8> src(reinterpret_cast<const u8 *>(b.in), eof ? 0 : n.value());
        XzAction action = eof ? XzAction::Finish : XzAction::Run;
        XzStatus st;
        do {
            Span<u8> dst(b.out, CHUNK);
            st = e.step(src, dst, action);
            if (st != XzStatus::Ok && st != XzStatus::More && st != XzStatus::End)
                co_return co_await fail(e.why());
            if ((co_await File::stdout().write(made(b, dst))).is_err())
                co_return 1;
        } while (eof ? st != XzStatus::End : !src.empty());

        if (eof)
            co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
    }
}

Task<i32> decompress(Buffers &b)
{
    XzDecoder d;
    if (d.init().is_err())
        co_return co_await fail("out of memory");

    bool ended = false; // .lzma is one stream and may end before the input
    for (;;) {
        Result<usize> n = co_await File::stdin().read(Span<char>(b.in, CHUNK));
        bool eof        = n.is_err();
        if (eof && n.error() != Error::Closed)
            co_return co_await fail(error_name(n.error()));

        Span<const u8> src(reinterpret_cast<const u8 *>(b.in), eof ? 0 : n.value());
        if (ended && !src.empty())
            co_return co_await fail("trailing garbage after the stream");
        while (!ended) {
            Span<u8> dst(b.out, CHUNK);
            XzStatus st = d.step(src, dst, eof);
            if (st != XzStatus::Ok && st != XzStatus::End)
                co_return co_await fail(d.why());
            if ((co_await File::stdout().write(made(b, dst))).is_err())
                co_return 1;
            ended = st == XzStatus::End;
            if (!ended && src.empty() && !dst.empty() && !eof)
                break;
        }
        if (ended && !src.empty())
            co_return co_await fail("trailing garbage after the stream");
        if (eof)
            co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
    }
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    bool decompressing = false;
    u32 preset         = 6;
    if (args.size() == 2) {
        Str a = args[1];
        if (a == "-d")
            decompressing = true;
        else if (a.size() == 2 && a[0] == '-' && a[1] >= '0' && a[1] <= '9')
            preset = u32(a[1] - '0');
        else
            co_return co_await usage_error(USAGE);
    } else if (args.size() != 1) {
        co_return co_await usage_error(USAGE);
    }

    Buffers *b = heap_new<Buffers>();
    if (!b)
        co_return co_await fail("out of memory");
    i32 rc = co_await (decompressing ? decompress(*b) : compress(*b, preset));
    heap_delete(b);
    co_return rc;
}
