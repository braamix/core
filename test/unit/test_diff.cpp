#include "cmd/diff/diff.h"
#include "harness.h"
#include "kernel/fmt.h"
#include "kernel/vec.h"

// The half of /bin/diff with no syscall in it: the Myers comparison and the
// three formats. The system suite runs the program; what is here is the edge
// cases a 60-column prompt cannot reach.

namespace {

// A file, as a table of views over one buffer that never regrows — which is
// how /bin/diff holds one, and what keeps the views valid.
struct Side {
    String text;
    Vec<Str> line;
    Vec<u64> hash;
    bool nonl = false;
};

// `s` split on '|', so a fixture reads as the file it stands for. A trailing
// '|' is an empty last line, not a missing one.
bool load(Side &v, Str s, u32 flags)
{
    if (!v.text.reserve(s.size()) || !v.text.assign(s))
        return false;
    Str all = v.text.str();
    for (usize at = 0; at <= all.size();) {
        usize i = all.find('|', at);
        usize n = (i == Str::npos ? all.size() : i) - at;
        if (!v.line.push(all.substr(at, n)))
            return false;
        if (i == Str::npos)
            break;
        at = i + 1;
    }
    if (s.empty())
        v.line.clear();
    return diff_hashes(v.line, flags, v.hash);
}

DiffText text_of(const Side &v)
{
    return DiffText{ v.line, v.hash, v.nonl };
}

DiffLines lines_of(const Side &v)
{
    return DiffLines{ v.line, v.nonl };
}

// Every hunk of `a` against `b`, as "a0,a1,b0,b1;" — the edit script itself,
// with no formatting in the way.
String script(Str a, Str b, u32 flags = 0)
{
    Side x, y;
    String out;
    Vec<DiffHunk> h;
    if (!load(x, a, flags) || !load(y, b, flags) ||
        !diff_compare(text_of(x), text_of(y), flags, h)) {
        out.assign("oom");
        return out;
    }
    for (usize i = 0; i < h.size(); i++) {
        Buf<64> t;
        t.put(h[i].a0).put(',').put(h[i].a1).put(',').put(h[i].b0).put(',').put(h[i].b1).put(';');
        if (!out.append(t.str())) {
            out.assign("oom");
            return out;
        }
    }
    return out;
}

bool is(Str got, Str want)
{
    return got == want;
}

// The whole of one format, with newlines shown as '|' so a case is one line.
String render(Str a, Str b, char form, u32 ctx = 3, u32 flags = 0, bool na = false, bool nb = false)
{
    Side x, y;
    String out;
    Vec<DiffHunk> h;
    if (!load(x, a, flags) || !load(y, b, flags)) {
        out.assign("oom");
        return out;
    }
    x.nonl = na;
    y.nonl = nb;
    if (!diff_compare(text_of(x), text_of(y), flags, h)) {
        out.assign("oom");
        return out;
    }

    DiffLines la = lines_of(x), lb = lines_of(y);
    Span<const DiffHunk> hs = h;
    bool ok                 = true;
    for (usize i = 0; ok && i < hs.size();) {
        if (form == 'n') {
            ok = emit_normal(la, lb, hs[i], out);
            i++;
        } else {
            usize to = diff_group(hs, i, ctx);
            ok       = form == 'u' ? emit_unified(la, lb, hs.subspan(i, to - i), ctx, out)
                                   : emit_context(la, lb, hs.subspan(i, to - i), ctx, out);
            i        = to;
        }
    }
    if (!ok) {
        out.assign("oom");
        return out;
    }
    for (usize i = 0; i < out.size(); i++)
        if (out[i] == '\n')
            out[i] = '|';
    if (!out.empty())
        out.pop(); // the trailing separator
    return out;
}

} // namespace

void test_diff()
{
    test_begin("diff");

    // ---------------------------------------------------------- the script

    // Nothing to say, whichever way the two sides are empty.
    CHECK(is(script("", ""), ""));
    CHECK(is(script("a|b|c", "a|b|c"), ""));
    CHECK(is(script("", "a"), "0,0,0,1;"));
    CHECK(is(script("a", ""), "0,1,0,0;"));

    // One line replaced, inserted, deleted; the ends and the middle.
    CHECK(is(script("a|b|c", "a|X|c"), "1,2,1,2;"));
    CHECK(is(script("a|b|c", "X|b|c"), "0,1,0,1;"));
    CHECK(is(script("a|b|c", "a|b|X"), "2,3,2,3;"));
    CHECK(is(script("a|b|c", "a|b|X|c"), "2,2,2,3;"));
    CHECK(is(script("a|b|c", "a|c"), "1,2,1,1;"));

    // Two runs, far enough apart to stay two hunks in the script.
    CHECK(is(script("a|b|c|d|e", "a|X|c|d|Y"), "1,2,1,2;4,5,4,5;"));

    // Nothing in common: one hunk covering both sides whole.
    CHECK(is(script("a|b|c", "x|y|z"), "0,3,0,3;"));

    // A common prefix and suffix around a change, which the trim takes
    // before the search ever runs.
    CHECK(is(script("a|b|c|d|e|f", "a|b|X|Y|e|f"), "2,4,2,4;"));

    // A move reads as a delete and an insert, since an LCS has no third
    // operation. The longer common run is the one kept.
    CHECK(is(script("a|b|c|d", "c|d|a|b"), "0,2,0,0;4,4,2,4;"));

    // Repeated lines: the match has to be a subsequence, not a set.
    CHECK(is(script("a|a|a", "a"), "1,3,1,1;")); // the prefix trim keeps the first
    CHECK(is(script("a", "a|a|a"), "1,1,1,3;"));
    CHECK(is(script("a|b|a|b", "a|b"), "2,4,2,2;"));

    // An empty line is a line.
    CHECK(is(script("a||b", "a|b"), "1,2,1,1;"));

    // ------------------------------------------------------- what it folds

    CHECK(is(script("Abc", "aBC"), "0,1,0,1;"));
    CHECK(is(script("Abc", "aBC", DIFF_ICASE), ""));
    CHECK(is(script("a  b", "a b"), "0,1,0,1;"));
    CHECK(is(script("a  b", "a b", DIFF_FOLDWS), ""));
    CHECK(is(script("a b ", "a b", DIFF_FOLDWS), ""));       // trailing blanks drop
    CHECK(is(script("a b", "ab", DIFF_FOLDWS), "0,1,0,1;")); // a run is not none
    CHECK(is(script("a b", "ab", DIFF_NOWS), ""));
    CHECK(is(script(" a b ", "ab", DIFF_NOWS), ""));
    CHECK(is(script("A b", "a  B", DIFF_ICASE | DIFF_FOLDWS), ""));

    // -B drops a hunk only when every line of it, both sides, is blank.
    CHECK(is(script("a|b", "a||b"), "1,1,1,2;"));
    CHECK(is(script("a|b", "a||b", DIFF_BLANKS), ""));
    CHECK(is(script("a|b", "a| \t |b", DIFF_BLANKS), "")); // blank is any blanks
    CHECK(is(script("a|b", "a|x|b", DIFF_BLANKS), "1,1,1,2;"));
    CHECK(is(script("a|b|c", "a||X|c", DIFF_BLANKS), "1,2,1,3;"));

    // ------------------------------------------------------ normal output

    CHECK(is(render("a|b|c", "a|X|c", 'n'), "2c2|< b|---|> X"));
    CHECK(is(render("a", "a|b", 'n'), "1a2|> b")); // after line 1
    CHECK(is(render("a|b", "a", 'n'), "2d1|< b")); // before line 1 of b
    CHECK(is(render("", "a", 'n'), "0a1|> a"));    // nothing, then a line
    CHECK(is(render("a", "", 'n'), "1d0|< a"));
    CHECK(is(render("a|b|c", "x|y|z", 'n'), "1,3c1,3|< a|< b|< c|---|> x|> y|> z"));
    CHECK(is(render("a|b|c|d|e", "a|X|c|d|Y", 'n'), "2c2|< b|---|> X|5c5|< e|---|> Y"));

    // The marker follows the last line of whichever side lacks a newline.
    CHECK(is(render("a", "a", 'n', 3, 0, true, false),
             "1c1|< a|\\ No newline at end of file|---|> a"));
    CHECK(is(render("a", "a", 'n', 3, 0, false, true),
             "1c1|< a|---|> a|\\ No newline at end of file"));
    CHECK(is(render("a", "a", 'n', 3, 0, true, true), ""));

    // ----------------------------------------------------- unified output

    CHECK(is(render("a|b|c", "a|X|c", 'u'), "@@ -1,3 +1,3 @@| a|-b|+X| c"));
    CHECK(is(render("a", "a|b", 'u'), "@@ -1 +1,2 @@| a|+b"));
    CHECK(is(render("a", "a|b", 'u', 0), "@@ -1,0 +2 @@|+b")); // an empty range
    CHECK(is(render("a|b", "a", 'u', 0), "@@ -2 +1,0 @@|-b"));
    CHECK(is(render("", "a", 'u'), "@@ -0,0 +1 @@|+a"));
    CHECK(is(render("a", "", 'u'), "@@ -1 +0,0 @@|-a"));

    // Two changes inside 2*ctx of each other are one @@; past it, two.
    CHECK(is(render("a|b|c|d|e", "X|b|c|d|Y", 'u', 2), "@@ -1,5 +1,5 @@|-a|+X| b| c| d|-e|+Y"));
    CHECK(is(render("a|b|c|d|e", "X|b|c|d|Y", 'u', 1),
             "@@ -1,2 +1,2 @@|-a|+X| b|@@ -4,2 +4,2 @@| d|-e|+Y"));

    // The context is bounded by the shorter side, so the two ranges stay
    // in step when a hunk sits near the start of a longer file.
    CHECK(is(render("b|c", "a|b|X", 'u', 1), "@@ -1,2 +1,3 @@|+a| b|-c|+X"));

    // ----------------------------------------------------- context output

    CHECK(is(render("a|b|c", "a|X|c", 'c'),
             "***************|*** 1,3 ****|  a|! b|  c|--- 1,3 ----|  a|! X|  c"));

    // A pure insertion prints no old block, a pure deletion no new one.
    CHECK(is(render("a", "a|b", 'c'), "***************|*** 1 ****|--- 1,2 ----|  a|+ b"));
    CHECK(is(render("a|b", "a", 'c'), "***************|*** 1,2 ****|  a|- b|--- 1 ----"));
    CHECK(is(render("a|b|c", "x|y|z", 'c'),
             "***************|*** 1,3 ****|! a|! b|! c|--- 1,3 ----|! x|! y|! z"));

    // ------------------------------------------------------- the long way

    // Past the 256-edit cost ceiling in one box: 2,000 lines against 2,000
    // wholly different ones is still one hunk, and still terminates.
    String big1, big2;
    for (u32 i = 0; i < 2000; i++) {
        Buf<16> a, b;
        a.put(i).put('|');
        b.put(i + 1000000).put('|');
        CHECK(big1.append(a.str()) && big2.append(b.str()));
    }
    big1.pop();
    big2.pop();
    CHECK(is(script(big1.str(), big2.str()), "0,2000,0,2000;"));

    // The same 2,000 lines with one dropped from the front and one added to
    // the end: the trim and the search between them, and two hunks.
    String big3;
    CHECK(big3.append(big1.str().substr(big1.str().find('|') + 1)));
    CHECK(big3.append("|2000"));
    CHECK(is(script(big1.str(), big3.str()), "0,1,0,0;2000,2000,1999,2000;"));
    CHECK(is(script(big1.str(), big1.str()), ""));
}
