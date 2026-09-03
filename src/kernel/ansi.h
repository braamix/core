// The escape-sequence parser: bytes in, grid operations out
// (doc/ANSI_Escape_Codes.md §3 and §4). It drives the screen through the
// screen_* calls and reaches nothing else, so the grid stays the model and an
// escape is one encoding into it.
//
// The state is a Term's, since a sequence may be split across two writes.
#pragma once

#include "screen.h"
#include "str.h"
#include "types.h"

// Parameters a CSI sequence may carry; the rest are dropped, not an error.
enum : u32 { ANSI_PARAMS = 16 };

// Where the state machine is. Ground decodes UTF-8 and paints; the rest are one
// byte at a time.
enum : u8 {
    ANSI_GROUND,
    ANSI_ESC,
    ANSI_CSI,
    ANSI_STR,     // OSC, DCS, APC, PM: discarded up to BEL or ST
    ANSI_STR_ESC, // an ESC inside a string, which ST completes
};

struct Ansi {
    u8 state;
    u8 nparam;
    bool priv;  // a leading '?', '<', '=' or '>'
    bool inter; // an intermediate byte: the sequence dispatches nothing
    bool bad;   // a runaway: consumed to its final byte, then dropped
    u16 len;    // bytes in this sequence
    u16 param[ANSI_PARAMS];

    // A rune split across two writes, held until the rest of it arrives.
    u8 pend[4];
    u8 pend_n;

    bool lnm; // LF is a full new line; set at boot (§6.1)
    bool irm; // a glyph pushes the rest of the line right
    bool awm; // autowrap; set at boot

    // §4.4's one invention: italic is a foreground colour, and the one it
    // replaced comes back.
    bool italic;
    u8 italic_fg;

    // DECSC: the cursor and the style, together.
    u32 save_x, save_y;
    u8 save_fg, save_bg, save_attrs;
    bool save_italic;
    u8 save_italic_fg;

    u32 tabs[SCREEN_MAX_COLS / 32];
};

// Ground, no parameters, LNM and DECAWM set, a tab stop every 8 columns.
void ansi_reset(Ansi &a);

// Bytes into the grid: §4 of doc/ANSI_Escape_Codes.md.
void ansi_write(Term &t, Ansi &a, Str utf8);
