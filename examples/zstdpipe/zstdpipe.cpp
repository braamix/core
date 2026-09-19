// zpipe for Zstandard: stdin compressed to stdout as a .zst frame, or with -d
// decompressed back, several frames one after another included. The SDK's
// worked example of LIBS braam::zstd; doc/Programming_Manual.md walks through
// it.
#include "kernel/alloc.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"
#include "zstd/zstd.h"

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    zstdpipe [-1 ... -19] <in >out\n"
    "    zstdpipe -d <in >out\n";

constexpr usize CHUNK = 16384;

// On the heap, not in the frame: a frame past 32 KiB costs whole spans.
struct Buffers {
    char in[CHUNK];
    char out[CHUNK];
};

Task<i32> fail(Str why)
{
    co_await write_err("zstdpipe: ");
    co_await write_err(why);
    co_await write_err("\n");
    co_return 1;
}

// ZSTD_getErrorName answers a C string. Str(const char *) and a counting loop
// both become a call to strlen, which no libc here defines.
__attribute__((no_builtin("strlen"))) Str error_of(size_t code)
{
    const char *name = ZSTD_getErrorName(code);
    usize n          = 0;
    while (name[n])
        n++;
    return Str(name, n);
}

Task<i32> fail(size_t code)
{
    co_return co_await fail(error_of(code));
}

// Read a chunk; each one stepped until it is consumed, or at the end until
// the frame is flushed whole.
Task<i32> compress(Buffers &b, int level)
{
    ZSTD_CCtx *c = ZSTD_createCCtx();
    if (!c)
        co_return co_await fail("out of memory");
    i32 rc     = 0;
    size_t err = ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, level);
    if (!ZSTD_isError(err))
        err = ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 1);

    for (bool eof = false; !ZSTD_isError(err) && !eof && rc == 0;) {
        Result<usize> n = co_await File::stdin().read(Span<char>(b.in, CHUNK));
        eof             = n.is_err();
        if (eof && n.error() != Error::Closed) {
            rc = co_await fail(error_name(n.error()));
            break;
        }

        ZSTD_inBuffer src      = { b.in, eof ? 0 : n.value(), 0 };
        ZSTD_EndDirective mode = eof ? ZSTD_e_end : ZSTD_e_continue;
        size_t left;
        do {
            ZSTD_outBuffer dst = { b.out, CHUNK, 0 };
            left               = ZSTD_compressStream2(c, &dst, &src, mode);
            if (ZSTD_isError(left)) {
                err = left;
                break;
            }
            if ((co_await File::stdout().write(Str(b.out, dst.pos))).is_err()) {
                rc = 1;
                break;
            }
        } while (eof ? left != 0 : src.pos != src.size);
    }

    ZSTD_freeCCtx(c);
    if (ZSTD_isError(err))
        co_return co_await fail(err);
    if (rc != 0)
        co_return rc;
    co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
}

Task<i32> decompress(Buffers &b)
{
    ZSTD_DCtx *d = ZSTD_createDCtx();
    if (!d)
        co_return co_await fail("out of memory");
    i32 rc      = 0;
    size_t err  = 0;
    size_t hint = 0; // 0: the last frame is whole

    for (bool eof = false; !eof && rc == 0 && !ZSTD_isError(err);) {
        Result<usize> n = co_await File::stdin().read(Span<char>(b.in, CHUNK));
        eof             = n.is_err();
        if (eof && n.error() != Error::Closed) {
            rc = co_await fail(error_name(n.error()));
            break;
        }
        // Called with no input, the hint is the next frame's header.
        if (eof)
            break;

        ZSTD_inBuffer src = { b.in, n.value(), 0 };
        bool full;
        do {
            ZSTD_outBuffer dst = { b.out, CHUNK, 0 };
            hint               = ZSTD_decompressStream(d, &dst, &src);
            if (ZSTD_isError(hint)) {
                err = hint;
                break;
            }
            if ((co_await File::stdout().write(Str(b.out, dst.pos))).is_err()) {
                rc = 1;
                break;
            }
            full = dst.pos == dst.size;
            // Called again, a finished frame's hint is the next one's header.
        } while (src.pos != src.size || (full && hint != 0));
    }

    ZSTD_freeDCtx(d);
    if (ZSTD_isError(err))
        co_return co_await fail(err);
    if (rc != 0)
        co_return rc;
    if (hint != 0)
        co_return co_await fail("the frame is cut short");
    co_return (co_await File::stdout().flush()).is_err() ? 1 : 0;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    bool decompressing = false;
    int level          = ZSTD_CLEVEL_DEFAULT;
    if (args.size() == 2) {
        Str a = args[1];
        if (a == "-d") {
            decompressing = true;
        } else if (a.size() >= 2 && a.size() <= 3 && a[0] == '-') {
            level = 0;
            for (usize i = 1; i < a.size(); i++) {
                if (a[i] < '0' || a[i] > '9')
                    co_return co_await usage_error(USAGE);
                level = level * 10 + (a[i] - '0');
            }
            if (level < 1 || level > 19)
                co_return co_await usage_error(USAGE);
        } else {
            co_return co_await usage_error(USAGE);
        }
    } else if (args.size() != 1) {
        co_return co_await usage_error(USAGE);
    }

    Buffers *b = heap_new<Buffers>();
    if (!b)
        co_return co_await fail("out of memory");
    i32 rc = co_await (decompressing ? decompress(*b) : compress(*b, level));
    heap_delete(b);
    co_return rc;
}
