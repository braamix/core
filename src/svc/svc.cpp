#include "svc.h"

#include "kernel/host.h"
#include "kernel/traits.h"

namespace {

// The first guess at a blob's size; the sized-twice protocol grows it.
constexpr usize BLOB_HINT = 512;

// What one read of a stream yields: a whole span (Concept.md §8.2).
constexpr usize STREAM_CHUNK = 65536;

} // namespace

Task<Result<String>> svc_blob(SvcCall &c)
{
    if (!c.ok() || !c.reserve(BLOB_HINT))
        co_return Err(Error::NoMemory);

    for (int attempt = 0; attempt < 2; attempt++) {
        CO_TRY_VOID(co_await c);
        HostReq &r = c.req();
        String out;
        if (r.h.buf_len > 0 || r.h.result_lo == 0) {
            if (!out.append(Str(r.buf.data(), r.h.buf_len)))
                co_return Err(Error::NoMemory);
            co_return move(out);
        }
        if (!c.reserve(r.h.result_lo))
            co_return Err(Error::NoMemory);
        r.done = false;
    }
    co_return Err(Error::Io);
}

Task<Result<String>> svc_chunk(SvcCall &c)
{
    if (!c.ok() || !c.reserve(STREAM_CHUNK))
        co_return Err(Error::NoMemory);
    CO_TRY_VOID(co_await c);

    HostReq &r = c.req();
    String out;
    if (r.h.buf_len && !out.append(Str(r.buf.data(), r.h.buf_len)))
        co_return Err(Error::NoMemory);
    co_return move(out);
}

void svc_drop(JsSlot slot)
{
    if (slot)
        host_svc(u32(SvcOp::Drop), 0, 0, jsref_get(slot));
}

Task<Result<WallClock>> svc_clock()
{
    SvcCall c(SvcOp::Clock);
    if (!c.ok())
        co_return Err(Error::NoMemory);
    CO_TRY_VOID(co_await c);

    WallClock w;
    w.epoch_ms = c.req().result();
    w.tz_min   = i32(c.req().h.flags) - 1440;
    co_return w;
}

Task<Result<String>> clip_read()
{
    SvcCall c(SvcOp::ClipRead);
    Task<Result<String>> t = svc_blob(c);
    if (!t)
        co_return Err(Error::NoMemory);
    co_return co_await t;
}

Task<Result<String>> clip_wait()
{
    SvcCall c(SvcOp::ClipWait);
    Task<Result<String>> t = svc_blob(c);
    if (!t)
        co_return Err(Error::NoMemory);
    co_return co_await t;
}

Task<Result<String>> host_info()
{
    SvcCall c(SvcOp::HostInfo);
    Task<Result<String>> t = svc_blob(c);
    if (!t)
        co_return Err(Error::NoMemory);
    co_return co_await t;
}

Task<Result<String>> svc_random(u32 n)
{
    SvcCall c(SvcOp::Random, "", n);
    if (!c.ok() || !c.reserve(n))
        co_return Err(Error::NoMemory);
    CO_TRY_VOID(co_await c);

    HostReq &r = c.req();
    if (r.h.buf_len != n)
        co_return Err(Error::Io);
    String out;
    if (!out.append(Str(r.buf.data(), r.h.buf_len)))
        co_return Err(Error::NoMemory);
    co_return move(out);
}

Task<Result<void>> clip_write(Str text)
{
    SvcCall c(SvcOp::ClipWrite, "", 0);
    if (!c.ok() || !c.put(text))
        co_return Err(Error::NoMemory);
    CO_TRY_VOID(co_await c);
    co_return {};
}
