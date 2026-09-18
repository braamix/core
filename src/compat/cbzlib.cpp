// libbzip2's C API over braam::bzip2. A bz_stream's state is one of these
// records; the stream's own fields are copied in before each call and out
// after.
#include <bzlib.h>

#include "bzip2/bzip2.h"
#include "kernel/alloc.h"

namespace {

struct BzShim {
    bz_stream *strm; // the stream this is the state of, as libbzip2 checks
    bool compressing;
    BzCompressor comp;
    BzDecompressor decomp;
};

BzShim *shim_of(bz_stream *strm, bool compressing)
{
    if (strm == nullptr || strm->state == nullptr)
        return nullptr;
    BzShim *sh = static_cast<BzShim *>(strm->state);
    if (sh->strm != strm || sh->compressing != compressing)
        return nullptr;
    return sh;
}

// A new record on the stream, or null.
BzShim *attach(bz_stream *strm, bool compressing)
{
    BzShim *sh = heap_new<BzShim>();
    if (!sh)
        return nullptr;
    sh->strm        = strm;
    sh->compressing = compressing;
    strm->state     = sh;
    return sh;
}

void detach(bz_stream *strm)
{
    heap_delete(static_cast<BzShim *>(strm->state));
    strm->state = nullptr;
}

void set_totals(bz_stream *strm, u64 in, u64 out)
{
    strm->total_in_lo32  = unsigned(in);
    strm->total_in_hi32  = unsigned(in >> 32);
    strm->total_out_lo32 = unsigned(out);
    strm->total_out_hi32 = unsigned(out >> 32);
}

Span<const u8> in_of(bz_stream *strm)
{
    return Span<const u8>(reinterpret_cast<const u8 *>(strm->next_in), strm->avail_in);
}

Span<u8> out_of(bz_stream *strm)
{
    return Span<u8>(reinterpret_cast<u8 *>(strm->next_out), strm->avail_out);
}

// The stream's fields after a call that consumed and produced.
void sync_out(bz_stream *strm, Span<const u8> in, Span<u8> out)
{
    strm->next_in   = const_cast<char *>(reinterpret_cast<const char *>(in.data()));
    strm->avail_in  = unsigned(in.size());
    strm->next_out  = reinterpret_cast<char *>(out.data());
    strm->avail_out = unsigned(out.size());
}

int compress_code(BzStatus s, int action)
{
    switch (s) {
    case BzStatus::Ok:
        return BZ_RUN_OK;
    case BzStatus::More:
        return action == BZ_FLUSH ? BZ_FLUSH_OK : BZ_FINISH_OK;
    case BzStatus::End:
        return BZ_STREAM_END;
    case BzStatus::Stuck:
        return action == BZ_RUN ? BZ_PARAM_ERROR : BZ_SEQUENCE_ERROR;
    case BzStatus::NoMemory:
        return BZ_MEM_ERROR;
    default:
        return BZ_SEQUENCE_ERROR;
    }
}

int decompress_code(BzStatus s)
{
    switch (s) {
    case BzStatus::Ok:
    case BzStatus::Stuck:
        return BZ_OK;
    case BzStatus::End:
        return BZ_STREAM_END;
    case BzStatus::Corrupt:
        return BZ_DATA_ERROR;
    case BzStatus::NotBzip2:
        return BZ_DATA_ERROR_MAGIC;
    case BzStatus::NoMemory:
        return BZ_MEM_ERROR;
    default:
        return BZ_SEQUENCE_ERROR;
    }
}

} // namespace

extern "C" {

const char *BZ2_bzlibVersion(void)
{
    return "1.0.8, 13-Jul-2019";
}

// ------------------------------------------------------------ compress

int BZ2_bzCompressInit(bz_stream *strm, int blockSize100k, int verbosity, int workFactor)
{
    (void)verbosity;
    if (strm == nullptr || blockSize100k < 1 || blockSize100k > 9 || workFactor < 0 ||
        workFactor > 250)
        return BZ_PARAM_ERROR;

    BzShim *sh = attach(strm, true);
    if (!sh)
        return BZ_MEM_ERROR;
    if (sh->comp.init(blockSize100k, workFactor).is_err()) {
        detach(strm);
        return BZ_MEM_ERROR;
    }
    set_totals(strm, 0, 0);
    return BZ_OK;
}

int BZ2_bzCompress(bz_stream *strm, int action)
{
    BzShim *sh = shim_of(strm, true);
    if (!sh || action < BZ_RUN || action > BZ_FINISH)
        return BZ_PARAM_ERROR;
    Span<const u8> in = in_of(strm);
    Span<u8> out      = out_of(strm);
    BzStatus s        = sh->comp.step(in, out, BzAction(action));
    sync_out(strm, in, out);
    set_totals(strm, sh->comp.total_in(), sh->comp.total_out());
    return compress_code(s, action);
}

int BZ2_bzCompressEnd(bz_stream *strm)
{
    if (!shim_of(strm, true))
        return BZ_PARAM_ERROR;
    detach(strm);
    return BZ_OK;
}

// ---------------------------------------------------------- decompress

int BZ2_bzDecompressInit(bz_stream *strm, int verbosity, int small)
{
    if (strm == nullptr || (small != 0 && small != 1) || verbosity < 0 || verbosity > 4)
        return BZ_PARAM_ERROR;

    BzShim *sh = attach(strm, false);
    if (!sh)
        return BZ_MEM_ERROR;
    if (sh->decomp.init(small).is_err()) {
        detach(strm);
        return BZ_MEM_ERROR;
    }
    set_totals(strm, 0, 0);
    return BZ_OK;
}

int BZ2_bzDecompress(bz_stream *strm)
{
    BzShim *sh = shim_of(strm, false);
    if (!sh)
        return BZ_PARAM_ERROR;
    Span<const u8> in = in_of(strm);
    Span<u8> out      = out_of(strm);
    BzStatus s        = sh->decomp.step(in, out);
    sync_out(strm, in, out);
    set_totals(strm, sh->decomp.total_in(), sh->decomp.total_out());
    return decompress_code(s);
}

int BZ2_bzDecompressEnd(bz_stream *strm)
{
    if (!shim_of(strm, false))
        return BZ_PARAM_ERROR;
    detach(strm);
    return BZ_OK;
}

// ----------------------------------------------------------- one-shots

int BZ2_bzBuffToBuffCompress(char *dest, unsigned int *destLen, char *source,
                             unsigned int sourceLen, int blockSize100k, int verbosity,
                             int workFactor)
{
    if (dest == nullptr || destLen == nullptr || source == nullptr || blockSize100k < 1 ||
        blockSize100k > 9 || verbosity < 0 || verbosity > 4 || workFactor < 0 || workFactor > 250)
        return BZ_PARAM_ERROR;

    BzCompressor c;
    if (c.init(blockSize100k, workFactor).is_err())
        return BZ_MEM_ERROR;
    Span<const u8> in(reinterpret_cast<const u8 *>(source), sourceLen);
    Span<u8> out(reinterpret_cast<u8 *>(dest), *destLen);
    BzStatus s = c.step(in, out, BzAction::Finish);
    if (s == BzStatus::More)
        return BZ_OUTBUFF_FULL;
    if (s != BzStatus::End)
        return compress_code(s, BZ_FINISH);
    *destLen -= unsigned(out.size());
    return BZ_OK;
}

int BZ2_bzBuffToBuffDecompress(char *dest, unsigned int *destLen, char *source,
                               unsigned int sourceLen, int small, int verbosity)
{
    if (dest == nullptr || destLen == nullptr || source == nullptr || (small != 0 && small != 1) ||
        verbosity < 0 || verbosity > 4)
        return BZ_PARAM_ERROR;

    BzDecompressor d;
    if (d.init(small).is_err())
        return BZ_MEM_ERROR;
    Span<const u8> in(reinterpret_cast<const u8 *>(source), sourceLen);
    Span<u8> out(reinterpret_cast<u8 *>(dest), *destLen);
    BzStatus s = d.step(in, out);
    if (s == BzStatus::Ok || s == BzStatus::Stuck)
        return out.empty() ? BZ_OUTBUFF_FULL : BZ_UNEXPECTED_EOF;
    if (s != BzStatus::End)
        return decompress_code(s);
    *destLen -= unsigned(out.size());
    return BZ_OK;
}

} // extern "C"
