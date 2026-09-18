// zlib's own examples/zpipe.c as a Braam program: stdin deflated to stdout in
// the zlib format, or with -d inflated back. The SDK's worked example of
// LIBS braam::zlib; doc/Programming_Manual.md walks through it.
#include "kernel/alloc.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"
#include "zlib/zlib.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    zpipe [-d] <in >out\n";

constexpr usize CHUNK = 16384;

// On the heap, not in the frame: a frame past 32 KiB costs whole spans.
struct Buffers {
    char in[CHUNK];
    u8 out[CHUNK];
};

Task<i32> fail(Str why)
{
    co_await write_err("zpipe: ");
    co_await write_err(why);
    co_await write_err("\n");
    co_return 1;
}

// A chunk of stdin at a time, stepped until the output stops filling.
Task<i32> pipe(bool decompress, Buffers &b)
{
    File &in  = File::stdin();
    File &out = File::stdout();
    Deflater def;
    Inflater inf;
    if (decompress ? inf.init().is_err() : def.init().is_err())
        co_return co_await fail("out of memory");

    ZStatus st = ZStatus::Ok;
    while (st != ZStatus::End) {
        Result<usize> n = co_await in.read(Span<char>(b.in, CHUNK));
        bool eof        = n.is_err();
        if (eof && n.error() != Error::Closed)
            co_return co_await fail(error_name(n.error()));

        Span<const u8> src(reinterpret_cast<const u8 *>(b.in), eof ? 0 : n.value());
        Span<u8> dst;
        do {
            dst = Span<u8>(b.out, CHUNK);
            st  = decompress ? inf.step(src, dst)
                             : def.step(src, dst, eof ? ZFlush::Finish : ZFlush::None);
            if (st == ZStatus::Corrupt)
                co_return co_await fail(inf.why());
            if (st == ZStatus::NeedDict)
                co_return co_await fail("the stream wants a dictionary");
            if (st == ZStatus::NoMemory || st == ZStatus::Misuse)
                co_return co_await fail("out of memory");
            Str made(reinterpret_cast<const char *>(b.out), CHUNK - dst.size());
            if ((co_await out.write(made)).is_err())
                co_return 1;
        } while (dst.empty());

        if (eof && st != ZStatus::End)
            co_return co_await fail("the stream is cut short");
    }

    co_return (co_await out.flush()).is_err() ? 1 : 0;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);
    bool decompress = args.size() == 2 && args[1] == "-d";
    if (args.size() != 1 && !decompress)
        co_return co_await usage_error(USAGE);

    Buffers *b = heap_new<Buffers>();
    if (!b)
        co_return co_await fail("out of memory");
    i32 rc = co_await pipe(decompress, *b);
    heap_delete(b);
    co_return rc;
}
