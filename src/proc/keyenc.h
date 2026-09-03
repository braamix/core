// What a program sends for a key: Table 2 of doc/ANSI_Escape_Codes.md §5.
//
// The kernel never turns a keystroke into bytes; a program feeding a guest or
// a remote host encodes its own. `simbesm' is the first caller and an ssh
// client is the next, so the table is here rather than in either.
//
// Pure: no syscall, no state, no timer -- a Key arrives whole, so the Escape
// key is a code and not an ambiguous byte (§6.5).
#pragma once

#include "kernel/key.h"

// ESC [ 24 ; 16 ~ -- a modified F12, the longest there is.
constexpr usize KEY_ENC_MAX = 8;

// The bytes for `k', or 0 for a key that sends nothing. Never partial: a
// caller feeds all of it or none.
usize key_encode(const Key &k, char out[KEY_ENC_MAX]);
