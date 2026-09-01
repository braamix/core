// Group B's three decisions that perform no syscall, so that the half which
// can be tested is (doc/Testing.md §2).
#pragma once

#include "kernel/str.h"
#include "kernel/types.h"

// fopen's mode string to SYS_O_* (sysabi.h). 0 for a mode with no leading
// r, w or a. 'b' and 't' are accepted and ignored, 'x' is SYS_O_EXCL, and a
// '+' anywhere past the first byte is the update bit.
u32 fmode_flags(Str mode);

// SYS_KIND_* to a struct stat's st_mode.
u32 stat_mode(u32 kind);

// A path to an st_ino: FNV-1a, so two names compare unequal and one name
// compares equal to itself.
u64 stat_ino(Str path);
