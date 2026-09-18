// zlib's C API over braam::zlib. A z_stream's state is one of these records;
// the stream's own fields are copied in before each call and out after.
#include <stddef.h>
#include <zlib.h>

#include "kernel/alloc.h"
#include "zlib/zlib.h"

static_assert(sizeof(gz_header) == sizeof(ZHeader), "gz_header is ZHeader");
static_assert(offsetof(gz_header, time) == offsetof(ZHeader, time), "gz_header is ZHeader");
static_assert(offsetof(gz_header, extra) == offsetof(ZHeader, extra), "gz_header is ZHeader");
static_assert(offsetof(gz_header, comm_max) == offsetof(ZHeader, comm_max), "gz_header is ZHeader");
static_assert(offsetof(gz_header, done) == offsetof(ZHeader, done), "gz_header is ZHeader");

struct internal_state {
    z_streamp strm; // the stream this is the state of, as zlib checks
    bool deflating;
    Inflater inf;
    Deflater def;
};

namespace {

using Shim = internal_state;

const char *const MESSAGES[10] = {
    "need dictionary",      // Z_NEED_DICT       2
    "stream end",           // Z_STREAM_END      1
    "",                     // Z_OK              0
    "file error",           // Z_ERRNO         (-1)
    "stream error",         // Z_STREAM_ERROR  (-2)
    "data error",           // Z_DATA_ERROR    (-3)
    "insufficient memory",  // Z_MEM_ERROR     (-4)
    "buffer error",         // Z_BUF_ERROR     (-5)
    "incompatible version", // Z_VERSION_ERROR (-6)
    "",
};

int code_of(ZStatus s)
{
    switch (s) {
    case ZStatus::Ok:
        return Z_OK;
    case ZStatus::End:
        return Z_STREAM_END;
    case ZStatus::NeedDict:
        return Z_NEED_DICT;
    case ZStatus::Stuck:
        return Z_BUF_ERROR;
    case ZStatus::Corrupt:
        return Z_DATA_ERROR;
    case ZStatus::NoMemory:
        return Z_MEM_ERROR;
    case ZStatus::Misuse:
        break;
    }
    return Z_STREAM_ERROR;
}

Shim *shim_of(z_streamp strm, bool deflating)
{
    if (strm == Z_NULL || strm->state == Z_NULL)
        return nullptr;
    Shim *sh = strm->state;
    if (sh->strm != strm || sh->deflating != deflating)
        return nullptr;
    return sh;
}

bool version_ok(const char *version, int stream_size)
{
    return version != Z_NULL && version[0] == ZLIB_VERSION[0] &&
           stream_size == int(sizeof(z_stream));
}

// A new record on the stream, or null.
Shim *attach(z_streamp strm, bool deflating)
{
    Shim *sh = heap_new<Shim>();
    if (!sh)
        return nullptr;
    sh->strm      = strm;
    sh->deflating = deflating;
    strm->state   = sh;
    return sh;
}

void detach(z_streamp strm)
{
    heap_delete(strm->state);
    strm->state = Z_NULL;
}

const char *message(Str why)
{
    return why.empty() ? Z_NULL : why.data();
}

// The stream's fields after a call that consumed and produced.
void sync_out(z_streamp strm, Span<const u8> in, Span<u8> out, Str why, u32 check, int data_type)
{
    strm->total_in += strm->avail_in - in.size();
    strm->total_out += strm->avail_out - out.size();
    strm->next_in   = const_cast<Bytef *>(in.data());
    strm->avail_in  = uInt(in.size());
    strm->next_out  = out.data();
    strm->avail_out = uInt(out.size());
    strm->msg       = const_cast<char *>(message(why));
    strm->adler     = check;
    strm->data_type = data_type;
}

void fresh(z_streamp strm, u32 check, int data_type)
{
    strm->total_in = strm->total_out = 0;
    strm->msg                        = Z_NULL;
    strm->adler                      = check;
    strm->data_type                  = data_type;
}

bool io_ok(z_streamp strm)
{
    return strm->next_out != Z_NULL && (strm->next_in != Z_NULL || strm->avail_in == 0);
}

// windowBits as the one argument zlib packs a format into.
bool inflate_format(int wb, ZFormat *f, u8 *bits)
{
    if (wb < 0) {
        if (wb < -15)
            return false;
        *f    = ZFormat::Raw;
        *bits = u8(-wb);
    } else if (wb < 16) {
        *f    = ZFormat::Zlib;
        *bits = u8(wb);
    } else if (wb < 32) {
        *f    = ZFormat::Gzip;
        *bits = u8(wb - 16);
    } else if (wb < 48) {
        *f    = ZFormat::Auto;
        *bits = u8(wb - 32);
    } else {
        return false;
    }
    return true;
}

} // namespace

extern "C" {

const char *zlibVersion(void)
{
    return ZLIB_VERSION;
}

uLong zlibCompileFlags(void)
{
    // 32-bit uInt, uLong, voidpf and z_off_t; no gz* to compress with.
    return 1 | (1 << 2) | (1 << 4) | (1 << 6) | (1UL << 16);
}

const char *zError(int err)
{
    return MESSAGES[err < -6 || err > 2 ? 9 : 2 - err];
}

// ------------------------------------------------------------- deflate

int deflateInit2_(z_streamp strm, int level, int method, int windowBits, int memLevel,
                  int strategy, const char *version, int stream_size)
{
    if (!version_ok(version, stream_size))
        return Z_VERSION_ERROR;
    if (strm == Z_NULL)
        return Z_STREAM_ERROR;
    strm->msg = Z_NULL;

    ZFormat f = ZFormat::Zlib;
    int bits  = windowBits;
    if (windowBits < 0) {
        f    = ZFormat::Raw;
        bits = -windowBits;
    } else if (windowBits > 15) {
        f    = ZFormat::Gzip;
        bits = windowBits - 16;
    }
    if (method != Z_DEFLATED || bits < 0 || bits > 15 || memLevel < 1 ||
        memLevel > MAX_MEM_LEVEL || strategy < 0 || strategy > Z_FIXED)
        return Z_STREAM_ERROR;

    Shim *sh = attach(strm, true);
    if (!sh)
        return Z_MEM_ERROR;
    Result<void> r = sh->def.init(level, f, u8(bits), u8(memLevel), ZStrategy(strategy));
    if (r.is_err()) {
        detach(strm);
        if (r.error() == Error::NoMemory) {
            strm->msg = const_cast<char *>(MESSAGES[6]);
            return Z_MEM_ERROR;
        }
        return Z_STREAM_ERROR;
    }
    fresh(strm, sh->def.check(), sh->def.data_type());
    return Z_OK;
}

int deflateInit_(z_streamp strm, int level, const char *version, int stream_size)
{
    return deflateInit2_(strm, level, Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY, version,
                         stream_size);
}

int deflate(z_streamp strm, int flush)
{
    Shim *sh = shim_of(strm, true);
    if (!sh || flush < 0 || flush > Z_BLOCK)
        return Z_STREAM_ERROR;
    if (!io_ok(strm)) {
        strm->msg = const_cast<char *>(MESSAGES[4]);
        return Z_STREAM_ERROR;
    }
    Span<const u8> in(strm->next_in, strm->avail_in);
    Span<u8> out(strm->next_out, strm->avail_out);
    ZStatus s = sh->def.step(in, out, ZFlush(flush));
    sync_out(strm, in, out, sh->def.why(), sh->def.check(), sh->def.data_type());
    return code_of(s);
}

int deflateEnd(z_streamp strm)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    ZStatus s = sh->def.end();
    detach(strm);
    return s == ZStatus::Corrupt ? Z_DATA_ERROR : Z_OK;
}

int deflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength)
{
    Shim *sh = shim_of(strm, true);
    if (!sh || dictionary == Z_NULL)
        return Z_STREAM_ERROR;
    ZStatus s   = sh->def.set_dictionary(Bytes(dictionary, dictLength));
    strm->adler = sh->def.check();
    return code_of(s);
}

int deflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    usize n = sh->def.get_dictionary(Span<u8>(dictionary, dictionary ? usize(1) << 15 : 0));
    if (dictLength != Z_NULL)
        *dictLength = uInt(n);
    return Z_OK;
}

int deflateCopy(z_streamp dest, z_streamp source)
{
    Shim *src = shim_of(source, true);
    if (!src || dest == Z_NULL)
        return Z_STREAM_ERROR;
    Shim *sh = heap_new<Shim>();
    if (!sh)
        return Z_MEM_ERROR;
    if (sh->def.copy_from(src->def) != ZStatus::Ok) {
        heap_delete(sh);
        return Z_MEM_ERROR;
    }
    __builtin_memcpy(dest, source, sizeof(z_stream));
    sh->strm      = dest;
    sh->deflating = true;
    dest->state   = sh;
    return Z_OK;
}

int deflateReset(z_streamp strm)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    ZStatus s = sh->def.reset();
    fresh(strm, sh->def.check(), sh->def.data_type());
    return code_of(s);
}

int deflateResetKeep(z_streamp strm)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    ZStatus s = sh->def.reset_keep();
    fresh(strm, sh->def.check(), sh->def.data_type());
    return code_of(s);
}

int deflateParams(z_streamp strm, int level, int strategy)
{
    Shim *sh = shim_of(strm, true);
    if (!sh || strategy < 0 || strategy > Z_FIXED)
        return Z_STREAM_ERROR;
    Span<const u8> in(strm->next_in, strm->avail_in);
    Span<u8> out(strm->next_out, strm->avail_out);
    ZStatus s = sh->def.params(level, ZStrategy(strategy), in, out);
    sync_out(strm, in, out, sh->def.why(), sh->def.check(), sh->def.data_type());
    return code_of(s);
}

int deflateTune(z_streamp strm, int good_length, int max_lazy, int nice_length, int max_chain)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->def.tune(u32(good_length), u32(max_lazy), u32(nice_length),
                                u32(max_chain)));
}

z_size_t deflateBound_z(z_streamp strm, z_size_t sourceLen)
{
    Shim *sh = shim_of(strm, true);
    return sh ? sh->def.bound(sourceLen) : Deflater::bound_any(sourceLen);
}

uLong deflateBound(z_streamp strm, uLong sourceLen)
{
    return deflateBound_z(strm, sourceLen);
}

int deflatePending(z_streamp strm, unsigned *pending, int *bits)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->def.pending(pending, bits));
}

int deflateUsed(z_streamp strm, int *bits)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    if (bits != Z_NULL)
        *bits = sh->def.used_bits();
    return Z_OK;
}

int deflatePrime(z_streamp strm, int bits, int value)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->def.prime(bits, value));
}

int deflateSetHeader(z_streamp strm, gz_headerp head)
{
    Shim *sh = shim_of(strm, true);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->def.set_header(reinterpret_cast<ZHeader *>(head)));
}

// ------------------------------------------------------------- inflate

int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size)
{
    if (!version_ok(version, stream_size))
        return Z_VERSION_ERROR;
    if (strm == Z_NULL)
        return Z_STREAM_ERROR;
    strm->msg = Z_NULL;

    ZFormat f;
    u8 bits;
    if (!inflate_format(windowBits, &f, &bits))
        return Z_STREAM_ERROR;
    Shim *sh = attach(strm, false);
    if (!sh)
        return Z_MEM_ERROR;
    Result<void> r = sh->inf.init(f, bits);
    if (r.is_err()) {
        detach(strm);
        return r.error() == Error::NoMemory ? Z_MEM_ERROR : Z_STREAM_ERROR;
    }
    fresh(strm, sh->inf.check(), sh->inf.data_type());
    return Z_OK;
}

int inflateInit_(z_streamp strm, const char *version, int stream_size)
{
    return inflateInit2_(strm, MAX_WBITS, version, stream_size);
}

int inflate(z_streamp strm, int flush)
{
    Shim *sh = shim_of(strm, false);
    if (!sh || !io_ok(strm))
        return Z_STREAM_ERROR;
    Span<const u8> in(strm->next_in, strm->avail_in);
    Span<u8> out(strm->next_out, strm->avail_out);
    ZStatus s = sh->inf.step(in, out, ZFlush(u8(flush)));
    sync_out(strm, in, out, sh->inf.why(), sh->inf.check(), sh->inf.data_type());
    return code_of(s);
}

int inflateEnd(z_streamp strm)
{
    if (!shim_of(strm, false))
        return Z_STREAM_ERROR;
    detach(strm);
    return Z_OK;
}

int inflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->inf.set_dictionary(Bytes(dictionary, dictLength)));
}

int inflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    usize n = sh->inf.get_dictionary(Span<u8>(dictionary, dictionary ? usize(1) << 15 : 0));
    if (dictLength != Z_NULL)
        *dictLength = uInt(n);
    return Z_OK;
}

int inflateSync(z_streamp strm)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    Span<const u8> in(strm->next_in, strm->avail_in);
    u64 before = sh->inf.total_in();
    ZStatus s  = sh->inf.sync(in);
    strm->total_in += sh->inf.total_in() - before;
    strm->next_in  = const_cast<Bytef *>(in.data());
    strm->avail_in = uInt(in.size());
    return code_of(s);
}

int inflateSyncPoint(z_streamp strm)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    return sh->inf.sync_point();
}

int inflateCopy(z_streamp dest, z_streamp source)
{
    Shim *src = shim_of(source, false);
    if (!src || dest == Z_NULL)
        return Z_STREAM_ERROR;
    Shim *sh = heap_new<Shim>();
    if (!sh)
        return Z_MEM_ERROR;
    if (sh->inf.copy_from(src->inf) != ZStatus::Ok) {
        heap_delete(sh);
        return Z_MEM_ERROR;
    }
    __builtin_memcpy(dest, source, sizeof(z_stream));
    sh->strm      = dest;
    sh->deflating = false;
    dest->state   = sh;
    return Z_OK;
}

int inflateReset(z_streamp strm)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    ZStatus s = sh->inf.reset();
    fresh(strm, sh->inf.check(), sh->inf.data_type());
    return code_of(s);
}

int inflateReset2(z_streamp strm, int windowBits)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    ZFormat f;
    u8 bits;
    if (!inflate_format(windowBits, &f, &bits))
        return Z_STREAM_ERROR;
    ZStatus s = sh->inf.reset(f, bits);
    if (s == ZStatus::Ok)
        fresh(strm, sh->inf.check(), sh->inf.data_type());
    return code_of(s);
}

int inflatePrime(z_streamp strm, int bits, int value)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->inf.prime(bits, value));
}

long inflateMark(z_streamp strm)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return -(1L << 16);
    return sh->inf.mark();
}

int inflateGetHeader(z_streamp strm, gz_headerp head)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->inf.set_header(reinterpret_cast<ZHeader *>(head)));
}

int inflateValidate(z_streamp strm, int check)
{
    Shim *sh = shim_of(strm, false);
    if (!sh)
        return Z_STREAM_ERROR;
    return code_of(sh->inf.validate(check != 0));
}

int inflateUndermine(z_streamp strm, int)
{
    return shim_of(strm, false) ? Z_DATA_ERROR : Z_STREAM_ERROR;
}

unsigned long inflateCodesUsed(z_streamp strm)
{
    Shim *sh = shim_of(strm, false);
    return sh ? sh->inf.codes_used() : (unsigned long)-1;
}

// ------------------------------------------------------------ one-shot

int compress2_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t sourceLen,
                int level)
{
    if ((sourceLen > 0 && source == Z_NULL) || destLen == Z_NULL || (*destLen > 0 && dest == Z_NULL))
        return Z_STREAM_ERROR;

    z_stream stream = {};
    int err         = deflateInit(&stream, level);
    if (err != Z_OK)
        return err;

    stream.next_out  = dest;
    stream.avail_out = uInt(*destLen);
    stream.next_in   = const_cast<Bytef *>(source);
    stream.avail_in  = uInt(sourceLen);
    err              = deflate(&stream, Z_FINISH);

    *destLen = z_size_t(stream.next_out - dest);
    deflateEnd(&stream);
    return err == Z_STREAM_END ? Z_OK : err == Z_OK ? Z_BUF_ERROR : err;
}

int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level)
{
    z_size_t got = *destLen;
    int ret      = compress2_z(dest, &got, source, sourceLen, level);
    *destLen     = got;
    return ret;
}

int compress_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t sourceLen)
{
    return compress2_z(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION);
}

int compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen)
{
    return compress2(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION);
}

z_size_t compressBound_z(z_size_t sourceLen)
{
    z_size_t bound = sourceLen + (sourceLen >> 12) + (sourceLen >> 14) + (sourceLen >> 25) + 13;
    return bound < sourceLen ? z_size_t(-1) : bound;
}

uLong compressBound(uLong sourceLen)
{
    return compressBound_z(sourceLen);
}

int uncompress2_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t *sourceLen)
{
    if (sourceLen == Z_NULL || (*sourceLen > 0 && source == Z_NULL) || destLen == Z_NULL ||
        (*destLen > 0 && dest == Z_NULL))
        return Z_STREAM_ERROR;

    z_stream stream = {};
    Bytef one;
    if (*destLen == 0 && dest == Z_NULL)
        dest = &one; // next_out cannot be NULL

    stream.next_in  = const_cast<Bytef *>(source);
    stream.avail_in = uInt(*sourceLen);
    int err         = inflateInit(&stream);
    if (err != Z_OK)
        return err;

    stream.next_out  = dest;
    stream.avail_out = uInt(*destLen);
    do {
        err = inflate(&stream, Z_NO_FLUSH);
    } while (err == Z_OK);

    // What was used, and what was made.
    *sourceLen -= stream.avail_in;
    *destLen -= stream.avail_out;
    usize left_in = stream.avail_in;

    inflateEnd(&stream);
    return err == Z_STREAM_END                  ? Z_OK
           : err == Z_NEED_DICT                 ? Z_DATA_ERROR
           : err == Z_BUF_ERROR && left_in == 0 ? Z_DATA_ERROR
                                                : err;
}

int uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong *sourceLen)
{
    z_size_t got = *destLen, used = *sourceLen;
    int ret      = uncompress2_z(dest, &got, source, &used);
    *sourceLen   = used;
    *destLen     = got;
    return ret;
}

int uncompress_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t sourceLen)
{
    z_size_t used = sourceLen;
    return uncompress2_z(dest, destLen, source, &used);
}

int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen)
{
    uLong used = sourceLen;
    return uncompress2(dest, destLen, source, &used);
}

// ------------------------------------------------------------ checksums

uLong adler32_z(uLong adler, const Bytef *buf, z_size_t len)
{
    if (buf == Z_NULL)
        return 1;
    return adler32_update(u32(adler), Bytes(buf, len));
}

uLong adler32(uLong adler, const Bytef *buf, uInt len)
{
    return adler32_z(adler, buf, len);
}

uLong adler32_combine64(uLong adler1, uLong adler2, z_off64_t len2)
{
    if (len2 < 0)
        return 0xffffffffUL;
    return adler32_combine(u32(adler1), u32(adler2), u64(len2));
}

uLong adler32_combine(uLong adler1, uLong adler2, z_off_t len2)
{
    return adler32_combine64(adler1, adler2, len2);
}

uLong crc32_z(uLong crc, const Bytef *buf, z_size_t len)
{
    if (buf == Z_NULL)
        return 0;
    return crc32_update(u32(crc), Bytes(buf, len));
}

uLong crc32(uLong crc, const Bytef *buf, uInt len)
{
    return crc32_z(crc, buf, len);
}

uLong crc32_combine_gen64(z_off64_t len2)
{
    return len2 < 0 ? 0 : crc32_combine_gen(u64(len2));
}

uLong crc32_combine_gen(z_off_t len2)
{
    return crc32_combine_gen64(len2);
}

uLong crc32_combine_op(uLong crc1, uLong crc2, uLong op)
{
    return crc32_combine_op(u32(crc1), u32(crc2), u32(op));
}

uLong crc32_combine64(uLong crc1, uLong crc2, z_off64_t len2)
{
    return crc32_combine_op(crc1, crc2, crc32_combine_gen64(len2));
}

uLong crc32_combine(uLong crc1, uLong crc2, z_off_t len2)
{
    return crc32_combine64(crc1, crc2, len2);
}

} // extern "C"
