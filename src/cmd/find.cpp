#include "cmd/sh/match.h"
#include "fs/path.h"
#include "kernel/alloc.h"
#include "kernel/fmt.h"
#include "kernel/vec.h"
#include "proc/file.h"
#include "proc/opt.h"
#include "proc/usage.h"

// A tree walked, and each name in it tested against a boolean expression. The
// walk is proc/io.h's TreeWalk — an explicit stack, so there is no depth to
// measure and none to declare. The expression is a node table over argv, and it
// is evaluated by a plain recursion rather than a coroutine: -print is the only
// action, so nothing in the tree writes.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    find <path>... [<expression>]\n"
    "Expression:\n"
    "    -name <pat>    the name matches: * ? [a-z] [!a-z]\n"
    "    -type f|d|l    a file, a directory, or a link\n"
    "    -newer <file>  changed more recently than <file>\n"
    "    -print         print the path; what happens anyway\n"
    "    ! ( )          not, and grouping\n"
    "    -a  -o         and, or; -a is what a space means\n";

// v7 rationed its parse tree with NNODE; the cap bounds the parser's own
// recursion too, which is what a command line of nothing but `!` would reach.
constexpr usize FIND_NODES = 64;

enum class FindOp : u8 { True, And, Or, Not, Print, Name, Type, Newer };

// v7 held an int, a char or a char * in the L and R pointer slots and read each
// back through a private struct shape per primary. Every operand has a field of
// its own here.
struct FindNode {
    FindOp op = FindOp::True;
    u32 l     = 0; // indices, not pointers: the Vec reallocates
    u32 r     = 0;
    Str pat;       // -name's pattern, and -newer's file until it is stat'd
    u32 kind  = 0; // -type
    u64 mtime = 0; // -newer
};

// One name under test. A Str rather than a DirEntry, so the operand itself
// needs no allocation to be tested.
struct FindCand {
    Str name;
    u32 kind  = 0;
    u64 mtime = 0;
};

// Everything that outlives an await, in one heap block: a coroutine frame past
// 512 bytes costs a whole 64 KiB span.
struct Finder {
    Vec<FindNode> nodes;
    u32 root    = 0;
    bool action = false; // a -print appears, so nothing prints implicitly
    Args args;
    usize at = 0; // the word being parsed

    Str why;  // a parse error's complaint
    Str word; // and the word it is about

    String row;  // one line, reused
    String path; // what the walk fills
    DirEntry ent;
};

// ------------------------------------------------------------------- parsing
//
// or := and [-o or];  and := not [[-a] and];  not := [!] not | prim
// prim := ( or ) | -print | -name <pattern> | -type f|d|l | -newer <file>

Str peek(const Finder &st)
{
    return st.at < st.args.size() ? st.args[st.at] : Str();
}

// An aggregate with one field named is a -Wmissing-field-initializers error.
FindNode node_of(FindOp op)
{
    FindNode n;
    n.op = op;
    return n;
}

Result<u32> mk(Finder &st, FindNode n)
{
    if (st.nodes.size() >= FIND_NODES) {
        st.why = "the expression is too long";
        return Err(Error::Invalid);
    }
    if (!st.nodes.push(n))
        return Err(Error::NoMemory);
    return u32(st.nodes.size() - 1);
}

Result<u32> parse_or(Finder &st);

Result<u32> parse_prim(Finder &st)
{
    Str a = peek(st);
    if (a.empty()) {
        st.why = "the expression ends early";
        return Err(Error::Invalid);
    }

    if (a == "(") {
        st.at++;
        u32 in = TRY(parse_or(st));
        if (!(peek(st) == ")")) {
            st.why  = "expected )";
            st.word = peek(st);
            return Err(Error::Invalid);
        }
        st.at++;
        return in;
    }

    st.at++;
    if (a == "-print") {
        st.action = true;
        return mk(st, node_of(FindOp::Print));
    }

    bool valued = a == "-name" || a == "-type" || a == "-newer";
    if (!valued) {
        st.why  = "bad option";
        st.word = a;
        return Err(Error::Invalid);
    }
    if (st.at >= st.args.size()) {
        st.why  = "needs an operand";
        st.word = a;
        return Err(Error::Invalid);
    }
    Str b = st.args[st.at++];

    FindNode n;
    if (a == "-name") {
        n.op  = FindOp::Name;
        n.pat = b;
    } else if (a == "-type") {
        n.op   = FindOp::Type;
        n.kind = b == "f"   ? SYS_KIND_FILE
                 : b == "d" ? SYS_KIND_DIR
                 : b == "l" ? SYS_KIND_LINK
                            : ~u32(0);
        if (n.kind == ~u32(0)) {
            st.why  = "-type takes f, d or l";
            st.word = b;
            return Err(Error::Invalid);
        }
    } else {
        // The stat is a syscall, so the file is kept and read once the whole
        // expression stands (fill_newer below).
        n.op  = FindOp::Newer;
        n.pat = b;
    }
    return mk(st, n);
}

Result<u32> parse_not(Finder &st)
{
    if (!(peek(st) == "!"))
        return parse_prim(st);
    st.at++;
    FindNode n = node_of(FindOp::Not);
    n.l        = TRY(parse_not(st));
    return mk(st, n);
}

Result<u32> parse_and(Finder &st)
{
    u32 left = TRY(parse_not(st));
    Str a    = peek(st);
    // -a is optional: two expressions in a row are one, which is what makes
    // `find . -type f -name x` read the way it looks.
    if (a == "-a")
        st.at++;
    else if (!(a == "(" || a == "!" || (a.size() > 1 && a[0] == '-' && !(a == "-o"))))
        return left;

    FindNode n = node_of(FindOp::And);
    n.l        = left;
    n.r        = TRY(parse_and(st));
    return mk(st, n);
}

Result<u32> parse_or(Finder &st)
{
    u32 left = TRY(parse_and(st));
    if (!(peek(st) == "-o"))
        return left;
    st.at++;
    FindNode n = node_of(FindOp::Or);
    n.l        = left;
    n.r        = TRY(parse_or(st));
    return mk(st, n);
}

// ---------------------------------------------------------------- evaluation

// Pure, and deliberately not a coroutine: -print records that it was reached
// and the walk writes once, so the tree costs no suspension point and no frame.
bool eval(const Finder &st, u32 at, const FindCand &c, bool &printed)
{
    const FindNode &n = st.nodes[at];
    switch (n.op) {
    case FindOp::True:
        return true;
    case FindOp::And:
        return eval(st, n.l, c, printed) && eval(st, n.r, c, printed);
    case FindOp::Or:
        return eval(st, n.l, c, printed) || eval(st, n.r, c, printed);
    case FindOp::Not:
        return !eval(st, n.l, c, printed);
    case FindOp::Print:
        printed = true;
        return true;
    case FindOp::Name:
        return glob_match(n.pat, {}, c.name);
    case FindOp::Type:
        return c.kind == n.kind;
    case FindOp::Newer:
        return c.mtime > n.mtime;
    }
    return false;
}

// What a failed write exits with: ^C stays 130, as it does everywhere else.
i32 write_bad(Error e)
{
    return e == Error::Cancelled ? 130 : 1;
}

Task<i32> report(Finder &st, Str path, FindCand c)
{
    bool printed = false;
    bool matched = eval(st, st.root, c, printed);
    // A live -print prints; with no -print anywhere, whatever matched does.
    // `-print -print` therefore prints once, which is the one place this
    // parts company with v7.
    if (!printed && !(matched && !st.action))
        co_return 0;

    st.row.clear();
    if (!st.row.append(path) || !st.row.push('\n'))
        co_return 1;
    if (Result<void> w = co_await File::stdout().write(st.row.str()); w.is_err())
        co_return write_bad(w.error());
    co_return 0;
}

// -------------------------------------------------------------------- the walk

Task<i32> walk_one(Finder &st, Str root)
{
    // The operand itself, which the walk does not report. Not followed: a link
    // named on the command line is the link, as POSIX find is without -H or -L.
    Result<FileInfo> s = Err(Error::NoMemory);
    if (Task<Result<FileInfo>> t = stat_of(root, false))
        s = co_await t;
    if (s.is_err()) {
        if (s.error() == Error::Cancelled)
            co_return 130;
        if (Task<void> x = errln("find", root, s.error()))
            co_await x;
        co_return 1;
    }

    Task<i32> first =
        report(st, root, FindCand{ path_basename(root), s.value().kind, s.value().mtime });
    if (!first)
        co_return 1;
    if (i32 bad = co_await first)
        co_return bad;

    if (s.value().kind != SYS_KIND_DIR)
        co_return 0;

    i32 status = 0;
    TreeWalk walk(root);
    for (;;) {
        Result<bool> more = Err(Error::NoMemory);
        if (Task<Result<bool>> t = walk.next(st.path, st.ent))
            more = co_await t;
        if (more.is_err()) {
            if (more.error() == Error::Cancelled)
                co_return 130;
            // A directory that will not list is reported and stepped over; the
            // walk has already dropped it.
            if (Task<void> x = errln("find", walk.at().empty() ? root : walk.at(), more.error()))
                co_await x;
            if (more.error() == Error::NoMemory)
                co_return 1;
            status = 1;
            continue;
        }
        if (!more.value())
            break;

        Task<i32> one =
            report(st, st.path.str(), FindCand{ st.ent.name.str(), st.ent.kind, st.ent.mtime });
        if (!one)
            co_return 1;
        if (i32 bad = co_await one)
            co_return bad;
    }
    co_return status;
}

// Every -newer's file, once the expression has parsed.
Task<Result<void>> fill_newer(Finder &st)
{
    for (FindNode &n : st.nodes) {
        if (n.op != FindOp::Newer)
            continue;
        Result<FileInfo> s = Err(Error::NoMemory);
        if (Task<Result<FileInfo>> t = stat_of(n.pat))
            s = co_await t;
        if (s.is_err()) {
            if (Task<void> x = errln("find", n.pat, s.error()))
                co_await x;
            co_return Err(s.error());
        }
        n.mtime = s.value().mtime;
    }
    co_return {};
}

Task<i32> complain(const Finder &st)
{
    Buf<128> b;
    b.put("find: ").put(st.why);
    if (!st.word.empty())
        b.put(": ").put(st.word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (args.size() == 1 || help_asked(args))
        co_return co_await usage_asked(USAGE);

    Finder *st = heap_new<Finder>();
    if (!st) {
        co_await write_all(SYS_STDERR, "find: out of memory\n");
        co_return 1;
    }
    struct Free {
        ~Free() { heap_delete(p); }
        Finder *p;
    } free_finder{ st };

    // v7's split: the paths run until the first word that could begin an
    // expression, and there must be one path.
    usize paths = 1;
    while (paths < args.size()) {
        Str w = args[paths];
        if (w == "(" || w == "!" || (w.size() > 1 && w[0] == '-'))
            break;
        paths++;
    }
    if (paths == 1)
        co_return co_await usage_error(USAGE);

    st->args = args;
    st->at   = paths;
    if (paths < args.size()) {
        Result<u32> root = parse_or(*st);
        if (root.is_err()) {
            if (root.error() == Error::NoMemory)
                co_return 1;
            co_return co_await complain(*st);
        }
        st->root = root.value();
        if (st->at < args.size()) {
            st->why  = "expected an operator";
            st->word = args[st->at];
            co_return co_await complain(*st);
        }
    } else {
        // No expression at all: everything matches, and everything prints.
        if (!st->nodes.push(FindNode{}))
            co_return 1;
    }

    if (Task<Result<void>> t = fill_newer(*st)) {
        Result<void> r = co_await t;
        if (r.is_err())
            co_return r.error() == Error::Cancelled ? 130 : 1;
    } else {
        co_return 1;
    }

    File &out  = File::stdout();
    i32 status = 0;
    for (usize i = 1; i < paths; i++) {
        Task<i32> t = walk_one(*st, args[i]);
        if (!t)
            co_return 1;
        if (i32 bad = co_await t) {
            if (bad == 130)
                co_return 130;
            status = 1;
        }
    }

    if (Result<void> w = co_await out.flush(); w.is_err())
        co_return write_bad(w.error());
    co_return status;
}
