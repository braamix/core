#include "kernel/fmt.h"
#include "proc/io.h"
#include "proc/opt.h"
#include "proc/size.h"
#include "proc/usage.h"

// Two byte streams, compared a chunk at a time: one loop over three ways of
// filling a buffer — a descriptor, stdin, a symbolic link's target.

namespace {

constexpr Str USAGE =
    "Usage:\n"
    "    cmp [-bhlsxz] [-i <skip>[:<skip>]] [-n <count>]\n"
    "        <file1> <file2> [<skip1> [<skip2>]]\n"
    "Options:\n"
    "    -b    show the two bytes that differ\n"
    "    -h    compare a symbolic link, not what it points at\n"
    "    -i    skip this many bytes, both or one for each\n"
    "    -l    list every difference, not only the first\n"
    "    -n    compare no more than this many bytes\n"
    "    -s    print nothing; the status is the answer\n"
    "    -x    list them in hex, counting from zero\n"
    "    -z    files of different sizes differ, unread\n"
    "A count is bytes; K M G T are 1024, KB MB GB TB 1000.\n";

// cmp(1)'s third status, which is usage_error's number as well.
constexpr i32 CMP_TROUBLE = 2;

// How much -l and -x gather before a write.
constexpr usize CMP_ROWS = 4096;

struct Flags {
    bool bytes = false; // -b
    bool link  = false; // -h
    bool list  = false; // -l, and -x
    bool quiet = false; // -s
    bool hex   = false; // -x
    bool size  = false; // -z
};

// One side. A literal side is a link's target, already whole; every other is
// refilled from its descriptor.
struct Side {
    Str name;
    String buf;
    u32 fd     = 0;
    usize at   = 0;
    bool lit   = false;
    bool ended = false;
};

Task<i32> complain(Str why, Str word)
{
    Buf<128> b;
    b.put("cmp: ").put(why);
    if (!word.empty())
        b.put(": ").put(word);
    b.put('\n');
    co_await write_all(SYS_STDERR, b.str());
    co_return co_await usage_error(USAGE);
}

// printf's %3o.
void put_oct3(Buf<64> &b, u32 v)
{
    char t[3];
    usize k = 0;
    do {
        t[k++] = char('0' + (v & 7));
        v >>= 3;
    } while (v);
    for (usize i = k; i < 3; i++)
        b.put(' ');
    while (k)
        b.put(t[--k]);
}

// printf's %0*x. Buf::put_hex carries an 0x and is 32 bits.
void put_hexw(Buf<64> &b, u64 v, usize w)
{
    for (usize i = w; i-- > 0;)
        b.put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
}

// The next bytes, or `ended`. An empty chunk is not an end of input.
Task<Result<void>> refill(Side &s)
{
    if (s.at < s.buf.size() || s.ended)
        co_return {};
    if (s.lit) {
        s.ended = true;
        co_return {};
    }
    for (;;) {
        Result<String> r = co_await read_chunk(s.fd);
        if (r.is_err()) {
            if (r.error() == Error::Closed) {
                s.ended = true;
                co_return {};
            }
            co_return Err(r.error());
        }
        if (!r.value().empty()) {
            s.buf = move(r.value());
            s.at  = 0;
            co_return {};
        }
    }
}

// `n` bytes off the front: a seek where the descriptor has one, reads where
// it has not.
Task<Result<void>> discard(Side &s, u64 n)
{
    if (n == 0)
        co_return {};
    if (!s.lit) {
        Result<u64> at = co_await seek_fd(s.fd, i64(n), SYS_SEEK_SET);
        if (at.is_ok())
            co_return {};
        if (at.error() != Error::Unsupported)
            co_return Err(at.error());
    }
    while (n) {
        if (Result<void> r = co_await refill(s); r.is_err())
            co_return r;
        if (s.ended)
            co_return {};
        usize have = s.buf.size() - s.at;
        usize take = usize(n < have ? n : u64(have));
        s.at += take;
        n -= take;
    }
    co_return {};
}

// "a b differ: char 5, line 1", with -b the two bytes after it.
Task<void> differ(const Flags &f, const Side &a, const Side &b, u64 byte, u64 line, u32 c1, u32 c2)
{
    if (f.quiet)
        co_return;
    Buf<128> t;
    t.put(a.name).put(' ').put(b.name).put(" differ: char ").put(byte).put(", line ").put(line);
    if (f.bytes) {
        Buf<64> v;
        put_oct3(v, c1);
        v.put(' ').put(char(c1)).put(' ');
        put_oct3(v, c2);
        v.put(' ').put(char(c2));
        t.put(" is ").put(v.str());
    }
    t.put('\n');
    co_await write_all(SYS_STDOUT, t.str());
}

Task<void> eofmsg(const Flags &f, Str name)
{
    if (f.quiet)
        co_return;
    Buf<128> t;
    t.put("cmp: EOF on ").put(name).put('\n');
    co_await write_all(SYS_STDERR, t.str());
}

// 0 the same, 1 different, 2 trouble.
Task<i32> compare(const Flags &f, Side &a, Side &b, u64 limit)
{
    u64 byte = 1, line = 1;
    bool found = false;
    String rows;
    bool oom = false;

    for (;;) {
        if (limit && byte > limit)
            break;
        if (Result<void> r = co_await refill(a); r.is_err())
            co_return r.error() == Error::Cancelled ? 130 : CMP_TROUBLE;
        if (Result<void> r = co_await refill(b); r.is_err())
            co_return r.error() == Error::Cancelled ? 130 : CMP_TROUBLE;
        if (a.ended || b.ended)
            break;

        // The overlap both sides have buffered, walked without a coroutine.
        usize n = a.buf.size() - a.at;
        if (usize m = b.buf.size() - b.at; m < n)
            n = m;
        if (limit && limit - byte + 1 < u64(n))
            n = usize(limit - byte + 1);

        const char *p = a.buf.data() + a.at;
        const char *q = b.buf.data() + b.at;
        usize i       = 0;
        for (; i < n; i++) {
            u32 c1 = u8(p[i]), c2 = u8(q[i]);
            if (c1 != c2) {
                if (!f.list) {
                    a.at += i;
                    b.at += i;
                    co_await differ(f, a, b, byte + i, line, c1, c2);
                    co_return 1;
                }
                found = true;
                Buf<64> row;
                if (f.hex) {
                    put_hexw(row, byte + i - 1, 8);
                    row.put(' ');
                    put_hexw(row, c1, 2);
                    row.put(' ');
                    put_hexw(row, c2, 2);
                } else {
                    row.put_right(byte + i, 6).put(' ');
                    put_oct3(row, c1);
                    if (f.bytes)
                        row.put(' ').put(char(c1));
                    row.put(' ');
                    put_oct3(row, c2);
                    if (f.bytes)
                        row.put(' ').put(char(c2));
                }
                row.put('\n');
                oom = !rows.append(row.str());
                if (oom)
                    break;
            }
            if (c1 == '\n')
                line++;
        }
        a.at += i;
        b.at += i;
        byte += i;
        if (oom)
            break;
        if (rows.size() >= CMP_ROWS) {
            if (Result<void> w = co_await write_all(SYS_STDOUT, rows.str()); w.is_err())
                co_return w.error() == Error::Cancelled ? 130 : CMP_TROUBLE;
            rows.clear();
        }
    }

    if (oom) {
        co_await write_all(SYS_STDERR, "cmp: out of memory\n");
        co_return CMP_TROUBLE;
    }
    if (!rows.empty()) {
        if (Result<void> w = co_await write_all(SYS_STDOUT, rows.str()); w.is_err())
            co_return w.error() == Error::Cancelled ? 130 : CMP_TROUBLE;
    }

    // One side ran out first. Under a limit neither did.
    if (a.ended != b.ended) {
        co_await eofmsg(f, a.ended ? a.name : b.name);
        co_return 1;
    }
    co_return found ? 1 : 0;
}

// A count operand: parse_size's grammar without its modifiers.
Result<u64> count_of(Str s)
{
    Result<SizeSpec> r = parse_size(s);
    if (r.is_err())
        return Err(r.error());
    if (r.value().mod != SizeMod::Set)
        return Err(Error::Invalid);
    return r.value().n;
}

} // namespace

Task<i32> proc_main(Args args)
{
    if (help_asked(args))
        co_return co_await usage_asked(USAGE);

    Flags f;
    u64 skip1 = 0, skip2 = 0, limit = 0;
    OptParse p(args, Opts{ "bhlsxz", "in" });
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
        case 'b':
            f.bytes = true;
            break;
        case 'h':
            f.link = true;
            break;
        case 'l':
            f.list = true;
            break;
        case 's':
            f.quiet = true;
            break;
        case 'x':
            f.list = f.hex = true;
            break;
        case 'z':
            f.size = true;
            break;
        case 'i': {
            Str rest;
            Str head        = o.value.split(':', rest);
            Result<u64> one = count_of(head);
            if (one.is_err()) {
                if (Task<i32> t = complain("not a count", head))
                    co_return co_await t;
                co_return 1;
            }
            skip1 = skip2 = one.value();
            if (head.size() != o.value.size()) {
                Result<u64> two = count_of(rest);
                if (two.is_err()) {
                    if (Task<i32> t = complain("not a count", rest))
                        co_return co_await t;
                    co_return 1;
                }
                skip2 = two.value();
            }
            break;
        }
        default: { // 'n'
            Result<u64> n = count_of(o.value);
            if (n.is_err()) {
                if (Task<i32> t = complain("not a count", o.value))
                    co_return co_await t;
                co_return 1;
            }
            limit = n.value();
            break;
        }
        }
    }

    if (f.list && f.quiet) {
        if (Task<i32> t = complain("-s goes with neither -l nor -x", {}))
            co_return co_await t;
        co_return 1;
    }

    Args rest = p.rest();
    if (rest.size() < 2 || rest.size() > 4) {
        if (Task<i32> t = complain("two files are needed", {}))
            co_return co_await t;
        co_return 1;
    }
    for (usize i = 2; i < rest.size(); i++) {
        Result<u64> n = count_of(rest[i]);
        if (n.is_err()) {
            if (Task<i32> t = complain("not a count", rest[i]))
                co_return co_await t;
            co_return 1;
        }
        (i == 2 ? skip1 : skip2) = n.value();
    }

    // A silent run that skips nothing is answered by the sizes alone.
    if (f.quiet && skip1 == 0 && skip2 == 0)
        f.size = true;

    Side a, b;
    a.name = rest[0];
    b.name = rest[1];

    // -h compares the links themselves, which is their targets as text. Both
    // must be links; the diagnostic names the one that is not.
    if (f.link) {
        u32 kind[2] = { SYS_KIND_FILE, SYS_KIND_FILE };
        for (usize i = 0; i < 2; i++) {
            Result<FileInfo> st = co_await stat_of(rest[i], false);
            if (st.is_err()) {
                if (!f.quiet)
                    co_await errln("cmp", rest[i], st.error());
                co_return CMP_TROUBLE;
            }
            kind[i] = st.value().kind;
        }
        if (kind[0] == SYS_KIND_LINK || kind[1] == SYS_KIND_LINK) {
            usize bad = kind[0] == SYS_KIND_LINK ? 1 : 0;
            if (kind[bad] != SYS_KIND_LINK) {
                if (!f.quiet) {
                    Buf<128> t;
                    t.put("cmp: ").put(rest[bad]).put(": not a symbolic link\n");
                    co_await write_all(SYS_STDERR, t.str());
                }
                co_return CMP_TROUBLE;
            }
            Side *both[2] = { &a, &b };
            for (usize i = 0; i < 2; i++) {
                Result<String> to = co_await read_link(rest[i]);
                if (to.is_err()) {
                    if (!f.quiet)
                        co_await errln("cmp", rest[i], to.error());
                    co_return CMP_TROUBLE;
                }
                both[i]->buf = move(to.value());
                both[i]->lit = true;
            }
        }
    }

    // What -h did not make a literal is opened. "-" is stdin, once.
    bool stdin_taken = false;
    i32 opened[2]    = { -1, -1 };
    i32 status       = -1;
    Side *side[2]    = { &a, &b };
    for (usize i = 0; i < 2 && status < 0; i++) {
        if (side[i]->lit)
            continue;
        if (rest[i] == "-") {
            if (stdin_taken) {
                if (!f.quiet)
                    co_await write_all(SYS_STDERR, "cmp: - names the input twice\n");
                status = CMP_TROUBLE;
                break;
            }
            stdin_taken   = true;
            side[i]->fd   = SYS_STDIN;
            side[i]->name = "stdin";
            continue;
        }
        Result<i32> fd = co_await open_read(rest[i]);
        if (fd.is_err()) {
            if (!f.quiet)
                co_await errln("cmp", rest[i], fd.error());
            status = fd.error() == Error::Cancelled ? 130 : CMP_TROUBLE;
            break;
        }
        opened[i]   = fd.value();
        side[i]->fd = u32(fd.value());
    }

    // -z answers from the sizes; a stream without them has no shortcut.
    if (status < 0 && f.size && !a.lit && !b.lit) {
        Result<FileInfo> s1 = co_await stat_fd(a.fd);
        Result<FileInfo> s2 = co_await stat_fd(b.fd);
        if (s1.is_ok() && s2.is_ok() && s1.value().size != s2.value().size) {
            if (!f.quiet) {
                Buf<128> t;
                t.put(a.name).put(' ').put(b.name).put(" differ: size\n");
                co_await write_all(SYS_STDOUT, t.str());
            }
            status = 1;
        }
    }

    if (status < 0) {
        Result<void> r = co_await discard(a, skip1);
        if (r.is_ok())
            r = co_await discard(b, skip2);
        if (r.is_err())
            status = r.error() == Error::Cancelled ? 130 : CMP_TROUBLE;
    }

    if (status < 0)
        status = co_await compare(f, a, b, limit);

    for (usize i = 0; i < 2; i++)
        if (opened[i] >= 0)
            co_await close_fd(u32(opened[i]));
    co_return status;
}
