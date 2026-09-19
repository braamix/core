// liblzma's config.h: a single-threaded, full-featured build for wasm32.
#pragma once

#define HAVE_STDBOOL_H 1
#define HAVE__BOOL     1
#define HAVE_STDINT_H  1
// HAVE_INTTYPES_H left undefined: clang's <inttypes.h> is include_next into a
// libc. sysdefs.h supplies the PRI* it wants.

#define HAVE_VISIBILITY      0
#define TUKLIB_SYMBOL_PREFIX lzma_

#ifndef NDEBUG
#define NDEBUG 1
#endif

#define HAVE_CHECK_CRC32  1
#define HAVE_CHECK_CRC64  1
#define HAVE_CHECK_SHA256 1

#define HAVE_MF_HC3 1
#define HAVE_MF_HC4 1
#define HAVE_MF_BT2 1
#define HAVE_MF_BT3 1
#define HAVE_MF_BT4 1

#define HAVE_ENCODER_LZMA1    1
#define HAVE_ENCODER_LZMA2    1
#define HAVE_ENCODER_DELTA    1
#define HAVE_ENCODER_X86      1
#define HAVE_ENCODER_POWERPC  1
#define HAVE_ENCODER_IA64     1
#define HAVE_ENCODER_ARM      1
#define HAVE_ENCODER_ARMTHUMB 1
#define HAVE_ENCODER_ARM64    1
#define HAVE_ENCODER_SPARC    1
#define HAVE_ENCODER_RISCV    1

#define HAVE_DECODER_LZMA1    1
#define HAVE_DECODER_LZMA2    1
#define HAVE_DECODER_DELTA    1
#define HAVE_DECODER_X86      1
#define HAVE_DECODER_POWERPC  1
#define HAVE_DECODER_IA64     1
#define HAVE_DECODER_ARM      1
#define HAVE_DECODER_ARMTHUMB 1
#define HAVE_DECODER_ARM64    1
#define HAVE_DECODER_SPARC    1
#define HAVE_DECODER_RISCV    1

#define HAVE_LZIP_DECODER 1

#define HAVE___BUILTIN_BSWAPXX        1
#define HAVE___BUILTIN_ASSUME_ALIGNED 1
#define TUKLIB_FAST_UNALIGNED_ACCESS  1

// Left undefined: WORDS_BIGENDIAN, every MYTHREAD_*, every TUKLIB_PHYSMEM_* and
// TUKLIB_CPUCORES_*, and every CPU-specific CRC path.
