#include "diff.h"
#include "kernel/fmt.h"

// The three formats, each one group at a time into a String, so the caller
// writes 4 KB at a time rather than a line at a time.

namespace {

bool put(String &out, Str s)
{
    return out.append(s);
}

bool put_n(String &out, u32 v)
{
    Buf<16> b;
    b.put(v);
    return out.append(b.str());
}

// diff's range: 1-based and inclusive, and an empty one is the line in front.
bool put_range(String &out, u32 lo, u32 hi)
{
    if (lo > hi)
        return put_n(out, hi);
    if (!put_n(out, lo))
        return false;
    return lo == hi ? true : put(out, ",") && put_n(out, hi);
}

// The same, as -u writes it: a start and a count, and 0 lines start one back.
bool put_uni(String &out, u32 lo, u32 hi)
{
    if (lo > hi)
        return put_n(out, hi) && put(out, ",0");
    if (!put_n(out, lo))
        return false;
    return lo == hi ? true : put(out, ",") && put_n(out, hi - lo + 1);
}

// One line, and the marker where the file stopped without a newline.
bool put_line(String &out, Str tag, const DiffLines &s, u32 i)
{
    if (!put(out, tag) || !put(out, s.line[i]) || !out.push('\n'))
        return false;
    if (s.nonl && i + 1 == s.line.size())
        return put(out, "\\ No newline at end of file\n");
    return true;
}

// The group plus its context. Context lines are matched pairs, so the two
// sides take the same count and the shorter end is what bounds it.
void frame(const DiffLines &a, const DiffLines &b, Span<const DiffHunk> g, u32 ctx, u32 &a0,
           u32 &a1, u32 &b0, u32 &b1)
{
    const DiffHunk &last = g[g.size() - 1];
    u32 pre              = ctx;
    if (pre > g[0].a0)
        pre = g[0].a0;
    if (pre > g[0].b0)
        pre = g[0].b0;
    u32 post = ctx;
    if (post > u32(a.line.size()) - last.a1)
        post = u32(a.line.size()) - last.a1;
    if (post > u32(b.line.size()) - last.b1)
        post = u32(b.line.size()) - last.b1;
    a0 = g[0].a0 - pre;
    b0 = g[0].b0 - pre;
    a1 = last.a1 + post;
    b1 = last.b1 + post;
}

} // namespace

bool emit_normal(const DiffLines &a, const DiffLines &b, DiffHunk h, String &out)
{
    char op = h.a0 == h.a1 ? 'a' : h.b0 == h.b1 ? 'd' : 'c';
    if (!put_range(out, h.a0 + 1, h.a1) || !out.push(op) || !put_range(out, h.b0 + 1, h.b1) ||
        !out.push('\n'))
        return false;
    for (u32 i = h.a0; i < h.a1; i++)
        if (!put_line(out, "< ", a, i))
            return false;
    if (op == 'c' && !put(out, "---\n"))
        return false;
    for (u32 j = h.b0; j < h.b1; j++)
        if (!put_line(out, "> ", b, j))
            return false;
    return true;
}

usize diff_group(Span<const DiffHunk> h, usize from, u32 ctx)
{
    usize to = from + 1;
    while (to < h.size() && h[to].a0 <= h[to - 1].a1 + 2 * ctx)
        to++;
    return to;
}

bool emit_unified(const DiffLines &a, const DiffLines &b, Span<const DiffHunk> g, u32 ctx,
                  String &out)
{
    if (g.empty())
        return true;
    u32 a0, a1, b0, b1;
    frame(a, b, g, ctx, a0, a1, b0, b1);

    if (!put(out, "@@ -") || !put_uni(out, a0 + 1, a1) || !put(out, " +") ||
        !put_uni(out, b0 + 1, b1) || !put(out, " @@\n"))
        return false;

    u32 at = a0;
    for (usize k = 0; k < g.size(); k++) {
        for (; at < g[k].a0; at++)
            if (!put_line(out, " ", a, at))
                return false;
        for (u32 i = g[k].a0; i < g[k].a1; i++)
            if (!put_line(out, "-", a, i))
                return false;
        for (u32 j = g[k].b0; j < g[k].b1; j++)
            if (!put_line(out, "+", b, j))
                return false;
        at = g[k].a1;
    }
    for (; at < a1; at++)
        if (!put_line(out, " ", a, at))
            return false;
    return true;
}

bool emit_context(const DiffLines &a, const DiffLines &b, Span<const DiffHunk> g, u32 ctx,
                  String &out)
{
    if (g.empty())
        return true;
    u32 a0, a1, b0, b1;
    frame(a, b, g, ctx, a0, a1, b0, b1);

    // A side is printed only where something on it changed.
    bool olds = false, news = false;
    for (usize k = 0; k < g.size(); k++) {
        olds = olds || g[k].a0 != g[k].a1;
        news = news || g[k].b0 != g[k].b1;
    }

    if (!put(out, "***************\n*** ") || !put_range(out, a0 + 1, a1) || !put(out, " ****\n"))
        return false;
    if (olds) {
        u32 at = a0;
        for (usize k = 0; k < g.size(); k++) {
            for (; at < g[k].a0; at++)
                if (!put_line(out, "  ", a, at))
                    return false;
            Str tag = g[k].b0 == g[k].b1 ? "- " : "! ";
            for (u32 i = g[k].a0; i < g[k].a1; i++)
                if (!put_line(out, tag, a, i))
                    return false;
            at = g[k].a1;
        }
        for (; at < a1; at++)
            if (!put_line(out, "  ", a, at))
                return false;
    }

    if (!put(out, "--- ") || !put_range(out, b0 + 1, b1) || !put(out, " ----\n"))
        return false;
    if (news) {
        u32 at = b0;
        for (usize k = 0; k < g.size(); k++) {
            for (; at < g[k].b0; at++)
                if (!put_line(out, "  ", b, at))
                    return false;
            Str tag = g[k].a0 == g[k].a1 ? "+ " : "! ";
            for (u32 j = g[k].b0; j < g[k].b1; j++)
                if (!put_line(out, tag, b, j))
                    return false;
            at = g[k].b1;
        }
        for (; at < b1; at++)
            if (!put_line(out, "  ", b, at))
                return false;
    }
    return true;
}

bool emit_header(bool ctx, Str n1, Str t1, Str n2, Str t2, String &out)
{
    Str one = ctx ? "*** " : "--- ";
    Str two = ctx ? "--- " : "+++ ";
    return put(out, one) && put(out, n1) && out.push('\t') && put(out, t1) && out.push('\n') &&
           put(out, two) && put(out, n2) && out.push('\t') && put(out, t2) && out.push('\n');
}
