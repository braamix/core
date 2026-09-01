// The three diagnostics the kit's headers answer with, in one place rather
// than a copy per header. BSD's name for this file, so a port that includes it
// by habit gets what it expects: nothing it can see.
#pragma once

// Blocking: the name exists, with a b_* spelling and an awaitable return.
#ifndef BRAAM_BLOCKS
#define BRAAM_BLOCKS(what)                                                                    \
    __attribute__((unavailable("blocking: co_await " what " from compat/cio.h — "             \
                               "doc/Compat.md §4")))
#endif

// The name exists under another spelling and does not block.
#ifndef BRAAM_RENAMED
#define BRAAM_RENAMED(what)                                                                   \
    __attribute__((unavailable("renamed in the port kit: " what " from compat/cio.h — "       \
                               "doc/Compat.md §4")))
#endif

// A name the kit does not supply: the compiler says so at the call site, rather
// than the linker at the end.
#ifndef BRAAM_ABSENT
#define BRAAM_ABSENT(what)                                                                    \
    __attribute__((unavailable("not in the port kit: " what " — doc/Compat.md")))
#endif
