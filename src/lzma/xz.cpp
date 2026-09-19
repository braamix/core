// XzEncoder and XzDecoder over lzma_code, and the one-shot calls.
#include "lzma/xz.h"

#include "kernel/alloc.h"
#include "lzma/lzma.h"

struct XzState {
    lzma_stream strm;
    const char *why;
    u64 memusage; // an encoder's, which lzma_memusage does not answer
    bool ended;
};

namespace {

constexpr usize CHUNK = 32768;

XzState *make_state()
{
    XzState *s = static_cast<XzState *>(heap_alloc(sizeof(XzState)));
    if (s)
        __builtin_memset(static_cast<void *>(s), 0, sizeof(XzState)); // LZMA_STREAM_INIT
    return s;
}

void free_state(XzState *s)
{
    if (!s)
        return;
    lzma_end(&s->strm);
    heap_free(s);
}

u64 limit_of(u64 memlimit)
{
    return memlimit ? memlimit : UINT64_MAX;
}

bool preset_ok(u32 preset)
{
    return (preset & ~XZ_PRESET_EXTREME) <= 9;
}

const char *text_of(lzma_ret r)
{
    switch (r) {
    case LZMA_MEM_ERROR:
        return "out of memory";
    case LZMA_MEMLIMIT_ERROR:
        return "the memory limit is too low for this stream";
    case LZMA_FORMAT_ERROR:
        return "not in the expected format";
    case LZMA_OPTIONS_ERROR:
        return "unsupported options";
    case LZMA_UNSUPPORTED_CHECK:
        return "unsupported integrity check";
    case LZMA_DATA_ERROR:
        return "the compressed data is corrupt";
    case LZMA_BUF_ERROR:
        return "the stream is cut short";
    case LZMA_PROG_ERROR:
        return "a call out of turn";
    default:
        return nullptr;
    }
}

XzStatus status_of(lzma_ret r)
{
    switch (r) {
    case LZMA_OK:
        return XzStatus::Ok;
    case LZMA_STREAM_END:
        return XzStatus::End;
    case LZMA_BUF_ERROR:
        return XzStatus::Stuck;
    case LZMA_DATA_ERROR:
        return XzStatus::Corrupt;
    case LZMA_FORMAT_ERROR:
        return XzStatus::NotXz;
    case LZMA_OPTIONS_ERROR:
    case LZMA_UNSUPPORTED_CHECK:
        return XzStatus::Unsupported;
    case LZMA_MEMLIMIT_ERROR:
        return XzStatus::MemLimit;
    case LZMA_MEM_ERROR:
        return XzStatus::NoMemory;
    default:
        return XzStatus::Misuse;
    }
}

// One lzma_code call over the two spans, advancing both.
lzma_ret code(XzState *s, Span<const u8> &in, Span<u8> &out, lzma_action action)
{
    s->strm.next_in   = in.data();
    s->strm.avail_in  = in.size();
    s->strm.next_out  = out.data();
    s->strm.avail_out = out.size();
    lzma_ret r        = lzma_code(&s->strm, action);
    in                = Span<const u8>(s->strm.next_in, s->strm.avail_in);
    out               = Span<u8>(s->strm.next_out, s->strm.avail_out);
    s->strm.next_in   = nullptr;
    s->strm.next_out  = nullptr;
    return r;
}

Str literal(const char *p)
{
    if (!p)
        return Str();
    usize n = 0;
    while (p[n])
        n++;
    return Str(p, n);
}

// A heap buffer for the length of a call.
struct XzChunk {
    u8 *p = static_cast<u8 *>(heap_alloc(CHUNK));
    ~XzChunk() { heap_free(p); }
};

bool append(String &out, const u8 *p, usize n)
{
    return out.append(Str(reinterpret_cast<const char *>(p), n));
}

} // namespace

XzEncoder &XzEncoder::operator=(XzEncoder &&o) noexcept
{
    if (this != &o) {
        free_state(s_);
        s_   = o.s_;
        o.s_ = nullptr;
    }
    return *this;
}

XzEncoder::~XzEncoder()
{
    free_state(s_);
}

Result<void> XzEncoder::init(u32 preset, XzCheck check, XzFormat format)
{
    free_state(s_);
    s_ = nullptr;
    if (!preset_ok(preset) || (format != XzFormat::Xz && format != XzFormat::Lzma))
        return Err(Error::Invalid);
    if (!lzma_check_is_supported(static_cast<lzma_check>(check)))
        return Err(Error::Invalid);

    XzState *s = make_state();
    if (!s)
        return Err(Error::NoMemory);

    lzma_ret r;
    if (format == XzFormat::Xz) {
        r           = lzma_easy_encoder(&s->strm, preset, static_cast<lzma_check>(check));
        s->memusage = lzma_easy_encoder_memusage(preset);
    } else {
        lzma_options_lzma opt;
        r                   = lzma_lzma_preset(&opt, preset) ? LZMA_OPTIONS_ERROR
                                                             : lzma_alone_encoder(&s->strm, &opt);
        lzma_filter chain[] = { { LZMA_FILTER_LZMA1, &opt }, { LZMA_VLI_UNKNOWN, nullptr } };
        s->memusage         = r == LZMA_OK ? lzma_raw_encoder_memusage(chain) : 0;
    }
    if (r != LZMA_OK) {
        free_state(s);
        return Err(r == LZMA_MEM_ERROR ? Error::NoMemory : Error::Invalid);
    }
    s_ = s;
    return {};
}

XzStatus XzEncoder::step(Span<const u8> &in, Span<u8> &out, XzAction action)
{
    if (!s_ || s_->ended)
        return XzStatus::Misuse;
    lzma_ret r = code(s_, in, out, static_cast<lzma_action>(action));
    s_->why    = text_of(r);

    if (r == LZMA_STREAM_END) {
        if (action != XzAction::Finish)
            return XzStatus::Ok;
        s_->ended = true;
        return XzStatus::End;
    }
    if (r == LZMA_OK && action != XzAction::Run)
        return XzStatus::More;
    return status_of(r);
}

u64 XzEncoder::total_in() const
{
    return s_ ? s_->strm.total_in : 0;
}

u64 XzEncoder::total_out() const
{
    return s_ ? s_->strm.total_out : 0;
}

u64 XzEncoder::memusage() const
{
    return s_ ? s_->memusage : 0;
}

Str XzEncoder::why() const
{
    return s_ ? literal(s_->why) : Str();
}

usize XzEncoder::bound(usize len)
{
    return lzma_stream_buffer_bound(len);
}

XzDecoder &XzDecoder::operator=(XzDecoder &&o) noexcept
{
    if (this != &o) {
        free_state(s_);
        s_   = o.s_;
        o.s_ = nullptr;
    }
    return *this;
}

XzDecoder::~XzDecoder()
{
    free_state(s_);
}

Result<void> XzDecoder::init(XzFormat format, u64 memlimit)
{
    free_state(s_);
    s_ = make_state();
    if (!s_)
        return Err(Error::NoMemory);

    u64 limit = limit_of(memlimit);
    lzma_ret r;
    switch (format) {
    case XzFormat::Xz:
        r = lzma_stream_decoder(&s_->strm, limit, LZMA_CONCATENATED);
        break;
    case XzFormat::Lzma:
        r = lzma_alone_decoder(&s_->strm, limit);
        break;
    case XzFormat::Lzip:
        r = lzma_lzip_decoder(&s_->strm, limit, LZMA_CONCATENATED);
        break;
    default:
        r = lzma_auto_decoder(&s_->strm, limit, LZMA_CONCATENATED);
        break;
    }
    if (r != LZMA_OK) {
        free_state(s_);
        s_ = nullptr;
        return Err(r == LZMA_MEM_ERROR ? Error::NoMemory : Error::Invalid);
    }
    return {};
}

XzStatus XzDecoder::step(Span<const u8> &in, Span<u8> &out, bool finish)
{
    if (!s_ || s_->ended)
        return XzStatus::Misuse;
    bool room  = !out.empty();
    lzma_ret r = code(s_, in, out, finish ? LZMA_FINISH : LZMA_RUN);
    s_->why    = text_of(r);

    if (r == LZMA_STREAM_END)
        s_->ended = true;
    // No progress with room to make it in, and nothing more to come.
    if (r == LZMA_BUF_ERROR && finish && room)
        return XzStatus::Corrupt;
    return status_of(r);
}

u64 XzDecoder::total_in() const
{
    return s_ ? s_->strm.total_in : 0;
}

u64 XzDecoder::total_out() const
{
    return s_ ? s_->strm.total_out : 0;
}

u64 XzDecoder::memusage() const
{
    return s_ ? lzma_memusage(&s_->strm) : 0;
}

Result<void> XzDecoder::set_memlimit(u64 memlimit)
{
    if (!s_)
        return Err(Error::Invalid);
    if (lzma_memlimit_set(&s_->strm, limit_of(memlimit)) != LZMA_OK)
        return Err(Error::Invalid);
    return {};
}

Str XzDecoder::why() const
{
    return s_ ? literal(s_->why) : Str();
}

Result<String> xz_compress(Str bytes, u32 preset, XzCheck check)
{
    XzEncoder e;
    TRY_VOID(e.init(preset, check));
    XzChunk chunk;
    if (!chunk.p)
        return Err(Error::NoMemory);

    String out;
    if (!out.reserve(XzEncoder::bound(bytes.size())))
        return Err(Error::NoMemory);
    Span<const u8> in(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
    for (;;) {
        Span<u8> room(chunk.p, CHUNK);
        XzStatus st = e.step(in, room, XzAction::Finish);
        if (!append(out, chunk.p, CHUNK - room.size()))
            return Err(Error::NoMemory);
        if (st == XzStatus::End)
            return out;
        if (st == XzStatus::NoMemory)
            return Err(Error::NoMemory);
        if (st != XzStatus::More)
            return Err(Error::Invalid);
    }
}

Result<String> xz_uncompress(Str bytes, usize limit)
{
    XzDecoder d;
    TRY_VOID(d.init());
    XzChunk chunk;
    if (!chunk.p)
        return Err(Error::NoMemory);

    String out;
    Span<const u8> in(reinterpret_cast<const u8 *>(bytes.data()), bytes.size());
    for (;;) {
        Span<u8> room(chunk.p, CHUNK);
        XzStatus st = d.step(in, room, true);
        usize got   = CHUNK - room.size();
        if (got > limit - out.size())
            return Err(Error::Invalid);
        if (!append(out, chunk.p, got))
            return Err(Error::NoMemory);
        if (st == XzStatus::End)
            break;
        if (st == XzStatus::NoMemory)
            return Err(Error::NoMemory);
        if (st != XzStatus::Ok)
            return Err(Error::Invalid);
    }
    // .lzma is one stream and ends before the input may.
    if (!in.empty())
        return Err(Error::Invalid);
    return out;
}
