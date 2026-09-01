#include "kernel/alloc.h"
#include "kernel/fmt.h"
#include "kernel/vec.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"

// A tree summed, over proc/io.h's TreeWalk — the walk copy_tree and /bin/find
// are written on. The walk is pre-order and a total is post-order, so a stack
// of levels here emits one as the walk leaves it.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    du [-a | -s] [-hk] [<path>...]\n"
    "Options:\n"
    "    -a    a line for every file, not only every directory\n"
    "    -s    one line per argument, and nothing under it\n"
    "    -h    scale the sizes for a reader\n"
    "    -k    kibibytes, the default, and what undoes -h\n";

// FS_BLOCK, restated as proc/size.h restates it. Every count here is one of
// these until it is printed.
constexpr u64 BLOCK = 512;

// The count's column, as df's are.
constexpr usize W_COUNT = 7;

u64 blocks_of(u64 bytes)
{
    return (bytes + BLOCK - 1) / BLOCK;
}

// 10G, 121M, 512B — one decimal below ten, as df and ls print it.
void human(Buf<32> &b, u64 bytes)
{
    constexpr Str UNIT[] = { "B", "K", "M", "G", "T", "P" };
    usize u              = 0;
    u64 whole = bytes, rem = 0;
    while (whole >= 1024 && u + 1 < sizeof(UNIT) / sizeof(UNIT[0])) {
        rem = whole % 1024;
        whole /= 1024;
        u++;
    }
    b.put(whole);
    if (u && whole < 10) {
        u64 tenth = (rem * 10) / 1024;
        if (tenth)
            b.put('.').put(tenth);
    }
    b.put(UNIT[u]);
}

// A directory the walk is inside, and what has been counted under it.
struct DuLevel {
    String path;
    u64 blocks = 0;
};

// Everything that outlives an await, in one heap block.
struct Duer {
    bool all    = false; // -a
    bool sum    = false; // -s
    bool scaled = false; // -h

    Vec<DuLevel> stack;
    String path; // what the walk fills
    DirEntry ent;
    String row;
};

// What a failed write exits with: ^C stays 130, as it does everywhere else.
i32 write_bad(Error e)
{
    return e == Error::Cancelled ? 130 : 1;
}

// One line: the count in a column, two spaces, the path. A column and not
// du(1)'s tab — the grid is cells and has no tab stop (Concept.md §2.3).
Task<i32> emit(Duer &st, u64 blocks, Str path)
{
    Buf<32> n;
    if (st.scaled)
        human(n, blocks * BLOCK);
    else
        n.put((blocks + 1) / 2); // kibibytes, rounded once and at the end

    Buf<32> col;
    col.put_right(n.str(), W_COUNT).put("  ");

    st.row.clear();
    if (!st.row.append(col.str()) || !st.row.append(path) || !st.row.push('\n'))
        co_return 1;
    if (Result<void> w = co_await File::stdout().write(st.row.str()); w.is_err())
        co_return write_bad(w.error());
    co_return 0;
}

// The level the walk has left: its total prints, then belongs to the directory
// above it. Under -s only the operand's own line is left to print.
Task<i32> leave(Duer &st)
{
    DuLevel lv = move(st.stack.back());
    st.stack.pop();
    if (!st.stack.empty()) {
        st.stack.back().blocks += lv.blocks;
        if (st.sum)
            co_return 0;
    }
    Task<i32> t = emit(st, lv.blocks, lv.path.str());
    if (!t)
        co_return 1;
    co_return co_await t;
}

Task<i32> walk_one(Duer &st, Str root)
{
    // Not followed: a link named on the command line is the link, as find's
    // operand is.
    Result<FileInfo> s = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_of(root, false))
        s = co_await t;
    if (s.is_err()) {
        if (s.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> x = errln("du", root, s.error()))
            co_await x;
        co_return 1;
    }

    u64 own = blocks_of(s.value().size);
    if (s.value().kind != SYS_KIND_DIR) {
        // A file named on the command line is its own total; v7's manual
        // called printing nothing here a bug in v7.
        Task<i32> t = emit(st, own, root);
        if (!t)
            co_return 1;
        co_return co_await t;
    }

    while (!st.stack.empty())
        st.stack.pop();
    DuLevel top;
    if (!top.path.assign(root))
        co_return 1;
    top.blocks = own;
    if (!st.stack.push(move(top)))
        co_return 1;

    i32 status = 0;
    TreeWalk walk(root);
    for (;;) {
        Result<bool> more = co_await walk.next(st.path, st.ent);
        if (more.is_err()) {
            if (more.error() == Error::Cancelled)
                co_return 130;
            // The walk dropped that level; the depth rule below pops it.
            if (Task<void> x = errln("du", walk.at().empty() ? root : walk.at(), more.error()))
                co_await x;
            if (more.error() == Error::NoMemory)
                co_return 1;
            status = 1;
            continue;
        }
        if (!more.value())
            break;

        // A name carries no '/', so counting them under the root is the depth.
        Str under  = st.path.str().substr(walk.root_len());
        usize deep = 0;
        for (usize i = 0; i < under.size(); i++)
            if (under[i] == '/')
                deep++;

        while (st.stack.size() > deep) {
            Task<i32> t = leave(st);
            if (!t)
                co_return 1;
            if (i32 bad = co_await t)
                co_return bad;
        }

        u64 blocks = blocks_of(st.ent.size);
        if (st.ent.kind == SYS_KIND_DIR) {
            DuLevel lv;
            if (!lv.path.assign(st.path.str()))
                co_return 1;
            lv.blocks = blocks;
            if (!st.stack.push(move(lv)))
                co_return 1;
            continue;
        }

        st.stack.back().blocks += blocks;
        if (st.all) {
            Task<i32> t = emit(st, blocks, st.path.str());
            if (!t)
                co_return 1;
            if (i32 bad = co_await t)
                co_return bad;
        }
    }

    while (!st.stack.empty()) {
        Task<i32> t = leave(st);
        if (!t)
            co_return 1;
        if (i32 bad = co_await t)
            co_return bad;
    }
    co_return status;
}

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("du: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Duer *st = heap_new<Duer>();
    if (!st) {
        co_await write_all(SYS_STDERR, "du: out of memory\n");
        co_return 1;
    }
    struct Free {
        ~Free() { heap_delete(p); }
        Duer *p;
    } free_duer{ st };

    OptParse p(args, Opts{ "ahks", "" });
    for (Opt o;;) {
        Result<bool> r = p.next(o);
        if (r.is_err()) {
            if (Task<i32> t = complain("bad option", Str(&o.name, 1)))
                co_return co_await t;
            co_return 1;
        }
        if (!r.value())
            break;
        if (o.name == 'a')
            st->all = true;
        else if (o.name == 's')
            st->sum = true;
        else
            st->scaled = o.name == 'h';
    }
    if (st->all && st->sum) {
        if (Task<i32> t = complain("-a and -s ask for different listings", {}))
            co_return co_await t;
        co_return 1;
    }

    Args rest  = p.rest();
    usize n    = rest.size();
    i32 status = 0;
    for (usize i = 0; i < (n ? n : 1); i++) {
        Task<i32> t = walk_one(*st, n ? rest[i] : Str("."));
        if (!t)
            co_return 1;
        if (i32 bad = co_await t) {
            if (bad == 130)
                co_return 130;
            status = 1;
        }
    }

    if (Result<void> w = co_await File::stdout().flush(); w.is_err())
        co_return write_bad(w.error());
    co_return status;
}
