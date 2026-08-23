#include "unzip.h"

#include "proc/io.h"

Task<Result<void>> FdZipSource::read(u64 off, Span<u8> out)
{
    if (Task<Result<u64>> t = seek_fd(fd_, i64(off), SYS_SEEK_SET)) {
        if (Result<u64> r = co_await t; r.is_err())
            co_return Err(r.error());
    } else
        co_return Err(Error::NoMemory);

    for (usize got = 0; got < out.size();) {
        Result<String> chunk = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_some(fd_, u32(out.size() - got)))
            chunk = co_await t;
        if (chunk.is_err())
            co_return Err(chunk.error() == Error::Closed ? Error::Invalid : chunk.error());
        Str s = chunk.value().str();
        for (usize i = 0; i < s.size(); i++)
            out[got + i] = u8(s[i]);
        got += s.size();
    }
    co_return {};
}

Task<Result<String>> zip_read(ZipSource &src, const ZipEntry &e)
{
    if (e.method == ZIP_STORE)
        co_return co_await zip_stored(src, e);
    if (e.method != ZIP_DEFLATE || e.packed > ZIP_PACKED_MAX)
        co_return Err(Error::Unsupported);

    Result<String> packed = Err(Error::NoMemory);
    if (Task<Result<String>> t = zip_packed(src, e))
        packed = co_await t;
    if (packed.is_err())
        co_return Err(packed.error());

    Result<i32> open = Err(Error::NoMemory);
    if (Task<Result<i32>> t = inflate(packed.value().str()))
        open = co_await t;
    if (open.is_err())
        co_return Err(open.error());
    u32 fd = u32(open.value());

    ZipSink sink(e.size);
    Error failure = Error::Invalid;
    bool ok       = false;
    for (;;) {
        Result<String> chunk = Err(Error::NoMemory);
        if (Task<Result<String>> t = read_chunk(fd))
            chunk = co_await t;
        if (chunk.is_err()) {
            if (chunk.error() == Error::Closed)
                ok = sink.complete();
            else
                failure = chunk.error();
            break;
        }
        if (!sink.take(chunk.value().str()))
            break; // past the declared size: a bomb, abandoned part way
    }

    if (Task<void> t = close_fd(fd))
        co_await t;
    if (!ok)
        co_return Err(failure);
    co_return move(sink.text());
}
