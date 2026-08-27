// A program's usage block: asked for, stdout and 0; got wrong, stderr and 2.
// A translation unit of its own, so a binary that never calls it does not
// carry it.
#pragma once

#include "kernel/str.h"
#include "kernel/task.h"
#include "rt.h"

// The block on stdout, and 0.
Task<i32> usage_asked(Str text);

// The block on stderr, and 2.
Task<i32> usage_error(Str text);
