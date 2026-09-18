// <zlib.h> for a port: zlib 1.3.2.1's API over braam::zlib (src/zlib/zlib.h),
// whose deflate is zlib's to the byte. Group A: every one of these is pure
// computation.
//
// zalloc, zfree and opaque are accepted and never called; the heap is used.
// Not here, each a compile error at the call: gz* (a gzip file is inflate()
// with windowBits 31 over bytes b_read gave), inflateBack*, inflateResetKeep
// and get_crc_table.
#pragma once

#include <stddef.h>
#include <sys/cdefs.h>

#define ZLIB_VERSION "1.3.2.1"
#define ZLIB_VERNUM 0x1321
#define ZLIB_VER_MAJOR 1
#define ZLIB_VER_MINOR 3
#define ZLIB_VER_REVISION 2
#define ZLIB_VER_SUBREVISION 1

#define MAX_MEM_LEVEL 9
#define MAX_WBITS 15

#define OF(args) args
#define Z_ARG(args) args
#define ZEXTERN extern
#define ZEXPORT
#define ZEXPORTVA
#define FAR
#ifdef ZLIB_CONST
#define z_const const
#else
#define z_const
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char Byte;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef Byte Bytef;
typedef char charf;
typedef int intf;
typedef uInt uIntf;
typedef uLong uLongf;
typedef void const *voidpc;
typedef void *voidpf;
typedef void *voidp;
typedef unsigned int z_crc_t;
typedef size_t z_size_t;
typedef long z_off_t;
typedef long long z_off64_t;

typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void (*free_func)(voidpf opaque, voidpf address);

struct internal_state;

typedef struct z_stream_s {
    z_const Bytef *next_in; // next input byte
    uInt avail_in;        // bytes available at next_in
    uLong total_in;       // input bytes read so far

    Bytef *next_out; // next output byte
    uInt avail_out;  // room at next_out
    uLong total_out; // bytes output so far

    z_const char *msg;            // last error message, or NULL
    struct internal_state *state; // not visible by applications

    alloc_func zalloc; // accepted, unused
    free_func zfree;   // accepted, unused
    voidpf opaque;     // accepted, unused

    int data_type;  // deflate: Z_BINARY or Z_TEXT; inflate: decoding state
    uLong adler;    // Adler-32 or CRC-32 of the data so far
    uLong reserved; // reserved for future use
} z_stream;

typedef z_stream *z_streamp;

typedef struct gz_header_s {
    int text;       // true if compressed data believed to be text
    uLong time;     // modification time
    int xflags;     // extra flags (not used when writing a gzip file)
    int os;         // operating system
    Bytef *extra;   // pointer to extra field or Z_NULL if none
    uInt extra_len; // extra field length (valid if extra != Z_NULL)
    uInt extra_max; // space at extra (only when reading header)
    Bytef *name;    // pointer to zero-terminated file name or Z_NULL
    uInt name_max;  // space at name (only when reading header)
    Bytef *comment; // pointer to zero-terminated comment or Z_NULL
    uInt comm_max;  // space at comment (only when reading header)
    int hcrc;       // true if there was or will be a header crc
    int done;       // true when done reading gzip header
} gz_header;

typedef gz_header *gz_headerp;

#define Z_NO_FLUSH 0
#define Z_PARTIAL_FLUSH 1
#define Z_SYNC_FLUSH 2
#define Z_FULL_FLUSH 3
#define Z_FINISH 4
#define Z_BLOCK 5
#define Z_TREES 6

#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NEED_DICT 2
#define Z_ERRNO (-1)
#define Z_STREAM_ERROR (-2)
#define Z_DATA_ERROR (-3)
#define Z_MEM_ERROR (-4)
#define Z_BUF_ERROR (-5)
#define Z_VERSION_ERROR (-6)

#define Z_NO_COMPRESSION 0
#define Z_BEST_SPEED 1
#define Z_BEST_COMPRESSION 9
#define Z_DEFAULT_COMPRESSION (-1)

#define Z_FILTERED 1
#define Z_HUFFMAN_ONLY 2
#define Z_RLE 3
#define Z_FIXED 4
#define Z_DEFAULT_STRATEGY 0

#define Z_BINARY 0
#define Z_TEXT 1
#define Z_ASCII Z_TEXT
#define Z_UNKNOWN 2

#define Z_DEFLATED 8

#define Z_NULL 0

#define zlib_version zlibVersion()

const char *zlibVersion(void);
uLong zlibCompileFlags(void);
const char *zError(int err);

int deflateInit_(z_streamp strm, int level, const char *version, int stream_size);
int deflateInit2_(z_streamp strm, int level, int method, int windowBits, int memLevel,
                  int strategy, const char *version, int stream_size);
int deflate(z_streamp strm, int flush);
int deflateEnd(z_streamp strm);
int deflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength);
int deflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength);
int deflateCopy(z_streamp dest, z_streamp source);
int deflateReset(z_streamp strm);
int deflateResetKeep(z_streamp strm);
int deflateParams(z_streamp strm, int level, int strategy);
int deflateTune(z_streamp strm, int good_length, int max_lazy, int nice_length, int max_chain);
uLong deflateBound(z_streamp strm, uLong sourceLen);
z_size_t deflateBound_z(z_streamp strm, z_size_t sourceLen);
int deflatePending(z_streamp strm, unsigned *pending, int *bits);
int deflateUsed(z_streamp strm, int *bits);
int deflatePrime(z_streamp strm, int bits, int value);
int deflateSetHeader(z_streamp strm, gz_headerp head);

int inflateInit_(z_streamp strm, const char *version, int stream_size);
int inflateInit2_(z_streamp strm, int windowBits, const char *version, int stream_size);
int inflate(z_streamp strm, int flush);
int inflateEnd(z_streamp strm);
int inflateSetDictionary(z_streamp strm, const Bytef *dictionary, uInt dictLength);
int inflateGetDictionary(z_streamp strm, Bytef *dictionary, uInt *dictLength);
int inflateSync(z_streamp strm);
int inflateSyncPoint(z_streamp strm);
int inflateCopy(z_streamp dest, z_streamp source);
int inflateReset(z_streamp strm);
int inflateReset2(z_streamp strm, int windowBits);
int inflatePrime(z_streamp strm, int bits, int value);
long inflateMark(z_streamp strm);
int inflateGetHeader(z_streamp strm, gz_headerp head);
int inflateValidate(z_streamp strm, int check);
int inflateUndermine(z_streamp strm, int subvert);
unsigned long inflateCodesUsed(z_streamp strm);

#define deflateInit(strm, level) deflateInit_((strm), (level), ZLIB_VERSION, (int)sizeof(z_stream))
#define inflateInit(strm) inflateInit_((strm), ZLIB_VERSION, (int)sizeof(z_stream))
#define deflateInit2(strm, level, method, windowBits, memLevel, strategy)                     \
    deflateInit2_((strm), (level), (method), (windowBits), (memLevel), (strategy),            \
                  ZLIB_VERSION, (int)sizeof(z_stream))
#define inflateInit2(strm, windowBits)                                                        \
    inflateInit2_((strm), (windowBits), ZLIB_VERSION, (int)sizeof(z_stream))

int compress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
int compress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level);
uLong compressBound(uLong sourceLen);
int compress_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t sourceLen);
int compress2_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t sourceLen,
                int level);
z_size_t compressBound_z(z_size_t sourceLen);
int uncompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen);
int uncompress2(Bytef *dest, uLongf *destLen, const Bytef *source, uLong *sourceLen);
int uncompress_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t sourceLen);
int uncompress2_z(Bytef *dest, z_size_t *destLen, const Bytef *source, z_size_t *sourceLen);

uLong adler32(uLong adler, const Bytef *buf, uInt len);
uLong adler32_z(uLong adler, const Bytef *buf, z_size_t len);
uLong adler32_combine(uLong adler1, uLong adler2, z_off_t len2);
uLong adler32_combine64(uLong adler1, uLong adler2, z_off64_t len2);
uLong crc32(uLong crc, const Bytef *buf, uInt len);
uLong crc32_z(uLong crc, const Bytef *buf, z_size_t len);
uLong crc32_combine(uLong crc1, uLong crc2, z_off_t len2);
uLong crc32_combine64(uLong crc1, uLong crc2, z_off64_t len2);
uLong crc32_combine_gen(z_off_t len2);
uLong crc32_combine_gen64(z_off64_t len2);
uLong crc32_combine_op(uLong crc1, uLong crc2, uLong op);

#define ZLIB_ABSENT_GZ BRAAM_ABSENT("gz*; b_read the file and inflate() it with windowBits 31")

typedef struct gzFile_s *gzFile;

gzFile gzopen(const char *path, const char *mode) ZLIB_ABSENT_GZ;
gzFile gzdopen(int fd, const char *mode) ZLIB_ABSENT_GZ;
int gzbuffer(gzFile file, unsigned size) ZLIB_ABSENT_GZ;
int gzsetparams(gzFile file, int level, int strategy) ZLIB_ABSENT_GZ;
int gzread(gzFile file, voidp buf, unsigned len) ZLIB_ABSENT_GZ;
z_size_t gzfread(voidp buf, z_size_t size, z_size_t nitems, gzFile file) ZLIB_ABSENT_GZ;
int gzwrite(gzFile file, voidpc buf, unsigned len) ZLIB_ABSENT_GZ;
z_size_t gzfwrite(voidpc buf, z_size_t size, z_size_t nitems, gzFile file) ZLIB_ABSENT_GZ;
int gzprintf(gzFile file, const char *format, ...) ZLIB_ABSENT_GZ;
int gzputs(gzFile file, const char *s) ZLIB_ABSENT_GZ;
char *gzgets(gzFile file, char *buf, int len) ZLIB_ABSENT_GZ;
int gzputc(gzFile file, int c) ZLIB_ABSENT_GZ;
int gzgetc(gzFile file) ZLIB_ABSENT_GZ;
int gzungetc(int c, gzFile file) ZLIB_ABSENT_GZ;
int gzflush(gzFile file, int flush) ZLIB_ABSENT_GZ;
z_off_t gzseek(gzFile file, z_off_t offset, int whence) ZLIB_ABSENT_GZ;
int gzrewind(gzFile file) ZLIB_ABSENT_GZ;
z_off_t gztell(gzFile file) ZLIB_ABSENT_GZ;
z_off_t gzoffset(gzFile file) ZLIB_ABSENT_GZ;
int gzeof(gzFile file) ZLIB_ABSENT_GZ;
int gzdirect(gzFile file) ZLIB_ABSENT_GZ;
int gzclose(gzFile file) ZLIB_ABSENT_GZ;
int gzclose_r(gzFile file) ZLIB_ABSENT_GZ;
int gzclose_w(gzFile file) ZLIB_ABSENT_GZ;
const char *gzerror(gzFile file, int *errnum) ZLIB_ABSENT_GZ;
void gzclearerr(gzFile file) ZLIB_ABSENT_GZ;

typedef unsigned (*in_func)(void *, const unsigned char **);
typedef int (*out_func)(void *, unsigned char *, unsigned);

int inflateBackInit_(z_streamp strm, int windowBits, unsigned char *window, const char *version,
                     int stream_size) BRAAM_ABSENT("inflateBack; inflate() does the same");
int inflateBack(z_streamp strm, in_func in, void *in_desc, out_func out, void *out_desc)
    BRAAM_ABSENT("inflateBack; inflate() does the same");
int inflateBackEnd(z_streamp strm) BRAAM_ABSENT("inflateBack; inflate() does the same");
int inflateResetKeep(z_streamp strm) BRAAM_ABSENT("inflateResetKeep; inflateReset()");
const z_crc_t *get_crc_table(void) BRAAM_ABSENT("get_crc_table; crc32() the bytes");

#ifdef __cplusplus
}
#endif
