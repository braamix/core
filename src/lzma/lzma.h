// liblzma 5.8.4's own C API, whole: `braam::lzma`, asked for by name. The
// Braam-shaped half is lzma/xz.h; a PORT target reaches this one as <lzma.h>.
//
// Not here, each a compile error at the call: lzma_stream_encoder_mt,
// lzma_stream_encoder_mt_memusage and lzma_stream_decoder_mt, which need
// threads. lzma_physmem and lzma_cputhreads answer 0.
#pragma once

// Before lzma.h, which otherwise reaches for <inttypes.h> and a libc.
#include <stddef.h>
#include <stdint.h>

#include "liblzma/api/lzma.h"

#define LZMA_BRAAM_NO_THREADS                                             \
    __attribute__((                                                       \
        unavailable("braam::lzma has no threads: lzma_stream_encoder or " \
                    "lzma_stream_decoder — doc/Programming_Manual.md")))

#ifdef __cplusplus
extern "C" {
#endif

extern LZMA_API(lzma_ret)
    lzma_stream_encoder_mt(lzma_stream *strm,
                           const lzma_mt *options) lzma_nothrow LZMA_BRAAM_NO_THREADS;
extern LZMA_API(uint64_t)
    lzma_stream_encoder_mt_memusage(const lzma_mt *options) lzma_nothrow LZMA_BRAAM_NO_THREADS;
extern LZMA_API(lzma_ret)
    lzma_stream_decoder_mt(lzma_stream *strm,
                           const lzma_mt *options) lzma_nothrow LZMA_BRAAM_NO_THREADS;

#ifdef __cplusplus
}
#endif
