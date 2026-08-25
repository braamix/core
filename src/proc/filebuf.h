// The half of a buffered stream that performs no syscall: the bytes in hand,
// the rune boundaries in them, and the room left. A File (file.h) owns one.
//
// The block is lent, never owned. One buffer serves both directions: held() is
// what a reader has not taken or a writer has not sent, room() what may follow.
#pragma once

#include "kernel/str.h"
#include "kernel/string.h"
#include "kernel/types.h"

// What take() did.
enum class RuneStep {
    Got,  // `out` holds a codepoint
    Need, // the bytes in hand do not finish one
};

// What take_line() did.
enum class LineStep {
    Done,     // a newline was found and `out` holds the line
    Need,     // `out` holds a fragment; there are more bytes to come
    NoMemory, // `out` would not grow
};

struct FileBuf {
    // Lends `cap` bytes at `p`, `len` of them already holding data.
    void adopt(char *p, usize cap, usize len = 0);

    bool ready() const { return b_ != nullptr; }
    usize cap() const { return cap_; }
    usize size() const { return len_ - pos_; }
    bool empty() const { return pos_ == len_; }

    // What a reader has not taken, or a writer has not sent.
    Str held() const { return Str(b_ + pos_, len_ - pos_); }

    void consume(usize n);

    // Moves the held bytes to the front.
    void compact();

    char *tail() { return b_ + len_; }
    usize room() const { return cap_ - len_; }
    void filled(usize n) { len_ += n; }
    void reset();

    // ---- reading -------------------------------------------------------

    RuneStep take(char32_t &out);

    // Consumes one byte and yields U+FFFD: a sequence end of input cut short.
    char32_t take_broken();

    // Puts `c` back, in front of the held bytes. False when there is no room.
    bool unget(char32_t c);

    // Appends the held bytes up to a newline, consuming what it appended.
    LineStep take_line(String &out, bool keep_nl);

    // ---- writing -------------------------------------------------------

    bool append(Str s);

    // The bytes the encoding took, or 0 when it would not fit.
    usize append_rune(char32_t c);

private:
    char *b_   = nullptr;
    usize cap_ = 0;
    usize pos_ = 0; // taken prefix of the block
    usize len_ = 0; // one past the last byte held
};
