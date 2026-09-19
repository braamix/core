// <zstd.h> for a port: zstd 1.6.0's own API, whole, as braam::zstd vendors it
// (src/zstd/zstd.h). Group A: every one of these is pure computation.
//
// Not here: ZDICT_*, the pre-1.0 formats and ZBUFF_*. ZSTD_c_nbWorkers accepts
// 0 alone.
#pragma once

#include "zstd/zstd.h"
