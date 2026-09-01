#include "diff.h"
#include "fs/path.h"
#include "kernel/alloc.h"
#include "kernel/fmt.h"
#include "kernel/text.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/time.h"
#include "proc/usage.h"

// The half of /bin/diff that reads and writes. Both files are held whole, so
// the comparison is bounded by this process's memory (Concept.md §4.1);
// diffreg.cpp and emit.cpp are the half with no syscall in it.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    diff [-c|-u|-q] [-C <n>|-U <n>] [-bBiNrw]\n"
    "         [-L <label>] <file1> <file2>\n"
    "Options:\n"
    "    -c -u -q  context, unified, or only whether they differ\n"
    "    -C -U     the same two formats, with a count of lines\n"
    "    -r        walk two directories, and the trees under them\n"
    "    -N        a file only one side has counts as empty\n"
    "    -L        the name to print for a file; twice for both\n"
    "    -i        upper and lower case compare equal\n"
    "    -b -w     -b folds runs of blanks, -w drops them all\n"
    "    -B        a change of blank lines alone does not count\n"
    "Both files are held in memory.\n";

// One block's worth of lines; a longer line takes a block of its own.
constexpr usize DIFF_BLOCK = 64 * 1024;

// How much output is gathered before a write.
constexpr usize DIFF_OUT = 4096;

// diff(1)'s third status, which is usage_error's number as well.
constexpr i32 DIFF_TROUBLE = 2;

struct Want {
    u32 flags  = 0;
    char form  = 'n'; // n normal, u unified, c context, q brief
    u32 ctx    = 3;
    bool rec   = false;
    bool empty = false; // -N
    Str label[2];
    u32 labels = 0;
};

// One file read whole: blocks that never move, and a view per line. A String
// never appended past its reserved capacity never reallocates, which is what
// keeps the views valid.
struct DiffFile {
    Vec<String> blocks;
    Vec<Str> lines;
    Vec<u64> hash;
    u64 mtime   = 0;
    bool binary = false;
    bool nonl   = false;
};

struct Pair {
    DiffFile a, b;
    Vec<DiffHunk> hunks;
};

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("diff: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

// 2 beats 1 beats 0.
i32 worse(i32 a, i32 b)
{
    if (a == DIFF_TROUBLE || b == DIFF_TROUBLE)
        return DIFF_TROUBLE;
    return a > b ? a : b;
}

Task<Result<void>> spill(String &out, bool force)
{
    if (out.empty() || (!force && out.size() < DIFF_OUT))
        co_return {};
    Result<void> w = co_await write_all(SYS_STDOUT, out.str());
    out.clear();
    co_return w;
}

bool keep(DiffFile &f, Str line)
{
    if (f.blocks.empty() || f.blocks.back().size() + line.size() > f.blocks.back().capacity()) {
        String b;
        if (!b.reserve(line.size() > DIFF_BLOCK ? line.size() : DIFF_BLOCK))
            return false;
        if (!f.blocks.push(move(b)))
            return false;
    }
    String &b = f.blocks.back();
    usize at  = b.size();
    if (!b.append(line))
        return false;
    return f.lines.push(Str(b.data() + at, line.size()));
}

// The whole file, split into lines. Chunks are split here rather than through
// a line reader: a coroutine per line is a stack frame per line.
Task<Result<void>> slurp(Str path, DiffFile &f)
{
    u32 fd   = SYS_STDIN;
    bool own = false;
    if (path != "-") {
        Result<i32> r = co_await open_read(path);
        if (r.is_err())
            co_return Err(r.error());
        fd  = u32(r.value());
        own = true;
    }

    String pending;
    Result<void> bad;
    bool oom = false;
    for (;;) {
        Result<String> r = co_await read_chunk(fd);
        if (r.is_err()) {
            if (r.error() != Error::Closed)
                bad = Err(r.error());
            break;
        }
        String chunk = move(r.value());
        Str s        = chunk.str();
        if (s.find('\0') != Str::npos) {
            f.binary = true;
            break;
        }
        if (s.empty())
            continue;
        f.nonl = s[s.size() - 1] != '\n';
        while (!s.empty()) {
            usize i = s.find('\n');
            if (i == Str::npos) {
                oom = !pending.append(s);
                break;
            }
            Str line = s.substr(0, i);
            if (!pending.empty()) {
                oom  = !pending.append(line);
                line = pending.str();
            }
            oom = oom || !keep(f, line);
            pending.clear();
            if (oom)
                break;
            s = s.substr(i + 1);
        }
        if (oom)
            break;
    }
    if (!oom && !f.binary && !pending.empty())
        oom = !keep(f, pending.str());
    if (own)
        co_await close_fd(fd);
    if (oom)
        co_return Err(Error::NoMemory);
    co_return bad;
}

// GNU's header stamp, in UTC: this system has no timezone.
void put_time(Buf<64> &b, u64 ms)
{
    Civil c  = civil(i64(ms / 1000));
    auto two = [&b](u32 v) { b.put(char('0' + v / 10 % 10)).put(char('0' + v % 10)); };
    u32 year = c.year < 0 ? 0 : u32(c.year);
    two(year / 100);
    two(year);
    b.put('-');
    two(c.month);
    b.put('-');
    two(c.day);
    b.put(' ');
    two(c.hour);
    b.put(':');
    two(c.min);
    b.put(':');
    two(c.sec);
    u32 msec = u32(ms % 1000);
    b.put('.').put(char('0' + msec / 100));
    two(msec);
    b.put("000000 +0000");
}

Str shown(const Want &w, u32 side, Str path)
{
    return side < w.labels ? w.label[side] : path;
}

// One line of prose, and the spill it may earn.
Task<Result<void>> say(String &out, Str a, Str b = {}, Str c = {}, Str d = {}, Str e = {})
{
    if (!out.append(a) || !out.append(b) || !out.append(c) || !out.append(d) || !out.append(e) ||
        !out.push('\n'))
        co_return Err(Error::NoMemory);
    co_return co_await spill(out, false);
}

// Two files. 0 the same, 1 different, 2 trouble. `banner` is printed in front
// of the first line of output, and is empty outside a directory walk.
Task<i32> compare_files(const Want &w, Str p1, Str p2, bool miss1, bool miss2, Str banner,
                        String &out)
{
    Pair *pp = heap_new<Pair>();
    if (!pp) {
        co_await write_all(SYS_STDERR, "diff: out of memory\n");
        co_return DIFF_TROUBLE;
    }
    struct Free {
        ~Free() { heap_delete(p); }
        Pair *p;
    } free_pair{ pp };

    Str path[2]  = { p1, p2 };
    bool miss[2] = { miss1, miss2 };
    DiffFile *f[2]{ &pp->a, &pp->b };
    for (usize i = 0; i < 2; i++) {
        if (miss[i])
            continue;
        if (Result<void> r = co_await slurp(path[i], *f[i]); r.is_err()) {
            co_await errln("diff", path[i], r.error());
            co_return r.error() == Error::Cancelled ? 130 : DIFF_TROUBLE;
        }
        if (path[i] != "-")
            if (Result<FileInfo> s = co_await stat_of(path[i]); s.is_ok())
                f[i]->mtime = s.value().mtime;
    }

    Str n1 = shown(w, 0, p1), n2 = shown(w, 1, p2);
    if (pp->a.binary || pp->b.binary) {
        if ((co_await say(out, "Binary files ", n1, " and ", n2, " differ")).is_err())
            co_return DIFF_TROUBLE;
        co_return 1;
    }

    if (!diff_hashes(pp->a.lines, w.flags, pp->a.hash) ||
        !diff_hashes(pp->b.lines, w.flags, pp->b.hash)) {
        co_await write_all(SYS_STDERR, "diff: out of memory\n");
        co_return DIFF_TROUBLE;
    }

    DiffText ta{ pp->a.lines, pp->a.hash, pp->a.nonl };
    DiffText tb{ pp->b.lines, pp->b.hash, pp->b.nonl };
    if (!diff_compare(ta, tb, w.flags, pp->hunks)) {
        co_await write_all(SYS_STDERR, "diff: out of memory\n");
        co_return DIFF_TROUBLE;
    }
    if (pp->hunks.empty())
        co_return 0;

    if (w.form == 'q') {
        if ((co_await say(out, "Files ", n1, " and ", n2, " differ")).is_err())
            co_return DIFF_TROUBLE;
        co_return 1;
    }

    if (!banner.empty() && (!out.append(banner) || !out.push('\n'))) {
        co_await write_all(SYS_STDERR, "diff: out of memory\n");
        co_return DIFF_TROUBLE;
    }

    DiffLines la{ pp->a.lines, pp->a.nonl }, lb{ pp->b.lines, pp->b.nonl };
    bool ok = true;
    if (w.form == 'u' || w.form == 'c') {
        Buf<64> t1, t2;
        put_time(t1, pp->a.mtime);
        put_time(t2, pp->b.mtime);
        ok = emit_header(w.form == 'c', n1, t1.str(), n2, t2.str(), out);
    }

    Span<const DiffHunk> hs = pp->hunks;
    for (usize i = 0; ok && i < hs.size();) {
        if (w.form == 'n') {
            ok = emit_normal(la, lb, hs[i], out);
            i++;
        } else {
            usize to = diff_group(hs, i, w.ctx);
            ok       = w.form == 'u' ? emit_unified(la, lb, hs.subspan(i, to - i), w.ctx, out)
                                     : emit_context(la, lb, hs.subspan(i, to - i), w.ctx, out);
            i        = to;
        }
        if (ok)
            if (Result<void> r = co_await spill(out, false); r.is_err())
                co_return r.error() == Error::Cancelled ? 130 : DIFF_TROUBLE;
    }
    if (!ok) {
        co_await write_all(SYS_STDERR, "diff: out of memory\n");
        co_return DIFF_TROUBLE;
    }
    co_return 1;
}

int name_cmp(Str a, Str b)
{
    usize n = a.size() < b.size() ? a.size() : b.size();
    for (usize i = 0; i < n; i++)
        if (a[i] != b[i])
            return u8(a[i]) < u8(b[i]) ? -1 : 1;
    return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

bool is_dir(u32 kind)
{
    return kind == SYS_KIND_DIR;
}

// One directory pair being merged, and how far through it the walk is.
struct Level {
    String p1, p2;
    Vec<DirEntry> l0, l1;
    usize x = 0, y = 0;
};

// Both listings, pushed as a level. Err is out of memory; ok(false) is a
// directory that would not open, already reported.
Task<Result<bool>> push_level(Vec<Level> &st, Str p1, Str p2)
{
    Level lv;
    if (!lv.p1.assign(p1) || !lv.p2.assign(p2))
        co_return Err(Error::NoMemory);
    Str path[2]            = { p1, p2 };
    Vec<DirEntry> *into[2] = { &lv.l0, &lv.l1 };
    for (usize i = 0; i < 2; i++) {
        Result<Vec<DirEntry>> r = co_await list_dir(path[i]);
        if (r.is_err()) {
            co_await errln("diff", path[i], r.error());
            co_return false;
        }
        *into[i] = move(r.value());
    }
    if (!st.push(move(lv)))
        co_return Err(Error::NoMemory);
    co_return true;
}

// Two directories, merged name by name and descended in place, so a
// subdirectory's output lands where its name does. The levels are an explicit
// stack: a deep tree must not be a deep chain of coroutine frames.
Task<i32> compare_dirs(const Want &w, Str d1, Str d2, Str banner, String &out)
{
    Vec<Level> st;
    i32 status = 0;
    if (Result<bool> r = co_await push_level(st, d1, d2); r.is_err()) {
        co_await write_all(SYS_STDERR, "diff: out of memory\n");
        co_return DIFF_TROUBLE;
    } else if (!r.value())
        co_return DIFF_TROUBLE;

    while (!st.empty()) {
        usize top = st.size() - 1;
        if (st[top].x >= st[top].l0.size() && st[top].y >= st[top].l1.size()) {
            st.pop();
            continue;
        }

        usize x = st[top].x, y = st[top].y;
        int c = x >= st[top].l0.size() ? 1
                : y >= st[top].l1.size()
                    ? -1
                    : name_cmp(st[top].l0[x].name.str(), st[top].l1[y].name.str());

        // The two paths, copied out: a push below moves the level they came
        // from, and a Str into it would not survive that.
        const DirEntry &e = c > 0 ? st[top].l1[y] : st[top].l0[x];
        String q[2];
        bool bad = path_join(st[top].p1.str(), e.name.str(), q[0]).is_err() ||
                   path_join(st[top].p2.str(), e.name.str(), q[1]).is_err();
        if (bad) {
            co_await write_all(SYS_STDERR, "diff: out of memory\n");
            co_return DIFF_TROUBLE;
        }
        String bn;
        if (!bn.assign(banner) || !bn.push(' ') || !bn.append(q[0].str()) || !bn.push(' ') ||
            !bn.append(q[1].str())) {
            co_await write_all(SYS_STDERR, "diff: out of memory\n");
            co_return DIFF_TROUBLE;
        }

        // In one side only: a name to report, or an empty file under -N.
        if (c != 0) {
            usize side = c < 0 ? 0 : 1;
            bool dir   = is_dir(e.kind);
            Str only   = side == 0 ? st[top].p1.str() : st[top].p2.str();
            (c < 0 ? st[top].x : st[top].y)++;
            if (w.empty && !dir)
                status = worse(status, co_await compare_files(w, q[0].str(), q[1].str(), side == 1,
                                                              side == 0, bn.str(), out));
            else {
                if ((co_await say(out, "Only in ", only, ": ", e.name.str())).is_err())
                    co_return DIFF_TROUBLE;
                status = worse(status, 1);
            }
            continue;
        }

        bool d0 = is_dir(st[top].l0[x].kind), dn = is_dir(st[top].l1[y].kind);
        st[top].x++;
        st[top].y++;
        if (d0 && dn) {
            if (!w.rec) {
                if ((co_await say(out, "Common subdirectories: ", q[0].str(), " and ", q[1].str()))
                        .is_err())
                    co_return DIFF_TROUBLE;
            } else if (Result<bool> r = co_await push_level(st, q[0].str(), q[1].str());
                       r.is_err()) {
                co_await write_all(SYS_STDERR, "diff: out of memory\n");
                co_return DIFF_TROUBLE;
            } else if (!r.value())
                status = DIFF_TROUBLE;
        } else if (d0 != dn) {
            Str mid  = d0 ? " is a directory while file " : " is a regular file while file ";
            Str tail = d0 ? " is a regular file" : " is a directory";
            if ((co_await say(out, "File ", q[0].str(), mid, q[1].str(), tail)).is_err())
                co_return DIFF_TROUBLE;
            status = worse(status, 1);
        } else {
            status = worse(status, co_await compare_files(w, q[0].str(), q[1].str(), false, false,
                                                          bn.str(), out));
        }
    }
    co_return status;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Want w;
    bool formed = false;
    OptParse p(args, Opts{ "cuqrNibwB", "CUL" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            Str why = r.error() == Error::NotFound ? "needs a value" : "bad option";
            if (Task<i32> t = complain(why, Str(&o.name, 1)))
                co_return co_await t;
            co_return 1;
        }
        if (!r.value())
            break;
        switch (o.name) {
        case 'c':
        case 'u':
        case 'q':
        case 'C':
        case 'U': {
            char form = o.name == 'C' ? 'c' : o.name == 'U' ? 'u' : o.name;
            if (formed && form != w.form) {
                if (Task<i32> t = complain("only one of -c, -u and -q", {}))
                    co_return co_await t;
                co_return 1;
            }
            w.form = form;
            formed = true;
            if (o.name == 'C' || o.name == 'U') {
                Option<u32> n = parse_u32(o.value);
                if (!n.has_value()) {
                    if (Task<i32> t = complain("not a line count", o.value))
                        co_return co_await t;
                    co_return 1;
                }
                w.ctx = n.value();
            }
            break;
        }
        case 'r':
            w.rec = true;
            break;
        case 'N':
            w.empty = true;
            break;
        case 'i':
            w.flags |= DIFF_ICASE;
            break;
        case 'b':
            w.flags |= DIFF_FOLDWS;
            break;
        case 'w':
            w.flags |= DIFF_NOWS;
            break;
        case 'B':
            w.flags |= DIFF_BLANKS;
            break;
        default: // 'L'
            if (w.labels == 2) {
                if (Task<i32> t = complain("at most two labels", o.value))
                    co_return co_await t;
                co_return 1;
            }
            w.label[w.labels++] = o.value;
            break;
        }
    }

    Args rest = p.rest();
    if (rest.size() != 2) {
        if (Task<i32> t = complain("two files are needed", {}))
            co_return co_await t;
        co_return 1;
    }

    // FreeBSD's diffargs: the banner a directory walk prints in front of each
    // pair, which is this command line with the two operands replaced.
    Buf<96> banner;
    banner.put("diff");
    for (usize i = 1; i + rest.size() < args.size(); i++)
        banner.put(' ').put(args[i]);

    // A directory on one side and a file on the other names the same leaf in
    // the directory, which is what makes `diff dir file` work.
    u32 kind[2] = { SYS_KIND_FILE, SYS_KIND_FILE };
    for (usize i = 0; i < 2; i++) {
        if (rest[i] == "-")
            continue;
        Result<FileInfo> s = co_await stat_of(rest[i]);
        if (s.is_err()) {
            co_await errln("diff", rest[i], s.error());
            co_return s.error() == Error::Cancelled ? 130 : DIFF_TROUBLE;
        }
        kind[i] = s.value().kind;
    }

    String out, spliced;
    Str path[2] = { rest[0], rest[1] };
    i32 status  = 0;
    if (kind[0] == SYS_KIND_DIR && kind[1] == SYS_KIND_DIR) {
        status = co_await compare_dirs(w, path[0], path[1], banner.str(), out);
    } else {
        for (usize i = 0; i < 2; i++) {
            if (kind[i] != SYS_KIND_DIR)
                continue;
            if (path_join(path[i], path_basename(path[1 - i]), spliced).is_err()) {
                co_await write_all(SYS_STDERR, "diff: out of memory\n");
                co_return DIFF_TROUBLE;
            }
            path[i] = spliced.str();
        }
        status = co_await compare_files(w, path[0], path[1], false, false, {}, out);
    }

    if (Result<void> r = co_await spill(out, true); r.is_err())
        status = worse(status, r.error() == Error::Cancelled ? 130 : DIFF_TROUBLE);
    co_return status;
}
