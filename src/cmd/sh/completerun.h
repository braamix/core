// The half of completion that walks the store and reads the shell's own
// tables. Not pure — hence its own file, as condrun.cpp is to cond.cpp and
// glob.cpp is to match.cpp. The arithmetic is complete.h.
#pragma once

#include "kernel/string.h"
#include "kernel/task.h"

// What one Tab produced. `insert` goes in at the cursor and nothing else is
// disturbed; `list` is the candidates in columns, filled only when a second
// Tab asked for them and there was nothing left to insert.
struct CompReply {
    String insert;
    String list;
    usize count = 0;
};

// The completion for `upto` — the line up to the cursor, and nothing after it
// — on a terminal `width` cells wide. `show` is the second Tab.
Task<Result<CompReply>> complete_line(Str upto, u32 width, bool show);
