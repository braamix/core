// DevFs — the character devices, mounted on /dev (Concept.md §5.1). The tree is
// flat and its entries are a fixed table; nothing here is stored, so a read is
// answered rather than fetched.
//
// It lives in src/fs/ rather than src/user/, as ProcFs does not: it reads no
// scheduler and no screen, only host_random.
#pragma once

#include "fs.h"

Fs *devfs_create();
