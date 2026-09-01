// What Tab needs that touches no store: where the word under the cursor is,
// what may finish it, and the arithmetic over a set of candidates. Pure — Str,
// String and Vec and nothing else, which is what lets tests.wasm compile it.
// The walk that produces the candidates is completerun.h.
#pragma once

#include "kernel/str.h"
#include "kernel/string.h"
#include "kernel/vec.h"

// What the word under the cursor is, and so where its candidates come from.
enum class CompKind : u8 {
    None,    // nothing to complete here
    Command, // a command word: functions, builtins, then PATH
    File,    // a path, against the store
    Var,     // a name after `$` or `${`
};

// The word being completed, in the bytes before the cursor.
struct CompSite {
    CompKind kind = CompKind::None;
    usize at      = 0; // where the word begins; the cursor is at at+len
    usize len     = 0;
    char quote    = 0;     // '\'' or '"' when the cursor is inside an unclosed one
    bool braced   = false; // a Var written `${`, so a unique match closes it
};

// The site in `upto`, which is the line up to the cursor and nothing after it.
// Never fails: an unclosed quote is what half a line being typed looks like.
CompSite comp_site(Str upto);

// The literal text `raw` stands for: quotes and backslashes off. `$`, `${` and
// a backtick are left as written, since nothing here expands one.
bool comp_unquote(Str raw, String &out);

// `text` appended to `out` as bytes that read back as themselves. `quote` is
// comp_site's: inside one, only what that quote cannot hold is escaped.
bool comp_quote(Str text, char quote, String &out);

// Everything through the last '/', and what follows it.
void comp_split(Str word, Str &dir, Str &leaf);

// The longest prefix every name shares, cut back to a codepoint boundary.
Str comp_common(const Vec<String> &names);

// Byte order, with adjacent duplicates dropped.
void comp_sort(Vec<String> &v);

// Past this a listing says how many it left out.
constexpr usize COMP_LIST_MAX = 256;

// The names as columns in a `width`-cell terminal, down a column and then
// across, each row newline-terminated.
bool comp_columns(const Vec<String> &names, u32 width, String &out);
