// <bzlib.h> for a port: libbzip2 1.0.8's API over braam::bzip2
// (src/bzip2/bzip2.h), whose output is libbzip2's to the byte. Group A: every
// one of these is pure computation.
//
// bzalloc, bzfree and opaque are accepted and never called; the heap is used.
// verbosity is accepted and prints nothing. Not here, each a compile error at
// the call: BZFILE and everything that takes one -- BZ2_bzRead, BZ2_bzWrite,
// BZ2_bzopen and the rest of the stdio half.
#pragma once

#include <stdio.h>
#include <sys/cdefs.h>

#define BZ_RUN    0
#define BZ_FLUSH  1
#define BZ_FINISH 2

#define BZ_OK               0
#define BZ_RUN_OK           1
#define BZ_FLUSH_OK         2
#define BZ_FINISH_OK        3
#define BZ_STREAM_END       4
#define BZ_SEQUENCE_ERROR   (-1)
#define BZ_PARAM_ERROR      (-2)
#define BZ_MEM_ERROR        (-3)
#define BZ_DATA_ERROR       (-4)
#define BZ_DATA_ERROR_MAGIC (-5)
#define BZ_IO_ERROR         (-6)
#define BZ_UNEXPECTED_EOF   (-7)
#define BZ_OUTBUFF_FULL     (-8)
#define BZ_CONFIG_ERROR     (-9)

#define BZ_API(func) func
#define BZ_EXTERN    extern

#define BZ_MAX_UNUSED 5000

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *next_in;         // next input byte
    unsigned int avail_in; // bytes available at next_in
    unsigned int total_in_lo32;
    unsigned int total_in_hi32;

    char *next_out;         // next output byte
    unsigned int avail_out; // room at next_out
    unsigned int total_out_lo32;
    unsigned int total_out_hi32;

    void *state; // not visible by applications

    void *(*bzalloc)(void *, int, int); // accepted, unused
    void (*bzfree)(void *, void *);     // accepted, unused
    void *opaque;                       // accepted, unused
} bz_stream;

int BZ2_bzCompressInit(bz_stream *strm, int blockSize100k, int verbosity, int workFactor);
int BZ2_bzCompress(bz_stream *strm, int action);
int BZ2_bzCompressEnd(bz_stream *strm);
int BZ2_bzDecompressInit(bz_stream *strm, int verbosity, int small);
int BZ2_bzDecompress(bz_stream *strm);
int BZ2_bzDecompressEnd(bz_stream *strm);

int BZ2_bzBuffToBuffCompress(char *dest, unsigned int *destLen, char *source,
                             unsigned int sourceLen, int blockSize100k, int verbosity,
                             int workFactor);
int BZ2_bzBuffToBuffDecompress(char *dest, unsigned int *destLen, char *source,
                               unsigned int sourceLen, int small, int verbosity);

const char *BZ2_bzlibVersion(void);

#define BZLIB_ABSENT_FILE BRAAM_ABSENT("BZFILE; b_read the file and BZ2_bzDecompress() it")

typedef void BZFILE;

BZFILE *BZ2_bzReadOpen(int *bzerror, FILE *f, int verbosity, int small, void *unused,
                       int nUnused) BZLIB_ABSENT_FILE;
void BZ2_bzReadClose(int *bzerror, BZFILE *b) BZLIB_ABSENT_FILE;
void BZ2_bzReadGetUnused(int *bzerror, BZFILE *b, void **unused, int *nUnused) BZLIB_ABSENT_FILE;
int BZ2_bzRead(int *bzerror, BZFILE *b, void *buf, int len) BZLIB_ABSENT_FILE;
BZFILE *BZ2_bzWriteOpen(int *bzerror, FILE *f, int blockSize100k, int verbosity,
                        int workFactor) BZLIB_ABSENT_FILE;
void BZ2_bzWrite(int *bzerror, BZFILE *b, void *buf, int len) BZLIB_ABSENT_FILE;
void BZ2_bzWriteClose(int *bzerror, BZFILE *b, int abandon, unsigned int *nbytes_in,
                      unsigned int *nbytes_out) BZLIB_ABSENT_FILE;
void BZ2_bzWriteClose64(int *bzerror, BZFILE *b, int abandon, unsigned int *nbytes_in_lo32,
                        unsigned int *nbytes_in_hi32, unsigned int *nbytes_out_lo32,
                        unsigned int *nbytes_out_hi32) BZLIB_ABSENT_FILE;
BZFILE *BZ2_bzopen(const char *path, const char *mode) BZLIB_ABSENT_FILE;
BZFILE *BZ2_bzdopen(int fd, const char *mode) BZLIB_ABSENT_FILE;
int BZ2_bzread(BZFILE *b, void *buf, int len) BZLIB_ABSENT_FILE;
int BZ2_bzwrite(BZFILE *b, void *buf, int len) BZLIB_ABSENT_FILE;
int BZ2_bzflush(BZFILE *b) BZLIB_ABSENT_FILE;
void BZ2_bzclose(BZFILE *b) BZLIB_ABSENT_FILE;
const char *BZ2_bzerror(BZFILE *b, int *errnum) BZLIB_ABSENT_FILE;

#ifdef __cplusplus
}
#endif
