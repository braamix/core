#include "diff.h"
#include "kernel/text.h"

// Myers' O(ND) walk of the edit graph, refined to linear space: each box is
// split at its middle snake and the halves are pushed on a stack. FreeBSD's
// diffreg.c is Hunt-Szymanski over a temp file instead.

namespace {

// The least a box may cost before the search gives up on an optimal split.
constexpr u32 DIFF_COST_MIN = 256;

char fold_one(char c, u32 flags)
{
    if ((flags & DIFF_ICASE) && c >= 'A' && c <= 'Z')
        return char(c - 'A' + 'a');
    return c;
}

// The next byte of `s` under the flags, or -1 at the end. -w drops every
// blank; -b makes a run of them one space, and a trailing run nothing.
int canon(Str s, usize &i, u32 flags)
{
    if (flags & DIFF_NOWS) {
        while (i < s.size() && is_space(s[i]))
            i++;
    } else if ((flags & DIFF_FOLDWS) && i < s.size() && is_space(s[i])) {
        while (i < s.size() && is_space(s[i]))
            i++;
        if (i < s.size())
            return ' ';
    }
    if (i >= s.size())
        return -1;
    return u8(fold_one(s[i++], flags));
}

u32 isqrt(u32 n)
{
    u32 r = 0;
    while ((r + 1) * (r + 1) <= n)
        r++;
    return r;
}

struct DiffBox {
    u32 a0, a1, b0, b1;
};

struct DiffRun {
    const DiffText *a;
    const DiffText *b;
    i32 *v1; // furthest x forward, diagonal-indexed
    i32 *v2; // and backward
    u32 flags;
    u32 cap;
};

// The hash first, the bytes only where it matched. A last line that ends in a
// newline is not the same as one that does not.
bool eq(const DiffRun &r, u32 i, u32 j)
{
    if (r.a->hash[i] != r.b->hash[j])
        return false;
    if (r.a->nonl != r.b->nonl && i + 1 == r.a->lines.size() && j + 1 == r.b->lines.size())
        return false;
    return diff_same(r.a->lines[i], r.b->lines[j], r.flags);
}

// The box's middle snake, or — past the cost cap — the furthest point reached,
// which splits it without claiming to be optimal.
void middle(const DiffRun &r, DiffBox box, u32 &sx, u32 &sy, u32 &ex, u32 &ey)
{
    i32 n = i32(box.a1 - box.a0), m = i32(box.b1 - box.b0);
    i32 delta = n - m;
    bool odd  = (delta & 1) != 0;
    i32 dmax  = (n + m + 1) / 2;
    i32 *v1 = r.v1, *v2 = r.v2;

    v1[1] = 0;
    v2[1] = 0;
    for (i32 d = 0; d <= dmax; d++) {
        for (i32 k = -d; k <= d; k += 2) {
            i32 x  = (k == -d || (k != d && v1[k - 1] < v1[k + 1])) ? v1[k + 1] : v1[k - 1] + 1;
            i32 y  = x - k;
            i32 x0 = x, y0 = y;
            while (x < n && y < m && eq(r, box.a0 + u32(x), box.b0 + u32(y))) {
                x++;
                y++;
            }
            v1[k]  = x;
            i32 kr = delta - k;
            if (odd && kr >= -(d - 1) && kr <= d - 1 && x + v2[kr] >= n) {
                sx = box.a0 + u32(x0);
                sy = box.b0 + u32(y0);
                ex = box.a0 + u32(x);
                ey = box.b0 + u32(y);
                return;
            }
        }
        for (i32 k = -d; k <= d; k += 2) {
            i32 x  = (k == -d || (k != d && v2[k - 1] < v2[k + 1])) ? v2[k + 1] : v2[k - 1] + 1;
            i32 y  = x - k;
            i32 x0 = x, y0 = y;
            while (x < n && y < m && eq(r, box.a1 - 1 - u32(x), box.b1 - 1 - u32(y))) {
                x++;
                y++;
            }
            v2[k]  = x;
            i32 kf = delta - k;
            if (!odd && kf >= -d && kf <= d && v1[kf] + x >= n) {
                sx = box.a0 + u32(n - x);
                sy = box.b0 + u32(m - y);
                ex = box.a0 + u32(n - x0);
                ey = box.b0 + u32(m - y0);
                return;
            }
        }

        if (d >= i32(r.cap)) {
            i32 best = -1, at = 0;
            for (i32 k = -d; k <= d; k += 2) {
                i32 x = v1[k], y = v1[k] - k;
                if (x < 0 || x > n || y < 0 || y > m)
                    continue;
                if (x + y > best) {
                    best = x + y;
                    at   = k;
                }
            }
            i32 x = best > 0 ? v1[at] : (n > 0 ? 1 : 0);
            i32 y = best > 0 ? v1[at] - at : (m > 0 ? 1 : 0);
            sx = ex = box.a0 + u32(x);
            sy = ey = box.b0 + u32(y);
            return;
        }
    }
    // Unreachable: an overlap always turns up by dmax.
    sx = ex = box.a1;
    sy = ey = box.b1;
}

bool all_blank(Span<const Str> v, u32 lo, u32 hi)
{
    for (u32 i = lo; i < hi; i++)
        for (usize j = 0; j < v[i].size(); j++)
            if (!is_space(v[i][j]))
                return false;
    return true;
}

} // namespace

bool diff_same(Str a, Str b, u32 flags)
{
    if (!(flags & (DIFF_ICASE | DIFF_FOLDWS | DIFF_NOWS)))
        return a == b;
    usize i = 0, j = 0;
    for (;;) {
        int x = canon(a, i, flags), y = canon(b, j, flags);
        if (x != y)
            return false;
        if (x < 0)
            return true;
    }
}

u64 diff_hash(Str s, u32 flags)
{
    u64 h   = 14695981039346656037ull;
    usize i = 0;
    for (int c; (c = canon(s, i, flags)) >= 0;) {
        h ^= u64(u32(c));
        h *= 1099511628211ull;
    }
    return h;
}

bool diff_hashes(Span<const Str> lines, u32 flags, Vec<u64> &out)
{
    if (!out.reserve(lines.size()))
        return false;
    for (usize i = 0; i < lines.size(); i++)
        if (!out.push(diff_hash(lines[i], flags)))
            return false;
    return true;
}

bool diff_compare(const DiffText &a, const DiffText &b, u32 flags, Vec<DiffHunk> &out)
{
    u32 n = u32(a.lines.size()), m = u32(b.lines.size());

    // ja[i] is the line of b that a's line i matches, or -1.
    Vec<i32> ja;
    if (!ja.resize(n))
        return false;
    for (u32 i = 0; i < n; i++)
        ja[i] = -1;

    // One diagonal array each, sized for the whole problem and reused by every
    // box. Only v[1] needs setting per box; the rest is written before read.
    usize span = usize(n) + usize(m) + 5;
    Vec<i32> v1, v2;
    if (!v1.resize(span) || !v2.resize(span))
        return false;
    usize mid = span / 2;

    u32 cap = isqrt(n + m);
    DiffRun r{
        &a, &b, v1.data() + mid, v2.data() + mid, flags, cap > DIFF_COST_MIN ? cap : DIFF_COST_MIN
    };

    Vec<DiffBox> stack;
    if (!stack.push(DiffBox{ 0, n, 0, m }))
        return false;
    while (!stack.empty()) {
        DiffBox box = stack.back();
        stack.pop();

        while (box.a0 < box.a1 && box.b0 < box.b1 && eq(r, box.a0, box.b0)) {
            ja[box.a0] = i32(box.b0);
            box.a0++;
            box.b0++;
        }
        while (box.a0 < box.a1 && box.b0 < box.b1 && eq(r, box.a1 - 1, box.b1 - 1)) {
            box.a1--;
            box.b1--;
            ja[box.a1] = i32(box.b1);
        }
        if (box.a0 == box.a1 || box.b0 == box.b1)
            continue;

        u32 sx, sy, ex, ey;
        middle(r, box, sx, sy, ex, ey);
        for (u32 i = sx, j = sy; i < ex; i++, j++)
            ja[i] = i32(j);
        if (!stack.push(DiffBox{ box.a0, sx, box.b0, sy }) ||
            !stack.push(DiffBox{ ex, box.a1, ey, box.b1 }))
            return false;
    }

    for (u32 i = 0, j = 0; i < n || j < m;) {
        if (i < n && ja[i] >= 0 && u32(ja[i]) == j) {
            i++;
            j++;
            continue;
        }
        u32 a0 = i, b0 = j;
        while (i < n && ja[i] < 0)
            i++;
        j = i < n ? u32(ja[i]) : m;
        if ((flags & DIFF_BLANKS) && all_blank(a.lines, a0, i) && all_blank(b.lines, b0, j))
            continue;
        if (!out.push(DiffHunk{ a0, i, b0, j }))
            return false;
    }
    return true;
}
