// getenv's pure half. Private: the port sees only <stdlib.h>'s getenv.
//
// Split because proc_env is braam_proc's and braam_compat_pure links into
// tests.wasm, where reaching braam_proc is a link error. The interning is the
// part worth testing, and it is all on this side.
#pragma once

#include "kernel/str.h"

// A NUL-terminated copy of value, in a block of its own that outlives every
// later call. The same name answers the same pointer; two names answer two,
// both valid at once. Empty value, or no memory, is nullptr.
char *env_intern(Str name, Str value);
