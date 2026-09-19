// Zstandard, RFC 8878: zstd 1.6.0's own C API, whole. `braam::zstd`, asked for
// by name:
//
//     braam_add_program(NAME zstdpipe SOURCES zstdpipe.cpp LIBS braam::zstd)
//
// libzstd by Yann Collet and others at Meta, vendored verbatim under lib/
// beside this file under the BSD licence (LICENSE). The output is libzstd's,
// byte for byte, because it is libzstd. A PORT target reaches the same header
// as <zstd.h>.
//
// Not here: ZDICT_* (dictBuilder/), the pre-1.0 formats (legacy/) and ZBUFF_*.
// ZSTD_c_nbWorkers accepts 0 alone, as in any single-threaded build.
//
// Memory, all of it from the heap and none of it in a frame: a decoder is 94
// KiB plus the window, 8 MiB for what level 19 makes. A compressing stream of
// unknown size is 3.5 MiB at level 3 (the default) and 89.5 MiB at 19; 20 and
// up exceed a process's 100 MiB. A known size shrinks both.
#pragma once

#include "lib/zstd.h"
