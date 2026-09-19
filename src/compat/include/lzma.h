// <lzma.h> for a port: liblzma 5.8.4's own API, whole, as braam::lzma vendors
// it (src/lzma/lzma.h). Group A: every one of these is pure computation.
//
// Not here, each a compile error at the call: lzma_stream_encoder_mt,
// lzma_stream_encoder_mt_memusage and lzma_stream_decoder_mt, which need
// threads. lzma_physmem and lzma_cputhreads answer 0. A buffer liblzma hands
// back, as lzma_str_from_filters does, is free()d as usual.
#pragma once

#include "lzma/lzma.h"
