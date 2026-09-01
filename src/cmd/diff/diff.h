// The two halves of /bin/diff that need no syscall: the comparison and the
// three output formats. Pure, so test/CMakeLists.txt compiles them straight
// into the unit suite (doc/Testing.md §2).
#pragma once

#include "kernel/span.h"
#include "kernel/str.h"
#include "kernel/string.h"
#include "kernel/types.h"
#include "kernel/vec.h"

// What a comparison ignores. -B is not here: it drops whole hunks rather than
// changing what two lines are.
constexpr u32 DIFF_ICASE  = 1; // -i
constexpr u32 DIFF_FOLDWS = 2; // -b
constexpr u32 DIFF_NOWS   = 4; // -w
constexpr u32 DIFF_BLANKS = 8; // -B

// One side: the lines, a hash of each under the same flags, and whether the
// file stopped without a newline — which makes its last line its own.
struct DiffText {
    Span<const Str> lines;
    Span<const u64> hash;
    bool nonl = false;
};

// A run that differs, half-open and zero-based.
struct DiffHunk {
    u32 a0, a1, b0, b1;
};

// Two lines under the flags.
bool diff_same(Str a, Str b, u32 flags);

u64 diff_hash(Str s, u32 flags);

// A hash per line. False is out of memory.
bool diff_hashes(Span<const Str> lines, u32 flags, Vec<u64> &out);

// The edit script, in line order. False is out of memory.
bool diff_compare(const DiffText &a, const DiffText &b, u32 flags, Vec<DiffHunk> &out);

// ------------------------------------------------------------------- output

// One side as the emitters see it.
struct DiffLines {
    Span<const Str> line;
    bool nonl = false; // the last line ended without a newline
};

// One hunk, ed-style: "3,4c3" and the lines either side of "---".
bool emit_normal(const DiffLines &a, const DiffLines &b, DiffHunk h, String &out);

// How many hunks from `from` share a group: those within 2*ctx of each other,
// which is what makes one "@@" out of two nearby changes.
usize diff_group(Span<const DiffHunk> h, usize from, u32 ctx);

bool emit_unified(const DiffLines &a, const DiffLines &b, Span<const DiffHunk> g, u32 ctx,
                  String &out);

bool emit_context(const DiffLines &a, const DiffLines &b, Span<const DiffHunk> g, u32 ctx,
                  String &out);

// "--- n1\tt1" and "+++ n2\tt2", or context's "*** " and "--- ".
bool emit_header(bool ctx, Str n1, Str t1, Str n2, Str t2, String &out);
